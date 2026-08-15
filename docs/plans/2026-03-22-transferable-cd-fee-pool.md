# Transferable CD with Fee Pool Interest — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make commitment deposits transferable, add a fee pool funded by atomic swap fees, and pay interest to CD holders at redemption.

**Architecture:** Three consensus additions activated at `BLOCK_MAJOR_VERSION_11`: (1) new `TransactionInputCommitmentTransfer` input type (with `newTerm` field) with ring-sig verification but no maturity requirement, (2) per-epoch fee pool accumulator funded by swap fees deducted from **claimer's output** (NOT miner reward), (3) "declare and verify" interest model where spender declares `claimedInterest` in `TransactionInputCommitmentSpend` and validator bounds-checks against max possible from oldest ring member. Auto-rollover is wallet-only.

**Tech Stack:** C++ (CryptoNote core), Boost.Variant, existing ring signature primitives, existing epoch/CommitmentIndex infrastructure.

**Spec:** `docs/superpowers/specs/2026-03-22-transferable-cd-fee-pool-design.md`

---

## File Map

### New Files
- `tests/TransferableCDTests/` — test directory for all new consensus logic

### Modified Files
| File | Changes |
|------|---------|
| `include/CryptoNote.h` | Add `TransactionInputCommitmentTransfer` struct (with `newTerm`); add `claimedInterest` to `TransactionInputCommitmentSpend`; add to `TransactionInput` variant |
| `src/CryptoNoteConfig.h` | Add `SWAP_FEE_RATE_BPS`, `SWAP_FEE_RATE_DIVISOR`, `FEE_POOL_ACTIVATION_HEIGHT` |
| `src/CryptoNoteCore/Blockchain.h` | Add `m_feePoolBalance`, declare `checkCommitmentTransferInput()` |
| `src/CryptoNoteCore/Blockchain.cpp` | Implement `checkCommitmentTransferInput()`, fee pool accumulation in `pushTransaction`, swap fee deduction in block reward |
| `src/CryptoNoteCore/CryptoNoteSerialization.cpp` | Serialize/deserialize `TransactionInputCommitmentTransfer` |
| `src/CryptoNoteCore/Currency.h` | Update `calculateInterest()` signature |
| `src/CryptoNoteCore/Currency.cpp` | Implement fee-pool-aware `calculateInterest()`, modify `getBlockReward()` for swap fee deduction |
| `src/CryptoNoteCore/CommitmentIndex.h` | Add `epochFeeRate`, `totalCdLockedAtEpochStart` to `EpochReport` |
| `src/CryptoNoteCore/CommitmentIndex.cpp` | Track per-epoch fee rates, total locked CD supply |
| `src/CryptoNoteCore/TransactionExtra.h` | (Optional) Add transfer metadata tag |
| `src/SimpleWallet/SimpleWallet.h` | Declare `transfer_cd()`, `redeem_cd()` wallet commands |
| `src/SimpleWallet/SimpleWallet.cpp` | Implement wallet commands |
| `src/WalletLegacy/WalletTransactionSender.cpp` | Add commitment transfer transaction construction |
| `src/Rpc/CoreRpcServerCommandsDefinitions.h` | Add RPC structs for fee pool / CD info |
| `src/Rpc/RpcServer.cpp` | Add RPC endpoint handlers |

---

## Task 1: Add TransactionInputCommitmentTransfer Type

**Files:**
- Modify: `include/CryptoNote.h:70-136`
- Modify: `src/CryptoNoteCore/CryptoNoteSerialization.cpp:379+`

- [ ] **Step 1: Define the struct in CryptoNote.h**

Add after `TransactionInputCommitmentSpend` (line 77):

```cpp
// v10+ commitment deposit transfer — transfers CD ownership without redeeming.
// Ring signature proves spend authority, key image prevents double-transfer.
// Does NOT require maturity — the CD stays locked with remaining term.
// Must produce exactly one TransactionOutputCommitment at the same amount.
struct TransactionInputCommitmentTransfer {
  uint64_t amount;                      // must match referenced CD amount
  std::vector<uint32_t> outputIndexes;  // ring: global commitment output indices (relative offsets)
  Crypto::KeyImage keyImage;            // H_p(commitKey) * keyScalar
  uint32_t newTerm;                     // new CD's term (spender-declared, >= 1)
};
```

- [ ] **Step 1b: Add claimedInterest to TransactionInputCommitmentSpend**

Add `claimedInterest` field to the existing `TransactionInputCommitmentSpend` struct:

```cpp
struct TransactionInputCommitmentSpend {
  uint64_t amount;
  std::vector<uint32_t> outputIndexes;
  Crypto::KeyImage keyImage;
  uint64_t claimedInterest;  // NEW: declared interest from fee pool (0 for pre-activation)
};
```

- [ ] **Step 2: Add to TransactionInput variant**

Update the `TransactionInput` typedef (line 134):

