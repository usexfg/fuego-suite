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

#pragma once

#include "SwapTypes.h"
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <cstdint>
#include <ctime>

namespace XfgSwap {

// A completed swap trade record used for TWAP calculation.
struct CompletedSwapTrade {
  SwapPair pair;
  uint64_t xfgAmount;     // atomic units (7 decimals)
  uint64_t ctrAmount;     // atomic units (pair-dependent decimals)
  double   rate;           // XFG per 1 CTR unit (human-readable)
  uint32_t blockHeight;    // XFG block height when HTLC was claimed
  time_t   timestamp;
};

// Swap rate validation result
enum class RateCheck {
  OK,                // rate is acceptable
  BELOW_FLOOR,       // rate is >= 50% below TWAP — REJECT
  ABOVE_MARKET,      // rate is significantly above TWAP — WARN but allow
  RATE_NO_DATA       // no TWAP yet, allow freely (bootstrap phase)
};

class PriceOracle {
public:
  PriceOracle();

  // ---------------------------------------------------------------------------
  // Seed prices (used before any swaps complete)
  // Derived from: 1 XFG = $0.01 USD seed
  // ---------------------------------------------------------------------------

  // Get the seed rate for a pair: XFG amount per 1 whole CTR coin.
  // e.g., ETH: 214,000 XFG per 1 ETH
  static double getSeedRate(SwapPair pair);

  // Get seed XFG price in USD
  static double getSeedXfgUsd();

  // ---------------------------------------------------------------------------
  // TWAP from completed swaps (self-referencing price discovery)
  // ---------------------------------------------------------------------------

  // Record a completed swap (called when an HTLC claim is detected on-chain)
  void recordCompletedSwap(const CompletedSwapTrade& trade);

  // Get volume-weighted average price (TWAP) for a pair.
  // Returns XFG per 1 whole CTR coin. Returns 0 if no data for this pair.
  // Uses last maxTrades completed swaps within maxAgeSec window.
  double getTwap(SwapPair pair) const;

  // Get number of completed swaps recorded for a pair
  size_t getTradeCount(SwapPair pair) const;

  // ---------------------------------------------------------------------------
  // Rate validation (one-directional floor protection)
  // ---------------------------------------------------------------------------

  // Validate a proposed swap rate.
  //   proposedRate = XFG per 1 whole CTR coin
  //
  // Rules:
  //   - If no TWAP data: OK (bootstrap, no restriction)
  //   - If rate < TWAP * 0.50: BELOW_FLOOR (reject — protects XFG sellers)
  //   - If rate > TWAP * 5.00: ABOVE_MARKET (warn, allow — don't cap appreciation)
  //   - Otherwise: OK
  RateCheck validateRate(SwapPair pair, double proposedRate) const;

  // Convenience: validate from atomic amounts
  RateCheck validateSwapAmounts(SwapPair pair, uint64_t xfgAmount, uint64_t ctrAmount) const;

  // Human-readable result
  static const char* rateCheckToString(RateCheck rc);

  // ---------------------------------------------------------------------------
  // Configuration
  // ---------------------------------------------------------------------------

  // Max trades to consider for TWAP (default: 20)
  void setTwapWindow(size_t maxTrades);

  // Max age of trades for TWAP in seconds (default: 7 days)
  void setTwapMaxAge(uint64_t seconds);

  // Floor protection threshold as fraction of TWAP (default: 0.50 = 50%)
  void setFloorThreshold(double fraction);

private:
  // Completed swap history per pair
  mutable std::mutex m_mutex;
  std::deque<CompletedSwapTrade> m_trades;
  size_t   m_twapMaxTrades;    // default 20
  uint64_t m_twapMaxAgeSec;    // default 604800 (7 days)
  double   m_floorThreshold;   // default 0.50

  // Convert atomic amounts to a rate (XFG per 1 whole CTR coin)
  static double atomicToRate(SwapPair pair, uint64_t xfgAmount, uint64_t ctrAmount);

  // CTR divisor for atomic→whole conversion
  static double ctrDivisor(SwapPair pair);
};

} // namespace XfgSwap
