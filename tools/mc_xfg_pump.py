#!/usr/bin/env python3
"""
Monte Carlo: XFG Pump vs PI Controller — Pool Management Strategy Comparison
==============================================================================

Two strategies for managing the HEAT/XFG AMM pool over 30 years:

  BASELINE (current): PI controller sets redemption price target, peg arb loop
  XFG PUMP (proposed): XFG from fees accumulated & added one-sided to pool,
                       protocol-owned HEAT reserve for interventions.

Both share:
  - Constant-product AMM (x*y=k) with 0.3% fee
  - 100% swap fees → CD yield (buy HEAT from AMM)
  - 180 epochs/year, 30 years = 5400 epochs

Usage:
  python tools/mc_xfg_pump.py
  python tools/mc_xfg_pump.py --sims 200 --years 30
"""

import numpy as np
import sys
import time
import argparse

# ═══════════════════════════════════════════════════════════════════
#  CONSTANTS
# ═══════════════════════════════════════════════════════════════════

EPY = 180                   # epochs per year
BLOCK_TIME_MIN = 8          # minutes per block
HEARTH_FEE_BPS = 30         # 0.3% AMM swap fee (30 bps)
HEAT_PEG = 1.00             # HEAT peg target ($1)
CD_FEE_RATE = 0.01          # 1% atomic swap fee → CD pool
TARGET_RATIO = 0.1          # target XFG/HEAT ratio (10:1 HEAT:XFG = 0.1)

# XFG emission schedule (halving every ~4 years / 720 epochs)
XFG_INITIAL_EMISSION_PER_BLOCK = 1.0
XFG_BLOCK_PER_EPOCH = 60 * BLOCK_TIME_MIN  # 60*8 = 480 blocks/epoch
XFG_EMISSION_PER_EPOCH = XFG_INITIAL_EMISSION_PER_BLOCK * XFG_BLOCK_PER_EPOCH  # 480 XFG/epoch

# AMM
INITIAL_XFG = 4_000_000    # 4M XFG in pool
INITIAL_HEAT = 40_000_000   # 40M HEAT in pool
INITIAL_RATIO = INITIAL_XFG / INITIAL_HEAT  # 0.1 (XFG:HEAT, i.e. 10:1 HEAT:XFG)

# XFG pump parameters
PUMP_FEE_SHARE = 1.0        # 100% of swap fees → accumulated XFG for pump
PUMP_INTERVAL = 1           # pump every epoch
PUMP_SELL_HEAT_THRESHOLD = 0.12  # sell protocol HEAT when XFG/HEAT > 0.12 (>10% XFG)
PUMP_MINT_HEAT_THRESHOLD = 0.08  # mint HEAT when XFG/HEAT < 0.08

# Baseline PI controller
PI_KP = 0.08                # proportional gain
PI_KI = 0.015               # integral gain
ARB_ROUNDS = 40             # peg arb iterations per epoch
ARB_SIZE_PCT = 0.03         # 3% of smaller reserve per arb round
ARB_MAX_SUPPLY = 5e8        # cap HEAT supply growth from arb

# CD pool
INITIAL_CD_LOCKED = 50_000  # XFG locked in CDs at start
CD_DUR_MEAN = 60            # epochs
CD_DUR_STD = 20

# Organic trading
VOL_MIN_PCT = 0.05          # 5% of pool per epoch
VOL_MAX_PCT = 0.20          # 20% of pool per epoch

# XFG external price
XFG_PRICE_START = 1.00
XFG_PRICE_ANNUAL_GROWTH = 0.08  # 8% annual growth
XFG_PRICE_NOISE = 0.30     # 30% annualized volatility (per-epoch noise)

# ═══════════════════════════════════════════════════════════════════
#  HELPERS
# ═══════════════════════════════════════════════════════════════════

def fmt(v):
    if abs(v) >= 1e9: return f"{v/1e9:.2f}B"
    if abs(v) >= 1e6: return f"{v/1e6:.2f}M"
    if abs(v) >= 1e3: return f"{v/1e3:.1f}K"
    return f"{v:.1f}"

def amm_swap(amount_in, reserve_in, reserve_out):
    """Constant-product AMM swap with 0.3% fee."""
    if reserve_in <= 0 or reserve_out <= 0 or amount_in <= 0:
        return 0.0
    fee = amount_in * HEARTH_FEE_BPS / 10000.0
    net_in = amount_in - fee
    return reserve_out * net_in / (reserve_in + net_in)

