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

#include "CryptoTypes.h"
#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace XfgSwap {

constexpr size_t ENCRYPTED_SECRET_SIZE = 32;
constexpr size_t CHACHA8_IV_BYTES = 8;
constexpr size_t CHACHA20_KEY_SIZE = 32;
constexpr size_t CHACHA20_NONCE_SIZE = 12;

class SwapSecretEncryption {
public:
  struct EncryptedSecret {
    std::array<uint8_t, CHACHA20_NONCE_SIZE> nonce;
    std::vector<uint8_t> ciphertext;
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

  static std::string deriveKey(const std::string& encryptionKey, const std::string& salt);
};

} // namespace XfgSwap