```cpp
typedef boost::variant<
  BaseInput, KeyInput, MultisignatureInput,
  TransactionInputCommitmentSpend,
  TransactionInputCommitmentTransfer,  // NEW
  TransactionInputUnified,
  TransactionInputHashLockClaim,
  TransactionInputHashLockRefund
> TransactionInput;
```

- [ ] **Step 3: Add serialization**

In `CryptoNoteSerialization.cpp`, add serializer for the new type:

```cpp
void serialize(TransactionInputCommitmentTransfer& in, ISerializer& serializer) {
  serializer(in.amount, "amount");
  serializer(in.outputIndexes, "output_indexes");
  serializer(in.keyImage, "key_image");
  serializer(in.newTerm, "new_term");
}
```

Also update the `TransactionInput` variant serializer visitor to handle the new type (mirror the `TransactionInputCommitmentSpend` case).

- [ ] **Step 4: Verify build compiles**

Run: `cmake --build /home/ar/fuego/build -j$(nproc) 2>&1 | tail -20`
Expected: Compiles cleanly (new type defined but not yet validated)

- [ ] **Step 5: Commit**

```bash
git add include/CryptoNote.h src/CryptoNoteCore/CryptoNoteSerialization.cpp
git commit -m "consensus: define TransactionInputCommitmentTransfer struct and serialization"
```

---

## Task 1.5: Propagate New Type Through All Variant Visitors

**Files (comprehensive — missing from original plan, required by spec):**
- Modify: `src/CryptoNoteCore/CryptoNoteFormatUtils.cpp` — `check_inputs_types_supported()`, `get_inputs_money_amount()`, `check_inputs_overflow()`
- Modify: `src/CryptoNoteCore/TransactionUtils.cpp` — `getRequiredSignaturesCount()`, `getTransactionInputAmount()`, `getTransactionInputType()`, `checkInputsKeyimagesDiff()` (also fix pre-existing bug: CommitmentSpend key images not checked)
- Modify: `src/CryptoNoteCore/Currency.cpp` — `getTransactionInputAmount()` (prevent assert crash), `getTransactionFee()` (account for claimedInterest)
- Modify: `src/CryptoNoteCore/TransactionPool.cpp` — `TransactionPoolTxValidator` must check CommitmentTransfer + CommitmentSpend key images
- Modify: `include/ITransaction.h` — Add `CommitmentTransfer` to `InputType` enum
- Modify: `src/CryptoNoteCore/Transaction.cpp` — `addInput()` / `signInput()` handle new type
- Modify: `include/BlockchainExplorerData.h` — Add `CommitmentTransferDetails` variant

Each of these files has a boost::variant visitor or type switch that will reject/crash on the new input type if not updated. This task ensures the new type propagates cleanly through the entire codebase before we add validation logic.

- [ ] **Step 1: Update CryptoNoteFormatUtils.cpp**
- [ ] **Step 2: Update TransactionUtils.cpp (+ fix CommitmentSpend key image bug)**
- [ ] **Step 3: Update Currency.cpp getTransactionInputAmount()**
- [ ] **Step 4: Update TransactionPool.cpp**
- [ ] **Step 5: Update ITransaction.h InputType enum**
- [ ] **Step 6: Update Transaction.cpp**
- [ ] **Step 7: Update BlockchainExplorerData.h**
- [ ] **Step 8: Verify build and commit**

```bash
cmake --build /home/ar/fuego/build -j$(nproc) 2>&1 | tail -20
git add -A
git commit -m "consensus: propagate TransactionInputCommitmentTransfer through all variant visitors"
```

---

## Task 2: Add Fee Pool Constants

**Files:**
- Modify: `src/CryptoNoteConfig.h:150+`

- [ ] **Step 1: Add constants**

After the `EPOCH_DURATION_BLOCKS` constants (line 153):

```cpp
// CD Fee Pool — funded by atomic swap (HTLC) fees, distributed to CD holders
const uint64_t SWAP_FEE_RATE_BPS = 30;                // 0.3% of HTLC claim/refund amount
const uint64_t SWAP_FEE_RATE_DIVISOR = 10000;          // basis point denominator
const uint64_t FEE_POOL_RATE_PRECISION = 1000000ULL;    // 1e6 fixed-point (fits uint32_t for div128_32)
const uint64_t TESTNET_SWAP_FEE_RATE_BPS = 100;        // 1% on testnet (higher for faster testing)

// Minimum remaining term for a transferred CD (prevents zero-term transfers)
const uint32_t CD_TRANSFER_MIN_REMAINING_TERM = 1;
```

- [ ] **Step 2: Verify build**

Run: `cmake --build /home/ar/fuego/build -j$(nproc) 2>&1 | tail -5`
Expected: Clean build

- [ ] **Step 3: Commit**

```bash
git add src/CryptoNoteConfig.h
git commit -m "config: add SWAP_FEE_RATE_BPS and fee pool constants"
```

---

## Task 3: Fee Pool State in Blockchain

**Files:**
- Modify: `src/CryptoNoteCore/Blockchain.h:336-390`
- Modify: `src/CryptoNoteCore/Blockchain.cpp` (pushTransaction, popTransaction)