def amm_spot_xfg_per_heat(px, ph):
    """XFG per HEAT in the pool."""
    if ph <= 0: return 1e12
    return px / ph

def xfg_emission(ep):
    """XFG emission per epoch with halving schedule."""
    halvings = ep // 720  # halve every 720 epochs (~4 years)
    return XFG_EMISSION_PER_EPOCH / (2 ** halvings)

def xfg_price(ep, rng):
    """Stochastic XFG price with geometric Brownian motion + jumps."""
    yr = ep / EPY
    drift = XFG_PRICE_ANNUAL_GROWTH / EPY
    sigma = XFG_PRICE_NOISE / np.sqrt(EPY)
    log_ret = drift + sigma * rng.normal()
    # occasional jumps
    if rng.random() < 0.02:  # 2% chance of jump per epoch
        log_ret += rng.normal(0, 0.15)
    return max(0.01, XFG_PRICE_START * np.exp(yr * XFG_PRICE_ANNUAL_GROWTH) * np.exp(log_ret))

# ═══════════════════════════════════════════════════════════════════
#  STRATEGY: BASELINE (PI Controller + Peg Arb)
# ═══════════════════════════════════════════════════════════════════

def run_baseline(rng, n_epochs):
    """Baseline: PI controller + peg arb loop. Bounded minting, CD yield from fees."""
    px = float(INITIAL_XFG)        # pool XFG
    ph = float(INITIAL_HEAT)       # pool HEAT
    hs = 0.0                        # HEAT supply (cumulative minted)
    cd_locked = float(INITIAL_CD_LOCKED)
    cd_acc = 0.0                    # CD yield accumulator (XFG-denominated)
    pi_integral = 0.0
    ema_price = XFG_PRICE_START

    total_minted = 0.0
    total_burned = 0.0
    healthy_epochs = 0
    total_epochs = 0
    cumulative_cd_yield = 0.0       # total XFG spent on CD yield

    # Records: [epoch, ratio, heat_price, cd_apy, supply, px, ph, drawdown]
    records = np.zeros((n_epochs, 8))
    peak_ratio = TARGET_RATIO
    drawdown = 0.0

    for ep in range(n_epochs):
        xp = xfg_price(ep, rng)
        ema_price = 0.30 * xp + 0.70 * ema_price

        # Organic trading volume
        vol_pct = rng.uniform(VOL_MIN_PCT, VOL_MAX_PCT)
        trade_vol_xfg = px * vol_pct

        n_trades = rng.randint(3, 12)
        for _ in range(n_trades):
            amt = trade_vol_xfg / n_trades
            if rng.random() < 0.5:
                out = amm_swap(amt, px, ph)
                if 0 < out < ph:
                    px += amt; ph -= out
            else:
                heat_amt = amt * (px / max(ph, 1))
                out = amm_swap(heat_amt, ph, px)
                if 0 < out < px:
                    ph += heat_amt; px -= out

        # CD accumulation: 50% of trade fees
        trade_fees_xfg = trade_vol_xfg * HEARTH_FEE_BPS / 10000.0
        cd_acc += trade_fees_xfg * 0.50

        # PI controller
        ratio = px / max(ph, 1e-9)
        heat_price = ratio * ema_price
        peg_error = (heat_price - HEAT_PEG) / HEAT_PEG
        pi_integral += peg_error * PI_KI
        pi_integral = np.clip(pi_integral, -0.5, 0.5)

        # Peg arb loop
        for _ in range(ARB_ROUNDS):
            ratio_now = px / max(ph, 1e-9)
            hp = ratio_now * ema_price
            gap = abs(hp - HEAT_PEG) / HEAT_PEG
            if gap < 0.001: break
            if hs > ARB_MAX_SUPPLY and hp > HEAT_PEG: break
            arb_size = min(px, ph) * ARB_SIZE_PCT
            if hp > HEAT_PEG:
                if arb_size > 0 and ph > 0:
                    out = amm_swap(arb_size, ph, px)
                    if 0 < out < px:
                        ph += arb_size; px -= out; hs += arb_size; total_minted += arb_size
            else:
                xfg_to_spend = min(arb_size, px * 0.01)
                out = amm_swap(xfg_to_spend, px, ph)
                if 0 < out < ph:
                    px += xfg_to_spend; ph -= out; total_burned += out

        # CD yield
        if cd_acc > 0 and ph > 0:
            cumulative_cd_yield += cd_acc
            heat_bought = amm_swap(cd_acc, px, ph)
            if 0 < heat_bought < ph * 0.9:
                px += cd_acc; ph -= heat_bought; hs += heat_bought; total_minted += heat_bought
                cd_acc = 0

        # CD APY = cumulative yield / locked (annualized)
        cd_apy_val = (cumulative_cd_yield / max(cd_locked, 1)) * EPY * 100 / max(ep + 1, 1)

        # CD elasticity
        if cd_apy_val > 100: cd_locked *= 1.05
        elif cd_apy_val > 50: cd_locked *= 1.03
        elif cd_apy_val > 20: cd_locked *= 1.02
        elif cd_apy_val > 5: cd_locked *= 1.005
        else: cd_locked *= 0.998
        cd_locked = max(1e3, cd_locked)

        # XFG emission
        px += xfg_emission(ep)

        # Health
        ratio_final = px / max(ph, 1e-9)
        hp_final = ratio_final * ema_price
        dev = abs(hp_final - HEAT_PEG) / HEAT_PEG
        total_epochs += 1
        if dev < 0.03: healthy_epochs += 1

        peak_ratio = max(peak_ratio, ratio_final)
        drawdown = max(drawdown, (peak_ratio - ratio_final) / peak_ratio)

        records[ep] = [ep, ratio_final, hp_final, cd_apy_val, hs, px, ph, drawdown]

    return records, healthy_epochs / max(1, total_epochs)

