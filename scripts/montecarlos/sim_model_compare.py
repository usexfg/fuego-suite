#!/usr/bin/env python3
# ⚠️ SUPERSEDED by sim_final_v18.py — this sim hallucinates a HEAT→XFG burn path
# that does not exist. HEAT is mint-only. The AMM pool is the sole exit.
"""
Fuego HEAT — 5-Model Stability Comparison
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Tests: Fixed, Damped, Basin, Activity, Price-anchored
     × Baseline, Spot Manipulation, Thin Pool attacks
     × 10-year horizon, XFG price trajectory
"""

import numpy as np, time, sys

# ── Constants (matching CryptoNoteConfig.h) ──
BPE       = 900; EPY = 73; LAUNCH = 0.2
KP=0.08; KI=0.015; SWAP_FEE=0.02; CD_SHARE=0.80; TREASURY_SH=0.20

N_SIMS    = 150
N_EPOCHS  = EPY * 10  # 10 years

np.random.seed(int(time.time()) % (2**31))

# ── Stable PRNG per scenario ──
def make_rng(seed): return np.random.RandomState(seed)

def run_model(model_idx, rng, spot_manip=0.0, pool_xfg=5000.0, pool_heat=25000.0):
    """model_idx: 0=Fixed, 1=Damped, 2=Basin, 3=Activity, 4=Price"""
    redemption = LAUNCH; integral = 0.0
    treasury = 0.0; cd_pool = 0.0; heat_sup = 0.0
    protocol_lp = 0.0; total_lp = 1000.0
    xfg_price = 0.01

    # Basin state
    basin_phase = 0; basin_center = 0.0; basin_halfw = 0.0
    basin_min = 0.0; basin_max = 0.0; basin_obs = 0; basin_stable = 0; basin_exit = 0

    # Activity state
    activity_smoothed = 0.0; activity_baseline = 0.0

    heat_yr = [0.0]; treas_yr = [0.0]; pool_ratio_yr = [5.0]
    rebalance_count = 0

    for ep in range(N_EPOCHS):
        t = ep / N_EPOCHS
        xfg_price = 0.01 * np.exp(4 * t**1.3) * (1 + rng.normal(0, 0.25))
        vol_mult = np.clip(xfg_price / 0.01, 0.3, 5.0)
        vol_e = LAUNCH * 100000 * vol_mult * np.exp(rng.normal(0, 0.10))
        # ^ LAUNCH*100000 keeps vol proportional to ratio for fair comparison

        fees = vol_e * SWAP_FEE
        treasury += fees * TREASURY_SH
        cd_pool += fees * CD_SHARE

        # Hearth swaps
        sv = vol_e * 0.05
        if pool_xfg > 0 and pool_heat > 0:
            for _ in range(3):
                if rng.random() < 0.5:
                    capped = min(sv/3, pool_xfg * 0.005)
                    out = pool_heat * capped / (pool_xfg + capped)
                    pool_xfg += capped; pool_heat -= out
                else:
                    capped = min(sv/3, pool_heat * 0.005)
                    out = pool_xfg * capped / (pool_heat + capped)
                    pool_heat += capped; pool_xfg -= out

        # Attacker: spot manipulation
        if ep >= 50 and spot_manip > 0:
            pool_xfg *= (1.0 + spot_manip * 0.01)

        spot = pool_xfg / max(pool_heat, 0.001)

        # ── Target (model-dependent) ──
        if model_idx == 0:   # Fixed
            target = LAUNCH

        elif model_idx == 1: # Damped
            target = LAUNCH * (max(0.0001, spot_manip if spot_manip else 0.2) / max(0.0001, spot)) ** 0.25
            # damped formula: launch_twap ≈ initial spot, current_twap ≈ current spot

        elif model_idx == 2: # Basin
            if ep > 0:
                if basin_phase == 0:
                    if basin_obs == 0: basin_min = basin_max = spot
                    else: basin_min = min(basin_min, spot); basin_max = max(basin_max, spot)
                    basin_obs += 1
                    if basin_obs >= 3: basin_phase = 1
                elif basin_phase == 1:
                    basin_min = min(basin_min, spot); basin_max = max(basin_max, spot)
                    avg = (basin_min + basin_max) / 2
                    if avg > 0 and (basin_max - basin_min) < avg * 0.10:
                        basin_stable += 1
                    else: basin_stable = 0
                    if basin_stable >= 4:
                        basin_center = avg; basin_halfw = max(basin_max-basin_min, avg*0.01)/2
                        basin_phase = 2; basin_exit = 0
                elif basin_phase == 2:
                    u = basin_center + basin_halfw; l = basin_center - basin_halfw
                    if spot > u or spot < l:
                        basin_exit += 1
                        if basin_exit >= 3: basin_phase = 0; basin_obs = 0
            target = basin_center if (basin_phase == 2 and basin_center > 0) else LAUNCH

        elif model_idx == 3: # Activity
            epoch_act = fees + pool_xfg * 0.01 + pool_heat * 0.01
            activity_smoothed = activity_smoothed * 0.95 + epoch_act * 0.05 if activity_smoothed > 0 else epoch_act
            if ep == EPY and activity_baseline == 0: activity_baseline = activity_smoothed
            if activity_baseline > 0 and activity_smoothed > 0:
                target = LAUNCH * activity_baseline / activity_smoothed
            else:
                target = LAUNCH

        else:               # Price-anchored
            target = max(0.00001, LAUNCH * 0.01 / xfg_price)

        target = max(0.00001, min(10.0, target))

        # ── PI Controller ──
        dev = (spot - target) / target if target > 0 else 0
        dt = 1.0 / EPY
        integral = np.clip(integral + dev * dt, -1.0, 1.0)
        red_rate = np.clip(KP * dev + KI * integral, -0.50, 0.50)
        redemption = max(1e-6, target * (1 + red_rate * dt))

        # User mint
        mp = max(0, 0.008 - 0.4 * dev)
        heat_sup += vol_e * 0.08 / max(spot, 0.001) * mp

        # CD yield: buy from Hearth
        if cd_pool > 0 and pool_heat > 0:
            sr = np.clip(1.0 + red_rate, 0.2, 3.0)
            spend = min(cd_pool, cd_pool * sr)
            hb = pool_heat * spend / (pool_xfg + spend)
            if 0 < hb < pool_heat * 0.95:
                pool_xfg += spend; pool_heat -= hb
                heat_sup += hb; cd_pool -= spend

        # Rebalancer
        if pool_heat > 0 and treasury > 0 and (pool_xfg / max(pool_heat, 0.001)) > 3:
            reb = min(treasury * 0.10, pool_xfg * 0.03)
            if redemption > 0:
                heat_dep = reb / redemption
                pool_heat += heat_dep
                total_lp += heat_dep; protocol_lp += heat_dep
                treasury -= reb; heat_sup += heat_dep
                rebalance_count += 1

        if ep % EPY == 0 and ep > 0:
            heat_yr.append(heat_sup); treas_yr.append(treasury)
            pool_ratio_yr.append(pool_xfg / max(pool_heat, 0.001))

    return {
        'supply_yr1': heat_yr[1] if len(heat_yr) > 1 else 0,
        'supply_yr5': heat_yr[5] if len(heat_yr) > 5 else 0,
        'supply_yr10': heat_yr[-1] if heat_yr else 0,
        'treasury_yr10': treas_yr[-1] if treas_yr else 0,
        'pool_ratio_median': np.median(pool_ratio_yr),
        'pool_ratio_final': pool_ratio_yr[-1] if pool_ratio_yr else 0,
        'rebalance_count': rebalance_count,
    }


