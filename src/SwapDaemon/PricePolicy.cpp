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
    case QuoteStatus::MISSING_DATA:   return "no-data";
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

static bool finiteUnitInterval(double value) {
  return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}

static bool validatePolicyLimits(const PricePolicy& policy, std::string& reason) {
  if (policy.maxProviderAgeSec < 0) {
    reason = "policy provider age is negative";
    return false;
  }
  if (policy.maxClockSkewSec < 0) {
    reason = "policy clock skew is negative";
    return false;
  }
  if (!finiteUnitInterval(policy.minConfidence)) {
    reason = "policy confidence floor is outside [0,1]";
    return false;
  }
  if (!finiteUnitInterval(policy.crossCheckTolerance) || policy.crossCheckTolerance == 1.0) {
    reason = "policy cross-check tolerance is outside [0,1)";
    return false;
  }
  return true;
}

uint64_t rateToNum(double rate) {
  if (!finitePositive(rate)) return 0;
  double scaled = rate * kRateScale;
  if (!std::isfinite(scaled)) return 0;
  // static_cast<double>(UINT64_MAX) is exactly 2^64, so this refuses everything
  // that does not fit a uint64.
  if (scaled >= static_cast<double>(std::numeric_limits<uint64_t>::max())) return 0;
  // Round in floating point and convert once. std::llround returns a signed
  // 64-bit value, so for scaled in [2^63, 2^64) it is out of range and its result
  // is unspecified, even though the guard above lets that range through.
  return static_cast<uint64_t>(std::round(scaled));
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

QuoteStatus validateObservation(const PriceObservation& o,
                                const PricePolicy& p,
                                int64_t now,
                                std::string& reason) {
  if (!validatePolicyLimits(p, reason)) return QuoteStatus::MISSING_DATA;
  if (now <= 0) {
    reason = "current time is not positive";
    return QuoteStatus::MISSING_DATA;
  }
  if (!finitePositive(o.price)) { reason = "price not finite or not positive"; return QuoteStatus::MISSING_DATA; }

  if (o.providerTimestamp <= 0) {
    reason = "provider timestamp missing";
    return QuoteStatus::MISSING_DATA;
  }
  if (o.fetchTimestamp <= 0) {
    reason = "fetch timestamp missing";
    return QuoteStatus::MISSING_DATA;
  }
  if (o.providerTimestamp < now && now - o.providerTimestamp > p.maxProviderAgeSec) {
    reason = "provider stamp older than maxProviderAgeSec";
    return QuoteStatus::STALE;
  }
  if (o.providerTimestamp > now && o.providerTimestamp - now > p.maxClockSkewSec) {
    reason = "provider timestamp ahead of clock skew allowance";
    return QuoteStatus::STALE;
  }
  if (o.fetchTimestamp < now && now - o.fetchTimestamp > p.maxProviderAgeSec) {
    reason = "observation held longer than maxProviderAgeSec";
    return QuoteStatus::STALE;
  }
  if (o.fetchTimestamp > now && o.fetchTimestamp - now > p.maxClockSkewSec) {
    reason = "receipt timestamp ahead of clock skew allowance";
    return QuoteStatus::STALE;
  }
  if (o.providerTimestamp > o.fetchTimestamp &&
      o.providerTimestamp - o.fetchTimestamp > p.maxClockSkewSec) {
    reason = "provider timestamp ahead of receipt skew allowance";
    return QuoteStatus::STALE;
  }
  if (o.confidenceSupplied) {
    if (!finiteUnitInterval(o.confidence)) {
      reason = "confidence is outside [0,1]";
      return QuoteStatus::LOW_CONFIDENCE;
    }
    if (o.confidence < p.minConfidence) {
      reason = "confidence below policy floor";
      return QuoteStatus::LOW_CONFIDENCE;
    }
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
  outObservation = AssetPrice();
  if (!asset.priced) {
    outObservation.reason = "no feed id serves this asset";
    return QuoteStatus::UNPRICED;
  }
  if (!primary.transportOk) {
    outObservation.reason = primary.transportReason.empty() ? "primary feed transport failed"
                                                          : primary.transportReason;
    return QuoteStatus::MISSING_DATA;
  }

  auto pit = primary.assets.find(asset.symbol);
  if (pit == primary.assets.end()) {
    outObservation.reason = "asset absent from feed response";
    return QuoteStatus::MISSING_DATA;
  }
  if (!pit->second.ok) {
    outObservation.reason = pit->second.reason.empty() ? "asset rejected by feed" : pit->second.reason;
    return QuoteStatus::MISSING_DATA;
  }

  QuoteStatus st = validateObservation(pit->second.observation, policy, now, outObservation.reason);
  if (st != QuoteStatus::OK) {
    // The primary is unusable -- usually stale. Measured 2026-10-04: DeFiLlama's per-coin
    // stamps for long-tail assets run 927-10650s against a 600s gate, while CoinGecko is
    // 3-26s old and agrees to several significant figures. Refusing to use a fresh, sane
    // price because the primary is slow is a self-inflicted outage.
    //
    // A usable secondary is better than no price, but it is NOT corroboration: only one
    // source passed validation, so this is reported SINGLE_SOURCE, never OK.
    // ONLY when the primary is genuinely TOO OLD, decided from the observation rather
    // than the status. Both "too old" and "ahead of the clock" return STALE, but they are
    // opposite problems: an old price is a coverage gap a fresh source can fill, while a
    // future timestamp is a broken clock or a bogus feed and must be surfaced, never
    // substituted away. LOW_CONFIDENCE and malformed data are likewise correctness
    // signals and are left alone.
    // Age is judged on the PROVIDER stamp alone. fetchTimestamp is our own clock at parse
    // time, so it is always ~0s old; requiring it to be stale too made this condition
    // false for every real feed, and the fallback never fired in production. The unit
    // test had aged fetchTimestamp by hand, which hid exactly this.
    //
    // The future-timestamp guards stay on both fields: a stamp ahead of the clock is a
    // broken clock or a bogus feed, never a coverage gap.
    const PriceObservation& pob = pit->second.observation;
    const bool primaryTooOld =
        pob.providerTimestamp > 0 && pob.fetchTimestamp > 0 &&
        now - pob.providerTimestamp > policy.maxProviderAgeSec &&
        pob.providerTimestamp <= now && pob.fetchTimestamp <= now;

    if (st == QuoteStatus::STALE && primaryTooOld &&
        secondary != nullptr && secondary->transportOk) {
      const std::string primaryReason = outObservation.reason;
      auto sit = secondary->assets.find(asset.symbol);
      if (sit != secondary->assets.end() && sit->second.ok) {
        std::string sreason;
        if (validateObservation(sit->second.observation, policy, now, sreason) == QuoteStatus::OK) {
          outObservation = sit->second;
          outObservation.reason = "primary feed unusable (" + primaryReason +
                                 "); priced from the secondary feed, uncorroborated";
          return QuoteStatus::SINGLE_SOURCE;
        }
      }
    }
    return st;
  }

  bool corroborated = false;
  if (secondary != nullptr && secondary->transportOk) {
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
          if (rel > policy.crossCheckTolerance) {
            outObservation.reason = "feed prices disagree beyond policy tolerance";
            return QuoteStatus::DISPUTED;
          }
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
  q.reason = obs.reason;

  // With corroboration required, evaluateAsset reports SINGLE_SOURCE without an
  // observation. Treating that like the quotable case below would price the pair
  // from an empty observation and surface a misleading "counterpartyUsd invalid".
  if (q.status == QuoteStatus::SINGLE_SOURCE && policy.requireCorroboration) {
    q.reason = "corroboration required but only one feed answered";
    q.managedOffersAllowed = false;
    q.newSwapAllowed = false;
    return q;
  }
  switch (q.status) {
    case QuoteStatus::UNPRICED:     q.reason = "no feed id serves this asset"; break;
    case QuoteStatus::MISSING_DATA: break;
    case QuoteStatus::STALE:
    case QuoteStatus::LOW_CONFIDENCE:
    case QuoteStatus::DISPUTED:
    case QuoteStatus::UNEXECUTABLE: break;
    // Keep a specific reason when evaluateAsset produced one. This switch used to
    // overwrite unconditionally, which erased *why* a quote was single-source -- most
    // visibly the "primary stale, priced from the secondary" case, whose explanation is
    // exactly what an operator needs.
    case QuoteStatus::SINGLE_SOURCE:
      if (q.reason.empty()) q.reason = "only one feed answered; not corroborated";
      break;
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
    q.status = QuoteStatus::MISSING_DATA;
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
    q.status = QuoteStatus::MISSING_DATA;
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

  if (pairIsStaged(static_cast<SwapPair>(pair))) {
    q.status = QuoteStatus::UNEXECUTABLE;
    q.reason = "pair is STAGED in the protocol catalog; new offers and swaps must be rejected";
    q.managedOffersAllowed = false;
    q.newSwapAllowed = false;
    return q;
  }

  q.managedOffersAllowed = true;
  q.newSwapAllowed = true;
  return q;
}

} // namespace XfgSwap
