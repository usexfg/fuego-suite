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

#include "DefiLlamaFeed.h"

#include <cstdio>
#include <string>

using namespace XfgSwap;

static int g_failures = 0;
static int g_checks = 0;

static void check(bool cond, const std::string& what) {
  ++g_checks;
  if (!cond) { ++g_failures; std::printf("FAIL  %s\n", what.c_str()); }
}

static std::string body(const std::string& inner, long long ts) {
  return "{\"coins\":{" + inner + "},\"ts\":" + std::to_string(ts) + "}";
}

static std::string eth(long long ts, const std::string& extra = "\"confidence\":0.99") {
  return body("\"coingecko:ethereum\":{\"price\":2700.5,\"symbol\":\"ETH\",\"timestamp\":"
              + std::to_string(ts) + (extra.empty() ? "" : "," + extra) + "}", ts);
}

int main() {
  const PricePolicy pol;
  const int64_t now = 1791000000;
  const DefiLlamaWanted wanted = defiLlamaWanted();

  check(!wanted.empty(), "catalog yields assets to request");
  bool hasEth = false, hasGram = false;
  for (const auto& w : wanted) {
    if (w.first == "coingecko:ethereum") hasEth = true;
    if (w.first == "coingecko:the-open-network") hasGram = true;
  }
  check(hasEth, "ethereum id is requested");
  check(hasGram, "GRAM id is requested for the TON pair");

  // happy path
  {
    FeedResult r = parseDefiLlamaBody(eth(now - 10), wanted, pol, now);
    check(r.transportOk, "well-formed body is transport-ok");
    check(r.feedName == "defillama", "feed name recorded");
    auto it = r.assets.find("ETH");
    check(it != r.assets.end() && it->second.ok, "ETH parsed ok");
    if (it != r.assets.end()) {
      check(it->second.observation.price > 2700.0 && it->second.observation.price < 2701.0,
            "ETH price read back");
      check(it->second.observation.providerTimestamp == now - 10, "provider timestamp read back");
      check(it->second.observation.fetchTimestamp == now, "fetch timestamp is our own clock");
      check(it->second.observation.confidenceSupplied, "confidence captured when supplied");
      check(it->second.observation.confidence > 0.98, "confidence value read back");
    }
  }

  // malformed / hostile bodies
  {
    FeedResult r = parseDefiLlamaBody("", wanted, pol, now);
    check(!r.transportOk && !r.transportReason.empty(), "empty body refused");

    r = parseDefiLlamaBody("{not json", wanted, pol, now);
    check(!r.transportOk, "malformed JSON refused");

    r = parseDefiLlamaBody("[]", wanted, pol, now);
    check(!r.transportOk, "top-level array refused");

    r = parseDefiLlamaBody("{\"nope\":{}}", wanted, pol, now);
    check(!r.transportOk, "missing coins object refused");
  }

  // per-entry failures must not poison the whole response
  {
    FeedResult r = parseDefiLlamaBody(body(
        "\"coingecko:ethereum\":{\"price\":2700,\"symbol\":\"ETH\",\"timestamp\":" + std::to_string(now) + "},"
        "\"coingecko:zcash\":{\"symbol\":\"ZEC\",\"timestamp\":" + std::to_string(now) + "},"
        "\"coingecko:dash\":{\"price\":-5,\"symbol\":\"DASH\",\"timestamp\":" + std::to_string(now) + "},"
        "\"coingecko:dogecoin\":{\"price\":0.09,\"timestamp\":" + std::to_string(now) + "}", now), wanted, pol, now);
    check(r.transportOk, "response with bad entries is still transport-ok");
    check(r.assets.at("ETH").ok, "the good entry survives its bad neighbours");
    check(!r.assets.at("ZEC").ok && r.assets.at("ZEC").reason.find("price") != std::string::npos,
          "missing price refused with a reason");
    check(!r.assets.at("DASH").ok, "negative price refused");
    check(!r.assets.at("DOGE").ok, "entry without a symbol refused rather than priced blind");
  }

  // the symbol guard: an id that resolves to a different asset must be dropped
  {
    FeedResult r = parseDefiLlamaBody(body(
        "\"coingecko:ethereum\":{\"price\":0.2,\"symbol\":\"ARB\",\"timestamp\":" + std::to_string(now) + "}", now),
        wanted, pol, now);
    check(!r.assets.at("ETH").ok, "symbol mismatch refuses the entry");
    check(r.assets.at("ETH").reason.find("mismatch") != std::string::npos, "mismatch is explained");
  }

  // GRAM is the expected symbol for the TON pair's asset, so GRAM must pass
  {
    FeedResult r = parseDefiLlamaBody(body(
        "\"coingecko:the-open-network\":{\"price\":1.51,\"symbol\":\"GRAM\",\"timestamp\":"
        + std::to_string(now) + "}", now), wanted, pol, now);
    check(r.assets.at("GRAM").ok, "GRAM is accepted for the TON settlement asset");
  }

  // confidence absent is not the same as confidence zero
  {
    FeedResult r = parseDefiLlamaBody(eth(now - 10, ""), wanted, pol, now);
    check(r.assets.at("ETH").ok, "entry without confidence still parses");
    check(!r.assets.at("ETH").observation.confidenceSupplied, "absent confidence is flagged as absent");
  }

  // staleness is a policy gate, not a parser gate, but the parser must carry the stamp
  {
    FeedResult r = parseDefiLlamaBody(eth(now - 20000), wanted, pol, now);
    check(r.assets.at("ETH").ok, "a very old entry still parses");
    AssetPrice out;
    check(evaluateAsset(*assetForSymbol("ETH"), r, nullptr, pol, now, out) == QuoteStatus::STALE,
          "policy then rejects it as stale");
  }

  // unrequested ids in the response are ignored rather than treated as assets
  {
    FeedResult r = parseDefiLlamaBody(body(
        "\"coingecko:ethereum\":{\"price\":2700,\"symbol\":\"ETH\",\"timestamp\":" + std::to_string(now) + "},"
        "\"coingecko:dogecoin-shibe\":{\"price\":1,\"symbol\":\"SHIB\",\"timestamp\":" + std::to_string(now) + "}", now),
        wanted, pol, now);
    check(r.assets.size() == 1, "only requested ids become assets");
    check(r.assets.count("SHIB") == 0, "an unrequested id is ignored");
  }

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}