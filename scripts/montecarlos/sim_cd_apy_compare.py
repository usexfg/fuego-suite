#!/usr/bin/env python3
# ⚠️ SUPERSEDED by sim_final_v18.py — this sim hallucinates a HEAT→XFG burn path
# that does not exist. HEAT is mint-only. The AMM pool is the sole exit.
"""
HEAT CD APY — 5-Model Monte Carlo (10,000 sims × 20yr)
=======================================================
Compares CD yield distribution models under identical 8:1 Full Float market conditions.

Models:
  M1 — Raw Fee Share (baseline, no smoothing)
  M2 — Treasury-Smoothed Macro-Epoch
  M3 — 3-Layer Stabilization (CD-SWF → LP Yield → Treasury)
  M4 — Floor+Bonus (guaranteed floor + variable upside)
  M5 — Pre-Funded Macro-Epoch
"""

import numpy as np
import time, sys
from collections import OrderedDict

# ══════════════════════════════════════════════════════════════
#  CONSTANTS
# ══════════════════════════════════════════════════════════════
EPY            = 24                       # epochs per year (monthly-ish)
N_SIMS         = 10000                    # Monte Carlo runs
N_EPOCHS       = EPY * 21                # 21 years (y0–y20)
N_YEARS        = 21
MACRO_LEN      = 3                       # epochs per macro-epoch

LAUNCH         = 0.125                    # 8:1 launch ratio
KP, KI         = 0.08, 0.015             # PI gains
SWAP_FEE       = 0.02
CD_SHARE       = 0.80
TREASURY_SH    = 0.20
MINT_PREMIUM   = 0.05
CD_LOCK_RATE   = 0.08                    # % of supply locked per epoch
CD_DUR_MEAN    = 60                      # mean CD duration in epochs (~2.5yr at EPY=24)
CD_DUR_STD     = 20

LP_YIELD_RATE  = 0.005                   # LP yield accrual rate

M2_WIN    = 3                            # M2 rolling window
M3_WIN    = 6                            # M3 rolling window
M3_TCAP   = 0.005                        # M3 treasury cap per epoch
M4_SM     = 0.80                         # M4 safety margin
M5_PCT    = 0.05                         # M5 pre-commit %

EPS = 1e-9
SEED_BASE = 20260520

# ══════════════════════════════════════════════════════════════
#  XFG PRICE MODEL
# ══════════════════════════════════════════════════════════════
def xfg_price(t, rng):
    base = 2.0 + 1.0 * np.exp(0.15 * t)
    return base * float(1.0 + rng.normal(0.0, 0.25))

# ══════════════════════════════════════════════════════════════
#  CD BOOK (shared state across models)
# ══════════════════════════════════════════════════════════════
class CDBook:
    def __init__(self, maturity_sched):
        self.locked    = 0.0
        self.mat       = maturity_sched  # array: maturity_sched[ep] = amount maturing

    def step(self, ep, supply, rng):
        """Lock/unlock — identical for all models."""
        matured = min(self.mat[ep], self.locked * 0.95) if self.locked > 0 else 0.0
        self.locked -= matured
        jitter = float(rng.random() * 0.5 + 0.75)
        new_lock = supply * CD_LOCK_RATE * jitter
        self.locked += new_lock
        dur = int(np.clip(rng.normal(CD_DUR_MEAN, CD_DUR_STD), 30, 400))
        mep = ep + dur
        if mep < len(self.mat):
            self.mat[mep] += new_lock


# ══════════════════════════════════════════════════════════════
#  M1 — RAW FEE SHARE (baseline)
# ══════════════════════════════════════════════════════════════
def m1_raw(book, cd_pool, ep, treasury, fees, lp_yield, state, rng):
    """Distribute 80% of fees as-is. Zero smoothing."""
    if book.locked <= 0 or cd_pool <= 0:
        return 0.0, cd_pool, treasury, state
    rate = cd_pool / book.locked
    paid = min(cd_pool, rate * book.locked)
    return paid, cd_pool - paid, treasury, state


