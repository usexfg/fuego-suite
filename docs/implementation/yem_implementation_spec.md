# H3 / YEM — Yield Emission Machine — Implementation Spec

## 1. Architecture Overview

The **Yield Emission Machine (YEM)** replaces the current single-stream CD yield model (`80% swap fees → CD pool → one-size-fits-all`) with a **3-CD-type, multi-source, SWF-buffered distribution engine**. It borrows from stock dividends (declare retroactively from accumulated earnings), sovereign wealth funds (save in boom, draw in bust), and bond markets (issue debt to bridge cold starts).

### What Changes

| Aspect | Current (v1) | H3 / YEM (v2) |
|--------|-------------|---------------|
| Fee split | 80% CD / 20% Treasury | 80% CD / 16% Treasury / 4% ReBalancer |
| CD types | 1 (variable epoch-rate) | 3 (Edition, EpochYield, PureRoll) |
| Yield smoothing | None | CD SWF + rolling 3-epoch avg + lag |
| Yield floor | None | LP yield feed + mint premium + SWF drip |
| Cold start | No CDs until fees accumulate | YEM bonds bridge gap |
| Treasury role | Passive accumulator | LP bootstrapping + emergency backstop (0.1%) |
| Rebalancer funding | Steals from CD share (CD_YIELD_TREASURY_ROUTE_PCT hack) | Dedicated 4% vault |

---

## 2. Fee Split Restructuring

### Current
```
swap_fees → 80% CD pool
         → 20% Treasury
```
When pool > 2:1 XFG-heavy: `CD_YIELD_TREASURY_ROUTE_PCT=40` steals from CD pool → Treasury.

### New
```
swap_fees → 80% CD pool → YEM (distributes to 3 CD types)
         → 16% Treasury  → LP bootstrapping + emergency backstop
         → 4%  ReBalancer vault → rebalancing operations
```
The `CD_YIELD_TREASURY_ROUTE_PCT` hack is **removed** — ReBalancer has its own funding stream.

### Code Changes

**`src/CryptoNoteConfig.h`:**
```cpp
constexpr uint64_t SWAP_FEE_CD_SHARE_PCT        = 80;   // unchanged
constexpr uint64_t SWAP_FEE_TREASURY_SHARE_PCT  = 16;   // was 20
constexpr uint64_t SWAP_FEE_REBALANCER_SHARE_PCT = 4;   // new
```
Remove or deprecate:
```cpp
// constexpr uint64_t CD_YIELD_TREASURY_ROUTE_PCT = 40;  // REMOVED
```

**`src/CryptoNoteCore/Blockchain.cpp`** — epoch boundary (~line 3706):
```cpp
uint64_t cdShare       = (epochSwapFees * SWAP_FEE_CD_SHARE_PCT) / 100;
uint64_t treasuryShare = (epochSwapFees * SWAP_FEE_TREASURY_SHARE_PCT) / 100;
uint64_t rebalShare    = (epochSwapFees * SWAP_FEE_REBALANCER_SHARE_PCT) / 100;
m_rebalancerVault += rebalShare;  // new state variable
```
Remove the `poolRatioScaled > 200` routing block (~line 3798-3812).

---

## 3. CD Types — Three Distinct Products

| # | Type | Demand Share | Rate | Duration | Funded By |
|---|------|-------------|------|----------|-----------|
| 1 | **Edition CDs** | 33% | Dynamic (1.5× organic, max 25%/yr) | 36 epochs | SWF reservation at open |
| 2 | **EpochYield CDs** | 33% | YEM-smoothed + SWF drip | User-chosen (1-72+) | Swap fees + SWF smoothing |
| 3 | **PureRoll CDs** | 34% | Raw organic rate | User-chosen | Swap fees only (no smoothing/caps) |

### 3a. Edition CDs

Fixed-rate tranches issued periodically. Once an edition opens, its rate is **locked** for the term. Funded by the SWF pre-committing the total liability at edition open.

- Rate: `min(global_organic_rate × 1.5, 25% annualized)`, floor 3%
- Term: 36 epochs (~6 months at EPY=72)
- Capacity: 33% of total CD locked per edition
- Gate: edition opens only when `SWF ≥ liability × 0.5`
- At maturity: principal unlocked, SWF liability released

### 3b. EpochYield CDs