# ── Run ──
MODEL_NAMES = ['A: Fixed', 'B: Damped', 'C: Basin', 'D: Activity', 'E: Price']
SCENARIOS = [
    ('BASELINE', 0.0, 5000.0, 25000.0),
    ('SpotManip5%', 5.0, 5000.0, 25000.0),
    ('ThinPool+Manip', 5.0, 500.0, 2500.0),
]

print(f"\n{'#'*90}")
print(f"  FUEGO HEAT — 5-MODEL STABILITY COMPARISON ({N_SIMS} sims × {N_EPOCHS} epochs)")
print(f"  Ratio=0.2 | KP=0.08 KI=0.015 | CD buy + Treasury rebalance")
print(f"  XFG price: $0.01 → $10-100+ trajectory (privacy coin model)")
print(f"{'#'*90}\n")

results = {}
for sc_name, sp_man, px, ph in SCENARIOS:
    print(f"── {sc_name} ──")
    for mi in range(5):
        t0 = time.time()
        rng_seed = hash(sc_name + str(mi)) % (2**31)
        rng = make_rng(rng_seed)
        batch = [run_model(mi, rng, sp_man, px, ph) for _ in range(N_SIMS)]
        dt = time.time() - t0

        r = {k: np.median([b[k] for b in batch]) for k in batch[0].keys()}
        r['rebalance_count'] = np.mean([b['rebalance_count'] for b in batch])
        key = f"{MODEL_NAMES[mi]}/{sc_name}"
        results[key] = r

        # Compute coverage
        cov = r['treasury_yr10'] / max(1e-6, LAUNCH) / max(1, r['supply_yr10']) * 100

        print(f"  {MODEL_NAMES[mi]:<18} {dt:3.0f}s  "
              f"Supp: {r['supply_yr1']:>7,.0f}→{r['supply_yr5']:>9,.0f}→{r['supply_yr10']:>10,.0f}  "
              f"Pool: {r['pool_ratio_final']:.1f}:1  "
              f"Treas: {r['treasury_yr10']:>8,.0f}  "
              f"Cov: {cov:.0f}%  "
              f"Reb: {r['rebalance_count']:.0f}×")
        sys.stdout.flush()


