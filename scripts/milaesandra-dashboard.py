#!/usr/bin/env python3
"""
Milæsandra Dashboard — Interactive Testnet Status Display
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Queries testnetd RPC endpoints and displays live protocol status:
HEAT supply, Hearth AMM, CD yield, Treasury, PI controller, activation state.
"""

import urllib.request, json, time, os, sys
from datetime import datetime

RPC = "http://localhost:28280"

def rpc(endpoint, method="GET", data=None):
    try:
        url = f"{RPC}/{endpoint}"
        req = urllib.request.Request(url, data=data.encode() if data else None,
            headers={"Content-Type": "application/json"} if data else {})
        with urllib.request.urlopen(req, timeout=5) as resp:
            return json.loads(resp.read())
    except Exception as e:
        return {"status": f"ERR: {e}"}

def fmt(val, decimals=2):
    if val is None: return "—"
    try:
        f = float(val)
        if f >= 1e6: return f"{f/1e6:.{decimals}f}M"
        if f >= 1e3: return f"{f/1e3:.{decimals}f}K"
        return f"{f:.{decimals}f}"
    except: return str(val)

def bold(s): return f"\033[1m{s}\033[0m"
def red(s): return f"\033[31m{s}\033[0m"
def green(s): return f"\033[32m{s}\033[0m"
def cyan(s): return f"\033[36m{s}\033[0m"
def yellow(s): return f"\033[33m{s}\033[0m"
def clear(): os.system('clear' 2>/dev/null or True)

while True:
    clear()
    info = rpc("getinfo")
    heat = rpc("heat_metrics")
    amm  = rpc("amm_pool_info")

    height = info.get("height", "?")
    status = info.get("status", "?")

    heat_sup = heat.get("heat_supply", 0)
    burned   = heat.get("burned_xfg", 0)
    rp_num   = heat.get("redemption_price_num", 0)
    rp_den   = heat.get("redemption_price_denom", 1)
    rp       = rp_num / max(rp_den, 1)
    treas    = heat.get("treasury_balance", 0)
    epoch_fees = heat.get("epoch_swap_fees", 0)

    rsv_xfg  = amm.get("reserve_xfg", 0)
    rsv_heat = amm.get("reserve_heat", 0)
    spot     = amm.get("spot_price", 0)
    lp_share = amm.get("total_lp_shares", 0)
    lp_fees  = amm.get("accumulated_lp_fees", 0)

    pool_ratio = rsv_xfg / max(rsv_heat, 1)
    cov = treas / max(0.2, 1) / max(heat_sup, 1) * 100
    activated = rp_num > 0 and rp_den > 0 and (rp_num / rp_den) <= 0.5  # band targets lower ratio than launch 0.2

    print(bold(cyan("╔══════════════════════════════════════════════════════════════╗")))
    print(bold(cyan("║")) + bold("  Milæsandra Dashboard — Testnet Live          ").center(58) + bold(cyan("║")))
    print(bold(cyan("╠══════════════════════════════════════════════════════════════╣")))
    print(bold(cyan("║")) + f"  {green('ONLINE') if status == 'OK' else red('DOWN')}  |  Height: {height}  |  {time.strftime('%H:%M:%S')} ".ljust(61) + bold(cyan("║")))
    print(bold(cyan("╠══════════════════════════════════════════════════════════════╣")))
    print(bold(cyan("║")) + bold("  HEAT Supply").ljust(60) + bold(cyan("║")))
    print(bold(cyan("║")) + f"  Supply: {fmt(heat_sup).rjust(30)} HEAT".ljust(61) + bold(cyan("║")))
    print(bold(cyan("║")) + f"  Burned XFG: {fmt(burned).rjust(26)} XFG".ljust(61) + bold(cyan("║")))
    print(bold(cyan("║")) + f"  Redemption: {rp:.6f} XFG/HEAT  (1 XFG = {1/rp:.0f} HEAT)".ljust(61) + bold(cyan("║")))
    act_state = green("ACTIVATED ($1.50-$2.50 band)") if activated else yellow("BOOTSTRAP (fixed 0.2)")
    print(bold(cyan("║")) + f"  Phase: {act_state}".ljust(61) + bold(cyan("║")))
    print(bold(cyan("╠══════════════════════════════════════════════════════════════╣")))
    print(bold(cyan("║")) + bold("  Hearth AMM Pool").ljust(60) + bold(cyan("║")))
    print(bold(cyan("║")) + f"  XFG Reserve:  {fmt(rsv_xfg).rjust(26)} XFG".ljust(61) + bold(cyan("║")))
    print(bold(cyan("║")) + f"  HEAT Reserve: {fmt(rsv_heat).rjust(26)} HEAT".ljust(61) + bold(cyan("║")))
    print(bold(cyan("║")) + f"  Pool Ratio:   {pool_ratio:.1f}:1 XFG/HEAT".ljust(61) + bold(cyan("║")))
    print(bold(cyan("║")) + f"  LP Shares:    {fmt(lp_share).rjust(30)}".ljust(61) + bold(cyan("║")))
    print(bold(cyan("║")) + f"  LP Fees:      {fmt(lp_fees).rjust(30)}".ljust(61) + bold(cyan("║")))
    print(bold(cyan("╠══════════════════════════════════════════════════════════════╣")))
    print(bold(cyan("║")) + bold("  Treasury & CD Yield").ljust(60) + bold(cyan("║")))
    print(bold(cyan("║")) + f"  Treasury:     {fmt(treas).rjust(30)} XFG".ljust(61) + bold(cyan("║")))
    print(bold(cyan("║")) + f"  CD Yield Pool:{fmt(epoch_fees).rjust(30)} XFG/epoch".ljust(61) + bold(cyan("║")))
    print(bold(cyan("║")) + f"  Coverage:     {cov:.0f}% (treasury can buy {cov:.0f}% of supply)".ljust(61) + bold(cyan("║")))
    print(bold(cyan("╠══════════════════════════════════════════════════════════════╣")))
    print(bold(cyan("║")) + bold("  CD Yield Decision:") + "".ljust(43) + bold(cyan("║")))
    if pool_ratio > 2.0:
        print(bold(cyan("║")) + f"  {yellow('ROUTING: 40% → Treasury (pool > 2:1)')}".ljust(61) + bold(cyan("║")))
    if pool_ratio > 4.0:
        print(bold(cyan("║")) + f"  {red('MINT MODE: Skipping pool (pool > 4:1)')}".ljust(61) + bold(cyan("║")))
    else:
        print(bold(cyan("║")) + f"  {green('BUY MODE: Buying HEAT from Hearth')}".ljust(61) + bold(cyan("║")))
    print(bold(cyan("║")) + f"  Rebalancer: {'ACTIVE' if pool_ratio > 3.0 else 'idle'} (triggers at > 3:1)".ljust(61) + bold(cyan("║")))
    print(bold(cyan("╚══════════════════════════════════════════════════════════════╝")))
    print()
    print(f"  Refreshing every 3s. Press Ctrl+C to exit.")
    time.sleep(3)
