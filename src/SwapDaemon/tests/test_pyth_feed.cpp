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

// Parser tests for the on-chain Pyth reader.
//
// The fixtures below are REAL payloads captured from
// eth_call getPriceUnsafe(0xff61..0dace) at Ethereum mainnet contract
// 0x4305FB66699C3B2702D4d05CF36551390A4c69C6 on 2026-10-04, not hand-written
// shapes. That matters: the captured ETH/USD update was already 47.7 days old, and
// 14 of the 23 catalog ids reverted outright. These tests therefore pin the
// behaviour that actually matters -- a stale or unpublished feed must be refused,
// not quoted.

#include "PythFeed.h"

#include <cstdio>
#include <string>

using namespace XfgSwap;

static int g_failures = 0;
static int g_checks = 0;

static void check(bool cond, const std::string& what) {
  ++g_checks;
  if (!cond) { ++g_failures; std::printf("FAIL  %s\n", what.c_str()); }
}

// ABI word = 32 bytes = 64 hex chars. Encoding from decoded numbers rather than a
// hand-copied hex string: an earlier hand-typed literal was 254 chars instead of 256
// and the parser correctly rejected it, which is exactly the failure mode to avoid.
static std::string encodeWords(int64_t price, uint64_t conf, int32_t expo, int64_t publishTime) {
  auto word = [](uint64_t v) {
    char b[65];
    std::snprintf(b, sizeof(b), "%064llx", (unsigned long long)v);
    return std::string(b);
  };
  return std::string("0x") + word(static_cast<uint64_t>(price)) + word(conf) +
         word(static_cast<uint64_t>(static_cast<int64_t>(expo))) +
         word(static_cast<uint64_t>(publishTime));
}

// Real capture from eth_call getPriceUnsafe(ETH/USD) at Ethereum mainnet contract
// 0x4305FB66699C3B2702D4d05CF36551390A4c69C6 on 2026-10-04: the four words decoded to
// price=190533915588 conf=106428258 expo=-8 publishTime=1786985890, i.e. 47.7 days old.
static const int64_t kRealEthPublish = 1786985890;
static const int64_t kRealEthPrice   = 190533915588LL;
static const uint64_t kRealEthConf   = 106428258ULL;
static const int32_t kRealEthExpo    = -8;

static std::string entry(int64_t id, const std::string& resultHex) {
  return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) + ",\"result\":\"" + resultHex + "\"}";
}

