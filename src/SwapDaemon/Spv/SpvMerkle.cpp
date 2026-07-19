// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the implied
// warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You can redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation. Fuego includes elements written
// by third parties. See file labeled LICENSE for more details.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

#include "SpvMerkle.h"
#include "../BitcoinCash/HtlcScript.h"
#include <algorithm>

namespace XfgSwap {

// Reverse a byte vector in-place and return it (for display <-> internal conversion).
static std::vector<uint8_t> rev(std::vector<uint8_t> v) {
  std::reverse(v.begin(), v.end());
  return v;
}

std::string SpvMerkle::computeRootHexDisplay(
    const std::string& txidDisplay,
    const std::vector<std::string>& branchDisplay,
    uint32_t pos) {
  // Start with the txid converted from display (BE) hex to internal (LE) bytes.
  std::vector<uint8_t> cur = rev(BchHtlcScript::hexToBytes(txidDisplay));

  for (const auto& bh : branchDisplay) {
    std::vector<uint8_t> b = rev(BchHtlcScript::hexToBytes(bh));
    std::vector<uint8_t> cat;
    if (pos & 1u) {
      // Branch is to the left: dsha256(branch || cur)
      cat = b;
      cat.insert(cat.end(), cur.begin(), cur.end());
    } else {
      // Branch is to the right: dsha256(cur || branch)
      cat = cur;
      cat.insert(cat.end(), b.begin(), b.end());
    }
    cur = BchHtlcScript::doubleSha256(cat);
    pos >>= 1;
  }

  // Convert back to display (BE) hex.
  return BchHtlcScript::bytesToHex(rev(cur));
}

} // namespace XfgSwap
