# YEM v4 — Sovereign Yield Engine

> Supersedes YEM_V3_PLAN.md and YEM_V11_IMPLEMENTATION_GUIDE.md.
> No bonds. No burn scalp. No coinbase payouts. No inflation.

---

## 1. Architecture

YEM is a yield smoothing engine for HEAT CD holders. Three revenue streams fund a single SWF buffer that smooths CD APY across lean and high-volume epochs.

### 1a. Revenue Streams

| Stream | Source | Recipient | Status |
|--------|--------|-----------|--------|
| Swap fees | AMM volume, per-epoch | CD yield pool → HEAT buyback | **Built** |
| Treasury LP yield | LP fees from protocol's Hearth LP shares | SWF → CD yield top-up | **To build** |
| Peg defense revenue | Treasury HEAT buy/sell profits | SWF → CD yield top-up | **To build** |

### 1b. Yield Flow

```
Swap fees (100%):
  └─→ CD yield pool → buys HEAT from Hearth → credits CD holders

HEAT mint burn (100%):
  ├─ 50% → Eternal Flame (permanent deflation)
  └─ 50% → Treasury
           ├─ 60% → m_treasuryLpReserve → Hearth LP → LP fees → SWF
           └─ 40% → m_treasuryBalance → Peg defense → profit → SWF

SWF:
  └─→ Drips 1% per epoch into CD yield pool (dividend for all CD holders)
```

### 1c. Pools

| Pool | Storage | Funded by | Purpose |
|------|---------|-----------|---------|
| CD Yield Pool | `m_cdYieldPool` | Swap fees (100% atomic) | Buys HEAT → credits CD holders per epoch |
| SWF | `m_yemState.swfBalance` | LP yield + peg profits | Smooths CD yield, 1% drip per epoch |
| Treasury LP Reserve | `m_treasuryLpReserve` | Mint burn (30% total) | Feeds Hearth LP → generates fees |
| Treasury Peg Reserve | `m_treasuryBalance` | Mint burn (20% total) | Defends peg, profit → SWF |
| Eternal Flame | `BankingIndex::m_ethereal_xfg` | Mint burn (50% total) | Permanent deflation, recycled via block rewards |

---

## 2. CD Rate Model

```
CD annualized yield = base_rate(epoch) × tier_multiplier(lock_duration) + drip_yield
```

### 2a. Base Rate

```
base_rate = epochCdShare / totalCdLocked
```

The organic rate from swap fees. Identical to today's CD fee rate.

### 2b. Time-Tiered Multiplier

| Lock (epochs) | Duration | Multiplier |
|---------------|----------|------------|
| 1–9 | ~3 days – 1.5 months | 1.0× |
| 10–30 | ~1.5 – 5 months | 1.5× |
| 31–71 | ~5 months – 1 year | 2.0× |
| 72+ | 1+ year | 2.5× |

Longer lock = higher multiple. Simple, transparent, no complex interpolation.

### 2c. SWF Drip

```
drip_yield = (swfBalance × 0.01) / totalCdLocked
per_cd_drip  = cd_amount × drip_yield
```

1% of SWF balance distributed per epoch to all CD holders proportional to locked amount. Acts as a dividend — accumulated protocol profits paid continuously to savers.

### 2d. CD Rate Cap

```
cd_rate = min(base_rate × tier_multiplier, MAX_CD_APY_BPS / 10000)
MAX_CD_APY_BPS = 8000   // 80% APY ceiling
```

Prevents yield from exceeding 80% APY even in extreme volume. Surplus flows to SWF.

---

## 3. SWF Smoothing

### 3a. Mechanism

The SWF acts as a buffer between volatile swap fee revenue and stable CD yield.

```
At epoch boundary:

surplus = epochCdShare - (base_rate × totalCdLocked)

if surplus > 0:
    swfBalance += surplus × 0.60   // save 60% for lean epochs
    // 40% already distributed as base yield to CD holders

if deficit > 0:
    drawn = min(deficit × 0.50, swfBalance)  // cover 50% of shortfall from SWF
    swfBalance -= drawn
    m_cdYieldPool += drawn                    // inject to maintain CD yield floor
    // remaining 50% deficit = lower yield that epoch (savers share the downside)
```

