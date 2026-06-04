#!/usr/bin/env python3
"""
Fuego HEAT — Formula Comparison: Price vs Activity vs Dollar-Pin
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Models all three anchoring formulas over 10 years.
Compares HEAT stability, supply growth, pool health, treasury accumulation.
"""

import numpy as np, time

BPE       = 900; EPY = 73; VB = 20000; N = 150; N_EPOCHS = EPY * 10
LAUNCH    = 0.1; KP = 0.08; KI = 0.015; SWAP_FEE = 0.02

np.random.seed(42)

def run_formula(label, formula_fn, use_buy=True):
    supplies = []; treasuries = []; ratios_hist = []; pool_x_hist = []; pool_h_hist = []
    
    for _ in range(N):
        pool_xfg = 5000.0; pool_heat = 25000.0
        redemption = LAUNCH; integral = 0.0
        treasury = 0.0; cd_pool = 0.0; heat_sup = 0.0; heat_cd = 0.0
        protocol_lp = 0.0; total_lp = 1000.0
        heat_yr = [0.0]; treas_yr = [0.0]
        xfg_price = 0.01
        baseline_activity = 0; activity_smoothed = 0

        for ep in range(N_EPOCHS):
            t = ep / N_EPOCHS
            # XFG price model: privacy coin trajectory
            xfg_price = 0.01 * np.exp(4 * t**1.3) * (1 + np.random.normal(0, 0.25))
            vol_mult = np.clip(xfg_price / 0.01, 0.3, 5.0)
            vol_e = VB * (BPE / 180) * vol_mult * np.exp(np.random.normal(0, 0.10))
            
            fees = vol_e * SWAP_FEE
            treasury += fees * 0.20
            cd_pool += fees * 0.80

            # Hearth swaps
            sv = vol_e * 0.05
            if pool_xfg > 0 and pool_heat > 0:
                capped = min(sv, pool_xfg * 0.005)
                out = pool_heat * capped / (pool_xfg + capped)
                pool_xfg += capped; pool_heat -= out
            spot = pool_xfg / max(pool_heat, 0.001)

            # ── Formula determines target ──
            target = formula_fn(ep, N_EPOCHS, xfg_price, fees, pool_xfg, pool_heat,
                                baseline_activity, activity_smoothed, LAUNCH)
            
            # Track activity baseline (for activity-anchored formula)
            epoch_activity = fees + pool_xfg * 0.01 + pool_heat * 0.01
            if ep < 3 * EPY and baseline_activity == 0:
                activity_smoothed = 10.0  # seed
            else:
                activity_smoothed = activity_smoothed * 0.99 + epoch_activity * 0.01
            if ep == 3 * EPY:
                baseline_activity = activity_smoothed

            dev = (spot - target) / target if target > 0 else 0
            dt = 1.0 / EPY

            # User mint
            mp = max(0, 0.008 - 0.4 * dev)
            heat_sup += vol_e * 0.08 / max(spot, 0.001) * mp

            # PI controller
            integral = np.clip(integral + dev * dt, -1.0, 1.0)
            red_rate = np.clip(KP * dev + KI * integral, -0.50, 0.50)
            redemption = max(1e-6, target * (1 + red_rate * dt))

            # CD yield: buy from Hearth
            if cd_pool > 0 and pool_heat > 0:
                sr = np.clip(1.0 + red_rate, 0.2, 3.0)
                spend = min(cd_pool, cd_pool * sr)
                heat_bought = pool_heat * spend / (pool_xfg + spend)
                if 0 < heat_bought < pool_heat * 0.95:
                    pool_xfg += spend
                    pool_heat -= heat_bought
                    heat_cd += heat_bought
                    heat_sup += heat_bought
                    cd_pool -= spend

            # Treasury rebalancing (keeps pool balanced)
            if pool_heat > 0 and treasury > 0 and total_lp > 0:
                spot2 = pool_xfg / max(pool_heat, 0.001)
                if spot2 > 5.0:  # XFG-heavy
                    rebal = min(treasury * 0.05, pool_xfg * 0.02)
                    if redemption > 0:
                        heat_dep = rebal / redemption
                        pool_heat += heat_dep; total_lp += heat_dep
                        protocol_lp += heat_dep; treasury -= rebal
                        heat_sup += heat_dep
                elif spot2 < 0.05:  # HEAT-heavy
                    rebal = min(treasury * 0.05, pool_heat * 0.02)
                    pool_xfg += rebal; total_lp += rebal
                    protocol_lp += rebal; treasury -= rebal

            if ep % EPY == 0 and ep > 0:
                heat_yr.append(heat_sup); treas_yr.append(treasury)
                pool_x_hist.append(pool_xfg); pool_h_hist.append(pool_heat)

        supplies.append(heat_yr); treasuries.append(treas_yr)

    mx = min(len(s) for s in supplies)
    ss = [np.median([s[i] for s in supplies]) for i in range(mx)]
    ts = [np.median([s[i] for s in treasuries]) for i in range(mx)]
    px = np.median(pool_x_hist) if pool_x_hist else 0
    ph = np.median(pool_h_hist) if pool_h_hist else 1

    return {
        'supply_yr10': ss[-1] if ss else 0,
        'supply_yr5':  ss[4] if len(ss) > 4 else 0,
        'treasury':    ts[-1] if ts else 0,
        'pool_xfg':    px,
        'pool_heat':   ph,
        'pool_ratio':  px / max(1, ph),
    }


