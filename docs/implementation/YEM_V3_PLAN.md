# YEM v3 — Master Implementation Plan & Dev Guide

> Supersedes `YEM_IMPLEMENTATION_PLAN.md` from 2026-05-22 and `yem_implementation_spec.md`.

---

## 1. Architecture Overview

YEM replaces the single-stream CD yield model (`80% swap fees → CD pool → one-size-fits-all`) with a smoothed, SWF-buffered distribution engine funded by three income streams.

### Income Streams

| Stream | Source | Timing | Recipient |
|---|---|---|---|
| Swap fee surplus | 60% of excess above rolling-avg target | Per epoch | SWF |
| Burn scalp | 8% of XFG burned in HEAT mint transactions | Per block | YEM Reserve |
| LP yield feed | 75% of treasury LP earnings | Per epoch | SWF |

### Payout Channels

| Payment | Mechanism | Source |
|---|---|---|
| CD yield (smoothed) | Fee pool credit, recorded in CommitmentIndex | Swap fees |
| SWF drip | Fee pool credit, proportional to lock amount | SWF |
| Bond coupons | Coinbase output to bondholder address | YEM Reserve |
| Bond maturity | Coinbase output to bondholder address | SWF + YEM Reserve |
| Deficit coverage | Coinbase output to fee pool | YEM Reserve |

### Single CD Type

All new CDs are YEM CDs. One product. One engine. The "cold start" is bridged by legacy-only bonds (pre-v10 COLD deposit migration).

---

## 2. Rate Model

```
CD annualized yield = min(rolling_avg_organic_rate, tier_cap(lock_duration))
                    + swf_drip_yield
```

### 2a. Organic Rate

```
organic_rate = epochCdShare / totalCdLocked
```

Computed identically to today's fee rate, but not passed directly to CD holders. It feeds the rolling average.

### 2b. Rolling Average

```
target_rate = mean(organic_rates[epoch-3], organic_rates[epoch-2], organic_rates[epoch-1])
```

Three-epoch window, computed each epoch boundary. The first 3 epochs (lag period) accumulate 100% of CD pool into the SWF with zero distribution — building the initial reserve and ensuring the first target has 3 data points.

### 2c. Time-Tiered Caps

```
cap(d) = 33% + (min(d, 72) - 1) / 71 × 47%
```

| Lock (epochs) | Duration | Cap (annualized) |
|---|---|---|
| 1–9 | ~3 days – 1.5 months | ~35% |
| 10–30 | ~1.5 – 5 months | ~45% |
| 31–71 | ~5 months – 1 year | ~66% |
| 72+ | 1+ year | 80% |

`d` is the `CommitmentEntry.term` field (already exists). No new storage.

### 2d. SWF Drip

```
drip_yield = (swfBalance × 0.01 / totalCdLocked)
per_cd_drip = amount × drip_yield
```

1% of total SWF balance distributed each epoch to all CD holders proportional to locked amount. Example: SWF = 371K, CD locked = 1.4B → drip adds ~0.19% to annual APY. This is the "dividend" — accumulated protocol profits paid continuously.

### 2e. Surplus/Deficit Smoothing

```
surplus = epochCdShare - (target_rate × totalCdLocked)
if surplus > 0:  swfBalance += surplus × 0.60   (save for lean epochs)
if deficit > 0:
    draw swfBalance first
    if swfBalance exhausted: draw YEM Reserve → coinbase → fee pool
    if reserve exhausted: draw treasury (0.1% of treasuryBalance, absolute floor)
```

---

## 3. Burn Scalp

### 3a. Mechanism

8% of every XFG burn in HEAT mint transactions is diverted. These 8% are NEITHER destroyed (not counted in EternalFlame) NOR created as spendable coins (no output produced). They exist as a paper credit in `m_yemState.yemReserve` — a deferred emission right.

### 3b. Interception

**File:** `src/CryptoNoteCore/Blockchain.cpp`, function `pushToBankingIndex()`.

Between the per-transaction accumulation loop (~line 3527) and the `addForeverDeposit()` commit (~line 3892):

```cpp
uint64_t scalped = (permanentBurns * YEM_BURN_SCALP_BPS) / 10000;
if (scalped > 0 && m_yemState.lagComplete) {
    m_yemState.yemReserve += scalped;
    permanentBurns -= scalped;
    m_blockBurnScalpContributions.push_back({block.height, scalped});
}
// THEN: m_bankingIndex.addForeverDeposit(permanentBurns, block.height);
```

Only activates post-lag. No scalp during the 3-epoch lag period — all burn goes to EternalFlame normally while the system boots.

### 3c. Emission Math

```
Without scalp:  burn 100 → EF += 100 → emission_capacity += 100/(2^20)/block
With 8% scalp:  burn 100 → EF += 92  → emission_capacity += 92/(2^20)/block
                          → reserve += 8 → coinbase can mint up to 8 total
```

Total coins entering circulation: identical. The 8 XFG flows through coinbase payouts instead of trickling through miner rewards. Miners earn slightly less over time; depositors earn more immediately.

