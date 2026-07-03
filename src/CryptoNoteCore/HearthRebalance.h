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

namespace CryptoNote {

struct RebalanceAction {
  enum Direction { NONE, SWAP_HEAT_FOR_XFG, SWAP_XFG_FOR_HEAT };
  Direction direction = NONE;
  uint64_t inputAmount = 0;   // HEAT (if HEAT→XFG) or XFG (if XFG→HEAT)
  uint64_t outputAmount = 0;  // XFG (if HEAT→XFG) or HEAT (if XFG→HEAT)
  uint64_t feeAmount = 0;     // fee paid to LPs
};

class HearthRebalance {
public:
  HearthRebalance(uint32_t feeBps);

  RebalanceAction computeRebalance(
    uint64_t currentXfgReserve,
    uint64_t currentHeatReserve,
    uint64_t P_clear,
    uint64_t depthBandPct);

  uint64_t applyRebalanceSwap(
    uint64_t inputAmount,
    uint64_t reserveIn,
    uint64_t reserveOut);

private:
  uint32_t m_feeBps;
};

} // namespace CryptoNote