- [ ] **Step 1: Add fee pool state to Blockchain.h**

In the private section near `m_commitmentOutputs` (line 387):

```cpp
// Fee pool: accumulates swap fees from HTLC claims, distributed as interest to CD holders.
uint64_t m_feePoolBalance = 0;        // total XFG available for CD interest payouts
uint64_t m_currentEpochSwapFees = 0;  // fees accumulated in current epoch (reset each epoch)
uint64_t m_totalCdLocked = 0;         // total XFG locked in CDs (for epoch rate calculation)
```

- [ ] **Step 2: Add helper to compute swap fee for an HTLC input**

In `Blockchain.cpp`, add a static helper:

```cpp
static uint64_t computeSwapFee(uint64_t claimAmount) {
  return (claimAmount * CryptoNote::parameters::SWAP_FEE_RATE_BPS)
       / CryptoNote::parameters::SWAP_FEE_RATE_DIVISOR;
}
```

- [ ] **Step 3: Accumulate fees in pushTransaction**

In `pushTransaction()`, after processing outputs, add fee pool tracking for HTLC claims:

```cpp
// Accumulate swap fees from HTLC claims/refunds into the fee pool
for (const auto& input : transaction.tx.inputs) {
  if (input.type() == typeid(TransactionInputHashLockClaim)) {
    const auto& claim = boost::get<TransactionInputHashLockClaim>(input);
    uint64_t swapFee = computeSwapFee(claim.amount);
    m_feePoolBalance += swapFee;
    m_currentEpochSwapFees += swapFee;
  } else if (input.type() == typeid(TransactionInputHashLockRefund)) {
    const auto& refund = boost::get<TransactionInputHashLockRefund>(input);
    uint64_t swapFee = computeSwapFee(refund.amount);
    m_feePoolBalance += swapFee;
    m_currentEpochSwapFees += swapFee;
  }
}
```

- [ ] **Step 4: Reverse fee pool in popTransaction**

In `popTransaction()`, add the reverse:

```cpp
for (const auto& input : transaction.tx.inputs) {
  if (input.type() == typeid(TransactionInputHashLockClaim)) {
    const auto& claim = boost::get<TransactionInputHashLockClaim>(input);
    uint64_t swapFee = computeSwapFee(claim.amount);
    m_feePoolBalance -= swapFee;
    m_currentEpochSwapFees -= swapFee;
  } else if (input.type() == typeid(TransactionInputHashLockRefund)) {
    const auto& refund = boost::get<TransactionInputHashLockRefund>(input);
    uint64_t swapFee = computeSwapFee(refund.amount);
    m_feePoolBalance -= swapFee;
    m_currentEpochSwapFees -= swapFee;
  }
}
```

- [ ] **Step 5: Track total CD locked supply**

In `pushTransaction()` output processing, update `m_totalCdLocked`:

```cpp
if (out.target.type() == typeid(TransactionOutputCommitment)) {
  m_totalCdLocked += out.amount;
  // ... existing commitment pool logic ...
}
```

And in `popTransaction()`:

```cpp
if (output.target.type() == typeid(TransactionOutputCommitment)) {
  m_totalCdLocked -= output.amount;
  // ... existing removal logic ...
}
```

For `CommitmentSpend` inputs (CD redemptions), subtract from `m_totalCdLocked`:

```cpp
if (input.type() == typeid(TransactionInputCommitmentSpend)) {
  m_totalCdLocked -= boost::get<TransactionInputCommitmentSpend>(input).amount;
}
```

- [ ] **Step 6: Verify build**

Run: `cmake --build /home/ar/fuego/build -j$(nproc) 2>&1 | tail -20`
Expected: Clean build

- [ ] **Step 7: Commit**

```bash
git add src/CryptoNoteCore/Blockchain.h src/CryptoNoteCore/Blockchain.cpp
git commit -m "consensus: add fee pool accumulator and CD locked supply tracking"
```

---

## Task 4: Per-Epoch Fee Rate in CommitmentIndex

**Files:**
- Modify: `src/CryptoNoteCore/CommitmentIndex.h:121-175`
- Modify: `src/CryptoNoteCore/CommitmentIndex.cpp:1029+`
- Modify: `src/CryptoNoteCore/Blockchain.cpp` (epoch boundary logic)

- [ ] **Step 1: Add epoch fee rate fields to EpochReport**

In `CommitmentIndex.h`, add to `EpochReport` struct:

```cpp
uint64_t swapFeesCollected = 0;       // total swap fees from HTLC claims this epoch
uint64_t totalCdLockedAtStart = 0;    // total XFG in CDs at epoch start
uint64_t feeRateFixedPoint = 0;       // (swapFeesCollected * RATE_PRECISION) / totalCdLockedAtStart
```

Update `serialize()` to include the new fields.

- [ ] **Step 2: Add epoch fee rate vector to CommitmentIndex**