### 3d. popBlock Reversal

Per-block snapshot recorded in `m_blockBurnScalpContributions`. On `popBlock`:

```cpp
if (!m_blockBurnScalpContributions.empty()) {
    auto& back = m_blockBurnScalpContributions.back();
    if (back.first == poppedHeight) {
        m_yemState.yemReserve -= back.second;
        m_blockBurnScalpContributions.pop_back();
    }
}
```

---

## 4. Coinbase Payouts

### 4a. Mechanism

Payments owed to specific addresses (bond coupons, bond maturity, deficit bailouts) are queued in `m_pendingYemPayouts` at epoch boundaries. Each subsequent block drains from the queue — up to `YEM_MAX_PAYOUTS_PER_BLOCK` (10) — by including them as additional outputs in the coinbase transaction.

### 4b. constructMinerTx Extension

**File:** `src/CryptoNoteCore/Currency.cpp`, function `constructMinerTx()`.

Add a new parameter:

```cpp
bool Currency::constructMinerTx(
    ...
    const std::vector<std::pair<AccountPublicAddress, uint64_t>>& protocolPayouts = {}
) const;
```

Replace the unused reward output loop (~lines 663-688) with:

```cpp
for (const auto& [recipient, amount] : protocolPayouts) {
    Crypto::KeyDerivation derivation;
    if (!generate_key_derivation(recipient.viewPublicKey, txkey.secretKey, derivation))
        return false;

    size_t outputIndex = tx.outputs.size();
    Crypto::PublicKey outEphemeralPubKey;
    derive_public_key(derivation, outputIndex, recipient.spendPublicKey, outEphemeralPubKey);
    Crypto::KeyOutput ko;
    ko.key = outEphemeralPubKey;
    tx.outputs.push_back(TransactionOutput(amount, ko));
    protocolPayoutTotal += amount;
}
```

Validation at the end:
```cpp
if (tx.outputs.size() > 2) {
    uint64_t totalOutputs = minerReward + protocolPayoutTotal;
    // validate sum matches
}
```

### 4c. Queue Draining

**File:** `src/CryptoNoteCore/Core.cpp`, in the block preparation path (~line 620, before `constructMinerTx()`).

```cpp
std::vector<std::pair<AccountPublicAddress, uint64_t>> protocolPayouts;
if (height >= m_yemActivationHeight) {
    uint64_t reserveAvailable = m_blockchain.getYemReserve();
    auto& queue = m_blockchain.getYemPayoutQueue();
    while (!queue.empty() && protocolPayouts.size() < YEM_MAX_PAYOUTS_PER_BLOCK) {
        auto& payout = queue.front();
        if (payout.amount > reserveAvailable) break;
        protocolPayouts.push_back({payout.recipient, payout.amount});
        reserveAvailable -= payout.amount;
        m_blockchain.debitYemReserve(payout.amount);
        queue.pop_front();
    }
}
m_currency.constructMinerTx(..., protocolPayouts);
```

### 4d. Consensus Validation

**File:** `src/CryptoNoteCore/Blockchain.cpp`, `validate_miner_transaction()` (~line 1374).

```cpp
uint64_t expectedYemPayouts = computeExpectedYemPayouts(height, *this);
if (blockMajorVersion >= BLOCK_MAJOR_VERSION_YEM) {
    if (coinbaseTotal != reward + expectedYemPayouts) {
        logger(ERROR) << "Coinbase mismatch: got " << coinbaseTotal
                      << " expected " << (reward + expectedYemPayouts);
        return false;
    }
}
```

`computeExpectedYemPayouts()` is a deterministic function that reads the payout queue state known at the given height. It computes the first N payouts that would be drained by the miner at that block.

---

## 5. Epoch Boundary Sequence

**File:** `src/CryptoNoteCore/Blockchain.cpp`, `pushBlock()`, ~line 3900.

Only the YEM-modified portions shown. Existing logic for fee splitting, legacy bonds, yield execution, and LP yield unchanged except where noted.

