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

#include "MarketOrderExecutor.h"
#include "AmmPool.h"
#include "Common/Int128.h"

#include <algorithm>

namespace CryptoNote {

uint64_t computeDepthBand(uint64_t totalReserve, uint64_t depthPct) {
  // depth = reserve * pct / 100, in atomic units
  return (static_cast<uint128_t>(totalReserve) * depthPct) / 100;
}

MarketOrderExecutor::MarketOrderExecutor(
    uint64_t hearthDepthBandPct,
    uint32_t maxLevels,
    uint32_t maxPriceDeviationPct,
    uint32_t feeBps)
  : m_depthBandPct(hearthDepthBandPct)
  , m_maxLevels(maxLevels)
  , m_maxPriceDeviationPct(maxPriceDeviationPct)
  , m_feeBps(feeBps) {}

uint64_t MarketOrderExecutor::computeMaxPrice(uint64_t P_clear, bool buying) const {
  // Buying: willing to pay up to (1 + pct/100) * P_clear
  // Selling: willing to receive down to (1 - pct/100) * P_clear
  if (buying) {
    return P_clear + (static_cast<uint128_t>(P_clear) * m_maxPriceDeviationPct) / 100;
  } else {
    uint64_t reduction = (static_cast<uint128_t>(P_clear) * m_maxPriceDeviationPct) / 100;
    if (reduction >= P_clear) return 1;  // floor at 1
    return P_clear - reduction;
  }
}

uint64_t MarketOrderExecutor::executeSwapOnHearth(
    uint64_t inputAmount, uint64_t reserveIn, uint64_t reserveOut,
    uint64_t /*P_clear*/, bool /*isBuy*/) const {
  // Use the existing constant-product AMM math
  return ammGetOutputAmount(inputAmount, reserveIn, reserveOut, m_feeBps);
}

MarketOrderExecutor::CascadeState MarketOrderExecutor::cascadeIntoOrderbook(
    uint64_t remaining,
    uint64_t P_clear,
    OrderbookIndex& orderbook,
    bool isBuy) {

  CascadeState state = {0, 0, 0, 0, false};
  if (remaining == 0) return state;

  uint64_t maxPrice = computeMaxPrice(P_clear, isBuy);

  if (isBuy) {
    // Buying XFG: walk asks ascending
    const auto& askCurve = orderbook.getAskCurve();
    for (auto it = askCurve.begin(); it != askCurve.end() && remaining > 0; ++it) {
      uint64_t askPrice = it->first;
      if (askPrice > maxPrice) {
        state.halted = true;  // exceeded price guard
        break;
      }
      if (state.levels >= m_maxLevels) {
        state.halted = true;
        break;
      }

      // Accumulate depth at this price level
      uint64_t levelDepth = 0;
      for (const auto& entry : it->second) {
        levelDepth += entry.amount;
      }

      uint64_t takeAmount = std::min(remaining, levelDepth);
      uint64_t cost = (static_cast<uint128_t>(takeAmount) * askPrice) / 100000000ULL;
      state.filled += takeAmount;
      state.cost += cost;
      state.peakPrice = std::max(state.peakPrice, askPrice);
      state.levels++;
      remaining -= takeAmount;
    }
  } else {
    // Selling XFG: walk bids descending
    const auto& bidCurve = orderbook.getBidCurve();
    for (auto it = bidCurve.begin(); it != bidCurve.end() && remaining > 0; ++it) {
      uint64_t bidPrice = it->first;
      if (bidPrice < maxPrice) {
        state.halted = true;
        break;
      }
      if (state.levels >= m_maxLevels) {
        state.halted = true;
        break;
      }

      uint64_t levelDepth = 0;
      for (const auto& entry : it->second) {
        levelDepth += entry.amount;
      }

      uint64_t takeAmount = std::min(remaining, levelDepth);
      uint64_t cost = (static_cast<uint128_t>(takeAmount) * bidPrice) / 100000000ULL;
      state.filled += takeAmount;
      state.cost += cost;
      state.peakPrice = std::max(state.peakPrice, bidPrice);
      state.levels++;
      remaining -= takeAmount;
    }
  }

  if (remaining > 0 && !state.halted) {
    // Book exhausted without hitting guard
    state.halted = false;
  }

  return state;
}

MarketOrderResult MarketOrderExecutor::executeMarketBuy(
    uint64_t xfgWanted,
    uint64_t maxHeatCost,
    uint64_t P_clear,
    uint64_t xfgReserve,
    uint64_t heatReserve,
    OrderbookIndex& orderbook) {

  MarketOrderResult result = {0, 0, 0, 0, 0, 0, false};
  if (xfgWanted == 0 || xfgReserve == 0 || heatReserve == 0)
    return result;

  // Step 1: Consume HEARTH depth band
  uint64_t sellDepth = computeDepthBand(xfgReserve, m_depthBandPct);
  uint64_t bandXfg = std::min(xfgWanted, sellDepth);

  if (bandXfg > 0) {
    // HEARTH swap: "how much HEAT for this much XFG?"
    // outputAmount(input: HEAT going in, reserveIn=heat, reserveOut=xfg)
    // We want XFG out, so need to compute HEAT in
    uint64_t heatNeeded = ammGetInputAmount(bandXfg, heatReserve, xfgReserve, m_feeBps);
    if (heatNeeded == 0) heatNeeded = bandXfg; // fallback for tiny trades

    if (result.totalCost + heatNeeded <= maxHeatCost || maxHeatCost == 0) {
      result.filledAmount = bandXfg;
      result.totalCost = heatNeeded;
      result.hearthFilled = bandXfg;
      xfgWanted -= bandXfg;
    } // else: can't afford even band depth
  }

  // Step 2: Cascade into orderbook
  if (xfgWanted > 0) {
    CascadeState cascade = cascadeIntoOrderbook(xfgWanted, P_clear, orderbook, true);
    uint64_t remainingCost = maxHeatCost - result.totalCost;
    if (cascade.cost <= remainingCost || maxHeatCost == 0) {
      result.filledAmount += cascade.filled;
      result.totalCost += cascade.cost;
      result.orderbookFilled = cascade.filled;
      result.levelsConsumed = cascade.levels;
      result.halted = cascade.halted;
      result.maxPriceDeviation = cascade.peakPrice;
    }
  }

  return result;
}

MarketOrderResult MarketOrderExecutor::executeMarketSell(
    uint64_t xfgToSell,
    uint64_t minHeatReceive,
    uint64_t P_clear,
    uint64_t xfgReserve,
    uint64_t heatReserve,
    OrderbookIndex& orderbook) {

  MarketOrderResult result = {0, 0, 0, 0, 0, 0, false};
  if (xfgToSell == 0 || xfgReserve == 0 || heatReserve == 0)
    return result;

  // Step 1: Consume HEARTH buy band
  uint64_t buyDepth = computeDepthBand(heatReserve, m_depthBandPct);
  // Buy depth in HEAT terms; convert to XFG at current P_clear
  uint64_t bandHeatValue = (static_cast<uint128_t>(buyDepth) * P_clear) / 100000000ULL;
  uint64_t bandXfg = std::min(xfgToSell, bandHeatValue);

  if (bandXfg > 0) {
    uint64_t heatOut = executeSwapOnHearth(bandXfg, xfgReserve, heatReserve, P_clear, false);
    if (result.totalCost + heatOut >= minHeatReceive || minHeatReceive == 0) {
      result.filledAmount = bandXfg;
      result.totalCost = heatOut;
      result.hearthFilled = bandXfg;
      xfgToSell -= bandXfg;
    }
  }

  // Step 2: Cascade into orderbook
  if (xfgToSell > 0) {
    CascadeState cascade = cascadeIntoOrderbook(xfgToSell, P_clear, orderbook, false);
    if (result.totalCost + cascade.cost >= minHeatReceive || minHeatReceive == 0) {
      result.filledAmount += cascade.filled;
      result.totalCost += cascade.cost;
      result.orderbookFilled = cascade.filled;
      result.levelsConsumed = cascade.levels;
      result.halted = cascade.halted;
      result.maxPriceDeviation = cascade.peakPrice;
    }
  }

  return result;
}

} // namespace CryptoNote
