# Transferable Commitment Deposits with Fee Pool Interest

## Summary

Three additions to the Fuego commitment deposit (CD) system, activated at `BLOCK_MAJOR_VERSION_11`:

1. **CD Transfer** — new input type `TransactionInputCommitmentTransfer` allows ownership transfer of locked CDs without redeeming. Spender declares the new term; consensus enforces `newTerm >= 1`.
2. **Fee Pool** — HTLC claim/refund transactions pay a swap fee of 1% on all claimed (XFG) amounts from the claimer's output into a per-epoch fee pool accumulator. Miner reward is unaffected.
3. **Interest at Redemption** — when a CD is redeemed via `CommitmentSpend`, the spender declares `claimedInterest` in the input. Consensus bounds-checks it against the maximum possible interest from the fee pool, then pays `principal + claimedInterest`.

A fourth behavior, **auto-rollover**, is wallet-level only (not consensus). The wallet defaults to re-locking at maturity unless the user explicitly redeems.

## Motivation

- **TradeOgre is dead.** XFG needs self-sustaining price discovery. Atomic swaps provide it, but need a flywheel.
- **CDs are one-way locks today.** If you need liquidity before maturity, you're stuck. Transferability creates a secondary market without unlocking the XFG (ring pool stays intact).
- **No incentive to lock beyond staking.** The fee pool creates yield from real economic activity (swap fees), making CDs a yield-bearing instrument priced by cash flows.
- **Flywheel:** swap volume -> fees -> CD yield -> more locking -> better privacy (ring pool depth) -> more network value -> more swap volume.
- **note:** may consider adding option for 3yr term on cold- for jumbo tier (800XFG) only issue w 3yr is `cold 80 3` would need to change to 90days `cold 8 90` 1yr (3yrs only for 800)
## Design

### 1. TransactionInputCommitmentTransfer

A new input type that spends a locked CD and produces a new locked CD with a different owner.

```cpp
// include/CryptoNote.h — added to TransactionInput variant
struct TransactionInputCommitmentTransfer {
  uint64_t amount;                      // must match referenced CD amount
  std::vector<uint32_t> outputIndexes;  // ring: global commitment output indices (relative offsets)
  Crypto::KeyImage keyImage;            // same key image derivation as CommitmentSpend
  uint32_t newTerm;                     // new CD's term (spender-declared, >= 1)
};
```

**The "unknown real output" problem and solution:**
In a ring signature, the validator cannot determine which ring member is the real spend. This means the validator cannot compute the "remaining term" from the real output's creation height. Instead, the spender declares `newTerm` and consensus only checks `newTerm >= CD_TRANSFER_MIN_REMAINING_TERM` (= 1). The secondary market enforces realistic terms economically: a buyer won't accept a CD with term=1 if they expect a long-dated bond. The term is part of the signed transaction data.

**Key properties:**
- Ring signature verification identical to `CommitmentSpend` (same ring selection from `m_commitmentOutputs[amount]`)
- **Does NOT require maturity.** Ring members can be immature (since the CD stays locked, nobody is withdrawing early).
- Key image is recorded in `m_spent_keys` — prevents double-spend/double-transfer
- The transaction MUST produce exactly one `TransactionOutputCommitment` at the same amount with `term == newTerm`
- Binary variant tag: `0x08` (next available after existing types)

**Ring pool interaction:**
- The old CD's `CommitmentOutputRef` stays in `m_commitmentOutputs` (it's "spent" via key image, not removed — still a valid decoy)
- The new CD is added as a new `CommitmentOutputRef`
- Net effect: ring pool grows by 1 for every transfer

### 2. Fee Pool Accumulator

A per-epoch accumulator that collects swap fees from HTLC transactions.

**Fee source:** Every `TransactionInputHashLockClaim` and `TransactionInputHashLockRefund` transaction pays a swap fee:

```
swap_fee = claim_amount * SWAP_FEE_RATE_BPS / SWAP_FEE_RATE_DIVISOR
SWAP_FEE_RATE = 30 basis points (0.3%)

Example: claim 100 XFG HTLC -> swap_fee = 0.3 XFG
```

**Where the fee comes from (claimer pays, not miner):**
The swap fee is deducted from the claimer's output — NOT from the miner's reward. An HTLC claim transaction's outputs must total at most `claim_amount - swap_fee`. The `swap_fee` goes into the fee pool. The miner still collects the standard transaction fee (inputs - outputs - swap_fee).

