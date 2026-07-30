# Atomic Swap Fee Implementation Plan
## 2% Fee with Bug Fixes

**Version:** 2.0  
**Date:** April 2025  

---

## Current Bugs

### Bug #1: Nothing Feeds `m_currentEpochSwapFees`

The swap fee constant is defined but never used:

```cpp
// CryptoNoteConfig.h - DEFINED
const uint64_t SWAP_FEE_RATE_BPS = 100;  // 1%

// But nowhere in the code does:
m_currentEpochSwapFees += swapFee;  // NEVER HAPPENS
```

**Result:** `m_currentEpochSwapFees` stays at 0 forever.

### Bug #2: Epoch Distribution Doesn't Add to Fee Pool

```cpp
// Blockchain.cpp:3255-3261 - CURRENT CODE (BUGGY)
m_totalSwapFeesCollected += epochSwapFees;  // Just tracking

m_feePoolBalance -= treasuryShare;  // BUG: Subtracts from fee pool!
m_treasuryBalance += treasuryShare;

// cdSwapShare is calculated but NEVER ADDED to m_feePoolBalance
```

**Result:** Fee pool never gets funded. CDs earn no swap-based interest.

---

## Fee Design

### Hybrid 1% + 1% Approach

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         FEE MECHANISM                                    │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  Agreed swap amount: 100 XFG                                            │
│                                                                          │
│  BOB (XFG seller) locks escrow:                                         │
│    - Agreed amount: 100 XFG                                             │
│    - BOB adds 1%: +1 XFG                                                │
│    - Total escrow: 101 XFG                                              │
│                                                                          │
│  ALICE (XFG buyer) claims:                                              │
│    - Receives: 99 XFG (agreed amount minus 1%)                          │
│                                                                          │
│  Fee pool:                                                               │
│    - Escrow (101) - Received (99) = 2 XFG                               │
│    - BOB contributed: 1 XFG (1% added on top)                           │
│    - ALICE contributed: 1 XFG (1% received less)                        │
│                                                                          │
│  At epoch boundary:                                                      │
│    m_feePoolBalance += cdSwapShare (80% = 1.6 XFG)                      │
│    m_treasuryBalance += treasuryShare (20% = 0.4 XFG)                   │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

**Why this hybrid approach:**
- Fair: Both parties contribute 1% to the fee
- BOB (XFG holder) pays by adding extra to escrow
- ALICE (counterparty) pays by receiving slightly less
- No wallet needed: fee pool is pure accounting
- Both sides have skin in the game

---

## Implementation

### Part 1: Fix Epoch Distribution Bug

**File: `src/CryptoNoteCore/Blockchain.cpp`**

Replace lines ~3255-3261:

```cpp
// BEFORE (BUGGY):
m_totalSwapFeesCollected += epochSwapFees;

// Deduct treasury share from fee pool; remainder stays as CD yield
if (treasuryShare > 0 && m_feePoolBalance >= treasuryShare) {
  m_feePoolBalance -= treasuryShare;  // BUG!
  m_treasuryBalance += treasuryShare;
  m_totalTreasuryAccrued += treasuryShare;
}

// AFTER (FIXED):
m_totalSwapFeesCollected += epochSwapFees;

// Add swap fees to pools: 80% CD yield, 20% treasury
if (epochSwapFees > 0) {
  m_feePoolBalance += cdSwapShare;      // 80% to CD interest pool
  m_treasuryBalance += treasuryShare;   // 20% to treasury
  m_totalTreasuryAccrued += treasuryShare;
}
```

### Part 2: Add Fee Calculation Function

**File: `src/CryptoNoteCore/Currency.h`**

```cpp
// Calculate atomic swap fee (2% of amount)
uint64_t calculateSwapFee(uint64_t swapAmount) const;
```

**File: `src/CryptoNoteCore/Currency.cpp`**

```cpp
uint64_t Currency::calculateSwapFee(uint64_t swapAmount) const {
  if (swapAmount == 0) return 0;
  
  uint64_t feeRate = m_testnet 
    ? parameters::TESTNET_SWAP_FEE_RATE_BPS 
    : parameters::SWAP_FEE_RATE_BPS;
  
  // fee = swapAmount * feeRate / 10000
  __uint128_t fee = (__uint128_t)swapAmount * feeRate / parameters::SWAP_FEE_RATE_DIVISOR;
  
  return static_cast<uint64_t>(fee);
}
```

### Part 3: Detect Swap Claim and Add Fee to Accumulator

**File: `src/CryptoNoteCore/Blockchain.cpp`**

