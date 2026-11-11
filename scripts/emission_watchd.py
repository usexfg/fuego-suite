#!/usr/bin/env python3
"""
emission_watchd — Fuego emission monitoring daemon

Tracks: years-to-dust, maintenance burn target, EternalFlame pool,
         and EF extension (how many extra years EF has added to emission window).

Writes metrics to a JSON file on each poll. Optionally serves via HTTP.

Usage:
  python scripts/emission_watchd.py --rpc http://127.0.0.1:17681
  python scripts/emission_watchd.py --rpc http://127.0.0.1:17681 --interval 3600
  python scripts/emission_watchd.py --rpc http://127.0.0.1:17681 --serve :9090
"""

import json
import sys
import time
import math
import argparse
import urllib.request
import urllib.error
from datetime import datetime, timezone


COIN = 10_000_000
MONEY_SUPPLY = 80_000_088_000_008
BLOCKS_PER_DAY = 180
BLOCKS_PER_YEAR = BLOCKS_PER_DAY * 365
DUST_XFG = 0.001
DUST_ATOMIC = int(DUST_XFG * COIN)
DEFAULT_INTERVAL = 900  # 15 min


def rpc_call(url, path="/json_rpc", method="get_info", params=None, timeout=10):
    body = json.dumps({"jsonrpc": "2.0", "id": 1, "method": method, "params": params or {}}).encode()
    req = urllib.request.Request(url.rstrip("/") + path, data=body,
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read()).get("result", {})


def base_reward(already_atomic, ef_atomic, speed):
    osv = max(already_atomic - ef_atomic, 0)
    return (MONEY_SUPPLY - osv) >> speed


def exhaust_years(reward_atomic, speed):
    if reward_atomic <= DUST_ATOMIC:
        return 0.0
    hl_blocks = 2 ** (speed - 1)
    halvings = math.log2(reward_atomic / DUST_ATOMIC)
    return (hl_blocks * halvings) / BLOCKS_PER_YEAR


def ef_extension_years(already_atomic, ef_atomic, speed):
    """How many extra years EF has added to the emission window."""
    r_no = base_reward(already_atomic, 0, speed)
    r_yes = base_reward(already_atomic, ef_atomic, speed)
    if r_no <= 0:
        return 0.0
    e_no = exhaust_years(r_no, speed)
    e_yes = exhaust_years(r_yes, speed)
    return e_yes - e_no


def collect_metrics(rpc_url, speed):
    info = rpc_call(rpc_url)

    height = info.get("height", 0)
    ef_atomic = int(info.get("ethereal_xfg", 0))
    last_reward = int(info.get("last_block_reward", 0))

    # Try to get alreadyGenerated from block details
    try:
        top_hash = info.get("top_block_hash", "")
        block = rpc_call(rpc_url, method="f_block_details_json",
                         params={"hash": top_hash}) if top_hash else {}
        ag_str = block.get("alreadyGeneratedCoins", "0")
        already_atomic = int(ag_str) if isinstance(ag_str, str) else int(ag_str)
    except Exception:
        # Fallback: estimate from reward + EF
        remaining = last_reward << speed
        already_atomic = MONEY_SUPPLY - remaining + ef_atomic

    reward_atomic = base_reward(already_atomic, ef_atomic, speed)
    maint_yr = (reward_atomic / COIN) * BLOCKS_PER_YEAR

    return {
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "height": height,
        "speed": speed,
        "reward_xfg": round(reward_atomic / COIN, 6),
        "already_generated_xfg": round(already_atomic / COIN, 1),
        "eternal_flame_xfg": round(ef_atomic / COIN, 6),
        "remaining_pool_xfg": round((MONEY_SUPPLY - max(already_atomic - ef_atomic, 0)) / COIN, 1),
        "exhaust_years": round(exhaust_years(reward_atomic, speed), 2),
        "exhaust_years_no_ef": round(exhaust_years(base_reward(already_atomic, 0, speed), speed), 2),
        "ef_extension_years": round(ef_extension_years(already_atomic, ef_atomic, speed), 2),
        "maintenance_xfg_per_year": round(maint_yr, 1),
        "maintenance_xfg_per_epoch": round((reward_atomic / COIN) * 900, 2),
        "maintenance_xfg_per_day": round((reward_atomic / COIN) * BLOCKS_PER_DAY, 2),
        "ef_pct_of_supply": round((ef_atomic / MONEY_SUPPLY) * 100, 6),
    }