```
At epoch boundary (height % epochDuration == 0):
─────────────────────────────────────────────────

0. SNAPSHOT
   EpochStateSnapshot preEpoch;  // captures 22 fields (16 existing + 6 YEM)

1. FEE SPLIT
   cdShare       = (epochSwapFees * 80) / 100;
   treasuryShare = (epochSwapFees * 16) / 100;
   rebalShare    = (epochSwapFees *  4) / 100;
   m_rebalancerVault += rebalShare;
   m_treasuryBalance  += treasuryShare;
   // CD share split between regular + legacy bonds (existing, unchanged)

2. LP YIELD FEED (post-activation)
   if (m_yemState.lagComplete) {
       uint64_t lpFeed = (m_treasuryLpYield * YEM_LP_FEED_PCT) / 100;
       m_yemState.swfBalance += lpFeed;
       m_treasuryLpYield -= lpFeed;
   }

3. BOND MATURITY
   for each bond in m_yemBonds where currentEpoch >= issuedAt + term:
       owed = principal + accrued_interest
       repayFrom = min(owed, m_yemState.swfBalance)
       m_yemState.swfBalance -= repayFrom
       if (owed > repayFrom):
           queue_coinbase(bond.creditor, owed - repayFrom)
       mark bond repaid

4. LAG CHECK  (post-activation)
   if (!m_yemState.lagComplete) {
       m_yemState.swfBalance += regularCdShare;   // accumulate 100%
       m_yemState.lagEpochCounter++;
       if (m_yemState.lagEpochCounter >= YEM_LAG_EPOCHS) {
           m_yemState.lagComplete = true;
       }
       GOTO step 12  // skip yield distribution during lag
   }

5. ROLLING AVERAGE
   organicRate = regularCdShare / epochCdLocked;
   m_yemState.rollingRates[m_yemState.rollingRateCount % 3] = organicRate;
   m_yemState.rollingRateCount++;
   targetRate = sum(rollingRates) / min(rollingRateCount, 3);

6. PER-CD CAPPED RATE
   for each active CD (post-activation):
       capRate = computeTierCap(cd.term)  // linear function
       cdRate  = min(targetRate, capRate)
       surplusOrDeficit += cdShare - (cdRate * cd.amount)

7. SWF SMOOTHING
   if surplus:
       m_yemState.swfBalance += (surplus * YEM_SWF_SAVE_PCT) / 100
   if deficit:
       drawn = min(deficit, m_yemState.swfBalance)
       m_yemState.swfBalance -= drawn
       remaining = deficit - drawn
       if remaining > 0:
           if (m_yemState.yemReserve >= remaining):
               queue_coinbase(FEE_POOL_ADDRESS, remaining)
               debitYemReserve(remaining)
           else:
               backstop = (m_treasuryBalance * YEM_TREASURY_BACKSTOP_BPS) / 10000
               // use what's available

8. SWF DRIP
   totalDrip = (m_yemState.swfBalance * YEM_SWF_DRIP_BPS) / 10000
   for each active CD:
       cdDrip = totalDrip * (cd.amount / totalCdLocked)
       // Add to CD's epoch fee pool credit

9. RECORD EPOCH FEE RATE
   per-CD: finalRate = cdRate + cdDrip
   m_commitmentIndex.recordEpochFeeRate(epochNumber, finalRate, ...)

10. EXECUTE CD YIELD
    Buy HEAT from AMM pool (existing mechanism).
    CD yield funded by m_cdYieldPool + m_yemState.swfBalance (for drip portion).

11. BOND COUPONS (post-lag)
    for each active bond:
        coupon = principal * rateBps / YEM_BOND_RATE_DIVISOR
        queue_coinbase(bond.creditor, coupon)
        debitYemReserve(coupon)

12. FINALIZE
    Store EpochStateSnapshot in m_epochSnapshots deque
    Record block contributions for popBlock reversal
    m_currentEpochSwapFees = 0
    Generate epoch report
```

---

## 6. State Variables

### 6a. New in Blockchain.h

```cpp
// Epoch boundary state
struct YemState {
    uint64_t swfBalance = 0;
    uint64_t yemReserve = 0;
    uint64_t rollingRates[YEM_ROLLING_WINDOW] = {};
    uint32_t rollingRateCount = 0;
    uint32_t lagEpochCounter = 0;
    bool     lagComplete = false;

    void serialize(ISerializer& s) {
        s(swfBalance, "swf");
        s(yemReserve, "yem_reserve");
        s.binary(rollingRates, sizeof(rollingRates), "rolling_rates");
        s(rollingRateCount, "rolling_count");
        s(lagEpochCounter, "lag_counter");
        s(lagComplete, "lag_complete");
    }
};

struct YemBond {
    uint64_t principal;
    uint32_t issuedAtEpoch;
    uint32_t termEpochs;
    uint64_t rateBps;
    Crypto::Hash txHash;
    AccountPublicAddress creditor;
    bool repaid;

    void serialize(ISerializer& s) {
        s(principal, "principal");
        s(issuedAtEpoch, "issued_epoch");
        s(termEpochs, "term");
        s(rateBps, "rate");
        s(txHash, "tx_hash");
        s(creditor, "creditor");
        s(repaid, "repaid");
    }
};

struct YemBondIndex {
    std::vector<YemBond> m_bonds;
    uint64_t m_totalOutstanding = 0;

    void issue(YemBond bond);
    uint64_t processMaturities(uint32_t currentEpoch, uint64_t swfAvailable,
                                std::deque<YemPayout>& payoutQueue);
    uint64_t getTotalOutstanding() const;
    bool isEmpty() const;
    void serialize(ISerializer& s);
};

// Coinbase payout queue entry
struct YemPayout {
    AccountPublicAddress recipient;
    uint64_t amount;
    Crypto::Hash sourceTxHash;  // for deterministic ordering
};

// === Blockchain class private members ===
YemState      m_yemState;
YemBondIndex  m_yemBonds;
uint64_t      m_rebalancerVault = 0;
std::deque<YemPayout> m_pendingYemPayouts;
std::vector<std::pair<uint32_t, uint64_t>> m_blockBurnScalpContributions;
```

