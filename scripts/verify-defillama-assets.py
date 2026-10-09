#!/usr/bin/env python3
"""Live identity audit for the DeFiLlama counterparty asset matrix.

Checks, in order:
  1. every pair in the C++ protocol catalog (SwapPairCatalog.h) is covered by the
     matrix, with a matching id, settlement asset and decimals
  2. the matrix agrees with Valise's SwapPairSdk enum (ids and tickers)
  3. every priced entry resolves on the live batched endpoint, with a matching
     symbol, finite positive price, acceptable confidence and a provider
     timestamp, with stale data reported separately
  4. entries without a verified identifier are explicitly unpriced

Exit code 0 means the asset identities agree with code and the live endpoint.
It does not certify current quote freshness or production feed integration.
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
MAX_TIMESTAMP = (1 << 63) - 1

failures = []
notes = []


def fail(msg):
    failures.append(msg)


def is_finite_number(value):
    if type(value) not in (int, float):
        return False
    try:
        return math.isfinite(value)
    except OverflowError:
        return False


def is_timestamp(value):
    return type(value) is int and 0 < value <= MAX_TIMESTAMP


def validate_numeric_policy(policy):
    if not isinstance(policy, dict):
        return ["policies must be an object"]
    errors = []
    for field in ("maxProviderAgeSeconds", "maxClockSkewSeconds"):
        value = policy.get(field)
        if type(value) is not int or not 0 <= value <= MAX_TIMESTAMP:
            errors.append("policies.%s must be a nonnegative int64" % field)
    confidence = policy.get("minConfidence")
    if not is_finite_number(confidence) or not 0 <= confidence <= 1:
        errors.append("policies.minConfidence must be finite in [0,1]")
    if "crossCheckTolerance" in policy:
        tolerance = policy["crossCheckTolerance"]
        if not is_finite_number(tolerance) or not 0 <= tolerance < 1:
            errors.append("policies.crossCheckTolerance must be finite in [0,1)")
    return errors


def validate_feed_entry(entry, expected_symbol, policy, now):
    errors = validate_numeric_policy(policy)
    if not is_timestamp(now):
        errors.append("current time must be a positive int64")
    if errors:
        return errors, None
    if not isinstance(entry, dict):
        return ["feed entry must be an object"], None

    symbol = entry.get("symbol")
    if not isinstance(symbol, str) or symbol.upper() != expected_symbol.upper():
        errors.append("returned symbol %r, expected %s" % (symbol, expected_symbol))

    price = entry.get("price")
    if not is_finite_number(price) or price <= 0:
        errors.append("non-finite or non-positive price %r" % price)

    if "confidence" in entry:
        confidence = entry["confidence"]
        if not is_finite_number(confidence) or not 0 <= confidence <= 1:
            errors.append("confidence %r must be finite in [0,1]" % confidence)
        elif confidence < policy["minConfidence"]:
            errors.append("confidence %r below %r" % (confidence, policy["minConfidence"]))

    timestamp = entry.get("timestamp")
    if not is_timestamp(timestamp):
        errors.append("provider timestamp must be a positive int64")
        return errors, None

    age = now - timestamp
    if age < -policy["maxClockSkewSeconds"]:
        errors.append("provider timestamp is %ds in the future" % -age)
    stale_age = age if age > policy["maxProviderAgeSeconds"] else None
    return errors, stale_age


# One row of XFG_SWAP_PAIR_CATALOG: name, id, key, asset, display, family, chainId,
# minBlockMs, maxBlockMs, decimals, support, txEnvelope, icon, color.
_CATALOG_ROW = re.compile(
    r'^\s*X\(\s*([A-Z_0-9]+)\s*,\s*(\d+)\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,'
    r'\s*([A-Z0-9_]+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*([A-Z_]+)\s*,',
    re.MULTILINE)


def parse_pair_catalog(path):
    """id -> {name, asset, decimals, support} from the append-only protocol catalog."""
    text = open(path, encoding="utf-8").read()
    found = {}
    for (name, raw, _key, asset, _display, _family, _chain, _min, _max, decimals, support) in _CATALOG_ROW.findall(text):
        found[int(raw)] = {"name": name, "asset": asset, "decimals": int(decimals), "support": support}
    return found


def parse_cpp_pairs(path):
    """Legacy fallback: the SwapPair enum as it was declared in SwapTypes.h before the catalog."""
    text = open(path, encoding="utf-8").read()
    start = text.index("enum class SwapPair")
    end = text.index("};", start)
    body = text[start:end]
    found = {}
    for name, raw in re.findall(r"([A-Z_0-9]+)\s*=\s*(\d+)\s*,?", body):
        found[int(raw)] = {"name": name, "asset": None, "decimals": None, "support": None}
    return found


def parse_provider_age(path):
    """PricePolicy::maxProviderAgeSec default, so the matrix cannot contradict the code."""
    text = open(path, encoding="utf-8").read()
    m = re.search(r"maxProviderAgeSec\s*=\s*(\d+)\s*;", text)
    return int(m.group(1)) if m else None


def parse_dart_pairs(path):
    text = open(path, encoding="utf-8").read()
    start = text.index("enum SwapPairSdk")
    end = text.index("}", start)
    body = text[start:end]
    found = {}
    for name, raw, ticker in re.findall(r"\b([a-z][a-z0-9]*)\((\d+),\s*'([^']*)'", body):
        found[int(raw)] = (name, ticker)
    return found


def parse_cpp_catalog(path):
    text = open(path, encoding="utf-8").read()
    block = text[text.index("kAssets[] = {"):]
    block = block[:block.index("};")]
    assets = {}
    row = re.compile(
        r'\{"([A-Z0-9]+)",\s*"([^"]*)",\s*"([^"]*)",\s*"([^"]*)",\s*([0-9]+)ULL,\s*"([^"]*)",\s*(true|false),\s*(true|false)\}')
    for sym, dl, pyth, exp, div, divtext, priced, execable in row.findall(block):
        assets[sym] = {
            "defiLlamaId": dl,
            "pythSymbol": pyth,
            "expectedFeedSymbol": exp,
            "atomicDivisor": int(div),
            "atomicDivisorText": divtext,
            "priced": priced == "true",
            "executable": execable == "true",
        }
    binds = {}
    for pair, sym in re.findall(r"\{SwapPair::([A-Z_0-9]+),\s*&kAssets\[(\d+)\]\}", text):
        binds[pair] = sym
    ordered = re.findall(r'\{"([A-Z0-9]+)",\s*"[^"]*",\s*"[^"]*",\s*"[^"]*",\s*[0-9]+ULL', block)
    for pair, idx in binds.items():
        binds[pair] = ordered[int(idx)]
    return assets, binds


def parse_registered_pairs(path):
    text = open(path, encoding="utf-8").read()
    return set(re.findall(r"registerChain\(SwapPair::([A-Z_0-9]+)", text))


def parse_feed_meta(path):
    text = open(path, encoding="utf-8").read()
    block = text[text.index("kFeedMeta[] = {"):]
    block = block[:block.index("};")]
    out = {}
    # Column order must match AssetDescriptor: symbol, defiLlamaId, pythSymbol,
    # pythId, expectedFeedSymbol.
    for sym, dl, pyth, pid, exp in re.findall(
            r'\{"([A-Z0-9]+)",\s*"([^"]*)",\s*"([^"]*)",\s*"([^"]*)",\s*"([^"]*)"\}', block):
        out[sym] = {"defiLlamaId": dl, "pythSymbol": pyth, "expectedFeedSymbol": exp,
                    "pythId": pid}
    return out


def fetch(ids):
    url = ENDPOINT + ",".join(ids)
    req = urllib.request.Request(url, headers={"Accept": "application/json"})
    with urllib.request.urlopen(req, timeout=TIMEOUT) as resp:
        if resp.status != 200:
            raise RuntimeError("unexpected HTTP status %s" % resp.status)
        return json.load(resp)


def main():
    failures.clear()
    notes.clear()
    matrix = json.load(open(MATRIX, encoding="utf-8"))
    pairs = matrix["pairs"]
    pol = matrix["policies"]
    policy_errors = validate_numeric_policy(pol)
    if policy_errors:
        for error in policy_errors:
            print("FAIL  %s" % error)
        return 1

    catalog_path = os.path.join(XFGO, "src", "SwapDaemon", "SwapPairCatalog.h")
    if os.path.exists(catalog_path):
        cpp = parse_pair_catalog(catalog_path)
    else:
        cpp = parse_cpp_pairs(os.path.join(XFGO, "src", "SwapDaemon", "SwapTypes.h"))
        notes.append("SwapPairCatalog.h not found; fell back to the legacy SwapTypes.h enum, so asset and decimals checks are skipped")
    if not cpp:
        fail("parsed no pairs from the C++ pair catalog; the parser no longer matches the source")

    matrix_ids = {p["id"] for p in pairs}
    if matrix_ids != set(cpp):
        fail("matrix ids %s != C++ catalog ids %s (missing from matrix: %s, missing from C++: %s)"
             % (sorted(matrix_ids), sorted(cpp), sorted(set(cpp) - matrix_ids), sorted(matrix_ids - set(cpp))))
    # The only pair whose protocol ticker differs from the asset it actually settles in.
    settlement_alias = {"TON": "GRAM"}
    for p in pairs:
        entry = cpp.get(p["id"])
        if entry is None:
            continue
        if entry["name"] != p["enum"]:
            fail("pair %d: matrix says %s, C++ says %s" % (p["id"], p["enum"], entry["name"]))
        if entry["asset"] is not None:
            want = settlement_alias.get(entry["asset"], entry["asset"])
            if want != p["settlementAsset"]:
                fail("pair %d (%s): C++ catalog settles in %s but the matrix says %s"
                     % (p["id"], p["enum"], want, p["settlementAsset"]))
            if p["atomicDivisor"] != "1e%d" % entry["decimals"]:
                fail("pair %d (%s): C++ catalog has %d decimals but the matrix divisor is %s"
                     % (p["id"], p["enum"], entry["decimals"], p["atomicDivisor"]))

    price_feed = os.path.join(XFGO, "src", "SwapDaemon", "PriceFeed.h")
    if os.path.exists(price_feed):
        cpp_age = parse_provider_age(price_feed)
        if cpp_age is None:
            fail("could not read PricePolicy::maxProviderAgeSec from PriceFeed.h")
        elif cpp_age != pol["maxProviderAgeSeconds"]:
            fail("matrix policies.maxProviderAgeSeconds is %d but PricePolicy::maxProviderAgeSec is %d"
                 % (pol["maxProviderAgeSeconds"], cpp_age))

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

    cat_path = os.path.join(XFGO, "src", "SwapDaemon", "AssetCatalog.cpp")
    if os.path.exists(cat_path):
        # AssetCatalog.cpp no longer restates pair bindings; it derives them from
        # SWAP_PAIR_CATALOG and keeps only feed metadata keyed by settlement asset.
        feed_meta = parse_feed_meta(cat_path)
        for pr in pairs:
            sym = "GRAM" if pr["enum"] == "TON" else pr["settlementAsset"]
            m2 = feed_meta.get(sym)
            if m2 is None:
                fail("AssetCatalog.cpp has no feed metadata for settlement asset %s (pair %d %s)"
                     % (sym, pr["id"], pr["enum"]))
                continue
            if m2["defiLlamaId"] != (pr["defiLlamaId"] or ""):
                fail("pair %d (%s): AssetCatalog defiLlamaId %r != matrix %r"
                     % (pr["id"], pr["enum"], m2["defiLlamaId"], pr["defiLlamaId"]))
            if m2["expectedFeedSymbol"].upper() != pr["expectSymbol"].upper():
                fail("pair %d (%s): AssetCatalog expectedFeedSymbol %r != matrix expectSymbol %r"
                     % (pr["id"], pr["enum"], m2["expectedFeedSymbol"], pr["expectSymbol"]))
        notes.append("AssetCatalog.cpp feed metadata cross-checked for %d assets" % len(feed_meta))
    else:
        notes.append("AssetCatalog.cpp not found at %s, skipped feed metadata cross-check" % cat_path)

    registered = parse_registered_pairs(os.path.join(XFGO, "src", "SwapDaemon", "SwapDaemon.cpp"))
    matrix_registered = {p["enum"] for p in pairs if p.get("chainClient") == "wired-config-gated"}
    matrix_unwired = {p["enum"] for p in pairs if p.get("chainClient") == "implemented-not-wired"}
    matrix_absent = {p["enum"] for p in pairs if p.get("chainClient") == "no-client"}
    matrix_adapter = {p["enum"] for p in pairs if p.get("chainClient") == "generic-adapter"}
    if any(e["support"] is not None for e in cpp.values()):
        catalog_adapter = {e["name"] for e in cpp.values() if e["support"] == "ADAPTER"}
        catalog_staged = {e["name"] for e in cpp.values() if e["support"] == "STAGED"}
        if matrix_adapter != catalog_adapter:
            fail("generic-adapter drift: matrix %s vs catalog support=ADAPTER %s"
                 % (sorted(matrix_adapter), sorted(catalog_adapter)))
        if matrix_unwired | matrix_absent != catalog_staged:
            fail("staged drift: matrix implemented-not-wired/no-client %s vs catalog support=STAGED %s"
                 % (sorted(matrix_unwired | matrix_absent), sorted(catalog_staged)))
    if matrix_registered != registered:
        only_matrix = sorted(matrix_registered - registered)
        only_code = sorted(registered - matrix_registered)
        fail("chainClient drift: matrix says wired %s, but SwapDaemon.cpp registers %d pairs (matrix-only %s, code-only %s)"
             % (sorted(matrix_registered), len(registered), only_matrix, only_code))
    for p in pairs:
        if p.get("chainClient") != "implemented-not-wired":
            continue
        src = p.get("chainClientSource")
        if not src or not os.path.exists(os.path.join(XFGO, src)):
            fail("pair %d (%s) is marked implemented-not-wired but %s is missing" % (p["id"], p["enum"], src))
        if p["enum"] in registered:
            fail("pair %d (%s) is marked implemented-not-wired but SwapDaemon.cpp does register it" % (p["id"], p["enum"]))
        notes.append("pair %d (%s) has an implemented client (%s) that is never registered, so it is priced and buildable but not executable"
                     % (p["id"], p["enum"], src))
    unwired = [p for p in pairs if p.get("chainClient") == "wired-config-gated"
               and p["defiLlamaId"] is None]
    for p in unwired:
        notes.append("pair %d (%s) has a wired chain client but no DeFiLlama id, so its client is wired but its reference price is unavailable" % (p["id"], p["enum"]))

    distinct = sorted({p["defiLlamaId"] for p in pairs if p["defiLlamaId"]})
    try:
        body = fetch(distinct)
    except (urllib.error.URLError, RuntimeError, TimeoutError, ValueError) as exc:
        fail("live fetch failed: %s" % exc)
        body = {"coins": {}}

    if not isinstance(body, dict) or not isinstance(body.get("coins"), dict):
        fail("live response must contain a coins object")
        coins = {}
    else:
        coins = body["coins"]
    clock = time.time()
    if not is_finite_number(clock) or not 0 < clock <= MAX_TIMESTAMP:
        print("FAIL  current time must be finite and positive within int64")
        return 1
    now = int(clock)
    if not is_timestamp(now):
        print("FAIL  current time must be a positive int64")
        return 1

    priced = 0
    for p in pairs:
        cid = p["defiLlamaId"]
        status = p["status"]
        if cid is None:
            if status != "unpriced":
                fail("pair %d (%s) has no DeFiLlama id but status is %s" % (p["id"], p["enum"], status))
            else:
                notes.append("pair %d (%s) is unpriced: no DeFiLlama id is verified for %s" % (p["id"], p["enum"], p["settlementAsset"]))
            continue
        entry = coins.get(cid)
        if entry is None:
            fail("pair %d (%s): %s did not resolve on the live endpoint" % (p["id"], p["enum"], cid))
            continue
        priced += 1

        errors, stale_age = validate_feed_entry(entry, p["expectSymbol"], pol, now)
        for error in errors:
            fail("pair %d (%s): %s %s" % (p["id"], p["enum"], cid, error))
        if stale_age is not None:
            notes.append("pair %d (%s): provider data is %ds old, the %ds staleness gate would reject this pair right now" % (p["id"], p["enum"], stale_age, pol["maxProviderAgeSeconds"]))

    print("distinct DeFiLlama ids requested: %d" % len(distinct))
    print("pairs resolving live: %d of %d" % (priced, len(pairs)))
    print("chain clients wired: %d, generic-adapter: %d, implemented-not-wired: %d, no client: %d"
          % (len(matrix_registered), len(matrix_adapter), len(matrix_unwired), len(matrix_absent)))
    for note in notes:
        print("NOTE  %s" % note)
    if failures:
        print("")
        for f in failures:
            print("FAIL  %s" % f)
        print("\n%d gate(s) failed" % len(failures))
        return 1
    print("\nidentity checks passed; freshness and executable swaps are separate gates")
    return 0


if __name__ == "__main__":
    sys.exit(main())