In `CommitmentIndex.h` private section:

```cpp
std::vector<uint64_t> m_epochFeeRates;  // indexed by epoch number, fixed-point fee rate
```

And add public accessor:

```cpp
uint64_t getEpochFeeRate(uint64_t epochNumber) const;
uint64_t getEpochCount() const { return m_epochFeeRates.size(); }
```

- [ ] **Step 3: Populate epoch fee rate at epoch boundary**

In `Blockchain.cpp`, at the epoch boundary (where `generateEpochReport` is called, ~line 3456), pass fee data:

```cpp
// At epoch boundary, snapshot the fee rate
uint64_t feeRate = 0;
if (m_totalCdLocked > 0) {
  feeRate = (m_currentEpochSwapFees * CryptoNote::parameters::FEE_POOL_RATE_PRECISION)
          / m_totalCdLocked;
}
m_commitmentIndex.recordEpochFeeRate(epochNumber, feeRate,
    m_currentEpochSwapFees, m_totalCdLocked);
m_currentEpochSwapFees = 0;  // reset for next epoch
```

- [ ] **Step 4: Implement CommitmentIndex methods**

In `CommitmentIndex.cpp`:

```cpp
void CommitmentIndex::recordEpochFeeRate(uint64_t epochNumber, uint64_t feeRate,
                                          uint64_t feesCollected, uint64_t totalLocked) {
  if (epochNumber >= m_epochFeeRates.size()) {
    m_epochFeeRates.resize(epochNumber + 1, 0);
  }
  m_epochFeeRates[epochNumber] = feeRate;
}

uint64_t CommitmentIndex::getEpochFeeRate(uint64_t epochNumber) const {
  if (epochNumber >= m_epochFeeRates.size()) return 0;
  return m_epochFeeRates[epochNumber];
}
```

- [ ] **Step 5: Verify build and commit**

```bash
cmake --build /home/ar/fuego/build -j$(nproc) 2>&1 | tail -20
git add src/CryptoNoteCore/CommitmentIndex.h src/CryptoNoteCore/CommitmentIndex.cpp src/CryptoNoteCore/Blockchain.cpp
git commit -m "consensus: per-epoch fee rate tracking in CommitmentIndex"
```

---

## Task 5: Implement calculateInterest() from Fee Pool

**Files:**
- Modify: `src/CryptoNoteCore/Currency.h:254`
- Modify: `src/CryptoNoteCore/Currency.cpp:283-309`

- [ ] **Step 1: Update calculateInterest() signature**

In `Currency.h`, update the declaration:

```cpp
// Fee-pool-aware interest: returns accrued interest from swap fees for a CD
// locked from creationHeight to currentHeight.
uint64_t calculateInterest(uint64_t amount, uint32_t creationHeight,
                            uint32_t currentHeight,
                            const CommitmentIndex& commitmentIndex) const;

// Legacy signature (returns 0, for backward compatibility with MultisignatureInput callers)
uint64_t calculateInterest(uint64_t amount, uint32_t term, uint32_t height) const;
```

- [ ] **Step 2: Implement fee-pool-aware calculateInterest()**

In `Currency.cpp`, add new overload:

```cpp
uint64_t Currency::calculateInterest(uint64_t amount, uint32_t creationHeight,
                                      uint32_t currentHeight,
                                      const CommitmentIndex& commitmentIndex) const {
  if (currentHeight <= creationHeight) return 0;

  uint64_t startEpoch = creationHeight / parameters::EPOCH_DURATION_BLOCKS;
  uint64_t endEpoch = currentHeight / parameters::EPOCH_DURATION_BLOCKS;
  uint64_t epochCount = commitmentIndex.getEpochCount();

  uint64_t interest = 0;
  for (uint64_t e = startEpoch; e <= endEpoch && e < epochCount; ++e) {
    uint64_t epochRate = commitmentIndex.getEpochFeeRate(e);
    // interest += amount * epochRate / RATE_PRECISION
    // amount * epochRate fits 64-bit: max 8e9 * 1e6 = 8e15 < 2^63
    // For multi-epoch, running sum is safe up to ~18e18
    interest += (amount * epochRate) / parameters::FEE_POOL_RATE_PRECISION;
  }

  return interest;
}
```

- [ ] **Step 3: Keep legacy overload returning 0**

The existing `calculateInterest(amount, term, height)` stays as-is (returns 0 for MultisignatureInput callers).

- [ ] **Step 4: Verify build and commit**

```bash
cmake --build /home/ar/fuego/build -j$(nproc) 2>&1 | tail -20
git add src/CryptoNoteCore/Currency.h src/CryptoNoteCore/Currency.cpp
git commit -m "consensus: implement fee-pool-aware calculateInterest()"
```

---

## Task 6: Validate CommitmentTransfer Inputs

**Files:**
- Modify: `src/CryptoNoteCore/Blockchain.h` (declare method)
- Modify: `src/CryptoNoteCore/Blockchain.cpp:2360+` (implement)

