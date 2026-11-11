// Copyright (c) 2017-2026, Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You can redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation. Fuego includes elements written
// by third parties. See file labeled LICENSE for more details.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

// Parser tests for the CoinGecko /simple/price reader.
//
// CoinGecko replaced Pyth as the second source. Unlike Pyth, which answered on-chain
// with data 47-641 days stale for the 9 ids that had not reverted, CoinGecko prices
// all 31 catalog ids in a single request. That is the whole point of batching: the
// keyless tier allows 3 requests per minute (measured 2026-10-04: 200,200,200,429,
// 429,429,429,429), so one call per poll is a hard requirement, not an optimisation.
//
// Two behaviours are specific to this feed and are pinned here:
//   * CoinGecko returns no confidence, so the confidence floor is skipped.
//   * CoinGecko returns no per-asset symbol, so identity is proven by key.

#include "CoinGeckoFeed.h"

#include <cstdio>
#include <string>

using namespace XfgSwap;

static int g_failures = 0;
static int g_checks = 0;

static void check(bool cond, const std::string& what) {
  ++g_checks;
  if (!cond) { ++g_failures; std::printf("FAIL  %s\n", what.c_str()); }
}

// Real response shape captured from
// /api/v3/simple/price?ids=ethereum,bitcoin&vs_currencies=usd&include_last_updated_at=true
static std::string two(int64_t ts) {
  return std::string("{\"ethereum\":{\"usd\":2583.12,\"last_updated_at\":") + std::to_string(ts) +
         "},\"bitcoin\":{\"usd\":83296,\"last_updated_at\":" + std::to_string(ts) + "}}";
}

