#!/usr/bin/env python3
"""
Emission Timer — Fuego block reward projection + Eternal Flame maintenance tracker

Projects when the emission curve exhausts at current levels, and computes
yearly EF burn targets to prevent reward decay. YEM can target these amounts
when distributing burn-scalp percentages.

Usage:
  python scripts/emission_timer.py                          # interactive manual mode
  python scripts/emission_timer.py --rpc http://127.0.0.1   # live daemon (needs /get_info + /f_block_details_json)
  python scripts/emission_timer.py --reward 0.32 --ef 15000 --generated 4500000  # manual
  python scripts/emission_timer.py --speed 19                 # test alternate emission speed
"""

import json
import sys
import math
import argparse
import urllib.request
import urllib.error
from datetime import datetime, timedelta


# ═══════════════════════════════════════════════════
#  CONSTANTS
# ═══════════════════════════════════════════════════

MONEY_SUPPLY_ATOMIC = 80_000_088_000_008  # 8M8 max supply in atomic units
COIN = 10_000_000  # atomic units per XFG (10^7)
DIFFICULTY_TARGET = 480  # seconds per block
BLOCKS_PER_DAY = 86400 // DIFFICULTY_TARGET  # 180
EPOCH_BLOCKS = 900
EPOCHS_PER_YEAR = BLOCKS_PER_DAY * 365 // EPOCH_BLOCKS  # 73
BLOCKS_PER_YEAR = BLOCKS_PER_DAY * 365

DEFAULT_SPEED = 20


def rpc_call(url, method, params=None):
    req_body = json.dumps({"jsonrpc": "2.0", "id": 1, "method": method, "params": params or {}}).encode()
    req = urllib.request.Request(url + "/json_rpc", data=req_body,
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=10) as resp:
        return json.loads(resp.read())


def fetch_live(rpc_url):
    info = rpc_call(rpc_url, "get_info")["result"]

    height = info["height"]
    ef_atomic = info.get("ethereal_xfg", 0)
    last_reward_atomic = info.get("last_block_reward", 0)

    try:
        details = rpc_call(rpc_url, "f_block_details_json", {"hash": info["top_block_hash"]})["result"]
        already_gen_str = details.get("alreadyGeneratedCoins", "0")
        already_gen = int(already_gen_str) if isinstance(already_gen_str, str) else already_gen_str
        base_reward_atomic = details.get("baseReward", 0)
    except Exception:
        already_gen = 0
        base_reward_atomic = last_reward_atomic  # best guess

    return {
        "height": height,
        "ef_XFG": ef_atomic / COIN,
        "last_reward_XFG": last_reward_atomic / COIN,
        "already_generated_XFG": already_gen / COIN,
        "base_reward_XFG": base_reward_atomic / COIN if base_reward_atomic else last_reward_atomic / COIN,
    }


def compute_base_reward(already_gen_atomic, ef_atomic, speed):
    osavvirsak = max(already_gen_atomic - ef_atomic, 0)
    remaining = MONEY_SUPPLY_ATOMIC - osavvirsak
    return remaining >> speed  # >> integer shift


def project_emission(already_gen_XFG, ef_XFG, current_reward_XFG, speed, years=50):
    already = int(already_gen_XFG * COIN)
    ef = int(ef_XFG * COIN)
    rows = []

    for yr in range(years + 1):
        reward_xfg = compute_base_reward(already, ef, speed) / COIN
        osavvirsak = max(already - ef, 0)
        remaining_atomic = MONEY_SUPPLY_ATOMIC - osavvirsak
        remaining_XFG = remaining_atomic / COIN

        blocks_this_year = BLOCKS_PER_DAY * 365
        emitted_this_year = reward_xfg * blocks_this_year

        # at current EF, to maintain reward, need: burn_rate_per_day = reward * 180
        maintenance_per_year = reward_xfg * BLOCKS_PER_DAY * 365

        rows.append({
            "year": yr,
            "reward": reward_xfg,
            "remaining_XFG": remaining_XFG,
            "emitted_this_year": emitted_this_year,
            "maintenance_burn_needed": maintenance_per_year,
            "already_gen": already / COIN,
            "ef": ef / COIN,
        })

        # advance one year: simulate blocks, reward decays each block
        blocks = BLOCKS_PER_DAY * 365
        for _ in range(blocks):
            reward = compute_base_reward(already, ef, speed)
            already += reward
            if ef >= reward:
                ef -= 0  # EF is cumulative burned, doesn't decay
            # each block: reward decreases naturally as already goes up
        # Keep EF constant (assume no new burns for projection)

    return rows


DUST_THRESHOLD_XFG = 0.001


