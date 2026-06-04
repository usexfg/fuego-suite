#!/usr/bin/env python3
"""
YEM v3 + Mode 4 — Full System Monte Carlo
==========================================
8 scenario profiles, 15 acceptance criteria, 2000 runs/scenario.

Mode 4: fixed-peg flatcoin  HEAT_PEG = 1.58 / oracle_xfg_price
YEM v3: single CD type, SWF smoothing, burn scalp, legacy-only bonds

Usage:  python scripts/yem_v3_sim.py                        # all scenarios
        python scripts/yem_v3_sim.py --baseline              # baseline only
        python scripts/yem_v3_sim.py --runs 100              # override run count
        python scripts/yem_v3_sim.py --output sim_out/       # output dir
"""

import numpy as np, time, sys, os, json, csv, argparse
from collections import defaultdict

# ═══════════════════════════════════════════════════════════════
#  CONFIG  — matches CryptoNoteConfig.h semantics
# ═══════════════════════════════════════════════════════════════

EPY = 73                     # epochs per year
N_EPOCHS = EPY * 12           # 12-year simulation window
N_YEARS = 12
HEAT_PEG = 1.58               # $1.58 in Dec 2008 dollars (BLS CPI-U)
HEAT_PEG_SCALE = 100          # peg index scale
SWAP_FEE_RATE = 0.01          # 1% swap fee (pool-level matching real HEARTH_FEE_BPS conceptually)
HEARTH_FEE_BPS = 30           # 0.3% AMM swap fee

# Fee split (Mode 4)
CD_SHARE = 0.80
TREAS_SHARE = 0.15            # peg defense + LP bootstrapping
SWF_DIRECT = 0.05             # direct SWF feed every epoch

# YEM core
YEM_LAG_EPOCHS = 3
YEM_ROLLING_WIN = 3
YEM_SWF_SAVE = 0.60
YEM_SWF_DRIP = 0.005            # 0.5%/epoch of SWF → CD holders (was 1%)
YEM_BURN_SCALP = 0.08          # 8% of mint premium → YEM Reserve
YEM_TREAS_BACKSTOP = 0.001     # 0.1% of treasury / epoch (floor)

# Time-tiered caps (annualized)
TIER_CAP_MIN = 0.33
TIER_CAP_MAX = 0.80
TIER_FULL = 72

# Bond
BOND_MAX_RATE = 0.25           # 25%/yr
BOND_MIN_RATE = 0.03           # 3%/yr floor
BOND_RATE_MULT = 1.5           # 1.5 × organic rate
BOND_TERM_EPOCHS = 72          # 1 year

# Mint / burn premiums (symmetric)
MINT_PREMIUM = 0.05
BURN_PREMIUM = 0.05

# Treasury LP
TREAS_LP_CONTRIB = 0.05        # 5% treasury → LP per epoch
LP_YIELD_FEED = 0.75           # 75% LP yield → SWF

# Pool / market
ARB_THRESHOLD = 0.01            # 1% deviation triggers arb (was 0.5%)
ARB_MAX_POOL = 0.01             # max 1% of pool per arb round
ARB_ITERS = 5                   # arb iterations per epoch

# Initial state (in XFG/HEAT, human-readable)
INIT_POOL_XFG = 5000
INIT_CD_LOCKED = 80000               # 80K XFG in CDs
INIT_TREASURY  = 30000               # 30K XFG treasury
INIT_BOND_POOL = 50000               # 50K XFG in pre-v10 COLD available
INIT_XFG_PRICE = 3.50                # start price

# CD demand (stochastic)
CD_LOCK_RATE = 0.08             # 8% annual CD creation pace
CD_DUR_MEAN = 60                # mean lock duration (epochs)
CD_DUR_STD = 20

# Output
SEED_BASE = 20260601

EPS = 1e-9

# ═══════════════════════════════════════════════════════════════
#  HELPERS
# ═══════════════════════════════════════════════════════════════

def tier_cap(duration_epochs):
    """cap(d) = 33% + (min(d,72)-1)/71 × 47%"""
    d = min(max(duration_epochs, 1), TIER_FULL)
    return TIER_CAP_MIN + (d - 1) / (TIER_FULL - 1) * (TIER_CAP_MAX - TIER_CAP_MIN)

def fmt(v):
    """Human-readable number formatting"""
    if abs(v) >= 1e6: return f"{v/1e6:7.2f}M"
    if abs(v) >= 1e3: return f"{v/1e3:7.1f}K"
    return f"{v:8,.0f}"

def amm_swap_output(amount_in, reserve_in, reserve_out, fee_bps):
    """Constant-product: how much of reserve_out you get for amount_in"""
    if reserve_in <= 0 or reserve_out <= 0 or amount_in <= 0:
        return 0.0
    amount_with_fee = amount_in * (10000 - fee_bps) / 10000.0
    return reserve_out * amount_with_fee / (reserve_in + amount_with_fee)

def amm_spot(reserve_xfg, reserve_heat):
    """Spot: XFG per 1 HEAT"""
    if reserve_heat <= 0: return 1e9
    return reserve_xfg / reserve_heat