### 6b. EpochStateSnapshot Additions

```cpp
// Append to existing struct at Blockchain.h:345
struct EpochStateSnapshot {
    // ... existing 16 fields (heatSupply through ammAccumulatedLpFees) ...
    uint64_t yemSwfBalance;
    uint64_t yemReserve;
    uint64_t rebalancerVault;
    uint32_t yemLagEpochCounter;
    bool     yemLagComplete;
    uint64_t yemBondOutstanding;
    uint64_t rollingRate0;
    uint64_t rollingRate1;
    uint64_t rollingRate2;
    uint32_t rollingRateCount;
    uint64_t pendingPayoutsHash;  // snapshot of queue for deterministic rollback
};
```

---

## 7. Legacy-Only Bonds

### 7a. Purpose

Pre-v10 COLD deposit holders convert locked XFG into YEM bonds. Principal seeds the SWF at launch. Interest paid via coinbase from YEM Reserve. Only available to deposits created before the v10 migration.

### 7b. Transaction Extra Tag

**File:** `src/CryptoNoteCore/TransactionExtra.h`:

```cpp
constexpr uint8_t TX_EXTRA_YEM_BOND = 0xCF;

struct TransactionExtraYemBond {
    Crypto::Hash originalDepositTx;
    uint64_t     amount;
    uint32_t     termEpochs;
    uint64_t     rateBps;
};
```

Follows the existing 0xCB/0xCC serialization pattern exactly:
- Writing: `addYemBondToExtra()` — tag byte + raw LE fields
- Reading: `parseTransactionExtra()` case `TX_EXTRA_YEM_BOND`
- Variant dispatch: `ExtraSerializerVisitor::operator()(TransactionExtraYemBond)`

### 7c. On-Chain Validation

**File:** `src/CryptoNoteCore/Blockchain.cpp`, `pushTransaction()`:

```cpp
case TX_EXTRA_YEM_BOND: {
    const auto& bond = boost::get<TransactionExtraYemBond>(field);
    // 1. Verify originalDepositTx exists and is a COLD deposit from pre-v10
    // 2. Verify deposit hasn't already been migrated (no 0xCE or 0xCF on it)
    // 3. Verify deposit is mature (currentHeight >= creationHeight + term)
    // 4. Verify amount matches deposit amount
    // 5. Compute expected rate, verify rateBps is within bounds
    // 6. On success: add bond to YemBondIndex
    m_yemBonds.issue({{bond.amount, currentEpoch, bond.termEpochs,
                       bond.rateBps, txHash, creditor, false}});
    m_yemState.swfBalance += bond.amount;  // principal → SWF
    break;
}
```

### 7d. Bond Rate

```
bond_rate = clamp(organic_rate × 1.5, 3%/yr, 25%/yr)
```

During lag (no organic rate), use floor (3%/yr). Post-lag, computed from the current rolling average.

### 7e. Wallet Command

```
migrate_legacy_deposit <deposit_id> --as-yem-bond <term_epochs>
```

Flow:
1. Look up deposit by ID in wallet cache
2. Verify deposit type = COLD (0xCD), pre-v10, not already migrated
3. Compute bond rate via RPC query for current organic rate
4. Display principal, term, rate, confirm
5. Create transaction with `TransactionExtraYemBond` (0xCF tag)
6. Destroy the COLD deposit output
7. Relay via `m_wallet->sendTransaction()`

---

## 8. Fee Split & ReBalancer Vault

### 8a. Config Changes

**File:** `src/CryptoNoteConfig.h`:

```cpp
constexpr uint64_t SWAP_FEE_CD_SHARE_PCT          = 80;   // unchanged
constexpr uint64_t SWAP_FEE_TREASURY_SHARE_PCT    = 16;   // was 20
constexpr uint64_t SWAP_FEE_REBALANCER_SHARE_PCT  = 4;    // new

// REMOVED:
// constexpr uint64_t CD_YIELD_TREASURY_ROUTE_PCT = 40;   // DELETE
```

### 8b. Epoch Boundary (Fee Split Section)

**File:** `src/CryptoNoteCore/Blockchain.cpp`, `pushBlock()`, ~line 3946:

Replace:
```cpp
uint64_t cdShare       = (epochSwapFees * SWAP_FEE_CD_SHARE_PCT) / 100;
uint64_t treasuryShare = (epochSwapFees * SWAP_FEE_TREASURY_SHARE_PCT) / 100;
// ... legacy bond split ...
m_treasuryBalance += treasuryShare;
```

With:
```cpp
uint64_t cdShare       = (epochSwapFees * SWAP_FEE_CD_SHARE_PCT) / 100;
uint64_t treasuryShare = (epochSwapFees * SWAP_FEE_TREASURY_SHARE_PCT) / 100;
uint64_t rebalShare    = (epochSwapFees * SWAP_FEE_REBALANCER_SHARE_PCT) / 100;
// ... legacy bond split on cdShare ...
m_treasuryBalance  += treasuryShare;
m_rebalancerVault  += rebalShare;
```