### 3b. Lag Period

First 3 epochs after activation: SWF accumulates swap fee surplus without distribution. This builds the initial buffer so smoothing can start on solid footing. During lag, CD holders earn base rate from swap fees normally — only SWF accumulation and drip are deferred.

### 3c. CD Yield Floor

```
floor_rate = swfBalance / (totalCdLocked × 50)   // SWF can cover ~50 epochs at zero volume
if base_rate < floor_rate:
    m_cdYieldPool += (floor_rate - base_rate) × totalCdLocked
    swfBalance     -= deficit
```

Never reduces CD yield below the floor the SWF can sustain. In a zero-volume apocalypse, SWF-funded floor ensures CDs still earn something for ~50 epochs.

---

## 4. Treasury LP Yield → SWF

### 4a. Mechanism

When protocol LP shares earn fees from Hearth volume, those fees flow to `m_treasuryLpYield`. At each epoch boundary:

```cpp
if (m_treasuryLpYield > 0 && m_yemState.lagComplete) {
    uint64_t lpFeed = (m_treasuryLpYield * YEM_LP_FEED_PCT) / 100;  // 75% → SWF
    m_yemState.swfBalance += lpFeed;
    m_treasuryLpYield     -= lpFeed;
    // 25% retained in treasury for LP compounding
}
```

### 4b. Peg Defense Revenue → SWF

When treasury buys HEAT below peg or sells HEAT above peg, the profit accumulates in `m_treasuryHeatReserve`. At epoch boundary:

```cpp
if (m_treasuryHeatReserve > 0) {
    // Convert HEAT profit to XFG equivalent at current pool rate
    uint64_t heatProfit  = m_treasuryHeatReserve;
    uint64_t xfgValue    = ammGetOutputAmount(heatProfit, m_ammPool.reserveHeat,
                                              m_ammPool.reserveXfg, 0);
    uint64_t pegFeed     = (xfgValue * YEM_PEG_FEED_PCT) / 100;  // 50% → SWF
    m_yemState.swfBalance += pegFeed;
    m_treasuryHeatReserve -= heatProfit;
    m_treasuryBalance     += (xfgValue - pegFeed);  // 50% retained for peg reserves
}
```

---

## 5. Epoch Boundary Sequence

At `pushBlock()` epoch boundary (height % epochDuration == 0):

```
0. SNAPSHOT EpochStateSnapshot (all fields preserved for popBlock)

1. FEE SPLIT (existing)
   cdShare       = swapFees × 100%    // unchanged
   treasuryShare = 0                  // treasury now funded by HEAT mint burns, not swap fees

2. LP YIELD FEED (new)
   if lagComplete:
       swfBalance += m_treasuryLpYield × 75%

3. PEG DEFENSE FEED (new)
   if treasuryHeatReserve > 0:
       convert to XFG, 50% → swfBalance, 50% → treasuryBalance

4. LEGACY BOND YIELD (existing, eventually deprecated)
   Legacy bond yield routed from cdShare per existing logic

5. CD RATE (modified)
   baseRate = cdShare / totalCdLocked
   for each active CD:
       capRate  = min(baseRate × tierMultiplier(term), MAX_CD_APY)
       cdYield  = capRate × cdAmount
       record in CommitmentIndex

6. SWF SMOOTHING (new)
   if lagEpochs < 3:
       swfBalance += cdShare × 0.60   // accumulate during lag
       lagEpochs++
       if lagEpochs == 3: lagComplete = true
   else:
       surplusOrDeficit = cdShare - (baseRate × totalCdLocked)
       if surplus:  swfBalance += surplus × 0.60
       if deficit:  draw from SWF → inject to m_cdYieldPool

7. SWF DRIP (new)
   if lagComplete:
       totalDrip = swfBalance × 1% / 100
       for each CD: credit proportional drip
       swfBalance -= totalDrip

8. CD YIELD EXECUTION (existing)
   m_cdYieldPool buys HEAT from Hearth pool
   If pool lopsided: mint HEAT at PI rate as fallback

9. PROTOCOL LP REBALANCE (existing, now from m_treasuryLpReserve)
   computeRebalanceAmount() → single-sided LP deposit from m_treasuryLpReserve

10. PEG ARBITRAGE (existing)
    PI controller peg loop: mint-sell or treasury-buy using m_treasuryBalance

11. FINALIZE
    Record epoch report, log metrics, reset accumulators
```