Variable-rate CDs backed by the YEM smoothing engine. Yield = base rate (3-epoch rolling avg, time-tiered cap) + SWF drip (1%/epoch of SWF balance distributed proportionally).

**Time-tiered caps** (linear function of lock duration):
```
cap(d) = 33% + (min(d, 72) - 1) / 71 × 47%
```
| Duration (epochs) | Midpoint | Cap (annualized) |
|-------------------|----------|------------------|
| 1–9 | 5 | 35% |
| 10–30 | 20 | 45% |
| 31–71 | 51 | 66% |
| 72+ | 72 | 80% |

Longer lock → higher cap. Incentivizes duration.

### 3c. PureRoll CDs

Raw, unprocessed swap-fee yield. No smoothing, no caps, no SWF drip. In high-fee epochs these beat capped EpochYield rates. In low-fee epochs they may pay zero. Pure risk/reward — the "unmanaged" CD.

### Wallet Commands

| Command | CD Type | Params |
|---------|---------|--------|
| `create_edition_cd <edition_id> <amount>` | Edition | edition_id (auto-assigned), amount (HEAT) |
| `create_epoch_cd <amount> <term_epochs>` | EpochYield | amount (HEAT), term (1–72+) |
| `create_pure_cd <amount> <term_epochs>` | PureRoll | amount (HEAT), term |
| `list_editions` | — | Show open/active editions |
| `yield_status` | — | Show YEM state (SWF, rolling avg, cap tiers) |
| `yield_project <amount> <term> <type>` | — | Estimate yield |

---

## 4. Yield Emission Machine (Core Engine)

### 4a. Epoch Lifecycle

Each epoch boundary (every 900 blocks / ~5 days):

1. **Collect fees**: 80% → CD pool, 16% → Treasury, 4% → ReBalancer
2. **LP yield feed**: 75% of treasury LP yield → SWF
3. **Mint premium feed**: 50% of burn-to-mint premium → SWF
4. **YEM bond servicing**: mature bonds repaid from SWF, new bonds issued if needed
5. **Edition CD management**: mature editions release principal, new editions open if SWF healthy
6. **Lag period**: first 3 epochs → CD pool accumulated in SWF, zero yield distributed (builds reserve)
7. **Rolling average**: target rate = mean of last 3 epoch organic rates
8. **Per-tier cap**: each CD tier gets `min(target_rate, tier_cap / EPY)`
9. **SWF smoothing**: surplus saved (60%), deficit drawn from SWF, treasury backstop (0.1%/epoch) as last resort
10. **SWF drip**: 1% of SWF balance distributed to all EpochYield CD holders

### 4b. Lag Period Rationale

The first 3 epochs (~15 days) accumulate fees into the SWF without any yield distribution. This builds an initial reserve so that:
- The first rolling average has 3 data points
- Deficit smoothing works from epoch 4 onward
- The SWF has a minimum buffer before any yield obligation exists

### 4c. SWF Smoothing

```
surplus = cd_pool - (target_rate × epoch_locked)
if surplus > 0:  SWF += surplus × 60%   (save for lean epochs)
if deficit > 0:
    draw SWF first
    if SWF exhausted: draw treasury (capped 0.1%/epoch)
```

The 60% save rate means 40% of surplus stays in the CD pool for next epoch — the SWF is built gradually, and the CD pool always has some buffer.

### 4d. SWF Drip

1% of SWF balance distributed every epoch to EpochYield CD holders, proportional to locked amount. This is the "dividend" — accumulated protocol profits distributed continuously. If SWF is 371K (typical Y20 value), drip = 3,710 XFG/epoch = 267K XFG/year. At 1.4B CD locked, adds ~0.19% to annual APY.

---

## 5. YEM Bond Program

### 5a. Purpose

The YEM starts cold (SWF = 0). Without initial capital, the first 3 epochs accumulate almost nothing from swap fees, and the SWF takes years to reach the 2,000 XFG operational floor. YEM bonds bridge this gap — the protocol borrows from legacy COLD deposit holders, repays with interest, and establishes an operational SWF from day 0.

### 5b. Mechanism