- [ ] **Step 1: Declare checkCommitmentTransferInput()**

In `Blockchain.h`, in the private section near `checkCommitmentSpendInput`:

```cpp
bool checkCommitmentTransferInput(const TransactionInputCommitmentTransfer& txin,
                                   const Crypto::Hash& tx_prefix_hash,
                                   const std::vector<Crypto::Signature>& sig,
                                   uint32_t* pmax_related_block_height);
```

- [ ] **Step 2: Implement validation**

In `Blockchain.cpp`, add after `checkCommitmentSpendInput()`:

```cpp
bool Blockchain::checkCommitmentTransferInput(
    const TransactionInputCommitmentTransfer& txin,
    const Crypto::Hash& tx_prefix_hash,
    const std::vector<Crypto::Signature>& sig,
    uint32_t* pmax_related_block_height) {

  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);

  // Subgroup check (same as CommitmentSpend)
  static const Crypto::KeyImage I = { /* identity point bytes */ };
  static const Crypto::KeyImage L = { /* L point bytes */ };
  if (!(scalarmultKey(txin.keyImage, L) == I)) {
    logger(ERROR) << "CommitmentTransfer key image not in valid Ed25519 domain";
    return false;
  }

  // Resolve commitment outputs for this amount
  auto it = m_commitmentOutputs.find(txin.amount);
  if (it == m_commitmentOutputs.end()) {
    logger(INFO) << "CommitmentTransfer: no commitment outputs for amount " << txin.amount;
    return false;
  }
  const auto& amountRefs = it->second;

  // Decode relative offsets to absolute indices
  std::vector<uint64_t> absoluteIndexes;
  absoluteIndexes.reserve(txin.outputIndexes.size());
  uint64_t absoluteIndex = 0;
  for (uint32_t relIdx : txin.outputIndexes) {
    absoluteIndex += relIdx;
    absoluteIndexes.push_back(absoluteIndex);
  }

  // Collect ring keys — NO maturity requirement for transfers
  // (the CD stays locked, so early "withdrawal" doesn't apply)
  std::vector<const Crypto::PublicKey*> ringKeys;
  ringKeys.reserve(absoluteIndexes.size());
  for (uint64_t absIdx : absoluteIndexes) {
    if (absIdx >= amountRefs.size()) {
      logger(INFO) << "CommitmentTransfer: index " << absIdx << " out of range";
      return false;
    }
    const CommitmentOutputRef& ref = amountRefs[absIdx];

    // Slashed outputs cannot be transferred
    if (ref.isSlashed) {
      logger(INFO) << "CommitmentTransfer: ring member at " << absIdx << " is slashed";
      return false;
    }

    ringKeys.push_back(&ref.commitKey);

    if (pmax_related_block_height) {
      uint32_t blockHeight = ref.transactionIndex.block;
      if (*pmax_related_block_height < blockHeight)
        *pmax_related_block_height = blockHeight;
    }
  }

  if (ringKeys.size() != sig.size()) {
    logger(ERROR) << "CommitmentTransfer: ring size mismatch";
    return false;
  }

  return Crypto::check_ring_signature(tx_prefix_hash, txin.keyImage, ringKeys, sig.data());
}
```

- [ ] **Step 3: Wire into transaction validation**

In the main transaction validation function (where `checkCommitmentSpendInput` is called), add a case for `TransactionInputCommitmentTransfer`:

```cpp
else if (txin.type() == typeid(TransactionInputCommitmentTransfer)) {
  const auto& commitTransfer = boost::get<TransactionInputCommitmentTransfer>(txin);
  if (!checkCommitmentTransferInput(commitTransfer, tx_prefix_hash,
                                     tx.signatures[inputIndex],
                                     pmax_related_block_height)) {
    logger(INFO) << "Transaction uses invalid commitment transfer input";
    return false;
  }
}
```

- [ ] **Step 4: Add transfer-specific validation rules**

After ring sig verification, validate the transaction as a whole:
- Must have exactly one `TransactionOutputCommitment` at the same amount
- The new CD's term must be > 0 (remaining term)
- Key image must not already exist in `m_spent_keys`

- [ ] **Step 5: Verify build and commit**

```bash
cmake --build /home/ar/fuego/build -j$(nproc) 2>&1 | tail -20
git add src/CryptoNoteCore/Blockchain.h src/CryptoNoteCore/Blockchain.cpp
git commit -m "consensus: implement checkCommitmentTransferInput() validation"
```

---

## Task 7: Swap Fee Deduction from Claimer's Output

**Files:**
- Modify: `src/CryptoNoteCore/Blockchain.cpp` (HTLC claim/refund validation)
- Modify: `src/CryptoNoteCore/Currency.cpp` (getTransactionFee to account for swap fee)