Add to `pushTransaction()` or `pushBlock()`:

```cpp
// Check if this is a swap claim transaction
// Swap claims spend from Musig2 escrow addresses
bool Blockchain::checkSwapClaimFee(const Transaction& tx, uint64_t& swapFee) {
  swapFee = 0;
  
  // Heuristic: Check if transaction spends from a known escrow
  // A swap claim transaction:
  // 1. Has exactly one input (the escrow)
  // 2. Input amount > output amount (fee gap)
  // 3. Spends from Musig2 aggregated key
  
  if (tx.inputs.size() != 1) return true;  // Not a swap claim
  
  const auto& in = tx.inputs[0];
  if (in.type() != typeid(KeyInput)) return true;
  
  const auto& keyIn = boost::get<KeyInput>(in);
  
  // Calculate total outputs
  uint64_t totalOutputs = 0;
  for (const auto& out : tx.outputs) {
    totalOutputs += out.amount;
  }
  
  // Fee gap = input amount - output amount
  uint64_t feeGap = keyIn.amount - totalOutputs;
  
  // Check if fee gap matches expected swap fee
  uint64_t expectedFee = m_currency.calculateSwapFee(totalOutputs);
  
  if (feeGap >= expectedFee) {
    // This looks like a swap claim with valid fee
    swapFee = feeGap;
    return true;
  }
  
  // If there's a fee gap but doesn't match expected, still track it
  // (allows for transaction fees on top of swap fee)
  if (feeGap > 0) {
    // Check if this is likely a swap (spending from escrow)
    // For now, assume any significant fee gap from single-input tx is a swap
    swapFee = feeGap;
  }
  
  return true;
}
```

**Integrate into `pushBlock()`:**

```cpp
bool Blockchain::pushBlock(...) {
  // ... existing code ...
  
  for (const auto& tx : txs) {
    uint64_t swapFee = 0;
    if (checkSwapClaimFee(tx, swapFee) && swapFee > 0) {
      m_currentEpochSwapFees += swapFee;
      logger(DEBUGGING) << "Swap fee collected: " << swapFee;
    }
  }
  
  // ... rest of pushBlock ...
}
```

### Part 4: Update Swap Fee Rate

**File: `src/CryptoNoteConfig.h`**

```cpp
// Atomic swap fee: 2% of claim amount
const uint64_t SWAP_FEE_RATE_BPS = 200;              // 2% (200 basis points)
const uint64_t SWAP_FEE_RATE_DIVISOR = 10000;
const uint64_t TESTNET_SWAP_FEE_RATE_BPS = 200;      // Same for testnet
```

### Part 5: Update Swap Initiation (BOB adds 1%)

**File: `src/SwapDaemon/SwapDaemon.cpp`**

When BOB initiates a swap (locks XFG to escrow), add 1% to the escrow amount:

```cpp
bool SwapDaemon::initiate(const std::string& swapId, SwapParams& params) {
  // ... existing validation ...
  
  // BOB adds 1% to escrow for fee contribution
  // Agreed amount is what ALICE expects to receive (minus her 1%)
  // BOB locks: agreedAmount + 1%
  uint64_t bobFeeShare = params.agreedAmount / 100;  // 1%
  params.escrowAmount = params.agreedAmount + bobFeeShare;
  
  // For a 100 XFG swap:
  // - agreedAmount = 100 XFG
  // - bobFeeShare = 1 XFG
  // - escrowAmount = 101 XFG (BOB locks this)
  
  m_logger(INFO) << "Swap fee structure:";
  m_logger(INFO) << "  Agreed amount: " << params.agreedAmount;
  m_logger(INFO) << "  BOB adds 1%: " << bobFeeShare;
  m_logger(INFO) << "  Escrow amount: " << params.escrowAmount;
  m_logger(INFO) << "  ALICE will receive: " << (params.agreedAmount - bobFeeShare);
  
  // ... rest of initiation ...
}
```

### Part 6: Update Claim Transaction (ALICE's 1%)

**File: `src/SwapDaemon/SwapTxBuilder.cpp`**

Modify `buildUnsignedEscrowSpend()`:

