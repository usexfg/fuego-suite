// Copyright (c) 2017-2026 Fuego Developers
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

#include "SwapTypes.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace XfgSwap {

constexpr size_t CHACHA8_KEY_SIZE   = 32;
constexpr size_t CHACHA8_NONCE_SIZE = 8;
constexpr size_t SALT_SIZE          = 16;
constexpr size_t TAG_SIZE           = 32;
constexpr size_t SECRET_PLAINTEXT   = 32;

class SwapSecretEncryption {
public:
  struct EncryptedSecret {
    std::array<uint8_t, CHACHA8_NONCE_SIZE> nonce;
    std::array<uint8_t, SALT_SIZE>          salt;
    std::vector<uint8_t>                    ciphertext;
    std::array<uint8_t, TAG_SIZE>           tag;
  };

  static bool encrypt(
    const Crypto::SecretKey& plaintext,
    const std::string& encryptionKey,
    EncryptedSecret& out
  );

  static bool decrypt(
    const EncryptedSecret& encrypted,
    const std::string& encryptionKey,
    Crypto::SecretKey& out
  );

private:
  static void secureZero(void* buf, size_t len);
};

} // namespace XfgSwap
