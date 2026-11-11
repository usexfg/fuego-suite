#!/usr/bin/env python3
# ⚠️ SUPERSEDED by sim_final_v18.py — this sim hallucinates a HEAT→XFG burn path
# that does not exist. HEAT is mint-only. The AMM pool is the sole exit.
"""
Fuego HEAT — 6-Mode Stability Comparison (20-year horizon)
===========================================================
  200 sims each, identical seeds across models.
  Compares: 5:1 / 8:1  ×  Fixed Band / Full Float / Sigmoid / Depth-Gated
"""

import numpy as np, time, sys
from collections import OrderedDict

# ── Constants ──
EPY = 73
N_SIMS   = 200
N_EPOCHS = EPY * 21            # years 0..20 inclusive
N_YEARS  = 21

LAUNCH_5 = 0.2     # 5:1
LAUNCH_8 = 0.125   # 8:1

KP = 0.08; KI = 0.015
SWAP_FEE     = 0.02
CD_SHARE     = 0.80
TREASURY_SH  = 0.20
MINT_PREMIUM = 0.05

BAND_FLOOR      = 1.50
BAND_CEIL       = 2.50
THRESHOLD_5     = 5.00
THRESHOLD_8     = 8.00
POOL_DEPTH_GATE = 5000.0       # Mode F: pool xfg must exceed initial depth

SIGMOID_S = 8.0
SIGMOID_M = 2.0

SEED_BASE = 20260520

# ── XFG price trajectory ──
# Starts ~$2, crosses $5 around year 4-6, $8 around year 8-10, ~$25 by year 20
def xfg_price_model(t, rng):
    base = 2.0 + 1.0 * np.exp(0.15 * t)
    return base * float(1.0 + rng.normal(0.0, 0.25))


# ═══════════════════════════════════════════════════════════════
#  SIMULATION CORE
# ═══════════════════════════════════════════════════════════════

