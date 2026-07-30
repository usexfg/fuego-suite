# Capital Allocation Optimization

> Every XFG from a HEAT mint is capital to be deployed. The question is: what split across Eternal Flame (hashrate), Treasury (peg defense), SWF LP (liquidity yield), and YEM (system lubrication) maximizes CD APY while keeping the system healthy?

---

## 1. The Recycling Loop

In the two-way model, 95% of every mint sat idle in the Collateral Reserve — dead capital that backed HEAT but produced nothing. In the one-way model, that capital is freed. Every XFG can work.

```
User pays 100 XFG → mint HEAT
  │
  ┌──────────┬──────────┬──────────┬──────────┐
  ▼          ▼          ▼          ▼          ▼
  EF        TREAS      SWF_LP     YEM       SWF_Liq
(Hashrate) (Peg Def)  (Fee Gen)  (Lube)   (Reserve)

Each destination generates a different return:
  EF      → baseReward↑ → hashrate↑ → network security
  TREAS   → peg defense capacity↑ → HEAT price stability
  SWF_LP  → swap fees↑ → CD APY↑ (direct)
  YEM     → bond backing↑ → system stability
  Liq     → dry powder for peg ops or emergencies
```

The optimization problem: allocate the mint capital across these destinations to maximize CD APY, subject to:
- Minimum hashrate for network security
- Maximum acceptable peg deviation
- Bond collateralization ≥ 1.0
- 8M8 supply cap (never violated)

---

## 2. Return-on-Capital Models

Each destination has a different "yield curve" — increasing allocation has diminishing returns, and at some point the marginal benefit of moving capital becomes negative.

### 2a. Eternal Flame (Hashrate Security)

```
EF_Allocation → ΔOsavvirsak = -EF_Amount
             → ΔbaseReward = EF_Amount / (2^20 / blocks_per_day) per day
             → Δhashrate = f(baseReward, xfg_price, energy_cost)
```

**Model**: `hashrate = K × sqrt(baseReward × xfg_price / energy_cost)`

**Diminishing returns**: More EF → more block rewards → more hashrate, but hashrate has a soft ceiling (hardware availability, energy constraints). The security benefit is:

```
security_value = min(hashrate / target_hashrate, 1.0)
```

Above target hashrate, additional EF yields no security benefit. Below target, security is constrained, and user confidence drops.

**Target**: EF allocation should hit the target hashrate and no more. The target hashrate is determined by:
- Daily transaction volume (need enough hashrate to prevent 51% attack on recent history)
- Block time target (too much hashrate → too-fast blocks → orphan risk)
- Historical norms for the network's security budget

**Estimated efficiency**: For every 1 XFG sent to EF:
- ~1 XFG returns as increased baseReward over the lifetime of the system
- ~20-30% of that is captured back by the protocol via swap fees when miners spend their rewards
- Net: 0.2-0.3 XFG effective return per 1 XFG allocated to EF (over many years)

### 2b. Treasury (Peg Defense)

```
Treasury_Allocation → peg_defense_capacity = Treasury_balance
                   → max_buy_power = Treasury_balance / HEAT_supply
```

**Model**: `peg_deflection = σ / (1 + Treasury / HEAT_supply × K_peg)`
where σ is the standard deviation of HEAT's market price drift without intervention.

**Diminishing returns**: Linear at low Treasury (big impact per XFG), diminishing at high Treasury (market depth becomes the constraint, not buying power).

**Target**: Treasury should be large enough to absorb worst-case 1-day sell pressure:
```
min_Treasury = HEAT_supply × expected_worst_daily_sell_pct
```

Above this, additional Treasury is idle (but available as emergency reserve).

**Estimated efficiency**: For every 1 XFG in Treasury:
- ~0.05-0.10 XFG/year in peg stability value (measured as reduced HEAT volatility)
- Indirect return through higher HEAT demand (more stable peg → more mints)
- Net: 5-10% annual effective return (not direct yield, but ecosystem health)

### 2c. SWF LP (Liquidity Yield)

```
SWF_LP → AMM pool depth = LP_XFG + LP_HEAT
       → swap_fee_volume = base_volume × f(depth, slippage_curve)
       → fee_revenue = swap_fee_volume × fee_rate
       → LP_yield = fee_revenue × (protocol_LP_share / total_LP)
```

**Model**: `fee_volume = base_volume × (1 - e^(-K_lp × depth))`
More LP depth → less slippage → more volume → more fees. But after a point, additional LP doesn't attract more volume.

**Diminishing returns**: Sharp at low depth (first LP XFG has highest marginal return), then flattens.

