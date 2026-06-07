#!/usr/bin/env python3
# ⚠️ SUPERSEDED by sim_final_v18.py — this sim hallucinates a HEAT→XFG burn path
# that does not exist. HEAT is mint-only. The AMM pool is the sole exit.
"""
Fuego HEAT — Full Mode Comparison v8
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Extended from sim_mode_compare_v2.py. Adds:
  - G: 8:1 CPI self-ref (MODE 2 current, target ×1.58)
  - H: Fixed peg 1.58 + arbitrage (MODE 4 proposed)

Tracks: XFG price, HEAT pool price, HEAT redeem price, supply,
treasury, CD depth, APY proxy, pool depth, death spiral risk,
peg stability %, mint volume, swap volume.

10-year horizon, 200 sims, identical seeds per model.
"""

import numpy as np, time, sys
from collections import OrderedDict

EPY = 73
N_SIMS = 200
N_EPOCHS = EPY * 11          # years 0..10 inclusive
N_YEARS = 11
DT = 1.0 / EPY

# ── Launch ratios ──
LAUNCH_5 = 0.2               # 5:1
LAUNCH_8 = 0.125             # 8:1
CPI_MULT = 1.58              # Dec 2008 → current
HEAT_PEG  = 1.58             # $1.58 target

# ── PI Controller ──
KP = 0.08; KI = 0.015
PI_MAX = 0.50; PI_CLAMP = 1.0

# ── Fees ──
SWAP_FEE  = 0.02              # 2% swap fee
CD_SHARE  = 0.80              # 80% → CD yield
TREAS_SH  = 0.20              # 20% → treasury
HEARTH_FEE = 0.003            # 0.3% Hearth fee

# ── Bands ──
BAND_FLOOR = 1.50; BAND_CEIL = 2.50
THRESHOLD_5 = 5.00; THRESHOLD_8 = 8.00

# ── Pool ──
POOL_START_XFG = 5000.0; POOL_START_HEAT = 25000.0

# ── XFG Price Model ──
SEED_BASE = 20260529

def xfg_price_model(t, rng):
    """XFG grows from $2 → $25 over 20 years, crosses $5 around yr4-6, $8 around yr8-10"""
    base = 2.0 + 1.0 * np.exp(0.15 * t)
    return base * float(1.0 + rng.normal(0.0, 0.25))


# ═══════════════════════════════════════════════════════════════
#  SIMULATION CORE
# ═══════════════════════════════════════════════════════════════

