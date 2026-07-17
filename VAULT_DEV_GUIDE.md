# Autonomous Treasury Vault — Dev Guide

## Objective

Replace all virtual treasury accounting counters with real UTXOs owned by a deterministic vault keypair. The vault executes exactly four predetermined operations — nothing else. Every node enforces this via consensus.

## Architecture

```
                           Swap fees (real XFG UTXOs from SwapDaemon)
                           HEAT burns (real XFG UTXOs burned)
                                     │
                                     ▼
              ┌──────────────────────────────────────────┐
              │            TREASURY VAULT                 │
              │  (one keypair, deterministic from genesis) │
              │                                          │
              │  ┌────────────────────────────────────┐  │
              │  │ CD_FEE_POOL    │ XFG + HEAT UTXOs  │  │  → pays CD APY
              │  ├────────────────┼───────────────────┤  │
              │  │ LP_RESERVE     │ XFG + HEAT UTXOs  │  │  → deploys to Hearth
              │  ├────────────────┼───────────────────┤  │
              │  │ GENERAL_RESERVE│ XFG + HEAT UTXOs  │  │  → bootstrap repay, transfers
              │  ├────────────────┼───────────────────┤  │
              │  │ SWF            │ HEAT UTXOs        │  │  → sovereign wealth fund
              │  └────────────────────────────────────┘  │
              │                                          │
              │  Consensus gate: 4 ops only              │
              └──────────────────────────────────────────┘
```

**One keypair, one set of UTXOs, four partitions.** Partitions tracked by purpose metadata in transaction outputs — not separate wallets or addresses.

## Vault Key Derivation

```
spendKey = H_scalar(genesisHash || "xfgo_treasury_vault_v1")
viewKey  = spendKey * G
```

Anyone can compute both keys. The security comes from consensus validation, not key secrecy. A compromised node signing an unauthorized vault transaction has its block rejected by every honest node.

## Permitted Operations

| # | Operation | Source Partition | Destination | Validation |
|---|-----------|-----------------|-------------|------------|
| 1 | CD APY payout | CD_FEE_POOL | CD holder (any address) | `claimedInterest ≤ vault.partitionBalance(CD_FEE_POOL)` |
| 2 | LP deploy/rebalance | LP_RESERVE | Hearth AMM | Has `TX_EXTRA_AMM_ADD_LIQ` or `TX_EXTRA_AMM_REM_LIQ` |
| 3 | HEAT mint from XFG burn | GENERAL_RESERVE (XFG burned) | CD_FEE_POOL (HEAT minted) | Has `TX_EXTRA_HEAT_MINT_AUTH`, validated by `HeatMintEngine` |
| 4 | Partition transfer | Any → Any | Same vault (reallocation) | Both source and dest are vault partitions |

Any other vault UTXO spend is **rejected by every node**.

## Implementation Phases

### Phase 1: Core Types

**Create:**

| File | Purpose |
|------|---------|
| `src/Treasury/VaultTypes.h` | `VaultPartition` enum, `VaultOutput` struct, partition helpers |
| `src/Treasury/VaultKeys.h` | `deriveVaultKeys(genesisHash)` → `{viewKey, spendKey, spendPub}` |

**VaultPartition enum:**
```cpp
enum class VaultPartition : uint8_t {
    CD_FEE_POOL     = 0,
    LP_RESERVE      = 1,
    GENERAL_RESERVE = 2,
    SWF             = 3,
};
```

**VaultOutput struct:**
```cpp
struct VaultOutput {
    uint64_t       globalOutputIndex;
    uint64_t       amount;
    AssetType      asset;       // XFG or HEAT
    VaultPartition partition;
    Crypto::Hash   txHash;
    bool           spent;
};
```

### Phase 2: Vault UTXO Set

**Create:**

| File | Purpose |
|------|---------|
| `src/Treasury/VaultUtxoSet.h` | UTXO tracking by partition, balance queries, spend marking, serialization |
| `src/Treasury/VaultUtxoSet.cpp` | Implementation |

**Key methods:**
```cpp
class VaultUtxoSet {
    void   addUtxo(uint64_t globalIndex, uint64_t amount, AssetType asset, VaultPartition partition, const Crypto::Hash& txHash);
    void   markSpent(uint64_t globalIndex);
    uint64_t partitionBalance(VaultPartition partition, AssetType asset) const;
    std::vector<VaultOutput> selectUtxos(VaultPartition partition, AssetType asset, uint64_t needed);
    bool   isVaultOutput(uint64_t globalIndex) const;
    VaultPartition getPartition(uint64_t globalIndex) const;
};
```