def amm_single_sided_lp(amount_xfg, amount_heat, total_lp, rsv_xfg, rsv_heat):
    """LP shares for single-sided deposit (proportional)"""
    if total_lp <= 0 or rsv_xfg <= 0 or rsv_heat <= 0:
        return 0.0
    if amount_xfg > 0:
        return total_lp * amount_xfg / rsv_xfg
    if amount_heat > 0:
        return total_lp * amount_heat / rsv_heat
    return 0.0

# ═══════════════════════════════════════════════════════════════
#  CD-BOOK — tracks individual CD positions
# ═══════════════════════════════════════════════════════════════

class CDBook:
    def __init__(self, maturity_sched_len):
        self.cds = []           # list of (amount, term_ep, creation_ep, tier_cap_annual)
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
        """Process maturities, return matured amount"""
        matured = self.maturity[ep]
        if matured > 0:
            self.total_locked -= matured
            self.cds = [c for c in self.cds if c['created'] + c['term'] != ep]
        return matured

    def compute_yield(self, target_rate, drip_rate):
        """Return per-CD yield list and total owed"""
        total = 0.0
        yields = []
        for c in self.cds:
            capped_rate = min(target_rate, c['cap'] / EPY)
            yld = c['amount'] * (capped_rate + drip_rate)
            yields.append(yld)
            total += yld
        return yields, total

    def get_apy_stats(self, target_rate, drip_rate):
        """Mean and min APY across all CDs"""
        if not self.cds:
            return 0.0, 0.0
        apys = []
        for c in self.cds:
            capped_rate = min(target_rate, c['cap'] / EPY)
            apys.append((capped_rate + drip_rate) * EPY * 100)
        if not apys:
            return 0.0, 0.0
        return float(sum(apys) / len(apys)), float(min(apys))

    def tier_category(self, duration):
        if duration < 10: return 0
        if duration < 31: return 1
        if duration < 72: return 2
        return 3

# ═══════════════════════════════════════════════════════════════
#  BOND-BOOK
# ═══════════════════════════════════════════════════════════════

class BondBook:
    def __init__(self):
        self.bonds = []
        self.total_outstanding = 0.0
        self.migrations_remaining = INIT_BOND_POOL

    def migrate(self, amount, creation_ep, rate_bps):
        """User converts COLD deposit → YEM bond"""
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

    def process_epoch(self, ep, swf_balance, yem_reserve, payout_queue):
        """Issue coupons + handle maturities. Returns (swf_after, yem_after, coupons_total, maturities_total)."""
        coupons = 0.0
        maturities = 0.0
        swf = swf_balance
        yem = yem_reserve

        for b in self.bonds:
            if b['repaid']:
                continue
            # Coupon
            coupon = b['principal'] * b['rate_per_epoch']
            coupons += coupon
            payout_queue.append(('bond_coupon', coupon))

            # Maturity
            if ep >= b['issued'] + b['term']:
                owed = b['principal'] + coupon
                from_swf = min(owed, swf)
                swf -= from_swf
                from_yem = owed - from_swf
                yem -= from_yem
                payout_queue.append(('bond_maturity', owed))
                maturities += b['principal']
                b['repaid'] = True
                self.total_outstanding -= b['principal']

        return swf, yem, coupons, maturities

# ═══════════════════════════════════════════════════════════════
#  SCENARIO CONFIGURATIONS
# ═══════════════════════════════════════════════════════════════

def scenario_config(name, prng):
    """Returns (vol_params, demand_params, oracle_params, bond_params) for a scenario."""
    base = {
        'vol_params': {
            'drift': 0.0008,        # daily log-return drift (~20% annual)
            'sigma': 0.035,         # daily vol (~67% annual)
            'crash_lambda': 0.01,   # crash prob per epoch
            'crash_mu': -0.20,
            'crash_sigma': 0.08,
            'jump_lambda': 0.03,    # jump prob per epoch
            'jump_sigma': 0.30,
        },
        'demand_params': {
            'cd_create_mult': 1.0,      # base multiplier on CD_LOCK_RATE
            'swap_vol_mult': 1.0,       # multiplier on swap volume
            'mint_elasticity': 1.0,     # how responsive mints are to peg deviation
        },
        'oracle_params': {
            'staleness_prob': 0.02,
            'swapxfg_stale_prob': 0.05,
            'tier2_min_trades': 5,
        },
        'bond_params': {
            'migration_speed': 0.10,    # fraction of remaining pool migrated per epoch
            'start_epoch': 3,           # start after lag
        },
    }

    configs = {
        'baseline': base,
        'bear_market': {
            **base,
            'vol_params': {**base['vol_params'], 'drift': -0.0002, 'sigma': 0.06, 'crash_lambda': 0.04},
        },
        'sideways_volatile': {
            **base,
            'vol_params': {**base['vol_params'], 'drift': 0.0, 'sigma': 0.08, 'jump_lambda': 0.10},
        },
        'volume_drought': {
            **base,
            'demand_params': {**base['demand_params'], 'swap_vol_mult': 0.10},
        },
        'bond_wave': {
            **base,
            'bond_params': {**base['bond_params'], 'migration_speed': 0.30},
        },
        'oracle_failure': {
            **base,
            'oracle_params': {**base['oracle_params'], 'staleness_prob': 0.10, 'swapxfg_stale_prob': 0.20},
        },
        'extreme_crash': {
            **base,
            'vol_params': {**base['vol_params'], 'sigma': 0.06, 'crash_lambda': 0.08, 'crash_mu': -0.60, 'crash_sigma': 0.25},
        },
        'full': base,   # baseline × 15yr
    }
    return configs.get(name, base)