def sim_core(rng, launch, threshold, activation_mode):
    px = POOL_START_XFG; ph = POOL_START_HEAT

    # Mode 4: bootstrap pool at target ratio
    if activation_mode == 'fixed_peg':
        init_price = xfg_price_model(0, rng)
        target0 = HEAT_PEG / init_price
        ph = px / target0 if target0 > 0 else POOL_START_HEAT

    redemption = launch
    if activation_mode == 'cpi_self_ref':
        redemption = launch * CPI_MULT
    integral = 0.0

    treasury = 0.0; cd_pool = 0.0; heat_sup = 0.0
    heat_cd_fees = 0.0
    oracle_bridge_start = max(1, int(rng.randint(EPY//2, EPY*2)))
    launch_twap = None

    # Yearly snapshots (last epoch per year wins)
    supply_y = np.zeros(N_YEARS)
    treas_y  = np.zeros(N_YEARS)
    cdpool_y = np.zeros(N_YEARS)
    ratio_y  = np.zeros(N_YEARS)
    redeem_y = np.zeros(N_YEARS)
    depth_y  = np.zeros(N_YEARS)
    heatval_y = np.zeros(N_YEARS)
    xfgprice_y = np.zeros(N_YEARS)
    mintvol_y = np.zeros(N_YEARS)
    swapvol_y = np.zeros(N_YEARS)

    # Death spiral tracking
    spiral_epochs = 0           # consecutive epochs of extreme deviation
    spiral_triggered = False
    max_deviation = 0.0
    breach_count = 0
    epoch_count_banded = 0

    history = []  # (xfg_price, heat_pool_price, heat_redeem_price, spot, target, dev)

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

        # Volume scaled to XFG price
        vol_mult = np.clip(xfg_price / 2.0, 0.3, 5.0)
        vol_e = 20000.0 * vol_mult * float(np.exp(rng.normal(0.0, 0.10)))
        swapvol_y[yr] += vol_e

        fees = vol_e * SWAP_FEE
        treasury += fees * TREAS_SH
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

        # ── TARGET RATIO ──
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
            if px > 5000.0 and oracle_active and not oracle_stale:
                activated = True
                heat_value = spot * oracle_price
                target = np.clip(heat_value, BAND_FLOOR, BAND_CEIL) / max(oracle_price, 0.01)
            elif launch_twap is not None and spot > 0.0001:
                target = launch * launch_twap / spot

        elif activation_mode == 'sigmoid':
            if launch_twap is not None and spot > 0.0001:
                target = launch * launch_twap / spot

        elif activation_mode == 'cpi_self_ref':
            # MODE 2 (current): self-ref × CPI
            if launch_twap is not None and spot > 0.0001:
                target = launch * CPI_MULT * launch_twap / spot
            else:
                target = launch * CPI_MULT

        elif activation_mode == 'fixed_peg':
            # MODE 4: fixed peg $1.58, oracle-dependent
            if oracle_active and not oracle_stale:
                target = HEAT_PEG / oracle_price
                activated = True
            else:
                # Oracle stale: fallback to self-ref
                if launch_twap is not None and spot > 0.0001:
                    target = launch * launch_twap / spot

        target = max(0.00001, target)

        # ── PI Controller (skipped for fixed_peg) ──
        if activation_mode == 'fixed_peg':
            redemption = target  # instant, no convergence
            dev = 0
            red_rate = 0
        else:
            dev = (spot - target) / max(target, 0.00001)
            dt = 1.0 / EPY
            integral = np.clip(integral + dev * dt, -PI_CLAMP, PI_CLAMP)
            red_rate = np.clip(KP * dev + KI * integral, -PI_MAX, PI_MAX)

            if activation_mode == 'sigmoid':
                S = 8.0; M = 2.0
                damp = 1.0 / (1.0 + np.exp(S * (abs(dev) - M)))
                red_rate *= (1.0 - damp)

            redemption = max(1e-6, target * (1.0 + red_rate * dt))

        # ── Arbitrage (Mode 4 only) ──
        if activation_mode == 'fixed_peg' and oracle_active and not oracle_stale:
            for _ in range(10):
                spot = px / max(ph, 0.001)
                pool_heat_price = spot * oracle_price
                gap = abs(pool_heat_price - HEAT_PEG) / HEAT_PEG
                if gap < 0.005:
                    break
                arb_size = min(px, ph) * 0.02
                if pool_heat_price > HEAT_PEG:
                    # Sell HEAT → push pool price down
                    out = px * arb_size * (1-HEARTH_FEE) / (ph + arb_size)
                    ph += arb_size; px -= out
                else:
                    # Buy HEAT → push pool price up
                    out = ph * arb_size * (1-HEARTH_FEE) / (px + arb_size)
                    px += arb_size; ph -= out

        spot = px / max(ph, 0.001)

        # Track band breach for banded models
        if activated and heat_value > 0:
            epoch_count_banded += 1
            if heat_value < BAND_FLOOR or heat_value > BAND_CEIL:
                breach_count += 1

        # Track death spiral
        pool_heat_price = spot * oracle_price if oracle_active else spot * xfg_price
        peg_dev = abs(pool_heat_price - HEAT_PEG) / HEAT_PEG if activation_mode == 'fixed_peg' else abs(dev)
        max_deviation = max(max_deviation, peg_dev if activation_mode == 'fixed_peg' else abs(dev))

        if peg_dev > 0.50:  # 50%+ deviation
            spiral_epochs += 1
            if spiral_epochs > EPY:  # sustained > 1 year
                spiral_triggered = True
        else:
            spiral_epochs = max(0, spiral_epochs - 1)

        # User mint
        mp = max(0.0, 0.008 - 0.4 * (abs(dev) if activation_mode != 'fixed_peg' else peg_dev))
        mint_vol = vol_e * 0.08 / max(spot, 0.001) * mp
        heat_sup += mint_vol * (1.0 - 0.05)  # 5% mint premium
        mintvol_y[yr] += mint_vol

        # CD yield → buy from Hearth
        if cd_pool > 0 and ph > 0:
            if activation_mode == 'fixed_peg':
                sr = 1.0
            else:
                sr = np.clip(1.0 + red_rate, 0.2, 3.0)
            spend = min(cd_pool, cd_pool * sr)
            hb = ph * spend / (px + spend)
            if 0 < hb < ph * 0.95:
                px += spend; ph -= hb
                heat_sup += hb; heat_cd_fees += hb; cd_pool -= spend

        # Treasury rebalancer
        if (activation_mode != 'fixed_peg' and
            ph > 0 and treasury > 0 and (px / max(ph, 0.001)) > 3.0):
            reb = min(treasury * 0.10, px * 0.03)
            if redemption > 0:
                heat_dep = reb / redemption
                ph += heat_dep; treasury -= reb
                heat_sup += heat_dep

        # ── Yearly snapshot ──
        supply_y[yr]    = heat_sup
        treas_y[yr]     = treasury
        cdpool_y[yr]    = cd_pool
        ratio_y[yr]     = spot
        redeem_y[yr]    = redemption
        depth_y[yr]     = px
        heatval_y[yr]   = pool_heat_price if activation_mode == 'fixed_peg' else heat_value
        xfgprice_y[yr]  = oracle_price if oracle_active else xfg_price

        # History for analysis
        if ep % max(1, N_EPOCHS // 100) == 0:
            history.append((ep, xfg_price, pool_heat_price if pool_heat_price > 0 else spot*xfg_price,
                          redemption * (oracle_price if oracle_active else xfg_price), spot, target,
                          peg_dev if activation_mode == 'fixed_peg' else abs(dev)))

    breach_pct = breach_count / max(1, epoch_count_banded) * 100.0

    # Volatility of HEAT pool price
    returns10 = []
    for i in range(2, min(11, N_YEARS)):
        if ratio_y[i-1] > 0:
            returns10.append((ratio_y[i] - ratio_y[i-1]) / ratio_y[i-1])
    volatility = np.std(returns10) if returns10 else 0.0

    # Healthy % (peg_dev < 8% AND no spiral)
    healthy_pct = 0
    if history:
        healthy_pct = np.mean([1 if h[6] < 0.08 else 0 for h in history])

    return {
        'supply':     supply_y,
        'treasury':   treas_y,
        'cd_pool':    cdpool_y,
        'ratio':      ratio_y,
        'redeem':     redeem_y,
        'depth':      depth_y,
        'heat_val':   heatval_y,
        'xfg_price':  xfgprice_y,
        'mint_vol':   mintvol_y,
        'swap_vol':   swapvol_y,
        'breach_pct': breach_pct,
        'volatility': volatility,
        'spiral':     spiral_triggered,
        'max_dev':    max_deviation,
        'healthy_pct': healthy_pct,
        'history':    history,
    }


# ═══════════════════════════════════════════════════════════════
#  MODELS
# ═══════════════════════════════════════════════════════════════

MODELS = OrderedDict([
    ('A: 5:1 Band',   dict(launch=LAUNCH_5, threshold=THRESHOLD_5, mode='oracle_price')),
    ('B: 5:1 Float',  dict(launch=LAUNCH_5, threshold=0,          mode='always')),
    ('C: 8:1 Band',   dict(launch=LAUNCH_8, threshold=THRESHOLD_8, mode='oracle_price')),
    ('D: 8:1 Float',  dict(launch=LAUNCH_8, threshold=0,          mode='always')),
    ('E: Sigmoid 8:1',dict(launch=LAUNCH_8, threshold=0,          mode='sigmoid')),
    ('F: DepthGate',  dict(launch=LAUNCH_5, threshold=0,          mode='pool_depth')),
    ('G: CPI SelfRef',dict(launch=LAUNCH_8, threshold=0,          mode='cpi_self_ref')),
    ('H: Fixed Peg',  dict(launch=LAUNCH_8, threshold=0,          mode='fixed_peg')),
])


# ═══════════════════════════════════════════════════════════════
#  RUN
# ═══════════════════════════════════════════════════════════════

def fmt_v(v, suffix='', scale=1.0):
    v = v * scale
    if abs(v) >= 1e6: return f"{v/1e6:7.2f}M{suffix}"
    if abs(v) >= 1e3: return f"{v/1e3:7.1f}K{suffix}"
    return f"{v:8,.0f}{suffix}"


print(f"\n{'='*120}")
print(f"  FUEGO HEAT — FULL MODE COMPARISON v8")
print(f"  {N_SIMS} sims × 10 years ({N_EPOCHS} epochs, {EPY} epochs/yr) | KP={KP} KI={KI} CPI={CPI_MULT}x")
print(f"  XFG price model: $2 → ~$25 (crosses $5 ~yr5, $8 ~yr10)")
print(f"  G = MODE 2 (current): 8:1 self-ref × CPI  |  H = MODE 4 (proposed): fixed $1.58 peg")
print(f"{'='*120}")

results = OrderedDict()
price_traces = OrderedDict()

for name, cfg in MODELS.items():
    sys.stdout.write(f"  {name:<24} "); sys.stdout.flush()
    t0 = time.time()

    batch = []
    for s in range(N_SIMS):
        rng = np.random.RandomState(SEED_BASE + hash(name + str(s)) % (2**30))
        batch.append(sim_core(rng, cfg['launch'], cfg['threshold'], cfg['mode']))

    dt = time.time() - t0

    # Medians
    med = {}
    for key in ['supply','treasury','cd_pool','ratio','redeem','depth','mint_vol','swap_vol','heat_val','xfg_price']:
        med[key] = [np.median([b[key][y] for b in batch]) for y in range(N_YEARS)]
    med['breach_pct'] = np.median([b['breach_pct'] for b in batch])
    med['volatility'] = np.median([b['volatility'] for b in batch])
    med['spiral_pct'] = np.mean([b['spiral'] for b in batch]) * 100
    med['max_dev']    = np.median([b['max_dev'] for b in batch])
    med['healthy_pct'] = np.median([b['healthy_pct'] for b in batch]) * 100

    results[name] = med
    price_traces[name] = batch[0]['history']  # first sim history

    print(f"{dt:4.0f}s  Y5_sup={fmt_v(med['supply'][5]):>10}  Y5_treas={fmt_v(med['treasury'][5]):>10}  "
          f"health={med['healthy_pct']:>5.1f}%  spiral={med['spiral_pct']:>4.1f}%")


# ═══════════════════════════════════════════════════════════════
#  YEARLY TABLES
# ═══════════════════════════════════════════════════════════════

for label, years in [("YEAR 1", [1]), ("YEAR 3", [3]), ("YEAR 5", [5]), ("YEAR 10", [10])]:
    print(f"\n{'='*140}")
    print(f"  {label}")
    print(f"{'='*140}")
    hdr = (f"  {'MODEL':<22} {'HEAT$_pool':>10} {'HEAT$_redeem':>12} {'XFG$':>8} "
           f"{'Supply':>10} {'Treasury':>10} {'PoolRatio':>9} {'Health':>7} {'Spiral%':>7}")
    print(hdr)
    print("  " + "─" * 136)
    for name, r in results.items():
        for y in years:
            if y >= N_YEARS: continue
            pool_hp = r['ratio'][y] * r['xfg_price'][y] if r['ratio'][y] > 0 and r['xfg_price'][y] > 0 else 0
            redeem_hp = r['redeem'][y] * r['xfg_price'][y] if r['xfg_price'][y] > 0 else 0
            xfp = r['xfg_price'][y]
            spot = r['ratio'][y]
            print(f"  {name:<22} ${pool_hp:>8.2f}   ${redeem_hp:>10.2f}    ${xfp:>6.2f}  "
                  f"{fmt_v(r['supply'][y]):>10}  {fmt_v(r['treasury'][y]):>10}  "
                  f"{1/spot if spot>0 else 0:>7.1f}:1  {r['healthy_pct']:>5.1f}%  {r['spiral_pct']:>5.1f}%")


# ═══════════════════════════════════════════════════════════════
#  DEATH SPIRAL ANALYSIS
# ═══════════════════════════════════════════════════════════════

print(f"\n{'='*120}")
print(f"  DEATH SPIRAL & PEG STABILITY ANALYSIS")
print(f"  (Spiral = sustained >50% peg deviation for >1 year)")
print(f"{'='*120}")
print(f"  {'MODEL':<22} {'Spiral%':>8} {'MaxDev%':>8} {'Healthy%':>9} {'Volatilty':>9} {'Breach%':>8}")
print("  " + "─" * 90)
for name, r in results.items():
    print(f"  {name:<22} {r['spiral_pct']:>7.1f}% {r['max_dev']*100:>7.1f}% {r['healthy_pct']:>8.1f}% "
          f"{r['volatility']:>8.3f} {r['breach_pct']:>7.1f}%")


# ═══════════════════════════════════════════════════════════════
#  APY & TREASURY
# ═══════════════════════════════════════════════════════════════

print(f"\n{'='*120}")
print(f"  APY PROXY (CD treasury / HEAT supply) — rough estimate")
print(f"{'='*120}")
print(f"  {'MODEL':<22} {'TreasY1':>10} {'TreasY5':>10} {'TreasY10':>10} {'APY~Y1':>7} {'APY~Y5':>7} {'APY~Y10':>7}")
print("  " + "─" * 85)
for name, r in results.items():
    s1 = r['supply'][1]; s5 = r['supply'][5]; s10 = r['supply'][10]
    t1 = r['treasury'][1]; t5 = r['treasury'][5]; t10 = r['treasury'][10]
    apy1 = t1/max(s1,1)*EPY; apy5 = t5/max(s5,1)*EPY; apy10 = t10/max(s10,1)*EPY
    print(f"  {name:<22} {fmt_v(t1):>10} {fmt_v(t5):>10} {fmt_v(t10):>10} "
          f"{apy1:>6.1f}% {apy5:>6.1f}% {apy10:>6.1f}%")


# ═══════════════════════════════════════════════════════════════
#  COMPOSITE SCORE
# ═══════════════════════════════════════════════════════════════

print(f"\n{'='*120}")
print(f"  COMPOSITE SCORE (higher = better)")
print(f"  Stability 35% + LowSpiral 20% + Treasury 25% + PoolDepth 10% + LowSupply 10%")
print(f"{'='*120}")

def norm(val, all_vals, lower_better):
    vmin, vmax = min(all_vals), max(all_vals)
    if vmax == vmin: return 0.5
    return (vmax - val) / (vmax - vmin) if lower_better else (val - vmin) / (vmax - vmin)

year_range = range(3, 11)  # years 3-10 (skip bootstrap)

all_h = [r['healthy_pct'] for r in results.values()]
all_s = [r['spiral_pct'] for r in results.values()]
all_t = [np.mean([r['treasury'][y] for y in year_range]) for r in results.values()]
all_d = [np.mean([r['depth'][y]    for y in year_range]) for r in results.values()]
all_up = [np.mean([r['supply'][y]   for y in year_range]) for r in results.values()]

scores = {}
for name, r in results.items():
    h = r['healthy_pct']
    s = r['spiral_pct']
    t = np.mean([r['treasury'][y] for y in year_range])
    d = np.mean([r['depth'][y]    for y in year_range])
    sup = np.mean([r['supply'][y]   for y in year_range])
    score = (norm(h, all_h, False) * 0.35 + norm(s, all_s, True)  * 0.20 +
             norm(t, all_t, False) * 0.25 + norm(d, all_d, False) * 0.10 +
             norm(sup, all_up, True) * 0.10)
    scores[name] = score

ranked = sorted(scores.items(), key=lambda x: x[1], reverse=True)
for i, (name, score) in enumerate(ranked):
    print(f"  {i+1}. {name:<24} {score:.3f}")

print(f"\n  WINNER: {ranked[0][0]}  (score: {ranked[0][1]:.3f})")
print(f"  MODE 2 (G): {scores.get('G: CPI SelfRef', 0):.3f}")
print(f"  MODE 4 (H): {scores.get('H: Fixed Peg', 0):.3f}")

print()
sys.stdout.flush()