**Design:** Swap fee is deducted from the **claimer's output**, NOT from the miner reward. An HTLC claim tx's outputs must total at most `claim_amount - swap_fee`. The miner still gets the standard tx fee (inputs - outputs - swap_fee). This avoids miner censorship incentive.
make it a 1% fee
```
HTLC output:      100.0000000 XFG
Claimer receives:   99.7000000 XFG (in outputs)
Swap fee to pool:    0.3000000 XFG
Miner tx fee:        0.0008000 XFG (standard MINIMUM_FEE, separate)
```

- [ ] **Step 1: Validate HTLC claim/refund output balance includes swap fee**

In the HTLC claim/refund validation path in `Blockchain.cpp`, enforce that the claimer's outputs are reduced by the swap fee:

```cpp
// For HTLC claim transactions:
uint64_t swapFee = computeSwapFee(claimAmount);
uint64_t maxClaimerOutput = claimAmount - swapFee;
// sum(outputs) + tx_fee must equal claimAmount
// so sum(outputs) <= maxClaimerOutput (remaining is tx_fee to miner)
```

- [ ] **Step 2: Update getTransactionFee() to account for swap fee**

In `Currency.cpp`, the fee calculation for HTLC transactions must subtract the swap fee from the "missing" amount (inputs - outputs), so the miner doesn't get the swap fee:

```cpp
// For transactions with HTLC inputs:
// fee = sum(inputs) - sum(outputs) - sum(swap_fees)
// The swap_fee goes to the fee pool, not the miner
```

- [ ] **Step 3: Verify build and commit**

```bash
cmake --build /home/ar/fuego/build -j$(nproc) 2>&1 | tail -20
git add src/CryptoNoteCore/Blockchain.cpp src/CryptoNoteCore/Currency.cpp
git commit -m "consensus: swap fee from claimer output into fee pool (not miner)"
```

---

## Task 8: Interest Payout — Declare and Verify Model

**Files:**
- Modify: `src/CryptoNoteCore/Blockchain.cpp` (CommitmentSpend validation)
- Modify: `src/CryptoNoteCore/Currency.cpp` (getTransactionFee to account for claimedInterest)

**Design:** "Declare and verify" — the spender declares `claimedInterest` in the `TransactionInputCommitmentSpend` input. The validator:
1. Computes `max_possible_interest` using the **oldest ring member's** creation epoch as earliest possible start
2. Checks: `claimedInterest <= max_possible_interest AND claimedInterest <= m_feePoolBalance`
3. Transaction balance: `sum(outputs) + tx_fee == sum(input_amounts) + sum(claimedInterest_per_input)`

The ring sig hides which member is real. The rational spender knows their own creation height and declares exact interest — no incentive to over-claim (rejected) or under-claim (leaving money).

- [ ] **Step 1: Validate claimedInterest in CommitmentSpend**

In `checkCommitmentSpendInput()` (or after it in the tx validation path):

```cpp
// Compute max possible interest from oldest ring member
uint32_t oldestRingEpoch = UINT32_MAX;
for (uint64_t absIdx : absoluteIndexes) {
  const auto& ref = amountRefs[absIdx];
  uint32_t creationEpoch = ref.transactionIndex.block / parameters::EPOCH_DURATION_BLOCKS;
  oldestRingEpoch = std::min(oldestRingEpoch, creationEpoch);
}
uint64_t maxInterest = m_currency.calculateInterest(
    txin.amount, oldestRingEpoch * parameters::EPOCH_DURATION_BLOCKS,
    currentHeight, m_commitmentIndex);
maxInterest = std::min(maxInterest, m_feePoolBalance);

if (txin.claimedInterest > maxInterest) {
  logger(INFO) << "CommitmentSpend: claimedInterest " << txin.claimedInterest
               << " exceeds max " << maxInterest;
  return false;
}
```

- [ ] **Step 2: Adjust transaction balance to include claimedInterest**

In `getTransactionFee()` or the balance validation:

```cpp
// For CommitmentSpend inputs: effective input = amount + claimedInterest
// fee = sum(effective_inputs) - sum(outputs)
// fee must be >= MINIMUM_FEE
```

- [ ] **Step 3: Deduct claimedInterest from fee pool in pushTransaction**

```cpp
if (input.type() == typeid(TransactionInputCommitmentSpend)) {
  const auto& spend = boost::get<TransactionInputCommitmentSpend>(input);
  if (spend.claimedInterest > 0) {
    m_feePoolBalance -= spend.claimedInterest;
  }
}
```

And reverse in `popTransaction()`:
```cpp
if (input.type() == typeid(TransactionInputCommitmentSpend)) {
  const auto& spend = boost::get<TransactionInputCommitmentSpend>(input);
  if (spend.claimedInterest > 0) {
    m_feePoolBalance += spend.claimedInterest;
  }
}
```

- [ ] **Step 4: Verify build and commit**

```bash
cmake --build /home/ar/fuego/build -j$(nproc) 2>&1 | tail -20
git add src/CryptoNoteCore/Blockchain.cpp src/CryptoNoteCore/Currency.cpp
git commit -m "consensus: declare-and-verify interest payout for CommitmentSpend"
```

---

## Task 9: Wallet — transfer_cd Command