# ═══════════════════════════════════════════════════════════════
#  CORE SIMULATION — one run
# ═══════════════════════════════════════════════════════════════

def run_sim(scenario_name, rng, callback=None):
    cfg = scenario_config(scenario_name, rng)
    vp = cfg['vol_params']
    dp = cfg['demand_params']
    op = cfg['oracle_params']
    bp = cfg['bond_params']

    # ── State ──
    px = INIT_POOL_XFG                           # pool reserve XFG
    t0 = HEAT_PEG / INIT_XFG_PRICE               # initial ratio
    ph = px / max(t0, 1e-6)                      # pool reserve HEAT
    total_lp = px * 2                             # initial LP shares (dummy)
    protocol_lp = 0.0                            # protocol LP shares
    accumulated_lp_yield = 0.0                    # LP yield accrued

    treasury = INIT_TREASURY
    heat_supply = ph * 8                          # estimated total HEAT supply
    cd_pool = 0.0                                 # accumulated CD share (reset each epoch)
    swf = 0.0
    yem_reserve = 0.0
    xfg_burned = 0.0                              # lifetime XFG→HEAT burn
    heat_burned_lifetime = 0.0                    # lifetime HEAT→XFG burn

    # YEM lag / rolling
    lag_counter = 0
    lag_complete = False
    rolling_rates = [0.0, 0.0, 0.0]
    rolling_count = 0

    # CD book — seed with spread of durations, all created at epoch 0
    cd_book = CDBook(N_EPOCHS + 500)
    n_seed = 400
    for _ in range(n_seed):
        amt = INIT_CD_LOCKED / n_seed * float(rng.random() * 0.5 + 0.75)
        dur = int(np.clip(rng.normal(CD_DUR_MEAN, CD_DUR_STD), 10, 150))
        cd_book.create(amt, dur, 0)

    # Bond book
    bond_book = BondBook()
    migration_epochs = []  # track bond migration schedule

    # Oracle
    oracle_price = 0.0
    last_oracle_price = 0.0
    oracle_stale_epochs = 0
    oracle_active = False

    # XFG price
    xfg_price = INIT_XFG_PRICE
    xfg_log_price = np.log(xfg_price)

    # Peg
    redemption_price = HEAT_PEG / max(xfg_price, 0.01)

    # Payout queue (coinbase)
    payout_queue = []   # ('reason', amount)

    # Per-block tracking for YEM reserve reversibility (simplified: per-epoch)
    block_scalp = []

    # ── Results storage ──
    rows = []
    yearly = defaultdict(list)

    for ep in range(N_EPOCHS):
        yr = ep // EPY
        if yr >= N_YEARS:
            break

        # ──────── 1. XFG PRICE (stochastic) ────────
        # GBM + jump-diffusion
        daily_ret = rng.normal(vp['drift'], vp['sigma'])
        xfg_log_price += daily_ret
        # Crash shock
        if rng.random() < vp['crash_lambda']:
            xfg_log_price += rng.normal(vp['crash_mu'], vp['crash_sigma'])
        # Jumps
        if rng.random() < vp['jump_lambda']:
            xfg_log_price += rng.normal(0, vp['jump_sigma'])
        xfg_price = max(0.01, np.exp(xfg_log_price))
        xfg_price = min(xfg_price, 500.0)  # cap extreme outliers

        # ──────── 2. ORACLE ────────
        oracle_update = not oracle_active or rng.random() > op['staleness_prob']
        if oracle_update:
            oracle_price = xfg_price * float(1 + rng.normal(0, 0.01))  # small measurement error
            last_oracle_price = oracle_price
            oracle_stale_epochs = 0
            oracle_active = True
        else:
            oracle_stale_epochs += 1
            oracle_price = last_oracle_price if last_oracle_price > 0 else xfg_price

        # ──────── 3. REDEMPTION PRICE ────────
        if oracle_price > 1e-6:
            redemption_price = HEAT_PEG / oracle_price

        # ──────── 4. SWAP VOLUME & FEES ────────
        base_vol = 2500 * np.clip(xfg_price / 2.0, 0.3, 5.0) * dp['swap_vol_mult']
        swap_volume = base_vol * float(np.exp(rng.normal(0, 0.3)))
        swap_volume = max(0.0, swap_volume)
        total_fees = swap_volume * SWAP_FEE_RATE

        # Fee split
        cd_share = total_fees * CD_SHARE
        treasury_share = total_fees * TREAS_SHARE
        direct_swf = total_fees * SWF_DIRECT
        treasury += treasury_share
        swf += direct_swf
        cd_pool += cd_share

        # ──────── 5. RANDOM POOL TRADES ────────
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

        # ──────── 6. CD MATURITY ────────
        matured = cd_book.step(ep)
        if matured > 0:
            # CD yield was paid when locked; principal returns to user
            pass

        # Spot price for demand modeling
        heat_price = amm_spot(px, ph) * xfg_price if ph > 0 else 0.0

        # ──────── 7. CD CREATION ────────
        # Base creation rate as % of HEAT market cap, not current CD lock
        heat_mcap = heat_supply * xfg_price * amm_spot(px, ph) if ph > 0 else heat_supply * xfg_price * 0.316
        base_create = max(
            INIT_CD_LOCKED * 0.005,   # floor: 0.5% of initial lock
            heat_mcap * CD_LOCK_RATE * dp['cd_create_mult'] / EPY * 2.0  # 2× multiplier for creation
        )
        # Confidence: higher when peg is tight and XFG is not crashing
        peg_dev_abs = abs(heat_price - HEAT_PEG) / HEAT_PEG if HEAT_PEG > 0 else 1.0
        confidence = max(0.05, 1.0 - peg_dev_abs * 5.0)
        # Random variation
        new_cd_amount = base_create * confidence * float(rng.random() * 0.5 + 0.75)
        new_cd_amount = max(INIT_CD_LOCKED * 0.0005, new_cd_amount)  # floor: 0.05%
        if new_cd_amount > 0:
            dur = int(np.clip(rng.normal(CD_DUR_MEAN, CD_DUR_STD), 10, 150))
            cd_book.create(new_cd_amount, dur, ep)

        # ──────── 8. USER MINT (XFG → HEAT) ────────
        php = amm_spot(px, ph) * xfg_price if ph > 0 else 0
        peg_deviation = (php - HEAT_PEG) / HEAT_PEG if HEAT_PEG > 0 else 0
        mint_volume = swap_volume * 0.05 * dp['mint_elasticity']
        if peg_deviation > 0.02:
            mint_xfg = mint_volume * min(peg_deviation * 10, 3.0)  # more mints when HEAT is expensive
        elif peg_deviation < -0.02:
            mint_xfg = 0.0  # no mints when HEAT is cheap
        else:
            mint_xfg = mint_volume * 0.1  # baseline organic mint

        mint_xfg = max(0.0, mint_xfg)
        mint_xfg = min(mint_xfg, INIT_CD_LOCKED * 0.05)  # cap at 5% of CD pool per epoch
        if mint_xfg > 0 and redemption_price > 0:
            heat_minted = mint_xfg / redemption_price
            user_gets_heat = heat_minted * (1.0 - MINT_PREMIUM)
            premium = heat_minted * MINT_PREMIUM * redemption_price  # XFG value of premium
            heat_supply += user_gets_heat
            xfg_burned += mint_xfg

            # Burn scalp: 8% of premium → YEM Reserve, 92% → EternalFlame
            if lag_complete:
                scalped = premium * YEM_BURN_SCALP
                yem_reserve += scalped
                block_scalp.append(scalped)

            # Add minted HEAT to pool (user sells for XFG)
            if ph > 0 and user_gets_heat > 0:
                pool_add = rng.random() * user_gets_heat * 0.5  # some sold to pool
                if pool_add > 0 and px > 0:
                    ph += pool_add
                    # User takes XFG from pool (arbitrage fee)
                    xfg_taken = amm_swap_output(pool_add, ph - pool_add, px, HEARTH_FEE_BPS)
                    if 0 < xfg_taken < px:
                        px -= xfg_taken
                        treasury += mint_xfg - xfg_taken  # premium captured

        # ──────── 9. USER BURN (HEAT → XFG) ────────
        burn_volume = swap_volume * 0.03 * dp['mint_elasticity']
        if peg_deviation < -0.02:
            burn_heat = burn_volume * min(-peg_deviation * 10, 3.0)
        elif peg_deviation > 0.02:
            burn_heat = 0.0
        else:
            burn_heat = burn_volume * 0.05

        burn_heat = max(0.0, burn_heat)
        burn_heat = min(burn_heat, ph * 0.05)  # cap at 5% of pool HEAT
        if burn_heat > 0 and redemption_price > 0 and px > 0:
            xfg_returned = burn_heat * redemption_price
            user_gets_xfg = xfg_returned * (1.0 - BURN_PREMIUM)
            protocol_keeps = xfg_returned * BURN_PREMIUM
            heat_supply -= burn_heat
            heat_burned_lifetime += burn_heat
            treasury += protocol_keeps

            # Protocol buys HEAT from pool, burns it
            pool_xfg_buy = user_gets_xfg
            if pool_xfg_buy > 0 and ph > 0:
                heat_from_pool = amm_swap_output(pool_xfg_buy, px, ph, HEARTH_FEE_BPS)
                if 0 < heat_from_pool < ph:
                    px += pool_xfg_buy
                    ph -= heat_from_pool

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
                    # HEAT expensive: mint HEAT → sell to pool (protocol profits)
                    heat_to_mint = arb_amt / max(redemption_price, 1e-9)
                    if heat_to_mint > 0 and ph > 0:
                        ph += heat_to_mint
                        out_xfg = amm_swap_output(heat_to_mint, ph - heat_to_mint, px, HEARTH_FEE_BPS)
                        if 0 < out_xfg < px:
                            px -= out_xfg
                            treasury += out_xfg
                            heat_supply += heat_to_mint
                else:
                    # HEAT cheap: protocol buys HEAT from pool → burns it
                    if treasury > arb_amt * 3 and ph > 0:  # need 3× coverage
                        xfg_spend = min(arb_amt * 0.25, treasury * 0.005)  # 0.5% max of treasury
                        heat_from_pool = amm_swap_output(xfg_spend, px, ph, HEARTH_FEE_BPS)
                        if 0 < heat_from_pool < ph:
                            px += xfg_spend
                            ph -= heat_from_pool
                            treasury -= xfg_spend
                            heat_supply -= heat_from_pool
                            heat_burned_lifetime += heat_from_pool

        # ──────── 11. YEM EPOCH BOUNDARY (fires every iteration = every blockchain epoch) ────────
        # 11a. Treasury LP contribution (yearly, at EPY-aligned boundaries, including end-of-sim)
        if ep > 0 and ep % EPY == 0:
            if treasury > 0 and px > 0 and ph > 0 and total_lp > 0:
                lp_xfg = treasury * TREAS_LP_CONTRIB
                shares = amm_single_sided_lp(lp_xfg, 0, total_lp, px, ph)
                if shares > 0:
                    px += lp_xfg
                    total_lp += shares
                    protocol_lp += shares
                    treasury -= lp_xfg

                # LP yield feed
                if protocol_lp > 0 and total_lp > 0:
                    lp_yield_share = accumulated_lp_yield * (protocol_lp / total_lp)
                    swf += lp_yield_share * LP_YIELD_FEED
                    treasury += lp_yield_share * (1.0 - LP_YIELD_FEED)
            accumulated_lp_yield = 0.0

            # Bond migrations (yearly)
            if ep >= bp['start_epoch'] * EPY and bond_book.migrations_remaining > 0:
                mig_amt = min(
                    bond_book.migrations_remaining * bp['migration_speed'],
                    cd_book.total_locked * 0.005  # cap: 0.5% of CD locked
                )
                org_rate = cd_pool / max(cd_book.total_locked, 1.0) if cd_book.total_locked > 0 else BOND_MIN_RATE
                bond_rate_bps = int(np.clip(org_rate * BOND_RATE_MULT * 10000, BOND_MIN_RATE * 10000, BOND_MAX_RATE * 10000))
                principal = bond_book.migrate(mig_amt, ep, bond_rate_bps)
                swf += principal

            # Bond coupons + maturities
            swf, yem_reserve, coupons_paid, maturities_paid = bond_book.process_epoch(
                ep, swf, yem_reserve, payout_queue)

            # Credit burn scalp for this year
            if block_scalp:
                yem_reserve += sum(block_scalp)
                block_scalp = []

        # 11b. YEM smoothing (every epoch after lag)
        if not lag_complete:
            lag_counter += 1
            swf += cd_pool  # accumulate 100% of CD pool into SWF during lag
            cd_pool = 0.0
            if lag_counter >= YEM_LAG_EPOCHS:
                lag_complete = True
        else:
            organic_rate = cd_pool / max(cd_book.total_locked, 1.0) if cd_book.total_locked > 0 else 0.0
            rolling_rates[rolling_count % 3] = organic_rate
            rolling_count += 1
            target_rate = np.mean(rolling_rates[:max(rolling_count, 1)]) if rolling_count > 0 else 0.0

            drip_rate = (swf * YEM_SWF_DRIP / max(cd_book.total_locked, 1.0)) if cd_book.total_locked > 0 else 0.0
            yields, total_yield = cd_book.compute_yield(target_rate, drip_rate)

            surplus = cd_pool - total_yield
            if surplus > 0:
                swf += surplus * YEM_SWF_SAVE
            elif surplus < 0:
                deficit = -surplus
                from_swf = min(deficit, swf)
                swf -= from_swf
                remaining = deficit - from_swf
                if remaining > 0:
                    from_reserve = min(remaining, yem_reserve)
                    yem_reserve -= from_reserve
                    if from_reserve > 0:
                        payout_queue.append(('deficit_coverage', from_reserve))
                    remaining -= from_reserve
                    if remaining > 0:
                        backstop = treasury * YEM_TREAS_BACKSTOP
                        treasury -= min(remaining, backstop)

            # Execute CD yield: CD pool XFG buys HEAT from AMM
            if total_yield > 0 and ph > 0 and px > 0:
                cd_spend = total_yield
                heat_bought = amm_swap_output(cd_spend, px, ph, HEARTH_FEE_BPS)
                if 0 < heat_bought < ph * 0.95:
                    px += cd_spend
                    ph -= heat_bought
                    heat_supply += heat_bought

            cd_pool = 0.0  # reset for next epoch

        # ──────── 12. RECORD METRICS ────────
        apy_mean, apy_min = cd_book.get_apy_stats(
            np.mean(rolling_rates[:max(rolling_count, 1)]) if rolling_count > 0 else 0.0 if lag_complete else 0.0,
            (swf * YEM_SWF_DRIP / max(cd_book.total_locked, 1.0)) if cd_book.total_locked > 0 else 0.0,
        )
        heat_price = amm_spot(px, ph) * xfg_price if ph > 0 else 0.0
        peg_dev = abs(heat_price - HEAT_PEG) / HEAT_PEG if HEAT_PEG > 0 else 0.0

        row = {
            'epoch': ep,
            'year': yr,
            'xfg_price': xfg_price,
            'oracle_price': oracle_price,
            'oracle_stale': int(oracle_stale_epochs > 0),
            'redemption_price': redemption_price,
            'heat_price': heat_price,
            'peg_deviation': peg_dev,
            'swap_volume': swap_volume,
            'total_fees': total_fees,
            'cd_pool': cd_pool,
            'swf_balance': swf,
            'yem_reserve': yem_reserve,
            'treasury': treasury,
            'heat_supply': heat_supply,
            'pool_xfg': px,
            'pool_heat': ph,
            'cd_locked': cd_book.total_locked,
            'cd_apy_mean': apy_mean,
            'cd_apy_min': apy_min,
            'bonds_outstanding': bond_book.total_outstanding,
            'bond_migrations_remaining': bond_book.migrations_remaining,
            'xfg_burned': xfg_burned,
            'heat_burned_lifetime': heat_burned_lifetime,
            'lag_complete': int(lag_complete),
            'organic_rate': rolling_rates[(rolling_count - 1) % 3] if rolling_count > 0 else 0.0,
            'rolling_avg_rate': np.mean(rolling_rates[:max(rolling_count, 1)]) if rolling_count > 0 else 0.0,
            'payout_queue_len': len(payout_queue),
            'solvent': int(swf >= 0 and yem_reserve >= 0 and treasury >= 0),
        }
        rows.append(row)

        # Yearly aggregates
        for k in ['cd_apy_mean', 'cd_apy_min', 'peg_deviation', 'swf_balance', 'yem_reserve', 'treasury', 'heat_supply']:
            yearly[k].append(row[k])

        if callback:
            callback(ep, row)

    # Final process: handle bond maturities + LP at end of sim
    swf, yem_reserve, _, _ = bond_book.process_epoch(
        N_EPOCHS, swf, yem_reserve, payout_queue)
    if protocol_lp > 0 and total_lp > 0 and accumulated_lp_yield > 0:
        lp_yield_share = accumulated_lp_yield * (protocol_lp / total_lp)
        swf += lp_yield_share * LP_YIELD_FEED
        treasury += lp_yield_share * (1.0 - LP_YIELD_FEED)
    if block_scalp:
        yem_reserve += sum(block_scalp)
        block_scalp = []

    # ── Death spiral check ──
    spiral = False
    spiral_strike = 0
    for r in rows:
        if r['peg_deviation'] > 0.5:
            spiral_strike += 1
        else:
            spiral_strike = max(0, spiral_strike - 1)
        if spiral_strike > EPY:
            spiral = True
            break

    # ── Acceptance metrics ──
    post_lag = [r for r in rows if r['lag_complete']]
    post_bootstrap = post_lag[int(len(post_lag) * 0.3):]  # skip first 30% of post-lag epochs

    def mean_or_zero(vals):
        clean = [v for v in vals if not np.isnan(v) and not np.isinf(v)]
        return float(sum(clean) / len(clean)) if clean else 0.0

    def min_or_zero(vals):
        clean = [v for v in vals if not np.isnan(v) and not np.isinf(v)]
        return float(min(clean)) if clean else 0.0

    # Bond obligations: all ISSUED bonds that are PAST maturity must be repaid
    all_matured_repaid = True
    for b in bond_book.bonds:
        if b['issued'] + b['term'] < N_EPOCHS and not b['repaid']:
            all_matured_repaid = False
            break
    bond_ok = 1.0 if all_matured_repaid else 0.0

    return {
        'scenario': scenario_name,
        'spiral': spiral,
        'metrics': {
            'bond_obligations_met': bond_ok,
            'mean_cd_apy': mean_or_zero([r['cd_apy_mean'] for r in post_lag]),
            'worst_cd_apy': min_or_zero([r['cd_apy_min'] for r in post_lag]),
            'peg_healthy_pct': mean_or_zero([1.0 if r['peg_deviation'] < 0.20 else 0.0 for r in post_bootstrap]) * 100,
            'peg_moderate_pct': mean_or_zero([1.0 if r['peg_deviation'] < 0.50 else 0.0 for r in post_bootstrap]) * 100,
            'swf_never_neg': 1.0 if all(r['swf_balance'] >= 0 for r in rows) else 0.0,
            'yem_never_neg': 1.0 if all(r['yem_reserve'] >= 0 for r in rows) else 0.0,
            'treasury_never_neg': mean_or_zero([1.0 if r['treasury'] >= 0 else 0.0 for r in rows]) * 100,
            'lag_completed': 1.0 if any(r['lag_complete'] for r in rows) else 0.0,
            'solvent_epochs_pct': mean_or_zero([r['solvent'] for r in rows]) * 100,
            'final_swf': rows[-1]['swf_balance'],
            'final_yem_reserve': rows[-1]['yem_reserve'],
            'final_treasury': rows[-1]['treasury'],
            'final_heat_supply': rows[-1]['heat_supply'],
            'final_cd_locked': rows[-1]['cd_locked'],
            'max_peg_deviation': max(r['peg_deviation'] for r in rows) * 100 if rows else 0,
        },
        'rows': rows,
    }

