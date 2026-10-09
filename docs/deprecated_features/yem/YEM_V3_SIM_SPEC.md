# YEM v3 + Mode 4 — Simulation Spec

## Purpose

Monte Carlo system simulation testing YEM v3's CD smoothing engine, burn scalp, and bond program under Mode 4 fixed-peg flatcoin dynamics. Replaces `sim_v13_full.py` (Mode 2 vs M4 comparison). Only Mode 4 + YEM v3 matters now.

## Sim Architecture

```
PER EPOCH LOOP:
─────────────────
 1. Update XFG price (stochastic step)
 2. Update oracle (staleness model)
 3. Compute redemption_price = HEAT_PEG / oracle_price
 4. User activity:
    a. CD creation (Poisson arrival, term-sampled)
    b. CD maturity (unlock + claim)
    c. HEAT mint (correlated with HEAT price > peg)
    d. HEAT burn (correlated with HEAT price < peg)
    e. Swap volume (correlated with price volatility)
    f. Bond migration (pre-v10 COLD, front-loaded)
 5. Mode 4 epoch handler:
    a. Private arbitrage (treasury as counterparty, earns spread)
    b. Protocol arb (peg defense: buy HEAT when cheap, mint HEAT when expensive)
 6. YEM v3 epoch handler:
    a. Fee split: 80% CD / 15% treasury / 5% SWF
    b. LP yield feed: 75% of treasury LP earnings → SWF
    c. Burn scalp: 8% of mint premium → YEM Reserve
    d. Bond maturity + coupon processing
    e. Lag check (3 epochs → 100% CD pool → SWF)
    f. Rolling average + target rate
    g. SWF smoothing (surplus → save 60%, deficit → draw)
    h. SWF drip (1% → all CD holders)
    i. Per-CD yield calculation (capped rate + drip)
 7. State update:
    a. HEAT supply +/- from mint/burn
    b. AMM pool rebalancing from trades + arb
    c. Treasury growth/drain from peg defense + LP bootstrapping
    d. SWF / YEM Reserve / ReBalancer balances
 8. Record metrics for this epoch
```

## Stochastic Parameters

| Parameter | Distribution | Baseline | Notes |
|---|---|---|---|
| XFG daily return | Normal(μ, σ) | μ=0.0003, σ=0.04 | ~73% annual vol, mild upward drift |
| XFG price shocks | Poisson(λ=0.02) × Normal(-0.3, 0.1) | Rare crashes | 2% per epoch crash probability |
| XFG price jumps | Poisson(λ=0.05) × Normal(0, 0.5) | Mixed-sign jumps | 5% per epoch, moderate |
| CD creation rate | Poisson(λ·totalCdLocked) | Price-dependent λ | λ scales with HEAT price confidence |
| CD term selection | Weighted Categorical | Heavily skewed to 72+ | Model: users prefer max cap (80%) |
| Swap volume | Lognormal(μ, σ) | μ=log(epochCdLocked·0.01), σ=0.5 | Vol-correlated |
| Mint volume | Logistic(sigmoid) on price deviation | Threshold at +2% above peg | Only when HEAT > $1.61 |
| Burn volume | Logistic(sigmoid) on price deviation | Threshold at -2% below peg | Only when HEAT < $1.55 |
| Oracle staleness | Bernoulli(p=0.02) per epoch | 2% failure per epoch | Tier fallback → freeze |
| Bond migrations | Front-loaded vector | 30% of pre-v10 COLD over 10 epochs | Sigmoid adoption curve |

## Oracle Model

```
Exbitron:   oracle_price = xfg_price × (1 + ε), ε ~ Normal(0, 0.005)
            staleness = Bernoulli(0.02)
SwapXFG:    oracle_price = xfg_price × (1 + ε), ε ~ Normal(0, 0.02)
            staleness = Bernoulli(0.05), min_trades=5 requirement
Freeze:     oracle_price = last_known_good   (if both stale > freeze_epochs)
            no arb, no mint, no burn, peg floats on pool only
```

## Mode 4: Two-Way Mint/Burn

