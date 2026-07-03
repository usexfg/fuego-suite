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

#include "HearthRebalance.h"
#include "AmmPool.h"
#include "Common/Int128.h"

#include <algorithm>

namespace CryptoNote {

HearthRebalance::HearthRebalance(uint32_t feeBps)
  : m_feeBps(feeBps) {}

uint64_t HearthRebalance::applyRebalanceSwap(
    uint64_t inputAmount,
    uint64_t reserveIn,
    uint64_t reserveOut) {
  return ammGetOutputAmount(inputAmount, reserveIn, reserveOut, m_feeBps);
}

RebalanceAction HearthRebalance::computeRebalance(
    uint64_t currentXfgReserve,
    uint64_t currentHeatReserve,
    uint64_t P_clear,
    uint64_t depthBandPct) {

  RebalanceAction action;
  if (currentXfgReserve == 0 || currentHeatReserve == 0 || P_clear == 0)
    return action;

  // Current pool ratio: xfgReserve / heatReserve scaled to 10^8
  uint64_t currentRatio = (static_cast<uint128_t>(currentXfgReserve) * 100000000ULL) / currentHeatReserve;

  // Tolerance: if ratio is within 0.1% of P_clear, skip rebalance
  uint64_t tolerance = P_clear / 1000;  // 0.1%
  if (currentRatio >= P_clear - tolerance && currentRatio <= P_clear + tolerance)
    return action;

  // Cap rebalance size to depth band to prevent draining treasury in one block
  uint64_t maxRebalanceXfg = (static_cast<uint128_t>(currentXfgReserve) * depthBandPct) / 100;
  uint64_t maxRebalanceHeat = (static_cast<uint128_t>(currentHeatReserve) * depthBandPct) / 100;

  if (currentRatio < P_clear) {
    // Pool is HEAT-heavy (XFG too cheap, ratio too low).
    // To INCREASE ratio toward P_clear: swap XFG→HEAT (add XFG, remove HEAT).
    action.direction = RebalanceAction::SWAP_XFG_FOR_HEAT;

    uint64_t lo = 0, hi = maxRebalanceXfg;
    while (lo < hi) {
      uint64_t mid = (lo + hi + 1) / 2;
      uint64_t heatGot = ammGetOutputAmount(mid, currentXfgReserve, currentHeatReserve, m_feeBps);
      if (heatGot == 0) { hi = mid - 1; continue; }

      uint64_t newXfg = currentXfgReserve + mid;
      uint64_t newHeat = currentHeatReserve - heatGot;
      if (newXfg == 0 || newHeat == 0) { hi = mid - 1; continue; }

      uint64_t newRatio = (static_cast<uint128_t>(newXfg) * 100000000ULL) / newHeat;
      if (newRatio < P_clear) {
        lo = mid;
      } else {
        hi = mid - 1;
      }
    }

    if (lo > 0) {
      uint64_t heatGot = ammGetOutputAmount(lo, currentXfgReserve, currentHeatReserve, m_feeBps);
      action.inputAmount = lo;
      action.outputAmount = heatGot;
      uint64_t grossHeat = ammGetOutputAmount(lo, currentXfgReserve, currentHeatReserve, 0);
      action.feeAmount = (grossHeat > heatGot) ? grossHeat - heatGot : 0;
    }
  } else {
    // Pool is XFG-heavy (HEAT too cheap, ratio too high).
    // To DECREASE ratio toward P_clear: swap HEAT→XFG (add HEAT, remove XFG).
    action.direction = RebalanceAction::SWAP_HEAT_FOR_XFG;

    uint64_t lo = 0, hi = maxRebalanceHeat;
    while (lo < hi) {
      uint64_t mid = (lo + hi + 1) / 2;
      uint64_t xfgGot = ammGetOutputAmount(mid, currentHeatReserve, currentXfgReserve, m_feeBps);
      if (xfgGot == 0) { hi = mid - 1; continue; }

      uint64_t newHeat = currentHeatReserve + mid;
      uint64_t newXfg = currentXfgReserve - xfgGot;
      if (newXfg == 0 || newHeat == 0) { hi = mid - 1; continue; }

      uint64_t newRatio = (static_cast<uint128_t>(newXfg) * 100000000ULL) / newHeat;
      if (newRatio > P_clear) {
        lo = mid;
      } else {
        hi = mid - 1;
      }
    }

    if (lo > 0) {
      uint64_t xfgGot = ammGetOutputAmount(lo, currentHeatReserve, currentXfgReserve, m_feeBps);
      action.inputAmount = lo;
      action.outputAmount = xfgGot;
      uint64_t grossXfg = ammGetOutputAmount(lo, currentHeatReserve, currentXfgReserve, 0);
      action.feeAmount = (grossXfg > xfgGot) ? grossXfg - xfgGot : 0;
    }
  }

  return action;
}

} // namespace CryptoNote
