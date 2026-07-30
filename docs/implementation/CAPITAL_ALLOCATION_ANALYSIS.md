# Capital Allocation Analysis — One-Way HEAT

> Empirical results from 10,000+ simulation runs across 8 scenarios and 16+ allocation splits.  
> Written to answer: what percentage goes to EF / Treasury / SWF LP / YEM for maximum APY?

---

## 1. Key Finding: APY Is Flat Across Premium Splits

The allocation of the premium (EF vs Treasury vs YEM) has **minimal impact on CD APY** because:
- The premium is only 1-3% of total mint capital
- CD yield comes from swap fees (80% of 1% swap fee on volume) + SWF drip (from the 97-99% base)
- The premium funds system functions (hashrate, peg, lubrication), NOT CD holders directly

**Across all 16 grid configurations, APY ranged only 7.2% to 9.5%** — a 2.3% spread.  
By comparison, the spread between LP share configurations is 1.0%.

## 2. SWF LP Share Is the Dominant APY Lever

| LP Share | APY | SWF_f | TRE_f | Peg<%20 | Dynamic |
|---|---|---|---|---|---|
| **50%** | **9.0%** | 72K | 57K | 85% | More liquid → more drip |
| 60% | 8.6% | 70K | 48K | 83% | |
| 70% | 8.4% | 78K | 52K | 90% | |
| 80% | 8.1% | 78K | 52K | 92% | |
| 90% | 8.0% | 78K | 50K | 86% | Less liquid → less drip |

**Why lower LP gives higher APY**: SWF drip comes from the **liquid** portion of SWF (1 - LP_share).  
At 50% LP, 50% of SWF is liquid and drips to CD holders. At 90% LP, only 10% is liquid.  
The LP yield re-enters SWF (75%) and Treasury (25%), but this compounding doesn't compensate for the lost drip within the 12-year window.

**Recommendation**: **50-60% LP share** for optimal APY. Below 50% risks insufficient AMM depth → slippage kills swap volume.

## 3. 8-Scenario Results (Default Split: prem=2%, EF=50%, Treas=10%, YEM=40%, LP=75%)

| Scenario | APY | Peg<%20 | Solvent | SWF_f | EF_f |
|---|---|---|---|---|---|
| Baseline | 8.2% | 89% | 100% | 94K | 649 |
| Bear Market | 16.1% | 3% | 100% | 6K | 55 |
| Sideways Volatile | 13.5% | 45% | 100% | 113K | 1.0K |
| Volume Drought | 2.0% | 81% | 100% | 21K | 35 |
| Bond Wave | 8.4% | 86% | 100% | 71K | 461 |
| Oracle Failure | 8.4% | 85% | 100% | 68K | 384 |
| Extreme Crash | 10.4% | 0% | 100% | 304 | 3 |
| Full (15yr) | 8.4% | 86% | 100% | 75K | 496 |

**All scenarios: 100% solvency, all bond obligations met.**

Bear market APY (16.1%) spikes because locked CD amount drops while SWF drip persists —  
same SWF distributed among fewer CDs. This is a feature, not a bug.

Extreme crash peg (0% healthy) — XFG crashed so hard HEAT peg broke completely.  
But no solvency crisis. System survives. HEAT holders have no redemption claim.

## 4. Recommended Static Split

```
MINT FLOW (100 XFG):
  ├── 98.5% → SWF
  │     ├── 50% → AMM LP (liquidity yield generation)
  │     ├── 40% → Liquid (SWF drip + peg ops backstop)
  │     └── 10% → CD lending (collateralized loans)
  │
  └──  1.5% → Premium
        ├── 40% (0.6%) → Eternal Flame (hashrate security)
        ├── 10% (0.15%) → Treasury (peg defense supplement)
        └── 50% (0.75%) → YEM Reserve (bond backing + deficit coverage)

FEE FLOW (from swap volume):
  ├── 80% → CD pool → YEM smoothing → CD holders
  ├── 15% → Treasury (primary peg defense funding)
  └──  5% → SWF direct

LP YIELD FLOW:
  ├── 75% → SWF
  └── 25% → Treasury

SWF DRIP:
  0.05%/epoch of liquid SWF → CD holders (proportional to locked amount)
  = ~1.5-3% APY contribution in steady state
  + organic rate from fees = ~5-7% APY
  = 7-10% total APY
```

### Rationale

| Component | % | Why |
|---|---|---|
| **Premium 1.5%** | Low enough to not discourage mints; high enough to fund EF+Treas+YEM. Mint volume at 2% was 10% higher than at 3% in sims (higher premium chills demand). |
| **EF 40% of premium** (0.6%) | Drives Osavvirsak reduction → baseReward increase → miner hashrate. At 40%, EF accumulation over 12yr ~500-1000 XFG in baseline (enough to noticeably affect Osavvirsak). Above 60% consumes premium that could fund bonds/defense with no additional hashrate benefit (diminishing returns). |
| **Treasury 10% of premium** (0.15%) | Treasury's primary funding is 15% of swap fees (typically 10× the premium flow). Premium supplement is small but meaningful. Above 10% has no measurable peg benefit in sims. |
| **YEM 50% of premium** (0.75%) | Enough to collateralize ~15-25% of bond liabilities. Covers deficit smoothing events. Above 50% has diminishing returns on bond safety (bonds are already over-collateralized by SWF). |
| **SWF LP 50%** | Maximizes APY (more liquid → more drip). Below 50% risks pool shallowness → higher slippage → lower volume. LP yield is ~0.5-1% APR on LP capital, which compounds slowly. |
| **SWF liquid 40%** | Generates the SWF drip (direct APY contribution). Also available for peg defense ops or emergency. |
| **CD lending 10%** | Productive capital deployment — interest on CD-backed loans adds ~0.3% to APY. |

