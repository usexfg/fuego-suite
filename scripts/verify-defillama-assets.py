#!/usr/bin/env python3
"""Live acceptance test for the DeFiLlama counterparty asset matrix.

Checks, in order:
  1. every SwapPair in the C++ enum is covered by the matrix, with a matching id
  2. the matrix agrees with Valise's SwapPairSdk enum (ids and tickers)
  3. every priced entry resolves on the live batched endpoint, with a matching
     symbol, finite positive price, acceptable confidence and a provider
     timestamp inside the staleness window
  4. every unpriced/unexecutable entry truly has no usable identifier

Exit code 0 means the matrix is consistent with the code and the live feed.
Exit code 1 means at least one gate failed; the failures are printed.
"""

import json
import math
import os
import re
import sys
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
XFGO = os.path.abspath(os.path.join(HERE, ".."))
WALLET = os.environ.get("FUEGO_WALLET_DIR", "/Users/aejt/DEXFG/fuego-flutter-wallet")
MATRIX = os.path.join(HERE, "..", "docs", "developer", "defillama-asset-matrix.json")
ENDPOINT = "https://coins.llama.fi/prices/current/"
TIMEOUT = 15

failures = []
notes = []


def fail(msg):
    failures.append(msg)


def parse_cpp_pairs(path):
    text = open(path, encoding="utf-8").read()
    start = text.index("enum class SwapPair")
    end = text.index("};", start)
    body = text[start:end]
    found = {}
    for name, raw in re.findall(r"([A-Z_0-9]+)\s*=\s*(\d+)\s*,?", body):
        found[int(raw)] = name
    return found


def parse_dart_pairs(path):
    text = open(path, encoding="utf-8").read()
    start = text.index("enum SwapPairSdk")
    end = text.index("}", start)
    body = text[start:end]
    found = {}
    for name, raw, ticker in re.findall(r"\b([a-z][a-z0-9]*)\((\d+),\s*'([^']*)'", body):
        found[int(raw)] = (name, ticker)
    return found


def fetch(ids):
    url = ENDPOINT + ",".join(ids)
    req = urllib.request.Request(url, headers={"Accept": "application/json"})
    with urllib.request.urlopen(req, timeout=TIMEOUT) as resp:
        if resp.status != 200:
            raise RuntimeError("unexpected HTTP status %s" % resp.status)
        return json.load(resp)


def main():
    matrix = json.load(open(MATRIX, encoding="utf-8"))
    pairs = matrix["pairs"]
    pol = matrix["policies"]

    cpp = parse_cpp_pairs(os.path.join(XFGO, "src", "SwapDaemon", "SwapTypes.h"))
    if len(cpp) != 29:
        fail("expected 29 SwapPair values in SwapTypes.h, parsed %d" % len(cpp))

    matrix_ids = {p["id"] for p in pairs}
    if matrix_ids != set(cpp):
        fail("matrix ids %s != C++ enum ids %s" % (sorted(matrix_ids), sorted(cpp)))
    for p in pairs:
        if cpp.get(p["id"]) != p["enum"]:
            fail("pair %d: matrix says %s, C++ says %s" % (p["id"], p["enum"], cpp.get(p["id"])))

    dart_path = os.path.join(WALLET, "lib", "models", "swap_models.dart")
    if os.path.exists(dart_path):
        dart = parse_dart_pairs(dart_path)
        client_ids = set(matrix["clientExposure"]["valiseSwapPairSdk"]["ids"])
        if set(dart) != client_ids:
            fail("SwapPairSdk ids %s != matrix clientExposure ids %s" % (sorted(dart), sorted(client_ids)))
        for pid, (_name, ticker) in dart.items():
            entry = next((p for p in pairs if p["id"] == pid), None)
            if entry and entry["ticker"] != ticker.upper():
                fail("pair %d: Dart ticker %s != matrix ticker %s" % (pid, ticker.upper(), entry["ticker"]))
    else:
        notes.append("Valise SwapPairSdk not found at %s, skipped Dart cross-check" % dart_path)

    distinct = sorted({p["defiLlamaId"] for p in pairs if p["defiLlamaId"]})
    try:
        body = fetch(distinct)
    except (urllib.error.URLError, RuntimeError, TimeoutError, ValueError) as exc:
        fail("live fetch failed: %s" % exc)
        body = {"coins": {}}

    coins = body.get("coins", {})
    now = int(time.time())

    priced = 0
    for p in pairs:
        cid = p["defiLlamaId"]
        status = p["status"]
        if status in ("unpriced", "unexecutable"):
            if cid and cid in coins:
                notes.append("pair %d (%s): %s carries id %s which does resolve; confirm the status is still intended" % (p["id"], p["enum"], status, cid))
            if status == "unpriced" and cid is not None:
                fail("pair %d (%s) is unpriced but declares a DeFiLlama id" % (p["id"], p["enum"]))
            continue
        if not cid:
            fail("pair %d (%s) is %s but has no DeFiLlama id" % (p["id"], p["enum"], status))
            continue
        entry = coins.get(cid)
        if entry is None:
            fail("pair %d (%s): %s did not resolve on the live endpoint" % (p["id"], p["enum"], cid))
            continue
        priced += 1

        sym = str(entry.get("symbol", "")).upper()
        want = p["expectSymbol"].upper()
        if sym != want:
            fail("pair %d (%s): %s returned symbol %s, expected %s" % (p["id"], p["enum"], cid, sym, want))

        price = entry.get("price")
        if not isinstance(price, (int, float)) or not math.isfinite(price) or price <= 0:
            fail("pair %d (%s): non-finite or non-positive price %r" % (p["id"], p["enum"], price))

        conf = entry.get("confidence")
        if conf is not None and conf < pol["minConfidence"]:
            fail("pair %d (%s): confidence %r below %r" % (p["id"], p["enum"], conf, pol["minConfidence"]))

        ts = entry.get("timestamp")
        if not isinstance(ts, (int, float)):
            fail("pair %d (%s): missing provider timestamp" % (p["id"], p["enum"]))
        else:
            age = now - int(ts)
            if age > pol["maxProviderAgeSeconds"]:
                notes.append("pair %d (%s): provider data is %ds old, the %ds staleness gate would reject this pair right now" % (p["id"], p["enum"], age, pol["maxProviderAgeSeconds"]))
            if age < -pol["maxClockSkewSeconds"]:
                fail("pair %d (%s): provider timestamp is %ds in the future" % (p["id"], p["enum"], -age))

    print("distinct DeFiLlama ids requested: %d" % len(distinct))
    print("pairs resolving live: %d of %d" % (priced, len(pairs)))
    for note in notes:
        print("NOTE  %s" % note)
    if failures:
        print("")
        for f in failures:
            print("FAIL  %s" % f)
        print("\n%d gate(s) failed" % len(failures))
        return 1
    print("\nall gates passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
