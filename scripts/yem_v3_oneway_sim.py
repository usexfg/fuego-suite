#!/usr/bin/env python3
"""
YEM v3 One-Way HEAT — Capital Allocation Optimizer
===================================================

Configurable mint capital split across:
  EF (Eternal Flame / hashrate)
  Treasury (peg defense)
  SWF LP (liquidity yield) + SWF Liquid (dry powder)
  YEM Reserve (system lubrication — bonds, deficit coverage)

One-way: HEAT is mint-only. No burn/redemption. Peg maintained via
arb ceiling (mint when above peg) + treasury market ops (buy when below).

Usage:
  python scripts/yem_v3_oneway_sim.py                      # baseline + grid search
  python scripts/yem_v3_oneway_sim.py --split "80/5/5/10"  # single split
  python scripts/yem_v3_oneway_sim.py --grid               # full 125-combination grid
  python scripts/yem_v3_oneway_sim.py --dynamic             # dynamic allocation
"""

import numpy as np, time, sys, os, json, csv, argparse
from collections import defaultdict
from itertools import product

# ═══════════════════════════════════════════════════════════════
#  CONSTANTS
# ═══════════════════════════════════════════════════════════════

EPY = 73
N_EPOCHS = EPY * 12
N_YEARS = 12
HEAT_PEG = 1.58
HEAT_PEG_SCALE = 100
SWAP_FEE_RATE = 0.01
HEARTH_FEE_BPS = 30

# Fee split (80/15/5) — same as two-way, applies to swap fees only
CD_SHARE = 0.80
TREAS_SHARE = 0.15
SWF_DIRECT = 0.05

# HEAT mint premium (what % above peg the user pays)
# This is PART of the optimization — varies across grid experiments
MINT_PREMIUM = 0.02          # default: 2%

# Capital allocation of the premium (EF / Treasury / YEM)
# ef_share + treas_share + yem_share = 1.0
EF_PREM_SHARE = 0.50         # 50% of premium → Eternal Flame
TREAS_PREM_SHARE = 0.10      # 10% → Treasury
YEM_PREM_SHARE = 0.40        # 40% → YEM Reserve

# SWF internal deployment (of the 98% base + fee revenue)
# lp_share + liq_share + lend_share = 1.0
SWF_LP_SHARE = 0.75          # 75% of SWF → AMM LP
SWF_LIQUID_SHARE = 0.20      # 20% → liquid reserve (peg defense, backstop)
SWF_LEND_SHARE = 0.05        # 5% → CD collateral lending

# YEM
YEM_LAG_EPOCHS = 3
YEM_ROLLING_WIN = 3
YEM_SWF_SAVE = 0.60
YEM_SWF_DRIP = 0.0005          # 0.05%/epoch (calibrated for large one-way SWF)
YEM_TREAS_BACKSTOP = 0.001

# Time-tiered caps
TIER_CAP_MIN = 0.33
TIER_CAP_MAX = 0.80
TIER_FULL = 72

# Bonds
BOND_MAX_RATE = 0.25
BOND_MIN_RATE = 0.03
BOND_RATE_MULT = 1.5
BOND_TERM_EPOCHS = 72

# LP / market
ARB_THRESHOLD = 0.01
ARB_MAX_POOL = 0.01
ARB_ITERS = 5

# Treasury LP
TREAS_LP_CONTRIB = 0.05
LP_YIELD_FEED = 0.75

# Initial state
INIT_POOL_XFG = 5000
INIT_CD_LOCKED = 80000
INIT_TREASURY = 30000
INIT_BOND_POOL = 50000
INIT_XFG_PRICE = 3.50

# CD demand
CD_LOCK_RATE = 0.08
CD_DUR_MEAN = 60
CD_DUR_STD = 20

SEED_BASE = 20260602
EPS = 1e-9

# Atomic swap fee rate — CD yield source (clean-revenue mode)
ATOMIC_SWAP_FEE_RATE = 0.01      # 1% of atomic swap volume → CD pool
ATOMIC_SWAP_MULT = 1.0           # atomic volume = 100% of regular swap volume

# Clean-revenue mode: regular swap fees → 100% LPs, atomic swap fees → 100% CD yield

# Mint collateral split (33/33/33 Treasury/Mining/SWF)
MINT_TREAS_SHARE = 0.333
MINT_MINING_SHARE = 0.333
MINT_SWF_SHARE = 0.333

# SWF drip split (45/45/10 Mining/SWF/Treasury)
DRIP_MINING_SHARE = 0.45
DRIP_SWF_SHARE = 0.45
DRIP_TREAS_SHARE = 0.10

# Premium dynamic gate — PI controller on pool balance
# When pool is balanced (peg ~1.0), premium = 0. When imbalanced, premium rises to slow minting.
PREMIUM_KP = 0.3        # proportional gain
PREMIUM_KI = 0.05       # integral gain
PREMIUM_MAX = 0.05      # max 5% premium
PREMIUM_TARGET = 0.0    # target peg deviation = 0 (HEAT at peg)

# BOUGH (third coin) — AMO peg defense + revenue smoothing
BOUGH_SUPPLY_CAP_RATIO = 8      # max mcap: SWF_balance / 8 (in XFG)
BOUGH_YIELD_SHARE = 0.05         # 5% of SWF drip → BOUGH holders (was 10%)
BOUGH_AMO_MINT_THRESHOLD = 0.97  # mint BOUGH when HEAT < 97% peg
BOUGH_AMO_BURN_THRESHOLD = 1.01  # burn BOUGH when HEAT > 101% peg
BOUGH_MAX_MINT_PCT = 0.05        # 5% max supply increase per event
BOUGH_MAX_BURN_PCT = 0.02        # 2% max supply decrease per event
BOUGH_TREAS_RATIO_TARGET = 0.15  # target treasury / heat_mcap
BOUGH_MINT_DISCOUNT = 0.95       # mint BOUGH at 95% of market price
BOUGH_PEG_BUY_FRAC = 0.50        # 50% of raised capital → HEAT buy
BOUGH_INIT_PRICE = 0.40          # initial BOUGH price (XFG) before market discovery
BOUGH_PRICE_FLOOR = 0.02         # minimum BOUGH price — prevents infinite dilution death spiral
BOUGH_MINT_COOLDOWN = 6           # min epochs between AMO mint events

# ═══════════════════════════════════════════════════════════════
#  DEFAULT ALLOCATION SPLITS (for "grid" mode)
# ═══════════════════════════════════════════════════════════════

GRID_PREMIUMS = [0.01, 0.03]            # 2 levels
GRID_EF_SHARES = [0.30, 0.70]           # 2 levels
GRID_TREAS_SHARES = [0.05, 0.30]        # 2 levels
GRID_SWF_LP_SHARES = [0.60, 0.90]       # 2 levels

# ═══════════════════════════════════════════════════════════════
#  HELPERS
# ═══════════════════════════════════════════════════════════════

def tier_cap(duration_epochs):
    d = min(max(duration_epochs, 1), TIER_FULL)
    return TIER_CAP_MIN + (d - 1) / (TIER_FULL - 1) * (TIER_CAP_MAX - TIER_CAP_MIN)