int main() {
  const PricePolicy pol;
  const int64_t now = 1791423568;  // matches the capture
  const CoinGeckoWanted wanted = coinGeckoWanted();

  // Coverage must be at least the DeFiLlama set: same ids, one source.
  check(wanted.size() == 31, "31 priced assets have a coingecko id");
  {
    bool hasEth = false, hasGram = false;
    for (const auto& w : wanted) {
      if (w.first == "ethereum") hasEth = true;
      // GRAM is the priceable asset for the staged TON pair.
      if (w.first == "the-open-network") hasGram = true;
      check(w.first.find("coingecko:") == std::string::npos, "the prefix is stripped: " + w.first);
    }
    check(hasEth, "ethereum is requested");
    check(hasGram, "the-open-network (GRAM) is requested");
  }

  // ---- happy path
  {
    FeedResult r = parseCoinGeckoBody(two(now - 80), wanted, pol, now);
    check(r.transportOk, "a well-formed body is transport-ok");
    check(r.feedName == "coingecko", "feed name recorded");
    auto it = r.assets.find("ETH");
    check(it != r.assets.end() && it->second.ok, "ETH parsed ok");
    if (it != r.assets.end()) {
      check(it->second.observation.price > 2583.0 && it->second.observation.price < 2583.3,
            "usd price read back");
      check(it->second.observation.providerTimestamp == now - 80, "last_updated_at read back");
      check(it->second.observation.fetchTimestamp == now, "fetch timestamp is our own clock");
      // CoinGecko publishes no confidence, so we must not invent one.
      check(!it->second.observation.confidenceSupplied, "no confidence is claimed");
    }
    check(r.assets.count("BTC") == 1, "BTC parsed from the same batch");
  }

  // ---- a missing last_updated_at must be refused: the policy needs a provider clock.
  {
    FeedResult r = parseCoinGeckoBody(
      "{\"ethereum\":{\"usd\":2583.12}}", wanted, pol, now);
    check(r.assets.count("ETH") == 1 && !r.assets.at("ETH").ok,
          "an entry without last_updated_at is refused");
    check(r.assets.at("ETH").reason.find("last_updated_at") != std::string::npos,
          "and says so");
  }

  // ---- malformed and hostile bodies
  {
    FeedResult r = parseCoinGeckoBody("", wanted, pol, now);
    check(!r.transportOk, "empty body refused");
    r = parseCoinGeckoBody("{not json", wanted, pol, now);
    check(!r.transportOk, "malformed JSON refused");
    r = parseCoinGeckoBody("[]", wanted, pol, now);
    check(!r.transportOk, "top-level array refused");
  }

  // ---- per-entry failures must not poison the batch
  {
    const std::string ts = std::to_string(now);
    const std::string mixed =
      std::string(R"({"ethereum":{"usd":2583.12,"last_updated_at":)") + ts +
      R"(},"bitcoin":{"usd":-1,"last_updated_at":)" + ts +
      R"(},"solana":{"last_updated_at":)" + ts +
      R"(},"avalanche-2":{"usd":0,"last_updated_at":)" + ts + "}}";
    FeedResult r = parseCoinGeckoBody(mixed, wanted, pol, now);
    check(r.transportOk, "a batch with bad entries is still transport-ok");
    check(r.assets.count("ETH") == 1 && r.assets.at("ETH").ok,
          "the good entry survives its bad neighbours");
    check(r.assets.count("BTC") == 1 && !r.assets.at("BTC").ok, "negative price refused");
    check(r.assets.count("SOL") == 1 && !r.assets.at("SOL").ok, "entry with no price refused");
    check(r.assets.count("AVAX") == 1 && !r.assets.at("AVAX").ok, "zero price refused");
  }

  // ---- an id we never requested must not become an asset.
  {
    FeedResult r = parseCoinGeckoBody(
      "{\"dogecoin\":{\"usd\":0.09,\"last_updated_at\":" + std::to_string(now) + "}}",
      CoinGeckoWanted(), pol, now);
    check(r.assets.empty(), "no wanted ids means no assets");
  }

  // ---- staleness is enforced by the shared policy, downstream of the parser.
  {
    FeedResult r = parseCoinGeckoBody(two(now - 5000), wanted, pol, now);
    check(r.assets.at("ETH").ok, "an old entry still parses");
    AssetPrice out;
    check(evaluateAsset(*assetForSymbol("ETH"), r, nullptr, pol, now, out) == QuoteStatus::STALE,
          "the policy then rejects it as stale");
  }

  // ---- a fresh entry is usable end to end as a cross-check against DeFiLlama.
  {
    FeedResult cg = parseCoinGeckoBody(two(now - 30), wanted, pol, now);
    // A DeFiLlama-shaped primary carrying the same prices for ETH.
    FeedResult dl;
    dl.feedName = "defillama";
    dl.transportOk = true;
    AssetPrice ap;
    ap.ok = true;
    ap.observation.price = 2584.0;                 // ~0.03% apart
    ap.observation.providerTimestamp = now - 30;
    ap.observation.fetchTimestamp = now;
    dl.assets["ETH"] = ap;

    // requireCorroboration is off by default, so agreeing feeds must collapse
    // SINGLE_SOURCE into plain OK; that collapse is what proves corroboration happened.
    PairQuote loose = buildPairQuote(1, dl, &cg, 0.158, pol, now);
    check(loose.status == QuoteStatus::OK, "two agreeing sources produce a plain OK");
    check(loose.status != QuoteStatus::SINGLE_SOURCE, "the quote is no longer single-source");
    check(loose.rateNum > 0, "and it carries a usable rate");

    PricePolicy strict = pol;
    strict.requireCorroboration = true;
    PairQuote tight = buildPairQuote(1, dl, &cg, 0.158, strict, now);
    check(tight.status == QuoteStatus::OK, "with corroboration required, agreement still passes");
    std::printf("      (agreeing feeds -> %s, rateNum=%llu)\n", quoteStatusToString(tight.status),
                (unsigned long long)tight.rateNum);
  }

  // ---- and a wide disagreement is caught rather than averaged.
  {
    FeedResult cg = parseCoinGeckoBody(two(now - 30), wanted, pol, now);
    FeedResult dl;
    dl.feedName = "defillama";
    dl.transportOk = true;
    AssetPrice ap;
    ap.ok = true;
    ap.observation.price = 9000.0;                 // wildly different from 2583
    ap.observation.providerTimestamp = now - 30;
    ap.observation.fetchTimestamp = now;
    dl.assets["ETH"] = ap;

    PairQuote q = buildPairQuote(1, dl, &cg, 0.158, pol, now);
    check(q.status == QuoteStatus::DISPUTED, "a 3x disagreement is refused");
    check(q.rateNum == 0, "and no rate is published for a disputed quote");
  }

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}