**Files:**
- Modify: `src/SimpleWallet/SimpleWallet.h`
- Modify: `src/SimpleWallet/SimpleWallet.cpp`
- Modify: `src/WalletLegacy/WalletTransactionSender.cpp`

- [ ] **Step 1: Add transfer_cd command registration**

In `SimpleWallet.cpp`, in the command registration section, add:

```cpp
m_consoleHandler.setHandler("transfer_cd",
    boost::bind(&SimpleWallet::transfer_cd, this, boost::arg<1>()),
    "Transfer a commitment deposit to a new owner. Usage: transfer_cd <commitment_index> <recipient_address>");
```

- [ ] **Step 2: Implement transfer_cd()**

```cpp
bool SimpleWallet::transfer_cd(const std::vector<std::string>& args) {
  if (args.size() < 2) {
    fail_msg_writer() << "Usage: transfer_cd <commitment_index> <recipient_address>";
    return true;
  }

  uint32_t commitIdx = std::stoul(args[0]);
  std::string recipientAddr = args[1];

  // 1. Look up the CD by index
  // 2. Derive the new owner's commitKey from their address
  // 3. Compute remaining term
  // 4. Construct transaction with CommitmentTransfer input + new Commitment output
  // 5. Sign with ring signature
  // 6. Broadcast

  success_msg_writer() << "CD #" << commitIdx << " transferred to " << recipientAddr;
  return true;
}
```

- [ ] **Step 3: Add transaction construction in WalletTransactionSender**

In `WalletTransactionSender.cpp`, add a `makeCommitmentTransferRequest()` method that:
- Selects ring members from the commitment pool (via RPC, same as withdrawal)
- Constructs `TransactionInputCommitmentTransfer`
- Produces `TransactionOutputCommitment` with new commitKey and remaining term
- Signs with ring signature

- [ ] **Step 4: Verify build and commit**

```bash
cmake --build /home/ar/fuego/build -j$(nproc) 2>&1 | tail -20
git add src/SimpleWallet/SimpleWallet.h src/SimpleWallet/SimpleWallet.cpp src/WalletLegacy/WalletTransactionSender.cpp
git commit -m "wallet: add transfer_cd command for CD ownership transfer"
```

---

## Task 10: Wallet — redeem_cd and Auto-Rollover

**Files:**
- Modify: `src/SimpleWallet/SimpleWallet.cpp`

- [ ] **Step 1: Add redeem_cd command**

```cpp
m_consoleHandler.setHandler("redeem_cd",
    boost::bind(&SimpleWallet::redeem_cd, this, boost::arg<1>()),
    "Redeem a mature CD to liquid XFG + interest. Usage: redeem_cd <commitment_index>");
```

Implementation uses existing `CommitmentSpend` logic but displays interest earned.

- [ ] **Step 2: Add auto-rollover prompt**

When the wallet detects a mature CD (during refresh), prompt:

```
CD #47 matured: 100.0000000 XFG + 0.3000000 XFG interest
  [R] Redeem to liquid XFG  [A] Auto-roll (re-lock for same term)  [S] Skip
```

Default action (if no response / daemon mode): auto-roll.

- [ ] **Step 3: Implement auto-roll transaction**

Auto-roll = CommitmentSpend (redeem) + new deposit tx in one step:
1. Redeem CD → receive principal + interest as liquid XFG
2. Immediately create new CD with (principal + interest), same term
3. Both transactions broadcast together

- [ ] **Step 4: Verify build and commit**

```bash
cmake --build /home/ar/fuego/build -j$(nproc) 2>&1 | tail -20
git add src/SimpleWallet/SimpleWallet.cpp
git commit -m "wallet: add redeem_cd with auto-rollover prompt"
```

---

## Task 11: RPC Endpoints

**Files:**
- Modify: `src/Rpc/CoreRpcServerCommandsDefinitions.h`
- Modify: `src/Rpc/RpcServer.h`
- Modify: `src/Rpc/RpcServer.cpp`

- [ ] **Step 1: Define RPC structs**

```cpp
// Get fee pool state
struct COMMAND_RPC_GET_FEE_POOL {
  struct request { void serialize(ISerializer& s) {} };
  struct response {
    uint64_t fee_pool_balance;
    uint64_t current_epoch_fees;
    uint64_t total_cd_locked;
    uint64_t current_epoch_number;
    uint64_t current_epoch_rate;
    std::string status;
    void serialize(ISerializer& s) {
      KV_MEMBER(fee_pool_balance)
      KV_MEMBER(current_epoch_fees)
      KV_MEMBER(total_cd_locked)
      KV_MEMBER(current_epoch_number)
      KV_MEMBER(current_epoch_rate)
      KV_MEMBER(status)
    }
  };
};

// Get CD info with accrued interest
struct COMMAND_RPC_GET_CD_INFO {
  struct request {
    uint32_t commitment_index;
    uint64_t amount;
    void serialize(ISerializer& s) {
      KV_MEMBER(commitment_index)
      KV_MEMBER(amount)
    }
  };
  struct response {
    uint64_t amount;
    uint32_t term;
    uint32_t creation_height;
    uint32_t maturity_height;
    uint64_t accrued_interest;
    bool is_mature;
    bool is_spent;
    std::string status;
    void serialize(ISerializer& s) {
      KV_MEMBER(amount)
      KV_MEMBER(term)
      KV_MEMBER(creation_height)
      KV_MEMBER(maturity_height)
      KV_MEMBER(accrued_interest)
      KV_MEMBER(is_mature)
      KV_MEMBER(is_spent)
      KV_MEMBER(status)
    }
  };
};
```

