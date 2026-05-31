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

static bool iequal3(const char* a, const char* b) {
  for (int i = 0; i < 3; ++i) {
    if (::toupper(a[i]) != ::toupper(b[i])) return false;
  }
  return true;
}

bool swapPairFromString(const std::string& s, SwapPair& out) {
  if (s.size() != 3) return false;
  const char* p = s.c_str();
  if (iequal3(p, "SOL")) { out = SwapPair::SOL; return true; }
  if (iequal3(p, "ETH")) { out = SwapPair::ETH; return true; }
  if (iequal3(p, "XMR")) { out = SwapPair::XMR; return true; }
  if (iequal3(p, "BCH")) { out = SwapPair::BCH; return true; }
  if (iequal3(p, "ARB")) { out = SwapPair::ARB; return true; }
  return false;
}

SwapPair swapPairFromString(const std::string& s) {
  SwapPair out;
  if (!swapPairFromString(s, out))
    throw std::runtime_error("Unknown swap pair: " + s);
  return out;
}

const char* swapPairToString(SwapPair p) {
  switch (p) {
    case SwapPair::SOL: return "SOL";
    case SwapPair::ETH: return "ETH";
    case SwapPair::XMR: return "XMR";
    case SwapPair::BCH: return "BCH";
    case SwapPair::ARB: return "ARB";
  }
  return "???";
}

const char* swapStateToString(SwapState s) {
  switch (s) {
    // Legacy HTLC flow
    case SwapState::INITIATED:               return "INITIATED";
    case SwapState::XFG_LOCKED:              return "XFG_LOCKED";
    case SwapState::CTR_LOCKED:              return "CTR_LOCKED";
    case SwapState::XFG_CLAIMED:             return "XFG_CLAIMED";
    case SwapState::CTR_CLAIMED:             return "CTR_CLAIMED";
    case SwapState::XFG_REFUNDED:            return "XFG_REFUNDED";
    case SwapState::CTR_REFUNDED:            return "CTR_REFUNDED";
    case SwapState::FAILED:                  return "FAILED";
    // Adaptor v1
    case SwapState::ADAPTOR_KEYS_EXCHANGED:  return "ADAPTOR_KEYS_EXCHANGED";
    case SwapState::ADAPTOR_ESCROW_FUNDED:   return "ADAPTOR_ESCROW_FUNDED";
    case SwapState::ADAPTOR_PRESIGS_READY:   return "ADAPTOR_PRESIGS_READY";
    case SwapState::ADAPTOR_CTR_LOCKED:      return "ADAPTOR_CTR_LOCKED";
    case SwapState::ADAPTOR_SECRET_REVEALED: return "ADAPTOR_SECRET_REVEALED";
    case SwapState::ADAPTOR_XFG_SPENT:       return "ADAPTOR_XFG_SPENT";
    case SwapState::ADAPTOR_REFUNDED:        return "ADAPTOR_REFUNDED";
    // AFK v2
    case SwapState::AFK_OFFER_LOCKED:        return "AFK_OFFER_LOCKED";
    case SwapState::AFK_OFFER_ACCEPTED:      return "AFK_OFFER_ACCEPTED";
    case SwapState::AFK_CLAIMED:             return "AFK_CLAIMED";
    case SwapState::AFK_REFUNDED:            return "AFK_REFUNDED";
  }
  return "???";
}

} // namespace XfgSwap
