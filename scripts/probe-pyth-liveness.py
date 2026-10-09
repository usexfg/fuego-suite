#!/usr/bin/env python3
"""Probe whether Pyth can currently serve as a live second price source.

Run this before enabling the Pyth feed. It reports, per path:

  * Hermes (classic)  -- /v2/price_feeds/latest by id
  * Hermes Stable      -- same route on the stable host
  * on-chain           -- eth_call getPriceUnsafe against the deployed contract

The on-chain check is the one that matters: a Pyth contract that has stopped being
written to still answers getPriceUnsafe with whatever it last stored, so a green
transport there can still be quoting months-old prices. This script prints the age of
each on-chain update for exactly that reason.

Exit code 0 means at least one path returned a genuinely fresh price.

Usage:
    python3 scripts/probe-pyth-liveness.py [--rpc URL] [--contract 0x...]
"""

import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

DEFAULT_RPC = "https://ethereum-rpc.publicnode.com"
DEFAULT_CONTRACT = "0x4305FB66699C3B2702D4d05CF36551390A4c69C6"
HERMES = "https://hermes.pyth.network"
HERMES_STABLE = "https://hermes-stable.pyth.network"
SELECTOR = "96834ad3"          # keccak256("getPriceUnsafe(bytes32)")[0:4]
FRESH_SECONDS = 600             # must match PricePolicy::maxProviderAgeSec
TIMEOUT = 25

HERE = os.path.dirname(os.path.abspath(__file__))
XFGO = os.path.dirname(HERE)


def http_get(url):
    req = urllib.request.Request(
        url, headers={"Accept": "application/json", "User-Agent": "xfg-swapd-pyth-probe/1"})
    with urllib.request.urlopen(req, timeout=TIMEOUT) as r:
        return r.status, r.read().decode()


def http_post(url, payload):
    req = urllib.request.Request(
        url, data=json.dumps(payload).encode(),
        headers={"Content-Type": "application/json", "User-Agent": "xfg-swapd-pyth-probe/1"})
    with urllib.request.urlopen(req, timeout=TIMEOUT) as r:
        return json.loads(r.read().decode())


def catalog_pyth_ids():
    """asset symbol -> 32-byte price id, from the C++ catalog."""
    path = os.path.join(XFGO, "src", "SwapDaemon", "AssetCatalog.cpp")
    text = open(path).read()
    block = text[text.index("kFeedMeta[] = {"):]
    block = block[:block.index("\n};")]
    import re
    out = {}
    for sym, pid in re.findall(r'\{"([A-Z0-9]+)",\s*"[^"]*",\s*"[^"]*",\s*"([0-9a-f]*)"', block):
        if len(pid) == 64:
            out[sym] = pid
    return out


def probe_hermes(host, ids):
    if not ids:
        return "no ids"
    q = urllib.parse.quote("[" + ",".join(ids) + "]")
    try:
        status, body = http_get("%s/v2/price_feeds/latest?ids=%s" % (host, q))
    except urllib.error.HTTPError as e:
        return "HTTP %s: %s" % (e.code, e.read().decode()[:80])
    except Exception as e:
        return "unreachable: %s" % type(e).__name__
    try:
        data = json.loads(body)
    except Exception:
        return "HTTP %s, unparseable body (%d bytes)" % (status, len(body))
    if not isinstance(data, list) or not data:
        return "HTTP %s, empty list" % status
    # A working price route carries a price per feed; a metadata route does not.
    first = data[0]
    if "price" in first or "attributes" in first and "price" in first.get("attributes", {}):
        return "HTTP %s, %d feed(s) with prices" % (status, len(data))
    return ("HTTP %s, %d feed(s) but NO price field -- this route is metadata only"
            % (status, len(data)))


def signed64(v):
    return v - (1 << 64) if v >= (1 << 63) else v


def signed32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v >= (1 << 31) else v


def probe_onchain(rpc, contract, ids):
    if not ids:
        return None
    reqs = [{"jsonrpc": "2.0", "id": i, "method": "eth_call",
             "params": [{"to": contract, "data": "0x" + SELECTOR + pid}, "latest"]}
            for i, (_, pid) in enumerate(ids.items())]
    try:
        out = http_post(rpc, reqs)
    except Exception as e:
        return "unreachable: %s (%s)" % (type(e).__name__, str(e)[:60])
    if isinstance(out, dict):
        out = [out]
    rows, now = [], int(time.time())
    for i, (sym, _) in enumerate(ids.items()):
        o = next((x for x in out if x.get("id") == i), None)
        if o is None or "error" in o:
            rows.append((sym, None, None, "reverted"))
            continue
        hexs = o.get("result", "")[2:]
        if len(hexs) < 256:
            rows.append((sym, None, None, "short return %dB" % (len(hexs) // 2)))
            continue
        w = [int(hexs[j:j + 64], 16) for j in (0, 64, 128, 192)]
        price = signed64(w[0]) * (10.0 ** signed32(w[2]))
        rows.append((sym, price, w[3], "ok"))
    return rows, now


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rpc", default=DEFAULT_RPC)
    ap.add_argument("--contract", default=DEFAULT_CONTRACT)
    args = ap.parse_args()

    ids = catalog_pyth_ids()
    print("catalog: %d assets carry a Pyth price id" % len(ids))
    if not ids:
        print("no ids -> nothing to probe")
        return 1
    sample = list(ids.items())[:3]
    qids = [p for _, p in sample]

    print()
    print("Hermes (classic) /v2/price_feeds/latest :")
    print("   ", probe_hermes(HERMES, qids))
    print("Hermes Stable /v2/price_feeds/latest :")
    print("   ", probe_hermes(HERMES_STABLE, qids))

    print()
    print("on-chain eth_call getPriceUnsafe at %s :" % args.contract)
    res = probe_onchain(args.rpc, args.contract, ids)
    if isinstance(res, str):
        print("   ", res)
        return 1

    rows, now = res
    fresh = 0
    print("   %-7s %18s  %-21s %s" % ("asset", "on-chain USD", "publishTime(UTC)", "age"))
    import datetime
    for sym, price, pt, status in rows:
        if status != "ok":
            print("   %-7s %18s  %-21s %s" % (sym, "-", "-", status))
            continue
        age = now - pt
        verdict = "FRESH" if age <= FRESH_SECONDS else "STALE"
        if age <= FRESH_SECONDS:
            fresh += 1
        print("   %-7s %18.4f  %-21s %6.1fd %s"
              % (sym, price, str(datetime.datetime.fromtimestamp(pt, datetime.UTC)), age / 86400.0, verdict))

    print()
    reverted = sum(1 for _, _, _, s in rows if s == "reverted")
    print("on-chain: %d ok, %d reverted, %d fresh (<=%ds)"
          % (len(rows) - reverted, reverted, fresh, FRESH_SECONDS))
    if fresh == 0:
        print()
        print("VERDICT: no Pyth path is serving a fresh price. Keep the Pyth feed")
        print("         unconfigured. DeFiLlama remains the only live source, and the")
        print("         quote policy stays permissive on a single source, so trading")
        print("         continues without a cross-check.")
        return 1
    print()
    print("VERDICT: %d asset(s) fresh on-chain -- Pyth can be enabled." % fresh)
    return 0


if __name__ == "__main__":
    sys.exit(main())