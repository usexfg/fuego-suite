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

#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace XfgSwap {

// Bitcoin-family 80-byte block header for SPV verification.
//
// Layout (80 bytes, little-endian):
//   version    (4 bytes LE)
//   prevHash   (32 bytes, internal LE)
//   merkleRoot (32 bytes, internal LE)
//   time       (4 bytes LE)
//   bits       (4 bytes LE, compact nBits target)
//   nonce      (4 bytes LE)
//
// Block hash = dsha256(80-byte serialization), displayed reversed (big-endian hex).
struct SpvHeader {
  uint32_t version = 0;
  std::vector<uint8_t> prevHash;     // 32 bytes, internal LE
  std::vector<uint8_t> merkleRoot;   // 32 bytes, internal LE
  uint32_t time = 0;
  uint32_t bits = 0;
  uint32_t nonce = 0;

  // Parse 80 raw bytes into header fields
  static SpvHeader parse(const std::vector<uint8_t>& raw80);

  // Serialize back to 80 bytes
  std::vector<uint8_t> serialize() const;

  // Compute block hash: dsha256(80 bytes), returns 32 bytes internal LE
  std::vector<uint8_t> hash() const;

  // Display (big-endian hex) helpers
  std::string hashDisplay() const;
  std::string prevHashDisplay() const;
  std::string merkleRootDisplay() const;

  // Convert compact nBits to 32-byte big-endian target
  static std::vector<uint8_t> nBitsToTargetBE(uint32_t bits);

  // Check if hash meets PoW requirement: hashBE <= targetBE
  bool meetsPoW() const;

  // Approximate work: 2^256 / (target + 1)
  long double work() const;
};

} // namespace XfgSwap
