# Unified Commitment Output Plan — Corrected

## Context

The original `docs/developer/unified-commitment-plan.md` was audited by Guardian and found to contain **4 critical errors** and **3 high-severity warnings**. This rewrite incorporates all corrections and recommendations. The goal is unchanged: collapse 4 output types, 3 input types, 2 ring pools, and 2 wallet scan paths into one of each.

### Critical Corrections Applied

| # | Original Error | Correction |
|---|---|---|
| F-001 | Plan targets `TransactionOutputCommitment` as unified type | **Target `TransactionOutputUnified`** — it already exists at `include/CryptoNote.h:96`, has `key`, `term`, `commitment`, `proof`, wire tag `0x5` |
| F-002 | Phase 4 removes `TransactionInputUnified` | **Keep `TransactionInputUnified`** — it IS the MLSAG input (`pseudoCommitment` + `sigC0`). Remove `TransactionInputCommitmentSpend` and `KeyInput` instead |
| F-003 | Phase 2 "repurposes" `amountProof` (fixed 256 bytes) for BP+ (~672 bytes variable) | **Add `std::vector<uint8_t> rangeProof` field** to `TransactionOutputUnified`. `MembershipProof` stays for CD tier proofs only |
| F-004 | Plan says "CommitmentIndex::add() handles all terms" and "m_allCommitmentOutputs" exists | **`CommitmentIndex` is a Merkle banking tracker, not the ring pool.** Ring pool is `m_commitmentOutputs` in `Blockchain` (per-amount map). `m_allCommitmentOutputs` must be created new |

### Additional Fixes

| # | Issue | Fix |
|---|---|---|
| W-001 | MLSAG presented as Phase 2/3 work to implement | `generate_mlsag`/`check_mlsag` exist in `src/crypto/mlsag.cpp`. Gap is wiring `check_mlsag` into `Blockchain::checkTransactionInputs()` |
| W-002 | `classifyOutputAsset` returns XFG unconditionally for `TransactionOutputUnified` | Add term discrimination mirroring the `TransactionOutputCommitment` branch |
| W-003 | Dual-write doubles supply calculation | Gate unified output amounts out of fee/supply sums during Phase 1 |

---

## Target Architecture (unchanged goal, corrected types)

### Outputs — `TransactionOutputUnified` (wire tag `0x5`)

```cpp
struct TransactionOutputUnified {
  Crypto::PublicKey key;                    // stealth address or commitKey
  uint32_t term;                            // 0=XFG, FOREVER=HEAT, finite=CD, magic=pools
  Crypto::EllipticCurvePoint commitment;    // C = amount*H + mask*G
  Crypto::MembershipProof proof;            // 1-of-N tier proof (CDs only)
  std::vector<uint8_t> rangeProof;          // BP+ range proof (Phase 2+)
};
```

### Inputs — `TransactionInputUnified` (wire tag `0x5`)

```cpp
struct TransactionInputUnified {
  std::vector<uint32_t> outputIndexes;              // global ring (relative offsets)
  Crypto::KeyImage keyImage;                         // double-spend prevention
  Crypto::EllipticCurvePoint pseudoCommitment;       // C_pseudo for MLSAG balance
  Crypto::EllipticCurveScalar sigC0;                 // MLSAG initial challenge
};
```

### Decoy Pool — flat global vector (new)

```cpp
// In Blockchain.h — ADD alongside existing m_commitmentOutputs
std::vector<CommitmentOutputRef> m_allUnifiedOutputs;  // flat global index, no amount key
```

---

## Phase 0: Constant Cleanup (no fork)

**Goal:** Rename `DEPOSIT_TERM_FOREVER` -> `HEAT_TERM`, define `TERM_REGULAR = 0`, fix `classifyOutputAsset`.

### Changes

1. **`src/CryptoNoteConfig.h`**
   - `DEPOSIT_TERM_FOREVER` -> `HEAT_TERM` (also `TESTNET_DEPOSIT_TERM_FOREVER` -> `TESTNET_HEAT_TERM`)
   - Remove `DEPOSIT_TERM_BURN` alias (same value, unused independently)
   - Add `const uint32_t TERM_REGULAR = 0;`