def exhaust_years(reward_XFG, speed):
    """Analytical: years until reward decays below DUST_THRESHOLD (no new burns)."""
    if reward_XFG <= 0:
        return None
    reward_atomic = int(reward_XFG * COIN)
    dust_atomic = int(DUST_THRESHOLD_XFG * COIN)
    if reward_atomic <= dust_atomic:
        return 0.0
    hl_blocks = half_life_blocks(speed)
    halvings = math.log2(reward_atomic / dust_atomic)
    blocks = int(hl_blocks * halvings)
    return blocks / (BLOCKS_PER_DAY * 365)


def half_life_blocks(speed):
    return 2 ** (speed - 1)  # blocks until reward halves


def format_table(rows, speed, current_reward, ef_XFG, already_gen_XFG):
    cols = ["Year", "Reward", "Remaining", "Emitted/Yr", "Maint/Yr", "AlreadyGen", "EF"]
    header = f"{cols[0]:>5}  {cols[1]:>9}  {cols[2]:>14}  {cols[3]:>11}  {cols[4]:>10}  {cols[5]:>14}  {cols[6]:>12}"
    sep = "-" * len(header)

    lines = []
    exhaust = exhaust_years(current_reward, speed)
    exhaust_str = f"{exhaust:.1f} yrs" if exhaust else "N/A"
    lines.append(f"Emission Speed: {speed}  |  Block: {current_reward:.6f} XFG  |  EF: {ef_XFG:,.1f} XFG  |  Generated: {already_gen_XFG:,.1f} XFG")
    lines.append(f"Half-life: {half_life_blocks(speed):,} blocks ({half_life_blocks(speed) / BLOCKS_PER_DAY:.1f} days)  |  Exhaust: {exhaust_str} (to {DUST_THRESHOLD_XFG} XFG)")
    lines.append(f"Maintenance burn needed: {rows[0]['maintenance_burn_needed']:,.1f} XFG/year at current level")
    lines.append("")
    lines.append(header)
    lines.append(sep)

    for row in rows:
        if row["year"] % 5 != 0 and row["year"] > 0 and row["year"] < 20:
            continue
        if row["year"] > 20 and row["year"] % 10 != 0:
            continue
        lines.append(f"{row['year']:>5}  {row['reward']:>9.6f}  {row['remaining_XFG']:>14,.1f}  {row['emitted_this_year']:>11,.1f}  {row['maintenance_burn_needed']:>10,.1f}  {row['already_gen']:>14,.1f}  {row['ef']:>12,.1f}")

    return "\n".join(lines)


def speed_comparison(already_gen_XFG, ef_XFG, current_reward):
    lines = []
    lines.append(f"\nSpeed Comparison | Reward: {current_reward:.6f} XFG | EF: {ef_XFG:,.1f} XFG | Generated: {already_gen_XFG:,.1f} XFG")
    lines.append(f"{'Speed':>7}  {'Reward':>9}  {'Half-life':>12}  {'Maint/Yr':>12}  {'Exhaust':>9}  {'1K EF→/blk':>11}  {'100K→/blk':>11}  {'500K→/blk':>11}")
    lines.append("-" * 100)

    for s in [18, 19, 20]:
        already = int(already_gen_XFG * COIN)
        ef = int(ef_XFG * COIN)
        reward = compute_base_reward(already, ef, s) / COIN
        maint = reward * BLOCKS_PER_DAY * 365
        hl = half_life_blocks(s)
        ex = exhaust_years(reward, s)
        def ef_boost(amount_xfg):
            return (int(amount_xfg * COIN) >> s) / COIN
        lines.append(f"  {s:>3}    {reward:>9.6f}  {hl:>8,} blk  {maint:>12,.1f}  {ex:>7.1f}y  {ef_boost(1000):>11.6f}  {ef_boost(100_000):>11.6f}  {ef_boost(500_000):>11.6f}")

    return "\n".join(lines)


