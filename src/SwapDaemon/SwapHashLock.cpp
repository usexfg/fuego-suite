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

#include "SwapHashLock.h"
#include "BitcoinCash/HtlcScript.h"

#include <cstdint>
#include <vector>

extern "C" {
#include "crypto/keccak.h"
}

namespace XfgSwap {

static std::string toHexLower(const uint8_t* p, size_t n) {
  static const char* h = "0123456789abcdef";
  std::string s;
  s.reserve(n * 2);
  for (size_t i = 0; i < n; ++i) {
    s += h[p[i] >> 4];
    s += h[p[i] & 0xf];
  }
  return s;
}

std::string solHashLockHex(const Crypto::SecretKey& adaptorSecret) {
  uint8_t md[32];
  // Keccak-256 (Ethereum/Solana variant) of the raw 32-byte secret.
  keccak(reinterpret_cast<const uint8_t*>(&adaptorSecret), 32, md, 32);
  return toHexLower(md, 32);
}

std::string bchHashLockHex(const Crypto::SecretKey& adaptorSecret) {
  std::vector<uint8_t> in(reinterpret_cast<const uint8_t*>(&adaptorSecret),
                          reinterpret_cast<const uint8_t*>(&adaptorSecret) + 32);
  return BchHtlcScript::bytesToHex(BchHtlcScript::sha256(in));
}

} // namespace XfgSwap
