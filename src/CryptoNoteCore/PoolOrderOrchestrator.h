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
#include <deque>

namespace CryptoNote {

class PoolOrderOrchestrator {
public:
  PoolOrderOrchestrator();

  // Update with latest market state, returns true if pool orders should be regenerated
  bool shouldRegenerate(
    uint64_t currentPclear,
    uint64_t priorPclear,
    uint64_t poolXfgReserve,
    uint64_t poolHeatReserve,
    uint64_t priorPoolXfgReserve,
    uint64_t priorPoolHeatReserve,
    uint64_t bandFilledThisBlock,   // how much of pool band was consumed
    uint64_t totalBandPlaced,        // total band depth placed last cycle
    uint32_t blocksSinceLastRegen
  );

  // Compute adaptive spread in bps (basis points, 1 = 0.01%)
  uint32_t computeSpreadBps(
    uint64_t P_clear,
    uint64_t bandFilledThisBlock,
    uint64_t totalBandPlaced
  );

  // Record P_clear snapshot for volatility tracking
  void recordPrice(uint64_t P_clear);

  // 30-block trailing average of P_clear (primary spot price at v11+)
  uint64_t getAveragePrice() const;

  // Current number of recorded prices
  size_t priceHistorySize() const { return m_priceHistory.size(); }

private:
  std::deque<uint64_t> m_priceHistory;  // last 30 blocks of P_clear
  uint64_t m_lastRegenPclear = 0;
  uint64_t m_lastRegenXfgReserve = 0;
  uint64_t m_lastRegenHeatReserve = 0;

  static constexpr uint32_t MAX_BLOCKS_WITHOUT_REGEN = 10;
  static constexpr uint32_t PRICE_CHANGE_THRESHOLD_BPS = 50;   // 0.5%
  static constexpr uint32_t RESERVE_CHANGE_THRESHOLD_BPS = 100; // 1%
  static constexpr uint32_t BAND_CONSUMPTION_THRESHOLD_PCT = 50; // 50%
  static constexpr uint32_t PRICE_HISTORY_SIZE = 30;
  static constexpr uint32_t BASE_SPREAD_BPS = 30;               // 0.3% base
  static constexpr uint32_t MAX_SPREAD_BPS = 300;               // 3% ceiling
};

} // namespace CryptoNote
