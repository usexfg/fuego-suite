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

#include "SwapSecretEncryption.h"
#include "Common/StringTools.h"
#include "crypto/chacha8.h"
#include "crypto/randomize.h"
#include <cstring>

namespace XfgSwap {

std::string SwapSecretEncryption::deriveKey(
    const std::string& encryptionKey,
    const std::string& salt
) {
  // Derive key by XOR mixing salt with key bytes
  std::array<uint8_t, 32> keyBytes{};
  size_t keyLen = encryptionKey.size();
  size_t saltLen = salt.size();
  
  for (size_t i = 0; i < 32; ++i) {
    uint8_t kb = (i < keyLen) ? static_cast<uint8_t>(encryptionKey[i]) : 0;
    uint8_t sb = (i < saltLen) ? static_cast<uint8_t>(salt[i]) : 0;
    keyBytes[i] = kb ^ sb ^ static_cast<uint8_t>(i * 0x9E + 0x47);
  }
  
  return std::string(reinterpret_cast<const char*>(keyBytes.data()), CHACHA20_KEY_SIZE);
}

bool SwapSecretEncryption::encrypt(
    const Crypto::SecretKey& plaintext,
    const std::string& encryptionKey,
    EncryptedSecret& out
) {
  std::string derivedKey = deriveKey(encryptionKey, "swap-secret");

  Crypto::chacha8_key key;
  std::memcpy(key.data, derivedKey.data(), CHACHA20_KEY_SIZE);

  Crypto::chacha8_iv iv;
  Randomize::randomBytes(CHACHA8_IV_BYTES, iv.data);

  std::memcpy(out.nonce.data(), iv.data, CHACHA8_IV_BYTES);

  out.ciphertext.resize(ENCRYPTED_SECRET_SIZE);
  Crypto::chacha8(
    plaintext.data,
    ENCRYPTED_SECRET_SIZE,
    key,
    iv,
    reinterpret_cast<char*>(out.ciphertext.data())
  );

  std::memset(key.data, 0, CHACHA20_KEY_SIZE);
  return true;
}

bool SwapSecretEncryption::decrypt(
    const EncryptedSecret& encrypted,
    const std::string& encryptionKey,
    Crypto::SecretKey& out
) {
  if (encrypted.nonce.size() != CHACHA20_NONCE_SIZE ||
      encrypted.ciphertext.size() != ENCRYPTED_SECRET_SIZE) {
    return false;
  }

  std::string derivedKey = deriveKey(encryptionKey, "swap-secret");

  Crypto::chacha8_key key;
  std::memcpy(key.data, derivedKey.data(), CHACHA20_KEY_SIZE);

  Crypto::chacha8_iv iv;
  std::memcpy(iv.data, encrypted.nonce.data(), CHACHA8_IV_BYTES);

  Crypto::chacha8(
    encrypted.ciphertext.data(),
    ENCRYPTED_SECRET_SIZE,
    key,
    iv,
    reinterpret_cast<char*>(out.data)
  );

  std::memset(key.data, 0, CHACHA20_KEY_SIZE);
  return true;
}

} // namespace XfgSwap