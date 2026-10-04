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
#include <cctype>
#include <string_view>

namespace XfgSwap {

static bool iequal(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    const auto ac = static_cast<unsigned char>(a[i]);
    const auto bc = static_cast<unsigned char>(b[i]);
    if (std::toupper(ac) != std::toupper(bc)) return false;
  }
  return true;
}

bool swapPairFromString(const std::string& s, SwapPair& out) {
  if (s.empty() || s.size() > 32) return false;
  for (const auto& descriptor : SWAP_PAIR_CATALOG) {
    if (iequal(s, descriptor.symbol) || iequal(s, descriptor.key)) {
      out = descriptor.pair;
      return true;
    }
  }

  // Compatibility aliases that are neither the protocol symbol nor config key.
  if (iequal(s, "SC"))       { out = SwapPair::SIA; return true; }
  if (iequal(s, "POLY"))     { out = SwapPair::POLYGON; return true; }
  if (iequal(s, "POLKADOT")) { out = SwapPair::DOT; return true; }
  return false;
}

SwapPair swapPairFromString(const std::string& s) {
  SwapPair out;
  if (!swapPairFromString(s, out))
    throw std::runtime_error("Unknown swap pair: " + s);
  return out;
}

const char* swapPairToString(SwapPair p) {
  const auto* descriptor = swapPairDescriptor(p);
  return descriptor ? descriptor->symbol : "???";
}

const char* swapLockTypeToString(SwapLockType t) {
  switch (t) {
    case SwapLockType::HTLC: return "HTLC";
    case SwapLockType::PTLC: return "PTLC";
    case SwapLockType::PTLC_HTLC_BRIDGE: return "PTLC_HTLC_BRIDGE";
  }
  return "???";
}

bool swapLockTypeFromString(const std::string& s, SwapLockType& out) {
  if (s.size() < 3 || s.size() > 20) return false;
  if (iequal(s, "HTLC")) { out = SwapLockType::HTLC; return true; }
  if (iequal(s, "PTLC")) { out = SwapLockType::PTLC; return true; }
  if (iequal(s, "PTLC_HTLC_BRIDGE")) { out = SwapLockType::PTLC_HTLC_BRIDGE; return true; }
  if (iequal(s, "BRIDGE")) { out = SwapLockType::PTLC_HTLC_BRIDGE; return true; }
  return false;
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
    // SPV confirmation
    case SwapState::ADAPTOR_WAITING_SPV:          return "ADAPTOR_WAITING_SPV";
    case SwapState::ADAPTOR_SECRET_CONFIRMED_SPV: return "ADAPTOR_SECRET_CONFIRMED_SPV";
    // AFK v2
    case SwapState::AFK_OFFER_LOCKED:        return "AFK_OFFER_LOCKED";
    case SwapState::AFK_OFFER_ACCEPTED:      return "AFK_OFFER_ACCEPTED";
    case SwapState::AFK_CLAIMED:             return "AFK_CLAIMED";
    case SwapState::AFK_REFUNDED:            return "AFK_REFUNDED";
  }
  return "???";
}

} // namespace XfgSwap