# ══════════════════════════════════════════════════════════════
#  M2 — TREASURY-SMOOTHED MACRO-EPOCH
# ══════════════════════════════════════════════════════════════
def m2_treasury_smoothed(book, cd_pool, ep, treasury, fees, lp_yield, state, rng):
    if book.locked <= 0:
        return 0.0, cd_pool, treasury, state

    org = (fees * CD_SHARE) / book.locked
    state.setdefault('rates', []).append(org)
    if len(state['rates']) > M2_WIN:
        state['rates'].pop(0)
    target = np.mean(state['rates']) if state['rates'] else org

    required = target * book.locked
    avail = fees * CD_SHARE
    deficit = max(0.0, required - avail)
    surplus = max(0.0, avail - required)

    paid = avail
    if deficit > 0:
        draw = min(deficit, treasury)
        treasury -= draw
        paid += draw

    if surplus > 0:
        treasury += surplus

    return paid, max(0.0, cd_pool - paid), treasury, state


# ══════════════════════════════════════════════════════════════
#  M3 — 3-LAYER STABILIZATION
# ══════════════════════════════════════════════════════════════
def m3_three_layer(book, cd_pool, ep, treasury, fees, lp_yield, state, rng):
    state.setdefault('reserve', 0.0)
    state.setdefault('lp_buf', 0.0)
    state.setdefault('rates', [])

    if book.locked <= 0:
        state['lp_buf'] += lp_yield * 0.5
        return 0.0, cd_pool, treasury, state

    org = (fees * CD_SHARE) / book.locked
    state['rates'].append(org)
    if len(state['rates']) > M3_WIN:
        state['rates'].pop(0)
    target = np.mean(state['rates']) if state['rates'] else org

    required = target * book.locked
    avail = fees * CD_SHARE
    deficit = max(0.0, required - avail)
    surplus = max(0.0, avail - required)

    if surplus > 0:
        state['reserve'] += surplus * 0.6   # save 60% of surplus

    # Layer 1: CD reserve
    d1 = min(deficit, state['reserve'])
    state['reserve'] -= d1
    deficit -= d1

    # Capture LP yield
    state['lp_buf'] += lp_yield * 0.5

    # Layer 2: LP yield buffer
    d2 = min(deficit, state['lp_buf'])
    state['lp_buf'] -= d2
    deficit -= d2

    # Layer 3: Treasury (capped)
    d3 = min(deficit, treasury * M3_TCAP)
    treasury -= d3
    deficit -= d3

    paid = avail + d1 + d2 + d3
    return paid, max(0.0, cd_pool - paid), treasury, state


# ══════════════════════════════════════════════════════════════
#  M4 — FLOOR+BONUS
# ══════════════════════════════════════════════════════════════
def m4_floor_bonus(book, cd_pool, ep, treasury, fees, lp_yield, state, rng):
    state.setdefault('reserve', 0.0)
    state.setdefault('lp_buf', 0.0)

    if book.locked <= 0:
        state['lp_buf'] += lp_yield * 0.5
        return 0.0, cd_pool, treasury, state

    org = (fees * CD_SHARE) / book.locked
    state['lp_buf'] += lp_yield * 0.5
    total_avail = state['reserve'] + state['lp_buf'] + treasury * 0.03
    floor_rate = (total_avail * M4_SM) / (book.locked * MACRO_LEN)

    if org >= floor_rate:
        paid = ((floor_rate + (org - floor_rate) * 0.9) * book.locked)  # 90% of bonus
        excess = (org - floor_rate) * book.locked * 0.1  # save 10% to reserve
        state['reserve'] += excess
    else:
        paid = org * book.locked

    shortfall = max(0.0, paid - fees * CD_SHARE)
    if shortfall > 0:
        d1 = min(shortfall, state['reserve'])
        state['reserve'] -= d1
        shortfall -= d1
    if shortfall > 0:
        d2 = min(shortfall, state['lp_buf'])
        state['lp_buf'] -= d2
        shortfall -= d2
    if shortfall > 0:
        d3 = min(shortfall, treasury * 0.003)
        treasury -= d3

    actual = max(fees * CD_SHARE, paid - shortfall)
    return actual, max(0.0, cd_pool - actual), treasury, state