---

## 6. State Variables

### 6a. New in Blockchain.h

```cpp
struct YemState {
    uint64_t swfBalance = 0;           // Sovereign Wealth Fund
    uint32_t lagEpochCounter = 0;      // epochs in lag phase
    bool     lagComplete = false;      // true after 3 epoch lag

    void serialize(ISerializer& s) {
        s(swfBalance, "swf_balance");
        s(lagEpochCounter, "lag_counter");
        s(lagComplete, "lag_complete");
    }
};

// Blockchain class members:
YemState  m_yemState;
```

### 6b. EpochStateSnapshot Additions

```cpp
struct EpochStateSnapshot {
    // ... existing fields ...
    uint64_t yemSwfBalance;
    uint32_t yemLagEpochCounter;
    bool     yemLagComplete;
};
```

### 6c. New Config Constants

```cpp
// YEM core
constexpr uint32_t YEM_ACTIVATION_HEIGHT      = 1111111;
constexpr uint32_t YEM_LAG_EPOCHS             = 3;
constexpr uint64_t YEM_SWF_SAVE_PCT           = 60;     // surplus savings rate
constexpr uint64_t YEM_SWF_DEFICIT_COVER_PCT  = 50;     // % of deficit SWF covers
constexpr uint64_t YEM_SWF_DRIP_BPS           = 100;    // 1% per epoch
constexpr uint64_t YEM_LP_FEED_PCT            = 75;     // LP yield → SWF
constexpr uint64_t YEM_PEG_FEED_PCT           = 50;     // peg profit → SWF

// CD rate
constexpr uint64_t MAX_CD_APY_BPS             = 8000;   // 80% APY ceiling

// CD tier multipliers (×100 scale)
constexpr uint64_t CD_TIER_1_MULTIPLIER       = 100;    // 1.0× (1-9 epochs)
constexpr uint64_t CD_TIER_2_MULTIPLIER       = 150;    // 1.5× (10-30 epochs)
constexpr uint64_t CD_TIER_3_MULTIPLIER       = 200;    // 2.0× (31-71 epochs)
constexpr uint64_t CD_TIER_4_MULTIPLIER       = 250;    // 2.5× (72+ epochs)
```

---

## 7. Implementation Phases

### Phase 1: YEM Core State + Serialization

**Files:** `Blockchain.h`, `Blockchain.cpp`

| Step | What |
|------|------|
| 1.1 | Add `YemState` struct to `Blockchain.h` |
| 1.2 | Add `m_yemState` member |
| 1.3 | Add YEM fields to `EpochStateSnapshot` |
| 1.4 | Serialize `m_yemState` in `BlockCacheSerializer` |
| 1.5 | Bump `BLOCKCACHE_STORAGE_ARCHIVE_VER` |
| 1.6 | Add `getYemStatus()` public getter |

**Build verification:** Compiles. New state loads from empty (defaults). Old caches rebuild from blocks.

### Phase 2: Epoch Boundary YEM Logic

**Files:** `Blockchain.cpp` (pushBlock epoch boundary), `Blockchain.h`

| Step | What |
|------|------|
| 2.1 | Lag period: accumulate SWF for first 3 epochs |
| 2.2 | SWF smoothing: surplus→save 60%, deficit→cover 50% from SWF |
| 2.3 | SWF drip: 1% of balance distributed per epoch |
| 2.4 | LP yield feed: m_treasuryLpYield × 75% → SWF |
| 2.5 | Peg defense feed: treasuryHeatReserve → convert → 50% SWF |
| 2.6 | Gated behind `YEM_ACTIVATION_HEIGHT` |

**Test:** Monte Carlo with sim_final_v19.py. Verify smoothed APY.

### Phase 3: Tiered CD Rate Caps