# ═══════════════════════════════════════════════════════════════
#  BATCH RUNNER
# ═══════════════════════════════════════════════════════════════

def run_scenario_batch(scenario_name, n_sims, quiet=False):
    t0 = time.time()
    results = []
    for s in range(n_sims):
        rng = np.random.RandomState(SEED_BASE + hash(scenario_name + str(s)) % (2**30))
        res = run_sim(scenario_name, rng)
        results.append(res)
        if not quiet and (s + 1) % max(1, n_sims // 10) == 0:
            sys.stdout.write(f"\r    {scenario_name}: {s+1}/{n_sims}"); sys.stdout.flush()

    dt = time.time() - t0
    if not quiet:
        sys.stdout.write(f"\r    {scenario_name}: {n_sims}/{n_sims} ({dt:.0f}s)\n")

    return results

# ═══════════════════════════════════════════════════════════════
#  OUTPUT WRITERS
# ═══════════════════════════════════════════════════════════════

def write_csv(results, output_dir):
    """Write one CSV per scenario (median run + worst-case run)"""
    os.makedirs(output_dir, exist_ok=True)
    for scenario_name in set(r['scenario'] for r in results):
        sc_results = [r for r in results if r['scenario'] == scenario_name]
        # Pick median and worst run by final SWF
        sc_results.sort(key=lambda r: r['rows'][-1]['swf_balance'] if r['rows'] else 0)
        median_run = sc_results[len(sc_results) // 2]
        worst_run = sc_results[0]

        for label, run in [('median', median_run), ('worst', worst_run)]:
            path = os.path.join(output_dir, f"{scenario_name}_{label}.csv")
            if run['rows']:
                with open(path, 'w', newline='') as f:
                    w = csv.DictWriter(f, fieldnames=run['rows'][0].keys())
                    w.writeheader()
                    w.writerows(run['rows'])
        print(f"  CSV written: {scenario_name} (median + worst)")

def write_summary(results, output_dir):
    """Aggregate summary JSON across all runs per scenario"""
    os.makedirs(output_dir, exist_ok=True)
    summary = {'config': {
        'heat_peg': HEAT_PEG, 'burn_scalp_bps': int(YEM_BURN_SCALP * 10000),
        'swf_save_pct': int(YEM_SWF_SAVE * 100), 'swf_drip_bps': int(YEM_SWF_DRIP * 10000),
        'lag_epochs': YEM_LAG_EPOCHS, 'rolling_window': YEM_ROLLING_WIN,
        'tier_cap_min': int(TIER_CAP_MIN * 100), 'tier_cap_max': int(TIER_CAP_MAX * 100),
        'bond_max_rate': int(BOND_MAX_RATE * 100), 'epy': EPY,
        'fee_split': f"{int(CD_SHARE*100)}/{int(TREAS_SHARE*100)}/{int(SWF_DIRECT*100)}",
    }, 'scenarios': {}}

    scenario_names = sorted(set(r['scenario'] for r in results))
    for sn in scenario_names:
        sc = [r for r in results if r['scenario'] == sn]
        med_metrics = {}
        for k in sc[0]['metrics']:
            vals = [r['metrics'][k] for r in sc]
            med_metrics[k] = float(np.median(vals))
        med_metrics['spiral_pct'] = float(np.mean([r['spiral'] for r in sc]) * 100)
        med_metrics['runs'] = len(sc)
        summary['scenarios'][sn] = med_metrics

    path = os.path.join(output_dir, 'summary.json')
    with open(path, 'w') as f:
        json.dump(summary, f, indent=2)
    print(f"  Summary: {path}")

def write_plots(results, output_dir):
    """Generate time-series plots per scenario"""
    try:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
    except ImportError:
        print("  matplotlib not available, skipping plots")
        return

    os.makedirs(output_dir, exist_ok=True)

    plot_keys = [
        ('xfg_price', 'XFG Price (USD)', 'Price'),
        ('heat_price', 'HEAT Price (USD)', 'Price'),
        ('peg_deviation', 'Peg Deviation (fraction)', 'Deviation'),
        ('cd_apy_mean', 'CD APY Mean (%)', 'APY'),
        ('cd_apy_min', 'CD APY Min (%)', 'Percent'),
        ('swf_balance', 'SWF Balance', 'XFG'),
        ('yem_reserve', 'YEM Reserve', 'XFG'),
        ('treasury', 'Treasury Balance', 'XFG'),
        ('heat_supply', 'HEAT Supply', 'HEAT'),
        ('cd_locked', 'CD Locked', 'XFG'),
    ]

    scenario_names = sorted(set(r['scenario'] for r in results))
    for sn in scenario_names:
        sc_results = [r for r in results if r['scenario'] == sn]
        runs = sc_results[:min(100, len(sc_results))]  # sample 100 runs

        fig, axes = plt.subplots(5, 2, figsize=(18, 22))
        fig.suptitle(f"YEM v3 + Mode 4 — {sn}", fontsize=14, fontweight='bold')

        for idx, (key, title, ylabel) in enumerate(plot_keys):
            ax = axes[idx // 2][idx % 2]
            # Plot median band
            all_series = []
            max_len = 0
            for run in runs:
                vals = [r[key] for r in run['rows']]
                all_series.append(vals)
                max_len = max(max_len, len(vals))
                ax.plot(vals, alpha=0.08, color='steelblue', linewidth=0.5)

            # Plot median
            if all_series:
                padded = [s + [s[-1]] * (max_len - len(s)) if s else [0] * max_len for s in all_series]
                arr = np.array(padded)
                median = np.median(arr, axis=0)
                p25 = np.percentile(arr, 25, axis=0)
                p75 = np.percentile(arr, 75, axis=0)
                ax.plot(median, color='darkblue', linewidth=2, label='Median')
                ax.fill_between(range(len(median)), p25, p75, alpha=0.15, color='darkblue')

            ax.set_title(title)
            ax.set_ylabel(ylabel)
            ax.set_xlabel('Epoch')
            ax.grid(True, alpha=0.3)

        plt.tight_layout()
        path = os.path.join(output_dir, f"{sn}_plots.png")
        plt.savefig(path, dpi=100)
        plt.close()
        print(f"  Plot: {path}")

def print_table(results):
    """Console summary table"""
    scenario_names = sorted(set(r['scenario'] for r in results))
    header = ['SCENARIO', 'CD_APY_μ', 'CD_APY_min', 'Peg<%20', 'Peg<%50', 'Swf≥0', 'Res≥0', 'Solv%', 'Spiral', 'Swf_f', 'Res_f', 'Tre_f']
    print(f"\n{'─'*130}")
    print(f"  {' '.join(f'{h:>10}' for h in header)}")
    print(f"{'─'*130}")

    for sn in scenario_names:
        sc = [r for r in results if r['scenario'] == sn]
        m = {k: np.median([r['metrics'][k] for r in sc]) for k in sc[0]['metrics']}
        spiral_pct = np.mean([r['spiral'] for r in sc]) * 100
        print(f"  {sn:<22} {m['mean_cd_apy']:>9.2f}% {m['worst_cd_apy']:>8.2f}% "
              f"{m['peg_healthy_pct']:>8.0f}% {m['peg_moderate_pct']:>8.0f}% "
              f"{m['swf_never_neg']:>6.0f} {m['yem_never_neg']:>6.0f} "
              f"{m['solvent_epochs_pct']:>6.0f}% {spiral_pct:>5.1f}% "
              f"{fmt(m['final_swf']):>10} {fmt(m['final_yem_reserve']):>10} {fmt(m['final_treasury']):>10}")

    print(f"{'─'*130}")

# ═══════════════════════════════════════════════════════════════
#  MAIN
# ═══════════════════════════════════════════════════════════════

def main():
    parser = argparse.ArgumentParser(description='YEM v3 + Mode 4 Monte Carlo Simulation')
    parser.add_argument('--scenario', type=str, default=None,
                        help='Run specific scenario (baseline, bear_market, sideways_volatile, volume_drought, bond_wave, oracle_failure, extreme_crash, full)')
    parser.add_argument('--runs', type=int, default=2000, help='Monte Carlo runs per scenario')
    parser.add_argument('--output', type=str, default='sim_out/', help='Output directory')
    parser.add_argument('--no-plots', action='store_true', help='Skip plot generation')
    parser.add_argument('--quiet', action='store_true', help='Suppress progress output')
    args = parser.parse_args()

    scenarios = ['baseline', 'bear_market', 'sideways_volatile', 'volume_drought',
                  'bond_wave', 'oracle_failure', 'extreme_crash', 'full']
    if args.scenario:
        scenarios = [args.scenario]

    print(f"\n{'═'*130}")
    print(f"  YEM v3 + Mode 4 — Full System Monte Carlo")
    print(f"  {len(scenarios)} scenario(s) × {args.runs} runs × {N_EPOCHS} epochs ({N_YEARS}yr)")
    print(f"  HEAT Peg: ${HEAT_PEG:.2f}  |  Fee split: {int(CD_SHARE*100)}/{int(TREAS_SHARE*100)}/{int(SWF_DIRECT*100)}")
    print(f"  Burn scalp: {int(YEM_BURN_SCALP*100)}%  |  SWF save: {int(YEM_SWF_SAVE*100)}%  |  SWF drip: {YEM_SWF_DRIP*100:.1f}%/epoch")
    print(f"  Tier caps: {int(TIER_CAP_MIN*100)}%–{int(TIER_CAP_MAX*100)}%  |  Bond max rate: {int(BOND_MAX_RATE*100)}%/yr")
    print(f"{'═'*130}")

    all_results = []
    for sn in scenarios:
        print(f"\n  {sn}...")
        sc_results = run_scenario_batch(sn, args.runs, quiet=args.quiet)
        all_results.extend(sc_results)

    print_table(all_results)

    if args.output:
        print(f"\n  Writing output to {args.output}/")
        write_csv(all_results, args.output)
        write_summary(all_results, args.output)
        if not args.no_plots:
            write_plots(all_results, args.output)

    print(f"\n  Done. {'─'*120}")

if __name__ == '__main__':
    main()