def yem_targets(reward_XFG, ef_XFG, annual_swap_fees_XFG=None, annual_mints_XFG=None, premium_pct=5.0):
    """Compute YEM burn-scalp targets: what % of fees/volume needed to hit maintenance."""
    maint_yr = reward_XFG * BLOCKS_PER_DAY * 365
    hl_yr = half_life_blocks(20) / BLOCKS_PER_YEAR

    # EF needed for +1yr extension = remaining_pool / half_life_yrs
    reward_atomic = int(reward_XFG * COIN)
    remaining_atomic = reward_atomic << 20  # reverse: remaining = baseReward * 2^speed
    remaining_pool_xfg = remaining_atomic / COIN
    ef_per_year = remaining_pool_xfg / hl_yr

    lines = []
    lines.append(f"\nYEM Eternal Flame Funding Targets")
    lines.append(f"{'─' * 62}")
    lines.append(f"  Yearly maintenance burn target:  {maint_yr:>12,.1f} XFG  (holds reward flat)")
    lines.append(f"  EF per +1yr extension:          {ef_per_year:>12,.1f} XFG")
    lines.append(f"  Monthly:                         {maint_yr/12:>12,.1f} XFG")
    lines.append(f"  Per epoch:                       {reward_XFG * 900:>12,.2f} XFG")
    lines.append("")

    total_fee_flow = annual_swap_fees_XFG or 0
    premium_flow = 0
    if annual_mints_XFG:
        premium_flow = annual_mints_XFG * (premium_pct / 100)
        total_fee_flow += premium_flow
        lines.append(f"  HEAT mint volume: {annual_mints_XFG:,.0f} XFG/yr × {premium_pct:.0f}% prem = {premium_flow:,.0f} XFG/yr")
    if annual_swap_fees_XFG:
        lines.append(f"  Swap fee revenue: {annual_swap_fees_XFG:,.0f} XFG/yr")
    if total_fee_flow > 0:
        lines.append(f"  Total flow to EF+YEM: {total_fee_flow:,.0f} XFG/yr")
        lines.append("")
        lines.append(f"  Burn Scalp %    EF gets/yr    Maint Coverage    +Ext/yr")
        lines.append(f"  {'─'*60}")
        for scalp_pct in [8, 20, 33, 50, 67, 75, 92, 100]:
            ef_from_total = total_fee_flow * (scalp_pct / 100)
            maint_cov = (ef_from_total / maint_yr) * 100
            yr_to_extend = ef_from_total / ef_per_year if ef_per_year else 0
            suff = " ✓" if maint_cov >= 100 else ""
            lines.append(f"  {scalp_pct:>4}%           {ef_from_total:>10,.1f}    {maint_cov:>7.1f}%{suff}            +{yr_to_extend:.2f}")

    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="Fuego Emission Timer + EF Maintenance Tracker")
    parser.add_argument("--rpc", help="Daemon RPC URL (e.g. http://127.0.0.1:17681)")
    parser.add_argument("--speed", type=int, default=DEFAULT_SPEED, help=f"Emission speed factor (default {DEFAULT_SPEED})")
    parser.add_argument("--reward", type=float, help="Current block reward in XFG (manual mode)")
    parser.add_argument("--ef", type=float, help="Eternal Flame balance in XFG (manual mode)")
    parser.add_argument("--generated", type=float, help="Already generated coins in XFG (manual mode)")
    parser.add_argument("--years", type=int, default=30, help="Projection years (default 30)")
    parser.add_argument("--fees", type=float, help="Annual swap fees in XFG (for YEM pct targets)")
    parser.add_argument("--mints", type=float, help="Annual HEAT mint volume in XFG (for premium calc)")
    parser.add_argument("--premium", type=float, default=5.0, help="HEAT premium percentage (default 5)")
    parser.add_argument("--compare", action="store_true", help="Show speed 18/19/20 comparison")
    args = parser.parse_args()

    if args.rpc:
        try:
            data = fetch_live(args.rpc)
            current_reward = data["base_reward_XFG"]
            ef = data["ef_XFG"]
            already_gen = data["already_generated_XFG"]
            height = data["height"]
            print(f"Live daemon  |  Height: {height:,}  |  Reward: {current_reward:.6f} XFG  |  EF: {ef:,.1f} XFG  |  Generated: {already_gen:,.1f} XFG\n")
        except Exception as e:
            print(f"RPC error: {e}", file=sys.stderr)
            sys.exit(1)
    elif args.reward is not None and args.ef is not None and args.generated is not None:
        current_reward = args.reward
        ef = args.ef
        already_gen = args.generated
    else:
        print("Interactive mode — enter current network values:")
        try:
            current_reward = float(input("Current block reward (XFG): [0.32] ") or "0.32")
            ef = float(input("Eternal Flame balance (XFG): [0] ") or "0")
            already_gen = float(input("Already generated (XFG): [4500000] ") or "4500000")
        except (EOFError, KeyboardInterrupt):
            print()
            sys.exit(0)

    rows = project_emission(already_gen, ef, current_reward, args.speed, args.years)
    print(format_table(rows, args.speed, current_reward, ef, already_gen))

    print(yem_targets(current_reward, ef, args.fees, args.mints, args.premium))

    if args.compare:
        print(speed_comparison(already_gen, ef, current_reward))


if __name__ == "__main__":
    main()
