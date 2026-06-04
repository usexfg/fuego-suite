# YEM v11 — Full Sovereign Yield Engine Implementation Guide

> Activates at height 1,111,111 | v10 skeleton is the prerequisite

---

## 1. Architecture Recap

### Pools (5)

| Pool | Storage | Funded by | Purpose |
|---|---|---|---|
| YEM Reserve | `m_yemState.yemReserve` (paper) | Burn scalp + swap fees | Bond coupons, deficit backstop, coinbase payout auth |
| SWF | `m_yemState.swfBalance` | Mint split (MS%), LP yield feed, surplus | Smooths CD yield, bond maturity principal |
| Treasury | `m_treasuryBalance` | Mint split (TRE%), LP yield | Peg defense, emergency backstop |
| Eternal Flame | `BankingIndex::m_ethereal_xfg` | 92% of mint premium | Emission recycling via block rewards |
| Collateral Reserve | `m_cdReserve` | 95% of mint value | 1:1 backs HEAT supply |

### Flow Map

```
HEAT Mint (XFG burned):
  │
  ├── 95% → Collateral Reserve (backs HEAT 1:1)
  │
  └──  5% → Mint Premium
               │
               ├──  8% → YEM Reserve (scalp — paper credit)
               │
               └── 92% → Tre/Min/SWF dynamic split:
                           TRE² = 15-60% → Treasury
                           MIN³ = 10-60% → Eternal Flame (recycled via block rewards)
                           SWF³ = 25-40% → Sovereign Wealth Fund

HEAT Burn (redemption):
  95% of redemption value → from Collateral Reserve → user
   5% of redemption value → Treasury (protocol revenue)

Swap Fees (per-epoch):
  ── v10: 100% → YEM Reserve (held)
  ── v11: 80% CD / 16% Treasury / 4% Rebalancer Vault

Atomic Swap Fees (2%):
  100% → CD yield pool (unchanged)
```

### Dynamic Mint Split Bands

| Peg deviation | TRE% | MIN% | SWF% |
|---|---|---|---|
| < 1% | 15 | 60 | 25 |
| 1-3% | 40 | 20 | 40 |
| > 3% | 60 | 10 | 30 |

Tight peg → maximize mining. Loose peg → save more to treasury + SWF.

---

## 2. Implementation Phases

### Phase 1 — Burn Scalp & Mint Split

**Files:** `Blockchain.cpp` (pushToBankingIndex)

After accumulating `permanentBurns` from HEAT commitments, before `addForeverDeposit()`:

```cpp
// ── YEM burn scalp (v11) ──
if (block.height >= CryptoNote::parameters::YEM_ACTIVATION_HEIGHT
    && m_yemState.lagComplete) {
    uint64_t scalped = (permanentBurns * 8) / 1000;  // 0.8% of total burned (8% of 10% premium)
    // Wait — recalibrate:
    // The mint flow: user burns XFG. 95% → collateral. 5% → premium.
    // Scalp = 8% of the PREMIUM portion, not total burned.
    // So scalp = (permanentBurns * 5 / 100) * 8 / 100 = permanentBurns * 40 / 10000
    // = 0.4% of permanentBurns.

    // Actually, in the current code, permanentBurns accumulates ALL XFG burned
    // in HEAT commitments. The premium (5%) is implicit — the HEAT mint engine
    // already splits 95%/5% when creating the HEAT tokens.

    // For the burn scalp, we take 8% of the premium. The premium is 5% of burned XFG.
    // But by the time we're in pushToBankingIndex, we only see total permanentBurns.
    // We need to know the premium portion.

    // APPROACH: track m_heatMintPremiumEpoch in a new accumulator alongside
    // permanentBurns. At this point, we scalp from that.
}

// SIMPLER APPROACH (if premium tracking exists):
// uint64_t scalpBasis = m_heatMintPremiumAccumulator;  // only the 5% premium
// uint64_t scalped = (scalpBasis * YEM_BURN_SCALP_BPS) / 10000;
// m_yemState.yemReserve += scalped;
// m_blockBurnScalpContributions.push_back({block.height, scalped});
```