| Parameter | Value | Description |
|-----------|-------|-------------|
| `YEM_BOND_TARGET` | 2,000 XFG | SWF operational floor — issue bonds if SWF < this |
| `YEM_BOND_TERM` | 72 epochs | Bond term (1 year at EPY=72) |
| `YEM_BOND_MAX_RATE` | 25%/yr | Maximum annualized interest |

- Bonds are issued **automatically** when SWF < target and system is past lag
- Bond rate: `min(global_organic_rate × 1.5, 25%/yr)`, floor 3%
- Bond principal capped at 0.5% of CD locked per epoch (prevents over-issuance)
- At maturity: principal + accrued interest repaid from SWF
- Interest = `principal × rate × term / EPY`
- New bonds replace maturing ones — revolving debt

### 5c. Funding Source

YEM bonds are **only available to legacy COLD deposit holders** — users who locked XFG under the deprecated COLD model (tag `0xCD`). These users convert their locked XFG into YEM bonds via `migrate_legacy_deposit --as-yem-bond`.

This serves two purposes:
1. Provides operational capital for the YEM
2. Gives legacy COLD holders a productive use for their locked XFG (earn dynamic interest instead of zero)

### 5d. Bond Lifecycle

```
User migrates COLD deposit → YEM bond
  ↓
XFG principal → SWF (operational capital)
  ↓
Each epoch: bond term decrements, SWF earns from inflows
  ↓
Maturity (72 epochs): principal × (1 + rate × term/EPY) repaid from SWF
  ↓
User receives XFG principal + interest
```

### 5e. New Deposit Type

A new transaction extra tag for YEM bonds:

**`src/CryptoNoteCore/TransactionExtra.h`:**
```cpp
constexpr uint8_t TX_EXTRA_YEM_BOND = 0xCF;  // new tag

struct TransactionExtraYemBond {
    Crypto::Hash originalDepositTx;  // legacy COLD deposit being converted
    uint64_t amount;                 // XFG principal
    uint32_t termEpochs;             // bond duration
    uint64_t rateBps;                // annualized interest in basis points
};
```

**`src/CryptoNoteCore/Blockchain.h`:**
```cpp
struct YemBond {
    uint64_t principal;
    uint32_t issuedAtEpoch;
    uint32_t termEpochs;
    uint64_t rateBps;         // annualized
    Crypto::Hash txHash;
    AccountPublicAddress creditor;
    bool repaid;
};

class YemBondIndex {
    std::vector<YemBond> m_bonds;
    uint64_t m_totalOutstanding;  // total principal owed
public:
    void issue(const YemBond& bond);
    void processMaturities(uint64_t currentEpoch, uint64_t& swfBalance);
    uint64_t getTotalOutstanding() const;
};
```

---

## 6. Treasury LP Bootstrapping

The treasury's 16% share serves a specific purpose beyond passive accumulation: **replacing the original Hearth bootstrap liquidity with protocol-owned LP.**

### 6a. Mechanism

Each epoch, treasury contributes a portion (configurable, e.g., 5%) to Hearth as single-sided LP. The protocol mints LP shares tracked in `m_protocolLpShares`. These shares earn the 0.3% Hearth swap fee → accumulated in `m_treasuryLpYield`. Of this yield, 75% feeds the CD SWF, 25% stays in treasury.

### 6b. Bootstrap Exit

Once `m_protocolLpShares` value ≥ `m_bootstrapXfgOwed` (the original seed: 1,000 XFG + 8,000 HEAT), the seed contributor can withdraw their original liquidity. The pool then runs entirely on protocol-owned + user-owned LP. After bootstrap repaid, treasury LP yield continues padding treasury for governance/emergency use.

### Code Changes

**`src/CryptoNoteConfig.h`:**
```cpp
constexpr uint64_t TREASURY_LP_CONTRIBUTION_PCT = 5;  // % treasury → LP per epoch
```

**`src/CryptoNoteCore/Blockchain.cpp`** — epoch boundary:
```cpp
// Before rebalancer, after fee split:
uint64_t lpContribution = (m_treasuryBalance * TREASURY_LP_CONTRIBUTION_PCT) / 100;
if (lpContribution > 0 && !m_ammPool.isEmpty()) {
    ammMintLpShares(lpContribution, 0, m_ammPool.totalLpShares,
                    m_ammPool.reserveXfg, m_ammPool.reserveHeat);
    m_ammPool.reserveXfg += lpContribution;
    m_protocolLpShares += shares;
    m_treasuryBalance -= lpContribution;
}
```

