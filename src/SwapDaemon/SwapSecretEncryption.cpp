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

#include "SwapSecretEncryption.h"
#include "Common/StringTools.h"
#include "crypto/chacha8.h"
#include "crypto/randomize.h"
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <cstring>
#include <algorithm>

namespace XfgSwap {

void SwapSecretEncryption::secureZero(void* buf, size_t len) {
  volatile uint8_t* p = static_cast<volatile uint8_t*>(buf);
  while (len--) *p++ = 0;
}

bool SwapSecretEncryption::encrypt(
    const Crypto::SecretKey& plaintext,
    const std::string& encryptionKey,
    EncryptedSecret& out
) {
  return encrypt(plaintext.data, SECRET_PLAINTEXT, encryptionKey, out);
}

bool SwapSecretEncryption::encrypt(
    const uint8_t* plaintext,
    size_t plaintextLen,
    const std::string& encryptionKey,
    EncryptedSecret& out
) {
  if (!plaintext || plaintextLen == 0 || encryptionKey.empty()) {
    return false;
  }

  // Generate random salt
  Randomize::randomBytes(SALT_SIZE, out.salt.data());

  // Combine key with salt for domain separation
  std::string saltedKey = encryptionKey;
  saltedKey.append(reinterpret_cast<const char*>(out.salt.data()), SALT_SIZE);

  // KDF: memory-hard cipher key via CryptoNight
  Crypto::cn_context ctx;
  Crypto::chacha8_key cipherKey;
  Crypto::generate_chacha8_key(ctx, saltedKey, cipherKey);

  // Zero the salted key material — std::string destructor does not zero memory
  std::fill(saltedKey.begin(), saltedKey.end(), '\0');

  // Generate random nonce
  Crypto::chacha8_iv iv;
  Randomize::randomBytes(CHACHA8_NONCE_SIZE, iv.data);
  std::memcpy(out.nonce.data(), iv.data, CHACHA8_NONCE_SIZE);

  // Encrypt
  out.ciphertext.resize(plaintextLen);
  Crypto::chacha8(
    plaintext, plaintextLen,
    cipherKey, iv,
    reinterpret_cast<char*>(out.ciphertext.data())
  );

  // Compute MAC tag = HMAC-SHA256(cipherKey, nonce || salt || ciphertext)
  std::string tagInput(reinterpret_cast<const char*>(out.nonce.data()), CHACHA8_NONCE_SIZE);
  tagInput.append(reinterpret_cast<const char*>(out.salt.data()), SALT_SIZE);
  tagInput.append(reinterpret_cast<const char*>(out.ciphertext.data()), out.ciphertext.size());

  unsigned int tagLen = TAG_SIZE;
  HMAC(EVP_sha256(),
       cipherKey.data, CHACHA8_KEY_SIZE,
       reinterpret_cast<const unsigned char*>(tagInput.data()), tagInput.size(),
       out.tag.data(), &tagLen);

  if (tagLen != TAG_SIZE) {
    secureZero(cipherKey.data, sizeof(cipherKey));
    return false;
  }

  secureZero(cipherKey.data, sizeof(cipherKey));
  return true;
}

bool SwapSecretEncryption::decrypt(
    const EncryptedSecret& encrypted,
    const std::string& encryptionKey,
    Crypto::SecretKey& out
) {
  return decrypt(encrypted, encryptionKey, out.data, SECRET_PLAINTEXT);
}

bool SwapSecretEncryption::decrypt(
    const EncryptedSecret& encrypted,
    const std::string& encryptionKey,
    uint8_t* out,
    size_t outLen
) {
  if (!out || outLen == 0 || encrypted.ciphertext.size() != outLen) {
    return false;
  }

  // Recompute salted key
  std::string saltedKey = encryptionKey;
  saltedKey.append(reinterpret_cast<const char*>(encrypted.salt.data()), SALT_SIZE);

  // Re-derive cipher key
  Crypto::cn_context ctx;
  Crypto::chacha8_key cipherKey;
  Crypto::generate_chacha8_key(ctx, saltedKey, cipherKey);
  std::fill(saltedKey.begin(), saltedKey.end(), '\0');

  // Recompute MAC tag = HMAC-SHA256(cipherKey, nonce || salt || ciphertext)
  std::string tagInput(reinterpret_cast<const char*>(encrypted.nonce.data()), CHACHA8_NONCE_SIZE);
  tagInput.append(reinterpret_cast<const char*>(encrypted.salt.data()), SALT_SIZE);
  tagInput.append(reinterpret_cast<const char*>(encrypted.ciphertext.data()), encrypted.ciphertext.size());

  uint8_t computedTag[TAG_SIZE];
  unsigned int tagLen = TAG_SIZE;
  HMAC(EVP_sha256(),
       cipherKey.data, CHACHA8_KEY_SIZE,
       reinterpret_cast<const unsigned char*>(tagInput.data()), tagInput.size(),
       computedTag, &tagLen);

  if (tagLen != TAG_SIZE) {
    secureZero(cipherKey.data, sizeof(cipherKey));
    return false;
  }

  uint8_t diff = 0;
  for (size_t i = 0; i < TAG_SIZE; ++i) {
    diff |= computedTag[i] ^ encrypted.tag[i];
  }

  if (diff != 0) {
    secureZero(cipherKey.data, sizeof(cipherKey));
    return false;
  }

  // Decrypt
  Crypto::chacha8_iv iv;
  std::memcpy(iv.data, encrypted.nonce.data(), CHACHA8_NONCE_SIZE);

  Crypto::chacha8(
    encrypted.ciphertext.data(), outLen,
    cipherKey, iv,
    reinterpret_cast<char*>(out)
  );

  secureZero(cipherKey.data, sizeof(cipherKey));
  return true;
}

} // namespace XfgSwap
