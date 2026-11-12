#!/usr/bin/env python3
"""Report what DeFiLlama and CoinGecko return for candidate price-feed ids.

    scripts/probe-feed-ids.py coingecko:peaq-2 coingecko:peaq ...

For each id: DeFiLlama's current price entry (symbol, price, timestamp,
confidence) and, for coingecko: ids, CoinGecko's own metadata (name, symbol,
homepage, platforms) so the asset's identity can be compared with the swap
pair's settlement asset. Prints facts only; deciding to price a pair stays a
reviewed change to AssetCatalog.cpp and the asset matrix.
"""

import json
import sys
import time
import urllib.error
import urllib.request

LLAMA = "https://coins.llama.fi/prices/current/"
GECKO = ("https://api.coingecko.com/api/v3/coins/%s?localization=false&tickers=false"
         "&market_data=false&community_data=false&developer_data=false&sparkline=false")
TIMEOUT = 20


def get_json(url):
    request = urllib.request.Request(url, headers={"User-Agent": "fuego-feed-probe"})
    with urllib.request.urlopen(request, timeout=TIMEOUT) as response:
        return json.loads(response.read().decode("utf-8"))


def llama_report(ids):
    try:
        coins = get_json(LLAMA + ",".join(ids)).get("coins", {})
    except (urllib.error.URLError, ValueError, TimeoutError) as exc:
        print("DeFiLlama request failed: %s" % exc)
        return
    now = int(time.time())
    for cid in ids:
        entry = coins.get(cid)
        if entry is None:
            print("DeFiLlama %-28s does not resolve" % cid)
            continue
        age = now - int(entry.get("timestamp", 0))
        print("DeFiLlama %-28s symbol=%s price=%s confidence=%s age=%ds"
              % (cid, entry.get("symbol"), entry.get("price"), entry.get("confidence"), age))


def gecko_report(ids):
    for cid in ids:
        if not cid.startswith("coingecko:"):
            continue
        slug = cid.split(":", 1)[1]
        try:
            coin = get_json(GECKO % slug)
        except (urllib.error.URLError, ValueError, TimeoutError) as exc:
            print("CoinGecko %-28s request failed: %s" % (slug, exc))
            time.sleep(15)
            continue
        homepage = [h for h in coin.get("links", {}).get("homepage", []) if h]
        print("CoinGecko %-28s name=%s symbol=%s homepage=%s asset_platform=%s platforms=%s"
              % (slug, coin.get("name"), coin.get("symbol"), homepage, coin.get("asset_platform_id"),
                 json.dumps(coin.get("platforms", {}))))
        time.sleep(15)  # keyless CoinGecko allows a few requests per minute


def main(argv):
    ids = argv[1:]
    if not ids:
        print(__doc__)
        return 2
    llama_report(ids)
    gecko_report(ids)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