2. **Rename across ~20 call sites** (not ~50 as original stated)
   - Primary: `Blockchain.cpp` (~11 sites), `Currency.cpp` (~3), `CryptoNoteFormatUtils.cpp` (~1), `TransactionExtra.cpp` (~1), `CryptoNoteConfig.h` (~4 including testnet)

3. **`src/CryptoNoteCore/Currency.cpp:449`** — Fix `classifyOutputAsset` for `TransactionOutputUnified`:
   ```cpp
   // BEFORE (wrong):
   if (target.type() == typeid(TransactionOutputUnified)) {
     return AssetType::XFG;  // unified outputs default to XFG
   }
   
   // AFTER (correct):
   if (target.type() == typeid(TransactionOutputUnified)) {
     auto& unified = boost::get<TransactionOutputUnified>(target);
     if (unified.term == parameters::HEAT_TERM)
       return AssetType::HEAT;
     if (unified.term == parameters::DEPOSIT_TERM_LP)
       return AssetType::LP;
     if (unified.term == parameters::DEPOSIT_TERM_POOL_XFG)
       return AssetType::XFG;
     if (unified.term == parameters::DEPOSIT_TERM_POOL_HEAT)
       return AssetType::HEAT;
     if (unified.term == parameters::DEPOSIT_TERM_SWAP_RECEIVE_XFG)
       return AssetType::XFG;
     return AssetType::XFG;  // term=0 regular or finite CD
   }
   ```

### Verification

- `grep -rn 'DEPOSIT_TERM_FOREVER\|DEPOSIT_TERM_BURN' src/` returns 0
- All build targets pass
- `classifyOutputAsset` unit test covers all term values for all output types

---

## Phase 1: Bridge — XFG Dual-Write with `TransactionOutputUnified` (no fork)

**Goal:** All new XFG sends emit BOTH a `KeyOutput` (legacy wallets) AND a `TransactionOutputUnified` with `term=0`. The unified output enters the ring pool. New wallets can scan and (in Phase 3) spend it.

### Changes

#### 1a. Add `addOutput(uint64_t, const TransactionOutputUnified&)` overload

**Files:** `include/ITransaction.h`, `src/CryptoNoteCore/Transaction.cpp`

Currently missing. Pattern follows existing `addOutput(uint64_t, const TransactionOutputCommitment&)` at `Transaction.cpp:361`:
```cpp
size_t TransactionImpl::addOutput(uint64_t amount, const TransactionOutputUnified& out) {
  checkIfSigning();
  TransactionOutput realOut = { amount, out };
  transaction.outputs.emplace_back(realOut);
  transaction.version = TRANSACTION_VERSION_2;
  invalidateHash();
  return transaction.outputs.size() - 1;
}
```

#### 1b. Wallet: emit paired unified output on XFG sends

**File:** `src/WalletLegacy/WalletTransactionSender.cpp` (around line 979 where `addOutput(amount, m_keys.address)` is called for KeyOutput)

After each `KeyOutput` send:
```cpp
// Legacy KeyOutput
transaction->addOutput(out.amount, KeyOutput{keys.pubKey});

// Paired unified output (term=0, commitment/proof zeroed until Phase 2)
auto depositSecret = deriveDepositSecret(derivation, outputIndex);  // same ECDH
auto commitKeys = deriveCommitmentKeys(depositSecret);
TransactionOutputUnified unified;
unified.key   = commitKeys.commitKey;
unified.term  = TERM_REGULAR;  // 0
// unified.commitment, unified.proof left zeroed
transaction->addOutput(0, unified);  // amount=0 on wire (paired with KeyOutput above)
```

**Critical: set wire amount to 0** on the unified output to prevent supply double-counting. The real amount is on the `KeyOutput`. Scanning wallets recover the amount via ECDH shared secret, not from `TransactionOutput.amount`.

#### 1c. Ring pool: index unified outputs in `m_commitmentOutputs`

**File:** `src/CryptoNoteCore/Blockchain.cpp`

