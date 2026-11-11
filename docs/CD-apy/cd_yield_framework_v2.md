# HEAT CD Yield Framework v2 — Development Plan

## 1. Overview

Replace the single CD type with a **dual-CD system** backed by a **Yield Emission Machine**. The protocol offers two CD products with fundamentally different risk/yield profiles, supported by a restructured fee flow and treasury LP bootstrapping strategy.

## 2. Fee Split Restructuring

### Current
| Destination | Share | Purpose |
|-------------|-------|---------|
| CD Yield Pool | 80% | CD holder returns |
| Treasury | 20% | Reserve + emergency |

### New (Default)
| Destination | Share | Purpose |
|-------------|-------|---------|
| CD Yield Pool | 80% | CD holder returns (both CD types) |
| Treasury | 12% | LP bootstrapping + emergency backstop |
| ReBalancer Vault | 8% | Dedicated rebalancer fuel |

**Dynamic swap (future):** If pool ratio indicates rebalancer needs > treasury needs → flip to 12% ReBalancer / 8% Treasury.

### Code Changes
- `CryptoNoteConfig.h`: Add `SWAP_FEE_REBALANCER_SHARE_PCT = 8`
- `Blockchain.cpp` epoch boundary (line ~3706): split into 3 destinations
- Remove `CD_YIELD_TREASURY_ROUTE_PCT` workaround — rebalancer has its own stream now

## 3. Treasury LP Bootstrapping Plan

**Goal:** Replace the original bootstrap liquidity (1000 XFG + 8000 HEAT) with protocol-owned LP, so the seed contributor can safely withdraw.

**Mechanism:**
1. Each epoch, treasury contributes a residual % (configurable, e.g., 5%) to Hearth as single-sided LP
2. Protocol mints LP shares tracked in `m_protocolLpShares`
3. Protocol LP earns 0.3% swap fees → accumulated in `m_treasuryLpYield`
4. `m_treasuryLpYield` feeds back into the Yield Emission Machine's CD SWF
5. Once `m_protocolLpShares` value ≥ `m_bootstrapXfgOwed` (the original seed value), the bootstrap contributor can withdraw
6. After bootstrap repaid, treasury LP yield continues padding treasury for "other uses"

### Code Changes
- `CryptoNoteConfig.h`: Add `TREASURY_LP_CONTRIBUTION_PCT = 5` (per-epoch contribution %)
- `Blockchain.cpp`: Epoch boundary — before rebalancer, treasury contributes LP
- New function: `treasuryContributeToLp()` — deposits XFG from treasury into Hearth, mints LP shares

## 4. CD Type 1 — Fixed-Term Guaranteed HEAT CDs (Editions)

### Concept
Limited-capacity, batch-issued CDs with **fixed, guaranteed APY** that is treasury-pre-committed and independent of on-chain fee activity. Each "edition" has a fixed capacity, term, and rate.

### Parameters Per Edition
| Parameter | Example | Description |
|-----------|---------|-------------|
| `edition_id` | 1 | Sequential edition number |
| `rate_bps` | 800 | Guaranteed APY in basis points (8.00%) |
| `term_epochs` | 36 | Lock duration (e.g., 36 epochs ≈ 6 months at EPY=73) |
| `max_capacity` | 1,000,000 HEAT | Total HEAT accepted |
| `min_deposit` | 100 HEAT | Minimum per-CD deposit |
| `treasury_commitment` | auto | Calculated: `max_capacity × (1 + rate/100 × term_epochs/EPY)` |

### Lifecycle
1. **Edition opens**: Treasury pre-commits total liability. APY and term locked.
2. **Subscription period**: Users lock HEAT. Edition fills or period expires.
3. **Lock period**: All CDs in edition are locked for `term_epochs`.
4. **Maturity**: Principal + guaranteed yield unlocked.

   If staged_unlock is enabled: yield unlocks in stages (e.g., 20% every `STAGE_INTERVAL_BLOCKS`), and principal unlocks at final stage.

### Yield Source
Pre-committed treasury funds + any organic swap fees that overlap the lock period (excess returned to treasury).

### Why This Is Different From Current CDs
- Current CDs: yield = variable epoch fee rate. Users don't know returns.
- Edition CDs: yield = fixed rate locked at purchase. Zero uncertainty.
- Treasury pre-commits liability at edition open — mathematically impossible to default.

### Code Changes
- New struct: `EditionConfig { rate_bps, term_epochs, max_capacity, min_deposit, treasury_commitment }`
- New wallet command: `create_edition_cd <edition_id> <amount>`
- `Blockchain.cpp`: Edition lifecycle state machine
- `StagedDepositUnlock.h`: Already supports staged unlock — reuse `useStagedUnlock=true` for editions

## 5. CD Type 2 — Epoch-Yield Hybrid CDs