```cpp
bool SwapTxBuilder::buildUnsignedEscrowSpend(
   FuegoRpcClient& rpc,
   const SwapParams& params,
   const Crypto::PublicKey& destinationKey,
   uint64_t tx_fee,  // Network transaction fee
   CryptoNote::Transaction& tx,
   Crypto::Hash& prefixHash,
   CollaborativeRingState& ringState) {

 // Hybrid fee: BOB added 1% when locking, ALICE receives 1% less
 // escrowAmount = agreedAmount + 1% (BOB's contribution already in escrow)
 // ALICE receives = agreedAmount - 1% (her contribution)
 // Total fee gap = escrow - received = 2%
  
 uint64_t onePercentFee = params.agreedAmount / 100;  // 1% of agreed
  
 // ALICE's output = agreed amount minus her 1% share and tx fee
 uint64_t aliceOutput = params.agreedAmount - onePercentFee - tx_fee;
  
 if (params.escrowAmount <= aliceOutput + tx_fee) return false;
  
 // ... existing ring signature code ...
  
 // Input: escrow amount (agreedAmount + BOB's 1% = 101 XFG for 100 XFG swap)
 input.amount = params.escrowAmount;
  
 // Output: ALICE receives agreed minus her 1% (99 XFG for 100 XFG swap)
 // Fee gap of 2 XFG stays in blockchain as accounting entry
 output.amount = aliceOutput;
  
 // Fee accounting happens automatically:
 // m_currentEpochSwapFees += (escrowAmount - aliceOutput - tx_fee)
 // = (agreed + 1%) - (agreed - 1%) - tx_fee = 2% of agreed
}
```

---

### Part 7: Update RPC for Fee Transparency

**File: `src/Rpc/CoreRpcServerCommandsDefinitions.h`**

Add fee fields to SwapStatus response:

```cpp
struct SwapStatus {
  // ... existing fields ...
  uint64_t agreedAmount;     // The agreed swap amount (before fees)
  uint64_t escrowAmount;     // What BOB locked (agreed + 1%)
  uint64_t receivedAmount;   // What ALICE receives (agreed - 1%)
  uint64_t bobFeeShare;      // BOB's 1% contribution
  uint64_t aliceFeeShare;    // ALICE's 1% contribution
  uint64_t totalSwapFee;     // Total fee (2%)
  // ...
};
```

**Note for swapxfg TUI:** The TUI is a Go-based display layer that fetches data via JSON-RPC. It doesn't calculate fees - it just displays what the daemon reports. To show fee transparency:

1. Update `swapxfg/app/rpc.go` `SwapStatus` struct to include fee fields
2. TUI will automatically display them when showing swap details
3. Users will see: "Agreed: 100 XFG, Escrow: 101 XFG, You receive: 99 XFG, Fee: 2 XFG"

The TUI already has no fee calculation logic (confirmed by grep search), so no changes needed there.

---

## Testing

### Unit Tests

```cpp
// tests/UnitTests/SwapFeeTests.cpp

TEST(SwapFee, CalculatesCorrectFee) {
  Currency currency = createTestCurrency();
  
  // 100 XFG = 1,000,000,000 atomic units
  // 2% fee = 20,000,000 atomic units = 2 XFG
  uint64_t fee = currency.calculateSwapFee(1000000000);
  EXPECT_EQ(fee, 20000000);
}

TEST(SwapFee, ZeroAmountReturnsZero) {
  Currency currency = createTestCurrency();
  EXPECT_EQ(currency.calculateSwapFee(0), 0);
}

TEST(SwapFee, EpochDistributionAddsToFeePool) {
  Blockchain blockchain = createTestBlockchain();
  
  // Simulate swap fee accumulation
  blockchain.m_currentEpochSwapFees = 100000000;  // 10 XFG in fees
  
  // Process epoch boundary
  blockchain.processEpochBoundary();
  
  // Check distribution: 80% to fee pool, 20% to treasury
  uint64_t expectedFeePool = 80000000;   // 8 XFG
  uint64_t expectedTreasury = 20000000;  // 2 XFG
  
  EXPECT_EQ(blockchain.getFeePoolBalance(), expectedFeePool);
  EXPECT_EQ(blockchain.getTreasuryBalance(), expectedTreasury);
}
```

### Integration Test

```cpp
TEST(SwapFeeIntegration, FullSwapFlowWithFee) {
  // Setup
  Blockchain blockchain = createTestBlockchain();
  Wallet bobWallet = createWallet(blockchain, 1000 * COIN);  // BOB has 1000 XFG
  Wallet aliceWallet = createWallet(blockchain, 0);
  
  uint64_t agreedAmount = 100 * COIN;  // 100 XFG swap
  
  // BOB initiates swap (locks 100 XFG to escrow)
  Transaction lockTx = bobWallet.initiateSwap(agreedAmount);
  blockchain.addTransaction(lockTx);
  
  // ALICE claims (receives 98 XFG, 2 XFG goes to fee pool)
  Transaction claimTx = aliceWallet.claimSwap(agreedAmount);
  blockchain.addTransaction(claimTx);
  
  // Verify ALICE received 98 XFG
  EXPECT_EQ(aliceWallet.getBalance(), 98 * COIN);
  
  // Verify fee pool has 2 XFG (after epoch boundary)
  blockchain.advanceToEpochBoundary();
  EXPECT_EQ(blockchain.getFeePoolBalance(), 160000000);  // 80% of 2 XFG
  EXPECT_EQ(blockchain.getTreasuryBalance(), 40000000);  // 20% of 2 XFG
}
```