```
HTLC output:      100.0000000 XFG
Claimer receives:   99.7000000 XFG (in outputs)
Swap fee to pool:    0.3000000 XFG
Miner tx fee:        0.0008000 XFG (standard MINIMUM_FEE, separate)
```

**This avoids miner incentive misalignment.** Miners have no reason to exclude HTLC claims because their tx fee is unaffected. The swap fee is a protocol-level surcharge on HTLC operations.

**Storage (persisted to disk alongside block cache):**
- `m_feePoolBalance` — cumulative uint64_t in blockchain state (persisted in storeCache/loadCache, analogous to `m_alreadyGeneratedCoins`)
- Per-epoch snapshots stored in `EpochReport` (already has `totalFeesDistributed`)
- `m_epochFeeRate[epoch]` = `fees_collected_this_epoch * RATE_PRECISION / total_cd_locked_at_epoch_start`
- On reorg across epoch boundary: invalidate affected `m_epochFeeRates` entries

**New constants:**
```cpp
// CryptoNoteConfig.h
const uint64_t SWAP_FEE_RATE_BPS = 30;               // 0.3% (30 basis points)
const uint64_t SWAP_FEE_RATE_DIVISOR = 10000;         // basis point denominator
const uint64_t FEE_POOL_RATE_PRECISION = 1000000ULL;  // 1e6 fixed-point (fits uint32_t for div128_32)
const uint64_t TESTNET_SWAP_FEE_RATE_BPS = 100;       // 1% on testnet
const uint32_t CD_TRANSFER_MIN_REMAINING_TERM = 1;    // minimum term for transferred CD
```

### 3. Interest at Redemption ("Declare and Verify" Model)

When a CD is redeemed via `CommitmentSpend`, the spender declares the interest they claim. The validator bounds-checks it.

**Why "declare and verify":** In a ring signature, the validator does not know which ring member is the real output and therefore cannot determine the CD's creation height. Instead:

1. Spender adds `uint64_t claimedInterest` field to `TransactionInputCommitmentSpend`
2. Validator computes `max_possible_interest` using the **oldest ring member's** creation epoch as the earliest possible start
3. Validator checks: `claimedInterest <= max_possible_interest AND claimedInterest <= m_feePoolBalance`
4. Transaction balance: `sum(outputs) + tx_fee == sum(input_amounts) + sum(claimedInterest_per_input)`

```cpp
// Updated CommitmentSpend with interest declaration
struct TransactionInputCommitmentSpend {
  uint64_t amount;
  std::vector<uint32_t> outputIndexes;
  Crypto::KeyImage keyImage;
  uint64_t claimedInterest;  // NEW: declared interest from fee pool (0 for pre-activation)
};
```

**Max interest calculation:**
```
oldest_ring_epoch = min(creation_epoch for each ring member)
max_interest = amount * SUM(epochFeeRate[e] for e in oldest_ring_epoch..current_epoch) / RATE_PRECISION
```

**Rational spender behavior:** The spender has their own creation height (they know their deposit secret). They compute exact interest and declare it. They have no incentive to over-claim (rejected by validator) or under-claim (leaving money on the table). The ring sig hides which member is real while the interest declaration is honest.

**128-bit arithmetic:** Use `FEE_POOL_RATE_PRECISION = 1e6` (fits uint32_t) for a single `div128_32` call. Max product: `8e9 (amount) * 1e6 (rate) = 8e15` which fits in 64 bits for single-epoch. Multi-epoch accumulation uses 64-bit running sum (safe up to ~18e18).

### 4. Auto-Rollover (Wallet-Level Only)

**Not a consensus change.** The wallet implements:
- At maturity, if the user hasn't manually called `redeem_cd`, the wallet prompts to auto-roll
- Auto-roll = single atomic transaction: CommitmentSpend (redeem + interest) -> new TransactionOutputCommitment (principal + interest, same term) in one tx
- Consecutive roll counter tracked in wallet state for UI display

### 5. On-Chain XFG/CD Swap Market

CD transfers enable a secondary market. The swap is a single atomic transaction:

```
Inputs:  CommitmentTransfer (seller's CD) + KeyInput (buyer's liquid XFG)
Outputs: new CD (buyer's commitKey, term=newTerm) + KeyOutput (seller receives liquid XFG)
```

Both parties must co-sign. For v1, manual peer-to-peer. EFier-served orderbook is a follow-on.

## Files Requiring Updates (Comprehensive)