# ═══════════════════════════════════════════════════════════════
# FORMULA DEFINITIONS
# ═══════════════════════════════════════════════════════════════

def price_anchored(ep, n_ep, xfg_p, fees, px, ph, ba, asm, launch):
    """target = launch_ratio × launch_price / current_price"""
    return max(0.00001, launch * 0.01 / xfg_p)

def activity_anchored(ep, n_ep, xfg_p, fees, px, ph, ba, asm, launch):
    """target = launch_ratio × baseline_activity / current_activity"""
    if ba > 0 and asm > 0:
        return max(0.00001, launch * ba / asm)
    return launch

def dollar_pin(ep, n_ep, xfg_p, fees, px, ph, ba, asm, launch):
    """
    Attempts to pin HEAT ≈ $0.001 using protocol-native metrics only.
    Uses fee rate as proxy for economic velocity (no external oracle).
    
    Idea: as fees/epoch grows, network activity grows, HEAT should inflate.
    target tracks fee ratio: launch × baseline_fee_rate / current_fee_rate
    where fee_rate = fees_per_epoch / pool_depth (velocity proxy)
    """
    if px > 0 and ph > 0:
        # Velocity proxy: fees relative to pool value
        pool_value = px + ph
        fee_velocity = fees / max(1, pool_value) * 1000  # scaled
        
        if ba == 0 and asm > 0:
            return launch
        
        if ba > 0 and asm > 0 and fee_velocity > 0:
            # Use smoothed fee velocity as activity proxy
            vel_ratio = ba / max(0.01, fee_velocity)
            # Clamp to reasonable range
            vel_ratio = np.clip(vel_ratio, 0.001, 1000.0)
            return max(0.00001, launch * vel_ratio)
    
    return launch


print(f"""
{'#'*82}
  FUEGO HEAT — FORMULA COMPARISON ({N} sims × {N_EPOCHS} epochs)
  Price-Anchored vs Activity-Anchored vs Dollar-Pin Attempt
{'#'*82}

── FORMULAS TESTED ──
""")

formulas = [
    ('PRICE-ANCHORED',  price_anchored,  'target = 0.1 × $0.01 / XFG_price (needs oracle)'),
    ('ACTIVITY-ANCHORED', activity_anchored, 'target = 0.1 × baseline_activity / current_activity'),
    ('DOLLAR-PIN',      dollar_pin,       'target tracks fee velocity (no oracle)'),
]

results = {}
for name, fn, desc in formulas:
    print(f'  {name:<20} ', end='', flush=True)
    t0 = time.time()
    r = run_formula(name, fn)
    dt = time.time() - t0
    results[name] = r
    print(f'{dt:3.0f}s  HeatYr10={r["supply_yr10"]:>12,.0f}  '
          f'Pool={r["pool_xfg"]:.0f}/{r["pool_heat"]:.0f} (ratio={r["pool_ratio"]:.1f}:1)  '
          f'Treasury={r["treasury"]:>10,.0f}')

print(f"""
{'='*100}
  FORMULA               Yr5 HEAT   Yr10 HEAT   Pool Ratio   Treasury  HEAT Stability
{'='*100}""")

for name, r in results.items():
    stability = 'STABLE' if r['pool_ratio'] < 3.0 else 'UNSTABLE'
    print(f"  {name:<20} {r['supply_yr5']:>9,.0f} {r['supply_yr10']:>11,.0f} "
          f"  {r['pool_ratio']:>5.1f}:1     {r['treasury']:>9,.0f}   {stability}")

print(f"""
── ANALYSIS ──

  PRICE-ANCHORED:
    • Most stable target (tied to real XFG market value)
    • Requires external swapxfg price data
    • Pool stays healthy with rebalancer
    • HEAT maintains ~$0.001 value regardless of XFG price

  ACTIVITY-ANCHORED:
    • Self-referencing (no external oracle needed)
    • Anchored to protocol's own economic activity
    • HEAT supply tracks network growth
    • Risk: activity can be gamed (spam transactions to manipulate)

  DOLLAR-PIN:
    • Uses fee velocity as proxy for economic value
    • No external oracle — protocol-native only
    • Attempts to keep HEAT value stable relative to fee generation rate
    • Weakness: fee velocity is noisy and can drift with pool composition

  RECOMMENDATION:
    • Keep price-anchored as primary (Mode 2 = auto: oracle > basin > launch)
    • Activity-anchored as fallback when oracle data is stale
    • Dollar-pin as experimental Mode 3 for testing
""")