- [ ] **Step 2: Register endpoints**

In `RpcServer.cpp`:

```cpp
{ "/getfeepool", { jsonMethod<COMMAND_RPC_GET_FEE_POOL>(&RpcServer::on_get_fee_pool), true } },
{ "/getcdinfo",  { jsonMethod<COMMAND_RPC_GET_CD_INFO>(&RpcServer::on_get_cd_info), true } },
```

- [ ] **Step 3: Implement handlers**

```cpp
bool RpcServer::on_get_fee_pool(const COMMAND_RPC_GET_FEE_POOL::request& req,
                                 COMMAND_RPC_GET_FEE_POOL::response& res) {
  res.fee_pool_balance = m_core.getFeePoolBalance();
  res.current_epoch_fees = m_core.getCurrentEpochSwapFees();
  res.total_cd_locked = m_core.getTotalCdLocked();
  // ... epoch number and rate ...
  res.status = CORE_RPC_STATUS_OK;
  return true;
}
```

- [ ] **Step 4: Verify build and commit**

```bash
cmake --build /home/ar/fuego/build -j$(nproc) 2>&1 | tail -20
git add src/Rpc/CoreRpcServerCommandsDefinitions.h src/Rpc/RpcServer.h src/Rpc/RpcServer.cpp
git commit -m "rpc: add /getfeepool and /getcdinfo endpoints"
```

---

## Task 12: Update Swap TUI Clients

**Files:**
- Modify: `xfg-eth-swap/app/rpc.go`
- Modify: `xfg-eth-swap/app/tui.go`
- (Same for xfg-xmr-swap and xfg-bch-swap)

- [ ] **Step 1: Add fee pool RPC call**

```go
type FeePoolResponse struct {
    FeePoolBalance   uint64 `json:"fee_pool_balance"`
    CurrentEpochFees uint64 `json:"current_epoch_fees"`
    TotalCdLocked    uint64 `json:"total_cd_locked"`
    CurrentEpochRate uint64 `json:"current_epoch_rate"`
    Status           string `json:"status"`
}

func (c *FuegoClient) GetFeePool() (*FeePoolResponse, error) {
    var resp FeePoolResponse
    if err := c.post("/getfeepool", nil, &resp); err != nil {
        return nil, err
    }
    return &resp, nil
}
```

- [ ] **Step 2: Display fee pool / CD yield in Info tab**

Add to `renderInfo()`:

```go
// Fee Pool section
lines = append(lines, headerStyle.Render("  CD Fee Pool"))
if feePool != nil {
    yield := float64(feePool.CurrentEpochRate) / 1e6 * 100.0
    lines = append(lines, fmt.Sprintf("  Pool balance:  %.4f XFG", float64(feePool.FeePoolBalance)/1e7))
    lines = append(lines, fmt.Sprintf("  Locked in CDs: %.2f XFG", float64(feePool.TotalCdLocked)/1e7))
    lines = append(lines, fmt.Sprintf("  Current yield: %.4f%% per epoch", yield))
}
```

- [ ] **Step 3: Verify Go builds and commit**

```bash
cd /home/ar/fuego/xfg-eth-swap && go build -o xfg-eth-swap . && cd /home/ar/fuego
git add xfg-eth-swap/ xfg-xmr-swap/ xfg-bch-swap/
git commit -m "tui: display CD fee pool yield in swap clients"
```

---

## Task 13: Testnet Validation

- [ ] **Step 1: Build testnet binaries**

```bash
cmake /home/ar/fuego -B /home/ar/fuego/build -j$(nproc)
cmake --build /home/ar/fuego/build -j$(nproc)
```

- [ ] **Step 2: Start testnet and verify**

Test scenarios:
1. Create a CD (deposit) → verify it appears in commitment pool
2. Create an HTLC → claim it → verify swap fee enters fee pool (`/getfeepool`)
3. Wait for epoch boundary → verify epoch fee rate computed
4. Redeem CD → verify interest = principal × rate × epochs
5. Transfer CD to another address → verify maturity preserved, new owner can redeem
6. Transfer immature CD → verify it works (no maturity requirement)
7. Attempt double-transfer (same key image) → verify rejection
8. Auto-rollover: mature CD → wallet auto-re-locks → verify new CD created

- [ ] **Step 3: Commit test results**

```bash
git commit -m "test: validate transferable CD + fee pool on testnet"
```