def fmt(v):
    if abs(v) >= 1e6: return f"{v/1e6:7.2f}M"
    if abs(v) >= 1e3: return f"{v/1e3:7.1f}K"
    return f"{v:8,.0f}"

def amm_swap_output(amount_in, reserve_in, reserve_out, fee_bps):
    if reserve_in <= 0 or reserve_out <= 0 or amount_in <= 0:
        return 0.0
    amount_with_fee = amount_in * (10000 - fee_bps) / 10000.0
    return reserve_out * amount_with_fee / (reserve_in + amount_with_fee)

def amm_spot(reserve_xfg, reserve_heat):
    if reserve_heat <= 0: return 1e9
    return reserve_xfg / reserve_heat

def amm_single_sided_lp(amount_xfg, amount_heat, total_lp, rsv_xfg, rsv_heat):
    if total_lp <= 0 or rsv_xfg <= 0 or rsv_heat <= 0:
        return 0.0
    if amount_xfg > 0:
        return total_lp * amount_xfg / rsv_xfg
    if amount_heat > 0:
        return total_lp * amount_heat / rsv_heat
    return 0.0

# ═══════════════════════════════════════════════════════════════
#  CD-BOOK
# ═══════════════════════════════════════════════════════════════

class CDBook:
    def __init__(self, maturity_sched_len):
        self.cds = []
        self.maturity = np.zeros(maturity_sched_len)
        self.total_locked = 0.0

    def create(self, amount, term_ep, creation_ep):
        cap = tier_cap(term_ep)
        self.cds.append({
            'amount': amount,
            'term': term_ep,
            'created': creation_ep,
            'cap': cap,
        })
        mat_ep = creation_ep + term_ep
        if mat_ep < len(self.maturity):
            self.maturity[int(mat_ep)] += amount
        self.total_locked += amount

    def step(self, ep):
        matured = self.maturity[ep]
        if matured > 0:
            self.total_locked -= matured
            self.cds = [c for c in self.cds if c['created'] + c['term'] != ep]
        return matured

    def compute_yield(self, target_rate, drip_rate):
        total = 0.0
        yields = []
        cap_hits = 0
        for c in self.cds:
            capped_rate = min(target_rate, c['cap'] / EPY)
            yld = c['amount'] * (capped_rate + drip_rate)
            yields.append(yld)
            total += yld
            if capped_rate < target_rate:
                cap_hits += 1
        return yields, total, cap_hits

    def get_apy_stats(self, target_rate, drip_rate):
        if not self.cds:
            return 0.0, 0.0
        apys = []
        for c in self.cds:
            capped_rate = min(target_rate, c['cap'] / EPY)
            apys.append((capped_rate + drip_rate) * EPY * 100)
        if not apys:
            return 0.0, 0.0
        return float(sum(apys) / len(apys)), float(min(apys))

# ═══════════════════════════════════════════════════════════════
#  BOND-BOOK
# ═══════════════════════════════════════════════════════════════

class BondBook:
    def __init__(self):
        self.bonds = []
        self.total_outstanding = 0.0
        self.migrations_remaining = INIT_BOND_POOL

    def migrate(self, amount, creation_ep, rate_bps):
        actual = min(amount, self.migrations_remaining)
        if actual <= 0: return 0.0
        self.migrations_remaining -= actual
        rate = min(rate_bps / 10000.0 / EPY, BOND_MAX_RATE / EPY)
        rate = max(rate, BOND_MIN_RATE / EPY)
        self.bonds.append({
            'principal': actual,
            'issued': creation_ep,
            'term': BOND_TERM_EPOCHS,
            'rate_per_epoch': rate,
            'repaid': False,
            'coupons_paid': 0,
        })
        self.total_outstanding += actual
        return actual

    def process_epoch(self, ep, swf_balance, yem_reserve):
        coupons = 0.0
        maturities = 0.0
        swf = swf_balance
        yem = yem_reserve

        for b in self.bonds:
            if b['repaid']:
                continue
            coupon = b['principal'] * b['rate_per_epoch']
            coupons += coupon
            b['coupons_paid'] += 1

            if ep >= b['issued'] + b['term']:
                owed = b['principal'] + coupon
                from_swf = min(owed, swf)
                swf -= from_swf
                from_yem = owed - from_swf
                yem -= from_yem
                maturities += b['principal']
                b['repaid'] = True
                self.total_outstanding -= b['principal']

        return swf, yem, coupons, maturities

# ═══════════════════════════════════════════════════════════════
#  DYNAMIC ALLOCATION CONTROLLER
# ═══════════════════════════════════════════════════════════════

class DynamicAllocator:
    """
    Adjusts allocation splits based on system state.
    Responds to hashrate deficit, peg deviation, LP shallowness, bond risk.
    """
    def __init__(self):
        # Base targets
        self.ef_target = 0.40
        self.treas_target = 0.10
        self.yem_target = 0.50
        self.premium_target = 0.02
        self.swf_lp_target = 0.75

    def update(self, state):
        h = state.get('hashrate_ratio', 1.0)
        p = state.get('peg_deviation', 0.0)
        d = state.get('lp_depth_ratio', 1.0)
        b = state.get('bond_ratio', 1.5)

        ef = 0.40
        treas = 0.10
        yem = 0.50
        prem = 0.02
        lp = 0.75

        if h < 0.8:
            boost = (1.0 - h) * 1.0
            ef = min(0.80, ef + boost)
            yem = max(0.10, yem - boost * 0.7)
            treas = max(0.05, treas - boost * 0.3)
        elif p > 0.15:
            boost = min(p * 2.0, 0.30)
            treas = min(0.50, treas + boost)
            yem = max(0.10, yem - boost * 0.6)
            ef = max(0.10, ef - boost * 0.4)
        elif d < 0.3:
            lp = min(0.95, lp + 0.15)
            prem = max(0.01, prem - 0.005)
        elif b < 1.2:
            yem = min(0.80, yem + 0.20)
            ef = max(0.10, ef - 0.10)
            treas = max(0.05, treas - 0.05)

        # Normalize EF + Treas + YEM = 1.0
        total = ef + treas + yem
        ef /= total
        treas /= total
        yem /= total

        self.ef_target = ef
        self.treas_target = treas
        self.yem_target = yem
        self.premium_target = prem
        self.swf_lp_target = lp

    def get(self):
        return {
            'premium': self.premium_target,
            'ef_share': self.ef_target,
            'treas_share': self.treas_target,
            'yem_share': self.yem_target,
            'swf_lp_share': self.swf_lp_target,
        }

# ═══════════════════════════════════════════════════════════════
#  CORE SIMULATION — single run
# ═══════════════════════════════════════════════════════════════