Delete the `poolRatioScaled > 200` routing block (~line 3982-3994) that used `CD_YIELD_TREASURY_ROUTE_PCT`.

### 8c. Rebalancer Funding Source

**File:** `src/CryptoNoteCore/Blockchain.cpp`, ~line 3326 (rebalancer call in epoch processing):

```cpp
FixedPoint64 rebalanceAmount = computeRebalanceAmount(...);

// Old funding source: m_treasuryBalance directly
// New: draw from vault first, treasury as fallback
uint64_t& fundingSource = m_rebalancerVault > 0 ? m_rebalancerVault : m_treasuryBalance;
uint64_t avail = std::min(rebalanceAmount.toUint64(),
                          (fundingSource * PROTOCOL_REBALANCE_MAX) / 100);
if (avail > 0) {
    // ... existing LP addition logic ...
    fundingSource -= avail;
}
```

---

## 9. Treasury LP Feed

### 9a. Mechanism

Uses existing `m_protocolLpShares` (Blockchain.h:398) and `m_treasuryLpYield` (Blockchain.h:399). Adds a contribution step at epoch boundary: treasury deposits XFG into Hearth AMM as single-sided LP, protocol mints LP shares.

### 9b. Epoch Boundary Code

```cpp
if (m_yemState.lagComplete) {
    uint64_t lpContribution = (m_treasuryBalance * TREASURY_LP_CONTRIBUTION_PCT) / 100;
    if (lpContribution > 0 && !m_ammPool.isEmpty()) {
        uint64_t shares = calculateSingleSidedLpShares(
            lpContribution, 0, m_ammPool.totalLpShares,
            m_ammPool.reserveXfg, m_ammPool.reserveHeat);
        m_ammPool.reserveXfg += lpContribution;
        m_protocolLpShares   += shares;
        m_treasuryBalance    -= lpContribution;
    }
}
```

### 9c. LP Yield → SWF

Already partially implemented: `m_treasuryLpYield` accumulates per epoch (existing code at ~line 4032). YEM adds the 75% feed to SWF:

```cpp
uint64_t lpYieldFeed = (m_treasuryLpYield * YEM_LP_FEED_PCT) / 100;
m_yemState.swfBalance += lpYieldFeed;
m_treasuryLpYield     -= lpYieldFeed;
```

### 9d. Bootstrap Exit

Tracked via existing `m_bootstrapRepaymentVault`. When protocol LP value ≥ original seed (1000 XFG + 8000 HEAT), seed contributor can withdraw. After repayment, LP yield continues padding treasury.

---

## 10. Rebalancer / Peg Interaction

### 10a. Smoother Pressure

CD yield currently buys HEAT from AMM each epoch in a single lump. After YEM: the smoothed rate means the buy pressure is spread more evenly across epochs, reducing rebalancer intervention frequency.

### 10b. Basin Defense (Unchanged)

`PiController::computeRebalanceAmount()` uses the basin model (spot vs basin center vs half-width). YEM doesn't touch this logic. The 4% vault gives the rebalancer dedicated funding so it never touches CD share.

### 10c. 2-Way Peg Dynamics

With 2-way mint/burn (HEAT flatcoin):
- **HEAT > peg**: XFG burns → HEAT mints → 8% scalp fires → YEM Reserve grows
- **HEAT < peg**: HEAT burns → XFG mints → no scalp, direction reversed

The burn scalp is pro-cyclical: it collects when HEAT is overvalued, just as the SWF should be accumulating for the inevitable lean period. The SWF architecture already handles this — high-yield epochs build reserves; low-yield epochs draw them. The 2-way peg doesn't change the model; it validates it.

### 10d. Basin Reobservation

`PiControllerState::basinPhase` cycles through BOOTSTRAP → OBSERVING → LOCKED → REOBSERVE. YEM doesn't affect this cycle. Rebalancer funding from the vault may increase slightly during LOCKED phase when the basin is well-defined and price is outside the band.

---

## 11. popBlock Reversibility

### 11a. Epoch Boundary Rollback

`popBlock()` ~line 4114 restores from `EpochStateSnapshot` if the popped height matches a snapshot entry:

```cpp
if (it != m_epochSnapshots.end() && it->first == poppedHeight) {
    auto& snap = it->second;
    m_yemState.swfBalance       = snap.yemSwfBalance;
    m_yemState.yemReserve       = snap.yemReserve;
    m_yemState.lagEpochCounter  = snap.yemLagEpochCounter;
    m_yemState.lagComplete      = snap.yemLagComplete;
    m_rebalancerVault           = snap.rebalancerVault;
    m_yemBonds.restoreState(snap.yemBondOutstanding);
    m_yemState.rollingRates[0]  = snap.rollingRate0;
    m_yemState.rollingRates[1]  = snap.rollingRate1;
    m_yemState.rollingRates[2]  = snap.rollingRate2;
    m_yemState.rollingRateCount = snap.rollingRateCount;
    // Rollback pending payouts: pop from queue back to snapshot hash
    rollbackYemPayouts(snap.pendingPayoutsHash);
    m_epochSnapshots.erase(it);
}
```

