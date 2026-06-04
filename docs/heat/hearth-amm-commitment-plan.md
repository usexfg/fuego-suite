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

## Files

### Done (2026-05-12)

**1. `include/CryptoNote.h`** ✅
Added `TransactionInputCommitmentSpend` and `TransactionOutputCommitment` structs. Restored both to the variant typedefs (tag `0x4` for both).
```cpp
struct TransactionInputCommitmentSpend {
  uint64_t amount;
  std::vector<uint32_t> outputIndexes;
  Crypto::KeyImage keyImage;
  uint64_t claimedInterest = 0;
};

struct TransactionOutputCommitment {
  uint64_t amount;
  Crypto::PublicKey key;
  uint32_t term = 0; // 0=non-locked (mint/AMM), >0=CD deposit
};

typedef boost::variant<BaseInput, KeyInput, MultisignatureInput,
                       TransactionInputCommitmentSpend> TransactionInput;

typedef boost::variant<KeyOutput, MultisignatureOutput,
                       TransactionOutputCommitment> TransactionOutputTarget;
```

**2. `src/CryptoNoteConfig.h`** ✅
HEAT tiers halved to match XFG tiers (8M/80M/800M/8B), eliminating amount-based fingerprinting.

**3. `src/CryptoNoteCore/CryptoNoteSerialization.h/cpp`** ✅
Full serialization for both commitment types — `serializeVarintVector` for `outputIndexes` (matching `KeyInput` pattern), binary for `amount`, `key`, `term`. Tag dispatch in `getVariantValue` for tag `0x4` on both input and output variants. `BinaryVariantTagGetter` and `txin_signature_size_visitor` updated.

**4. `src/CryptoNoteCore/Blockchain.cpp`** ✅
- Output loop: commitment outputs classified as HEAT transactions, indexed into `m_outputs` with XFG-assetId encoding for indistinguishability
- `pushTransaction`: double-spend key image tracking for `TransactionInputCommitmentSpend`. All 5 rollback paths handle commitment outputs/inputs
- `popTransaction`: commitment output/index reversal, commitment input key image erase
- `checkTransactionInputs`: validates commitment spends (non-empty outputIndexes, key image not spent)

**5. `src/CryptoNoteCore/HeatMintEngine.cpp`** ✅
`isHeatMint()` detects commitment outputs with `term==0`. `validateMint()` reads amount from commitment outputs for mint validation.

**6. `src/CryptoNoteCore/CryptoNoteFormatUtils.cpp`** ✅
`check_inputs_types_supported` accepts `TransactionInputCommitmentSpend`.

### Not Done (v2)

**WalletGreen.cpp** — `createHeatMintTransaction()` / `createAmmSwapTransaction()` not implemented. These need to select XFG inputs, produce `TransactionOutputCommitment` via `StarkCommitmentGenerator`, add 0xD5 recovery secret, and inject AMM tx_extra tags.

**WalletTransactionSender.cpp** — commitment spend path and `makeGetRandomCommitmentOutsRequest` not extended.

**Ring signature for commitment spends** — `check_tx_input` in `Blockchain.cpp` rejects `TransactionOutputCommitment` targets in its output visitor (only collects `KeyOutput::key`). Spending commitment outputs (e.g. CD withdrawal) requires extending the visitor to handle `TransactionOutputCommitment::key`.

## What Stays The Same

- Hearth AMM math (`AmmPool.h/cpp`) — constant-product, unchanged
- PI controller — unchanged
- Epoch boundary CD yield pipeline — unchanged
- RPC endpoints — unchanged
- Wallet CLI commands — unchanged
- Standard transfers — unchanged (KeyOutput + assetId)
- All deposit infra (`DepositCommitment.h`, `CommitmentIndex.h`, `BankingIndex.h`) — already present

## Implementation Status

| Phase | File | Status |
|-------|------|--------|
| A | `include/CryptoNote.h` | Done |
| A | `src/CryptoNoteConfig.h` | Done |
| A | `CryptoNoteSerialization.h/cpp` | Done |
| B | `WalletGreen.cpp` | Done |
| B | `WalletTransactionSender.cpp` | Done (prepareCommitmentInputs added) |
| C | `Blockchain.cpp` | Done |
| C | `HeatMintEngine.cpp` | Done |
| — | `ITransaction.h` / `Transaction.cpp` | Done (extra — addOutput/addInput for commitment types) |
| — | `Blockchain.h` | Done (extra — scanOutputKeysForIndexes overload + check_tx_input_commitment) |
| — | `ITransaction.h` (TransactionTypes) | Done (extra — OutputType::Commitment) |
| — | `TransactionUtils.cpp` | Done (extra — getTransactionOutputType for Commitments) |
| — | `CryptoNoteFormatUtils.cpp` | Done (extra) |

### Not Done (v2)

**doSendCommitmentTransaction** — full wallet send path for spending commitment outputs. The `prepareCommitmentInputs` utility exists, but the `constructTx` / `doSendTransaction` pipeline in WalletTransactionSender doesn't call it yet. This needs a `doSendCommitmentTransaction` function that adds `TransactionInputCommitmentSpend` inputs via `ITransaction::addInput`.

**RPC server output resolution** — `RpcServer.cpp` line 342 calls `boost::get<KeyOutput>(o.target)` which will throw if a commitment output is encountered during getRandomOutsByAmount.

**TransfersContainer** — transfer scanning doesn't recognize `OutputType::Commitment` outputs yet. The `TransactionOutputInformation` union has no commitment-specific field.

**Net: 15 files changed, ~350 lines net. 22/22 build targets pass.**

## Future Scope: Hearth LP Fee + Payouts

The current swap fee (`SWAP_FEE_RATE_BPS = 100`, 1%) is split 80/20 between the CD yield pool and treasury. LP providers receive no direct swap fee incentive — their return is solely from impermanent-loss arbitrage.

A separate `HEARTH_FEE_RATE_BPS` (30 bps, 0.3%) should be introduced, taken from each swap, paid to LP providers. This is a **separate plan**, to be scoped after this commitment-output plan is fully complete.

**Phases (suggested):**

| Phase | What |
|-------|------|
| 1 | Define `HEARTH_FEE_RATE_BPS = 30` in `CryptoNoteConfig.h`. Add `m_hearthLpFees` accumulator in `AmmPoolState`. Deduct hearth fee alongside swap fee in `Blockchain.cpp` pushTransaction. |
| 2 | Track per-LP shares: store LP contributions indexed by public key in `Blockchain`. Derive payout amounts proportional to share of pool on removal. Pay out hearth fee + share of reserves on `removeLiquidity`. |
| 3 | Auto-compound option: LP flag in tx_extra (new tag `0xF3`) to reinvest earned fees without withdrawal. |
| 4 | Claim APY without LP withdrawal: new tx_extra tag `0xF4` to claim accumulated hearth fees while leaving principal in pool. |

**Files affected:** `CryptoNoteConfig.h`, `AmmPool.h/cpp`, `Blockchain.cpp`, `TransactionExtra.h/cpp`, possibly `BankingIndex.h` for LP tracking.