int main() {
  const PricePolicy pol;
  const int64_t now = 1791105201;  // 2026-10-04, matches the capture
  const PythWanted wanted = pythWanted();

  // The catalog must carry real 32-byte ids.
  check(wanted.size() == 23, "23 assets carry a Pyth price id");
  bool hasEth = false;
  for (const auto& w : wanted) {
    if (w.first == "ff61491a931112ddf1bd8147cd1b641375f79f5825126d665480874634fd0ace") hasEth = true;
    check(w.first.size() == 64, "price id is 32 bytes for " + w.second->symbol);
  }
  check(hasEth, "the documented ETH/USD id is present");

  // ---- Real stale capture must be refused, not quoted.
  {
    PythWanted one;
    one.emplace_back("ff61491a931112ddf1bd8147cd1b641375f79f5825126d665480874634fd0ace", assetForSymbol("ETH"));
    FeedResult r = parsePythBatch("[" + entry(0, encodeWords(kRealEthPrice, kRealEthConf,
                                                              kRealEthExpo, kRealEthPublish)) + "]",
                                  one, pol, now);
    check(r.transportOk, "a valid capture is transport-ok");
    auto it = r.assets.find("ETH");
    check(it != r.assets.end(), "the asset is reported");
    if (it != r.assets.end()) {
      check(!it->second.ok, "a 47-day-old on-chain update is refused");
      check(it->second.reason.find("older than") != std::string::npos,
            "and it is refused for age, not for some other reason");
    }
  }

  // ---- A fresh update of the same feed is accepted.
  {
    PythWanted one;
    one.emplace_back("ff61491a931112ddf1bd8147cd1b641375f79f5825126d665480874634fd0ace", assetForSymbol("ETH"));
    // 2700.50 with expo -8
    FeedResult r = parsePythBatch("[" + entry(0, encodeWords(270050000000LL, 106428258ULL, -8, now - 20)) + "]", one, pol, now);
    auto it = r.assets.find("ETH");
    check(it != r.assets.end() && it->second.ok, "a fresh on-chain update is accepted");
    if (it != r.assets.end() && it->second.ok) {
      check(it->second.observation.price > 2699.0 && it->second.observation.price < 2702.0,
            "expo is applied to recover the USD price");
      check(it->second.observation.providerTimestamp == now - 20, "publishTime comes from the chain");
      check(it->second.observation.confidenceSupplied, "Pyth confidence is captured");
    }
  }

  // ---- A negative expo and a large price still decode.
  {
    PythWanted one;
    one.emplace_back("ff61491a931112ddf1bd8147cd1b641375f79f5825126d665480874634fd0ace", assetForSymbol("ETH"));
    FeedResult r = parsePythBatch("[" + entry(0, encodeWords(8500881691900LL, 106428258ULL, -8, now - 5)) + "]", one, pol, now);
    auto it = r.assets.find("ETH");
    check(it != r.assets.end() && it->second.ok, "a 85008 BTC-scale value decodes");
    if (it != r.assets.end() && it->second.ok) {
      check(it->second.observation.price > 84000.0 && it->second.observation.price < 86000.0,
            "large price round-trips");
    }
  }

  // ---- A reverted call is surfaced per asset, not fatal to the batch.
  {
    FeedResult r = parsePythBatch(
      "[{\"jsonrpc\":\"2.0\",\"id\":0,\"error\":{\"code\":3,\"message\":\"execution reverted\"}}]",
      wanted, pol, now);
    check(r.transportOk, "a reverted call is still transport-ok");
    check(r.assets.size() == 1, "only the reverted id is reported");
    check(!r.assets.begin()->second.ok, "the reverted asset is not ok");
    check(r.assets.begin()->second.reason.find("never published") != std::string::npos,
          "the revert is explained as an unpublished id");
  }

  // ---- Hostile / malformed bodies.
  {
    FeedResult r = parsePythBatch("", wanted, pol, now);
    check(!r.transportOk, "empty body refused");
    r = parsePythBatch("{not json", wanted, pol, now);
    check(!r.transportOk, "malformed JSON refused");
    r = parsePythBatch("{\"not\":\"an array\"}", wanted, pol, now);
    check(!r.transportOk && r.transportReason.find("batch array") != std::string::npos,
          "a non-array body is refused");
  }

  // ---- A short return is refused rather than read past the end.
  {
    PythWanted one;
    one.emplace_back("ff61491a931112ddf1bd8147cd1b641375f79f5825126d665480874634fd0ace", assetForSymbol("ETH"));
    FeedResult r = parsePythBatch(
      "[{\"jsonrpc\":\"2.0\",\"id\":0,\"result\":\"0x0000000000000001\"}]", one, pol, now);
    check(!r.assets.at("ETH").ok, "a truncated return is refused");
    check(r.assets.at("ETH").reason.find("short") != std::string::npos, "and is reported as short");
  }

  // ---- An id outside the wanted list must not create an asset.
  {
    FeedResult r = parsePythBatch("[" + entry(0, encodeWords(100LL, 106428258ULL, -8, now - 5)) + "]",
                                  PythWanted(), pol, now);
    check(r.assets.empty(), "no wanted ids means no assets");
  }

  // ---- Out-of-range batch ids are ignored.
  {
    FeedResult r = parsePythBatch("[" + entry(9999, encodeWords(kRealEthPrice, kRealEthConf,
                                                                kRealEthExpo, kRealEthPublish)) + "]",
                                  wanted, pol, now);
    check(r.transportOk && r.assets.empty(), "an out-of-range batch id is ignored");
  }

  // ---- An unconfigured feed refuses to fetch rather than guessing an endpoint.
  {
    PythFeed feed;
    check(!feed.configured(), "a fresh PythFeed is not configured");
    FeedResult r = feed.fetch(pol);
    check(!r.transportOk && r.transportReason.find("not configured") != std::string::npos,
          "an unconfigured feed reports why rather than calling something");
  }

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}