def serve_http(port, rpc_url, speed):
    from http.server import HTTPServer, BaseHTTPRequestHandler

    class MetricsHandler(BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path == "/metrics":
                try:
                    m = collect_metrics(rpc_url, speed)
                    lines = []
                    lines.append(f"# HELP fuego_emission_reward_xfg Current block reward")
                    lines.append(f"# TYPE fuego_emission_reward_xfg gauge")
                    lines.append(f"fuego_emission_reward_xfg {m['reward_xfg']}")
                    lines.append(f"# HELP fuego_emission_exhaust_years Years until reward reaches {DUST_XFG} XFG")
                    lines.append(f"# TYPE fuego_emission_exhaust_years gauge")
                    lines.append(f"fuego_emission_exhaust_years {m['exhaust_years']}")
                    lines.append(f"# HELP fuego_emission_exhaust_no_ef Years without EF")
                    lines.append(f"# TYPE fuego_emission_exhaust_no_ef gauge")
                    lines.append(f"fuego_emission_exhaust_no_ef {m['exhaust_years_no_ef']}")
                    lines.append(f"# HELP fuego_emission_ef_extension Years EF has extended emission")
                    lines.append(f"# TYPE fuego_emission_ef_extension gauge")
                    lines.append(f"fuego_emission_ef_extension {m['ef_extension_years']}")
                    lines.append(f"# HELP fuego_emission_maintenance_xfg_per_year Burn needed to maintain reward")
                    lines.append(f"# TYPE fuego_emission_maintenance_xfg_per_year gauge")
                    lines.append(f"fuego_emission_maintenance_xfg_per_year {m['maintenance_xfg_per_year']}")
                    lines.append(f"# HELP fuego_eternal_flame_xfg Total burned XFG in EF")
                    lines.append(f"# TYPE fuego_eternal_flame_xfg gauge")
                    lines.append(f"fuego_eternal_flame_xfg {m['eternal_flame_xfg']}")
                    lines.append(f"# HELP fuego_remaining_pool_xfg Remaining emission pool")
                    lines.append(f"# TYPE fuego_remaining_pool_xfg gauge")
                    lines.append(f"fuego_remaining_pool_xfg {m['remaining_pool_xfg']}")
                    self.send_response(200)
                    self.send_header("Content-Type", "text/plain")
                    self.end_headers()
                    self.wfile.write("\n".join(lines).encode())
                except Exception as e:
                    self.send_response(500)
                    self.end_headers()
                    self.wfile.write(f"# ERROR: {e}".encode())
            elif self.path == "/":
                self.send_response(200)
                self.send_header("Content-Type", "text/html")
                self.end_headers()
                try:
                    m = collect_metrics(rpc_url, speed)
                except Exception as e:
                    m = {"error": str(e)}
                html = f"""<html><head><title>Fuego Emission Watch</title><meta http-equiv="refresh" content="30">
<style>body{{font-family:monospace;background:#111;color:#0f0;padding:20px}}
.m{{display:inline-block;min-width:380px;margin:8px;padding:12px;border:1px solid #333;border-radius:6px}}
.v{{font-size:28px;font-weight:bold;color:#fff}}
.l{{font-size:12px;color:#666;text-transform:uppercase;letter-spacing:1px}}</style></head><body>
<h2>Fuego Emission Watch</h2>
<div class="m"><div class="l">Block Reward</div><div class="v">{m.get('reward_xfg','-')} XFG</div></div>
<div class="m"><div class="l">Exhaustion (to {DUST_XFG} XFG)</div><div class="v">{m.get('exhaust_years','-')} yrs</div></div>
<div class="m"><div class="l">EF Extension</div><div class="v">+{m.get('ef_extension_years','-')} yrs</div></div>
<div class="m"><div class="l">Eternal Flame</div><div class="v">{m.get('eternal_flame_xfg','-'):,.4f} XFG</div></div>
<div class="m"><div class="l">Maintenance / Year</div><div class="v">{m.get('maintenance_xfg_per_year','-'):,.1f} XFG</div></div>
<div class="m"><div class="l">Remaining Pool</div><div class="v">{m.get('remaining_pool_xfg','-'):,.1f} XFG</div></div>
<div class="m"><div class="l">Height</div><div class="v">{m.get('height','-'):,}</div></div>
<div class="m"><div class="l">Already Generated</div><div class="v">{m.get('already_generated_xfg','-'):,.1f} XFG</div></div>
</body></html>"""
                self.wfile.write(html.encode())
            else:
                self.send_response(404)
                self.end_headers()

    server = HTTPServer(("", port), MetricsHandler)
    print(f"Metrics HTTP server at http://0.0.0.0:{port}  (/metrics  /)")
    server.serve_forever()


def main():
    parser = argparse.ArgumentParser(description="Fuego Emission Watch Daemon")
    parser.add_argument("--rpc", required=True, help="Daemon JSON-RPC URL (e.g. http://127.0.0.1:17681)")
    parser.add_argument("--interval", type=int, default=DEFAULT_INTERVAL, help=f"Poll interval in seconds (default {DEFAULT_INTERVAL})")
    parser.add_argument("--serve", type=int, metavar="PORT", help="Start HTTP metrics server on PORT")
    parser.add_argument("--out", default="emission_metrics.json", help="Metrics output file (default emission_metrics.json)")
    parser.add_argument("--once", action="store_true", help="Run once and exit (no daemon)")
    parser.add_argument("--speed", type=int, default=20, help="Emission speed factor")
    args = parser.parse_args()

    if args.serve:
        import threading
        t = threading.Thread(target=serve_http, args=(args.serve, args.rpc, args.speed), daemon=True)
        t.start()

    while True:
        try:
            m = collect_metrics(args.rpc, args.speed)
            with open(args.out, "w") as f:
                json.dump(m, f, indent=2)

            ts = datetime.now(timezone.utc).strftime("%H:%M:%S")
            print(f"[{ts}] h={m['height']:,}  reward={m['reward_xfg']:.6f}  "
                  f"exhaust={m['exhaust_years']:.2f}y  ef_ext=+{m['ef_extension_years']:.2f}y  "
                  f"ef={m['eternal_flame_xfg']:,.2f}  maint={m['maintenance_xfg_per_year']:,.0f}/yr")
        except Exception as e:
            print(f"[{datetime.now(timezone.utc).strftime('%H:%M:%S')}] RPC error: {e}", file=sys.stderr)

        if args.once:
            break
        time.sleep(args.interval)


if __name__ == "__main__":
    main()
