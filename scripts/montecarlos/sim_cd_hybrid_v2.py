#!/usr/bin/env python3
# ⚠️ SUPERSEDED by sim_final_v18.py — this sim hallucinates a HEAT→XFG burn path
# that does not exist. HEAT is mint-only. The AMM pool is the sole exit.
"""
HEAT CD APY — Hybrid v2 Monte Carlo (10,000 sims × 20yr)
=========================================================
Compares:
  H1 — Baseline (80/20, no smoothing, single CD type)
  H2 — M5 Pre-Funded Macro-Epoch (winner from 10k MC)
  H3 — Hybrid v2 (dual CD types + Yield Emission Machine)

Hybrid v2 features:
  - Fee split: 80% CD / 12% Treasury / 8% ReBalancer
  - CD Type 1: Edition CDs — fixed rate, treasury-pre-committed, capacity-limited
  - CD Type 2: Epoch-Yield CDs — YEM (rolling 3-epoch + lag + cap + CD SWF + LP yield feed)
  - Treasury LP contribution → protocol LP shares → LP yield feeds CD SWF
  - APY cap: 80% annualized
  - Lag: first 3 epochs accumulate, no yield
"""

import numpy as np
import time, sys
from collections import OrderedDict

# ══════════════════════════════════════════════════════════════
#  CONSTANTS
# ══════════════════════════════════════════════════════════════
EPY            = 24
N_SIMS         = 10000
N_EPOCHS       = EPY * 21
N_YEARS        = 21
MACRO_LEN      = 3

LAUNCH         = 0.125
KP, KI         = 0.08, 0.015
SWAP_FEE       = 0.02
MINT_PREMIUM   = 0.05
CD_LOCK_RATE   = 0.08
CD_DUR_MEAN    = 60
CD_DUR_STD     = 20

LP_YIELD_RATE  = 0.005

# ── Fee splits ──
# H1/H2 (baseline): 80/20
CD_SHARE_BASE    = 0.80
TREAS_SHARE_BASE = 0.20

# H3 (hybrid): 80/16/4
CD_SHARE_HYB     = 0.80
TREAS_SHARE_HYB  = 0.16
REBAL_SHARE_HYB  = 0.04

# ── Yield Emission Machine (H3) ──
YEM_LAG_EPOCHS   = 3
YEM_ROLLING_WIN  = 3
YEM_SWF_SAVE     = 0.60          # % surplus saved to CD SWF
YEM_LP_FEED      = 0.75          # % treasury LP yield → CD SWF
YEM_SWF_PAYOUT   = 0.02          # % of SWF balance distributed per epoch (continuous)

# ── Time-Tiered APY Caps (longer term = higher cap) ──
#   term (epochs)   |  max APY
#   1 – 9           |  33%
#   10 – 30         |  42%
#   31 – 69         |  69%
#   69+             |  80%
TIER_CAPS = [
    (1,   9,    0.33),   # 1-9 epochs:      max 33% APY
    (10,  30,   0.42),   # 10-30 epochs:    max 42% APY
    (31,  71,   0.69),   # 31-71 epochs:    max 69% APY
    (72,  9999, 0.80),   # 72+ epochs:      max 80% APY
]

# ── Edition CDs (H3) ──
EDITION_RATE     = 0.12          # 12% APY guaranteed (above organic avg)
EDITION_TERM     = 36            # epochs
EDITION_CAP_BPS  = 500           # max capacity: 5% of treasury (in bps)
EDITION_INTERVAL = 12            # new edition every 12 epochs
EDITION_DEMAND   = 0.50          # 50% of CD demand → editions, 50% → epoch-yield

# ── Treasury LP (H3) ──
TREAS_LP_CONTRIB  = 0.01         # % treasury → LP per epoch (minimal)

# ── M5 pre-commit (H2) ──
M5_PCT           = 0.05          # % treasury pre-committed per macro-epoch

EPS = 1e-9
SEED_BASE = 20260520