def run_sim(scenario_name, rng, alloc=None, dynamic=False, bough=False, clean_revenue=False, quiet=False):
    """
    alloc: dict with keys:
      'premium'      — total premium rate (0.01-0.05)
      'ef_share'     — fraction of premium → EF
      'treas_share'  — fraction of premium → Treasury
      'yem_share'    — fraction of premium → YEM
      'swf_lp_share' — fraction of SWF → LP (rest is liquid)
    
    If alloc is None, use defaults.
    If dynamic is True, alloc is overridden by DynamicAllocator each epoch.
    """
    cfg = scenario_config(scenario_name, rng)
    vp = cfg['vol_params']
    dp = cfg['demand_params']
    op = cfg['oracle_params']
    bp = cfg['bond_params']

    if alloc is None:
        alloc = {
            'premium': MINT_PREMIUM,
            'ef_share': EF_PREM_SHARE,
            'treas_share': TREAS_PREM_SHARE,
            'yem_share': YEM_PREM_SHARE,
            'swf_lp_share': SWF_LP_SHARE,
        }

    dyn = DynamicAllocator() if dynamic else None

    # ── State ──
    px = INIT_POOL_XFG
    t0 = HEAT_PEG / INIT_XFG_PRICE
    ph = px / max(t0, 1e-6)
    total_lp = px * 2
    protocol_lp = 0.0
    accumulated_lp_yield = 0.0

    treasury = INIT_TREASURY
    heat_supply = ph * 8
    cd_pool = 0.0
    swf = 0.0
    yem_reserve = 0.0
    eternal_flame = 0.0                # cumulative XFG destroyed via EF
    xfg_burned_lifetime = 0.0          # lifetime XFG → HEAT

    # YEM lag
    lag_counter = 0
    lag_complete = False
    rolling_rates = [0.0, 0.0, 0.0]
    rolling_count = 0

    # CD book
    cd_book = CDBook(N_EPOCHS + 500)
    n_seed = 400
    for _ in range(n_seed):
        amt = INIT_CD_LOCKED / n_seed * float(rng.random() * 0.5 + 0.75)
        dur = int(np.clip(rng.normal(CD_DUR_MEAN, CD_DUR_STD), 10, 150))
        cd_book.create(amt, dur, 0)

    # Bond book
    bond_book = BondBook()

    # Oracle
    oracle_price = 0.0
    last_oracle_price = 0.0
    oracle_stale_epochs = 0
    oracle_active = False

    # XFG price
    xfg_price = INIT_XFG_PRICE
    xfg_log_price = np.log(xfg_price)

    # Peg
    mint_price = HEAT_PEG / max(xfg_price, 0.01)

    # Risk state (for dynamic allocator)
    hashrate_ratio = 1.0    # simulated — based on EF total vs target

    # BOUGH state (if enabled)
    bough_supply = 0.0
    bough_price = BOUGH_INIT_PRICE
    bough_yield_accrued = 0.0
    amo_mint_events = 0
    amo_burn_events = 0
    last_amo_mint_epoch = -999

    # Clean-revenue state
    bond_premium = 0.0  # premium pooled to pay legacy bond interest
    premium_integral = 0.0  # PI controller integral term

    # ── Results storage ──
    rows = []

    for ep in range(N_EPOCHS):
        yr = ep // EPY
        if yr >= N_YEARS:
            break

        # ──────── 1. XFG PRICE ────────
        daily_ret = rng.normal(vp['drift'], vp['sigma'])
        xfg_log_price += daily_ret
        if rng.random() < vp['crash_lambda']:
            xfg_log_price += rng.normal(vp['crash_mu'], vp['crash_sigma'])
        if rng.random() < vp['jump_lambda']:
            xfg_log_price += rng.normal(0, vp['jump_sigma'])
        xfg_price = max(0.01, np.exp(xfg_log_price))
        xfg_price = min(xfg_price, 500.0)

        # ──────── 2. ORACLE ────────
        oracle_update = not oracle_active or rng.random() > op['staleness_prob']
        if oracle_update:
            oracle_price = xfg_price * float(1 + rng.normal(0, 0.01))
            last_oracle_price = oracle_price
            oracle_stale_epochs = 0
            oracle_active = True
        else:
            oracle_stale_epochs += 1
            oracle_price = last_oracle_price if last_oracle_price > 0 else xfg_price

        # ──────── 3. MINT PRICE ────────
        if oracle_price > 1e-6:
            mint_price = HEAT_PEG / oracle_price

        # ──────── 4. ALLOCATION (static or dynamic) ────────
        premium_rate = alloc['premium']
        ef_share = alloc['ef_share']
        treas_share = alloc['treas_share']
        yem_share = alloc['yem_share']
        swf_lp_share = alloc['swf_lp_share']

        if dynamic and lag_complete:
            state = {
                'hashrate_ratio': hashrate_ratio,
                'peg_deviation': abs(amm_spot(px, ph) * xfg_price - HEAT_PEG) / max(HEAT_PEG, EPS) if ph > 0 else 0.0,
                'lp_depth_ratio': protocol_lp / max(px, 1.0),
                'bond_ratio': (swf + yem_reserve) / max(bond_book.total_outstanding, 1.0),
            }
            dyn.update(state)
            dyn_alloc = dyn.get()
            premium_rate = dyn_alloc['premium']
            ef_share = dyn_alloc['ef_share']
            treas_share = dyn_alloc['treas_share']
            yem_share = dyn_alloc['yem_share']
            swf_lp_share = dyn_alloc['swf_lp_share']

        # Clean-revenue: dynamic premium gate (PI on peg deviation)
        if clean_revenue and lag_complete:
            heat_price_pi = amm_spot(px, ph) * oracle_price if ph > 0 else HEAT_PEG
            peg_error = (HEAT_PEG - heat_price_pi) / HEAT_PEG  # positive = HEAT below peg
            premium_integral += peg_error * PREMIUM_KI
            premium_integral = np.clip(premium_integral, -PREMIUM_MAX, PREMIUM_MAX)
            premium_rate = np.clip(peg_error * PREMIUM_KP + premium_integral, 0.0, PREMIUM_MAX)
        elif clean_revenue and not lag_complete:
            premium_rate = 0.0  # no gate during lag phase

        # ──────── 5. SWAP VOLUME & FEES ────────
        base_vol = 2500 * np.clip(xfg_price / 2.0, 0.3, 5.0) * dp['swap_vol_mult']
        # LP depth affects effective volume (deeper pool = less slippage = more volume)
        lp_depth_pct = protocol_lp / max(px + protocol_lp, 1.0) if total_lp > 0 else 0.0
        lp_efficiency = min(1.0, lp_depth_pct * 10)  # scales with LP share
        swap_volume = base_vol * float(np.exp(rng.normal(0, 0.3))) * (0.5 + 0.5 * lp_efficiency)
        swap_volume = max(0.0, swap_volume)
        if clean_revenue:
            # Regular swap fees → 100% LPs (no protocol revenue from regular AMM)
            # Atomic swap fees → 100% CD pool (the only CD yield source)
            atomic_vol = swap_volume * ATOMIC_SWAP_MULT * float(np.exp(rng.normal(0, 0.3)))
            atomic_vol = max(0.0, atomic_vol)
            atomic_fees = atomic_vol * ATOMIC_SWAP_FEE_RATE
            cd_pool += atomic_fees
        else:
            total_fees = swap_volume * SWAP_FEE_RATE
            cd_share = total_fees * CD_SHARE
            treasury_share_fees = total_fees * TREAS_SHARE
            direct_swf = total_fees * SWF_DIRECT
            treasury += treasury_share_fees
            swf += direct_swf
            cd_pool += cd_share

        # ──────── 6. LP TRADES ────────
        trade_vol = swap_volume * 0.03
        for _ in range(rng.randint(1, 5)):
            if px <= 0 or ph <= 0: break
            if rng.random() < 0.5:
                amt = min(trade_vol / 3, px * 0.01)
                out = amm_swap_output(amt, px, ph, HEARTH_FEE_BPS)
                if out > 0:
                    px += amt
                    ph -= out
                    accumulated_lp_yield += amt * HEARTH_FEE_BPS / 10000.0
            else:
                amt = min(trade_vol / 3, ph * 0.01)
                out = amm_swap_output(amt, ph, px, HEARTH_FEE_BPS)
                if out > 0:
                    ph += amt
                    px -= out
                    accumulated_lp_yield += out * HEARTH_FEE_BPS / 10000.0

        # ──────── 7. CD MATURITY ────────
        matured = cd_book.step(ep)

        # Spot price for demand modeling
        heat_price = amm_spot(px, ph) * xfg_price if ph > 0 else 0.0

        # ──────── 8. CD CREATION ────────
        heat_mcap = heat_supply * xfg_price * amm_spot(px, ph) if ph > 0 else heat_supply * xfg_price * 0.316
        base_create = max(
            INIT_CD_LOCKED * 0.005,
            heat_mcap * CD_LOCK_RATE * dp['cd_create_mult'] / EPY * 2.0
        )
        peg_dev_abs = abs(heat_price - HEAT_PEG) / HEAT_PEG if HEAT_PEG > 0 else 1.0
        confidence = max(0.05, 1.0 - peg_dev_abs * 5.0)
        new_cd_amount = base_create * confidence * float(rng.random() * 0.5 + 0.75)
        new_cd_amount = max(INIT_CD_LOCKED * 0.0005, new_cd_amount)
        if new_cd_amount > 0:
            dur = int(np.clip(rng.normal(CD_DUR_MEAN, CD_DUR_STD), 10, 150))
            cd_book.create(new_cd_amount, dur, ep)

        # ──────── 9. USER MINT (XFG → HEAT) — ONE-WAY ────────
        spot_ratio = amm_spot(px, ph) if ph > 0 else 0
        heat_price_usd = spot_ratio * xfg_price if ph > 0 else 0
        peg_deviation = (heat_price_usd - HEAT_PEG) / HEAT_PEG if HEAT_PEG > 0 else 0

        mint_volume = swap_volume * 0.05 * dp['mint_elasticity']
        if peg_deviation > 0.02:
            mint_xfg = mint_volume * min(peg_deviation * 10, 3.0)
        elif peg_deviation < -0.02:
            mint_xfg = 0.0
        else:
            mint_xfg = mint_volume * 0.1

        mint_xfg = max(0.0, mint_xfg)
        mint_xfg = min(mint_xfg, INIT_CD_LOCKED * 0.05)

        if mint_xfg > 0 and mint_price > 0:
            base = mint_xfg / (1.0 + premium_rate)
            premium = mint_xfg - base

            heat_minted = base / mint_price
            heat_supply += heat_minted
            xfg_burned_lifetime += mint_xfg

            if clean_revenue:
                # Premium → legacy bond interest
                bond_premium += premium
                # Base → 33/33/33 Treasury/Mining/SWF
                treasury += base * MINT_TREAS_SHARE
                eternal_flame += base * MINT_MINING_SHARE
                swf += base * MINT_SWF_SHARE
            else:
                # Allocate the base (value of minted HEAT) → SWF
                swf += base
                to_ef = premium * ef_share
                to_treasury = premium * treas_share
                to_yem = premium * yem_share
                eternal_flame += to_ef
                if lag_complete:
                    yem_reserve += to_yem
                treasury += to_treasury

            # User sells some minted HEAT to AMM pool
            if ph > 0 and heat_minted > 0:
                pool_add = rng.random() * heat_minted * 0.5
                if pool_add > 0 and px > 0:
                    ph += pool_add
                    xfg_taken = amm_swap_output(pool_add, ph - pool_add, px, HEARTH_FEE_BPS)
                    if 0 < xfg_taken < px:
                        px -= xfg_taken

        # ──────── NO USER BURN (one-way: no HEAT→XFG) ────────

        # ──────── 10. PROTOCOL ARBITRAGE (peg defense) ────────
        if oracle_stale_epochs < EPY * 2 and oracle_price > 0 and ph > 0 and px > 0:
            for _ in range(ARB_ITERS):
                spot_ratio = amm_spot(px, ph)
                peg_ratio = HEAT_PEG / max(oracle_price, 0.01)
                heat_price_usd = spot_ratio * oracle_price
                deviation = abs(heat_price_usd - HEAT_PEG) / HEAT_PEG
                if deviation < ARB_THRESHOLD:
                    break

                arb_amt = min(px, ph) * rng.random() * ARB_MAX_POOL

                if heat_price_usd > HEAT_PEG:
                    heat_to_mint = arb_amt / max(mint_price, 1e-9)
                    if heat_to_mint > 0 and ph > 0:
                        # Protocol mints HEAT (paying no premium — it's arb)
                        ph += heat_to_mint
                        out_xfg = amm_swap_output(heat_to_mint, ph - heat_to_mint, px, HEARTH_FEE_BPS)
                        if 0 < out_xfg < px:
                            px -= out_xfg
                            treasury += out_xfg
                            heat_supply += heat_to_mint
                            # The base cost goes back to SWF
                            base_cost = heat_to_mint * mint_price
                            swf += base_cost * (1 - premium_rate)
                else:
                    # HEAT cheap: protocol buys from pool, holds (no burn in one-way)
                    # Larger treasury = more aggressive peg defense
                    trea_buy_cap = treasury * 0.005 * min(1.0, treasury / max(heat_supply * mint_price, 1.0) * 10)
                    arb_buy_power = min(arb_amt * 0.25, trea_buy_cap)
                    if treasury > arb_buy_power and ph > 0:
                        heat_from_pool = amm_swap_output(arb_buy_power, px, ph, HEARTH_FEE_BPS)
                        if 0 < heat_from_pool < ph:
                            px += arb_buy_power
                            ph -= heat_from_pool
                            treasury -= arb_buy_power
                            # Protocol holds HEAT, does NOT burn it (one-way)
                            # Could sell later when price recovers

        # ──────── 11. SWF DEPLOYMENT ────────
        # SWF deploys its balance into LP positions
        swf_lp_deployed = swf * swf_lp_share
        swf_liquid = swf * (1.0 - swf_lp_share)

        # Deploy SWF LP capital into AMM
        if ep > 0 and ep % EPY == 0:  # yearly LP deployment
            if swf_lp_deployed > 0 and px > 0 and ph > 0 and total_lp > 0:
                deploy_xfg = min(swf_lp_deployed * 0.1, px * 0.05)  # gradual: 10% of SWF LP per year
                if deploy_xfg > 0:
                    shares = amm_single_sided_lp(deploy_xfg, 0, total_lp, px, ph)
                    if shares > 0:
                        px += deploy_xfg
                        total_lp += shares
                        protocol_lp += shares
                        swf -= deploy_xfg

            # LP yield feed
            if protocol_lp > 0 and total_lp > 0 and accumulated_lp_yield > 0:
                lp_yield_share = accumulated_lp_yield * (protocol_lp / total_lp)
                if clean_revenue:
                    swf += lp_yield_share
                else:
                    swf += lp_yield_share * LP_YIELD_FEED
                    treasury += lp_yield_share * (1.0 - LP_YIELD_FEED)
                accumulated_lp_yield = 0.0

            # Bond migrations (yearly)
            if ep >= bp['start_epoch'] * EPY and bond_book.migrations_remaining > 0:
                mig_amt = min(
                    bond_book.migrations_remaining * bp['migration_speed'],
                    cd_book.total_locked * 0.005
                )
                org_rate = cd_pool / max(cd_book.total_locked, 1.0) if cd_book.total_locked > 0 else BOND_MIN_RATE
                bond_rate_bps = int(np.clip(org_rate * BOND_RATE_MULT * 10000, BOND_MIN_RATE * 10000, BOND_MAX_RATE * 10000))
                principal = bond_book.migrate(mig_amt, ep, bond_rate_bps)
                swf += principal

            # Bond coupons + maturities
            swf, yem_reserve, coupons_paid, maturities_paid = bond_book.process_epoch(ep, swf, yem_reserve)
            # Clean-revenue: reimburse SWF from premium collected for bond interest
            if clean_revenue and bond_premium > 0 and (coupons_paid + maturities_paid) > 0:
                from_premium = min(bond_premium, coupons_paid + maturities_paid)
                bond_premium -= from_premium
                swf += from_premium

        # ──────── 11b. BOUGH AMO ────────
        if bough and lag_complete:
            heat_price_amo = amm_spot(px, ph) * oracle_price if ph > 0 else 0
            peg_ratio_amo = heat_price_amo / HEAT_PEG if HEAT_PEG > 0 else 1.0
            heat_mcap_amo = heat_supply * mint_price if mint_price > 0 else 1.0
            treasury_ratio_amo = treasury / max(heat_mcap_amo, 1.0)

            bough_theoretical_mcap = swf / BOUGH_SUPPLY_CAP_RATIO
            bough_intrinsic = bough_theoretical_mcap / max(bough_supply, 1.0) if bough_supply > 0 else BOUGH_INIT_PRICE
            peg_sentiment = max(0.1, min(2.0, peg_ratio_amo))
            bough_price = bough_intrinsic * (0.5 + 0.5 * peg_sentiment) * (0.97 + 0.06 * rng.random())
            bough_price = max(bough_price, BOUGH_PRICE_FLOOR)

            if (
                peg_ratio_amo < BOUGH_AMO_MINT_THRESHOLD
                and treasury_ratio_amo < BOUGH_TREAS_RATIO_TARGET
                and bough_price > BOUGH_PRICE_FLOOR * 1.01
                and ep - last_amo_mint_epoch >= BOUGH_MINT_COOLDOWN
            ):
                treasury_deficit = max(0.0, BOUGH_TREAS_RATIO_TARGET * heat_mcap_amo - treasury)
                bough_current_mcap = bough_supply * bough_price
                bough_room_xfg = max(0.0, bough_theoretical_mcap - bough_current_mcap)
                # Only mint if there is room under the supply cap
                bough_to_mint = 0.0
                if bough_room_xfg > 0 and bough_price > 0.01:
                    max_from_deficit = treasury_deficit / bough_price
                    max_from_room = bough_room_xfg / bough_price
                    max_from_market = bough_current_mcap * 0.10 / bough_price  # 10% of existing mcap
                    bough_to_mint = min(
                        max_from_deficit,
                        max_from_room,
                        max_from_market if bough_supply > 0.1 else 100.0,
                    )
                    if bough_to_mint > 0.1:
                        xfg_raised = bough_to_mint * bough_price * BOUGH_MINT_DISCOUNT
                        treasury += xfg_raised
                        bough_supply += bough_to_mint
                        last_amo_mint_epoch = ep
                        amo_mint_events += 1
                        heat_buy = min(xfg_raised * BOUGH_PEG_BUY_FRAC, treasury * 0.3)
                        if heat_buy > 0 and ph > 0 and px > 0:
                            heat_bought = amm_swap_output(heat_buy, px, ph, HEARTH_FEE_BPS)
                            if 0 < heat_bought < ph * 0.95:
                                px += heat_buy
                                ph -= heat_bought
                                treasury -= heat_buy
                        bough_price *= bough_supply / (bough_supply + bough_to_mint)
                        bough_price = max(bough_price, BOUGH_PRICE_FLOOR)

            elif peg_ratio_amo > BOUGH_AMO_BURN_THRESHOLD and bough_supply > 0.1:
                surplus = max(0.0, treasury - BOUGH_TREAS_RATIO_TARGET * heat_mcap_amo)
                bough_to_burn = min(
                    surplus / max(bough_price, 0.01) if bough_price > 0 else 0.0,
                    bough_supply * BOUGH_MAX_BURN_PCT,
                )
                if bough_to_burn > 0.1:
                    cost = bough_to_burn * bough_price
                    treasury -= cost
                    bough_supply -= bough_to_burn
                    amo_burn_events += 1
                    if bough_supply > 0:
                        bough_price *= bough_supply / (bough_supply + bough_to_burn)

        # ──────── 12. YEM SMOOTHING ────────
        deployable_swf = 0.0
        drip_rate = 0.0
        if not lag_complete:
            lag_counter += 1
            swf += cd_pool
            cd_pool = 0.0
            if lag_counter >= YEM_LAG_EPOCHS:
                lag_complete = True
        else:
            organic_rate = cd_pool / max(cd_book.total_locked, 1.0) if cd_book.total_locked > 0 else 0.0
            rolling_rates[rolling_count % 3] = organic_rate
            rolling_count += 1
            target_rate = np.mean(rolling_rates[:max(rolling_count, 1)]) if rolling_count > 0 else 0.0

            # SWF drip: rate based on liquid portion of SWF
            deployable_swf = swf * (1.0 - swf_lp_share)  # only liquid SWF drips
            if clean_revenue:
                # In clean-revenue: SWF drip → 45/45/10 Mining/SWF/Treasury
                # CD yield comes from atomic swap fees only
                deployable_drip = deployable_swf * YEM_SWF_DRIP
                if bough and bough_supply > 0.1:
                    bough_drip_yield = deployable_swf * YEM_SWF_DRIP * BOUGH_YIELD_SHARE
                    bough_yield_accrued += bough_drip_yield
                    bough_drip_remain = deployable_drip - bough_drip_yield
                else:
                    bough_drip_remain = deployable_drip
                eternal_flame += bough_drip_remain * DRIP_MINING_SHARE
                swf += bough_drip_remain * DRIP_SWF_SHARE
                treasury += bough_drip_remain * DRIP_TREAS_SHARE
                drip_rate = 0.0  # CDs get nothing from SWF drip
            else:
                effective_drip_rate = YEM_SWF_DRIP
                if bough and bough_supply > 0.1:
                    bough_drip_yield = deployable_swf * YEM_SWF_DRIP * BOUGH_YIELD_SHARE
                    bough_yield_accrued += bough_drip_yield
                    effective_drip_rate = YEM_SWF_DRIP * (1.0 - BOUGH_YIELD_SHARE)
                drip_rate = (deployable_swf * effective_drip_rate / max(cd_book.total_locked, 1.0)) if cd_book.total_locked > 0 else 0.0

            yields, total_yield, cap_hits = cd_book.compute_yield(target_rate, drip_rate)

            surplus = cd_pool - total_yield
            if surplus > 0:
                swf += surplus * YEM_SWF_SAVE
            elif surplus < 0:
                deficit = -surplus
                from_swf = min(deficit, swf * 0.5)  # use at most 50% of SWF liquid
                swf -= from_swf
                remaining = deficit - from_swf
                if remaining > 0:
                    from_reserve = min(remaining, yem_reserve)
                    yem_reserve -= from_reserve
                    remaining -= from_reserve
                    if remaining > 0:
                        backstop = treasury * YEM_TREAS_BACKSTOP
                        treasury -= min(remaining, backstop)

            # Execute CD yield
            if total_yield > 0 and ph > 0 and px > 0:
                cd_spend = total_yield
                heat_bought = amm_swap_output(cd_spend, px, ph, HEARTH_FEE_BPS)
                if 0 < heat_bought < ph * 0.95:
                    px += cd_spend
                    ph -= heat_bought
                    heat_supply += heat_bought

            cd_pool = 0.0

        # ──────── 13. HASHRATE MODEL (simplified) ────────
        # Hashrate ratio = f(total EF, time) — simplified as:
        # More EF → lower Osavvirsak → higher baseReward → more hashrate
        total_ef_ratio = eternal_flame / max(xfg_burned_lifetime, 1.0) if xfg_burned_lifetime > 0 else 0.0
        time_factor = min(1.0, (ep + 1) / (EPY * 3))  # ramps up over 3 years
        hashrate_ratio = min(1.5, total_ef_ratio * time_factor * 5.0)
        # Bound: if EF is 0, some base hashrate from mining alone
        hashrate_ratio = max(0.3, hashrate_ratio)

        # ──────── 14. RECORD METRICS ────────
        apy_mean, apy_min = cd_book.get_apy_stats(
            np.mean(rolling_rates[:max(rolling_count, 1)]) if rolling_count > 0 else 0.0 if lag_complete else 0.0,
            drip_rate if lag_complete else 0.0,
        )
        heat_price = amm_spot(px, ph) * xfg_price if ph > 0 else 0.0
        peg_stable = abs(heat_price - HEAT_PEG) / HEAT_PEG if HEAT_PEG > 0 else 0.0

        row = {
            'epoch': ep, 'year': yr,
            'xfg_price': xfg_price,
            'oracle_price': oracle_price,
            'mint_price': mint_price,
            'heat_price': heat_price,
            'peg_deviation': peg_stable,
            'swap_volume': swap_volume,
            'total_fees': total_fees if not clean_revenue else 0.0,
            'cd_pool': cd_pool,
            'swf_balance': swf,
            'swf_lp': swf * swf_lp_share if lag_complete else 0.0,
            'swf_liquid': swf * (1.0 - swf_lp_share) if lag_complete else 0.0,
            'yem_reserve': yem_reserve,
            'treasury': treasury,
            'eternal_flame': eternal_flame,
            'heat_supply': heat_supply,
            'pool_xfg': px,
            'pool_heat': ph,
            'protocol_lp': protocol_lp,
            'cd_locked': cd_book.total_locked,
            'cd_apy_mean': apy_mean,
            'cd_apy_min': apy_min,
            'bonds_outstanding': bond_book.total_outstanding,
            'bond_migrations_remaining': bond_book.migrations_remaining,
            'xfg_burned': xfg_burned_lifetime,
            'lag_complete': int(lag_complete),
            'organic_rate': rolling_rates[(rolling_count - 1) % 3] if rolling_count > 0 else 0.0,
            'rolling_avg_rate': np.mean(rolling_rates[:max(rolling_count, 1)]) if rolling_count > 0 else 0.0,
            'hashrate_ratio': hashrate_ratio,
            'bough_supply': bough_supply,
            'bough_price': bough_price,
            'bough_yield_accrued': bough_yield_accrued,
            'amo_mint_events': amo_mint_events,
            'amo_burn_events': amo_burn_events,
            'solvent': int(swf >= 0 and yem_reserve >= 0 and treasury >= 0),
            'premium_rate': premium_rate,
            'ef_alloc_share': ef_share,
            'treas_alloc_share': treas_share,
            'yem_alloc_share': yem_share,
            'swf_lp_share': swf_lp_share,
        }
        rows.append(row)

    # Final bond processing (handle maturities at N_EPOCHS boundary)
    swf, yem_reserve, _, _ = bond_book.process_epoch(N_EPOCHS, swf, yem_reserve)

    # Final bond check
    all_matured_repaid = True
    for b in bond_book.bonds:
        if b['issued'] + b['term'] < N_EPOCHS and not b['repaid']:
            all_matured_repaid = False
            break
    bond_ok = 1.0 if all_matured_repaid else 0.0

    # ── Results ──
    post_lag = [r for r in rows if r['lag_complete']]
    post_bootstrap = post_lag[int(len(post_lag) * 0.3):] if len(post_lag) > 0 else rows

    def mean_or_zero(vals):
        clean = [v for v in vals if not np.isnan(v) and not np.isinf(v)]
        return float(sum(clean) / len(clean)) if clean else 0.0

    def min_or_zero(vals):
        clean = [v for v in vals if not np.isnan(v) and not np.isinf(v)]
        return float(min(clean)) if clean else 0.0

    return {
        'scenario': scenario_name,
        'alloc_label': f"prem={alloc['premium']:.2f}_ef={alloc['ef_share']:.2f}_tr={alloc['treas_share']:.2f}_ym={alloc['yem_share']:.2f}_lp={alloc['swf_lp_share']:.2f}",
        'alloc': {k: float(v) for k, v in alloc.items()},
        'metrics': {
            'mean_cd_apy': mean_or_zero([r['cd_apy_mean'] for r in post_lag]),
            'worst_cd_apy': min_or_zero([r['cd_apy_min'] for r in post_lag]),
            'peg_healthy_pct': mean_or_zero([1.0 if r['peg_deviation'] < 0.20 else 0.0 for r in post_bootstrap]) * 100,
            'peg_moderate_pct': mean_or_zero([1.0 if r['peg_deviation'] < 0.50 else 0.0 for r in post_bootstrap]) * 100,
            'swf_never_neg': 1.0 if all(r['swf_balance'] >= 0 for r in rows) else 0.0,
            'yem_never_neg': 1.0 if all(r['yem_reserve'] >= 0 for r in rows) else 0.0,
            'treasury_never_neg': mean_or_zero([1.0 if r['treasury'] >= 0 else 0.0 for r in rows]) * 100,
            'lag_completed': 1.0 if any(r['lag_complete'] for r in rows) else 0.0,
            'solvent_epochs_pct': mean_or_zero([r['solvent'] for r in rows]) * 100,
            'bond_obligations_met': bond_ok,
            'final_swf': rows[-1]['swf_balance'] if rows else 0.0,
            'final_yem': rows[-1]['yem_reserve'] if rows else 0.0,
            'final_treasury': rows[-1]['treasury'] if rows else 0.0,
            'final_heat_supply': rows[-1]['heat_supply'] if rows else 0.0,
            'final_cd_locked': rows[-1]['cd_locked'] if rows else 0.0,
            'final_eternal_flame': rows[-1]['eternal_flame'] if rows else 0.0,
            'max_peg_deviation': max(r['peg_deviation'] for r in rows) * 100 if rows else 0.0,
            'mean_hashrate': mean_or_zero([r['hashrate_ratio'] for r in post_lag]),
            'final_protocol_lp': rows[-1]['protocol_lp'] if rows else 0.0,
        },
        'rows': rows,
    }