---

## Summary of Changes

| Part | File | Change |
|------|------|--------|
| 1 | `Blockchain.cpp` | Fix epoch distribution to ADD fees, not subtract |
| 2 | `Currency.h/cpp` | Add `calculateSwapFee()` function |
| 3 | `Blockchain.cpp` | Add swap claim detection in `pushBlock()` |
| 4 | `CryptoNoteConfig.h` | Update `SWAP_FEE_RATE_BPS` to 200 (2%) |
| 5 | `SwapDaemon.cpp` | BOB adds 1% when initiating swap (escrow = agreed + 1%) |
| 6 | `SwapTxBuilder.cpp` | ALICE receives agreed - 1% (output calculation) |
| 7 | `CoreRpcServerCommandsDefinitions.h` | Add fee fields to SwapStatus for TUI display |

---

## Fee Flow Diagram

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         SWAP FEE FLOW                                    │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  SWAP INITIATION                                                         │
│  ────────────────                                                        │
│    Agreed: 100 XFG                                                       │
│    BOB locks 101 XFG (agreed + 1%) → Musig2 Escrow Address              │
│    BOB's contribution: 1 XFG (1%)                                        │
 │                                                                          │
│  SWAP CLAIM (when ALICE reveals adaptor secret)                         │
│  ─────────────────────────────────────────────────────                  │
│    Input:  101 XFG (from escrow)                                        │
│    Output: 99 XFG (to ALICE = agreed - 1%)                              │
│    Gap:   2 XFG (swap fee = BOB's 1% + ALICE's 1%)                      │
 │                                                                          │
│  ACCOUNTING (during claim transaction processing)                        │
│  ─────────────────────────────────────────────────────                  │
│    m_currentEpochSwapFees += 2 XFG                                      │
 │                                                                          │
│  EPOCH BOUNDARY (every 900 blocks)                                       │
│  ─────────────────────────────────────                                   │
│    m_feePoolBalance += 1.6 XFG (80% for CD interest)                    │
│    m_treasuryBalance += 0.4 XFG (20% for treasury)                      │
│    m_currentEpochSwapFees = 0 (reset)                                   │
 │                                                                          │
│  CD REDEMPTION (when CD matures)                                         │
│  ─────────────────────────────────────                                   │
│    CD holder receives: principal + interest                             │
│    Interest calculated from: epoch fee rates × CD lock duration         │
│    m_feePoolBalance -= interestPaid                                     │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Rollback Support

The existing `popBlock()` code already handles fee rollback via `m_blockSwapFeeContributions`:

```cpp
void Blockchain::popBlock() {
  // ... 
  
  if (!m_blockSwapFeeContributions.empty()) {
    uint64_t contribution = m_blockSwapFeeContributions.back();
    m_blockSwapFeeContributions.pop_back();
    
    if (poppedHeight % epochDuration == 0) {
      // Epoch boundary: restore full epoch accumulator
      m_currentEpochSwapFees += contribution;
      m_totalSwapFeesCollected -= contribution;
      m_commitmentIndex.popEpochFeeRate();
    } else {
      // Regular block: subtract contribution
      m_currentEpochSwapFees -= contribution;
    }
  }
}
```

This works correctly as long as we properly track contributions in `m_blockSwapFeeContributions`.

---

## Timeline

| Task | Effort |
|------|--------|
| Part 1: Fix epoch distribution bug | 2 hours |
| Part 2: Add fee calculation function | 1 hour |
| Part 3: Add swap claim detection | 4 hours |
| Part 4: Update fee rate constant | 0.5 hours |
| Part 5: Update SwapDaemon initiation | 2 hours |
| Part 6: Update SwapTxBuilder claim | 2 hours |
| Part 7: Update RPC for fee transparency | 1 hour |
| Unit tests | 4 hours |
| Integration tests | 4 hours |
| Testing & debugging | 4 hours |
| **Total** | ~24.5 hours |

---

*End of Document*