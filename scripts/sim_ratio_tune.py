#!/usr/bin/env python3
"""
HEAT — Launch Ratio Tuning: 5:1 → 100:1
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Finds the point where higher ratios stop being beneficial.
Models: full float (best performer from 6-mode comparison).
"""

import numpy as np, time, sys
from collections import OrderedDict

EPY = 73; N_SIMS = 100; N_EPOCHS = EPY * 21; N_YEARS = 21
KP = 0.08; KI = 0.015; SWAP_FEE = 0.02; CD_SHARE = 0.80; TREASURY_SH = 0.20; MINT_PREMIUM = 0.05

SEED_BASE = 20260520

RATIOS = OrderedDict([
    ('5:1',   0.2),
    ('8:1',   0.125),
    ('10:1',  0.1),
    ('15:1',  1.0/15),
    ('20:1',  0.05),
    ('50:1',  0.02),
    ('100:1', 0.01),
])

def xfg_price_model(t, rng):
    base = 2.0 + 1.0 * np.exp(0.15 * t)
    return base * float(1.0 + rng.normal(0.0, 0.25))

def sim_core(rng, launch):
    px = 5000.0; ph = 25000.0
    redemption = launch; integral = 0.0
    treasury = 0.0; cd_pool = 0.0; heat_sup = 0.0
    oracle_active = False; oracle_stale = True
    oracle_bridge_start = int(rng.randint(EPY//2, EPY*2))
    launch_twap = None

    supply_y = np.zeros(N_YEARS); treas_y = np.zeros(N_YEARS)
    cdpool_y = np.zeros(N_YEARS); ratio_y = np.zeros(N_YEARS)
    price_y = np.zeros(N_YEARS); depth_y = np.zeros(N_YEARS)

    for ep in range(N_EPOCHS):
        t_years = ep / EPY; yr = ep // EPY
        if yr >= N_YEARS: break

        xfg_price = xfg_price_model(t_years, rng)
        oracle_active = False; oracle_price = 0.0
        if ep >= oracle_bridge_start:
            oracle_active = True
            oracle_stale = rng.random() < 0.05
            oracle_price = xfg_price * float(1.0 + rng.normal(0.0, 0.05))
        else:
            oracle_stale = True

        vol_mult = np.clip(xfg_price / 2.0, 0.3, 5.0)
        vol_e = 20000.0 * vol_mult * float(np.exp(rng.normal(0.0, 0.10)))

        fees = vol_e * SWAP_FEE
        treasury += fees * TREASURY_SH
        cd_pool  += fees * CD_SHARE

        sv = vol_e * 0.05
        if px > 0 and ph > 0:
            for _ in range(3):
                if rng.random() < 0.5:
                    cap = min(sv/3.0, px * 0.005)
                    ph -= ph * cap / (px + cap); px += cap
                else:
                    cap = min(sv/3.0, ph * 0.005)
                    px -= px * cap / (ph + cap); ph += cap

        spot = px / max(ph, 0.001)
        if launch_twap is None and oracle_active and not oracle_stale and spot > 0:
            launch_twap = spot

        # Full float: self-referencing
        if launch_twap is not None and spot > 0.0001:
            target = launch * launch_twap / spot
        else:
            target = launch

        target = np.clip(target, 0.00001, 10.0)
        dev = (spot - target) / max(target, 0.00001)
        dt = 1.0 / EPY
        integral = np.clip(integral + dev * dt, -1.0, 1.0)
        red_rate = np.clip(KP * dev + KI * integral, -0.50, 0.50)
        redemption = max(1e-6, target * (1.0 + red_rate * dt))

        # User mint
        mp = max(0.0, 0.008 - 0.4 * dev)
        mint_vol = vol_e * 0.08 / max(spot, 0.001) * mp
        heat_sup += mint_vol * (1.0 - MINT_PREMIUM)

        # CD yield
        if cd_pool > 0 and ph > 0:
            sr = np.clip(1.0 + red_rate, 0.2, 3.0)
            spend = min(cd_pool, cd_pool * sr)
            hb = ph * spend / (px + spend)
            if 0 < hb < ph * 0.95:
                px += spend; ph -= hb; heat_sup += hb; cd_pool -= spend

        # Rebalancer
        if ph > 0 and treasury > 0 and (px / max(ph, 0.001)) > 3.0:
            reb = min(treasury * 0.10, px * 0.03)
            if redemption > 0:
                heat_dep = reb / redemption
                ph += heat_dep; treasury -= reb; heat_sup += heat_dep

        supply_y[yr]  = heat_sup
        treas_y[yr]   = treasury
        cdpool_y[yr]  = cd_pool
        ratio_y[yr]   = spot
        price_y[yr]   = redemption
        depth_y[yr]   = px

    return {
        'supply': supply_y, 'treasury': treas_y, 'cd_pool': cdpool_y,
        'ratio': ratio_y, 'price': price_y, 'depth': depth_y,
    }


print(f"\n{'='*130}")
print(f"  LAUNCH RATIO TUNING — 5:1 → 100:1 (Full Float, {N_SIMS} sims × 20yr)")
print(f"  XFG: $2 → ~$25 | PI: KP={KP} KI={KI} | Swap fee: 2% | Mint premium: 5%")
print(f"{'='*130}\n")

results = OrderedDict()
for label, launch in RATIOS.items():
    print(f"  {label:<8} ", end='', flush=True)
    t0 = time.time()
    batch = []
    for s in range(N_SIMS):
        rng = np.random.RandomState(SEED_BASE + hash(label + str(s)) % (2**30))
        batch.append(sim_core(rng, launch))
    dt = time.time() - t0

    med = {}
    for key in ['supply','treasury','cd_pool','ratio','price','depth']:
        med[key] = [np.median([b[key][y] for b in batch]) for y in range(N_YEARS)]

    results[label] = med

    # HEAT supply in XFG terms: supply × price (approximate backing value)
    backing_y5 = med['supply'][5] * med['price'][5]
    backing_y20 = med['supply'][20] * med['price'][20]

    # APY proxy
    apy5 = med['treasury'][5] / max(med['supply'][5], 1) * 100
    apy20 = med['treasury'][20] / max(med['supply'][20], 1) * 100

    # Pool health: ratio closer to 1:1 is better
    pool5 = med['ratio'][5]
    pool20 = med['ratio'][20]

    # Supply efficiency: treasury per unit of HEAT — higher = better ROI per HEAT
    eff5 = med['treasury'][5] / max(med['supply'][5], 1)
    eff20 = med['treasury'][20] / max(med['supply'][20], 1)

    # Heat price (XFG per HEAT)
    heat_price_xfg_5 = med['price'][5]
    heat_price_xfg_20 = med['price'][20]

    print(f"{dt:3.0f}s  "
          f"Y5:supply={med['supply'][5]:>10,.0f}  "
          f"treasury={med['treasury'][5]:>10,.0f}  "
          f"apy={apy5:5.2f}%  "
          f"price={heat_price_xfg_5:.4f}  "
          f"pool={pool5:.1f}:1  "
          f"eff={eff5:.4f}")
    sys.stdout.flush()

print(f"\n{'='*130}")
print(f"  YEAR 5 COMPARISON")
print(f"{'='*130}")
print(f"  {'RATIO':<8}  {'SUPPLY':>10}  {'TREASURY':>10}  {'APY':>6}  {'PRICE(xfg)':>11}  {'POOL':>6}  {'EFFICIENCY':>10}  {'HEAT_DILUTION':>13}")
for label, r in results.items():
    s = r['supply'][5]; t = r['treasury'][5]
    apy = t / max(s, 1) * 100
    eff = t / max(s, 1)
    v = s * r['price'][5]  # backing XFG value
    print(f"  {label:<8}  {s:>10,.0f}  {t:>10,.0f}  {apy:>5.2f}%  {r['price'][5]:.10f}  {r['ratio'][5]:.1f}:1  {eff:>10.4f}  {v:>13,.0f}")

print(f"\n{'='*130}")
print(f"  YEAR 20 COMPARISON")
print(f"{'='*130}")
print(f"  {'RATIO':<8}  {'SUPPLY':>10}  {'TREASURY':>10}  {'APY':>6}  {'PRICE(xfg)':>11}  {'POOL':>6}  {'EFFICIENCY':>10}  {'HEAT_DILUTION':>13}")
for label, r in results.items():
    s = r['supply'][20]; t = r['treasury'][20]
    apy = t / max(s, 1) * 100
    eff = t / max(s, 1)
    v = s * r['price'][20]
    print(f"  {label:<8}  {s:>10,.0f}  {t:>10,.0f}  {apy:>5.2f}%  {r['price'][20]:.10f}  {r['ratio'][20]:.1f}:1  {eff:>10.4f}  {v:>13,.0f}")

# Find diminishing returns
print(f"\n{'='*130}")
print(f"  DIMINISHING RETURNS ANALYSIS")
print(f"  (ΔAPY from previous ratio, treasury growth rate)")
print(f"{'='*130}")

prev_label = None; prev_t5 = None; prev_t20 = None; prev_apy5 = None
for label, r in results.items():
    s5 = r['supply'][5]; t5 = r['treasury'][5]; apy5 = t5/max(s5,1)*100
    s20 = r['supply'][20]; t20 = r['treasury'][20]; apy20 = t20/max(s20,1)*100

    delta_t5 = t5 - prev_t5 if prev_t5 else 0
    delta_apy5 = apy5 - prev_apy5 if prev_apy5 else 0
    mult = s5 / min([r2['supply'][5] for r2 in results.values()]) if results else 0

    print(f"  {label:<8}  ΔTreasY5: {delta_t5:>+10,.0f}  ΔAPY: {delta_apy5:>+6.2f}%  "
          f"Supply×baseline: {mult:.1f}×  "
          f"Y20_APY: {apy20:.2f}%")

    prev_label = label; prev_t5 = t5; prev_t20 = t20; prev_apy5 = apy5

# Optimal range
print(f"\n  → Sweet spot: ratio where ΔAPY < 0.5% from previous step = diminishing return")

# Find the ratio where supply growth outpaces treasury growth
print(f"\n{'='*130}")
print(f"  HEAT DILUTION SCORE (backing XFG value / year-0 backing)")
print(f"  Lower = HEAT retains more value per unit. Higher = hyper-supply dilutes value.")
print(f"{'='*130}")
for label, r in results.items():
    s5 = r['supply'][5]; p5 = r['price'][5]
    s20 = r['supply'][20]; p20 = r['price'][20]
    backing5 = s5 * p5; backing20 = s20 * p20
    print(f"  {label:<8}  Y5 backing: {backing5:>12,.0f} XFG  Y20 backing: {backing20:>12,.0f} XFG")

print()