Uses `parallel_flat_hash_map<uint64_t, VaultOutput>` — same pattern as existing `m_lpCommitmentShares` at `Blockchain.h:441`.

### Phase 3: Vault Policy (Consensus Gate)

**Create:**

| File | Purpose |
|------|---------|
| `src/Treasury/VaultPolicy.h` | `classifySpend(tx)` → `VaultPartition`, `isPermitted(tx, partition)` → `bool` |
| `src/Treasury/VaultPolicy.cpp` | Implementation |

```cpp
class VaultPolicy {
    static bool isVaultKeyImage(const Crypto::KeyImage& ki);
    static VaultPartition classifySpend(const Transaction& tx);
    static bool isPermitted(const Transaction& tx, VaultPartition source);
};
```

**Permission rules (hard-coded, no config):**
- `CD_FEE_POOL` → only if output goes to a CD holder claiming interest (checked via commitment spend validation)
- `LP_RESERVE` → only if tx has `TX_EXTRA_AMM_ADD_LIQ` or `TX_EXTRA_AMM_REM_LIQ`
- `GENERAL_RESERVE` → only if tx has `TX_EXTRA_HEAT_MINT_AUTH` (XFG burned, HEAT minted to vault) or is a partition transfer
- `SWF` → reserved for future cross-chain liquidity operations; currently locked

### Phase 4: Epoch Boundary — Replace Counters with UTXOs

**Modify:** `src/CryptoNoteCore/Blockchain.cpp` epoch boundary (~line 4175-4340)

All counter arithmetic replaced with vault UTXO creation:

```cpp
// OLD (counter):
m_treasuryHeatReserve += treasuryHeat;
m_treasuryBalance += treasuryFeeShare;

// NEW (UTXO):
m_vaultUtxos.addUtxo(globalIndex, treasuryHeat, AssetType::HEAT, VaultPartition::GENERAL_RESERVE, txHash);
m_vaultUtxos.addUtxo(globalIndex, treasuryFeeShare, AssetType::XFG, VaultPartition::GENERAL_RESERVE, txHash);
```

Each treasury allocation creates a real `TransactionOutputUnified` with `key = vaultViewKey-derived output key`. The SwapDaemon sends swap fees as a real transaction to the vault address at epoch boundary (RPC-based, not counter increment).

### Phase 5: CD Redemption — Spend Vault UTXOs

**Files:** `VaultUtxoSet.h/.cpp`, `Blockchain.cpp`, `Blockchain.h`

When a CD is claimed with `claimedInterest > 0`, the daemon spends vault CD_FEE_POOL HEAT UTXOs to back the interest payout:

```cpp
// Blockchain.cpp — connect block, CD redemption
if (cin.claimedInterest > 0) {
    uint64_t vaultAvailable = m_vault.partitionBalance(VaultPartition::CD_FEE_POOL, AssetType::HEAT);
    uint64_t effectiveCap = std::min(m_feePoolBalance, vaultAvailable);
    if (cin.claimedInterest <= effectiveCap) {
        auto spendResult = m_vault.spendUtxos(VaultPartition::CD_FEE_POOL, AssetType::HEAT, cin.claimedInterest);
        if (spendResult.amountSpent == cin.claimedInterest) {
            m_vaultSpentByTx[transactionHash] = std::move(spendResult.spentIndices);
        }
        m_feePoolBalance -= cin.claimedInterest;
        m_totalCdInterestPaid += cin.claimedInterest;
    }
}
```

**Reversal (popBlock):**
```cpp
auto vaultIt = m_vaultSpentByTx.find(transactionHash);
if (vaultIt != m_vaultSpentByTx.end()) {
    m_vault.unSpendUtxos(vaultIt->second);
    m_vaultSpentByTx.erase(vaultIt);
}
```

**Validation:** Aggregate fee-pool cap also checks vault balance:
```cpp
uint64_t vaultAvailable = m_vault.partitionBalance(VaultPartition::CD_FEE_POOL, AssetType::HEAT);
if (txClaimedInterest > vaultAvailable) { reject; }
```

The `getTransactionInputAmount()` hack in `Currency.cpp:435-443` (`amount + claimedInterest`) is removed. CD payout is a real transaction spending vault UTXOs to the CD holder.

### Phase 6: CD Yield Floor — Partition Transfer

**Modify:** `src/CryptoNoteCore/Blockchain.cpp` yield floor (~line 4197-4222)

The 2% APY floor injection transfers HEAT from `GENERAL_RESERVE` to `CD_FEE_POOL`:

```cpp
// OLD:
m_treasuryHeatReserve -= heatInjection;
m_heatCdFeePool += heatInjection;

// NEW: re-tag vault UTXOs from GENERAL_RESERVE to CD_FEE_POOL
vault.transferPartition(VaultPartition::GENERAL_RESERVE, VaultPartition::CD_FEE_POOL, heatInjection);
```

### Phase 7: Treasury LP — Vault-Owned LP Shares

**Modify:** `src/CryptoNoteCore/Blockchain.cpp` LP add/remove/claim (~line 4471-4494, 4276-4283)

Protocol LP shares are real `DEPOSIT_TERM_LP` outputs owned by the vault. LP fees accrue as vault UTXOs. LP add/remove spends LP_RESERVE partition UTXOs.

### Phase 8: PopBlock Reversal

**Modify:** `src/CryptoNoteCore/Blockchain.cpp` popBlock (~line 4550-4640)

UTXO-based vault state uses epoch snapshots for reversal. Each block push records a `VaultSnapshot` of the UTXO set. `popBlock()` restores the previous snapshot.

### Phase 9: Serialization

**Modify:** `src/CryptoNoteCore/Blockchain.cpp` save/load (~line 200-230)

`VaultUtxoSet` serializes to/from the blockchain storage. All existing treasury counters are removed from serialization — replaced by `m_vaultUtxos.serialize(s)`.

### Phase 10: RPC

**Modify:** `src/Rpc/RpcServer.cpp` treasury/AMM endpoints

`/get_treasury_info` returns real UTXO-backed balances per partition. `/get_fee_pool_info` reads from `vault.partitionBalance(CD_FEE_POOL)`.

## Constants (add to `CryptoNoteConfig.h`)

```cpp
const char VAULT_KEY_SEED[] = "xfgo_treasury_vault_v1";
const uint64_t VAULT_EPOCH_SNAPSHOT_DEPTH = 100;
```

## Files

| File | Action | Phase |
|------|--------|-------|
| `src/Treasury/VaultTypes.h` | Create | 1 |
| `src/Treasury/VaultKeys.h` | Create | 1 |
| `src/Treasury/VaultUtxoSet.h` | Create | 2 |
| `src/Treasury/VaultUtxoSet.cpp` | Create | 2 |
| `src/Treasury/VaultPolicy.h` | Create | 3 |
| `src/Treasury/VaultPolicy.cpp` | Create | 3 |
| `src/CryptoNoteCore/Blockchain.h` | Modify | 4 |
| `src/CryptoNoteCore/Blockchain.cpp` | Modify | 4-8 |
| `src/CryptoNoteCore/Currency.cpp` | Modify | 5 |
| `src/CryptoNoteCore/BlockchainSerialization.cpp` | Modify | 9 |
| `src/CryptoNoteConfig.h` | Modify | All |
| `src/CMakeLists.txt` | Modify | 1-3 |
| `src/Rpc/RpcServer.cpp` | Modify | 10 |

## Verification

```
Test: unauthorized vault spend
  Construct tx spending a CD_FEE_POOL UTXO to random address (not a CD claim).
  → REJECTED. VaultPolicy::isPermitted() → false.

Test: CD_FEE_POOL overdraft
  Construct CD claim for more interest than CD_FEE_POOL holds.
  → REJECTED. claimedInterest > vault.partitionBalance(CD_FEE_POOL, HEAT).

Test: LP_RESERVE used for non-LP spend
  Construct tx spending LP_RESERVE UTXO without AMM tags.
  → REJECTED. No TX_EXTRA_AMM_ADD_LIQ or TX_EXTRA_AMM_REM_LIQ.

Test: popBlock restores vault state
  Push 10 blocks with epoch boundaries, pop 5.
  → vault.partitionBalance() matches state at block 5.

Test: vault key is derivable by anyone
  Given genesis hash, compute vault keys.
  → Same keys every time on every architecture.

Test: honest nodes reject compromised node's theft block
  Modify node to skip vault gate, mine theft block.
  → Honest nodes reject. pushTransaction() validation runs on all nodes.
```

## Future Work

### DIGM Redemption for HEAT

SWF counter HEAT (`m_swfHeatBalance`) is counter-only — never UTXOs. When DIGM holders redeem for HEAT:

1. Burn DIGM tokens via burn-to-mint gate
2. Mint new HEAT at pool rate
3. Decrement `m_swfHeatBalance` by the redeemed amount
4. Send minted HEAT to the redeemer

This avoids creating SWF vault UTXOs while still backing DIGM with SWF reserves. The counter acts as an internal accounting offset — DIGM redemption mints fresh HEAT and reduces the SWF obligation accordingly.
