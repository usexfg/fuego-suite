#!/usr/bin/env python3
# ⚠️ SUPERSEDED by sim_final_v18.py — this sim hallucinates a HEAT→XFG burn path
# that does not exist. HEAT is mint-only. The AMM pool is the sole exit.
"""
HEAT — Sigmoid vs Pure Float: 5:1 / 8:1 / 10:1
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Tests if sigmoid soft-band improves or harms APY vs pure float.
"""

import numpy as np, time, sys
from collections import OrderedDict

EPY = 73; N_SIMS = 150; N_EPOCHS = EPY * 21; N_YEARS = 21
KP = 0.08; KI = 0.015; SWAP_FEE = 0.02; CD_SHARE = 0.80; TREASURY_SH = 0.20; MINT_PREMIUM = 0.05
SIGMOID_S = 8.0; SIGMOID_M = 2.0

SEED_BASE = 20260520

def xfg_price_model(t, rng):
    base = 2.0 + 1.0 * np.exp(0.15 * t)
    return base * float(1.0 + rng.normal(0.0, 0.25))

def sim_core(rng, launch, use_sigmoid):
    px = 5000.0; ph = 25000.0
    redemption = launch; integral = 0.0
    treasury = 0.0; cd_pool = 0.0; heat_sup = 0.0
    oracle_bridge_start = int(rng.randint(EPY//2, EPY*2))
    launch_twap = None
    rebalance_count = 0; sigmoid_activations = 0

    supply_y = np.zeros(N_YEARS); treas_y = np.zeros(N_YEARS)
    ratio_y = np.zeros(N_YEARS); price_y = np.zeros(N_YEARS)

    for ep in range(N_EPOCHS):
        t_years = ep / EPY; yr = ep // EPY
        if yr >= N_YEARS: break

        xfg_price = xfg_price_model(t_years, rng)
        oracle_active = False; oracle_stale = True
        if ep >= oracle_bridge_start:
            oracle_active = True
            oracle_stale = rng.random() < 0.05
            oracle_price = xfg_price * float(1.0 + rng.normal(0.0, 0.05))
        else:
            oracle_price = 0.0

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

        # Target
        if launch_twap is not None and spot > 0.0001:
            target = launch * launch_twap / spot
        else:
            target = launch

        target = np.clip(target, 0.00001, 10.0)
        dev = (spot - target) / max(target, 0.00001)
        dt = 1.0 / EPY
        integral = np.clip(integral + dev * dt, -1.0, 1.0)
        red_rate = np.clip(KP * dev + KI * integral, -0.50, 0.50)

        if use_sigmoid:
            abs_dev = abs(dev)
            damp = 1.0 / (1.0 + np.exp(SIGMOID_S * (abs_dev - SIGMOID_M)))
            red_rate *= (1.0 - damp)
            if damp < 0.5:
                sigmoid_activations += 1

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
                rebalance_count += 1

        supply_y[yr]  = heat_sup
        treas_y[yr]   = treasury
        ratio_y[yr]   = spot
        price_y[yr]   = redemption

    # Volatility
    returns = []
    for i in range(2, min(11, N_YEARS)):
        if price_y[i-1] > 0:
            returns.append((price_y[i] - price_y[i-1]) / price_y[i-1])
    volatility = np.std(returns) if returns else 0.0

    return {
        'supply': supply_y, 'treasury': treas_y,
        'ratio': ratio_y, 'price': price_y,
        'volatility': volatility, 'rebalance_count': rebalance_count,
        'sigmoid_activations': sigmoid_activations,
    }

MODELS = [
    ('5:1 Pure Float',   0.2,   False),
    ('5:1 Sigmoid',      0.2,   True),
    ('8:1 Pure Float',   0.125, False),
    ('8:1 Sigmoid',      0.125, True),
    ('10:1 Pure Float',  0.1,   False),
    ('10:1 Sigmoid',     0.1,   True),
]

print(f"\n{'='*120}")
print(f"  SIGMOID vs PURE FLOAT ({N_SIMS} sims × 20yr)")
print(f"  Sigmoid: S={SIGMOID_S} M={SIGMOID_M} — damps PI at extremes")
print(f"{'='*120}\n")

results = OrderedDict()
for label, launch, sig in MODELS:
    print(f"  {label:<20} ", end='', flush=True)
    t0 = time.time()
    batch = []
    for s in range(N_SIMS):
        rng = np.random.RandomState(SEED_BASE + hash(label + str(s)) % (2**30))
        batch.append(sim_core(rng, launch, sig))
    dt = time.time() - t0

    med = {}
    for key in ['supply','treasury','ratio','price']:
        med[key] = [np.median([b[key][y] for b in batch]) for y in range(N_YEARS)]
    med['volatility'] = np.median([b['volatility'] for b in batch])
    med['rebalance_count'] = np.median([b['rebalance_count'] for b in batch])
    med['sigmoid_activations'] = np.median([b['sigmoid_activations'] for b in batch])
    results[label] = med

    apy5 = med['treasury'][5]/max(med['supply'][5],1)*100
    apy20 = med['treasury'][20]/max(med['supply'][20],1)*100
    print(f"{dt:3.0f}s  "
          f"Y5_supply={med['supply'][5]:>10,.0f}  "
          f"treasury={med['treasury'][5]:>10,.0f}  "
          f"apy={apy5:5.2f}%  "
          f"vol={med['volatility']:.3f}  "
          f"reb={med['rebalance_count']:>4.0f}")
    sys.stdout.flush()

print(f"\n{'='*120}")
print(f"  YEAR 5 COMPARISON")
print(f"{'='*120}")
print(f"  {'MODEL':<20} {'SUPPLY':>10} {'TREASURY':>10} {'APY':>6} {'POOL':>6} {'PRICE':>10} {'VOL':>6} {'REB':>5}")
for label, r in results.items():
    s=r['supply'][5]; t=r['treasury'][5]
    print(f"  {label:<20} {s:>10,.0f} {t:>10,.0f} {t/max(s,1)*100:>5.2f}% "
          f"{r['ratio'][5]:.1f}:1  {r['price'][5]:.8f}  {r['volatility']:.3f}  {r['rebalance_count']:>5.0f}")

print(f"\n{'='*120}")
print(f"  YEAR 20 COMPARISON")
print(f"{'='*120}")
print(f"  {'MODEL':<20} {'SUPPLY':>10} {'TREASURY':>10} {'APY':>6} {'POOL':>6} {'PRICE':>10} {'VOL':>6}")
for label, r in results.items():
    s=r['supply'][20]; t=r['treasury'][20]
    print(f"  {label:<20} {s:>10,.0f} {t:>10,.0f} {t/max(s,1)*100:>5.2f}% "
          f"{r['ratio'][20]:.1f}:1  {r['price'][20]:.8f}  {r['volatility']:.3f}")

# Sigmoid delta
print(f"\n{'='*120}")
print(f"  SIGMOID vs PURE FLOAT — DELTA")
print(f"{'='*120}")
print(f"  {'RATIO':<8} {'ΔSupply Y5':>12} {'ΔTreasury Y5':>14} {'ΔAPY Y5':>9} {'ΔAPY Y20':>9}  {'ΔVol':>7}  {'ΔReb':>7}")
for base_label in ['5:1', '8:1', '10:1']:
    pure = results[f'{base_label} Pure Float']
    sig  = results[f'{base_label} Sigmoid']
    ds = sig['supply'][5] - pure['supply'][5]
    dt = sig['treasury'][5] - pure['treasury'][5]
    da5 = (sig['treasury'][5]/max(sig['supply'][5],1) - pure['treasury'][5]/max(pure['supply'][5],1))*100
    da20 = (sig['treasury'][20]/max(sig['supply'][20],1) - pure['treasury'][20]/max(pure['supply'][20],1))*100
    dv = sig['volatility'] - pure['volatility']
    dr = sig['rebalance_count'] - pure['rebalance_count']
    print(f"  {base_label:<8} {ds:>+12,.0f} {dt:>+14,.0f} {da5:>+8.2f}% {da20:>+8.2f}%  {dv:>+6.3f}  {dr:>+6.0f}")

print(f"\n  + = sigmoid improves, - = sigmoid degrades")
print(f"  Sigmoid activations (yr5): 5:1={results['5:1 Sigmoid']['sigmoid_activations']:.0f}  8:1={results['8:1 Sigmoid']['sigmoid_activations']:.0f}  10:1={results['10:1 Sigmoid']['sigmoid_activations']:.0f}")
print()
