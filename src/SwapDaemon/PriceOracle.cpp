// Copyright (c) 2017-2026 Fuego Developers
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

#include "PriceOracle.h"
#include <cmath>
#include <algorithm>

namespace XfgSwap {

// =============================================================================
// Seed prices: 1 XFG = $0.158 (10:1 Hearth pool bootstrap; HEAT peg = $1.58)
// =============================================================================
//
// Counterparty prices (Sept 2026):
//   SOL = $170    →  1 SOL =   1,076 XFG
//   ETH = $2,140  →  1 ETH =  13,544 XFG
//   BCH = $469    →  1 BCH =   2,968 XFG
//   XMR = $343    →  1 XMR =   2,171 XFG
//
// These seed rates bootstrap the system before any swaps complete.
// Once >= 5 real swaps exist for a pair, TWAP takes over entirely.
// =============================================================================

static const double SEED_XFG_USD = 0.158;
static const double SEED_SOL_USD = 170.0;
static const double SEED_ETH_USD = 2140.0;
static const double SEED_BCH_USD = 469.0;
static const double SEED_XMR_USD = 343.0;
static const double SEED_BNB_USD = 590.0;
static const double SEED_DCR_USD = 20.0;
static const double SEED_POLY_USD = 0.55;
static const double SEED_GLEEC_USD = 0.05;
static const double SEED_AVAX_USD = 32.0;
static const double SEED_CRO_USD = 0.15;
static const double SEED_SIA_USD = 0.008;
static const double SEED_PLASMA_USD = 0.60;
static const double SEED_TON_USD = 5.50;
static const double SEED_DOGE_USD = 0.22;
static const double SEED_DASH_USD = 30.0;
static const double SEED_ZEC_USD = 45.0;
static const double SEED_PULSECHAIN_USD = 0.0007;  // native PLS on PulseChain
static const double SEED_ZANO_USD = 3.20;
static const double SEED_BTC_USD = 65000.0;
static const double SEED_LTC_USD = 90.0;
static const double SEED_KMD_USD = 0.35;
static const double SEED_MONAD_USD = 1.20;
static const double SEED_OP_USD = 1.40;
static const double SEED_DOT_USD = 6.50;

// Minimum completed swaps before TWAP replaces seed rate
static const size_t TWAP_MIN_TRADES = 5;

// =============================================================================
// Constructor
// =============================================================================

PriceOracle::PriceOracle()
  : m_twapMaxTrades(20)
  , m_twapMaxAgeSec(604800)   // 7 days
  , m_floorThreshold(0.20)    // fractional tolerance: +/-20% around the reference
  // Bootstrap used +/-50%, which is wider than the guard below and meant a fresh node
  // -- the common case, zero recorded trades -- accepted quotes far outside the +/-20%
  // band before ever reaching it. Both windows now share one tolerance.
  , m_maxBootstrapDrift(0.20) // +/-20% drift from seed while bootstrapping
  , m_liveXfgUsd(0.0) {
}

// =============================================================================
// Seed rates
// =============================================================================

double PriceOracle::getSeedXfgUsd() {
  return SEED_XFG_USD;
}

void PriceOracle::setLiveXfgUsd(double usd) {
  std::lock_guard<std::mutex> lock(m_mutex);
  // Refuse a non-positive or non-finite live price instead of storing it.
  // getEffectiveRate() already falls back to the seed when m_liveXfgUsd <= 0, but a
  // stored -1 is still a landmine for every other reader of getLiveXfgUsd().
  if (!(usd > 0.0) || !std::isfinite(usd)) return;
  m_liveXfgUsd = usd;
}

double PriceOracle::getLiveXfgUsd() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_liveXfgUsd;
}