### Concept
Variable-yield CDs backed by the **Yield Emission Machine** — a CD Sovereign Wealth Fund (Model 13) with rolling 3-epoch averaging, lagged start, APY cap, and treasury LP yield recapture. Like stock dividends: companies don't pay immediately after each sale; they accumulate, then declare.

### Yield Emission Machine Architecture

```
                    ┌──────────────────────────────┐
                    │      SWAP FEES (100%)         │
                    └──────────┬───────────────────┘
                               │
            ┌──────────────────┼──────────────────┐
            ▼                  ▼                  ▼
    ┌──────────────┐  ┌──────────────┐  ┌──────────────┐
    │ CD Pool 80%  │  │ Treasury 12% │  │ ReBalancer 8%│
    └──────┬───────┘  └──────┬───────┘  └──────────────┘
           │                 │
           │    ┌────────────┘
           ▼    ▼
    ┌─────────────────────────────────┐
    │   YIELD EMISSION MACHINE        │
    │                                 │
    │  ┌───────────────────────────┐  │
    │  │ 1. Accumulate 3 epochs    │  │ ← lag: no yield first 3 epochs
    │  │ 2. Compute rolling avg    │  │
    │  │ 3. Apply APY cap (40%)    │  │
    │  │ 4. CD SWF smoothing       │  │ ← save surplus, draw deficit
    │  │ 5. Treasury LP yield feed │  │ ← from protocol LP position
    │  │ 6. Distribute target rate │  │
    │  └───────────────────────────┘  │
    └─────────────────────────────────┘
```

### Step-by-Step Per Epoch

1. **Accumulate** CD share (80% of swap fees) + treasury LP yield allocation
2. **Rolling average**: `targetRate = avg(last 3 epochFeeRates)`
3. **Apply cap**: `targetRate = min(targetRate, MAX_EPOCH_RATE)` where `MAX_EPOCH_RATE = 0.40 / EPY` (40% annualized)
4. **CD SWF smoothing**:
   - If organic fees > targetRate: save excess to CD SWF reserve
   - If organic fees < targetRate: draw deficit from CD SWF (capped by balance)
   - If CD SWF exhausted: pay organic rate (no smoothing possible)
5. **Treasury LP yield feed**: `m_treasuryLpYield` contributes a slice each epoch to CD SWF
6. **Distribute**: all active Epoch-Yield CDs receive `targetRate × principal` pro-rata

### Lagged Start
- First 3 epochs (or configurable): CD pool accumulates fees but **no yield distributed**
- This builds the CD SWF reserve before any yield obligation exists
- After lag: yield begins at the rolling average rate
- Users who lock during the lag period start earning yield when distribution begins (pro-rata for their active epochs)

### CD Parameters
| Parameter | Default | Description |
|-----------|---------|-------------|
| `term_epochs` | user-chosen | Lock duration (from CD_ALLOWED_TIERS: 1, 18, 36, 72) |
| `amount` | user-chosen | HEAT to lock |

### Code Changes
- `CryptoNoteConfig.h`:
  - `YIELD_MACHINE_LAG_EPOCHS = 3`
  - `YIELD_MACHINE_ROLLING_WINDOW = 3`
  - `YIELD_MACHINE_MAX_APY_BPS = 4000` (40%)
  - `YIELD_MACHINE_SWF_SAVE_PCT = 60` (% of surplus saved to SWF)
- New state in `Blockchain`:
  - `m_yieldMachineState { cdSwf, rollingRates[], lagEpochCounter }`
  - `m_epochYieldCds[]` — active CD tracking
- New wallet command or RPC param: `create_epoch_cd <amount> <term_epochs>` (replaces current `create_cd`)

## 6. Staged Unlock for Edition CDs

Reuse existing `StagedDepositUnlock` infrastructure:

```
Edition CD (term=36 epochs, 5 stages):
  Stage 1 (epoch 36+7):  20% principal + 100% yield unlocked
  Stage 2 (epoch 36+14): 20% principal unlocked
  Stage 3 (epoch 36+21): 20% principal unlocked
  Stage 4 (epoch 36+28): 20% principal unlocked
  Stage 5 (epoch 36+35): 20% principal unlocked → fully unlocked
```

The existing `StagedUnlockConfig` (STAGE_INTERVAL_BLOCKS=25920, 5 stages, 20% each) maps naturally to this. Edition CDs set `useStagedUnlock=true` by default.

## 7. Implementation Phases

### Phase 1: Fee Split + ReBalancer Vault
- Add `SWAP_FEE_REBALANCER_SHARE_PCT = 8`
- Adjust `SWAP_FEE_TREASURY_SHARE_PCT` to 12
- Route rebalancer share to dedicated vault
- Wire rebalancer to draw from its vault first, then treasury