# ═══════════════════════════════════════════════════════════════
#  SCENARIO CONFIG
# ═══════════════════════════════════════════════════════════════

def scenario_config(name, prng):
    base = {
        'vol_params': {
            'drift': 0.0008, 'sigma': 0.035,
            'crash_lambda': 0.01, 'crash_mu': -0.20, 'crash_sigma': 0.08,
            'jump_lambda': 0.03, 'jump_sigma': 0.30,
        },
        'demand_params': {
            'cd_create_mult': 1.0, 'swap_vol_mult': 1.0, 'mint_elasticity': 1.0,
        },
        'oracle_params': {
            'staleness_prob': 0.02, 'swapxfg_stale_prob': 0.05, 'tier2_min_trades': 5,
        },
        'bond_params': {
            'migration_speed': 0.10, 'start_epoch': 3,
        },
    }
    configs = {
        'baseline': base,
        'bear_market': {**base, 'vol_params': {**base['vol_params'], 'drift': -0.0002, 'sigma': 0.06, 'crash_lambda': 0.04}},
        'sideways_volatile': {**base, 'vol_params': {**base['vol_params'], 'drift': 0.0, 'sigma': 0.08, 'jump_lambda': 0.10}},
        'volume_drought': {**base, 'demand_params': {**base['demand_params'], 'swap_vol_mult': 0.10}},
        'bond_wave': {**base, 'bond_params': {**base['bond_params'], 'migration_speed': 0.30}},
        'oracle_failure': {**base, 'oracle_params': {**base['oracle_params'], 'staleness_prob': 0.10}},
        'extreme_crash': {**base, 'vol_params': {**base['vol_params'], 'sigma': 0.06, 'crash_lambda': 0.08, 'crash_mu': -0.60, 'crash_sigma': 0.25}},
        'full': base,
    }
    return configs.get(name, base)