double PriceOracle::getEffectiveRate(SwapPair pair) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  double xfgUsd = (m_liveXfgUsd > 0.0) ? m_liveXfgUsd : SEED_XFG_USD;
  switch (pair) {
    case SwapPair::SOL: return SEED_SOL_USD / xfgUsd;
    case SwapPair::ETH: return SEED_ETH_USD / xfgUsd;
    case SwapPair::BCH: return SEED_BCH_USD / xfgUsd;
    case SwapPair::XMR: return SEED_XMR_USD / xfgUsd;
    case SwapPair::ARB: return SEED_ETH_USD / xfgUsd;
    case SwapPair::BASE: return SEED_ETH_USD / xfgUsd;
    case SwapPair::BNB: return SEED_BNB_USD / xfgUsd;
    case SwapPair::DCR: return SEED_DCR_USD / xfgUsd;
    case SwapPair::POLYGON: return SEED_POLY_USD / xfgUsd;
    case SwapPair::GLEEC: return SEED_GLEEC_USD / xfgUsd;
    case SwapPair::ROBINHOOD: return SEED_ETH_USD / xfgUsd;  // ETH gas
    case SwapPair::AVAX: return SEED_AVAX_USD / xfgUsd;
    case SwapPair::CRO: return SEED_CRO_USD / xfgUsd;
    case SwapPair::BOB: return SEED_ETH_USD / xfgUsd;        // ETH gas
    case SwapPair::SIA: return SEED_SIA_USD / xfgUsd;
    case SwapPair::TON: return SEED_TON_USD / xfgUsd;
    case SwapPair::UNICHAIN: return SEED_ETH_USD / xfgUsd;   // ETH gas
    case SwapPair::PLASMA: return SEED_PLASMA_USD / xfgUsd;
    case SwapPair::DOGE: return SEED_DOGE_USD / xfgUsd;
    case SwapPair::DASH: return SEED_DASH_USD / xfgUsd;
    case SwapPair::ZEC: return SEED_ZEC_USD / xfgUsd;
    case SwapPair::PULSECHAIN: return SEED_PULSECHAIN_USD / xfgUsd;
    case SwapPair::ZANO: return SEED_ZANO_USD / xfgUsd;
    case SwapPair::BTC: return SEED_BTC_USD / xfgUsd;
    case SwapPair::LTC: return SEED_LTC_USD / xfgUsd;
    case SwapPair::KMD_SPV: return SEED_KMD_USD / xfgUsd;
    case SwapPair::MONAD: return SEED_MONAD_USD / xfgUsd;
    case SwapPair::OPTIMISM: return SEED_OP_USD / xfgUsd;
    case SwapPair::DOT: return SEED_DOT_USD / xfgUsd;
    default:            return 0.0;
  }
}

double PriceOracle::getSeedRate(SwapPair pair) {
  // Returns: how many XFG per 1 whole counterparty coin
  switch (pair) {
    case SwapPair::SOL: return SEED_SOL_USD / SEED_XFG_USD;
    case SwapPair::ETH: return SEED_ETH_USD / SEED_XFG_USD;
    case SwapPair::BCH: return SEED_BCH_USD / SEED_XFG_USD;
    case SwapPair::XMR: return SEED_XMR_USD / SEED_XFG_USD;
    case SwapPair::ARB: return SEED_ETH_USD / SEED_XFG_USD;
    case SwapPair::BASE: return SEED_ETH_USD / SEED_XFG_USD;
    case SwapPair::BNB: return SEED_BNB_USD / SEED_XFG_USD;
    case SwapPair::DCR: return SEED_DCR_USD / SEED_XFG_USD;
    case SwapPair::POLYGON: return SEED_POLY_USD / SEED_XFG_USD;
    case SwapPair::GLEEC: return SEED_GLEEC_USD / SEED_XFG_USD;
    case SwapPair::ROBINHOOD: return SEED_ETH_USD / SEED_XFG_USD;
    case SwapPair::AVAX: return SEED_AVAX_USD / SEED_XFG_USD;
    case SwapPair::CRO: return SEED_CRO_USD / SEED_XFG_USD;
    case SwapPair::BOB: return SEED_ETH_USD / SEED_XFG_USD;
    case SwapPair::SIA: return SEED_SIA_USD / SEED_XFG_USD;
    case SwapPair::TON: return SEED_TON_USD / SEED_XFG_USD;
    case SwapPair::UNICHAIN: return SEED_ETH_USD / SEED_XFG_USD;
    case SwapPair::PLASMA: return SEED_PLASMA_USD / SEED_XFG_USD;
    case SwapPair::DOGE: return SEED_DOGE_USD / SEED_XFG_USD;
    case SwapPair::DASH: return SEED_DASH_USD / SEED_XFG_USD;
    case SwapPair::ZEC: return SEED_ZEC_USD / SEED_XFG_USD;
    case SwapPair::PULSECHAIN: return SEED_PULSECHAIN_USD / SEED_XFG_USD;
    case SwapPair::ZANO: return SEED_ZANO_USD / SEED_XFG_USD;
    case SwapPair::BTC: return SEED_BTC_USD / SEED_XFG_USD;
    case SwapPair::LTC: return SEED_LTC_USD / SEED_XFG_USD;
    case SwapPair::KMD_SPV: return SEED_KMD_USD / SEED_XFG_USD;
    case SwapPair::MONAD: return SEED_MONAD_USD / SEED_XFG_USD;
    // Optimism locks native ETH (HashedTimelock.lock is payable with no ERC-20 arg),
    // so it must be priced off ETH -- not off the OP token.
    case SwapPair::OPTIMISM: return SEED_ETH_USD / SEED_XFG_USD;
    case SwapPair::DOT: return SEED_DOT_USD / SEED_XFG_USD;
    default:            return 0.0;
  }
}