### 11b. Per-Block Burn Scalp Rollback

```cpp
// In popBlock(), after restoring epoch snapshot:
if (!m_blockBurnScalpContributions.empty()) {
    auto& back = m_blockBurnScalpContributions.back();
    if (back.first == poppedHeight) {
        m_yemState.yemReserve -= back.second;
        m_blockBurnScalpContributions.pop_back();
    }
}
```

### 11c. Bond Reversal

If a block containing a 0xCF bond registration is popped:
- Remove bond from `YemBondIndex`
- Restore `swfBalance` (debit the principal that was added)
- Same pattern as existing 0xCB reversal at Blockchain.cpp:4616-4623

### 11d. Coinbase Payout Rollback

`m_pendingYemPayouts` entries popped during block production must be pushed back during `popBlock`. Track per-block payout vectors:

```cpp
std::vector<std::pair<uint32_t, YemPayout>> m_blockYemPayouts;
```

On popBlock: push back into `m_pendingYemPayouts` in reverse order.

---

## 12. Serialization

### 12a. BlockCacheSerializer

**File:** `src/CryptoNoteCore/Blockchain.cpp`, in `BlockCacheSerializer::serialize()`:

```cpp
s(m_bs.m_yemState, "yem_state");
s(m_bs.m_yemBonds, "yem_bonds");
s(m_bs.m_rebalancerVault, "rebalancer_vault");
s(m_bs.m_pendingYemPayouts, "yem_payouts");
```

Bump `CURRENT_BLOCKCACHE_STORAGE_ARCHIVE_VER`. Old-version caches rebuild from raw blocks (safe).

### 12b. YemPayout Serialization

```cpp
void serialize(ISerializer& s) {
    s(recipient, "recipient");
    s(amount, "amount");
    s(sourceTxHash, "source_tx");
}
```

---

## 13. Activation Height

All YEM logic gated behind a single height constant:

```cpp
// src/CryptoNoteConfig.h
constexpr uint32_t YEM_ACTIVATION_HEIGHT = 450000;  // TBD
```

Gate patterns in Blockchain.cpp:

```cpp
bool yemActive = (height >= YEM_ACTIVATION_HEIGHT);

if (yemActive) {
    // YEM epoch boundary logic
} else {
    // Existing CD yield logic (unchanged)
}
```

Activation height for burns and bonds enforced separately:
- Burn scalp only starts post-activation + post-lag
- Bonds only accepted post-activation
- New CD types only post-activation
- Fee split (Phase 1) is NOT gated — it's a hard fork that applies at activation height

---

## 14. Wallet Commands

| Command | Description | New? |
|---|---|---|
| `create_cd <amount> <term_epochs>` | Create YEM CD (replaces old semantics) | Modified |
| `list_my_cds` | Show active CD positions with projected YEM yield | New |
| `yield_status` | SWF balance, rolling avg, reserve, bonds outstanding | New |
| `yield_project <amount> <term>` | Project yield for hypothetical CD | New |
| `migrate_legacy_deposit <id> --as-yem-bond <term>` | Convert pre-v10 COLD → YEM bond | New |
| `list_yem_bonds` | Show active/paid bond positions | New |
| `withdraw_bond <id>` | Claim mature bond payout | Existing (modified) |

---

## 15. RPC Endpoints

| Endpoint | Description |
|---|---|
| `get_yield_status` | `{ swf_balance, yem_reserve, rolling_avg_rate, lag_complete, bonds_outstanding, rebalancer_vault }` |
| `get_cd_projection` | `{ amount, term, projected_apy, tier_cap, drip_share }` |
| `get_yem_bonds` | `[{ principal, term, rate, creditor, repaid, maturity_epoch }]` |

---

## 16. Config Constants Summary