# ══════════════════════════════════════════════════════════════
#  M5 — PRE-FUNDED MACRO-EPOCH
# ══════════════════════════════════════════════════════════════
def m5_prefunded(book, cd_pool, ep, treasury, fees, lp_yield, state, rng):
    state.setdefault('committed', 0.0)
    state.setdefault('remaining', 0.0)

    phase = ep % MACRO_LEN

    if phase == 0 and treasury > 0:
        state['committed'] = treasury * M5_PCT
        state['remaining'] = state['committed']
        treasury -= state['committed']

    per_epoch = state['committed'] / MACRO_LEN
    avail = fees * CD_SHARE + per_epoch

    if book.locked > 0:
        paid = avail
        used_committed = paid - fees * CD_SHARE
        state['remaining'] = max(0.0, state['remaining'] - used_committed)
    else:
        paid = fees * CD_SHARE

    if phase == MACRO_LEN - 1:
        treasury += max(0.0, state['remaining'])
        state['committed'] = 0.0
        state['remaining'] = 0.0

    return paid, max(0.0, cd_pool - paid), treasury, state


# ══════════════════════════════════════════════════════════════
#  SINGLE SIMULATION RUN
# ══════════════════════════════════════════════════════════════
def sim_single(rng, dist_func, maturity_sched):
    """One 21-year simulation with integrated market + CD model."""
    px = 5000.0; ph = 25000.0
    redemption = LAUNCH; integral = 0.0
    treasury_bal = 0.0; cd_pool = 0.0
    heat_sup = ph  # initial supply = pool HEAT (bootstrap for CD locking)
    lp_yield_acc = 0.0
    oracle_start = int(rng.randint(EPY//2, EPY*2))
    launch_twap = None
    model_state = {}

    # Per-year accumulators
    supply_yr   = np.zeros(N_YEARS)
    treas_yr    = np.zeros(N_YEARS)
    ratio_yr    = np.zeros(N_YEARS)
    price_yr    = np.zeros(N_YEARS)
    depth_yr    = np.zeros(N_YEARS)
    fees_yr     = np.zeros(N_YEARS)
    swap_yr     = np.zeros(N_YEARS)
    yield_yr    = np.zeros(N_YEARS)       # total yield paid in year
    locked_yr   = np.zeros(N_YEARS)       # sum of cd_locked per epoch (for avg)
    reserve_yr  = np.zeros(N_YEARS)
    lp_buf_yr   = np.zeros(N_YEARS)
    epoch_cnt_yr= np.zeros(N_YEARS)       # epochs counted in each year

    book = CDBook(maturity_sched)

    for ep in range(N_EPOCHS):
        yr = ep // EPY
        if yr >= N_YEARS:
            break
        t = ep / EPY
        epoch_cnt_yr[yr] += 1.0

        xp = xfg_price(t, rng)
        oracle_stale = (ep < oracle_start) or (rng.random() < 0.05)

        vm = np.clip(xp / 2.0, 0.3, 5.0)
        vol_e = 20000.0 * vm * float(np.exp(rng.normal(0.0, 0.10)))
        swap_yr[yr] += vol_e

        fees = vol_e * SWAP_FEE
        treasury_bal += fees * TREASURY_SH
        cd_pool      += fees * CD_SHARE
        fees_yr[yr]  += fees

        lp_yield_acc += fees * LP_YIELD_RATE

        # Hearth swaps
        sv = vol_e * 0.05
        if px > 0 and ph > 0:
            for _ in range(3):
                if rng.random() < 0.5:
                    cap = min(sv / 3.0, px * 0.005)
                    ph -= ph * cap / (px + cap); px += cap
                else:
                    cap = min(sv / 3.0, ph * 0.005)
                    px -= px * cap / (ph + cap); ph += cap

        spot = px / max(ph, EPS)

        if launch_twap is None and not oracle_stale and spot > 0:
            launch_twap = spot

        target = (LAUNCH * launch_twap / spot) if (launch_twap is not None and spot > EPS) else LAUNCH
        target = np.clip(target, 1e-5, 10.0)
        dev = (spot - target) / max(target, EPS)
        dt = 1.0 / EPY
        integral = np.clip(integral + dev * dt, -1.0, 1.0)
        red_rate = np.clip(KP * dev + KI * integral, -0.50, 0.50)
        redemption = max(EPS, target * (1.0 + red_rate * dt))

        # Mint
        mp = max(0.0, 0.008 - 0.4 * dev)
        mint_vol = vol_e * 0.08 / max(spot, EPS) * mp
        heat_sup += mint_vol * (1.0 - MINT_PREMIUM)

        # CD lifecycle
        book.step(ep, heat_sup, rng)

        # Model-specific yield distribution
        yield_paid, cd_pool, treasury_bal, model_state = dist_func(
            book, cd_pool, ep, treasury_bal, fees, lp_yield_acc, model_state, rng
        )

        # CD yield buyback from Hearth (moves HEAT from pool → CD holders, not new supply)
        if yield_paid > 0 and ph > 0:
            hb = ph * yield_paid / (px + yield_paid)
            if 0 < hb < ph * 0.95:
                px += yield_paid; ph -= hb

        # Treasury rebalancer
        if ph > 0 and treasury_bal > 0 and (px / max(ph, EPS)) > 3.0:
            reb = min(treasury_bal * 0.10, px * 0.03)
            if redemption > 0:
                ph += reb / redemption; treasury_bal -= reb
                heat_sup += reb / redemption

        # Annual accumulators
        yield_yr[yr]  += yield_paid
        locked_yr[yr]  += book.locked

        supply_yr[yr]   = heat_sup
        treas_yr[yr]    = treasury_bal
        ratio_yr[yr]    = spot
        price_yr[yr]    = redemption
        depth_yr[yr]    = px
        reserve_yr[yr]  = model_state.get('reserve', 0.0)
        lp_buf_yr[yr]   = model_state.get('lp_buf', 0.0)

    # Compute per-year APY
    apy_yr = np.zeros(N_YEARS)
    for yr in range(N_YEARS):
        ec = epoch_cnt_yr[yr]
        if ec > 0 and locked_yr[yr] > 0:
            avg_locked = locked_yr[yr] / ec
            apy_yr[yr] = (yield_yr[yr] / avg_locked) * EPY / ec * 100.0

    # APY stability metrics
    tail = apy_yr[3:]  # skip first 3 years
    apy_vol = float(np.std(tail)) if len(tail) > 1 else 0.0
    worst_apy = float(np.min(apy_yr[1:])) if len(apy_yr) > 1 else 0.0

    total_fees = np.sum(fees_yr)
    total_yield = np.sum(yield_yr)
    fee_eff = total_yield / max(total_fees, EPS) * 100.0

    return {
        'supply':     supply_yr,
        'treasury':   treas_yr,
        'locked':     locked_yr,
        'yield':      yield_yr,
        'apy':        apy_yr,
        'reserve':    reserve_yr,
        'lp_buf':     lp_buf_yr,
        'ratio':      ratio_yr,
        'price':      price_yr,
        'depth':      depth_yr,
        'fees':       fees_yr,
        'swap':       swap_yr,
        'treasury_final': treasury_bal,
        'total_yield':    total_yield,
        'worst_apy':      worst_apy,
        'apy_vol':        apy_vol,
        'fee_eff':        fee_eff,
    }


# ══════════════════════════════════════════════════════════════
#  MAIN
# ══════════════════════════════════════════════════════════════

MODELS = OrderedDict([
    ('M1: Raw Fee Share',              m1_raw),
    ('M2: Treasury-Smoothed Macro',     m2_treasury_smoothed),
    ('M3: 3-Layer Stabilization',       m3_three_layer),
    ('M4: Floor+Bonus',                 m4_floor_bonus),
    ('M5: Pre-Funded Macro-Epoch',      m5_prefunded),
])

def fmt(v):
    if abs(v) >= 1e9: return f"{v/1e9:7.2f}B"
    if abs(v) >= 1e6: return f"{v/1e6:7.2f}M"
    if abs(v) >= 1e3: return f"{v/1e3:7.1f}K"
    return f"{v:8,.0f}"

def norm(val, all_vals, lower_better):
    vmin, vmax = min(all_vals), max(all_vals)
    if vmax == vmin: return 0.5
    return (vmax - val) / (vmax - vmin) if lower_better else (val - vmin) / (vmax - vmin)

def run():
    print(f"\n{'='*130}")
    print(f"  HEAT CD APY — 5-MODEL MONTE CARLO ({N_SIMS:,} sims × {N_YEARS-1}yr)")
    print(f"  8:1 Full Float | CD lock={CD_LOCK_RATE*100:.0f}%/epoch | Duration~{CD_DUR_MEAN} epochs")
    print(f"  M2 win={M2_WIN} | M3 win={M3_WIN} tcap={M3_TCAP} | M4 sm={M4_SM} | M5 pct={M5_PCT*100:.0f}%")
    print(f"{'='*130}\n")

    all_res = OrderedDict()
    t_total = time.time()

    for name, fn in MODELS.items():
        t0 = time.time()
        batch = []

        for s in range(N_SIMS):
            rng = np.random.RandomState(SEED_BASE + s)
            maturity = np.zeros(N_EPOCHS + 500)
            r = sim_single(rng, fn, maturity)
            batch.append(r)

            # Progress
            if (s + 1) % 1000 == 0:
                elapsed = time.time() - t0
                eta = elapsed / (s + 1) * (N_SIMS - s - 1)
                print(f"\r  {name:<32} {s+1:>5,}/{N_SIMS:,}  "
                      f"{elapsed:.0f}s  eta={eta:.0f}s", end='', flush=True)

        dt = time.time() - t0

        # Compute medians across all sims
        med = {}
        for key in ['supply','treasury','locked','yield','apy','reserve','lp_buf',
                     'ratio','price','depth','fees','swap']:
            med[key] = np.array([np.median([b[key][y] for b in batch]) for y in range(N_YEARS)])
        for key in ['treasury_final','total_yield','worst_apy','apy_vol','fee_eff']:
            med[key] = np.median([b[key] for b in batch])

        all_res[name] = med

        avg_apy = np.mean(med['apy'][3:])
        print(f"\r  {name:<32} {s+1:>5,}/{N_SIMS:,}  {dt:6.0f}s  "
              f"AvgAPY={avg_apy:5.2f}%  Worst={med['worst_apy']:5.2f}%  "
              f"Vol={med['apy_vol']:.3f}  TreasFinal={fmt(med['treasury_final'])}",
              flush=True)

    total_dt = time.time() - t_total
    print(f"\n  Total: {total_dt:.0f}s ({total_dt/60:.1f} min)\n")
    return all_res


def report(results):
    yr_range = range(3, N_YEARS)

    # ── Year snapshots ──
    for label, years in [("YEAR 5", [5]), ("YEAR 10", [10]), ("YEAR 20", [20])]:
        print(f"\n{'─'*140}")
        print(f"  {label}")
        print(f"{'─'*140}")
        print(f"  {'MODEL':<30} {'TREASURY':>10} {'APY':>7} {'CD_LOCKED':>10} "
              f"{'YIELD':>12} {'CD_RES':>10} {'LP_BUF':>10} {'POOL':>7} {'FEE_EFF':>8}")
        for name, r in results.items():
            y = years[0]
            print(f"  {name:<30} {fmt(r['treasury'][y]):>10} {r['apy'][y]:6.2f}% "
                  f"{fmt(r['locked'][y]):>10} {fmt(r['yield'][y]):>12} "
                  f"{fmt(r['reserve'][y]):>10} {fmt(r['lp_buf'][y]):>10} "
                  f"{r['ratio'][y]:.1f}:1 {r['fee_eff']:7.2f}%")

    # ── APY timeline ──
    print(f"\n{'='*130}")
    print(f"  APY OVER TIME (median %)")
    print(f"{'='*130}")
    yrs = [1,2,3,5,7,10,15,20]
    print(f"  {'MODEL':<30} " + " ".join([f"Y{y:>2}" for y in yrs]))
    for name, r in results.items():
        vals = " ".join([f"{r['apy'][y]:6.2f}" for y in yrs])
        print(f"  {name:<30} {vals}")

    # ── Composite score ──
    print(f"\n{'='*110}")
    print(f"  COMPOSITE SCORE  (weights: APY 35% | Treasury 25% | Stability 20% | FeeEff 10% | Depth 10%)")
    print(f"{'='*110}")

    all_apy   = [np.mean([r['apy'][y] for y in yr_range]) for r in results.values()]
    all_treas = [np.mean([r['treasury'][y] for y in yr_range]) for r in results.values()]
    all_vol   = [r['apy_vol'] for r in results.values()]
    all_eff   = [r['fee_eff'] for r in results.values()]
    all_depth = [np.mean([r['depth'][y] for y in yr_range]) for r in results.values()]

    scores = OrderedDict()
    for i, (name, r) in enumerate(results.items()):
        s_apy   = norm(all_apy[i],   all_apy,   False)
        s_treas = norm(all_treas[i], all_treas, False)
        s_vol   = norm(all_vol[i],   all_vol,   True)
        s_eff   = norm(all_eff[i],   all_eff,   False)
        s_depth = norm(all_depth[i], all_depth, False)

        total = s_apy*0.35 + s_treas*0.25 + s_vol*0.20 + s_eff*0.10 + s_depth*0.10
        scores[name] = total

        print(f"  {name:<30} APY={s_apy:.3f} Treas={s_treas:.3f} "
              f"Stab={s_vol:.3f} Eff={s_eff:.3f} Depth={s_depth:.3f} = {total:.3f}")

    # Ranking
    ranked = sorted(scores.items(), key=lambda x: x[1], reverse=True)
    print(f"\n  {'─'*70}")
    print(f"  RANKING")
    for i, (n, sc) in enumerate(ranked, 1):
        bar = '█' * int(sc * 60)
        print(f"  #{i} {n:<30} {sc:.3f}  {bar}")
    print(f"\n  WINNER:       {ranked[0][0]}  ({ranked[0][1]:.3f})")
    print(f"  RUNNER-UP:    {ranked[1][0]}  ({ranked[1][1]:.3f})")
    if len(ranked) > 2:
        print(f"  THIRD PLACE:  {ranked[2][0]}  ({ranked[2][1]:.3f})")

    # ── Summary ──
    print(f"\n{'='*120}")
    print(f"  KEY METRICS SUMMARY")
    print(f"{'='*120}")
    print(f"  {'MODEL':<30} {'AvgAPY':>8} {'WorstAPY':>9} {'APY_Vol':>8} "
          f"{'TreasFinal':>12} {'FeeEff':>8} {'CD_Locked_Avg':>13}")
    for name, r in results.items():
        avg_apy = np.mean([r['apy'][y] for y in yr_range])
        cd_avg = np.mean([r['locked'][y] for y in yr_range])
        print(f"  {name:<30} {avg_apy:7.2f}% {r['worst_apy']:8.2f}% {r['apy_vol']:7.3f}  "
              f"{fmt(r['treasury_final']):>12} {r['fee_eff']:7.2f}% {fmt(cd_avg):>13}")

    print()


if __name__ == '__main__':
    results = run()
    report(results)