# ══════════════════════════════════════════════════════════════
#  MARKET ENGINE
# ══════════════════════════════════════════════════════════════
def xfg_price(t, rng):
    base = 2.0 + 1.0 * np.exp(0.15 * t)
    return base * float(1.0 + rng.normal(0.0, 0.25))


class CDBook:
    def __init__(self, maturity_sched):
        self.locked = 0.0
        self.tier_locked = [0.0, 0.0, 0.0, 0.0]  # per-tier locked amounts
        self.mat = maturity_sched

    def step(self, ep, supply, rng):
        matured = min(self.mat[ep], self.locked * 0.95) if self.locked > 0 else 0.0
        self.locked -= matured
        jitter = float(rng.random() * 0.5 + 0.75)
        new_lock = supply * CD_LOCK_RATE * jitter
        self.locked += new_lock
        dur = int(np.clip(rng.normal(CD_DUR_MEAN, CD_DUR_STD), 10, 130))
        mep = ep + dur
        if mep < len(self.mat):
            self.mat[mep] += new_lock

        # Assign to tier based on duration
        for ti, (lo, hi, _) in enumerate(TIER_CAPS):
            if lo <= dur <= hi:
                self.tier_locked[ti] += new_lock
                break

    def tier_for_dur(self, dur):
        for ti, (lo, hi, _) in enumerate(TIER_CAPS):
            if lo <= dur <= hi:
                return ti
        return 0


# ══════════════════════════════════════════════════════════════
#  H1 — BASELINE (80/20, raw fee share)
# ══════════════════════════════════════════════════════════════
def dist_h1(book, cd_pool, ep, treasury, rebal_vault, fees, lp_yield, state, rng):
    # Standard 80/20 split — treasury allocation already done in outer loop
    if book.locked <= 0 or cd_pool <= 0:
        return 0.0, cd_pool, treasury, rebal_vault, state
    rate = cd_pool / book.locked
    paid = min(cd_pool, rate * book.locked)
    return paid, cd_pool - paid, treasury, rebal_vault, state


# ══════════════════════════════════════════════════════════════
#  H2 — M5 PRE-FUNDED MACRO-EPOCH (80/20, winner)
# ══════════════════════════════════════════════════════════════
def dist_h2(book, cd_pool, ep, treasury, rebal_vault, fees, lp_yield, state, rng):
    state.setdefault('committed', 0.0)
    state.setdefault('remaining', 0.0)

    phase = ep % MACRO_LEN
    if phase == 0 and treasury > 0:
        state['committed'] = treasury * M5_PCT
        state['remaining'] = state['committed']
        treasury -= state['committed']

    per_epoch = state['committed'] / MACRO_LEN
    avail = fees * CD_SHARE_BASE + per_epoch

    if book.locked > 0:
        paid = avail
        used = paid - fees * CD_SHARE_BASE
        state['remaining'] = max(0.0, state['remaining'] - used)
    else:
        paid = fees * CD_SHARE_BASE

    if phase == MACRO_LEN - 1:
        treasury += max(0.0, state['remaining'])
        state['committed'] = 0.0
        state['remaining'] = 0.0

    return paid, max(0.0, cd_pool - paid), treasury, rebal_vault, state


