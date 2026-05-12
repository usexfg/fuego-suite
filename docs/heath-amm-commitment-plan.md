# HEAT & Hearth AMM — Commitment Output Model (v1 Privacy)

## Goal

Route HEAT mint and Hearth AMM operations through the existing `TransactionOutputCommitment` type so they share the same on-chain footprint as all other deposit operations.

## Privacy Model

| Observer sees | Current | After |
|---|---|---|
| HEAT mint output | `TransactionOutput{assetId=1}` — clearly HEAT | `TransactionOutputCommitment{amount, key, term}` — indistinguishable |
| AMM swap output | `TransactionOutput{assetId=0/1}` — clearly XFG/HEAT | Same commitment type |
| CD deposit | Commitment type (already) | Commitment type (unchanged) |
| XFG transfer | KeyOutput (stays simple) | KeyOutput (unchanged) |
| HEAT transfer | KeyOutput + assetId=1 (stays simple) | KeyOutput + assetId=1 (unchanged) |

**Single remaining fingerprint:** `term > 0` = CD deposit, `term = 0` = non-locked (mint or AMM). A 2-way partition instead of the current 4-way by public assetId.

## Design Decisions

| Decision | Resolution |
|---|---|
| HEAT tiers vs XFG tiers | Same values — eliminates amount-based fingerprinting |
| Decoy pool separation | `encodeAssetAmount(amount, assetId)` — internal only, invisible on-chain |
| Standard transfers | Unchanged (KeyOutput + assetId) |
| AMM swap amounts | Public in v1 (from tx_extra AMM tag) — ZK-private deferred |
| Pool reserves | Public (AMM math requires it) |
| Commitment infra | All exists — DepositCommitment, CommitmentIndex, BankingIndex, StarkCommitmentGenerator |

## Files (6)

### 1. `src/CryptoNoteConfig.h`
```cpp
// HEAT tiers match XFG tiers to prevent fingerprinting
HEAT_TIER_0 = 8,000,000   (was 16,000,000)
HEAT_TIER_1 = 80,000,000  (was 160,000,000)
HEAT_TIER_2 = 800,000,000
HEAT_TIER_3 = 8,000,000,000
HEAT_CD_TIER_0..3 = same values
```

### 2. `include/CryptoNote.h`
Restore `TransactionOutputCommitment`, `TransactionInputCommitmentSpend` to variants:
```cpp
boost::variant<BaseInput, KeyInput, MultisignatureInput,
               TransactionInputCommitmentSpend> TransactionInput;

boost::variant<KeyOutput, MultisignatureOutput,
               TransactionOutputCommitment> TransactionOutputTarget;
```

### 3. `src/Wallet/WalletGreen.cpp`
- `createHeatMintTransaction(xfgAmount)` — select XFG inputs, produce `TransactionOutputCommitment` via StarkCommitmentGenerator, add 0xD5 secret for recovery
- `createAmmSwapTransaction(direction, amount, minOutput)` — same pattern, term=0, AMM tag in extra
- Amount visible to validator via commitment's public amount field

### 4. `src/WalletLegacy/WalletTransactionSender.cpp`
- Extend `prepareKeyInputs()` for commitment spend path
- `makeGetRandomCommitmentOutsRequest()` already exists (used for CD deposits)

### 5. `src/CryptoNoteCore/Blockchain.cpp`
- AMM validation reads amount from `TransactionOutputCommitment.amount` (public field)
- Detects HEAT mint: XFG inputs + commitment output with term=0
- `pushToBankingIndex()` already handles commitment types
- `encodeAssetAmount` already separates decoy pools by assetId

### 6. `src/CryptoNoteCore/CryptoNoteSerialization.h/cpp`
Restore commitment type serialization (exists in original codebase).

## What Stays The Same

- Hearth AMM math (`AmmPool.h/cpp`) — constant-product, unchanged
- PI controller — unchanged
- Epoch boundary CD yield pipeline — unchanged
- RPC endpoints — unchanged
- Wallet CLI commands — unchanged
- Standard transfers — unchanged (KeyOutput + assetId)
- All deposit infra (`DepositCommitment.h`, `CommitmentIndex.h`, `BankingIndex.h`) — already present

## Implementation Phases

### Phase A — Restore Types (2 files)
`include/CryptoNote.h`, `CryptoNoteSerialization.h/cpp` — add commitment types to variants and serialization. Match tier values. Build.

### Phase B — Wallet Construction (2 files)
`WalletGreen.cpp`, `WalletTransactionSender.cpp` — HEAT mint + AMM produce commitment outputs. Build.

### Phase C — Consensus (2 files)
`Blockchain.cpp` — AMM reads from commitment outputs, validates. Build.

**Total: ~6 files, ~200 lines net change.** Low risk — all infrastructure exists.
