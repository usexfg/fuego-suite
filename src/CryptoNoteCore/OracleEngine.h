// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See file labeled LICENSE for more details.

#pragma once

#include <cstdint>
#include <vector>

namespace CryptoNote {

// OracleEngine: provides XFG/USD price derived from atomic swap TWAPs.
//
// Computes a XFG fiat price from cross-chain atomic swap exchange rates:
//   XFG/USD = avg(swap rates) across all configured pair TWAPs
//
// The PI controller uses this to keep HEAT stable in USD terms.
// HEAT remains small-value (~$2-3) even if XFG fiat value moons.
//
// When no swap data is available (e.g., no swaps yet), the PI runs
// in XFG-only mode where HEAT tracks XFG value.

struct SwapPairPrice {
  uint64_t xfgPerUnit;     // XFG atomic units per 1 unit of counterparty
  uint64_t cpUsdPrice;     // counterparty unit price in USD cents (scaled by 100)

  // Convenience: XFG per USD = xfgPerUnit / (cpUsdPrice / 100)
};

class OracleEngine {
public:
  OracleEngine();

  // Feed swap TWAP data into the oracle.
  // Called at epoch boundary with recent swap pair prices.
  void feedSwapPrices(const std::vector<SwapPairPrice>& pairs);

  // Get current XFG/USD rate: XFG atomic units per $1.00.
  // Returns 0 if no swap data (PI runs in XFG-only mode).
  uint64_t getXfgPerUsd() const { return m_xfgPerUsd; }

  // Configure manually (fallback / governance override).
  void overrideRate(uint64_t xfgPerUsd) { m_xfgPerUsd = xfgPerUsd; }

private:
  uint64_t m_xfgPerUsd = 0;

  // Compute XFG/USD from swap pair TWAPs:
  //   For each pair: xfgPerUsd_pair = xfgPerUnit / (cpUsdPrice / 100)
  //   Aggregate: median or average of valid pairs
  uint64_t computeFromPairs(const std::vector<SwapPairPrice>& pairs);
};

} // namespace CryptoNote