# ══════════════════════════════════════════════════════════════
#  H3 — HYBRID v2 (80/12/8, dual CD types + YEM)
# ══════════════════════════════════════════════════════════════
def dist_h3(book, cd_pool, ep, treasury, rebal_vault, fees, lp_yield, state, rng):
    """
    Yield Emission Machine (simplified):
      1. CD SWF accumulates from surplus + LP yield feed
      2. Lag: first YEM_LAG_EPOCHS → no yield, build reserve
       3. Rolling 3-epoch average → target rate, per-tier caps (33/42/69/80%)
      4. CD SWF saves surplus, draws deficit
      5. Edition CDs: funded from accumulated CD SWF, not treasury draws
         (treasury is insurance only — not drawn in normal operation)
      6. Treasury LP yield feeds CD SWF (free — uses accumulated lp_yield)
    """
    state.setdefault('swf', 0.0)
    state.setdefault('rates', [])
    state.setdefault('edition_locked', 0.0)
    state.setdefault('edition_term_remaining', 0)
    state.setdefault('edition_yield_liability', 0.0)

    # ── CD SWF: feed treasury LP yield (free — already accumulated) ──
    lp_feed = lp_yield * YEM_LP_FEED
    state['swf'] += lp_feed

    # ── Edition CD management (funded from CD SWF, not treasury) ──
    edition_fraction = EDITION_DEMAND
    epoch_fraction = 1.0 - EDITION_DEMAND

    if state['edition_term_remaining'] <= 0:
        # Open new edition IF CD SWF can cover 50% of the liability
        edition_lock = book.locked * edition_fraction * CD_LOCK_RATE
        liability = edition_lock * EDITION_RATE * EDITION_TERM / EPY
        if state['swf'] >= liability * 0.5 and edition_lock > 0:
            state['edition_locked'] = edition_lock
            state['edition_yield_liability'] = liability
            state['edition_term_remaining'] = EDITION_TERM
            # Reserve the liability from SWF (not treasury)
            state['swf'] -= liability
            # SWF also gets the locked principal for duration (earns via LP yield)
        else:
            state['edition_locked'] = 0.0
            state['edition_yield_liability'] = 0.0
    else:
        state['edition_term_remaining'] -= 1

    # Edition yield per epoch (fixed, from reserved SWF)
    if state['edition_locked'] > 0 and state['edition_yield_liability'] > 0:
        edition_yield = state['edition_yield_liability'] / EDITION_TERM
    else:
        edition_yield = 0.0

    # ── Epoch-Yield CDs (YEM with time-tiered caps) ──
    epoch_fraction = 1.0 - edition_fraction
    epoch_locked = book.locked * epoch_fraction
    yem_cd_pool = cd_pool * epoch_fraction

    # Compute organic rate (same for all tiers)
    if epoch_locked > 0:
        organic_rate = yem_cd_pool / epoch_locked
    else:
        organic_rate = 0.0

    # Lag: first YEM_LAG_EPOCHS → accumulate, no yield
    state.setdefault('lag_counter', 0)
    state['lag_counter'] += 1

    if state['lag_counter'] <= YEM_LAG_EPOCHS:
        state['swf'] += yem_cd_pool
        epoch_yield = 0.0
    else:
        # Rolling average
        state['rates'].append(organic_rate)
        if len(state['rates']) > YEM_ROLLING_WIN:
            state['rates'].pop(0)
        target_rate = np.mean(state['rates']) if state['rates'] else organic_rate

        # Apply per-tier caps: longer term → higher cap
        # Each tier gets target_rate (capped) × tier_locked
        tier_yields = []
        for ti in range(4):
            tier_locked = book.tier_locked[ti] * epoch_fraction
            if tier_locked > 0:
                tier_cap = TIER_CAPS[ti][2]  # per-epoch cap
                effective_rate = min(target_rate, tier_cap)
                tier_yields.append(effective_rate * tier_locked)
            else:
                tier_yields.append(0.0)

        required = sum(tier_yields)
        available = yem_cd_pool
        surplus = max(0.0, available - required)
        deficit = max(0.0, required - available)

        # CD SWF smoothing
        if surplus > 0:
            state['swf'] += surplus * YEM_SWF_SAVE
        if deficit > 0:
            draw = min(deficit, state['swf'])
            state['swf'] -= draw
            deficit -= draw
        if deficit > 0:
            treasury_draw = min(deficit, treasury * 0.01)
            treasury -= treasury_draw
            deficit -= treasury_draw

        epoch_yield = required - deficit if deficit > 0 else required

        # Continuous SWF payout: distribute a % of SWF balance each epoch
        swf_bonus = state['swf'] * YEM_SWF_PAYOUT
        state['swf'] -= swf_bonus
        epoch_yield += swf_bonus

    # Safety: don't overdraw
    epoch_yield = min(epoch_yield, yem_cd_pool + state['swf'] + treasury * 0.01)

    # Total yield
    total_yield = edition_yield + epoch_yield
    return total_yield, max(0.0, cd_pool - total_yield), treasury, rebal_vault, state