---

## 7. Mint Premium Routing

### 7a. Burn-to-Mint

When a user mints HEAT:
1. ALL XFG paid is **burned** (destroyed)
2. HEAT is created from thin air at the PI conversion rate
3. The "premium" is the extra XFG beyond the base PI rate

### 7b. Premium Distribution

```
mint_premium_xfg = total_xfg_burned × (premium_pct - 5%) / (1 + 5%)
  ↓
50% → CD SWF (swap-independent yield floor)
50% → Treasury (value accrual via XFG scarcity)
```

The premium only activates in bear markets (HEAT below peg). During bull runs, it stays at 5% base — no distortion. During crashes, it ramps to 12% via slow-TWAP crash detection.

### Code Changes

**`src/CryptoNoteConfig.h`:**
```cpp
constexpr uint64_t MINT_PREMIUM_MIN_BPS = 500;   // 5% base
constexpr uint64_t MINT_PREMIUM_MAX_BPS = 1200;  // 12% max
constexpr uint64_t MINT_PREMIUM_SLOPE  = 20;     // 20 bps per %|dev|
constexpr double   SLOW_TWAP_ALPHA      = 0.005; // crash detection EMA
```

**`src/CryptoNoteCore/PiController.cpp`:**
```cpp
uint64_t computeDynamicPremium(uint64_t currentSpot, uint64_t slowSpotEma) {
    if (currentSpot >= slowSpotEma) return MINT_PREMIUM_MIN_BPS;
    uint64_t crashPct = ((slowSpotEma - currentSpot) * 10000) / slowSpotEma;
    uint64_t bump = crashPct * MINT_PREMIUM_SLOPE / 100;
    return std::min(MINT_PREMIUM_MIN_BPS + bump, MINT_PREMIUM_MAX_BPS);
}
```

---

## 8. SWF Accounting

### 8a. State Variables

**`src/CryptoNoteCore/Blockchain.h`:**
```cpp
struct YemState {
    uint64_t swfBalance;              // CD Sovereign Wealth Fund (XFG)
    uint64_t cdReserveY5;             // snapshot for rollback
    std::deque<uint64_t> rollingRates; // last 3 epoch organic rates
    uint32_t lagEpochCounter;         // 0..YEM_LAG_EPOCHS
    bool lagComplete;
};

YemState m_yemState;
YemBondIndex m_yemBonds;
```

### 8b. Inflows

| Source | Rate | Notes |
|--------|------|-------|
| LP yield feed | 75% of `m_treasuryLpYield` | Protocol LP earns swap fees |
| Mint premium | 50% of burn-to-mint premium | Only activates during crashes |
| SWF surplus | 60% of excess above target | Saved during boom epochs |
| Lag accumulation | 100% of CD pool (first 3 epochs) | Builds initial reserve |
| Bond proceeds | 100% of issued principal | From legacy COLD holders |

### 8c. Outflows

| Destination | Rate | Notes |
|-------------|------|-------|
| SWF drip | 1% of SWF balance / epoch | Paid to EpochYield CD holders |
| Deficit draw | As needed | Smooths lean epochs |
| Edition reservation | Full liability at open | Pre-committed for Edition CD term |
| Bond repayment | Principal + interest at maturity | Repaid from SWF |
| Treasury backstop | Max 0.1% of treasury / epoch | Emergency only |

### 8d. Persistence

SWF state must survive node restarts and be reversible on `popBlock`:

```cpp
// Serialization (Blockchain.cpp save/load)
s(m_yemState.swfBalance, "yem_swf");
s(m_yemState.lagEpochCounter, "yem_lag_counter");
s(m_yemState.lagComplete, "yem_lag_done");
s(m_yemBonds, "yem_bonds");

// EpochStateSnapshot (for popBlock reversal)
struct EpochStateSnapshot {
    // ... existing fields ...
    uint64_t yemSwfBalance;
    uint64_t yemBondOutstanding;
};
```

---

## 9. Config Constants Summary

