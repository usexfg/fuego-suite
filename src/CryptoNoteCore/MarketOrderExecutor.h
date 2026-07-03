// Copyright (c) 2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You can redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <cstdint>
#include "OrderbookIndex.h"

namespace CryptoNote {

struct MarketOrderResult {
  uint64_t filledAmount;     // XFG atomic units (buy) or HEAT atomic (sell)
  uint64_t totalCost;        // HEAT atomic (buy) or XFG atomic (sell)
  uint64_t hearthFilled;     // amount filled from HEARTH band
  uint64_t orderbookFilled;  // amount filled from orderbook
  uint32_t levelsConsumed;   // orderbook price levels consumed
  uint64_t maxPriceDeviation; // worst price encountered (for estimate)
  bool halted;               // true if guard stopped the order
};

class MarketOrderExecutor {
public:
  MarketOrderExecutor(uint64_t hearthDepthBandPct, uint32_t maxLevels,
                       uint32_t maxPriceDeviationPct, uint32_t feeBps);

  MarketOrderResult executeMarketBuy(
    uint64_t xfgWanted,
    uint64_t maxHeatCost,
    uint64_t P_clear,
    uint64_t xfgReserve,
    uint64_t heatReserve,
    OrderbookIndex& orderbook);

  MarketOrderResult executeMarketSell(
    uint64_t xfgToSell,
    uint64_t minHeatReceive,
    uint64_t P_clear,
    uint64_t xfgReserve,
    uint64_t heatReserve,
    OrderbookIndex& orderbook);

private:
  uint64_t m_depthBandPct;
  uint32_t m_maxLevels;
  uint32_t m_maxPriceDeviationPct;
  uint32_t m_feeBps;

  uint64_t computeMaxPrice(uint64_t P_clear, bool buying) const;
  uint64_t executeSwapOnHearth(
    uint64_t inputAmount, uint64_t reserveIn, uint64_t reserveOut,
    uint64_t P_clear, bool isBuy) const;

  struct CascadeState {
    uint64_t filled;
    uint64_t cost;
    uint32_t levels;
    uint64_t peakPrice;
    bool halted;
  };

  CascadeState cascadeIntoOrderbook(
    uint64_t remaining,
    uint64_t P_clear,
    OrderbookIndex& orderbook,
    bool isBuy);
};

uint64_t computeDepthBand(uint64_t totalReserve, uint64_t depthPct);

} // namespace CryptoNote
