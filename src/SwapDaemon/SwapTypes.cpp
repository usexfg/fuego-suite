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

} // namespace XfgSwap