```cpp
// src/CryptoNoteConfig.h

// Fee split
constexpr uint64_t SWAP_FEE_CD_SHARE_PCT          = 80;
constexpr uint64_t SWAP_FEE_TREASURY_SHARE_PCT    = 16;   // was 20
constexpr uint64_t SWAP_FEE_REBALANCER_SHARE_PCT  = 4;    // new

// Treasury LP bootstrapping
constexpr uint64_t TREASURY_LP_CONTRIBUTION_PCT    = 5;

// YEM
constexpr uint32_t YEM_LAG_EPOCHS                  = 3;
constexpr uint32_t YEM_ROLLING_WINDOW              = 3;
constexpr uint64_t YEM_SWF_SAVE_PCT                = 60;
constexpr uint64_t YEM_SWF_PAYOUT_BPS              = 100;   // 1% per epoch
constexpr uint64_t YEM_LP_FEED_PCT                 = 75;
constexpr uint64_t YEM_TREASURY_BACKSTOP_BPS       = 10;    // 0.1% per epoch

// YEM Bonds
constexpr uint64_t YEM_BOND_TARGET                 = 2'000'000'000'000ULL; // 2K XFG in atomic units
constexpr uint32_t YEM_BOND_TERM_EPOCHS            = 72;
constexpr uint64_t YEM_BOND_MAX_RATE_BPS           = 2500;  // 25% annualized

// Mint premium (dynamic)
constexpr uint64_t MINT_PREMIUM_MIN_BPS            = 500;
constexpr uint64_t MINT_PREMIUM_MAX_BPS            = 1200;
constexpr uint64_t MINT_PREMIUM_SLOPE              = 20;
constexpr uint64_t MINT_PREMIUM_SWF_FEED_PCT       = 50;

// Edition CDs
constexpr uint32_t EDITION_TERM_EPOCHS              = 36;
constexpr uint64_t EDITION_DEMAND_PCT               = 33;
constexpr uint64_t EDITION_MAX_RATE_BPS             = 2500;
constexpr uint64_t EDITION_MIN_RATE_BPS             = 300;
constexpr uint64_t EDITION_RATE_MULTIPLIER          = 150;  // 1.5× organic rate

// CD type demand split
constexpr uint64_t CD_EDITION_DEMAND_PCT            = 33;
constexpr uint64_t CD_EPOCH_DEMAND_PCT              = 33;
constexpr uint64_t CD_PUREROLL_DEMAND_PCT           = 34;

// Time-tiered caps: cap(d) = 33 + (min(d,72)-1)/71 × 47  (annualized %)
constexpr uint64_t TIER_CAP_MIN_BPS                 = 3300;
constexpr uint64_t TIER_CAP_MAX_BPS                 = 8000;
constexpr uint32_t TIER_CAP_FULL_EPOCHS             = 72;
```

---

## 10. Wallet Commands (New)

| Command | Description |
|---------|-------------|
| `list_editions` | List active/open Edition CD tranches with rates |
| `create_edition_cd <edition_id> <amount>` | Lock HEAT into a fixed-rate edition |
| `create_epoch_cd <amount> <term_epochs>` | Lock HEAT into EpochYield CD (replaces old `create_cd`) |
| `create_pure_cd <amount> <term_epochs>` | Lock HEAT into PureRoll CD |
| `list_my_cds` | Show all active CD positions with type breakdown |
| `yield_status` | YEM state: SWF balance, rolling avg, bonds outstanding, current target rate |
| `yield_project <amount> <term> <type>` | Project yield for a specific CD |
| `migrate_legacy_deposit <id> --as-yem-bond <term>` | Convert COLD deposit → YEM bond |

### `migrate_legacy_deposit --as-yem-bond` Flow

1. Reads deposit by ID from wallet cache
2. Validates deposit type = COLD (0xCD), deposit is mature (`currentHeight ≥ creationHeight + term`)
3. Computes bond rate: `min(global_organic_rate × 1.5, 25%/yr)`
4. Creates `TransactionExtraYemBond` (tag 0xCF) with:
   - `originalDepositTx` = COLD deposit's creating tx hash
   - `amount` = COLD deposit amount (XFG principal)
   - `termEpochs` = user-chosen bond term
   - `rateBps` = computed bond rate
5. Destroys (spends) the COLD deposit output
6. Creates new YEM bond entry in `YemBondIndex`
7. SWF credited with `amount` (XFG principal)

---

## 11. RPC Endpoints

