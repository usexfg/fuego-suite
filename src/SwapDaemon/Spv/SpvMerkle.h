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

#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace XfgSwap {

// Pure Merkle branch verifier for SPV light-client use.
//
// Takes a txid, a list of branch hashes (from Electrum's get_merkle),
// and a position, then computes the Merkle root.  The result is compared
// against the block header's merkleRoot to prove transaction inclusion.
//
// Fold rule (Electrum convention):
//   cur = txid (internal LE bytes)
//   for each branch hash:
//     if (pos & 1)  cur = dsha256(branch || cur)
//     else           cur = dsha256(cur || branch)
//     pos >>= 1
//   result == merkleRoot (internal LE bytes)
//
// Inputs are in display (big-endian) hex; internal byte-reversal
// is handled inside.
class SpvMerkle {
public:
  // Compute the Merkle root from a txid and branch hashes.
  // All hex strings are in display (big-endian) byte order.
  // Returns the computed root in display hex.
  static std::string computeRootHexDisplay(
      const std::string& txidDisplay,
      const std::vector<std::string>& branchDisplay,
      uint32_t pos);
};

} // namespace XfgSwap