**Design decision:** We need a `m_heatMintPremiumAccumulator` on Blockchain that tracks the premium portion of each HEAT mint, separate from `permanentBurns`. The HeatMintEngine already validates the mint — it knows the premium amount. Add a new accumulator.

**Files:** `Blockchain.h`

```cpp
uint64_t m_heatMintPremiumAccumulator = 0;  // per-block HEAT mint premium (for scalp)
```

**Files:** `Blockchain.cpp`, pushToBankingIndex, AFTER accumulating permanentBurns:

```cpp
if (block.height >= YEM_ACTIVATION_HEIGHT && m_yemState.lagComplete) {
    uint64_t scalped = (m_heatMintPremiumAccumulator * YEM_BURN_SCALP_BPS) / 10000;
    if (scalped > 0) {
        m_yemState.yemReserve += scalped;
        m_heatMintPremiumAccumulator -= scalped;
        m_blockBurnScalpContributions.push_back({block.height, scalped});
    }
    // Remaining premium goes to Eternal Flame (recycled as block rewards)
    permanentBurns += m_heatMintPremiumAccumulator;
}
m_heatMintPremiumAccumulator = 0;
m_bankingIndex.addForeverDeposit(permanentBurns, block.height);
```

### Phase 2 — Coinbase Payout Engine

**Files:** `Currency.h`

```cpp
bool constructMinerTx(
    uint32_t height,
    size_t effectiveMedianSize,
    uint64_t alreadyGeneratedCoins,
    uint64_t fee,
    const AccountPublicAddress& minerAddress,
    Transaction& tx,
    const BinaryArray& extraNonce = BinaryArray(),
    const std::vector<std::pair<AccountPublicAddress, uint64_t>>&
        protocolPayouts = {}   // NEW — YEM bond coupons + maturities
) const;
```

**Files:** `Currency.cpp`, `constructMinerTx()`

Locate the existing unused reward output loop (~lines 663-688) and replace with:

```cpp
// YEM coinbase payouts (v11)
uint64_t protocolPayoutTotal = 0;
for (const auto& [recipient, amount] : protocolPayouts) {
    Crypto::KeyDerivation derivation;
    if (!generate_key_derivation(recipient.viewPublicKey, txkey.secretKey, derivation))
        return false;

    Crypto::PublicKey outEphemeralPubKey;
    derive_public_key(derivation, static_cast<size_t>(tx.outputs.size()),
                      recipient.spendPublicKey, outEphemeralPubKey);

    Crypto::KeyOutput ko;
    ko.key = outEphemeralPubKey;
    tx.outputs.push_back(TransactionOutput(amount, ko));
    protocolPayoutTotal += amount;
}

// End-of-function validation: ensure coinbase outputs sum correctly
uint64_t expectedTotal = baseReward + protocolPayoutTotal;
// ... existing validation ...
```

### Phase 3 — Queue Draining (Core.cpp)

**Files:** `Core.cpp`, block preparation path (~line 620)

Before calling `constructMinerTx()`:

```cpp
std::vector<std::pair<AccountPublicAddress, uint64_t>> protocolPayouts;

if (height >= CryptoNote::parameters::YEM_ACTIVATION_HEIGHT) {
    uint64_t reserveAvailable = m_blockchain.getYemReserve();
    auto& queue = m_blockchain.getYemPayoutQueue();

    while (!queue.empty() && protocolPayouts.size() < 10) {  // MAX_PAYOUTS_PER_BLOCK
        auto& payout = queue.front();
        if (payout.amount > reserveAvailable) break;

        protocolPayouts.push_back({payout.recipient, payout.amount});
        reserveAvailable -= payout.amount;
        m_blockchain.debitYemReserve(payout.amount);
        m_blockTracker.yemPayouts.push_back(payout);  // for popBlock
        queue.pop_front();
    }
}
```