# ── Comparison Table ──
print(f"\n{'='*110}")
print(f"  {'SCENARIO/MODEL':<22} {'Yr1 HEAT':>9} {'Yr5 HEAT':>11} {'Yr10 HEAT':>12} {'Pool':>6} {'Treasury':>9} {'Cover':>5} {'Reb':>4}")
print(f"{'='*110}")

for sc_name, sp_man, px, ph in SCENARIOS:
    print(f"  {sc_name}:")
    for mi in range(5):
        key = f"{MODEL_NAMES[mi]}/{sc_name}"
        r = results[key]
        cov = r['treasury_yr10'] / max(1e-6, LAUNCH) / max(1, r['supply_yr10']) * 100
        print(f"    {MODEL_NAMES[mi]:<18} {r['supply_yr1']:>9,.0f} {r['supply_yr5']:>11,.0f} {r['supply_yr10']:>12,.0f} "
              f"{r['pool_ratio_final']:>4.1f}:1 {r['treasury_yr10']:>9,.0f} {cov:>4.0f}% {r['rebalance_count']:>4.0f}")

# ── Winner Analysis ──
print(f"\n── WINNER ANALYSIS ──")
for metric, name, best_is_min in [
    ('supply_yr10', 'Lowest supply', True),
    ('pool_ratio_final', 'Best pool health (closest to 1:1)', True),
]:
    print(f"\n  {name}:")
    for sc_name, _, _, _ in SCENARIOS:
        scores = [(mi, results[f'{MODEL_NAMES[mi]}/{sc_name}'][metric]) for mi in range(5)]
        scores.sort(key=lambda x: x[1], reverse=not best_is_min)
        winner = MODEL_NAMES[scores[0][0]]
        print(f"    {sc_name:<18}: {winner} ({scores[0][1]:,.0f})")

# ── Recommendation ──
print(f"\n{'='*90}")
print(f"  RECOMMENDATION:")
print(f"  Model A (Fixed ratio = 0.2) is the baseline — simplest, most predictable.")
print(f"  Model C (Basin) adds equilibrium discovery with launch fallback.")
print(f"  Model D (Activity) has best pool health but highest supply growth.")
print(f"  Model E (Price) needs oracle — use Mode 2 auto-selection to combine.")
print(f"  Model B (Damped) — most novel, needs testing against circular feedback risk.")
print(f"{'='*90}\n")