def sim_core(rng, launch, threshold, activation_mode):
    px = 5000.0; ph = 25000.0
    redemption = launch; integral = 0.0
    treasury = 0.0; cd_pool = 0.0; heat_sup = 0.0
    oracle_active = False; oracle_stale = True
    oracle_bridge_start = int(rng.randint(EPY//2, EPY*2))
    launch_twap = None

    supply_y  = np.zeros(N_YEARS)
    treas_y   = np.zeros(N_YEARS)
    cdpool_y  = np.zeros(N_YEARS)
    ratio_y   = np.zeros(N_YEARS)
    price_y   = np.zeros(N_YEARS)
    depth_y   = np.zeros(N_YEARS)
    mintvol_y = np.zeros(N_YEARS)
    swapvol_y = np.zeros(N_YEARS)
    heatval_y = np.zeros(N_YEARS)  # HEAT dollar value (for banded models)

    breach_count = 0
    epoch_count_banded = 0

    for ep in range(N_EPOCHS):
        t_years = ep / EPY
        yr = ep // EPY
        if yr >= N_YEARS:
            break

        xfg_price = xfg_price_model(t_years, rng)

        # ── Oracle ──
        oracle_active = False
        oracle_stale = True
        oracle_price = 0.0
        if ep >= oracle_bridge_start:
            oracle_active = True
            oracle_stale = rng.random() < 0.05
            oracle_price = xfg_price * float(1.0 + rng.normal(0.0, 0.05))
        else:
            oracle_active = False

        # ── Volume (scaled to XFG price) ──
        vol_mult = np.clip(xfg_price / 2.0, 0.3, 5.0)
        vol_e = 20000.0 * vol_mult * float(np.exp(rng.normal(0.0, 0.10)))
        swapvol_y[yr] += vol_e

        fees = vol_e * SWAP_FEE
        treasury += fees * TREASURY_SH
        cd_pool  += fees * CD_SHARE

        # ── Hearth swaps ──
        sv = vol_e * 0.05
        if px > 0 and ph > 0:
            for _ in range(3):
                if rng.random() < 0.5:
                    cap = min(sv/3.0, px * 0.005)
                    ratio_out = cap / (px + cap)
                    ph -= ph * ratio_out
                    px += cap
                else:
                    cap = min(sv/3.0, ph * 0.005)
                    ratio_out = cap / (ph + cap)
                    px -= px * ratio_out
                    ph += cap

        spot = px / max(ph, 0.001)

        if launch_twap is None and oracle_active and not oracle_stale and spot > 0:
            launch_twap = spot

        # ── TARGET ──
        activated = False
        heat_value = 0.0
        target = launch

        if activation_mode == 'always':
            if launch_twap is not None and spot > 0.0001:
                target = launch * launch_twap / spot

        elif activation_mode == 'oracle_price':
            if oracle_active and not oracle_stale and oracle_price >= threshold:
                activated = True
                heat_value = spot * oracle_price
                target = np.clip(heat_value, BAND_FLOOR, BAND_CEIL) / max(oracle_price, 0.01)
            elif launch_twap is not None and spot > 0.0001:
                target = launch * launch_twap / spot

        elif activation_mode == 'pool_depth':
            if px > POOL_DEPTH_GATE and oracle_active and not oracle_stale:
                activated = True
                heat_value = spot * oracle_price
                target = np.clip(heat_value, BAND_FLOOR, BAND_CEIL) / max(oracle_price, 0.01)
            elif launch_twap is not None and spot > 0.0001:
                target = launch * launch_twap / spot

        elif activation_mode == 'sigmoid':
            if launch_twap is not None and spot > 0.0001:
                target = launch * launch_twap / spot

        # ── PI Controller ──
        target = np.clip(target, 0.00001, 10.0)
        dev = (spot - target) / max(target, 0.00001)
        dt  = 1.0 / EPY
        integral = np.clip(integral + dev * dt, -1.0, 1.0)
        red_rate = np.clip(KP * dev + KI * integral, -0.50, 0.50)

        if activation_mode == 'sigmoid':
            abs_dev = abs(dev)
            damp = 1.0 / (1.0 + np.exp(SIGMOID_S * (abs_dev - SIGMOID_M)))
            red_rate *= (1.0 - damp)

        redemption = max(1e-6, target * (1.0 + red_rate * dt))

        # Band breach tracking
        if activated and heat_value > 0:
            epoch_count_banded += 1
            if heat_value < BAND_FLOOR or heat_value > BAND_CEIL:
                breach_count += 1

        # User mint (with premium)
        mp = max(0.0, 0.008 - 0.4 * dev)
        mint_vol = vol_e * 0.08 / max(spot, 0.001) * mp
        heat_sup += mint_vol * (1.0 - MINT_PREMIUM)
        mintvol_y[yr] += mint_vol

        # CD yield — buy from Hearth
        if cd_pool > 0 and ph > 0:
            sr = np.clip(1.0 + red_rate, 0.2, 3.0)
            spend = min(cd_pool, cd_pool * sr)
            hb = ph * spend / (px + spend)
            if 0 < hb < ph * 0.95:
                px += spend; ph -= hb
                heat_sup += hb; cd_pool -= spend

        # Treasury rebalancer
        if ph > 0 and treasury > 0 and (px / max(ph, 0.001)) > 3.0:
            reb = min(treasury * 0.10, px * 0.03)
            if redemption > 0:
                heat_dep = reb / redemption
                ph += heat_dep; treasury -= reb
                heat_sup += heat_dep

        # ── Snapshot (overwrite per year — last epoch wins) ──
        supply_y[yr]  = heat_sup
        treas_y[yr]   = treasury
        cdpool_y[yr]  = cd_pool
        ratio_y[yr]   = spot
        price_y[yr]   = redemption
        depth_y[yr]   = px
        heatval_y[yr] = heat_value

    breach_pct = breach_count / max(1, epoch_count_banded) * 100.0

    # Compute volatility (σ of annual price returns, years 1-10)
    returns10 = []
    for i in range(2, min(11, N_YEARS)):
        if price_y[i-1] > 0:
            returns10.append((price_y[i] - price_y[i-1]) / price_y[i-1])
    volatility = np.std(returns10) if returns10 else 0.0

    return {
        'supply':   supply_y,
        'treasury': treas_y,
        'cd_pool':  cdpool_y,
        'ratio':    ratio_y,
        'price':    price_y,
        'depth':    depth_y,
        'mint_vol': mintvol_y,
        'swap_vol': swapvol_y,
        'heat_val': heatval_y,
        'breach_pct': breach_pct,
        'volatility': volatility,
    }


# ═══════════════════════════════════════════════════════════════
#  MODELS
# ═══════════════════════════════════════════════════════════════

MODELS = OrderedDict([
    ('A: 5:1 Fixed Band',  dict(launch=LAUNCH_5, threshold=THRESHOLD_5, mode='oracle_price')),
    ('B: 5:1 Full Float',  dict(launch=LAUNCH_5, threshold=0,          mode='always')),
    ('C: 8:1 Fixed Band',  dict(launch=LAUNCH_8, threshold=THRESHOLD_8, mode='oracle_price')),
    ('D: 8:1 Full Float',  dict(launch=LAUNCH_8, threshold=0,          mode='always')),
    ('E: Sigmoid 8:1',     dict(launch=LAUNCH_8, threshold=0,          mode='sigmoid')),
    ('F: DepthGate 5:1',   dict(launch=LAUNCH_5, threshold=0,          mode='pool_depth')),
])


# ═══════════════════════════════════════════════════════════════
#  RUN
# ═══════════════════════════════════════════════════════════════

print(f"\n{'='*100}")
print(f"  FUEGO HEAT — 6-MODE STABILITY COMPARISON")
print(f"  {N_SIMS} sims × 20 years ({N_EPOCHS} epochs) | KP={KP} KI={KI}")
print(f"  XFG price: $2 → ~$25 over 20yr (crosses $5 ~yr5, $8 ~yr10)")
print(f"  Band: ${BAND_FLOOR}–${BAND_CEIL} | Swap fee: 2% | Depth-gate: {POOL_DEPTH_GATE:,.0f} XFG")
print(f"{'='*100}\n")

results = OrderedDict()

for name, cfg in MODELS.items():
    print(f"  {name:<24} ", end='', flush=True)
    t0 = time.time()

    batch = []
    for s in range(N_SIMS):
        rng = np.random.RandomState(SEED_BASE + hash(name + str(s)) % (2**30))
        batch.append(sim_core(rng, cfg['launch'], cfg['threshold'], cfg['mode']))

    dt = time.time() - t0

    # Medians
    med = {}
    for key in ['supply','treasury','cd_pool','ratio','price','depth','mint_vol','swap_vol','heat_val']:
        med[key] = [np.median([b[key][y] for b in batch]) for y in range(N_YEARS)]
    med['breach_pct'] = np.median([b['breach_pct'] for b in batch])
    med['volatility'] = np.median([b['volatility'] for b in batch])

    results[name] = med
    print(f"{dt:4.0f}s   "
          f"Y5_supply={med['supply'][5]:>10,.0f}   "
          f"Y5_treas={med['treasury'][5]:>10,.0f}   "
          f"breach={med['breach_pct']:>5.1f}%   "
          f"vol={med['volatility']:.3f}")
    sys.stdout.flush()


# ═══════════════════════════════════════════════════════════════
#  HELPERS
# ═══════════════════════════════════════════════════════════════

def fmt_v(v, suffix='', scale=1.0):
    v = v * scale
    if abs(v) >= 1e6: return f"{v/1e6:7.2f}M{suffix}"
    if abs(v) >= 1e3: return f"{v/1e3:7.1f}K{suffix}"
    return f"{v:8,.0f}{suffix}"


# ═══════════════════════════════════════════════════════════════
#  YEAR 5 TABLE
# ═══════════════════════════════════════════════════════════════

for label, years in [("YEAR 5", [5]), ("YEAR 10", [10]), ("YEAR 20", [20])]:
    print(f"\n{'─'*150}")
    print(f"  {label}")
    print(f"{'─'*150}")
    hdr = (f"  {'MODEL':<22} {'SUPPLY':>9}  {'PRICE':>8}  {'POOL':>6}  "
           f"{'TREASURY':>10}  {'CD_POOL':>10}  {'DEPTH':>10}  {'BREACH':>7}  {'VOL':>6}  {'HEAT_VAL':>9}")
    print(hdr)
    for name, r in results.items():
        for y in years:
            if y >= N_YEARS: continue
            hv = r['heat_val'][y]
            heat_val_str = f"${hv:.2f}" if hv > 0 else "  —"
            print(f"  {name:<22} "
                  f"{fmt_v(r['supply'][y]):>9}  "
                  f"${r['price'][y]:.4f}  "
                  f"{r['ratio'][y]:.1f}:1  "
                  f"{fmt_v(r['treasury'][y]):>10}  "
                  f"{fmt_v(r['cd_pool'][y]):>10}  "
                  f"{fmt_v(r['depth'][y]):>10}  "
                  f"{r['breach_pct']:>6.1f}%  "
                  f"{r['volatility']:.3f}  "
                  f"{heat_val_str:>9}")


# ═══════════════════════════════════════════════════════════════
#  COMPOSITE SCORING
# ═══════════════════════════════════════════════════════════════

print(f"\n{'='*120}")
print(f"  COMPOSITE SCORE  (higher = better)")
print(f"  Weights: Treasury(APY) 40% | Low Supply 15% | Pool Health 20% | Low Breach 15% | Depth 10%")
print(f"{'='*120}")

def norm(val, all_vals, lower_better):
    vmin, vmax = min(all_vals), max(all_vals)
    if vmax == vmin: return 0.5
    return (vmax - val) / (vmax - vmin) if lower_better else (val - vmin) / (vmax - vmin)

def compute_score(name2r, year_range):
    all_t = [np.mean([r['treasury'][y] for y in year_range]) for r in name2r.values()]
    all_s = [np.mean([r['supply'][y]   for y in year_range]) for r in name2r.values()]
    all_r = [np.mean([r['ratio'][y]    for y in year_range]) for r in name2r.values()]
    all_d = [np.mean([r['depth'][y]    for y in year_range]) for r in name2r.values()]
    all_b = [r['breach_pct'] for r in name2r.values()]

    scores = {}
    for name, r in name2r.items():
        t = np.mean([r['treasury'][y] for y in year_range])
        s = np.mean([r['supply'][y]   for y in year_range])
        ratio = np.mean([r['ratio'][y]    for y in year_range])
        d = np.mean([r['depth'][y]    for y in year_range])
        b = r['breach_pct']

        score = (
            norm(t,     all_t, False) * 0.40 +
            norm(s,     all_s, True)  * 0.15 +
            norm(ratio, all_r, True)  * 0.20 +
            norm(b,     all_b, True)  * 0.15 +
            norm(d,     all_d, False) * 0.10
        )
        scores[name] = score
    return scores

scores_early = compute_score(results, range(1, 6))    # years 1-5
scores_late  = compute_score(results, range(10, 21))   # years 10-20

print(f"\n  {'MODEL':<24}  {'1–5yr':>8}  {'10–20yr':>8}  {'Overall*':>8}  {'TreasY5':>10}  {'TreasY20':>10}")
print(f"  {'─'*85}")
overall_scores = {}
for name in results:
    overall = scores_early[name] * 0.4 + scores_late[name] * 0.6
    overall_scores[name] = overall
    t5  = results[name]['treasury'][5]
    t20 = results[name]['treasury'][20] if 20 < N_YEARS else 0
    print(f"  {name:<24}  {scores_early[name]:8.3f}  {scores_late[name]:8.3f}  {overall:8.3f}  {fmt_v(t5):>10}  {fmt_v(t20):>10}")

w_early   = max(scores_early,   key=scores_early.get)
w_late    = max(scores_late,    key=scores_late.get)
w_overall = max(overall_scores, key=overall_scores.get)

print(f"\n  {'─'*85}")
print(f"  WINNER (1–5yr):    {w_early}")
print(f"  WINNER (10–20yr):  {w_late}")
print(f"  BEST OVERALL:      {w_overall}      (40/60 early/late)")
print()

# ── APY ──
print(f"{'='*100}")
print(f"  CD YIELD / APY PROXY (Treasury = fees → APY pool)")
print(f"{'='*100}")
for name, r in results.items():
    print(f"  {name:<24}  Yr5: {fmt_v(r['treasury'][5]):>10}  Yr10: {fmt_v(r['treasury'][10]):>10}  Yr20: {fmt_v(r['treasury'][20]):>10}")
best_t5  = max(results, key=lambda n: results[n]['treasury'][5])
best_t20 = max(results, key=lambda n: results[n]['treasury'][20])
print(f"\n  Highest APY (yr5):  {best_t5}")
print(f"  Highest APY (yr20): {best_t20}")

# ── Quick-n-dirty APY estimate ──
print(f"\n{'='*100}")
print(f"  APPROXIMATE APY ESTIMATE (treasury / HEAT supply)")
print(f"  (rough proxy — actual APY depends on CD utilization)")
print(f"{'='*100}")
for name, r in results.items():
    s5 = r['supply'][5]; t5 = r['treasury'][5]
    s20 = r['supply'][20] if 20<N_YEARS else 0; t20 = r['treasury'][20] if 20<N_YEARS else 0
    apy5 = t5/max(s5,1)*100; apy20=t20/max(s20,1)*100
    print(f"  {name:<24}  APY~yr5: {apy5:.2f}%  APY~yr20: {apy20:.2f}%")

print()