### Phase 4 — Epoch Boundary: Coupons + Maturities

**Files:** `Blockchain.cpp`, `pushBlock()`, after the v10 YEM allocation block

```cpp
// ── YEM v11: bond coupons (post-lag) ──
if (newHeight >= YEM_ACTIVATION_HEIGHT && m_yemState.lagComplete) {
    uint64_t currentEpoch = newHeight / epochDuration;

    // Coupons: per-bond, quarterly, paid from YEM Reserve → coinbase queue
    for (auto& bond : m_yemBonds.getActiveBonds(currentEpoch)) {
        if (bond.repaid) continue;
        if ((currentEpoch - bond.issuedAtEpoch) % 18 != 0) continue;  // quarterly

        uint64_t couponPerYear = (bond.principal * bond.rateBps) / 10000;
        uint64_t couponPerEpoch = couponPerYear / 73;
        if (couponPerEpoch > 0 && m_yemState.yemReserve >= couponPerEpoch) {
            m_yemState.yemReserve -= couponPerEpoch;
            m_pendingYemPayouts.push_back({
                bond.creditor, couponPerEpoch, bond.depositTxHash
            });
        }
    }

    // Maturities: principal from SWF, gap from YEM Reserve → coinbase
    for (auto& bond : m_yemBonds.getActiveBonds(currentEpoch)) {
        if (bond.repaid) continue;
        if (currentEpoch < bond.issuedAtEpoch + bond.termEpochs) continue;

        uint64_t owed = bond.principal;
        uint64_t fromSWF = std::min(owed, m_yemState.swfBalance);
        if (fromSWF > 0) {
            m_yemState.swfBalance -= fromSWF;
            m_pendingYemPayouts.push_back({
                bond.creditor, fromSWF, bond.depositTxHash
            });
        }
        uint64_t gap = owed - fromSWF;
        if (gap > 0 && m_yemState.yemReserve >= gap) {
            m_yemState.yemReserve -= gap;
            m_pendingYemPayouts.push_back({
                bond.creditor, gap, bond.depositTxHash
            });
        }
        bond.repaid = true;
    }
}
```

### Phase 5 — SWF Smoothing & Drip

**Files:** `Blockchain.cpp`, epoch boundary (after coupons + maturities)

```cpp
// ── YEM v11: CD rate smoothing ──
if (newHeight >= YEM_ACTIVATION_HEIGHT) {
    uint64_t organicRate = (regularCdShare > 0 && m_totalCdLocked > 0)
        ? (regularCdShare * FEE_POOL_RATE_PRECISION) / m_totalCdLocked : 0;

    // Lag period: first 3 epochs → all surplus goes to SWF
    if (!m_yemState.lagComplete) {
        m_yemState.swfBalance += regularCdShare;
        m_yemState.lagEpochCounter++;
        if (m_yemState.lagEpochCounter >= 3) {
            m_yemState.lagComplete = true;
        }
    } else {
        // Rolling 3-epoch average
        m_yemState.rollingRates[m_yemState.rollingRateCount % 3] = organicRate;
        m_yemState.rollingRateCount++;
        uint64_t targetRate = 0;
        for (int i = 0; i < 3 && i < m_yemState.rollingRateCount; ++i)
            targetRate += m_yemState.rollingRates[i];
        targetRate /= std::min(m_yemState.rollingRateCount, 3u);

        // Surplus / deficit
        uint64_t targetPayout = (targetRate * m_totalCdLocked) / FEE_POOL_RATE_PRECISION;
        if (regularCdShare > targetPayout) {
            uint64_t surplus = regularCdShare - targetPayout;
            m_yemState.swfBalance += (surplus * 60) / 100;   // 60% save
        } else if (targetPayout > regularCdShare) {
            uint64_t deficit = targetPayout - regularCdShare;
            uint64_t drawn = std::min(deficit, m_yemState.swfBalance);
            m_yemState.swfBalance -= drawn;
            deficit -= drawn;
            if (deficit > 0 && m_yemState.yemReserve >= deficit) {
                m_yemState.yemReserve -= deficit;
                // queue coinbase payout to fee pool
            }
        }

        // SWF drip: 1% of SWF balance distributed to CD holders (adds to CD yield)
        uint64_t drip = (m_yemState.swfBalance * 100) / 10000;  // 1%
        m_cdYieldPool += drip;
        m_yemState.swfBalance -= drip;
    }
}
```

