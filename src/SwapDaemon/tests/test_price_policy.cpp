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

#include "PriceFeed.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

using namespace XfgSwap;

static int g_failures = 0;
static int g_checks = 0;

static void check(bool cond, const std::string& what) {
  ++g_checks;
  if (!cond) { ++g_failures; std::printf("FAIL  %s\n", what.c_str()); }
}

static FeedResult makeFeed(const std::string& name,
                           const std::string& symbol,
                           double price,
                           int64_t providerTs,
                           int64_t fetchTs,
                           double confidence,
                           bool confSupplied) {
  FeedResult f;
  f.feedName = name;
  f.transportOk = true;
  AssetPrice ap;
  ap.ok = true;
  ap.observation.price = price;
  ap.observation.providerTimestamp = providerTs;
  ap.observation.fetchTimestamp = fetchTs;
  ap.observation.confidence = confidence;
  ap.observation.confidenceSupplied = confSupplied;
  f.assets[symbol] = ap;
  return f;
}

int main() {
  const PricePolicy pol;
  const int64_t now = 1791000000;

  check(pol.maxProviderAgeSec == 600, "policy provider age is 600s (two 300s buckets)");
  check(pol.crossCheckTolerance == 0.02, "cross-check tolerance 2%");

  // ── rate direction: referenceRate is whole XFG per one whole counterparty coin
  {
    RateResult r = computeReferenceRate(2700.0, 0.158);
    check(r.ok, "computeReferenceRate ok for valid inputs");
    const double expected = 2700.0 / 0.158;
    check(std::fabs(r.referenceRate - expected) < 1e-9, "referenceRate == counterpartyUsd / xfgUsd");
    check(r.rateNum == static_cast<uint64_t>(std::llround(expected * 1e7)), "rateNum == rate * 1e7");

    RateResult inv = computeReferenceRate(0.158, 2700.0);
    check(inv.ok && inv.referenceRate < 1.0, "raising xfgUsd lowers XFG-per-coin (inverse holds)");

    check(!computeReferenceRate(0.0, 0.158).ok, "zero counterpartyUsd rejected");
    check(!computeReferenceRate(2700.0, 0.0).ok, "zero xfgUsd rejected");
    check(!computeReferenceRate(std::numeric_limits<double>::quiet_NaN(), 0.158).ok, "NaN counterparty rejected");
    check(!computeReferenceRate(std::numeric_limits<double>::infinity(), 0.158).ok, "infinite counterparty rejected");
    check(!computeReferenceRate(2700.0, -1.0).ok, "negative xfgUsd rejected");
  }

  // ── overflow at 1e7 scaling
  {
    RateResult r = computeReferenceRate(2700.0, 0.158);
    check(r.ok, "ordinary rate survives scaling");

    double huge = 1e300;
    check(!computeReferenceRate(huge, 0.158).ok, "absurd counterparty price overflows 1e7 scaling and is refused");

    double tiny = 1e-12;
    RateResult t = computeReferenceRate(tiny, 0.158);
    check(!t.ok, "rate underflowing to zero rateNum is refused");

    check(rateToNum(0.0) == 0, "rateToNum(0) == 0");
    check(rateToNum(-5.0) == 0, "rateToNum(negative) == 0");
    double back = 0.0;
    check(rateFromNum(r.rateNum, back), "rateFromNum round-trips");
    check(std::fabs(back - r.referenceRate) < 1e-6, "rateFromNum recovers the rate within 1e-6");
  }

  // ── two-sided guard, symmetric +/-20%
  {
    const double ref = 1000.0;
    check(withinRateGuard(1000.0, ref), "reference itself is inside the guard");
    check(withinRateGuard(ref / 1.2, ref), "exactly at the lower bound is accepted");
    check(withinRateGuard(ref / 0.8, ref), "exactly at the upper bound is accepted");
    check(!withinRateGuard(ref / 1.2 * 0.999, ref), "just below the lower bound is refused");
    check(!withinRateGuard(ref / 0.8 * 1.001, ref), "just above the upper bound is refused");
    check(withinRateGuard(ref / 1.1, ref), "-9% deviation accepted");
    check(withinRateGuard(ref / 0.9, ref), "+11% deviation accepted");
    check(!withinRateGuard(ref / 2.0, ref), "-50% deviation refused");
    check(!withinRateGuard(ref * 2.0, ref), "+100% deviation refused");
    check(!withinRateGuard(0.0, ref), "zero proposed rate refused");
    check(!withinRateGuard(ref, 0.0), "zero reference rate refuses everything");
    check(!withinRateGuard(std::numeric_limits<double>::quiet_NaN(), ref), "NaN proposed refused");
  }

  // ── catalog: 29 pairs, six share ETH, SIA unexecutable
  {
    std::vector<PairAssetBinding> b = allBindings();
    check(b.size() == 29, "catalog binds 29 pairs");

    int eth = 0, unpriced = 0, unexec = 0;
    for (const auto& x : b) {
      if (x.asset == nullptr) continue;
      if (x.asset->symbol == "ETH") ++eth;
      if (!x.asset->priced) ++unpriced;
      if (!x.asset->executable) ++unexec;
    }
    check(eth == 7, "seven pairs settle in ETH (Ethereum itself plus ARB, BASE, ROBINHOOD, BOB, UNICHAIN, OPTIMISM)");
    check(unpriced == 2, "exactly two assets are unpriced (KMD, GLEEC)");
    check(unexec == 1, "exactly one asset is unexecutable (SC, 1e24)");

    const AssetDescriptor* sc = assetForSymbol("SC");
    check(sc != nullptr && sc->atomicDivisor == 0, "SC divisor is 0 rather than a lying value");
    check(sc != nullptr && sc->atomicDivisorText == "1e24", "SC divisor text records the true 1e24");
    check(sc != nullptr && !sc->executable, "SC is marked unexecutable");

    check(assetForSymbol("KMD") != nullptr && !assetForSymbol("KMD")->priced, "KMD unpriced");
    check(assetForSymbol("GRAM") != nullptr && assetForSymbol("GRAM")->defiLlamaId == "coingecko:the-open-network",
          "TON pair settles in GRAM via coingecko:the-open-network");
    check(assetForSymbol("NOPE") == nullptr, "unknown symbol returns nullptr");

    check(distinctSymbolsForFeed("defillama").size() == 21, "21 distinct DeFiLlama assets");
    check(distinctSymbolsForFeed("pyth").size() == 17, "17 Pyth-corroborated assets");
  }

  // ── observation validation
  {
    AssetPrice out;
    const AssetDescriptor* btc = assetForSymbol("BTC");
    check(btc != nullptr, "BTC descriptor present");

    FeedResult fresh = makeFeed("defillama", "BTC", 84000.0, now - 5, now - 2, 0.99, true);
    check(evaluateAsset(*btc, fresh, nullptr, pol, now, out) == QuoteStatus::SINGLE_SOURCE,
          "one feed answering yields SINGLE_SOURCE, still quotable");

    FeedResult stale = makeFeed("defillama", "BTC", 84000.0, now - 5000, now - 2, 0.99, true);
    check(evaluateAsset(*btc, stale, nullptr, pol, now, out) == QuoteStatus::STALE,
          "old provider stamp is STALE");

    FeedResult future = makeFeed("defillama", "BTC", 84000.0, now + 100000, now - 2, 0.99, true);
    check(evaluateAsset(*btc, future, nullptr, pol, now, out) == QuoteStatus::STALE,
          "far-future provider stamp is STALE");

    FeedResult lowConf = makeFeed("defillama", "BTC", 84000.0, now - 5, now - 2, 0.42, true);
    check(evaluateAsset(*btc, lowConf, nullptr, pol, now, out) == QuoteStatus::LOW_CONFIDENCE,
          "confidence 0.42 is LOW_CONFIDENCE");

    FeedResult noConf = makeFeed("defillama", "BTC", 84000.0, now - 5, now - 2, 0.0, false);
    check(evaluateAsset(*btc, noConf, nullptr, pol, now, out) == QuoteStatus::SINGLE_SOURCE,
          "absent confidence is not penalised");

    FeedResult negative = makeFeed("defillama", "BTC", -1.0, now - 5, now - 2, 0.99, true);
    check(evaluateAsset(*btc, negative, nullptr, pol, now, out) == QuoteStatus::NO_DATA,
          "negative price is NO_DATA");

    FeedResult missing;
    missing.feedName = "defillama";
    missing.transportOk = true;
    check(evaluateAsset(*btc, missing, nullptr, pol, now, out) == QuoteStatus::NO_DATA,
          "asset absent from a reachable feed is NO_DATA");

    const AssetDescriptor* kmd = assetForSymbol("KMD");
    check(evaluateAsset(*kmd, fresh, nullptr, pol, now, out) == QuoteStatus::UNPRICED,
          "KMD is UNPRICED regardless of feed health");

    // cross-check
    FeedResult agree = makeFeed("pyth", "BTC", 84050.0, now - 5, now - 2, 0.0, false);
    check(evaluateAsset(*btc, fresh, &agree, pol, now, out) == QuoteStatus::OK,
          "two feeds within tolerance corroborate -> OK");
    FeedResult disagree = makeFeed("pyth", "BTC", 90000.0, now - 5, now - 2, 0.0, false);
    check(evaluateAsset(*btc, fresh, &disagree, pol, now, out) == QuoteStatus::DISPUTED,
          "two feeds far apart are DISPUTED");
    FeedResult justInside = makeFeed("pyth", "BTC", 84500.0, now - 5, now - 2, 0.0, false);
    check(evaluateAsset(*btc, fresh, &justInside, pol, now, out) == QuoteStatus::OK,
          "0.6% disagreement is inside the 2% tolerance");

    PricePolicy strict = pol;
    strict.requireCorroboration = true;
    check(evaluateAsset(*btc, fresh, nullptr, strict, now, out) == QuoteStatus::SINGLE_SOURCE,
          "requireCorroboration refuses single-source");
    check(evaluateAsset(*btc, fresh, &agree, strict, now, out) == QuoteStatus::OK,
          "requireCorroboration accepts corroborated");
  }

  // ── pair quotes
  {
    FeedResult primary = makeFeed("defillama", "ETH", 2700.0, now - 5, now - 2, 0.99, true);
    FeedResult secondary = makeFeed("pyth", "ETH", 2701.0, now - 5, now - 2, 0.0, false);

    PairQuote q = buildPairQuote(1, primary, &secondary, 0.158, pol, now);
    check(q.status == QuoteStatus::OK, "ETH pair quote is OK");
    check(q.counterpartyUsdOk && std::fabs(q.counterpartyUsd - 2700.0) < 1e-9, "counterpartyUsd carried through");
    check(q.xfgUsdOk && std::fabs(q.xfgUsd - 0.158) < 1e-12, "xfgUsd carried from Hearth, not the feed");
    check(q.referenceRateOk, "reference rate computed");
    check(q.rateNum > 0, "rateNum produced");
    check(q.managedOffersAllowed && q.newSwapAllowed, "fresh corroborated quote allows managed offers and swaps");
    check(q.settlementAsset == "ETH", "settlement asset is ETH");

    PairQuote arb = buildPairQuote(4, primary, &secondary, 0.158, pol, now);
    check(arb.status == QuoteStatus::OK && arb.settlementAsset == "ETH",
          "ARB pair prices off ETH, not the ARB governance token");

    PairQuote noHearth = buildPairQuote(1, primary, &secondary, 0.0, pol, now);
    check(noHearth.status == QuoteStatus::NO_DATA, "missing Hearth price blocks the quote");
    check(!noHearth.managedOffersAllowed && !noHearth.newSwapAllowed, "missing Hearth price blocks funding");

    FeedResult sia = makeFeed("defillama", "SC", 0.00101, now - 5, now - 2, 0.99, true);
    PairQuote siaQ = buildPairQuote(17, sia, nullptr, 0.158, pol, now);
    check(siaQ.status == QuoteStatus::UNEXECUTABLE, "SIA priced but UNEXECUTABLE on the 1e24 divisor");
    check(!siaQ.managedOffersAllowed && !siaQ.newSwapAllowed, "SIA cannot fund despite a live price");

    FeedResult kmd;
    kmd.feedName = "defillama";
    kmd.transportOk = true;
    PairQuote kmdQ = buildPairQuote(6, kmd, nullptr, 0.158, pol, now);
    check(kmdQ.status == QuoteStatus::UNPRICED, "KMD pair is UNPRICED");
    check(!kmdQ.newSwapAllowed, "unpriced pair cannot start a swap");

    FeedResult tonPrimary = makeFeed("defillama", "GRAM", 1.51, now - 5, now - 2, 0.99, true);
    FeedResult tonSecondary = makeFeed("pyth", "GRAM", 1.51, now - 5, now - 2, 0.0, false);
    PairQuote tonQ = buildPairQuote(27, tonPrimary, &tonSecondary, 0.158, pol, now);
    check(tonQ.status == QuoteStatus::OK && tonQ.settlementAsset == "GRAM", "TON pair prices in GRAM");

    FeedResult dead;
    dead.feedName = "defillama";
    dead.transportOk = false;
    dead.transportReason = "tls handshake failed";
    PairQuote deadQ = buildPairQuote(1, dead, nullptr, 0.158, pol, now);
    check(deadQ.status == QuoteStatus::NO_DATA && deadQ.reason == "tls handshake failed",
          "transport failure surfaces its reason and blocks funding");
  }

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}