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

#include "PoolOrderOrchestrator.h"
#include "Common/Int128.h"

#include <algorithm>
#include <cmath>

namespace CryptoNote {

PoolOrderOrchestrator::PoolOrderOrchestrator() {}

bool PoolOrderOrchestrator::shouldRegenerate(
    uint64_t currentPclear,
    uint64_t priorPclear,
    uint64_t poolXfgReserve,
    uint64_t poolHeatReserve,
    uint64_t priorPoolXfgReserve,
    uint64_t priorPoolHeatReserve,
    uint64_t bandFilledThisBlock,
    uint64_t totalBandPlaced,
    uint32_t blocksSinceLastRegen) {

  // Floor: regenerate at least every N blocks to keep orders fresh
  if (blocksSinceLastRegen >= MAX_BLOCKS_WITHOUT_REGEN)
    return true;

  // First run
  if (m_lastRegenPclear == 0)
    return true;

  // 1. Price moved significantly
  if (priorPclear > 0) {
    uint64_t delta = (currentPclear > priorPclear)
      ? currentPclear - priorPclear
      : priorPclear - currentPclear;
    uint64_t threshold = (static_cast<uint128_t>(priorPclear) * PRICE_CHANGE_THRESHOLD_BPS) / 10000;
    if (delta > threshold)
      return true;
  }

  // 2. Pool reserves changed (LP deposit/withdrawal)
  if (priorPoolXfgReserve > 0 && priorPoolHeatReserve > 0) {
    uint64_t xfgDelta = (poolXfgReserve > priorPoolXfgReserve)
      ? poolXfgReserve - priorPoolXfgReserve
      : priorPoolXfgReserve - poolXfgReserve;
    uint64_t xfgThreshold = (static_cast<uint128_t>(priorPoolXfgReserve) * RESERVE_CHANGE_THRESHOLD_BPS) / 10000;

    uint64_t heatDelta = (poolHeatReserve > priorPoolHeatReserve)
      ? poolHeatReserve - priorPoolHeatReserve
      : priorPoolHeatReserve - poolHeatReserve;
    uint64_t heatThreshold = (static_cast<uint128_t>(priorPoolHeatReserve) * RESERVE_CHANGE_THRESHOLD_BPS) / 10000;

    if (xfgDelta > xfgThreshold || heatDelta > heatThreshold)
      return true;
  }

  // 3. Band consumption exceeded threshold (pool orders being eaten)
  if (totalBandPlaced > 0) {
    uint64_t consumptionPct = (static_cast<uint128_t>(bandFilledThisBlock) * 100) / totalBandPlaced;
    if (consumptionPct >= BAND_CONSUMPTION_THRESHOLD_PCT)
      return true;
  }

  // Update state after decision
  m_lastRegenPclear = currentPclear;
  m_lastRegenXfgReserve = poolXfgReserve;
  m_lastRegenHeatReserve = poolHeatReserve;

  return false;
}

uint32_t PoolOrderOrchestrator::computeSpreadBps(
    uint64_t P_clear,
    uint64_t bandFilledThisBlock,
    uint64_t totalBandPlaced) {

  uint32_t spread = BASE_SPREAD_BPS;

  // Volatility multiplier: 30-block stddev as fraction of P_clear
  if (m_priceHistory.size() >= 2 && P_clear > 0) {
    // Compute mean
    uint64_t sum = 0;
    for (auto p : m_priceHistory) sum += p;
    uint64_t mean = sum / static_cast<uint64_t>(m_priceHistory.size());

    // Compute variance
    uint64_t varSum = 0;
    for (auto p : m_priceHistory) {
      int64_t diff = static_cast<int64_t>(p) - static_cast<int64_t>(mean);
      varSum += static_cast<uint64_t>(diff * diff);
    }
    uint64_t variance = varSum / static_cast<uint64_t>(m_priceHistory.size());
    double stddev = std::sqrt(static_cast<double>(variance));

    // volatility = stddev / P_clear as basis points
    double volBps = (stddev / static_cast<double>(P_clear)) * 10000.0;
    uint32_t volMultiplier = static_cast<uint32_t>(1.0 + volBps / 100.0);
    spread = std::max(spread, BASE_SPREAD_BPS * volMultiplier);
  }

  // Consumption multiplier: wider spread if pool orders are being eaten fast
  if (totalBandPlaced > 0) {
    uint64_t consumptionPct = (static_cast<uint128_t>(bandFilledThisBlock) * 100) / totalBandPlaced;
    // Linear: 0% consumed → 1x, 100% consumed → 2x
    uint32_t consumptionMultiplier = 100 + static_cast<uint32_t>(consumptionPct);
    spread = std::max(spread, (BASE_SPREAD_BPS * consumptionMultiplier) / 100);
  }

  return std::min(spread, MAX_SPREAD_BPS);
}

void PoolOrderOrchestrator::recordPrice(uint64_t P_clear) {
  m_priceHistory.push_back(P_clear);
  if (m_priceHistory.size() > PRICE_HISTORY_SIZE)
    m_priceHistory.pop_front();
}

} // namespace CryptoNote
