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
#include <map>
#include <set>
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

static void checkBlockedQuote(const PairQuote& quote, QuoteStatus status, const std::string& what) {
  check(quote.status == status, what + " status");
  check(!quote.managedOffersAllowed && !quote.newSwapAllowed, what + " blocks funding");
  check(!quote.referenceRateOk && quote.rateNum == 0, what + " carries no usable rate");
  check(!quote.reason.empty(), what + " explains rejection");
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

    // scaled values in [2^63, 2^64) fit a uint64 but not a signed long long, which
    // is what std::llround returns. They must convert exactly, not saturate or wrap.
    check(rateToNum(1.0e12) == 10000000000000000000ULL,
          "rate*1e7 = 1e19 (above INT64_MAX, below UINT64_MAX) converts exactly");
    check(rateToNum(1.8e12) == 18000000000000000000ULL,
          "rate*1e7 = 1.8e19 converts exactly");
    check(rateToNum(9.0e11) == 9000000000000000000ULL,
          "rate*1e7 = 9e18 (just under INT64_MAX) converts exactly");
    check(rateToNum(1.9e12) == 0, "rate*1e7 above UINT64_MAX is refused");
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

  // ── catalog: every pair bound, fourteen share ETH, SIA unexecutable
  {
    std::vector<PairAssetBinding> b = allBindings();
    check(b.size() == SWAP_PAIR_COUNT, "catalog binds every pair in SWAP_PAIR_CATALOG");
    check(b.size() == 46, "catalog binds 46 pairs");

    int eth = 0, unpriced = 0, unexec = 0;
    for (const auto& x : b) {
      if (x.asset == nullptr) continue;
      if (x.asset->symbol == "ETH") ++eth;
      if (!x.asset->priced) ++unpriced;
      if (!x.asset->executable) ++unexec;
    }
    check(eth == 14, "fourteen pairs settle in ETH (Ethereum itself, ARB, BASE, ROBINHOOD, BOB, UNICHAIN, OPTIMISM "
                     "and the seven generic-adapter ETH chains)");
    check(unpriced == 2, "exactly two assets are unpriced: GLEEC and PEAQ (no feed id serves either)");
    check(unexec == 1, "exactly one asset is unexecutable (SC, 1e24)");

    const AssetDescriptor* sc = assetForSymbol("SC");
    check(sc != nullptr && sc->atomicDivisor == 0, "SC divisor is 0 rather than a lying value");
    check(sc != nullptr && sc->atomicDivisorText == "1e24", "SC divisor text records the true 1e24");
    check(sc != nullptr && !sc->executable, "SC is marked unexecutable");

    check(assetForSymbol("KMD") != nullptr && assetForSymbol("KMD")->priced,
          "KMD is priced via coingecko:komodo (intermittent, and observed 6.6h stale)");
    check(assetForSymbol("GLEEC") != nullptr && !assetForSymbol("GLEEC")->priced, "GLEEC unpriced");
    check(assetForSymbol("PEAQ") != nullptr && !assetForSymbol("PEAQ")->priced, "PEAQ unpriced");
    check(assetForSymbol("RBTC") != nullptr && assetForSymbol("RBTC")->defiLlamaId == "coingecko:rootstock",
          "RSK settles in RBTC, priced off Rootstock rather than BTC");
    check(assetForSymbol("XDAI") != nullptr && assetForSymbol("XDAI")->defiLlamaId == "coingecko:xdai",
          "Gnosis settles in xDAI, priced off xdai rather than DAI");
    check(assetForSymbol("BEAM") != nullptr && assetForSymbol("BEAM")->defiLlamaId == "coingecko:beam-2",
          "Beam uses coingecko:beam-2, the chain token per DeFiLlama's chain registry");
    check(assetForSymbol("GRAM") != nullptr && assetForSymbol("GRAM")->defiLlamaId == "coingecko:the-open-network",
          "TON pair settles in GRAM via coingecko:the-open-network");
    check(assetForSymbol("NOPE") == nullptr, "unknown symbol returns nullptr");

    check(distinctSymbolsForFeed("defillama").size() == 31, "31 distinct DeFiLlama assets");
    check(distinctSymbolsForFeed("pyth").size() == 23, "23 Pyth-corroborated assets");
  }

  // ── the price catalog and the protocol pair catalog describe the same pairs
  // SwapPairCatalog.h is the append-only protocol source of truth. If a pair is added
  // there without a settlement-asset binding here, it must be a test failure, not a
  // silently unpriced pair that nobody decided on.
  {
    std::map<uint8_t, const AssetDescriptor*> bound;
    for (const auto& x : allBindings()) bound[static_cast<uint8_t>(x.pair)] = x.asset;
    check(bound.size() == allBindings().size(), "no pair is bound twice");

    // The only pair whose protocol ticker differs from its feed symbol: the TON pair
    // settles in GRAM (see the matrix note on coingecko:the-open-network).
    const std::map<std::string, std::string> tickerAlias = {{"TON", "GRAM"}};

    for (size_t i = 0; i < SWAP_PAIR_COUNT; ++i) {
      const SwapPairDescriptor& d = SWAP_PAIR_CATALOG[i];
      auto it = bound.find(d.id);
      const std::string who = std::string(d.symbol) + " (id " + std::to_string(d.id) + ")";
      check(it != bound.end() && it->second != nullptr, who + " has a settlement-asset binding");
      if (it == bound.end() || it->second == nullptr) continue;
      const AssetDescriptor& a = *it->second;

      std::string wantSymbol = d.assetTicker;
      auto alias = tickerAlias.find(wantSymbol);
      if (alias != tickerAlias.end()) wantSymbol = alias->second;
      check(a.symbol == wantSymbol, who + " settles in " + wantSymbol + " per the protocol catalog, bound to " + a.symbol);

      // Divisor must equal 10^decimals, except where it cannot fit a uint64.
      uint64_t pow10 = 1;
      bool fits = d.decimals <= 19;
      for (unsigned n = 0; fits && n < d.decimals; ++n) pow10 *= 10;
      if (a.executable) {
        check(fits && a.atomicDivisor == pow10, who + " atomicDivisor equals 10^decimals");
      } else {
        check(!fits || a.atomicDivisor == 0, who + " is unexecutable only because its divisor does not fit a uint64");
      }
      check(a.atomicDivisorText == "1e" + std::to_string(d.decimals), who + " divisor text matches decimals");
    }
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

    const AssetDescriptor* gleec = assetForSymbol("GLEEC");
    check(evaluateAsset(*gleec, fresh, nullptr, pol, now, out) == QuoteStatus::UNPRICED,
          "GLEEC is UNPRICED regardless of feed health");

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

    FeedResult emptyFeed;
    emptyFeed.feedName = "defillama";
    emptyFeed.transportOk = true;
    PairQuote gleecQ = buildPairQuote(12, emptyFeed, nullptr, 0.158, pol, now);
    check(gleecQ.status == QuoteStatus::UNPRICED, "GLEEC pair is UNPRICED");
    check(!gleecQ.newSwapAllowed, "unpriced pair cannot start a swap");

    FeedResult tonPrimary = makeFeed("defillama", "GRAM", 1.51, now - 5, now - 2, 0.99, true);
    FeedResult tonSecondary = makeFeed("pyth", "GRAM", 1.51, now - 5, now - 2, 0.0, false);
    PairQuote tonQ = buildPairQuote(27, tonPrimary, &tonSecondary, 0.158, pol, now);
    check(tonQ.settlementAsset == "GRAM", "TON pair settles and prices in GRAM");
    check(tonQ.status == QuoteStatus::UNEXECUTABLE && !tonQ.newSwapAllowed,
          "TON is STAGED in the catalog, so a live GRAM price still cannot fund it");

    // requireCorroboration at the quote level: one feed is not enough, and the reason
    // must say so rather than reporting a bogus counterpartyUsd failure.
    PricePolicy strictPol = pol;
    strictPol.requireCorroboration = true;
    PairQuote strictSingle = buildPairQuote(1, primary, nullptr, 0.158, strictPol, now);
    check(strictSingle.status == QuoteStatus::SINGLE_SOURCE, "strict policy keeps SINGLE_SOURCE as the status");
    check(!strictSingle.managedOffersAllowed && !strictSingle.newSwapAllowed,
          "strict policy blocks funding on a single feed");
    check(strictSingle.reason.find("corroboration") != std::string::npos,
          "strict policy explains that corroboration is required");
    check(!strictSingle.counterpartyUsdOk, "strict policy does not price from an empty observation");
    PairQuote strictBoth = buildPairQuote(1, primary, &secondary, 0.158, strictPol, now);
    check(strictBoth.status == QuoteStatus::OK && strictBoth.newSwapAllowed,
          "strict policy still quotes a corroborated pair");

    // A generic-adapter pair with no verified feed id fails closed.
    FeedResult hypeFeed = makeFeed("defillama", "HYPE", 89.0, now - 5, now - 2, 0.99, true);
    PairQuote hype = buildPairQuote(static_cast<uint8_t>(SwapPair::HYPEREVM), hypeFeed, nullptr, 0.158, pol, now);
    check(hype.settlementAsset == "HYPE" && hype.counterpartyUsdOk,
          "HYPEREVM prices in its own native HYPE, not ETH");
    PairQuote peaq = buildPairQuote(static_cast<uint8_t>(SwapPair::PEAQ), primary, &secondary, 0.158, pol, now);
    check(peaq.status == QuoteStatus::UNPRICED && !peaq.newSwapAllowed,
          "PEAQ has no verified feed id and fails closed");
    PairQuote linea = buildPairQuote(static_cast<uint8_t>(SwapPair::LINEA), primary, &secondary, 0.158, pol, now);
    check(linea.status == QuoteStatus::OK && linea.settlementAsset == "ETH",
          "LINEA settles in native ETH and prices off ETH");

    FeedResult dead;
    dead.feedName = "defillama";
    dead.transportOk = false;
    dead.transportReason = "tls handshake failed";
    PairQuote deadQ = buildPairQuote(1, dead, nullptr, 0.158, pol, now);
    check(deadQ.status == QuoteStatus::NO_DATA && deadQ.reason == "tls handshake failed",
          "transport failure surfaces its reason and blocks funding");
  }

  {
    const FeedResult primary = makeFeed("defillama", "ETH", 2700.0, now - 5, now - 2, 0.99, true);
    const FeedResult secondary = makeFeed("pyth", "ETH", 2701.0, now - 5, now - 2, 0.99, true);
    const double invalidConfidence[] = {
      std::numeric_limits<double>::quiet_NaN(),
      std::numeric_limits<double>::infinity(),
      -std::numeric_limits<double>::infinity(), -0.001, 1.001
    };
    for (double confidence : invalidConfidence) {
      FeedResult malformed = primary;
      malformed.assets["ETH"].observation.confidence = confidence;
      checkBlockedQuote(buildPairQuote(1, malformed, &secondary, 0.158, pol, now),
                        QuoteStatus::LOW_CONFIDENCE, "malformed supplied confidence");
    }

    PricePolicy zeroFloor = pol;
    zeroFloor.minConfidence = 0.0;
    FeedResult zeroConfidence = primary;
    zeroConfidence.assets["ETH"].observation.confidence = 0.0;
    check(buildPairQuote(1, zeroConfidence, nullptr, 0.158, zeroFloor, now).newSwapAllowed,
          "confidence zero is valid at a configured zero floor");
    PricePolicy fullFloor = pol;
    fullFloor.minConfidence = 1.0;
    FeedResult fullConfidence = primary;
    fullConfidence.assets["ETH"].observation.confidence = 1.0;
    check(buildPairQuote(1, fullConfidence, nullptr, 0.158, fullFloor, now).newSwapAllowed,
          "confidence one satisfies a configured full floor");

    FeedResult absentConfidence = primary;
    absentConfidence.assets["ETH"].observation.confidenceSupplied = false;
    absentConfidence.assets["ETH"].observation.confidence = std::numeric_limits<double>::quiet_NaN();
    check(buildPairQuote(1, absentConfidence, nullptr, 0.158, pol, now).newSwapAllowed,
          "absent confidence remains optional");

    for (double threshold : invalidConfidence) {
      PricePolicy invalidFloor = pol;
      invalidFloor.minConfidence = threshold;
      checkBlockedQuote(buildPairQuote(1, primary, &secondary, 0.158, invalidFloor, now),
                        QuoteStatus::NO_DATA, "invalid confidence floor");
      PricePolicy invalidTolerance = pol;
      invalidTolerance.crossCheckTolerance = threshold;
      checkBlockedQuote(buildPairQuote(1, primary, &secondary, 0.158, invalidTolerance, now),
                        QuoteStatus::NO_DATA, "invalid cross-check tolerance");
    }
    PricePolicy disabledCrossCheck = pol;
    disabledCrossCheck.crossCheckTolerance = 1.0;
    checkBlockedQuote(buildPairQuote(1, primary, &secondary, 0.158, disabledCrossCheck, now),
                      QuoteStatus::NO_DATA, "tolerance one cannot disable corroboration checks");
    PricePolicy exactCrossCheck = pol;
    exactCrossCheck.crossCheckTolerance = 0.0;
    check(buildPairQuote(1, primary, &primary, 0.158, exactCrossCheck, now).status == QuoteStatus::OK,
          "zero tolerance accepts identical prices");
    checkBlockedQuote(buildPairQuote(1, primary, &secondary, 0.158, exactCrossCheck, now),
                      QuoteStatus::DISPUTED, "zero tolerance rejects distinct prices");

    FeedResult failedPrimary = primary;
    failedPrimary.transportOk = false;
    failedPrimary.transportReason = "fetch failed after a previous valid response";
    checkBlockedQuote(buildPairQuote(1, failedPrimary, &secondary, 0.158, pol, now),
                      QuoteStatus::NO_DATA, "failed primary with usable asset data");
    AssetPrice reused = primary.assets.at("ETH");
    check(evaluateAsset(*assetForSymbol("ETH"), failedPrimary, &secondary, pol, now, reused) == QuoteStatus::NO_DATA &&
          !reused.ok && reused.observation.price == 0.0,
          "failed evaluation clears the caller's prior observation");

    FeedResult failedSecondary = secondary;
    failedSecondary.transportOk = false;
    PricePolicy strict = pol;
    strict.requireCorroboration = true;
    checkBlockedQuote(buildPairQuote(1, primary, &failedSecondary, 0.158, strict, now),
                      QuoteStatus::SINGLE_SOURCE, "failed secondary cannot corroborate cached data");
    PairQuote single = buildPairQuote(1, primary, &failedSecondary, 0.158, pol, now);
    check(single.status == QuoteStatus::SINGLE_SOURCE && single.newSwapAllowed,
          "optional corroboration uses only the healthy primary");
    failedSecondary.assets["ETH"].observation.price = 90000.0;
    check(buildPairQuote(1, primary, &failedSecondary, 0.158, pol, now).status == QuoteStatus::SINGLE_SOURCE,
          "failed secondary cannot dispute a healthy primary");
    FeedResult malformedSecondary = secondary;
    malformedSecondary.assets["ETH"].observation.confidence = std::numeric_limits<double>::quiet_NaN();
    checkBlockedQuote(buildPairQuote(1, primary, &malformedSecondary, 0.158, strict, now),
                      QuoteStatus::SINGLE_SOURCE, "malformed secondary confidence cannot corroborate");

    const int64_t invalidClock[] = {0, -1, std::numeric_limits<int64_t>::min()};
    for (int64_t clock : invalidClock) {
      checkBlockedQuote(buildPairQuote(1, primary, &secondary, 0.158, pol, clock),
                        QuoteStatus::NO_DATA, "non-positive current clock");
    }
    for (int64_t timestamp : invalidClock) {
      FeedResult missingTime = primary;
      missingTime.assets["ETH"].observation.providerTimestamp = timestamp;
      checkBlockedQuote(buildPairQuote(1, missingTime, &secondary, 0.158, pol, now),
                        QuoteStatus::NO_DATA, "non-positive provider timestamp");
      missingTime = primary;
      missingTime.assets["ETH"].observation.fetchTimestamp = timestamp;
      checkBlockedQuote(buildPairQuote(1, missingTime, &secondary, 0.158, pol, now),
                        QuoteStatus::NO_DATA, "non-positive receipt timestamp");
    }
    FeedResult futureReceipt = primary;
    futureReceipt.assets["ETH"].observation.fetchTimestamp = std::numeric_limits<int64_t>::max();
    checkBlockedQuote(buildPairQuote(1, futureReceipt, &secondary, 0.158, pol, now),
                      QuoteStatus::STALE, "receipt at INT64_MAX is outside skew");
    FeedResult futureProvider = primary;
    futureProvider.assets["ETH"].observation.providerTimestamp = std::numeric_limits<int64_t>::max();
    checkBlockedQuote(buildPairQuote(1, futureProvider, &secondary, 0.158, pol, now),
                      QuoteStatus::STALE, "provider at INT64_MAX is outside skew");

    PriceObservation observation = primary.assets.at("ETH").observation;
    std::string reason;
    observation.providerTimestamp = now - pol.maxProviderAgeSec;
    observation.fetchTimestamp = now;
    check(validateObservation(observation, pol, now, reason) == QuoteStatus::OK,
          "provider at the exact age limit is accepted");
    --observation.providerTimestamp;
    check(validateObservation(observation, pol, now, reason) == QuoteStatus::STALE,
          "provider one second past the age limit is stale");
    observation.providerTimestamp = observation.fetchTimestamp = now - pol.maxProviderAgeSec;
    check(validateObservation(observation, pol, now, reason) == QuoteStatus::OK,
          "receipt at the exact age limit is accepted");
    --observation.fetchTimestamp;
    check(validateObservation(observation, pol, now, reason) == QuoteStatus::STALE,
          "receipt one second past the age limit is stale");
    observation.providerTimestamp = now;
    observation.fetchTimestamp = now + pol.maxClockSkewSec;
    check(validateObservation(observation, pol, now, reason) == QuoteStatus::OK,
          "receipt at the exact future skew limit is accepted");
    ++observation.fetchTimestamp;
    check(validateObservation(observation, pol, now, reason) == QuoteStatus::STALE,
          "receipt beyond future skew is rejected");
    observation.providerTimestamp = now + pol.maxClockSkewSec;
    observation.fetchTimestamp = now;
    check(validateObservation(observation, pol, now, reason) == QuoteStatus::OK,
          "provider at the exact future skew limit is accepted");
    ++observation.providerTimestamp;
    check(validateObservation(observation, pol, now, reason) == QuoteStatus::STALE,
          "provider beyond future skew is rejected");
    observation.providerTimestamp = now;
    observation.fetchTimestamp = now - pol.maxClockSkewSec - 1;
    check(validateObservation(observation, pol, now, reason) == QuoteStatus::STALE,
          "provider cannot postdate its receipt beyond skew");

    observation.providerTimestamp = observation.fetchTimestamp = std::numeric_limits<int64_t>::max();
    check(validateObservation(observation, pol, std::numeric_limits<int64_t>::max(), reason) == QuoteStatus::OK,
          "equal maximum int64 timestamps do not overflow");
    observation.providerTimestamp = observation.fetchTimestamp = 1;
    check(validateObservation(observation, pol, std::numeric_limits<int64_t>::max(), reason) == QuoteStatus::STALE,
          "maximum representable timestamp age is safely rejected");
    observation.providerTimestamp = observation.fetchTimestamp = now;
    for (int64_t limit : {-1LL, std::numeric_limits<int64_t>::min()}) {
      PricePolicy invalidAge = pol;
      invalidAge.maxProviderAgeSec = limit;
      check(validateObservation(observation, invalidAge, now, reason) == QuoteStatus::NO_DATA,
            "negative provider age limit fails closed");
      PricePolicy invalidSkew = pol;
      invalidSkew.maxClockSkewSec = limit;
      check(validateObservation(observation, invalidSkew, now, reason) == QuoteStatus::NO_DATA,
            "negative skew limit fails closed");
    }
    PricePolicy exactClock = pol;
    exactClock.maxProviderAgeSec = 0;
    exactClock.maxClockSkewSec = 0;
    check(validateObservation(observation, exactClock, now, reason) == QuoteStatus::OK,
          "zero age and skew limits accept an exact current observation");
  }

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