In the block processing path where `TransactionOutputCommitment` outputs are added to `m_commitmentOutputs`, add a parallel branch for `TransactionOutputUnified` (tag `0x5`). Use the real amount from the paired `KeyOutput` for the per-amount map key.

#### 1d. Wallet scan: detect `TransactionOutputUnified` with `term=0`

**File:** `src/Transfers/TransfersConsumer.cpp:102-131`

Add branch after the existing `TransactionOutputCommitment` scan:
```cpp
} else if (outType == TransactionTypes::OutputType::Unified) {
  TransactionOutputUnified out;
  tx.getOutput(idx, out, amount);
  if (out.term == TERM_REGULAR) {
    // Re-derive depositSecret and commitKey (same path as commitment scan)
    auto depositSecret = deriveDepositSecret(derivation, idx);
    auto ck = deriveCommitmentKeys(depositSecret);
    if (ck.commitKey == out.key) {
      // Belongs to us — recover amount from paired KeyOutput
      outputs[spendKey].push_back(static_cast<uint32_t>(idx));
    }
  }
}
```

#### 1e. `getTransactionOutputAssetAmounts` — skip paired unified outputs

**File:** `src/CryptoNoteCore/Currency.cpp:455-468`

When summing output amounts, skip `TransactionOutputUnified` outputs with `term=0` and `amount=0` (they're paired with a KeyOutput that carries the real amount). This prevents double-counting.

### Verification

- XFG sends produce 2 outputs per recipient (KeyOutput + Unified)
- Unified output has amount=0 on wire
- Wallet scan recovers unified output and associates correct amount
- Ring pool includes term=0 unified outputs
- Supply/fee calculations are unchanged (no double-count)
- Legacy wallets ignore tag `0x5` outputs (unknown variant → skip)

---

## Phase 2: Pedersen Commitments + Range Proofs (hidden amounts)

**Goal:** Every `TransactionOutputUnified` carries a real Pedersen commitment and a BP+ range proof. Wire amount set to 0. One output per transfer (no denomination decomposition).

### Prerequisites

- `src/crypto/pedersen.h/.cpp` — exists, compiles, no callers. **Ready.**
- `deriveCommitmentKeys()` — already derives `amountMask`. **Ready.**
- `src/crypto/mlsag.h/.cpp` — `generate_mlsag`/`check_mlsag` implemented. **Ready.** Gap is Blockchain wiring only.
- **MUST PORT:** Bulletproofs+ from Monero `rct/bulletproofs_plus.cc` → `src/crypto/bulletproofs_plus.h/.cpp`
- **MUST PORT:** Multi-scalar multiplication from Monero `common/multiexp.cc` → `src/crypto/multiexp.h/.cpp`

### Changes

#### 2a. Add `rangeProof` field to `TransactionOutputUnified`

**File:** `include/CryptoNote.h:96-101`

```cpp
struct TransactionOutputUnified {
  Crypto::PublicKey key;
  uint32_t term;
  Crypto::EllipticCurvePoint commitment;
  Crypto::MembershipProof proof;            // CD tier proof (zeroed for non-CD)
  std::vector<uint8_t> rangeProof;          // NEW — BP+ range proof (empty for CD)
};
```

**File:** `src/CryptoNoteCore/CryptoNoteSerialization.cpp:379-384`

Update serializer to include `rangeProof`. Version-gate: only serialize if block >= v12. For v10/v11 blocks, the field is absent (empty vector on deserialize).

#### 2b. Wallet: generate Pedersen commitment + BP+ proof

**File:** Wallet send path (`WalletTransactionSender.cpp`, `WalletGreen.cpp`)

```cpp
auto commitment = pedersen_commit(out.amount, commitKeys.amountMask);
auto bp = bulletproofs_plus_generate({commitment}, {out.amount}, {commitKeys.amountMask});

TransactionOutputUnified unified;
unified.key        = commitKeys.commitKey;
unified.term       = TERM_REGULAR;
unified.commitment = commitment;
// unified.proof stays zeroed (no tier membership for regular XFG)
unified.rangeProof = bp.serialize();
// TransactionOutput.amount = 0 on wire
```

#### 2c. Wire MLSAG verification into `Blockchain::checkTransactionInputs()`

**File:** `src/CryptoNoteCore/Blockchain.cpp`

The main validation dispatcher at line 2119 handles `TransactionInputCommitmentSpend` but has **no handler for `TransactionInputUnified`**. Add:

```cpp
} else if (in.type() == typeid(TransactionInputUnified)) {
  const auto& txin = boost::get<TransactionInputUnified>(in);
  // Double-spend check
  if (!checkKeyImage(txin.keyImage)) return false;
  // Validate ring + MLSAG
  if (!checkUnifiedInput(txin, tx_prefix_hash, tx.signatures[inputIndex], pmax_used_block_height))
    return false;
}
```

New function `checkUnifiedInput()` follows the structure of `checkCommitmentSpendInput()` (line 2265) but:
- Collects both `commitKey` AND `amountCommitment` from ring members
- Calls `Crypto::check_mlsag()` instead of `Crypto::check_ring_signature()`
- Reads `pseudoCommitment` and `sigC0` from the input struct
- Reads `sig_s` from `tx.signatures[inputIndex]` (2 scalars per ring member, packed as `Signature` structs)

#### 2d. Balance proof verification

After all inputs/outputs processed:
```cpp
// sum(C_pseudo) - sum(C_output) - fee*H == identity point
EllipticCurvePoint balance = identity;
for (auto& in : tx.inputs)
  if (is TransactionInputUnified)
    balance += in.pseudoCommitment;
for (auto& out : tx.outputs)
  if (is TransactionOutputUnified)
    balance -= out.commitment;
balance -= fee * pedersen_H();
if (balance != identity) return false;
```

#### 2e. Global flat ring pool

**File:** `src/CryptoNoteCore/Blockchain.h`

Add alongside existing `m_commitmentOutputs`:
```cpp
std::vector<CommitmentOutputRef> m_allUnifiedOutputs;  // flat, no amount key
```

**File:** `src/CryptoNoteCore/Blockchain.cpp`

New RPC: `getRandomUnifiedOutputs(count)` — random selection from `m_allUnifiedOutputs` with triangular distribution bias toward recent outputs. Returns `commitKey` + `commitment` point (not amount — amounts are hidden).

#### 2f. Range proof verification on block processing

```cpp
if (target.type() == typeid(TransactionOutputUnified)) {
  auto& unified = boost::get<TransactionOutputUnified>(target);
  if (!unified.rangeProof.empty()) {
    point_validate(unified.commitment);
    if (!bulletproofs_plus_verify({unified.commitment}, unified.rangeProof))
      return false;
  }
}
```

### Verification

- `TransactionOutput.amount == 0` for all unified outputs
- `bulletproofs_plus_verify` passes for valid commitments
- `check_mlsag` passes for valid signatures
- Balance proof: `sum(C_pseudo) - sum(C_output) - fee*H == 0`
- Inflation attempt fails at balance check
- Ring selection from global pool (no amount filter)
- Legacy v10 outputs still spendable (retroactive: `C = amount*H + 0*G`)

---

## Phase 3: Fully Shielded (hard fork v12)

**Goal:** All new transactions use `TransactionOutputUnified` + `TransactionInputUnified`. Hidden amounts mandatory. `KeyOutput` only in historical blocks.

### Changes

#### 3a. Wallet: default `TransactionOutputUnified` for all sends

All `addOutput(amount, KeyOutput{...})` calls replaced with `addOutput(0, TransactionOutputUnified{...})`. No more dual-write.

#### 3b. Wallet: default `TransactionInputUnified` for all spends

All `KeyInput` and `TransactionInputCommitmentSpend` creation replaced with `TransactionInputUnified`. MLSAG signing via `generate_mlsag()`.

#### 3c. Block validation: reject legacy output types in v12+ blocks

```cpp
if (blockMajorVersion >= BLOCK_MAJOR_VERSION_12) {
  // Reject KeyOutput, MultisignatureOutput, TransactionOutputCommitment
  if (target.type() != typeid(TransactionOutputUnified))
    return false;
  // Reject KeyInput, TransactionInputCommitmentSpend
  // Only allow BaseInput (coinbase) and TransactionInputUnified
}
```

#### 3d. Legacy ring sig code path → historical only

`Crypto::check_ring_signature()` only called when validating pre-v12 blocks during sync.

### Verification

- All v12+ txs have zero visible amounts
- All v12+ outputs are `TransactionOutputUnified`
- All v12+ inputs are `TransactionInputUnified` (or `BaseInput` for coinbase)
- Pre-v12 blocks still sync and validate

---

## Phase 4: Cleanup — Remove Dead Types (no fork)

**Goal:** Remove creation/consumption of legacy types from wallet and validation hot paths. Keep deserialization for historical block sync.

### Remove (creation only, keep deserializers)

| Type | Action |
|---|---|
| `KeyOutput` | Remove from wallet sends, keep deserializer for sync |
| `KeyInput` | Remove from wallet spends, keep deserializer for sync |
| `MultisignatureOutput` | Remove entirely (no longer created since v10) |
| `MultisignatureInput` | Remove from validation hot path |
| `TransactionOutputCommitment` | Deprecate creation, keep deserializer. All new outputs → `TransactionOutputUnified` |
| `TransactionInputCommitmentSpend` | Deprecate creation, keep deserializer. All new inputs → `TransactionInputUnified` |

### Remove

| Type | Action |
|---|---|
| `TransactionInputCommitmentTransfer` | Evaluate if CD transfers should migrate to `TransactionInputUnified` with a `newTerm` extension, or stay separate |
| `m_outputs` (per-amount `KeyOutput` ring pool) | Can be removed once all `KeyInput` spending is gone |

### Keep

| Type | Reason |
|---|---|
| `TransactionOutputUnified` | The sole output type |
| `TransactionInputUnified` | The sole input type |
| `m_allUnifiedOutputs` | The sole ring pool |
| `m_commitmentOutputs` | Needed until all pre-v12 commitment outputs are spent or aged out of ring selection |

### Verification

- No new `KeyOutput`, `KeyInput`, `MultisignatureOutput` in any transaction
- Historical blocks still sync (deserializers intact)
- All wallet operations use unified types exclusively

---

## Migration Timeline

| Phase | Fork? | Blocks | Description |
|---|---|---|---|
| 0 | No | 0 | Rename constants, fix `classifyOutputAsset` |
| 1 | No | ~10k | Dual-write `KeyOutput` + `TransactionOutputUnified` (term=0) |
| 2 | Soft | ~50k | BP+ range proofs, MLSAG wiring, global ring pool |
| 3 | **Hard (v12)** | ~100k | Unified types mandatory, hidden amounts enforced |
| 4 | No | ~200k | Remove dead type creation, shrink codebase |

Phase 2 is "soft" because it adds new validation rules that degrade gracefully (empty `rangeProof` → skip verification). Phase 3 is hard because it rejects legacy types and changes the wire format.

---

## Key Files to Modify (by phase)

### Phase 0
- `src/CryptoNoteConfig.h` — constant renames
- `src/CryptoNoteCore/Blockchain.cpp` — ~11 rename sites
- `src/CryptoNoteCore/Currency.cpp` — `classifyOutputAsset` fix + ~3 renames
- `src/CryptoNoteCore/CryptoNoteFormatUtils.cpp` — ~1 rename
- `src/CryptoNoteCore/TransactionExtra.cpp` — ~1 rename

### Phase 1
- `include/ITransaction.h` — add `addOutput(uint64_t, const TransactionOutputUnified&)`
- `src/CryptoNoteCore/Transaction.cpp` — implement overload
- `src/WalletLegacy/WalletTransactionSender.cpp` — dual-write on XFG sends
- `src/Wallet/WalletGreen.cpp` — dual-write (if sends also originate here)
- `src/Transfers/TransfersConsumer.cpp` — scan for unified outputs
- `src/CryptoNoteCore/Blockchain.cpp` — index unified outputs in ring pool
- `src/CryptoNoteCore/Currency.cpp` — skip paired outputs in supply sum

### Phase 2
- `include/CryptoNote.h` — add `rangeProof` field to `TransactionOutputUnified`
- `src/CryptoNoteCore/CryptoNoteSerialization.cpp` — serialize `rangeProof` (version-gated)
- `src/crypto/bulletproofs_plus.h/.cpp` — **new files**, port from Monero
- `src/crypto/multiexp.h/.cpp` — **new files**, port from Monero
- `src/CryptoNoteCore/Blockchain.cpp` — `checkUnifiedInput()`, balance proof, BP+ verify, global pool
- `src/CryptoNoteCore/Blockchain.h` — `m_allUnifiedOutputs` vector
- Wallet send paths — Pedersen + BP+ generation, MLSAG signing

### Phase 3
- `src/CryptoNoteCore/Blockchain.cpp` — reject legacy types in v12+ blocks
- Wallet send paths — remove `KeyOutput`/`KeyInput` creation
- `src/CryptoNoteConfig.h` — define `BLOCK_MAJOR_VERSION_12`

---

## Risks

| Risk | Mitigation |
|---|---|
| Phase 1 doubles output count | Wire amount=0 on unified output, short dual-write window (~10k blocks) |
| BP+ port from Monero has bugs | Monero's test vectors, exhaustive unit tests, testnet-only until audited |
| MLSAG wiring bug causes inflation | Range proof caps to [0, 2^64). Balance proof is independent defense. Two layers |
| `rangeProof` variable-length field breaks old deserializers | Version-gate: only serialize in v12+. Pre-v12 nodes won't see it |
| `classifyOutputAsset` misclassifies HEAT unified outputs | Fixed in Phase 0 before any unified outputs exist on chain |

---

## Existing Code to Reuse

| Component | Location | Status |
|---|---|---|
| `pedersen_commit()` / `pedersen_verify()` | `src/crypto/pedersen.h/.cpp` | Compiles, no callers — ready |
| `generate_mlsag()` / `check_mlsag()` | `src/crypto/mlsag.h/.cpp` | Fully implemented — needs wiring only |
| `deriveCommitmentKeys()` | `src/CryptoNoteCore/TransactionExtra.cpp:1920` | Returns `commitKey`, `keyScalar`, `keyImage`, `amountMask` |
| Wallet commitment scanning | `src/Transfers/TransfersConsumer.cpp:102-131` | Pattern to clone for unified output scanning |
| `checkCommitmentSpendInput()` | `src/CryptoNoteCore/Blockchain.cpp:2265` | Template for new `checkUnifiedInput()` |
| `getRandomCommitmentOutputsForAmount()` | `src/CryptoNoteCore/Blockchain.cpp:1821` | Pattern for new `getRandomUnifiedOutputs()` |
| `TransactionOutputUnified` serializer | `src/CryptoNoteCore/CryptoNoteSerialization.cpp:379` | Wire tag `0x5` already assigned |
| `TransactionInputUnified` serializer | `src/CryptoNoteCore/CryptoNoteSerialization.cpp:386` | Wire tag `0x5` already assigned |

---

## Verification Strategy

### Per-phase testing

- **Phase 0:** `grep` confirms zero legacy constant references. Full build. Unit test for `classifyOutputAsset` with all output types and term values.
- **Phase 1:** Integration test: send XFG, verify 2 outputs (KeyOutput + Unified). Wallet restore from seed finds unified outputs. Ring pool includes unified outputs. Supply calculation unchanged.
- **Phase 2:** Crypto unit tests: BP+ generate/verify round-trip. MLSAG generate/verify round-trip. Integration test: hidden-amount tx passes balance proof. Inflation attempt rejected. Global ring pool selection works.
- **Phase 3:** Testnet hard fork at block N. All post-N txs use unified types. Pre-N blocks still sync. Cross-version wallet compatibility (old wallet sends to new, new wallet sends to old during transition).
- **Phase 4:** Build with legacy creation paths removed. Full chain sync from genesis succeeds. No regressions.