### New input type propagation (ALL boost::variant visitors):

| File | Function/Visitor | Change |
|------|-----------------|--------|
| `include/CryptoNote.h` | `TransactionInput` variant | Add `TransactionInputCommitmentTransfer` |
| `include/CryptoNote.h` | `TransactionInputCommitmentSpend` | Add `claimedInterest` field |
| `src/CryptoNoteCore/CryptoNoteSerialization.cpp` | `BinaryVariantTagGetter` | Add tag `0x08` |
| `src/CryptoNoteCore/CryptoNoteSerialization.cpp` | `getVariantValue()` | Add deserializer case |
| `src/CryptoNoteCore/CryptoNoteSerialization.cpp` | `txin_signature_size_visitor` | Add `operator()` |
| `src/CryptoNoteCore/CryptoNoteSerialization.cpp` | `serialize(TransactionInputCommitmentTransfer)` | New serializer |
| `src/CryptoNoteCore/CryptoNoteFormatUtils.cpp` | `check_inputs_types_supported()` | Add type check |
| `src/CryptoNoteCore/CryptoNoteFormatUtils.cpp` | `get_inputs_money_amount()` | Add amount extraction |
| `src/CryptoNoteCore/CryptoNoteFormatUtils.cpp` | `check_inputs_overflow()` | Add overflow check |
| `src/CryptoNoteCore/TransactionUtils.cpp` | `getRequiredSignaturesCount()` | Return ring size |
| `src/CryptoNoteCore/TransactionUtils.cpp` | `getTransactionInputAmount()` | Return amount |
| `src/CryptoNoteCore/TransactionUtils.cpp` | `getTransactionInputType()` | Return type enum |
| `src/CryptoNoteCore/TransactionUtils.cpp` | `checkInputsKeyimagesDiff()` | Check key images (also fix for CommitmentSpend) |
| `src/CryptoNoteCore/Currency.cpp` | `getTransactionInputAmount()` | Handle new type (prevent assert crash) |
| `src/CryptoNoteCore/Currency.cpp` | `getTransactionFee()` | Account for claimedInterest in balance |
| `src/CryptoNoteCore/Blockchain.cpp` | `pushTransaction()` | Insert key image for transfers; track fee pool |
| `src/CryptoNoteCore/Blockchain.cpp` | `popTransaction()` | Remove key image; reverse fee pool |
| `src/CryptoNoteCore/Blockchain.cpp` | `storeCache()` / `loadCache()` | Persist fee pool state |
| `src/CryptoNoteCore/Blockchain.cpp` | tx validation switch | Call `checkCommitmentTransferInput()` |
| `src/CryptoNoteCore/TransactionPool.cpp` | `TransactionPoolTxValidator` | Check CommitmentTransfer + CommitmentSpend key images |
| `include/ITransaction.h` | `InputType` enum | Add `CommitmentTransfer` |
| `src/CryptoNoteCore/Transaction.cpp` | `addInput()` / `signInput()` | Handle new type |
| `include/BlockchainExplorerData.h` | Input details variant | Add `CommitmentTransferDetails` |

## What Does NOT Change

- `TransactionOutputCommitment` struct (unchanged — only the inputs change)
- Ring pool structure (`m_commitmentOutputs`)
- HTLC output/input type definitions
- Regular transaction handling (KeyInput/KeyOutput)
- Stealth addresses, ring sig primitives
- Existing wallet deposit/burn commands

## Risk Analysis

| Risk | Severity | Mitigation |
|------|----------|------------|
| Fee pool drain (more interest paid than collected) | Critical | `claimedInterest <= m_feePoolBalance` enforced at validation; fee pool can never go negative |
| Spender over-claims interest | Critical | Bounded by `max_possible_interest` from oldest ring member epoch; rational spenders self-limit |
| CD transfer enables wash trading for ring pool inflation | Medium | Key images prevent double-transfer; ring pool growth bounded by real economic activity |
| Epoch rate manipulation (spam CDs to dilute rate) | Low | Minimum CD amount is AMOUNT_TIER_0 (0.8 XFG); tier system prevents dust |
| Reorg across epoch boundary corrupts fee rates | Medium | Epoch rates invalidated on reorg; rebuilt from block data |
| Miner censorship of HTLC claims | None | Swap fee comes from claimer, not miner; miner has no incentive to exclude |
| Auto-rollover wallet bug loses funds | Low | Wallet-only; user can always manually redeem; single atomic tx prevents partial rollover |