// =============================================================================
// CTR unit conversion
// =============================================================================

double PriceOracle::ctrDivisor(SwapPair pair) {
  const auto* descriptor = swapPairDescriptor(pair);
  // No descriptor means the pair has no known decimals. Returning a plausible 1e8
  // here silently mis-scales every amount comparison downstream, so fail visibly
  // instead; callers must treat 0.0 as "cannot convert".
  return descriptor ? std::pow(10.0, static_cast<double>(descriptor->decimals)) : 0.0;
}

double PriceOracle::atomicToRate(SwapPair pair, uint64_t xfgAmount, const AtomicAmount& ctrAmount) {
  if (ctrAmount == 0) return 0.0;

  // XFG: 7 decimals (COIN = 10,000,000)
  double xfgWhole = static_cast<double>(xfgAmount) / 1e7;
  double ctrWhole = ctrAmount.convert_to<double>() / ctrDivisor(pair);

  if (ctrWhole <= 0.0) return 0.0;

  // Rate = XFG per 1 whole CTR coin
  return xfgWhole / ctrWhole;
}

// =============================================================================
// TWAP: record + calculate
// =============================================================================

void PriceOracle::recordCompletedSwap(const CompletedSwapTrade& trade) {
  std::lock_guard<std::mutex> lock(m_mutex);

  // getTwap weights by rate*volume, so a trade recorded with an uncomputed rate of 0
  // would drag the TWAP toward zero instead of merely being ignored. Refuse it here
  // and let the caller keep publishing its own reason.
  if (trade.rate <= 0.0) return;

  m_trades.push_back(trade);

  // Trim to max history size (keep 10x window for multi-pair storage)
  while (m_trades.size() > m_twapMaxTrades * 10) {
    m_trades.pop_front();
  }
}

double PriceOracle::getTwap(SwapPair pair) const {
  std::lock_guard<std::mutex> lock(m_mutex);

  time_t now = std::time(nullptr);
  double weightedSum = 0.0;
  double volumeSum = 0.0;
  size_t count = 0;

  // Walk backwards through trades, newest first
  for (auto it = m_trades.rbegin(); it != m_trades.rend() && count < m_twapMaxTrades; ++it) {
    if (it->pair != pair) continue;

    // Skip stale trades
    if (m_twapMaxAgeSec > 0 && (now - it->timestamp) > static_cast<time_t>(m_twapMaxAgeSec)) {
      continue;
    }

    double volume = static_cast<double>(it->xfgAmount) / 1e7;  // XFG volume
    weightedSum += it->rate * volume;
    volumeSum += volume;
    ++count;
  }

  if (count < TWAP_MIN_TRADES || volumeSum <= 0.0) {
    return 0.0;  // not enough data, caller should use seed rate
  }

  return weightedSum / volumeSum;
}

size_t PriceOracle::getTradeCount(SwapPair pair) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  size_t count = 0;
  for (const auto& t : m_trades) {
    if (t.pair == pair) ++count;
  }
  return count;
}