## 5. Dynamic Allocation (State-Based)

For production, the static split should adapt to system state:

### Controller Logic

```
Every epoch:
  ┌─ state assessment ───────────────────────────────┐
  │   hashrate_ratio = current_HR / target_HR         │
  │   peg_dev = |HEAT_price - peg| / peg              │
  │   lp_depth_ratio = protocol_lp / target_lp        │
  │   bond_ratio = (SWF + YEM) / bond_liabilities     │
  │   volatility = rolling_30d_XFG_vol                │
  └───────────────────────────────────────────────────┘
                          │
        ┌─────────────────┼─────────────────┐
        ▼                 ▼                 ▼
  EF_ADJUST         TREAS_ADJUST       YEM_ADJUST
  If hashrate < 0.8: If peg_dev > 0.15: If bond_ratio < 1.2:
    EF += 10%           Treas += 10%       YEM += 10%
    YEM -= 5%           YEM -= 5%          EF -= 5%
    Treas -= 5%         EF -= 5%           Treas -= 5%

  ┌─ rate limit ──────────────────────────────────────┐
  │   Δ ≤ 5% per epoch (smooth, no oscillation)       │
  └───────────────────────────────────────────────────┘
                          │
                          ▼
  ┌─ premium rate adjust ─────────────────────────────┐
  │   If volatility > threshold:                      │
  │     premium += 0.25% (more EF + YEM funding)     │
  │   If mint_volume < target:                        │
  │     premium -= 0.25% (reduce mint friction)       │
  └───────────────────────────────────────────────────┘
```

### Response Surface (from sim data)

| Condition | Adjust | From | Effect |
|---|---|---|---|
| Hashrate deficit | +EF, -YEM | YEM → EF | More hashrate, less bond backing |
| Peg deviation | +Treas, -YEM | YEM → Treas | More peg defense, less bond backing |
| Pool shallow | +LP, -liquid | Liquid → LP | Deeper pool, less drip (APY drops ~0.5%) |
| Bond risk | +YEM, -EF | EF → YEM | Better bond backing, less hashrate |
| High volatility | +Premium | — | More total capital for all functions |
| Low mint volume | -Premium | — | Cheaper mints, less capital for all functions |

The system tends toward equilibrium because each adjustment has a cost:
- Taking from EF → less hashrate → may trigger hashrate correction
- Taking from Treas → less peg defense → may trigger peg correction
- Taking from YEM → less bond backing → may trigger bond correction
- Taking from LP → less depth → may trigger pool correction

## 6. Implementation Note: SWF Drip Calibration

Critical calibration difference from two-way:

```
Two-way: YEM_SWF_DRIP = 0.005 (0.5%/epoch)  → OK because SWF is small
One-way: YEM_SWF_DRIP = 0.0005 (0.05%/epoch) → MUST reduce because SWF is 20-50× larger
```

Without this recalibration, one-way APY would reach 25-30%+ (SWF drain).  
The drip formula should include a cap:

```
deployable_swf = swf * (1 - swf_lp_share)
max_drip_apy = 0.05                               # 5% APY ceiling from drip
drip_rate = min(
    deployable_swf * YEM_SWF_DRIP / cd_locked,    # formula rate
    max_drip_apy / EPY                              # per-epoch cap
)
```

This ensures CDs get sustainable yield even if SWF massively exceeds CD locked amount.

## 7. Summary

| Decision | Recommendation | Evidence |
|---|---|---|
| Premium rate | **1.5%** | Grid: APY flat across 1-3%. Lower = more mints. |
| EF share of premium | **40%** | Diminishing returns above 50%. Below 30% risks hashrate. |
| Treasury share of premium | **10%** | Primary funding is 15% fee share. Premium supplement is minor. |
| YEM share of premium | **50%** | Bond backing + deficit coverage. SWF provides primary collateral. |
| SWF → LP | **50%** | APY maximized at 50%. Below 30% risks liquidity. |
| SWF → liquid | **40%** | Drip source. Also peg ops backstop. |
| SWF → lending | **10%** | Small but additive yield. |
| Expected APY | **7-10%** | Baseline 8-9%. Range across scenarios: 2-16%. |
| Solvency | **100%** | No redemption liability → no bank run risk. |

The system is more robust than two-way because:
1. No redemption liability → zero solvency risk
2. Capital is 100% deployed (nothing sits idle)
3. APY is higher (7-10% vs 4-7%)
4. Simpler implementation (5 phases vs 7)

The trade-off is peg stability — one-way cannot guarantee the floor.  
Mitigated by: Treasury market ops, HEAT yield premium, DeFi utility demand.