# ═══════════════════════════════════════════════════════════════
#  BATCH RUNNER
# ═══════════════════════════════════════════════════════════════

def run_single_scenario(scenario_name, n_sims, alloc=None, dynamic=False, bough=False, clean_revenue=False, quiet=False):
    t0 = time.time()
    results = []
    for s in range(n_sims):
        rng = np.random.RandomState(SEED_BASE + hash(scenario_name + str(s)) % (2**30))
        res = run_sim(scenario_name, rng, alloc=alloc, dynamic=dynamic, bough=bough, clean_revenue=clean_revenue, quiet=quiet)
        results.append(res)
        if not quiet and (s + 1) % max(1, n_sims // 5) == 0:
            sys.stdout.write(f"\r    {s+1}/{n_sims}"); sys.stdout.flush()
    dt = time.time() - t0
    if not quiet:
        sys.stdout.write(f"\r    {n_sims}/{n_sims} ({dt:.0f}s, {dt/n_sims:.1f}s/run)\n")
    return results

def run_grid(scenario_name, n_sims, quiet=False):
    """Run a grid of allocation combinations."""
    all_results = []
    total_cells = len(GRID_PREMIUMS) * len(GRID_EF_SHARES) * len(GRID_TREAS_SHARES) * len(GRID_SWF_LP_SHARES)
    cell = 0

    for prem, ef, treas, lp in product(GRID_PREMIUMS, GRID_EF_SHARES, GRID_TREAS_SHARES, GRID_SWF_LP_SHARES):
        cell += 1
        yem = max(0.0, 1.0 - ef - treas)
        alloc = {
            'premium': prem,
            'ef_share': ef,
            'treas_share': treas,
            'yem_share': yem,
            'swf_lp_share': lp,
        }
        label = f"p{prem:.2f}_e{ef:.2f}_t{treas:.2f}_l{lp:.2f}"
        if not quiet:
            print(f"  [{cell}/{total_cells}] {label}")

        for s in range(n_sims):
            rng = np.random.RandomState(SEED_BASE + hash(f"{scenario_name}_grid_{cell}_{s}") % (2**30))
            res = run_sim(scenario_name, rng, alloc=alloc, dynamic=False, quiet=True)
            all_results.append(res)

    return all_results

# ═══════════════════════════════════════════════════════════════
#  OUTPUT
# ═══════════════════════════════════════════════════════════════

def write_summary(results, output_dir):
    os.makedirs(output_dir, exist_ok=True)
    summary = {'scenarios': {}}

    for scenario_name in sorted(set(r['scenario'] for r in results)):
        sc = [r for r in results if r['scenario'] == scenario_name]
        # Group by alloc label
        by_alloc = defaultdict(list)
        for r in sc:
            by_alloc[r['alloc_label']].append(r)

        for label, runs in by_alloc.items():
            alloc_info = runs[0]['alloc'] if runs else {}
            med_metrics = {}
            if runs:
                for k in runs[0]['metrics']:
                    vals = [r['metrics'][k] for r in runs]
                    med_metrics[k] = float(np.median(vals))
            entry = {
                'n_runs': len(runs),
                'alloc': alloc_info,
                'metrics': med_metrics,
            }
            summary['scenarios'][f"{scenario_name}/{label}"] = entry

    path = os.path.join(output_dir, 'summary.json')
    with open(path, 'w') as f:
        json.dump(summary, f, indent=2)
    print(f"  Summary: {path}")

def print_table(results):
    scenario_names = sorted(set(r['scenario'] for r in results))
    header = ['SCENARIO/ALLOC', 'APY_μ', 'Peg<20%', 'Swf≥0', 'Solv%', 'BondOK', 'EF_f', 'SWF_f', 'TRE_f']
    print(f"\n{'─'*140}")
    print(f"  {' '.join(f'{h:>12}' for h in header)}")
    print(f"{'─'*140}")

    for sn in scenario_names:
        sc = [r for r in results if r['scenario'] == sn]
        by_alloc = defaultdict(list)
        for r in sc:
            by_alloc[r['alloc_label']].append(r)

        for label in sorted(by_alloc.keys()):
            runs = by_alloc[label]
            m = runs[0]['metrics']
            for k in runs[0]['metrics']:
                vals = [r['metrics'][k] for r in runs]
                m[k] = float(np.median(vals))
            print(f"  {sn[:10]}/{label[:20]:20s} {m['mean_cd_apy']:>11.2f}% "
                  f"{m['peg_healthy_pct']:>9.0f}% "
                  f"{m['swf_never_neg']:>6.0f} "
                  f"{m['solvent_epochs_pct']:>6.0f}% "
                  f"{m['bond_obligations_met']:>6.0f} "
                  f"{fmt(m['final_eternal_flame']):>8} "
                  f"{fmt(m['final_swf']):>8} "
                  f"{fmt(m['final_treasury']):>8}")
    print(f"{'─'*140}")

# ═══════════════════════════════════════════════════════════════
#  MAIN
# ═══════════════════════════════════════════════════════════════

def main():
    global BOUGH_YIELD_SHARE, BOUGH_SUPPLY_CAP_RATIO, BOUGH_AMO_MINT_THRESHOLD
    parser = argparse.ArgumentParser(description='YEM v3 One-Way HEAT — Capital Allocation Optimizer')
    parser.add_argument('--scenario', type=str, default=None, help='Run specific scenario')
    parser.add_argument('--runs', type=int, default=100, help='Monte Carlo runs per config')
    parser.add_argument('--output', type=str, default='sim_oneway_out/', help='Output directory')
    parser.add_argument('--no-plots', action='store_true', help='Skip plot generation')
    parser.add_argument('--quiet', action='store_true', help='Suppress progress')
    parser.add_argument('--grid', action='store_true', help='Run grid search over allocation splits')
    parser.add_argument('--dynamic', action='store_true', help='Use dynamic allocation controller')
    parser.add_argument('--bough', action='store_true', help='Enable BOUGH third coin (AMO peg defense + yield)')
    parser.add_argument('--bough-yield', type=float, default=None, help='BOUGH yield share (default: 0.10)')
    parser.add_argument('--bough-cap', type=float, default=None, help='BOUGH supply cap ratio SWF/N (default: 8)')
    parser.add_argument('--bough-threshold', type=float, default=None, help='AMO mint peg threshold (default: 0.97)')
    parser.add_argument('--clean-revenue', action='store_true', help='Clean-revenue mode: banking fees→infra, premium mining share→bonds first')
    parser.add_argument('--split', type=str, default=None,
                        help='Custom split: "prem=0.02,ef=0.40,tr=0.10,ym=0.50,lp=0.75"')
    args = parser.parse_args()

    scenarios = ['baseline', 'bear_market', 'sideways_volatile', 'volume_drought',
                  'bond_wave', 'oracle_failure', 'extreme_crash', 'full']
    if args.scenario:
        scenarios = [args.scenario]

    custom_alloc = None
    if args.split:
        parts = dict(kv.split('=') for kv in args.split.split(','))
        custom_alloc = {
            'premium': float(parts.get('prem', MINT_PREMIUM)),
            'ef_share': float(parts.get('ef', EF_PREM_SHARE)),
            'treas_share': float(parts.get('tr', TREAS_PREM_SHARE)),
            'yem_share': float(parts.get('ym', YEM_PREM_SHARE)),
            'swf_lp_share': float(parts.get('lp', SWF_LP_SHARE)),
        }

    n_runs = args.runs

    # Override BOUGH parameters from CLI
    if not args.bough:
        args.bough_yield = None
    if args.bough_yield is not None:
        BOUGH_YIELD_SHARE = args.bough_yield
    if args.bough_cap is not None:
        BOUGH_SUPPLY_CAP_RATIO = args.bough_cap
    if args.bough_threshold is not None:
        BOUGH_AMO_MINT_THRESHOLD = args.bough_threshold

    print(f"\n{'═'*140}")
    print(f"  YEM v3 One-Way HEAT — Capital Allocation Optimizer")
    mode = "grid search" if args.grid else ("dynamic" if args.dynamic else "single split")
    mode_label = ""
    if args.bough:
        bp = f"yld={BOUGH_YIELD_SHARE:.2f} cap={BOUGH_SUPPLY_CAP_RATIO} thr={BOUGH_AMO_MINT_THRESHOLD:.2f}"
        mode_label = f" [BOUGH {bp}]"
    if args.clean_revenue:
        mode_label += " [CLEAN-REVENUE]"
    print(f"  Mode: {mode}{mode_label}  |  {len(scenarios)} scenario(s) × {n_runs} runs × {N_EPOCHS} epochs ({N_YEARS}yr)")

    if custom_alloc:
        a = custom_alloc
        assert abs(a['ef_share'] + a['treas_share'] + a['yem_share'] - 1.0) < 0.001, \
            f"EF ({a['ef_share']}) + Treas ({a['treas_share']}) + YEM ({a['yem_share']}) must sum to 1.0"
        print(f"  Premium: {a['premium']*100:.1f}%  |  EF: {a['ef_share']*100:.0f}%  |"
              f"  Treasury: {a['treas_share']*100:.0f}%  |  YEM: {a['yem_share']*100:.0f}%  |"
              f"  SWF LP: {a['swf_lp_share']*100:.0f}%")
    elif args.grid:
        cells = len(GRID_PREMIUMS) * len(GRID_EF_SHARES) * len(GRID_TREAS_SHARES) * len(GRID_SWF_LP_SHARES)
        print(f"  Grid: {cells} alloc combinations × {n_runs} runs = {cells * n_runs * len(scenarios)} total")
        print(f"  Premiums: {GRID_PREMIUMS}  |  EF: {GRID_EF_SHARES}  |  Treas: {GRID_TREAS_SHARES}  |  LP: {GRID_SWF_LP_SHARES}")

    print(f"{'═'*140}")

    all_results = []

    for sn in scenarios:
        print(f"\n  {sn}...")
        if args.grid:
            sc_results = run_grid(sn, n_runs, quiet=args.quiet)
        else:
            sc_results = run_single_scenario(sn, n_runs, alloc=custom_alloc, dynamic=args.dynamic, bough=args.bough, clean_revenue=args.clean_revenue, quiet=args.quiet)
        all_results.extend(sc_results)

    if not args.grid and not args.dynamic and not custom_alloc:
        # Run default split for comparison
        pass

    print_table(all_results)

    if args.output:
        print(f"\n  Writing output to {args.output}/")
        write_summary(all_results, args.output)
        # CSV for the median run per scenario
        os.makedirs(args.output, exist_ok=True)
        for sn in set(r['scenario'] for r in all_results):
            sc = [r for r in all_results if r['scenario'] == sn]
            by_alloc = defaultdict(list)
            for r in sc:
                by_alloc[r['alloc_label']].append(r)
            for label, runs in by_alloc.items():
                runs.sort(key=lambda r: r['metrics']['mean_cd_apy'])
                median_run = runs[len(runs) // 2]
                path = os.path.join(args.output, f"{sn}_{label}.csv")
                if median_run['rows']:
                    with open(path, 'w', newline='') as f:
                        w = csv.DictWriter(f, fieldnames=median_run['rows'][0].keys())
                        w.writeheader()
                        w.writerows(median_run['rows'])

        if not args.no_plots:
            try:
                from generate_plots import generate_oneway_plots
                generate_oneway_plots(all_results, args.output)
            except ImportError:
                print("  Plot module not available, skipping")

    print(f"\n  Done. {'─'*120}")

if __name__ == '__main__':
    main()