// =============================================================================
// Rate validation: one-directional floor protection
// =============================================================================

RateCheck PriceOracle::validateRate(SwapPair pair, double proposedRate) const {
  if (proposedRate <= 0.0) return RateCheck::BELOW_FLOOR;

  // Get reference rate: TWAP if available, else seed
  double refRate = getTwap(pair);
  if (refRate <= 0.0) {
    // Not enough TWAP data — use seed rate if we have some trades but < minimum
    size_t trades = getTradeCount(pair);
    if (trades == 0) {
      // True bootstrap: enforce bounded drift from seed rate if configured
      if (m_maxBootstrapDrift > 0.0) {
        double seed = getSeedRate(pair);
        if (seed > 0.0) {
          if (proposedRate < seed * (1.0 - m_maxBootstrapDrift)) {
            return RateCheck::BELOW_FLOOR;
          }
          if (proposedRate > seed * (1.0 + m_maxBootstrapDrift)) {
            return RateCheck::ABOVE_MARKET;
          }
        }
      }
      return RateCheck::RATE_NO_DATA;
    }
    // Have some trades but < TWAP_MIN_TRADES: use seed as soft reference
    refRate = getSeedRate(pair);
    if (refRate <= 0.0) return RateCheck::RATE_NO_DATA;
  }

  // Rate = XFG per 1 whole CTR coin, so a HIGH rate sells XFG cheaply (the seller
  // gives away more XFG) and a LOW rate sells XFG expensively. Both directions carry
  // risk -- the first against the seller, the second against the buyer -- so the
  // band is symmetric about the reference and both tails are rejected.
  //
  // m_floorThreshold is the fractional tolerance (0.20 = +/-20%), which yields
  //     ref / 1.20 <= rate <= ref / 0.80
  // matching PricePolicy::withinRateGuard, so the live and legacy paths enforce the
  // same band. The previous code divided by 0.80 above but compared against a
  // hardcoded 0.20 below, which admitted a -80% quote while the header claimed 0.50.
  if (m_floorThreshold <= 0.0 || m_floorThreshold >= 1.0) return RateCheck::RATE_NO_DATA;

  const double tol = m_floorThreshold;
  if (proposedRate > refRate / (1.0 - tol)) {
    return RateCheck::BELOW_FLOOR;
  }

  if (proposedRate < refRate / (1.0 + tol)) {
    return RateCheck::ABOVE_MARKET;
  }

  return RateCheck::OK;
}

RateCheck PriceOracle::validateSwapAmounts(SwapPair pair, uint64_t xfgAmount, const AtomicAmount& ctrAmount) const {
  double rate = atomicToRate(pair, xfgAmount, ctrAmount);
  return validateRate(pair, rate);
}

const char* PriceOracle::rateCheckToString(RateCheck rc) {
  switch (rc) {
    case RateCheck::OK:           return "OK";
    case RateCheck::BELOW_FLOOR:  return "REJECTED: rate too low (floor protection)";
    case RateCheck::ABOVE_MARKET: return "WARNING: rate significantly above market";
    case RateCheck::RATE_NO_DATA:      return "OK (no price data, bootstrap mode)";
    default:                      return "UNKNOWN";
  }
}

// =============================================================================
// Configuration
// =============================================================================

void PriceOracle::setTwapWindow(size_t maxTrades) {
  m_twapMaxTrades = maxTrades;
}

void PriceOracle::setTwapMaxAge(uint64_t seconds) {
  m_twapMaxAgeSec = seconds;
}

void PriceOracle::setFloorThreshold(double fraction) {
  // Fractional tolerance, not a divisor. Clamped so a bad config cannot silently
  // disable the band: at >= 1.0 the upper bound would divide by zero.
  if (fraction <= 0.0 || fraction >= 1.0) return;
  m_floorThreshold = fraction;
}

void PriceOracle::setMaxBootstrapDrift(double drift) {
  if (drift < 0.0 || drift > 1.0) {
    throw std::invalid_argument("maxBootstrapDrift must be in [0.0, 1.0]");
  }
  m_maxBootstrapDrift = drift;
}

} // namespace XfgSwap
