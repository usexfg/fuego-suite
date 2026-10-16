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

#pragma once

#include "SwapTypes.h"

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace XfgSwap {

// ── Asset identity ────────────────────────────────────────────────────────
// One row per priceable asset. Many pairs settle in the same asset (six EVM
// pairs settle in ETH), so the catalog is keyed by asset, not by pair.
struct AssetDescriptor {
  std::string symbol;        // settlement ticker actually locked
  std::string defiLlamaId;   // coins.llama.fi id, "coingecko:ethereum"
  std::string pythSymbol;    // Pyth "Crypto.BTC/USD" symbol part, empty if unlisted
  // Pythnet 32-byte price-feed id, hex, no 0x. Resolved from live Hermes metadata and
  // verified against the documented canonical feed; empty when Pyth does not list it.
  std::string pythId;
  // Symbol the feed must report back. Guards against an id that resolves to a
  // different asset, the way coingecko:the-open-network reports GRAM.
  std::string expectedFeedSymbol;
  // Atomic units per whole coin. Zero when the divisor does not fit uint64 (SC is
  // 10^24 hastings, which exceeds UINT64_MAX); atomicDivisorText keeps the true
  // value so nothing has to guess it. Never divide by this when it is zero.
  uint64_t    atomicDivisor;
  std::string atomicDivisorText;
  bool        priced;        // a feed id is known and verified live
  bool        executable;    // atomic divisor fits the uint64 amount model
};

struct PairAssetBinding {
  SwapPair pair;
  const AssetDescriptor* asset;
};

// ── Observations ──────────────────────────────────────────────────────────
struct PriceObservation {
  double   price = 0.0;
  int64_t  providerTimestamp = 0;  // feed's own stamp, unix seconds
  int64_t  fetchTimestamp = 0;     // our receipt time, unix seconds
  double   confidence = 0.0;      // supplied quality scores must be finite in [0,1]
  bool     confidenceSupplied = false;
};

struct AssetPrice {
  PriceObservation observation;
  bool        ok = false;
  std::string reason;
};

struct FeedResult {
  std::string feedName;
  std::map<std::string, AssetPrice> assets;  // keyed by AssetDescriptor::symbol
  bool        transportOk = false;
  std::string transportReason;
};

// ── Policy ────────────────────────────────────────────────────────────────
struct PricePolicy {
  // DeFiLlama stamps /prices/current in 300-second buckets shared across every
  // coin in a response, so observed age ramps 0->300s. A 120s provider-age gate
  // would reject ~80% of a 30s poll cycle. 600s tolerates two missed buckets.
  int64_t  maxProviderAgeSec = 600;
  int64_t  maxClockSkewSec = 120;
  double   minConfidence = 0.8;       // finite in [0,1]
  double   crossCheckTolerance = 0.02;  // finite relative disagreement limit in [0,1)
  bool     requireCorroboration = false;  // refuse SINGLE_SOURCE assets
  uint32_t pollIntervalSec = 30;
  int      connectTimeoutSec = 4;
  int      readTimeoutSec = 4;
  size_t   maxResponseBytes = 262144;
};

// referenceRate = counterpartyUsd / xfgUsd, in whole XFG per whole coin.
struct RateResult {
  bool    ok = false;
  double  referenceRate = 0.0;  // XFG per 1 whole counterparty coin
  uint64_t rateNum = 0;         // referenceRate * 1e7, checked for overflow
  std::string reason;
};

enum class QuoteStatus {
  OK,
  UNPRICED,        // no feed id serves this asset
  NO_DATA,         // feed reachable but asset absent from the response
  STALE,           // provider stamp or fetch too old
  LOW_CONFIDENCE,  // confidence below policy floor
  DISPUTED,        // two feeds disagree beyond tolerance
  UNEXECUTABLE,    // atomic divisor does not fit the uint64 amount model
  SINGLE_SOURCE    // only one feed answered; not corroborated
};

const char* quoteStatusToString(QuoteStatus s);

// ── The rate and guard arithmetic, free of I/O ────────────────────────────
RateResult computeReferenceRate(double counterpartyUsd, double xfgUsd);

// Two-sided guard: referenceRate/1.2 <= proposed <= referenceRate/0.8.
bool withinRateGuard(double proposedRate, double referenceRate);

// Freshness / confidence / plausibility gate for one observation. Shared so a feed
// reader and the quote path cannot disagree about what "usable" means.
QuoteStatus validateObservation(const PriceObservation& o,
                               const PricePolicy& policy,
                               int64_t now,
                               std::string& reason);

uint64_t rateToNum(double rate);
bool     rateFromNum(uint64_t rateNum, double& out);

// ── Feed abstraction ──────────────────────────────────────────────────────
class PriceFeed {
public:
  virtual ~PriceFeed() = default;
  virtual const char* name() const = 0;
  virtual std::vector<std::string> assets() const = 0;
  virtual FeedResult fetch(const PricePolicy& policy) = 0;
};

// ── Catalog ───────────────────────────────────────────────────────────────
const std::deque<AssetDescriptor>& assetCatalog();
const AssetDescriptor* assetForSymbol(const std::string& symbol);
std::vector<PairAssetBinding> bindingsForFeed(const std::string& feedName);
std::vector<PairAssetBinding> allBindings();
std::vector<std::string> distinctSymbolsForFeed(const std::string& feedName);

// True when the protocol catalog marks the pair STAGED: the id is reserved for
// old records but new offers and swaps must be rejected regardless of pricing.
bool pairIsStaged(SwapPair pair);

// ── Evaluation (pure; no I/O, no clock beyond the passed `now`) ───────────
QuoteStatus evaluateAsset(const AssetDescriptor& asset,
                          const FeedResult& primary,
                          const FeedResult* secondary,
                          const PricePolicy& policy,
                          int64_t now,
                          AssetPrice& outObservation);

struct PairQuote {
  uint8_t     pair = 0;
  std::string settlementAsset;
  std::string defiLlamaId;
  bool        counterpartyUsdOk = false;
  double      counterpartyUsd = 0.0;
  bool        xfgUsdOk = false;
  double      xfgUsd = 0.0;
  bool        referenceRateOk = false;
  double      referenceRate = 0.0;
  uint64_t    rateNum = 0;
  int64_t     providerTimestamp = 0;
  int64_t     fetchTimestamp = 0;
  bool        confidenceSupplied = false;
  double      confidence = 0.0;
  std::string primarySource;
  std::string secondarySource;
  QuoteStatus status = QuoteStatus::UNPRICED;
  std::string reason;
  bool        managedOffersAllowed = false;
  bool        newSwapAllowed = false;
};

// xfgUsd comes from Hearth's raw price plus the fixed HEAT peg, never from a
// feed and never from the SEED_XFG_USD constant.
PairQuote buildPairQuote(uint8_t pair,
                         const FeedResult& primary,
                         const FeedResult* secondary,
                         double xfgUsd,
                         const PricePolicy& policy,
                         int64_t now);

} // namespace XfgSwap