**Files:** `CommitmentIndex.cpp`, `Blockchain.cpp`

| Step | What |
|------|------|
| 3.1 | `computeTierMultiplier(uint32_t term)` → returns ×100 multiplier |
| 3.2 | Per-CD cap: `min(baseRate × multiplier, MAX_CD_APY)` |
| 3.3 | Record capped rate in `recordEpochFeeRate` |

**Test:** 1-epoch CD gets 1.0×, 72-epoch CD gets 2.5×. Verify overflow safety.

### Phase 4: CD Yield Floor

**Files:** `Blockchain.cpp`

| Step | What |
|------|------|
| 4.1 | Compute floor: `swfBalance / (totalCdLocked × 50)` |
| 4.2 | If base_rate < floor: inject SWF to maintain floor |
| 4.3 | Cap floor at `MAX_CD_APY_BPS` |

**Test:** Zero swap fee epoch → floor kicks in. SWF drains predictably.

### Phase 5: popBlock Reversibility

**Files:** `Blockchain.cpp`

| Step | What |
|------|------|
| 5.1 | Per-epoch SWF delta tracked in `m_epochSnapshots` |
| 5.2 | `popBlock` restores SWF balance from pre-epoch snapshot |
| 5.3 | Reverse drip + smoothing from per-epoch records |

**Test:** Multi-epoch chain → pop to epoch N → verify SWF restored.

### Phase 6: RPC + Wallet

| Step | What |
|------|------|
| 6.1 | RPC `get_yem_status`: swfBalance, lagComplete, currentBaseRate |
| 6.2 | RPC `get_cd_projection`: projected APY for (amount, term) |
| 6.3 | Wallet `yield_status` command |

---

## 8. What Was Removed vs V3/V11 Plans

| Feature | V3/V11 Plan | V4 Status |
|---------|-------------|-----------|
| Bonds | Legacy COLD → YEM bonds | **Removed** — community transition to HEAT CDs |
| Burn scalp | 8% skim from HEAT mint burns | **Removed** — mint burn already splits 50/50 EF/Treasury |
| Coinbase payouts | Coupons + maturities via miner tx | **Removed** — no bonds, no coupons |
| Staged bond unlock | 4-stage 25% withdrawal | **Removed** — no bonds |
| YEM Reserve (paper credit) | "Deferred emission right" | **Removed** — confusing, inflationary |
| Rebalancer vault | 4% swap fee allocation | **Not needed** — LP reserve already funded by mint burns |
| Fee split change | 80/16/4 | **Not changed** — 100% atomic → CD yield, treasury funded by burns |
| Rolling 3-epoch average | Complex interpolation | **Simplified** — base rate from organic, SWF covers shortfalls |
| SWF smoothing | Saved 60%, drew SWF → Reserve → Treasury backstop | **Simplified** — SWF covers 50% of deficit, rest is lower yield |
| Burn scalp reversal | Per-block contribution tracking | **Not needed** — no scalp |
| Bond serialization | `YemBondIndex` | **Not needed** — no bonds |

---

## 9. Testing Checklist

| Test | Verifies |
|------|----------|
| Lag accumulation | First 3 epochs: 60% surplus → SWF, no drip, no smoothing |
| Lag completion | Epoch 4: SWF smoothing + drip activate |
| Surplus savings | 1000 XFG surplus → 600 to SWF, 400 to CD holders |
| Deficit coverage | 1000 XFG deficit → 500 from SWF, 500 = lower yield |
| SWF drip | SWF=100K, CdLocked=1M → drip adds 1% → ~0.1% APY boost |
| CD tier caps | 1-epoch CD = 1.0×, 10-epoch = 1.5×, 72-epoch = 2.5× |
| CD floor | Zero swap fees → SWF covers floor rate |
| LP yield feed | Treasury LP earns 100 XFG → 75 → SWF, 25 → treasury |
| Peg profit feed | Treasury sells HEAT for 100 XFG profit → 50 → SWF |
| popBlock restoration | Multi-epoch → pop → all YEM state restored |
| No bonds anywhere | Search codebase: zero bond structs, zero coinbase payouts |