### Phase 6 — Fee Split Change (80/16/4)

**Files:** `CryptoNoteConfig.h`

```cpp
constexpr uint64_t SWAP_FEE_CD_SHARE_PCT          = 80;   // unchanged
constexpr uint64_t SWAP_FEE_TREASURY_SHARE_PCT    = 16;   // was 20
constexpr uint64_t SWAP_FEE_REBALANCER_SHARE_PCT  = 4;    // new
```

**Files:** `Blockchain.cpp`, epoch boundary fee split (~line 3988)

Replace the existing `treasuryShare` calculation:

```cpp
uint64_t cdShare       = (epochSwapFees * SWAP_FEE_CD_SHARE_PCT) / 100;
uint64_t treasuryShare = (epochSwapFees * SWAP_FEE_TREASURY_SHARE_PCT) / 100;
uint64_t rebalShare    = (epochSwapFees * SWAP_FEE_REBALANCER_SHARE_PCT) / 100;
m_rebalancerVault     += rebalShare;
// Then proceed with existing legacy bond CD share split
```

**NOTE for v10→v11 transition:** In v10, 100% of swap fees go to YEM Reserve. At v11, the YEM allocation MUST be gated:

```cpp
if (newHeight >= YEM_V10_ACTIVATION_HEIGHT && newHeight < YEM_ACTIVATION_HEIGHT) {
    // v10: 100% → YEM Reserve
    uint64_t yemAllocation = epochSwapFees;
    m_yemState.yemReserve += yemAllocation;
    treasuryShare = (treasuryShare >= yemAllocation) ? (treasuryShare - yemAllocation) : 0;
} else if (newHeight >= YEM_ACTIVATION_HEIGHT) {
    // v11: normal fee split, burn scalp takes over
    // YEM reserve grows from scalp instead
}
```

### Phase 7 — Consensus Validation

**Files:** `Blockchain.cpp`, `validate_miner_transaction()` (~line 1374)

```cpp
// After the existing validation, add:
if (blockMajorVersion >= BLOCK_MAJOR_VERSION_11) {
    uint64_t expectedYemPayouts = computeExpectedYemPayouts(height);
    if (coinbaseTotal != baseReward + expectedYemPayouts) {
        logger(ERROR) << "Coinbase YEM payout mismatch";
        return false;
    }
}
```

### Phase 8 — Treasury LP Feed

**Files:** `Blockchain.cpp`, epoch boundary (after SWF smoothing)

```cpp
// ── YEM v11: Treasury LP contribution ──
if (newHeight >= YEM_ACTIVATION_HEIGHT && m_yemState.lagComplete) {
    uint64_t lpContribution = (m_treasuryBalance * 5) / 100;  // 5%
    if (lpContribution > 0 && !m_ammPool.isEmpty()) {
        uint64_t shares = calculateSingleSidedLpShares(
            lpContribution, 0,
            m_ammPool.totalLpShares,
            m_ammPool.reserveXfg, m_ammPool.reserveHeat);
        m_ammPool.reserveXfg += lpContribution;
        m_protocolLpShares   += shares;
        m_treasuryBalance    -= lpContribution;
    }
    // LP yield → SWF (75% of treasury's LP earnings)
    uint64_t lpYieldFeed = (m_treasuryLpYield * 75) / 100;
    m_yemState.swfBalance += lpYieldFeed;
    m_treasuryLpYield     -= lpYieldFeed;
}
```

### Phase 9 — Staged Bond Unlock (v11)