**Target**: LP depth should be at least 10× the largest single trade expected, but no more than 50× (beyond which fee revenue doesn't grow significantly).

**Estimated efficiency**: For every 1 XFG in SWF LP:
- 0.5-2% APR in swap fees (direct, ongoing)
- 75% of fee revenue returns to SWF (which drips to CD holders)
- Net: 0.375-1.5% annual direct return

### 2d. YEM Lubrication

```
YEM_Allocation → YEM_Reserve_credit = YEM_Allocation
              → bond_collateralization_ratio = (SWF + YEM) / Bond_Liabilities
              → deficit_absorption_capacity = YEM_Reserve
```

**Model**: YEM Reserve doesn't generate yield directly. It provides:
1. Bond collateral (enables legacy migration → SWF capital)
2. Deficit coverage (smooths CD yield during low-volume epochs)
3. Protects SWF from drainage during bond maturity waves

**Target**: YEM should be at least 20% of bond liabilities for safe collateralization ratio.

**Diminishing returns**: After YEM > 50% of bond liabilities, additional YEM has minimal marginal benefit.

**Estimated efficiency**: For every 1 XFG in YEM Reserve (paper credit):
- Enables ~2 XFG of bond migration → SWF capital
- Prevents SWF drawdown of ~0.5 XFG per deficit event
- Indirect: enables higher CD APY by absorbing shock
- Net: 0.1-0.3 XFG/year value in avoided SWF drainage

---

## 3. The Optimization Problem

### Variables

```
For each 100 XFG minted:
  E = % to Eternal Flame  (0 ≤ E ≤ 100)
  T = % to Treasury        (0 ≤ T ≤ 100)  
  L = % to SWF LP          (0 ≤ L ≤ 100)
  Y = % to YEM Reserve     (0 ≤ Y ≤ 100)

  Constraint: E + T + L + Y = 100
```

Actually, SWF also has a liquid portion. Let me refine:

```
SWF_breakdown:
  L = % to LP positions
  R = % to liquid reserve
  S = % to CD lending

  Constraint: L + R + S = swf_pct  (where swf_pct = % of mint going to SWF)
```

But for the top-level optimization, the key decision is the split across E, T, L, Y.

### Objective

```
Maximize: CD_APY(E, T, L, Y)

Where CD_APY = base_yield(L)          // from swap fees generated by LP depth
              + swf_drip(L, S)         // from SWF drip (fed by LP yield + CD lending)
              + peg_stability_bonus(T) // tighter peg → more HEAT demand → more fees
              + confidence_factor(E)    // hashrate security → user trust → adoption
```

Wait, confidence and peg stability don't contribute to APY directly — they contribute to the SUSTAINABILITY of the system. A good allocation might sacrifice short-term APY for long-term health.

**Better formulation**: Multi-objective optimization.

```
Maximize: w1 × CD_APY + w2 × hashrate_security + w3 × peg_stability + w4 × bond_safety

Subject to:
  E + T + L + Y = 100
  E ≥ E_min (minimum hashrate floor)
  T ≥ T_min (minimum peg defense floor)
  L ≥ L_min (minimum LP depth for swap functionality)
  Y ≥ Y_min (minimum bond collateralization)
```

Where w1 >> w2, w3, w4 (APY is the primary objective, others are constraints).

### Default Split Candidates

Based on the return-on-capital models:

| Split | E (EF) | T (Treas) | L (SWF LP) | Y (YEM) | Expected APY | Risk |
|---|---|---|---|---|---|---|
| Conservative | 2.0% | 5.0% | 88.0% | 5.0% | 8-10% | Low peg def. |
| Aggressive LP | 1.0% | 2.0% | 95.0% | 2.0% | 10-14% | Low hashrate |
| Balanced | 2.0% | 8.0% | 80.0% | 10.0% | 7-10% | Balanced |
| Hashrate-focus | 5.0% | 5.0% | 80.0% | 10.0% | 6-9% | High security |
| DeFi-focus | 1.0% | 2.0% | 96.0% | 1.0% | 11-15% | High peg risk |

Note: "Expected APY" is rough — it depends heavily on mint volume, swap volume, and XFG price.

---

## 4. Dynamic Allocation

The optimal split is NOT static. It depends on system state:

```
State variables:
  h = current hashrate / target hashrate
  p = current HEAT peg deviation (absolute)
  d = current LP depth relative to volume
  b = bond_collateralization_ratio

Decision:
  if h < 0.8:  increase E (need more hashrate)
  elif p > 0.1: increase T (need more peg defense)
  elif d < 5:  increase L (need more LP depth)
  elif b < 1.2: increase Y (need more bond backing)
  else:         maintain balanced split
```

**Allocation function**:

```
E_alloc = E_base + max(0, (1.0 - h) × K_boost_E)
T_alloc = T_base + max(0, (p / max_p) × K_boost_T)
L_alloc = L_base + max(0, (optimal_depth / d - 1.0) × K_boost_L)
Y_alloc = Y_base + max(0, (min_bond_ratio / b - 1.0) × K_boost_Y)

// Normalize to 100%
total = E_alloc + T_alloc + L_alloc + Y_alloc
E_split = E_alloc / total
T_split = T_alloc / total
L_split = L_alloc / total
Y_split = Y_alloc / total
```

### Control System Response

The allocation changes are gradual (not step functions) to avoid system shock:

```
ΔE_per_epoch = max(min(E_target - E_current, K_max_change), -K_max_change)
E_current += ΔE_per_epoch
```

This prevents oscillation and gives the system time to respond to each allocation change.

---

## 5. Simulation Framework

To find the optimal split, the simulation needs to model each function's response surface:

### Simulation Variables (Configurable)

```
ALLOC_EF     = 0.02    # 2% → Eternal Flame
ALLOC_TREAS  = 0.05    # 5% → Treasury
ALLOC_SWF_LP = 0.88    # 88% → SWF LP
ALLOC_YEM    = 0.05    # 5% → YEM Reserve
```

### Return Models (To Calibrate)

```
# Hashrate model
hashrate = K_hash × sqrt(ef_total / time_factor)

# Peg deviation model
peg_volatility = base_vol / (1 + treasury / heat_supply × K_peg)

# LP fee model
fee_volume = base_swap_vol × (1 - exp(-K_lp × lp_depth / base_swap_vol))
lp_yield = fee_volume × fee_rate × protocol_lp_share

# YEM model
bond_ratio = (swf + yem_reserve) / max(bond_liabilities, 1)
deficit_coverage = min(deficit, yem_reserve)
```

### Experiment Design

Run the simulation across a grid of allocation splits:

```
Grid:
  E: [0.5%, 1.0%, 2.0%, 3.0%, 5.0%]
  T: [1.0%, 3.0%, 5.0%, 8.0%, 12.0%]
  L: remaining after E + T + Y
  Y: [1.0%, 3.0%, 5.0%, 8.0%, 12.0%]

Total combinations: 5 × 5 × 5 = 125
Each: 200 runs × 876 epochs = 175,200 sim-years
Total: 21.9M sim-years  (computable in ~30 min)
```

Output: For each (E, T, L, Y) combination:
- Mean CD APY over 12 years
- Peg deviation (95th percentile)
- Hashrate security margin (if modeled)
- Bond solvency margin
- SWF + Treasury terminal balance

---

## 6. Implementation Plan

### Step 1: Build one-way simulation with configurable alloc

Create `scripts/yem_v3_oneway_sim.py` based on the existing sim but:
- One-way only (no HEAT→XFG burn)
- Configurable E, T, L, Y allocation
- Return-on-capital models for each function
- Dynamic allocation option (state-based)

### Step 2: Run grid search

Run the 125 combinations × 200 runs to map the response surface.

### Step 3: Identify pareto-optimal frontier

For each scenario (baseline, bear, drought, etc.), find the allocation that maximizes APY subject to constraints.

### Step 4: Design dynamic allocation rule

Based on the pareto frontier, design a state-based allocation function that adapts to conditions.

### Step 5: Backtest

Run the dynamic allocation against all 8 scenarios and compare to static baselines.

---

## 7. Wire Model (Minimal Viable)

For the first pass, the simulation needs these relationships:

```
Per 100 XFG minted:
  ├── EF_pct → EternalFlame_counter += EF_pct * mint_xfg
  │             (hashrate modeled externally as: HR = K_hr * sqrt(EF_total))
  │
  ├── TREAS_pct → treasury += TREAS_pct * mint_xfg
  │                peg_defense_capacity = treasury / heat_supply
  │
  ├── SWF_LP_pct → swf_lp += SWF_LP_pct * mint_xfg
  │                 lp_depth = swf_lp + other_lp
  │                 fee_volume = base_vol * min(1, lp_depth / optimal_depth)
  │
  └── YEM_pct → yem_reserve += YEM_pct * mint_xfg
                 collateral_ratio = (swf + yem_reserve) / bond_liabilities
```

Plus the SWF drip to CD holders:

```
swf_drip = swf_balance * drip_rate / EPY
holder_apy = (organic_rate + drip_rate) * EPY * 100
```
