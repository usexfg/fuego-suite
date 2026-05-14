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
// Seed prices: 1 XFG = $0.01 USD
// =============================================================================
//
// Counterparty prices (March 2026):
//   SOL = $170    →  1 SOL =  17,000 XFG
//   ETH = $2,140  →  1 ETH = 214,000 XFG
//   BCH = $469    →  1 BCH =  46,900 XFG
//   XMR = $343    →  1 XMR =  34,300 XFG
//
// These seed rates bootstrap the system before any swaps complete.
// Once >= 5 real swaps exist for a pair, TWAP takes over entirely.
// =============================================================================

static const double SEED_XFG_USD = 0.01;
static const double SEED_SOL_USD = 170.0;
static const double SEED_ETH_USD = 2140.0;
static const double SEED_BCH_USD = 469.0;
static const double SEED_XMR_USD = 343.0;

// Minimum completed swaps before TWAP replaces seed rate
static const size_t TWAP_MIN_TRADES = 5;

// =============================================================================
// Constructor
// =============================================================================

PriceOracle::PriceOracle()
  : m_twapMaxTrades(20)
  , m_twapMaxAgeSec(604800)   // 7 days
  , m_floorThreshold(0.50) {  // reject if < 50% of TWAP
}

// =============================================================================
// Seed rates
// =============================================================================

double PriceOracle::getSeedXfgUsd() {
  return SEED_XFG_USD;
}

double PriceOracle::getSeedRate(SwapPair pair) {
  // Returns: how many XFG per 1 whole counterparty coin
  switch (pair) {
    case SwapPair::SOL: return SEED_SOL_USD / SEED_XFG_USD;  //  17,000
    case SwapPair::ETH: return SEED_ETH_USD / SEED_XFG_USD;  // 214,000
    case SwapPair::BCH: return SEED_BCH_USD / SEED_XFG_USD;  //  46,900
    case SwapPair::XMR: return SEED_XMR_USD / SEED_XFG_USD;  //  34,300
    default:            return 0.0;
  }
}

// =============================================================================
// CTR unit conversion
// =============================================================================

double PriceOracle::ctrDivisor(SwapPair pair) {
  switch (pair) {
    case SwapPair::SOL: return 1e9;   // lamports (1 SOL = 1e9 lamports)
    case SwapPair::ETH: return 1e18;  // wei
    case SwapPair::BCH: return 1e8;   // satoshi
    case SwapPair::XMR: return 1e12;  // piconero
    default:            return 1e8;
  }
}

double PriceOracle::atomicToRate(SwapPair pair, uint64_t xfgAmount, uint64_t ctrAmount) {
  if (ctrAmount == 0) return 0.0;

  // XFG: 7 decimals (COIN = 10,000,000)
  double xfgWhole = static_cast<double>(xfgAmount) / 1e7;
  double ctrWhole = static_cast<double>(ctrAmount) / ctrDivisor(pair);

  if (ctrWhole <= 0.0) return 0.0;

  // Rate = XFG per 1 whole CTR coin
  return xfgWhole / ctrWhole;
}

// =============================================================================
// TWAP: record + calculate
// =============================================================================

void PriceOracle::recordCompletedSwap(const CompletedSwapTrade& trade) {
  std::lock_guard<std::mutex> lock(m_mutex);
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
      return RateCheck::RATE_NO_DATA;  // true bootstrap, no restriction
    }
    // Have some trades but < TWAP_MIN_TRADES: use seed as soft reference
    refRate = getSeedRate(pair);
    if (refRate <= 0.0) return RateCheck::RATE_NO_DATA;
  }

  // Floor protection: reject if proposed rate gives XFG sellers < 50% of fair value
  // "rate" = XFG per 1 CTR. Higher rate = MORE XFG for 1 CTR = CHEAPER XFG.
  // Selling XFG cheap = high rate. Protect sellers = reject if rate is TOO HIGH
  // (buyer getting too many XFG per CTR coin).
  //
  // Actually: from XFG seller's perspective, a HIGH rate means they're giving away
  // more XFG for the same CTR. So floor protection = reject if rate > refRate * 2.0
  // (seller gets less than 50% fair value per XFG).
  //
  // Conversely, a LOW rate means XFG is MORE expensive (fewer XFG per CTR).
  // We never block price going UP (XFG getting more expensive = lower rate).

  if (proposedRate > refRate / m_floorThreshold) {
    // Proposed rate gives too many XFG per CTR — seller is getting ripped off
    return RateCheck::BELOW_FLOOR;
  }

  if (proposedRate < refRate * 0.20) {
    // Rate is 5x+ below market (XFG priced 5x higher than TWAP) — warn but allow
    return RateCheck::ABOVE_MARKET;
  }

  return RateCheck::OK;
}

RateCheck PriceOracle::validateSwapAmounts(SwapPair pair, uint64_t xfgAmount, uint64_t ctrAmount) const {
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
  m_floorThreshold = fraction;
}

} // namespace XfgSwap