# ═══════════════════════════════════════════════════════════════════
#  STRATEGY: XFG PUMP (No PI, Protocol Reserve, One-Sided Add)
# ═══════════════════════════════════════════════════════════════════

def run_xfg_pump(rng, n_epochs):
    """XFG Pump: No PI, XFG from fees pumped one-sided, protocol HEAT reserve."""
    px = float(INITIAL_XFG)
    ph = float(INITIAL_HEAT)
    hs = 0.0
    cd_locked = float(INITIAL_CD_LOCKED)
    cd_acc = 0.0
    ema_price = XFG_PRICE_START

    protocol_heat_reserve = 0.0
    protocol_xfg_accumulated = 0.0
    total_minted = 0.0
    total_sold = 0.0
    healthy_epochs = 0
    total_epochs = 0
    cumulative_cd_yield = 0.0

    records = np.zeros((n_epochs, 8))
    peak_ratio = TARGET_RATIO
    drawdown = 0.0

    for ep in range(n_epochs):
        xp = xfg_price(ep, rng)
        ema_price = 0.30 * xp + 0.70 * ema_price

        vol_pct = rng.uniform(VOL_MIN_PCT, VOL_MAX_PCT)
        trade_vol_xfg = px * vol_pct

        n_trades = rng.randint(3, 12)
        for _ in range(n_trades):
            amt = trade_vol_xfg / n_trades
            if rng.random() < 0.5:
                out = amm_swap(amt, px, ph)
                if 0 < out < ph:
                    px += amt; ph -= out
            else:
                heat_amt = amt * (px / max(ph, 1))
                out = amm_swap(heat_amt, ph, px)
                if 0 < out < px:
                    ph += heat_amt; px -= out

        # Fees → 50% to CD yield
        trade_fees_xfg = trade_vol_xfg * HEARTH_FEE_BPS / 10000.0
        cd_acc += trade_fees_xfg * 0.50

        # XFG emission → protocol pump
        protocol_xfg_accumulated += xfg_emission(ep)

        # ── XFG PUMP MECHANIC ──
        if protocol_xfg_accumulated > 0 and px > 0 and ph > 0:
            px += protocol_xfg_accumulated
            protocol_xfg_accumulated = 0.0

        # ── PROTOCOL HEAT RESERVE ──
        ratio_now = px / max(ph, 1e-9)
        if ratio_now > PUMP_SELL_HEAT_THRESHOLD and protocol_heat_reserve > 0:
            sell_amount = min(protocol_heat_reserve, ph * 0.02, px * 0.02 * (ratio_now / TARGET_RATIO))
            if sell_amount > 0 and ph > 0:
                out = amm_swap(sell_amount, ph, px)
                if 0 < out < px:
                    ph += sell_amount; px -= out
                    protocol_heat_reserve -= sell_amount; total_sold += sell_amount
        elif ratio_now < PUMP_MINT_HEAT_THRESHOLD:
            deficit = TARGET_RATIO - ratio_now
            mint_amount = min(deficit * ph * 0.01, ph * 0.05)
            if mint_amount > 0:
                protocol_heat_reserve += mint_amount; hs += mint_amount; total_minted += mint_amount

        # CD yield
        if cd_acc > 0 and ph > 0:
            cumulative_cd_yield += cd_acc
            heat_bought = amm_swap(cd_acc, px, ph)
            if 0 < heat_bought < ph * 0.9:
                px += cd_acc; ph -= heat_bought; hs += heat_bought; total_minted += heat_bought
                cd_acc = 0

        cd_apy_val = (cumulative_cd_yield / max(cd_locked, 1)) * EPY * 100 / max(ep + 1, 1)

        if cd_apy_val > 100: cd_locked *= 1.05
        elif cd_apy_val > 50: cd_locked *= 1.03
        elif cd_apy_val > 20: cd_locked *= 1.02
        elif cd_apy_val > 5: cd_locked *= 1.005
        else: cd_locked *= 0.998
        cd_locked = max(1e3, cd_locked)

        ratio_final = px / max(ph, 1e-9)
        hp_final = ratio_final * ema_price
        dev = abs(hp_final - HEAT_PEG) / HEAT_PEG
        total_epochs += 1
        if dev < 0.03: healthy_epochs += 1

        peak_ratio = max(peak_ratio, ratio_final)
        drawdown = max(drawdown, (peak_ratio - ratio_final) / peak_ratio)

        records[ep] = [ep, ratio_final, hp_final, cd_apy_val, hs, px, ph, drawdown]

    return records, healthy_epochs / max(1, total_epochs)

