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

#include <cstdint>
#include <string>
#include <vector>

namespace XfgSwap {

// Zano / CryptoNote address encoding for the ZANO swap leg.
// Address = block-base58( varint(prefix) || spend(32) || view(32) || keccak(...)[0:4] ),
// which is exactly Tools::Base58::encode_addr(prefix, spend||view). Zano and
// Fuego share this scheme; only the prefix differs. Zano mainnet = 0xc5 (197),
// which produces addresses starting with "Zx".
class ZanoAddress {
public:
  // Encode a standard (non-subaddress) address. Returns "" on bad key sizes.
  static std::string encode(const std::vector<uint8_t>& spendPub,
                            const std::vector<uint8_t>& viewPub,
                            uint64_t prefix);

  // Shared 2-of-2 spend pubkey = A + B (ed25519 point addition).
  // Returns false if A or B are not valid 32-byte curve points.
  static bool sharedSpendPub(const std::vector<uint8_t>& A,
                             const std::vector<uint8_t>& B,
                             std::vector<uint8_t>& out);

  // Zano network prefixes (varint values). Mainnet = 0xc5 (addresses start
  // with 'Zx'); testnet builds use a distinct prefix supplied via config.
  static constexpr uint64_t MAINNET  = 0xc5;   // addresses start with 'Zx'
  static constexpr uint64_t TESTNET  = 0xc5;   // operator-overridden for testnet
  static constexpr uint64_t STAGENET = 0xc5;
};

} // namespace XfgSwap