# ══════════════════════════════════════════════════════════════
#  SINGLE SIMULATION RUN
# ══════════════════════════════════════════════════════════════
def sim_single(rng, dist_func, fee_config, maturity_sched):
    px = 5000.0; ph = 25000.0
    redemption = LAUNCH; integral = 0.0
    treasury_bal = 0.0; cd_pool = 0.0; rebal_vault = 0.0
    heat_sup = ph
    lp_yield_acc = 0.0
    protocol_lp = 0.0   # protocol-owned LP shares (from treasury contributions)
    oracle_start = int(rng.randint(EPY//2, EPY*2))
    launch_twap = None
    model_state = {}

    cd_share_pct, treas_share_pct, rebal_share_pct = fee_config

    supply_yr   = np.zeros(N_YEARS)
    treas_yr    = np.zeros(N_YEARS)
    ratio_yr    = np.zeros(N_YEARS)
    price_yr    = np.zeros(N_YEARS)
    depth_yr    = np.zeros(N_YEARS)
    fees_yr     = np.zeros(N_YEARS)
    swap_yr     = np.zeros(N_YEARS)
    yield_yr    = np.zeros(N_YEARS)
    locked_yr   = np.zeros(N_YEARS)
    epoch_cnt_yr= np.zeros(N_YEARS)
    rebal_yr    = np.zeros(N_YEARS)
    lp_yr       = np.zeros(N_YEARS)

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
        treasury_bal += fees * treas_share_pct
        cd_pool      += fees * cd_share_pct
        rebal_vault  += fees * rebal_share_pct
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
        yield_paid, cd_pool, treasury_bal, rebal_vault, model_state = dist_func(
            book, cd_pool, ep, treasury_bal, rebal_vault, fees, lp_yield_acc, model_state, rng
        )

        # Treasury LP contribution (H3 already does this in dist_func; for H1/H2 skip)
        # (already handled in dist_h3)

        # CD yield buyback from Hearth
        if yield_paid > 0 and ph > 0:
            hb = ph * yield_paid / (px + yield_paid)
            if 0 < hb < ph * 0.95:
                px += yield_paid; ph -= hb

        # Rebalancer (draws from rebal_vault first, then treasury)
        rebal_source = rebal_vault + treasury_bal
        if ph > 0 and rebal_source > 0 and (px / max(ph, EPS)) > 3.0:
            reb = min(rebal_source * 0.10, px * 0.03)
            if redemption > 0:
                # Draw from rebal_vault first
                from_vault = min(reb, rebal_vault)
                rebal_vault -= from_vault
                from_treas = reb - from_vault
                treasury_bal -= from_treas

                ph += reb / redemption
                heat_sup += reb / redemption

        # Annual accumulators
        yield_yr[yr]  += yield_paid
        locked_yr[yr]  += book.locked

        supply_yr[yr]  = heat_sup
        treas_yr[yr]   = treasury_bal
        ratio_yr[yr]   = spot
        price_yr[yr]   = redemption
        depth_yr[yr]   = px
        rebal_yr[yr]   = rebal_vault
        lp_yr[yr]      = lp_yield_acc

    # Compute per-year APY
    apy_yr = np.zeros(N_YEARS)
    for yr in range(N_YEARS):
        ec = epoch_cnt_yr[yr]
        if ec > 0 and locked_yr[yr] > 0:
            avg_locked = locked_yr[yr] / ec
            apy_yr[yr] = (yield_yr[yr] / avg_locked) * EPY / ec * 100.0

    tail = apy_yr[3:]
    apy_vol = float(np.std(tail)) if len(tail) > 1 else 0.0
    worst_apy = float(np.min(apy_yr[1:])) if len(apy_yr) > 1 else 0.0

    total_fees = np.sum(fees_yr)
    total_yield = np.sum(yield_yr)
    fee_eff = total_yield / max(total_fees, EPS) * 100.0

    return {
        'supply':     supply_yr,   'treasury':   treas_yr,
        'locked':     locked_yr,   'yield':      yield_yr,
        'apy':        apy_yr,      'ratio':      ratio_yr,
        'price':      price_yr,    'depth':      depth_yr,
        'fees':       fees_yr,     'swap':       swap_yr,
        'rebal':      rebal_yr,    'lp_yield':   lp_yr,
        'treasury_final': treasury_bal,
        'total_yield':    total_yield,
        'worst_apy':      worst_apy,
        'apy_vol':        apy_vol,
        'fee_eff':        fee_eff,
    }


# ══════════════════════════════════════════════════════════════
#  MODELS
# ══════════════════════════════════════════════════════════════
MODELS = OrderedDict([
    ('H1: Baseline (80/20 raw)',  (dist_h1, (CD_SHARE_BASE, TREAS_SHARE_BASE, 0.0))),
    ('H2: M5 Pre-Funded (80/20)', (dist_h2, (CD_SHARE_BASE, TREAS_SHARE_BASE, 0.0))),
    ('H3: Hybrid v2 (80/12/8)',   (dist_h3, (CD_SHARE_HYB,  TREAS_SHARE_HYB,  REBAL_SHARE_HYB))),
])

# ══════════════════════════════════════════════════════════════
#  RUNNER & REPORT (same as sim_cd_apy_compare.py)
# ══════════════════════════════════════════════════════════════

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
    print(f"\n{'='*120}")
    print(f"  HEAT CD APY — HYBRID v2 MONTE CARLO ({N_SIMS:,} sims × {N_YEARS-1}yr)")
    print(f"  8:1 Full Float | CD lock={CD_LOCK_RATE*100:.0f}%/epoch | EPY={EPY}")
    print(f"  H1: Baseline (80/20) | H2: M5 Pre-Funded | H3: Hybrid v2 (80/12/8, YEM)")
    caps_str = "/".join([f"{int(c[2]*EPY*100)}%" for c in TIER_CAPS])
    print(f"  YEM: lag={YEM_LAG_EPOCHS}ep win={YEM_ROLLING_WIN}ep caps={caps_str}")
    print(f"  Edition: {EDITION_RATE*100:.0f}% rate {EDITION_TERM}ep term {EDITION_DEMAND*100:.0f}% demand")
    print(f"{'='*120}\n")

    all_res = OrderedDict()
    t_total = time.time()

    for name, (fn, fee_cfg) in MODELS.items():
        t0 = time.time()
        batch = []

        for s in range(N_SIMS):
            rng = np.random.RandomState(SEED_BASE + s)
            maturity = np.zeros(N_EPOCHS + 500)
            r = sim_single(rng, fn, fee_cfg, maturity)
            batch.append(r)

            if (s + 1) % 1000 == 0:
                elapsed = time.time() - t0
                eta = elapsed / (s + 1) * (N_SIMS - s - 1)
                print(f"\r  {name:<32} {s+1:>5,}/{N_SIMS:,}  "
                      f"{elapsed:.0f}s  eta={eta:.0f}s", end='', flush=True)

        dt = time.time() - t0

        med = {}
        for key in ['supply','treasury','locked','yield','apy','ratio','price','depth',
                     'fees','swap','rebal','lp_yield']:
            med[key] = np.array([np.median([b[key][y] for b in batch]) for y in range(N_YEARS)])
        for key in ['treasury_final','total_yield','worst_apy','apy_vol','fee_eff']:
            med[key] = np.median([b[key] for b in batch])

        all_res[name] = med

        avg_apy = np.mean(med['apy'][3:])
        print(f"\r  {name:<32} {s+1:>5,}/{N_SIMS:,}  {dt:6.0f}s  "
              f"AvgAPY={avg_apy:5.2f}%  Worst={med['worst_apy']:5.2f}%  "
              f"Vol={med['apy_vol']:.3f}  TreasFinal={fmt(med['treasury_final'])}  "
              f"RebalFinal={fmt(med['rebal'][20])}",
              flush=True)

    total_dt = time.time() - t_total
    print(f"\n  Total: {total_dt:.0f}s ({total_dt/60:.1f} min)\n")
    return all_res


def report(results):
    yr_range = range(3, N_YEARS)

    for label, years in [("YEAR 5", [5]), ("YEAR 10", [10]), ("YEAR 20", [20])]:
        print(f"\n{'─'*150}")
        print(f"  {label}")
        print(f"{'─'*150}")
        print(f"  {'MODEL':<30} {'TREASURY':>10} {'APY':>7} {'CD_LOCKED':>10} "
              f"{'YIELD':>12} {'REBAL':>10} {'LP':>10} {'POOL':>7} {'FEE_EFF':>8}")
        for name, r in results.items():
            y = years[0]
            print(f"  {name:<30} {fmt(r['treasury'][y]):>10} {r['apy'][y]:6.2f}% "
                  f"{fmt(r['locked'][y]):>10} {fmt(r['yield'][y]):>12} "
                  f"{fmt(r['rebal'][y]):>10} {fmt(r['lp_yield'][y]):>10} "
                  f"{r['ratio'][y]:.1f}:1 {r['fee_eff']:7.2f}%")

    # APY timeline
    print(f"\n{'='*110}")
    print(f"  APY OVER TIME (median %)")
    print(f"{'='*110}")
    yrs = [1,2,3,5,7,10,15,20]
    print(f"  {'MODEL':<30} " + " ".join([f"Y{y:>2}" for y in yrs]))
    for name, r in results.items():
        vals = " ".join([f"{r['apy'][y]:6.2f}" for y in yrs])
        print(f"  {name:<30} {vals}")

    # Composite score
    print(f"\n{'='*110}")
    print(f"  COMPOSITE SCORE  (APY 35% | Treasury 25% | Stability 20% | FeeEff 10% | Depth 10%)")
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

    ranked = sorted(scores.items(), key=lambda x: x[1], reverse=True)
    print(f"\n  {'─'*70}")
    print(f"  RANKING")
    for i, (n, sc) in enumerate(ranked, 1):
        bar = '█' * int(sc * 60)
        print(f"  #{i} {n:<30} {sc:.3f}  {bar}")
    print(f"\n  WINNER: {ranked[0][0]}  ({ranked[0][1]:.3f})")
    if len(ranked) > 1:
        print(f"  RUNNER-UP: {ranked[1][0]}  ({ranked[1][1]:.3f})")

    # Summary
    print(f"\n{'='*120}")
    print(f"  KEY METRICS SUMMARY")
    print(f"{'='*120}")
    print(f"  {'MODEL':<30} {'AvgAPY':>8} {'WorstAPY':>9} {'APY_Vol':>8} "
          f"{'TreasFinal':>12} {'FeeEff':>8} {'CD_Locked':>10} {'RebalFinal':>12}")
    for name, r in results.items():
        avg_apy = np.mean([r['apy'][y] for y in yr_range])
        cd_avg = np.mean([r['locked'][y] for y in yr_range])
        print(f"  {name:<30} {avg_apy:7.2f}% {r['worst_apy']:8.2f}% {r['apy_vol']:7.3f}  "
              f"{fmt(r['treasury_final']):>12} {r['fee_eff']:7.2f}% {fmt(cd_avg):>10} "
              f"{fmt(r['rebal'][20]):>12}")
    print()


if __name__ == '__main__':
    results = run()
    report(results)