# ═══════════════════════════════════════════════════════════════════
#  ANALYSIS
# ═══════════════════════════════════════════════════════════════════

def analyze_batch(batch_records, n_epochs, strategy_name):
    n_sims = len(batch_records)
    # Stack all records
    all_ratios = np.array([b[:, 1] for b in batch_records])
    all_prices = np.array([b[:, 2] for b in batch_records])
    all_cd_apys = np.array([b[:, 3] for b in batch_records])
    all_supply = np.array([b[:, 4] for b in batch_records])
    all_px = np.array([b[:, 5] for b in batch_records])
    all_ph = np.array([b[:, 6] for b in batch_records])
    all_drawdowns = np.array([b[:, 7] for b in batch_records])

    final_yr = EPY * 30 - 1

    stats = {
        'name': strategy_name,
        'final_ratio_med': float(np.median(all_ratios[:, final_yr])),
        'final_ratio_p5': float(np.percentile(all_ratios[:, final_yr], 5)),
        'final_ratio_p95': float(np.percentile(all_ratios[:, final_yr], 95)),
        'final_ratio_mean': float(np.mean(all_ratios[:, final_yr])),
        'final_heat_price_med': float(np.median(all_prices[:, final_yr])),
        'final_supply_med': float(np.median(all_supply[:, final_yr])),
        'final_px_med': float(np.median(all_px[:, final_yr])),
        'final_ph_med': float(np.median(all_ph[:, final_yr])),
        'max_drawdown_med': float(np.median(np.max(all_drawdowns, axis=1))),
        'max_drawdown_p95': float(np.percentile(np.max(all_drawdowns, axis=1), 95)),
        'cd_apy_y10_med': float(np.median(all_cd_apys[:, EPY*10-1])),
        'cd_apy_y20_med': float(np.median(all_cd_apys[:, EPY*20-1])),
        'cd_apy_y30_med': float(np.median(all_cd_apys[:, EPY*30-1])),
        'peg_health': None,  # filled by caller
        'supply_growth_y10': float(np.median(all_supply[:, EPY*10-1])),
        'supply_growth_y20': float(np.median(all_supply[:, EPY*20-1])),
        'supply_growth_y30': float(np.median(all_supply[:, EPY*30-1])),
        'ratio_convergence_y10': float(np.std(all_ratios[:, EPY*10-1])),
        'ratio_convergence_y20': float(np.std(all_ratios[:, EPY*20-1])),
        'ratio_convergence_y30': float(np.std(all_ratios[:, EPY*30-1])),
        'time_to_parity': None,  # filled by caller
    }
    return stats