**Files:** `SimpleWallet.cpp` — new command `claim_bond_stage`

```
claim_bond_stage <deposit_id>

1. Lookup deposit, verify LEGACY_BOND type with YemBondIndex entry
2. Query RPC: get next mature stage
3. Display stage info + 1% service fee
4. Create tx with 0xCC claim for stage principal + interest
5. Deduct 1% service fee from interest → routed to YEM Reserve
```

**Files:** `Blockchain.cpp` — validation

```cpp
// In pushTransaction, when processing a staged bond withdrawal:
bool validateBondStageClaim(depositTxHash, stageNumber, principal, interest) {
    // Verify stage is mature and unclaimed
    // Verify principal matches schedule (25% of bond)
    // Verify interest ≤ available in legacyBondYieldPool
    // Deduct 1% svc fee from interest → m_yemState.yemReserve
    // Credit remaining interest to sender
    // Debit m_legacyBondYieldPool
    // Debit m_totalLegacyBondLocked
    // Mark stage claimed
}
```

### Phase 10 — popBlock Reversibility

All new state must survive `popBlock()`. This means:

| State | Tracked in | Rollback |
|---|---|---|
| Burn scalp | `m_blockBurnScalpContributions` | Reverse scalp, credit back `yemReserve` |
| Coinbase payouts | `m_blockYemPayouts` | Push back to queue |
| Epoch SWF/Rates | `EpochStateSnapshot` (new fields) | Restore from snapshot |
| Bond repayments | `m_blockBondRepayments` (new) | Mark bonds active again |
| Fee split | `EpochStateSnapshot.rebalancerVault` | Restore |

**New EpochStateSnapshot fields (9 additions to existing 16):**

```cpp
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
```

### Phase 11 — Serialization

**Files:** `Blockchain.cpp`, `BlockCacheSerializer::serialize()`

```cpp
s(m_bs.m_yemState, "yem_state");
s(m_bs.m_yemBonds, "yem_bonds");
s(m_bs.m_rebalancerVault, "rebalancer_vault");
s(m_bs.m_heatMintPremiumAccumulator, "heat_mint_premium_acc");
```

Bump `CURRENT_BLOCKCACHE_STORAGE_ARCHIVE_VER` from 8 → 9.

---

## 3. RPC Endpoints

| Endpoint | Returns |
|---|---|
| `get_yem_status` | `{ swf_balance, yem_reserve, lag_complete, bonds_outstanding, rebalancer_vault }` |
| `get_bond_stages` | `{ deposit_tx_hash, staged, stages[], accrued_interest }` |
| `get_cd_projection` | `{ amount, term, projected_apy, tier_cap, drip_share }` |

---

## 4. Config Constants (Final)

```cpp
// Activation
constexpr uint32_t YEM_V10_ACTIVATION_HEIGHT  = 445000;
constexpr uint32_t YEM_ACTIVATION_HEIGHT      = 1111111;

// Burn scalp
constexpr uint64_t YEM_BURN_SCALP_BPS         = 800;    // 8% of mint premium

// SWF
constexpr uint32_t YEM_LAG_EPOCHS             = 3;
constexpr uint32_t YEM_ROLLING_WINDOW         = 3;
constexpr uint64_t YEM_SWF_SAVE_PCT           = 60;
constexpr uint64_t YEM_SWF_DRIP_BPS           = 100;    // 1%/epoch
constexpr uint64_t YEM_TREASURY_BACKSTOP_BPS  = 10;     // 0.1%

// Coinbase
constexpr uint64_t YEM_MAX_PAYOUTS_PER_BLOCK  = 10;

// Fee split (v11)
constexpr uint64_t SWAP_FEE_CD_SHARE_PCT          = 80;
constexpr uint64_t SWAP_FEE_TREASURY_SHARE_PCT    = 16;
constexpr uint64_t SWAP_FEE_REBALANCER_SHARE_PCT  = 4;

// Bonds
constexpr uint64_t YEM_BOND_MAX_RATE_BPS          = 2500;  // 25%
constexpr uint64_t YEM_BOND_MIN_RATE_BPS          = 300;   // 3%
constexpr uint64_t YEM_BOND_RATE_MULTIPLIER       = 150;   // 1.5×
constexpr uint64_t YEM_BOND_ISSUE_CAP_PCT         = 50;    // 0.5%/epoch

// Treasury LP
constexpr uint64_t TREASURY_LP_CONTRIBUTION_PCT   = 5;
constexpr uint64_t YEM_LP_FEED_PCT                = 75;

// v10 pre-allocation
constexpr uint64_t YEM_V10_ALLOCATION_BPS         = 10000; // 100% until v11
constexpr uint64_t YEM_V10_RESERVE_DENOM          = 10000;

// Legacy bonds
constexpr uint64_t LEGACY_BOND_CD_SHARE_PCT       = 50;
constexpr uint64_t LEGACY_BOND_TARGET_APY         = 50;
constexpr uint64_t LEGACY_BOND_TERM_EPOCHS        = 72;
constexpr uint64_t LEGACY_BOND_DEBT_CAP           = 2542500000000;
```