```cpp
// src/CryptoNoteConfig.h

// Fee split
constexpr uint64_t SWAP_FEE_CD_SHARE_PCT          = 80;
constexpr uint64_t SWAP_FEE_TREASURY_SHARE_PCT    = 16;
constexpr uint64_t SWAP_FEE_REBALANCER_SHARE_PCT  = 4;

// YEM core
constexpr uint32_t YEM_ACTIVATION_HEIGHT           = 450000;     // TBD
constexpr uint32_t YEM_LAG_EPOCHS                  = 3;
constexpr uint32_t YEM_ROLLING_WINDOW              = 3;
constexpr uint64_t YEM_SWF_SAVE_PCT                = 60;
constexpr uint64_t YEM_SWF_DRIP_BPS                = 100;        // 1%/epoch
constexpr uint64_t YEM_TREASURY_BACKSTOP_BPS       = 10;         // 0.1%/epoch
constexpr uint64_t YEM_MAX_PAYOUTS_PER_BLOCK       = 10;

// Burn scalp
constexpr uint64_t YEM_BURN_SCALP_BPS              = 800;        // 8%

// Time-tiered caps
constexpr uint64_t TIER_CAP_MIN_BPS                = 3300;       // 33%
constexpr uint64_t TIER_CAP_MAX_BPS                = 8000;       // 80%
constexpr uint32_t TIER_CAP_FULL_EPOCHS            = 72;

// Bonds
constexpr uint64_t YEM_BOND_MAX_RATE_BPS           = 2500;       // 25%/yr
constexpr uint64_t YEM_BOND_MIN_RATE_BPS           = 300;        // 3%/yr
constexpr uint64_t YEM_BOND_RATE_MULTIPLIER        = 150;        // 1.5× organic
constexpr uint64_t YEM_BOND_ISSUE_CAP_PCT          = 50;         // 0.5% of CD locked (×100 bps)
constexpr uint64_t YEM_BOND_RATE_DIVISOR           = 10000;

// Treasury LP
constexpr uint64_t TREASURY_LP_CONTRIBUTION_PCT    = 5;
constexpr uint64_t YEM_LP_FEED_PCT                 = 75;
```

---

## 17. Implementation Phases

### Phase 1: Fee Split + ReBalancer Vault

**Files touched:** `CryptoNoteConfig.h`, `Blockchain.h`, `Blockchain.cpp`

| Step | File | What |
|---|---|---|
| 1.1 | CryptoNoteConfig.h | Change `SWAP_FEE_TREASURY_SHARE_PCT` = 16, add `SWAP_FEE_REBALANCER_SHARE_PCT` = 4, delete `CD_YIELD_TREASURY_ROUTE_PCT` |
| 1.2 | Blockchain.h | `uint64_t m_rebalancerVault = 0;` |
| 1.3 | Blockchain.h (EpochStateSnapshot) | `uint64_t rebalancerVault;` |
| 1.4 | Blockchain.cpp (pushBlock, ~3946) | Add rebalShare calc, `m_rebalancerVault += rebalShare` |
| 1.5 | Blockchain.cpp (~3982) | Remove `poolRatioScaled > 200` routing block |
| 1.6 | Blockchain.cpp (~3326) | Rebalancer draws from vault first, treasury fallback |
| 1.7 | Blockchain.cpp (popBlock, ~4174) | Restore `m_rebalancerVault` from snapshot |
| 1.8 | Blockchain.cpp (serialization) | `s(m_bs.m_rebalancerVault, "rebalancer_vault")` |

**Test:** Existing CD yield unit tests pass with adjusted constants. Rebalancer still activates — verifiable by pool ratio behavior.

### Phase 2: YEM Core (SWF + Smoothing)

**Files touched:** `CryptoNoteConfig.h`, `Blockchain.h`, `Blockchain.cpp`, `Currency.cpp`, `CommitmentIndex.h`, `CommitmentIndex.cpp`

| Step | File | What |
|---|---|---|
| 2.1 | Blockchain.h | `YemState` struct, `m_yemState` member |
| 2.2 | Blockchain.h (EpochStateSnapshot) | YEM fields (7 additions) |
| 2.3 | CryptoNoteConfig.h | YEM activation height, lag, smoothing, drip, tier cap constants |
| 2.4 | Blockchain.h | `computeTierCap(uint32_t durationEpochs)` helper |
| 2.5 | Blockchain.cpp (pushBlock) | Lag period block, rolling average, SWF smoothing |
| 2.6 | Currency.cpp | `calculateYemInterest()` — type-aware, gated on activation |
| 2.7 | CommitmentIndex.cpp | Record per-CD YEM rate (extend `recordEpochFeeRate`) |
| 2.8 | Blockchain.cpp (serialization) | `YemState::serialize()` + BlockCacheSerializer |
| 2.9 | Blockchain.cpp (popBlock) | Restore all YEM fields from snapshot |

**Test:** Monte Carlo simulation against historical swap fee data. Verify smoothed APY > raw APY for same CD terms.

### Phase 3: Burn Scalp

**Files touched:** `CryptoNoteConfig.h`, `Blockchain.h`, `Blockchain.cpp`

| Step | File | What |
|---|---|---|
| 3.1 | CryptoNoteConfig.h | `YEM_BURN_SCALP_BPS = 800` |
| 3.2 | Blockchain.h | `m_blockBurnScalpContributions` vector |
| 3.3 | Blockchain.cpp (pushToBankingIndex, ~3527) | Scalp 8% before `addForeverDeposit()` |
| 3.4 | Blockchain.cpp (popBlock) | Reverse scalp from per-block contributions |
| 3.5 | EpochStateSnapshot | `yemReserve` field for rollback |

**Test:** Burn a known amount, verify `yemReserve` credits 8%, verify `BankingIndex::m_ethereal_xfg` gets 92%. Verify popBlock reversal.

### Phase 4: Coinbase Payouts

**Files touched:** `Currency.h`, `Currency.cpp`, `Core.cpp`, `Core.h`, `Blockchain.h`, `Blockchain.cpp`