def compute_time_to_parity(batch_records, n_epochs, target_ratio=TARGET_RATIO, tolerance=0.05):
    """How many epochs until ratio stays within tolerance of target."""
    times = []
    for records in batch_records:
        ratios = records[:, 1]
        found = False
        for ep in range(n_epochs):
            window = ratios[max(0, ep-EPY):ep+1]  # check 1-year window
            if len(window) == EPY and np.std(window) < tolerance * target_ratio:
                times.append(ep)
                found = True
                break
        if not found:
            times.append(n_epochs)
    return float(np.median(times)), float(np.percentile(times, 90))

def compute_peg_health(batch_records, n_epochs, strategy_name):
    """Fraction of epochs within 3% of HEAT peg."""
    healthy_counts = []
    for records in batch_records:
        prices = records[:, 2]
        healthy = np.sum(np.abs(prices - HEAT_PEG) / HEAT_PEG < 0.03)
        healthy_counts.append(healthy / n_epochs)
    return float(np.median(healthy_counts))

# ═══════════════════════════════════════════════════════════════════
#  MAIN
# ═══════════════════════════════════════════════════════════════════

def main():
    parser = argparse.ArgumentParser(description='XFG Pump vs PI Controller Monte Carlo')
    parser.add_argument('--sims', type=int, default=100, help='Number of Monte Carlo runs')
    parser.add_argument('--years', type=int, default=30, help='Simulation years')
    parser.add_argument('--seed', type=int, default=20260619, help='Base RNG seed')
    args = parser.parse_args()

    N_SIMS = args.sims
    N_YEARS = args.years
    N_EPOCHS = EPY * N_YEARS

    print(f"\n{'='*90}")
    print(f"  XFG Pump vs PI Controller — Pool Management Monte Carlo")
    print(f"  {N_SIMS:,} sims x {N_YEARS}yr ({N_EPOCHS} epochs) | {EPY} epochs/yr")
    print(f"  Initial: {fmt(INITIAL_XFG)} XFG + {fmt(INITIAL_HEAT)} HEAT (ratio {INITIAL_RATIO:.3f})")
    print(f"  Target ratio: {TARGET_RATIO:.1f}:1 | HEAT peg: ${HEAT_PEG:.2f}")
    print(f"  AMM fee: {HEARTH_FEE_BPS}bps | CD fee: {CD_FEE_RATE*100:.1f}%")
    print(f"{'='*90}\n")

    # ── Run baseline ──
    t0 = time.time()
    sys.stdout.write(f"  Running BASELINE (PI controller)... ")
    sys.stdout.flush()
    baseline_batch = []
    baseline_health = []
    for s in range(N_SIMS):
        rng = np.random.RandomState(args.seed + s)
        records, health = run_baseline(rng, N_EPOCHS)
        baseline_batch.append(records)
        baseline_health.append(health)
    print(f"done ({time.time()-t0:.1f}s)")

    # ── Run XFG pump ──
    t0 = time.time()
    sys.stdout.write(f"  Running XFG PUMP (protocol reserve)... ")
    sys.stdout.flush()
    pump_batch = []
    pump_health = []
    for s in range(N_SIMS):
        rng = np.random.RandomState(args.seed + s)
        records, health = run_xfg_pump(rng, N_EPOCHS)
        pump_batch.append(records)
        pump_health.append(health)
    print(f"done ({time.time()-t0:.1f}s)")

    # ── Analyze ──
    sys.stdout.write(f"  Analyzing... ")
    sys.stdout.flush()

    baseline_stats = analyze_batch(baseline_batch, N_EPOCHS, "BASELINE (PI)")
    baseline_stats['peg_health'] = float(np.median(baseline_health))

    pump_stats = analyze_batch(pump_batch, N_EPOCHS, "XFG PUMP")
    pump_stats['peg_health'] = float(np.median(pump_health))

    # Time to parity
    bl_parity_med, bl_parity_p90 = compute_time_to_parity(baseline_batch, N_EPOCHS)
    baseline_stats['time_to_parity_med'] = bl_parity_med
    baseline_stats['time_to_parity_p90'] = bl_parity_p90
    baseline_stats['time_to_parity_yr'] = bl_parity_med / EPY

    pu_parity_med, pu_parity_p90 = compute_time_to_parity(pump_batch, N_EPOCHS)
    pump_stats['time_to_parity_med'] = pu_parity_med
    pump_stats['time_to_parity_p90'] = pu_parity_p90
    pump_stats['time_to_parity_yr'] = pu_parity_med / EPY

    print("done\n")

    # ── Print Results ──
    W = 90
    print(f"{'='*W}")
    print(f"  RESULTS COMPARISON")
    print(f"{'='*W}")

    def row(label, bl_val, pu_val, fmt_str=".2f", suffix=""):
        bl_s = f"{bl_val:{fmt_str}}{suffix}"
        pu_s = f"{pu_val:{fmt_str}}{suffix}"
        winner = "<" if abs(bl_val) > abs(pu_val) else ">"
        if label in ["Final Ratio", "Ratio Std Y30", "Max Drawdown (med)", "Max Drawdown (P90)"]:
            # Lower is better
            w = "BASELINE" if bl_val <= pu_val else "XFG PUMP"
        else:
            w = "BASELINE" if bl_val >= pu_val else "XFG PUMP"
        marker = " *" if w == "XFG PUMP" else "  "
        print(f"  {label:<30s}  {bl_s:>14s}  {pu_s:>14s}{marker}")

    print(f"\n  {'Metric':<30s}  {'BASELINE':>14s}  {'XFG PUMP':>14s}")
    print(f"  {'─'*62}")

    print(f"\n  ── Pool Ratio Convergence ──")
    row("Final Ratio (median)", baseline_stats['final_ratio_med'], pump_stats['final_ratio_med'], ".4f")
    row("Final Ratio (P5)", baseline_stats['final_ratio_p5'], pump_stats['final_ratio_p5'], ".4f")
    row("Final Ratio (P95)", baseline_stats['final_ratio_p95'], pump_stats['final_ratio_p95'], ".4f")
    row("Ratio Std Y10", baseline_stats['ratio_convergence_y10'], pump_stats['ratio_convergence_y10'], ".4f")
    row("Ratio Std Y20", baseline_stats['ratio_convergence_y20'], pump_stats['ratio_convergence_y20'], ".4f")
    row("Ratio Std Y30", baseline_stats['ratio_convergence_y30'], pump_stats['ratio_convergence_y30'], ".4f")

    print(f"\n  ── XFG Price Stability ──")
    row("Final XFG Pool (med)", baseline_stats['final_px_med'], pump_stats['final_px_med'], ".0f", " XFG")
    row("Final HEAT Pool (med)", baseline_stats['final_ph_med'], pump_stats['final_ph_med'], ".0f", " HEAT")
    row("Final HEAT Price (med)", baseline_stats['final_heat_price_med'], pump_stats['final_heat_price_med'], ".4f", "$")

    print(f"\n  ── Risk ──")
    row("Max Drawdown (med)", baseline_stats['max_drawdown_med']*100, pump_stats['max_drawdown_med']*100, ".2f", "%")
    row("Max Drawdown (P90)", baseline_stats['max_drawdown_p95']*100, pump_stats['max_drawdown_p95']*100, ".2f", "%")

    print(f"\n  ── HEAT Supply Growth ──")
    row("Supply Y10", baseline_stats['supply_growth_y10'], pump_stats['supply_growth_y10'], ".0f")
    row("Supply Y20", baseline_stats['supply_growth_y20'], pump_stats['supply_growth_y20'], ".0f")
    row("Supply Y30", baseline_stats['supply_growth_y30'], pump_stats['supply_growth_y30'], ".0f")

    print(f"\n  ── CD APY ──")
    row("CD APY Y10", baseline_stats['cd_apy_y10_med'], pump_stats['cd_apy_y10_med'], ".2f", "%")
    row("CD APY Y20", baseline_stats['cd_apy_y20_med'], pump_stats['cd_apy_y20_med'], ".2f", "%")
    row("CD APY Y30", baseline_stats['cd_apy_y30_med'], pump_stats['cd_apy_y30_med'], ".2f", "%")

    print(f"\n  ── Peg Health ──")
    row("Peg Health (<3%)", baseline_stats['peg_health']*100, pump_stats['peg_health']*100, ".1f", "%")

    print(f"\n  ── Time to Parity ──")
    row("Median Epochs", baseline_stats['time_to_parity_med'], pump_stats['time_to_parity_med'], ".0f")
    row("Median Years", baseline_stats['time_to_parity_yr'], pump_stats['time_to_parity_yr'], ".1f", "yr")
    row("P90 Epochs", baseline_stats['time_to_parity_p90'], pump_stats['time_to_parity_p90'], ".0f")

    # ── Year-by-Year comparison ──
    print(f"\n{'='*W}")
    print(f"  YEAR-BY-YEAR MEDIAN COMPARISON")
    print(f"{'='*W}")
    header = f"  {'Yr':>3s}  {'BL Ratio':>9s} {'PMP Ratio':>10s}  {'BL Price':>9s} {'PMP Price':>10s}  {'BL APY':>7s} {'PMP APY':>8s}  {'BL Sup':>9s} {'PMP Sup':>10s}"
    print(header)
    print(f"  {'─'*85}")

    for yr in [1, 2, 3, 5, 7, 10, 15, 20, 25, 30]:
        if yr > N_YEARS: break
        ep = min(yr * EPY - 1, N_EPOCHS - 1)
        bl_rat = np.median([b[ep, 1] for b in baseline_batch])
        pu_rat = np.median([b[ep, 1] for b in pump_batch])
        bl_pri = np.median([b[ep, 2] for b in baseline_batch])
        pu_pri = np.median([b[ep, 2] for b in pump_batch])
        bl_apy = np.median([b[ep, 3] for b in baseline_batch])
        pu_apy = np.median([b[ep, 3] for b in pump_batch])
        bl_sup = np.median([b[ep, 4] for b in baseline_batch])
        pu_sup = np.median([b[ep, 4] for b in pump_batch])
        print(f"  Y{yr:>2d}  {bl_rat:>9.4f} {pu_rat:>10.4f}  {bl_pri:>8.4f}$ {pu_pri:>9.4f}$  {bl_apy:>6.1f}% {pu_apy:>7.1f}%  {fmt(bl_sup):>9s} {fmt(pu_sup):>10s}")

    # ── Verdict ──
    print(f"\n{'='*W}")
    print(f"  VERDICT")
    print(f"{'='*W}")

    # Score cards
    bl_score = 0
    pu_score = 0

    # Ratio convergence (lower std = better)
    if baseline_stats['ratio_convergence_y30'] < pump_stats['ratio_convergence_y30']:
        bl_score += 1
    else:
        pu_score += 1

    # Max drawdown (lower = better)
    if baseline_stats['max_drawdown_med'] < pump_stats['max_drawdown_med']:
        bl_score += 1
    else:
        pu_score += 1

    # Peg health (higher = better)
    if baseline_stats['peg_health'] > pump_stats['peg_health']:
        bl_score += 1
    else:
        pu_score += 1

    # CD APY (higher = better)
    if baseline_stats['cd_apy_y30_med'] > pump_stats['cd_apy_y30_med']:
        bl_score += 1
    else:
        pu_score += 1

    # Supply growth (lower = better for peg stability)
    if baseline_stats['supply_growth_y30'] < pump_stats['supply_growth_y30']:
        bl_score += 1
    else:
        pu_score += 1

    # Time to parity (lower = better)
    if baseline_stats['time_to_parity_yr'] < pump_stats['time_to_parity_yr']:
        bl_score += 1
    else:
        pu_score += 1

    print(f"\n  Score: BASELINE={bl_score}  |  XFG PUMP={pu_score}")
    if pu_score > bl_score:
        print(f"  Winner: XFG PUMP — {pu_score}/{bl_score+pu_score}")
    elif bl_score > pu_score:
        print(f"  Winner: BASELINE — {bl_score}/{bl_score+pu_score}")
    else:
        print(f"  Result: TIE")

    print(f"\n  Key findings:")
    ratio_delta = pump_stats['final_ratio_med'] - baseline_stats['final_ratio_med']
    print(f"    - Final pool ratio: XFG PUMP {'closes' if abs(ratio_delta) < abs(baseline_stats['final_ratio_med'] - TARGET_RATIO) else 'widens'} gap to target")
    dd_delta = pump_stats['max_drawdown_med'] - baseline_stats['max_drawdown_med']
    print(f"    - Max drawdown: XFG PUMP {'better' if dd_delta < 0 else 'worse'} by {abs(dd_delta)*100:.2f}%")
    health_delta = pump_stats['peg_health'] - baseline_stats['peg_health']
    print(f"    - Peg health: XFG PUMP {'better' if health_delta > 0 else 'worse'} by {abs(health_delta)*100:.1f}%")
    print(f"    - CD APY (Y30): BASELINE {baseline_stats['cd_apy_y30_med']:.1f}% vs XFG PUMP {pump_stats['cd_apy_y30_med']:.1f}%")

    print(f"\n{'='*W}\n")
    sys.stdout.flush()

if __name__ == '__main__':
    main()
