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

#include "SwapTypes.h"
#include <stdexcept>
#include <algorithm>

namespace XfgSwap {

const char* swapStateToString(SwapState s) {
  switch (s) {
    // Legacy HTLC states (kept for DB compat)
    case SwapState::INITIATED:    return "INITIATED";
    case SwapState::XFG_LOCKED:   return "XFG_LOCKED";
    case SwapState::CTR_LOCKED:   return "CTR_LOCKED";
    case SwapState::XFG_CLAIMED:  return "XFG_CLAIMED";
    case SwapState::CTR_CLAIMED:  return "CTR_CLAIMED";
    case SwapState::XFG_REFUNDED: return "XFG_REFUNDED";
    case SwapState::CTR_REFUNDED: return "CTR_REFUNDED";
    case SwapState::FAILED:       return "FAILED";
    // Adaptor signature flow
    case SwapState::ADAPTOR_KEYS_EXCHANGED:  return "KEYS_EXCHANGED";
    case SwapState::ADAPTOR_ESCROW_FUNDED:   return "ESCROW_FUNDED";
    case SwapState::ADAPTOR_PRESIGS_READY:   return "PRESIGS_READY";
    case SwapState::ADAPTOR_CTR_LOCKED:      return "CTR_LOCKED(adaptor)";
    case SwapState::ADAPTOR_SECRET_REVEALED: return "SECRET_REVEALED";
    case SwapState::ADAPTOR_XFG_SPENT:       return "XFG_SPENT";
    case SwapState::ADAPTOR_REFUNDED:        return "REFUNDED(adaptor)";
    default:                                 return "UNKNOWN";
  }
}

const char* swapPairToString(SwapPair p) {
  switch (p) {
    case SwapPair::SOL: return "SOL";
    case SwapPair::ETH: return "ETH";
    case SwapPair::XMR: return "XMR";
    case SwapPair::BCH: return "BCH";
    default:            return "UNKNOWN";
  }
}

SwapPair swapPairFromString(const std::string& s) {
  std::string upper = s;
  std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
  if (upper == "SOL") return SwapPair::SOL;
  if (upper == "ETH") return SwapPair::ETH;
  if (upper == "XMR") return SwapPair::XMR;
  if (upper == "BCH") return SwapPair::BCH;
  throw std::runtime_error("Unknown swap pair: " + s);
}

} // namespace XfgSwap
