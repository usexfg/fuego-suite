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

#include <cmath>
#include <limits>
#include <string>

namespace XfgSwap {

static const double kRateScale = 1e7;
static const double kGuardLowDivisor = 1.2;
static const double kGuardHighDivisor = 0.8;

const char* quoteStatusToString(QuoteStatus s) {
  switch (s) {
    case QuoteStatus::OK:             return "ok";
    case QuoteStatus::UNPRICED:       return "unpriced";
    case QuoteStatus::NO_DATA:        return "no-data";
    case QuoteStatus::STALE:          return "stale";
    case QuoteStatus::LOW_CONFIDENCE: return "low-confidence";
    case QuoteStatus::DISPUTED:       return "disputed";
    case QuoteStatus::UNEXECUTABLE:   return "unexecutable";
    case QuoteStatus::SINGLE_SOURCE:  return "single-source";
    default:                          return "unknown";
  }
}

static bool finitePositive(double v) {
  return std::isfinite(v) && v > 0.0;
}

uint64_t rateToNum(double rate) {
  if (!finitePositive(rate)) return 0;
  double scaled = rate * kRateScale;
  if (!std::isfinite(scaled)) return 0;
  if (scaled >= static_cast<double>(std::numeric_limits<uint64_t>::max())) return 0;
  return static_cast<uint64_t>(std::llround(scaled));
}

bool rateFromNum(uint64_t rateNum, double& out) {
  out = static_cast<double>(rateNum) / kRateScale;
  return finitePositive(out);
}

RateResult computeReferenceRate(double counterpartyUsd, double xfgUsd) {
  RateResult r;
  if (!finitePositive(counterpartyUsd)) { r.reason = "counterpartyUsd invalid"; return r; }
  if (!finitePositive(xfgUsd))         { r.reason = "xfgUsd invalid";        return r; }

  double rate = counterpartyUsd / xfgUsd;
  if (!finitePositive(rate))           { r.reason = "referenceRate non-finite"; return r; }

  uint64_t num = rateToNum(rate);
  if (num == 0) {
    r.reason = "rateNum overflow or underflow at 1e7 scaling";
    return r;
  }
  r.ok = true;
  r.referenceRate = rate;
  r.rateNum = num;
  return r;
}

bool withinRateGuard(double proposedRate, double referenceRate) {
  if (!finitePositive(proposedRate) || !finitePositive(referenceRate)) return false;
  const double low = referenceRate / kGuardLowDivisor;
  const double high = referenceRate / kGuardHighDivisor;
  return proposedRate >= low && proposedRate <= high;
}

static QuoteStatus validateObservation(const PriceObservation& o,
                                       const PricePolicy& p,
                                       int64_t now,
                                       std::string& reason) {
  if (!finitePositive(o.price)) { reason = "price not finite or not positive"; return QuoteStatus::NO_DATA; }

  if (o.providerTimestamp <= 0) {
    reason = "provider timestamp missing";
    return QuoteStatus::NO_DATA;
  }
  if (o.fetchTimestamp <= 0) {
    reason = "fetch timestamp missing";
    return QuoteStatus::NO_DATA;
  }
  if (now - o.providerTimestamp > p.maxProviderAgeSec) {
    reason = "provider stamp older than maxProviderAgeSec";
    return QuoteStatus::STALE;
  }
  if (o.providerTimestamp - now > p.maxClockSkewSec) {
    reason = "provider timestamp ahead of clock skew allowance";
    return QuoteStatus::STALE;
  }
  if (now - o.fetchTimestamp > p.maxProviderAgeSec) {
    reason = "observation held longer than maxProviderAgeSec";
    return QuoteStatus::STALE;
  }
  if (o.confidenceSupplied && o.confidence < p.minConfidence) {
    reason = "confidence below policy floor";
    return QuoteStatus::LOW_CONFIDENCE;
  }
  reason.clear();
  return QuoteStatus::OK;
}

QuoteStatus evaluateAsset(const AssetDescriptor& asset,
                          const FeedResult& primary,
                          const FeedResult* secondary,
                          const PricePolicy& policy,
                          int64_t now,
                          AssetPrice& outObservation) {
  if (!asset.priced) return QuoteStatus::UNPRICED;

  auto pit = primary.assets.find(asset.symbol);
  if (pit == primary.assets.end() || !pit->second.ok) return QuoteStatus::NO_DATA;

  std::string reason;
  QuoteStatus st = validateObservation(pit->second.observation, policy, now, reason);
  if (st != QuoteStatus::OK) return st;

  bool corroborated = false;
  if (secondary != nullptr) {
    auto sit = secondary->assets.find(asset.symbol);
    if (sit != secondary->assets.end() && sit->second.ok) {
      std::string sreason;
      if (validateObservation(sit->second.observation, policy, now, sreason) == QuoteStatus::OK) {
        corroborated = true;
        double a = pit->second.observation.price;
        double b = sit->second.observation.price;
        double denom = std::fmax(std::fabs(a), std::fabs(b));
        if (denom > 0.0) {
          double rel = std::fabs(a - b) / denom;
          if (rel > policy.crossCheckTolerance) return QuoteStatus::DISPUTED;
        }
      }
    }
  }

  if (!corroborated) {
    if (policy.requireCorroboration) return QuoteStatus::SINGLE_SOURCE;
    outObservation = pit->second;
    return QuoteStatus::SINGLE_SOURCE;
  }

  outObservation = pit->second;
  return QuoteStatus::OK;
}

PairQuote buildPairQuote(uint8_t pair,
                         const FeedResult& primary,
                         const FeedResult* secondary,
                         double xfgUsd,
                         const PricePolicy& policy,
                         int64_t now) {
  PairQuote q;
  q.pair = pair;
  q.primarySource = primary.feedName;
  q.secondarySource = secondary != nullptr ? secondary->feedName : std::string();

  const std::vector<PairAssetBinding> bindings = allBindings();
  const PairAssetBinding* self = nullptr;
  for (const auto& b : bindings) {
    if (static_cast<uint8_t>(b.pair) == pair) { self = &b; break; }
  }
  if (self == nullptr || self->asset == nullptr) {
    q.status = QuoteStatus::UNPRICED;
    q.reason = "pair not bound to an asset";
    return q;
  }

  const AssetDescriptor& asset = *self->asset;
  q.settlementAsset = asset.symbol;
  q.defiLlamaId = asset.defiLlamaId;

  AssetPrice obs;
  q.status = evaluateAsset(asset, primary, secondary, policy, now, obs);
  q.reason.clear();
  switch (q.status) {
    case QuoteStatus::UNPRICED:     q.reason = "no feed id serves this asset"; break;
    case QuoteStatus::NO_DATA:      q.reason = primary.transportOk ? "asset absent from feed response"
                                                                  : primary.transportReason; break;
    case QuoteStatus::STALE:
    case QuoteStatus::LOW_CONFIDENCE:
    case QuoteStatus::DISPUTED:
    case QuoteStatus::UNEXECUTABLE: break;
    case QuoteStatus::SINGLE_SOURCE: q.reason = "only one feed answered; not corroborated"; break;
    case QuoteStatus::OK: break;
  }

  if (q.status != QuoteStatus::OK && q.status != QuoteStatus::SINGLE_SOURCE) {
    q.managedOffersAllowed = false;
    q.newSwapAllowed = false;
    if (!asset.executable && q.status != QuoteStatus::UNPRICED) {
      q.reason += "; atomic divisor " + asset.atomicDivisorText +
                  " does not fit the uint64 amount model";
    }
    return q;
  }

  q.counterpartyUsdOk = true;
  q.counterpartyUsd = obs.observation.price;
  q.providerTimestamp = obs.observation.providerTimestamp;
  q.fetchTimestamp = obs.observation.fetchTimestamp;
  q.confidenceSupplied = obs.observation.confidenceSupplied;
  q.confidence = obs.observation.confidence;

  if (!finitePositive(xfgUsd)) {
    q.status = QuoteStatus::NO_DATA;
    q.reason = "XFG/USD from Hearth unavailable or invalid";
    q.counterpartyUsdOk = false;
    q.managedOffersAllowed = false;
    q.newSwapAllowed = false;
    return q;
  }
  q.xfgUsdOk = true;
  q.xfgUsd = xfgUsd;

  RateResult rr = computeReferenceRate(q.counterpartyUsd, q.xfgUsd);
  if (!rr.ok) {
    q.status = QuoteStatus::NO_DATA;
    q.reason = rr.reason;
    q.managedOffersAllowed = false;
    q.newSwapAllowed = false;
    return q;
  }
  q.referenceRateOk = true;
  q.referenceRate = rr.referenceRate;
  q.rateNum = rr.rateNum;

  if (!asset.executable) {
    q.status = QuoteStatus::UNEXECUTABLE;
    q.reason = "atomic divisor " + asset.atomicDivisorText +
               " does not fit the uint64 amount model";
    q.managedOffersAllowed = false;
    q.newSwapAllowed = false;
    return q;
  }

  q.managedOffersAllowed = true;
  q.newSwapAllowed = true;
  return q;
}

} // namespace XfgSwap