| Step | File | What |
|---|---|---|
| 4.1 | Currency.h | `protocolPayouts` parameter on `constructMinerTx()` |
| 4.2 | Currency.cpp (constructMinerTx) | Replace old reward loop with generic protocol payouts |
| 4.3 | Blockchain.h | `m_pendingYemPayouts` deque, `YemPayout` struct |
| 4.4 | Core.cpp (~620) | Drain queue into `constructMinerTx` protocolPayouts |
| 4.5 | Blockchain.cpp (validateMinerTx, ~1374) | Updated validation for YEM payouts |
| 4.6 | Blockchain.cpp (popBlock) | `m_blockYemPayouts` for rollback |

**Test:** Queue known payouts, mine a block, verify coinbase outputs include them. Pop block, verify payouts restored to queue.

### Phase 5: Legacy-Only Bonds

**Files touched:** `TransactionExtra.h`, `TransactionExtra.cpp`, `Blockchain.h`, `Blockchain.cpp`, `SimpleWallet.cpp`, `WalletLegacy.cpp`

| Step | File | What |
|---|---|---|
| 5.1 | TransactionExtra.h | `TX_EXTRA_YEM_BOND = 0xCF`, `TransactionExtraYemBond` struct |
| 5.2 | TransactionExtra.cpp | `addYemBondToExtra()`, parse case, variant visitor |
| 5.3 | Blockchain.h | `YemBond` struct, `YemBondIndex` class, `m_yemBonds` |
| 5.4 | Blockchain.cpp (pushTransaction) | 0xCF validation + bond issuance |
| 5.5 | Blockchain.cpp (pushBlock) | Bond maturity + coupon processing |
| 5.6 | Blockchain.cpp (popBlock) | Bond reversal |
| 5.7 | SimpleWallet.cpp | `migrate_legacy_deposit --as-yem-bond` command |

**Test:** Migrate a pre-v10 COLD deposit, verify bond appears in index, verify SWF credited. Fast-forward epochs, verify maturity payout.

### Phase 6: Treasury LP Feed

**Files touched:** `CryptoNoteConfig.h`, `Blockchain.cpp`

| Step | File | What |
|---|---|---|
| 6.1 | CryptoNoteConfig.h | `TREASURY_LP_CONTRIBUTION_PCT`, `YEM_LP_FEED_PCT` |
| 6.2 | Blockchain.cpp (pushBlock) | LP contribution + yield feed at epoch boundary |

**Test:** Verify LP shares minted. Verify SWF receives 75% of LP yield.

### Phase 7: Wallet + RPC Integration

| Step | File | What |
|---|---|---|
| 7.1 | SimpleWallet.cpp | `yield_status`, `yield_project`, `list_yem_bonds` commands |
| 7.2 | JsonRpcServer.cpp | `get_yield_status`, `get_cd_projection`, `get_yem_bonds` endpoints |

---

## 18. Testing Strategy

### Unit Tests

| Test | What it verifies |
|---|---|
| `ComputeTierCap` | `cap(1) == 33%`, `cap(72) == 80%`, linear interpolation |
| `RollingAverage` | Empty window → 0; 1 entry → that entry; 3 entries → mean |
| `SwfSmoothing` | Surplus saves 60%; deficit draws SWF; SWF empty → reserve → backstop |
| `BurnScalp` | 1000 XFG burn → 920 EF + 80 reserve; verify popBlock reversal |
| `BondLifecycle` | Issue → coupon_epoch → coupon_epoch → maturity → payout |
| `CoinbasePayouts` | Queue N payouts, mine block, verify coinbase outputs |

### Integration Tests

| Test | What it verifies |
|---|---|
| `FullEpochCycle` | Multi-epoch simulation: lag → active → surplus → deficit → backstop |
| `PopBlockEpoch` | Epoch boundary + popBlock restores all 22 snapshot fields |
| `LegacyMigration` | pre-v10 COLD → YEM bond → principal in SWF → coupons via coinbase |
| `RebalancerWithVault` | 4% vault → rebalancer draws vault → falls back to treasury |

### Simulation

Existing `sim_cd_hybrid_v2.py` should be updated for YEM v3 with 8% burn scalp, single CD type, and coinbase payout modeling. 10,000 Monte Carlo runs minimum.

---

## 19. Migration Path

1. **Deploy Phase 1 at activation height.** Fee split + ReBalancer vault. Old CDs unchanged.
2. **3-epoch lag period.** No yield. SWF builds.
3. **Post-lag.** YEM engine activates. Old CDs continue earning via `calculateCdInterest()`. New CDs use YEM-smoothed rate.
4. **Deploy Phase 5 (bonds).** Pre-v10 COLD holders can migrate to YEM bonds.
5. **Deploy Phase 4 (coinbase).** Bond coupons and deficit coverage flow via coinbase.
6. **Old CDs mature naturally.** Over ~72 epochs, all pre-YEM CDs unlock. System is 100% YEM.