```
HEAT_PEG = 1.58 ($1.58 in Dec 2008 dollars)
redemption_price = 1.58 / oracle_xfg_price    (XFG per 1 HEAT)

MINT: user_pays_xfg = heat_wanted × redemption_price × 1.05
      user_receives_heat = heat_wanted
      protocol_gets_xfg = 0.05 × heat_wanted × redemption_price  (5% premium)
      → 92% of premium → EternalFlame (boosts block rewards)
      →  8% of premium → YEM Reserve (paper credit)

BURN: user_burns_heat = heat_burned
      user_receives_xfg = heat_burned × redemption_price × 0.95
      protocol_gets_xfg = 0.05 × heat_burned × redemption_price   (5% premium → treasury)
      → protocol pays XFG from treasury, burns HEAT from supply

ARB (protocol, per epoch):
      deviation = pool_ratio / peg_ratio - 1
      if |deviation| < 0.005: no action
      if pool_ratio > peg_ratio (HEAT overvalued): mint HEAT → sell to pool
      if pool_ratio < peg_ratio (HEAT undervalued): buy HEAT from pool → burn HEAT
      max arb = min(pool_depth × 0.01, treasury_balance × 0.10)
```

## YEM v3: CD Yield Model

```
IN LAG (epochs 0-2):
  organic_rate = swap_fee_cd_share / total_cd_locked  (computed, not distributed)
  100% of CD pool → SWF
  yield_distributed = 0
  bond_coupons = bond_principal × min(3%/yr, bond_rate) / EPY
  coupons paid from YEM Reserve → coinbase

POST-LAG (epochs 3+):
  target_rate = mean(organic_rates[e-3], organic_rates[e-2], organic_rates[e-1])
  for each CD:
    cap = computeTierCap(cd.term_epochs)
    base_yield = min(target_rate, cap)
    drip_share = (swf × 0.01 / total_cd_locked)
    cd.yield_this_epoch = cd.amount × (base_yield + drip_share)

  surplus = cd_pool - sum(cd.yield_this_epoch)
  if surplus > 0: swf += surplus × 0.60
  if deficit > 0:
    drawn = min(deficit, swf)
    swf -= drawn
    remaining = deficit - drawn
    if remaining > 0:
      coinbase_payout(FEE_POOL, min(remaining, yem_reserve))
      yem_reserve -= min(remaining, yem_reserve)

BONDS (per epoch, post-lag):
  for each active bond:
    coupon = principal × bond_rate / EPY
    coinbase_payout(creditor, coupon)
    yem_reserve -= coupon

MATURITY (per epoch):
  for each bond where current_epoch >= issued_epoch + term:
    total_owed = principal + final_coupon
    repay_from_swf = min(total_owed, swf)
    swf -= repay_from_swf
    coinbase_payout(creditor, total_owed - repay_from_swf)
    yem_reserve -= (total_owed - repay_from_swf)
```

## Fee Split (Mode 4)

```
swap_fees_per_epoch → 80% CD pool → YEM engine
                    → 15% Treasury (peg defense + LP bootstrapping)
                    →  5% SWF direct feed
```

## Balance Sheet Tracking

Per-epoch columns:

| Column | Source |
|---|---|
| epoch | incremental |
| xfg_price_usd | stochastic step |
| oracle_price | oracle model |
| redemption_price | 1.58 / oracle_price |
| heat_peg_deviation | pool_ratio / peg_ratio - 1 |
| swap_volume_xfg | Lognormal |
| swap_fees_xfg | swap_volume × 0.01 |
| cd_pool_xfg | 80% of swap_fees |
| treasury_income_xfg | 15% of swap_fees |
| swf_direct_xfg | 5% of swap_fees |
| mint_volume_xfg | mint model |
| mint_premium_xfg | 5% of mint_volume |
| burn_volume_heat | burn model |
| burn_premium_xfg | 5% of burn_heat × redemption_price |
| scalp_to_reserve | 8% of mint_premium |
| total_cd_locked | cumulative create - mature + accrued yield |
| organic_rate | cd_pool / total_cd_locked |
| rolling_avg_rate | mean(last 3 organic) |
| swf_balance | SWF state |
| yem_reserve | YEM Reserve state |
| treasury_balance | Treasury state |
| cd_yield_paid | sum of all CD yield this epoch |
| bond_coupons_paid | sum of bond coupons |
| bond_principal_repaid | sum of matured bond principals |
| heat_supply | cumulative mint - burn |
| amm_pool_xfg | pool reserve XFG |
| amm_pool_heat | pool reserve HEAT |
| arb_profit_xfg | treasury profit from peg defense trades |
| cd_apy_mean | mean annualized yield across all CDs |
| cd_apy_min | worst CD APY this epoch |
| solvency_flag | 1 if all obligations met, 0 if shortfall |

## Scenario Profiles (2,000 runs each)

### 1. Baseline (Bullish XFG)
- XFG starts at $5, grows to $50 over 5yr, moderate vol
- Expected: treasury grows, SWF accumulates, CD APY healthy, HEAT peg holds