| Endpoint | Description |
|----------|-------------|
| `get_yield_status` | Returns YEM state (SWF, bonds, rolling rate, cap tiers) |
| `get_editions` | Returns active/open Edition CD tranches |
| `get_cd_projections` | Project yield for hypothetical CD |
| `get_yem_bonds` | List outstanding YEM bonds |
| `create_yem_bond` | Create a YEM bond (admin/DAO) |

---

## 12. Implementation Phases

### Phase 1: Fee Split + ReBalancer Vault
- Add `SWAP_FEE_REBALANCER_SHARE_PCT = 4`
- Add `m_rebalancerVault` state to Blockchain
- Adjust `SWAP_FEE_TREASURY_SHARE_PCT` to 16
- Remove `CD_YIELD_TREASURY_ROUTE_PCT` hack
- Wire rebalancer to draw from its vault first, then treasury

### Phase 2: YEM Core (SWF + Smoothing)
- Add `YemState` struct to Blockchain
- Implement lag period (first 3 epochs → SWF)
- Implement rolling 3-epoch average and target rate
- Implement SWF surplus/deficit smoothing
- Implement SWF drip (1%/epoch → EpochYield CDs)
- Modify `recordEpochFeeRate` to use YEM-smoothed rate
- Persist YEM state in serialization + epoch snapshots

### Phase 3: Time-Tiered Caps
- Modify CD creation to store lock duration per CD
- Implement `computeTierCap(durationEpochs)` — linear function
- Apply caps during YEM rate calculation per CD
- Old `calculateCdInterest` still works — rates are stored per-epoch

### Phase 4: Edition CDs
- Add `EditionConfig` struct
- Edition lifecycle state machine in Blockchain
- SWF pre-commitment at edition open
- New wallet command: `create_edition_cd`
- Dynamic rate calculation from global organic rate

### Phase 5: PureRoll CDs
- Add PureRoll CD type
- New wallet command: `create_pure_cd`
- PureRoll yield = raw swap fee share, no YEM processing

### Phase 6: YEM Bonds
- Add `TransactionExtraYemBond` (tag 0xCF)
- Add `YemBondIndex` to Blockchain
- Bond issuance / maturation logic at epoch boundary
- Bond interest rate = `global_organic_rate × 1.5`
- Modify `migrate_legacy_deposit` with `--as-yem-bond` flag

### Phase 7: Treasury LP Contribution
- Add `TREASURY_LP_CONTRIBUTION_PCT`
- Epoch boundary: treasury deposits XFG → Hearth → mints protocol LP shares
- Track LP shares growth toward bootstrap replacement
- LP yield feeds SWF (75%)

### Phase 8: Dynamic Mint Premium
- Implement `computeDynamicPremium()` in PiController
- Slow-TWAP crash detection EMA
- Route 50% of premium to SWF at epoch boundary

### Phase 9: Wallet + RPC Integration
- All new wallet commands and RPC endpoints
- `yield_status` / `yield_project` display
- Edition listing and subscription

---

## 13. Simulation Verification

All models verified via `sim_cd_hybrid_v2.py` with 10,000 Monte Carlo runs:

| Model | Score | Avg APY | Worst APY | Treasury | Notes |
|-------|-------|---------|-----------|----------|-------|
| H1 (current) | 0.250 | 2.14% | 1.53% | 359.3K | Baseline |
| H2 (M5 pre-funded) | 0.270 | 2.90% | 2.27% | 23.3K | High APY, destroys treasury |
| **H3 (YEM with bonds)** | **0.929** | **3.95%** | **2.98%** | **223.4K** | **Winner** |

H3 delivers +85% more APY than baseline while preserving 62% of the treasury. The YEM bond program bootstraps the SWF from a cold start without draining protocol reserves. Time-tiered caps incentivize longer lock-ups, reducing CD churn and stabilizing the fee base.

---

## 14. Migration Path

1. Deploy Phase 1 (fee split) — backward compatible, no CD changes
2. Deploy Phase 2 (YEM core) — old CDs continue earning via existing `calculateCdInterest`, new CDs use YEM rates
3. Deploy Phase 3-5 (CD types) — users choose CD type at creation
4. Deploy Phase 6 (bonds) — legacy COLD holders get migration path
5. Deploy Phase 7-8 (LP + premium) — incremental improvements to yield sources
6. Full v2 activation at upgrade height