---

## 5. State Variables Summary (Blockchain.h)

```cpp
// YEM v10 (already exists)
YemState      m_yemState;                 // yemReserve, swfBalance, rollingRates, lag
YemBondIndex  m_yemBonds;                 // bond registry
std::deque<YemPayout> m_pendingYemPayouts;

// YEM v11 (new)
uint64_t m_rebalancerVault = 0;
uint64_t m_heatMintPremiumAccumulator = 0;  // per-block HEAT mint premium
std::vector<std::pair<uint32_t, uint64_t>> m_blockBurnScalpContributions;
std::vector<YemPayout> m_blockYemPayouts;
```

---

## 6. Implementation Order (Dependency Graph)

```
 Phase 6 (fee split 80/16/4) ──────────────────────┐
 Phase 1 (burn scalp + mint split) ─────────────────┤
 Phase 7 (consensus validation) ────────────────────┤
 Phase 2 (coinbase engine) ─────────────────────────┤
 Phase 3 (queue draining) ─── requires Phase 2 ─────┤
 Phase 4 (coupons + maturities) ─── requires 2,3 ───┤
 Phase 5 (SWF smoothing) ─── requires Phase 4 ──────┤
 Phase 8 (treasury LP feed) ─── requires Phase 5 ───┤
 Phase 9 (staged bond unlock) ─── requires Phase 4 ─┤
 Phase 10 (popBlock) ─── touches all phases ────────┤
 Phase 11 (serialization) ─── touches all ──────────┘

Recommended build order:  6 → 1 → 2 → 3 → 4 → 5 → 7 → 8 → 9 → 10 → 11
```

---

## 7. Testing Checklist

| Test | What it verifies |
|---|---|
| Burn scalp | 1000 XFG HEAT mint → 50 XFG premium → 4 XFG scalp → YEM Reserve |
| Scalp popBlock | Pop block with scalp → `yemReserve` decremented correctly |
| Coinbase payout | Queue 5 entries → mine block → coinbase has 5 extra outputs |
| Queue overflow | 20 entries → only 10 drain per block → remaining deferred |
| Bond coupon | Bond 100 XFG at 25% → quarterly coupon 0.342 XFG → coinbase |
| Bond maturity | Bond matures → principal from SWF → gap from Reserve → coinbase |
| Lag period | First 3 epochs → all CD surplus → SWF, no distribution |
| Rolling avg | 4th epoch → 3-window average computed, SWF drip fires |
| Fee split | 1000 XFG swap fees → 800 CD / 160 treasury / 40 rebalancer |
| SWF deficit | CD rate drops → SWF covers → Reserve backstop → treasury floor |
| Staged bond | Stage 2 mature → claim 25% principal + interest − 1% fee |
| popBlock full | Multi-epoch chain → pop to any height → all YEM state restored |