### Phase 2: Treasury LP Contribution
- Add `TREASURY_LP_CONTRIBUTION_PCT`
- Epoch boundary: treasury deposits XFG → Hearth → mints protocol LP shares
- Track `m_protocolLpShares` growth toward bootstrap replacement
- Treasury LP yield feeds into CD SWF (Phase 3)

### Phase 3: Yield Emission Machine
- Add `m_yieldMachineState` with CD SWF, rolling rates, lag counter
- Modify epoch boundary to run YEM after fee split
- Replace current `recordEpochFeeRate` with YEM-smoothed rate
- Existing `calculateCdInterest` continues to work (reads per-epoch rates)

### Phase 4: Edition CDs
- Add `EditionConfig` struct and edition state machine
- New wallet command: `create_edition_cd`
- Treasury pre-commitment at edition open
- Maturity + staged unlock at term

### Phase 5: Epoch-Yield CDs
- Wire YEM output to Epoch-Yield CD holders
- Lagged start (3 epochs of reserve building)
- APY cap enforcement
- CD SWF surplus/deficit mechanics

### Phase 6: Bootstrap Exit
- Once protocol LP value ≥ bootstrap owed, enable `withdraw_bootstrap` command
- Bootstrap contributor can withdraw their original seed (XFG + HEAT)
- Pool continues with protocol-owned LP + user LP

## 8. Simulation Plan

New simulation: `sim_cd_hybrid_v2.py`

### Models to Compare
| # | Model | CD Types | Yield Mechanism |
|---|-------|----------|----------------|
| H1 | Baseline (Current) | Epoch-Variable only | Raw fee share (80/20 split) |
| H2 | M5 Pre-Funded | Epoch-Variable only | Treasury pre-committed APY |
| **H3** | **Hybrid v2** | **Edition + Epoch-Yield** | **YEM (CD SWF + rolling 3-epoch + lag + cap + LP yield feed)** |

### What H3 Tests
1. **Edition CDs**: treasury-pre-committed fixed-rate capacity-limited CDs
2. **Epoch-Yield CDs**: YEM-smoothed variable-rate CDs
3. **Fee split**: 80% CD / 12% Treasury / 8% ReBalancer
4. **Treasury LP contribution**: treasury buys LP shares, earns LP yield, feeds CD SWF
5. **Lag**: first 3 epochs no yield → builds CD SWF reserve
6. **APY cap**: max 40% annualized

### Metrics
| Metric | Weight |
|--------|--------|
| CD APY (avg across CD types) | 35% |
| Treasury preservation | 25% |
| APY stability | 20% |
| Fee efficiency | 10% |
| Pool depth (liquidity) | 10% |

## 9. Config Constants Summary

```cpp
// Fee split
constexpr uint64_t SWAP_FEE_CD_SHARE_PCT        = 80;   // 80% → CD Yield Pool
constexpr uint64_t SWAP_FEE_TREASURY_SHARE_PCT  = 12;   // 12% → Treasury (was 20)
constexpr uint64_t SWAP_FEE_REBALANCER_SHARE_PCT = 8;   // 8% → ReBalancer Vault (new)

// Treasury LP bootstrapping
constexpr uint64_t TREASURY_LP_CONTRIBUTION_PCT  = 5;   // % of treasury contributed to LP per epoch

// Yield Emission Machine
constexpr uint32_t YIELD_MACHINE_LAG_EPOCHS       = 3;     // epochs before yield begins
constexpr uint32_t YIELD_MACHINE_ROLLING_WINDOW   = 3;     // epochs for rolling average
constexpr uint64_t YIELD_MACHINE_MAX_APY_BPS      = 4000;  // 40% annualized cap
constexpr uint64_t YIELD_MACHINE_SWF_SAVE_PCT     = 60;    // % surplus saved to CD SWF
constexpr uint64_t YIELD_MACHINE_LP_FEED_PCT      = 50;    // % treasury LP yield → CD SWF

// Edition CDs
constexpr uint64_t EDITION_DEFAULT_RATE_BPS        = 800;  // 8% default guaranteed APY
constexpr uint64_t EDITION_DEFAULT_TERM_EPOCHS     = 36;   // default term
constexpr uint64_t EDITION_DEFAULT_MAX_CAPACITY    = 1'000'000'000'000ULL; // 1M HEAT (in atomic units)
```

## 10. Wallet Commands (New)

| Command | Description |
|---------|-------------|
| `list_editions` | List available/active Edition CDs |
| `create_edition_cd <edition_id> <amount>` | Lock HEAT into a fixed-rate edition |
| `create_epoch_cd <amount> <term_epochs>` | Lock HEAT into epoch-yield CD (replaces `create_cd`) |
| `list_my_eds` | List user's active Edition CD positions |
| `list_my_eycds` | List user's active Epoch-Yield CD positions |
| `yield_status` | Show YEM state (SWF balance, rolling avg, lag status, current target rate) |
| `yield_project <amount> <term>` | Project estimated yield for epoch-yield CD |