### 2. Bear Market (XFG Decline)
- XFG starts at $5, declines to $0.50 over 3yr, stays there
- Expected: treasury draws for peg defense, SWF buffer tested, YEM Reserve may drain

### 3. Volatile Sideways
- XFG oscillates $2-$8 with 120% annual vol, jump shocks
- Expected: most stressful for YEM smoothing, SWF tested on both sides

### 4. Volume Drought
- XFG stable at $5, swap volume at 10% of baseline for 50 epochs
- Expected: SWF should sustain CD yield through drought using accumulated reserves

### 5. Bond Wave vs Reserve
- 30% of pre-v10 COLD (simulated as 500K XFG) migrated to bonds over 10 epochs
- Bond maturity wave at epoch 72 — all bonds mature simultaneously
- Expected: YEM Reserve + SWF must cover the spike

### 6. Oracle Failure Cascade
- Exbitron fails at epoch 50, SwapXFG fails at epoch 55
- Stay frozen for 30 epochs, then recover
- Expected: HEAT floats on pool only, peg widens but doesn't collapse, no death spiral

### 7. Extreme Crash + Recovery
- XFG drops 80% in 5 epochs (flash crash), then recovers over 20 epochs
- Expected: CD APY doesn't crater — SWF smoothing absorbs the shock

### 8. Full System (All Features Active)
- All stochastic parameters baseline
- 15yr, 2000 epochs
- Expected: full stress test of all interactions

## Acceptance Criteria

| Metric | Threshold | Scenario |
|---|---|---|
| Mean CD APY (post-lag, annualized) | ≥ 2% | All scenarios |
| Worst single-epoch CD APY | ≥ 0.5% | All scenarios |
| HEAT peg deviation < 20% for | > 75% of epochs | Baseline, Sideways |
| HEAT peg deviation < 50% for | > 90% of epochs | Bear Market |
| SWF never negative | 100% of epochs | All scenarios |
| YEM Reserve never negative | 100% of epochs | All scenarios |
| Treasury never negative | > 95% of epochs | All scenarios |
| All bond obligations met | 100% | All scenarios |
| No YEM Reserve overdraw | 100% | All scenarios |
| Death spiral events (HEAT → 0) | < 5% of runs | All scenarios |
| Lag period completes (SWF ≥ 0 after lag) | 100% | All scenarios |

## Output

`yem_v3_sim_output.csv` — one row per epoch × run, all balance sheet columns.

`yem_v3_sim_summary.json`:
```json
{
  "scenarios": {
    "baseline": { "runs": 2000, "mean_cd_apy": 3.95, "worst_cd_apy": 2.1, "peg_held_pct": 82, "death_spirals": 0, "yem_reserve_drained": 0 },
    "bear_market": { "runs": 2000, ... },
    ...
  },
  "config": {
    "heat_peg": 1.58,
    "burn_scalp_bps": 800,
    "swf_save_pct": 60,
    "swf_drip_bps": 100,
    "lag_epochs": 3,
    "rolling_window": 3,
    "tier_cap_min": 3300,
    "tier_cap_max": 8000,
    "bond_max_rate": 2500,
    "yem_activation_epoch": 0,
    "fee_split": "80/15/5"
  }
}
```

`yem_v3_sim_plots/` — per-scenario time series charts:
- `xfg_price.png`
- `heat_peg_deviation.png`
- `cd_apy.png` (mean + min band)
- `swf_balance.png`
- `yem_reserve.png`
- `treasury_balance.png`
- `heat_supply.png`
- `solvency_heatmap.png` (epoch × run, green = solvent, red = shortfall)

## Implementation

Write `scripts/yem_v3_sim.py` as a standalone Python script (no blockchain dependency). All dynamics are simulated in pure math. Input: the config constants from `CryptoNoteConfig.h`. Output: CSV + JSON + plots.

Initial state:
```
xfg_price_start       = 5.00 (USD)
total_cd_locked_start = 1_400_000_000_000_000  (~1.4M XFG in atomic units, current mainnet estimate)
swf_start             = 0
yem_reserve_start     = 0
treasury_start        = 20_000_000_000_000      (~20K XFG)
amm_pool_xfg_start    = 100_000_000_000_000     (~100K XFG)
amm_pool_heat_start   = 800_000_000_000_000     (~800K HEAT, 8:1 ratio)
heat_supply_start     = 10_000_000_000_000_000  (~10M HEAT)
epy                   = 72  (epochs per year)
epoch_duration_days   = 5
bond_program_xfg      = 500_000_000_000_000     (~500K XFG in pre-v10 COLD)
```

Run: `python scripts/yem_v3_sim.py --scenarios all --runs 2000 --output sim_out/`
