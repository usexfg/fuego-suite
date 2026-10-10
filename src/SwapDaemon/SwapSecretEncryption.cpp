// Copyright (c) 2017-2026 Fuego Developers
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.
//
// At-rest encryption for the swap daemon's secrets: the adaptor preimage, the
// swap sec key, the ring and MuSig2 signing nonces, and the Monero spend/view
// keys. The encryption key is derived from the maker wallet key and never
// persisted (see SwapDaemon::start), so a stolen database should still cost
// real work to attack offline.
//
// v2 (current) is ChaCha20-Poly1305 through OpenSSL EVP. v1 was ChaCha8 with a
// hand-rolled HMAC-SHA256 Encrypt-then-MAC; its construction was correct but
// the cipher is an 8-round variant, roughly a quarter of ChaCha20's margin.
// v1 is retained for reading only, because a swap interrupted across an upgrade
// must still recover its secret.

#include "SwapSecretEncryption.h"
#include "Common/StringTools.h"
#include "crypto/chacha8.h"
#include "crypto/randomize.h"
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <cstring>
#include <algorithm>

namespace XfgSwap {

void SwapSecretEncryption::secureZero(void* buf, size_t len) {
  volatile uint8_t* p = static_cast<volatile uint8_t*>(buf);
  while (len--) *p++ = 0;
}

// ─── v1 (legacy, read-only compatibility) ───────────────────────────────────

static bool encryptV1(
    const uint8_t* plaintext,
    size_t plaintextLen,
    const std::string& encryptionKey,
    SwapSecretEncryption::EncryptedSecret& out) {
  out.version = SECRET_ENVELOPE_V1;

  Randomize::randomBytes(SALT_SIZE, out.salt.data());

  std::string saltedKey = encryptionKey;
  saltedKey.append(reinterpret_cast<const char*>(out.salt.data()), SALT_SIZE);

  Crypto::cn_context ctx;
  Crypto::chacha8_key cipherKey;
  Crypto::generate_chacha8_key(ctx, saltedKey, cipherKey);

  // Zero the salted key material — std::string destructor does not zero memory
  std::fill(saltedKey.begin(), saltedKey.end(), '\0');

  Crypto::chacha8_iv iv;
  Randomize::randomBytes(CHACHA8_NONCE_SIZE, iv.data);
  std::memcpy(out.nonce.data(), iv.data, CHACHA8_NONCE_SIZE);

  out.ciphertext.resize(plaintextLen);
  Crypto::chacha8(
    plaintext, plaintextLen,
    cipherKey, iv,
    reinterpret_cast<char*>(out.ciphertext.data())
  );

  // MAC tag = HMAC-SHA256(cipherKey, nonce || salt || ciphertext)
  std::string tagInput(reinterpret_cast<const char*>(out.nonce.data()), CHACHA8_NONCE_SIZE);
  tagInput.append(reinterpret_cast<const char*>(out.salt.data()), SALT_SIZE);
  tagInput.append(reinterpret_cast<const char*>(out.ciphertext.data()), out.ciphertext.size());

  unsigned int tagLen = TAG_SIZE;
  HMAC(EVP_sha256(),
       cipherKey.data, CHACHA8_KEY_SIZE,
       reinterpret_cast<const unsigned char*>(tagInput.data()), tagInput.size(),
       out.tag.data(), &tagLen);

  if (tagLen != TAG_SIZE) {
    SwapSecretEncryption::secureZero(cipherKey.data, sizeof(cipherKey));
    return false;
  }

  SwapSecretEncryption::secureZero(cipherKey.data, sizeof(cipherKey));
  return true;
}

static bool decryptV1(
    const SwapSecretEncryption::EncryptedSecret& encrypted,
    const std::string& encryptionKey,
    uint8_t* out,
    size_t outLen) {
  if (encrypted.ciphertext.size() != outLen) return false;

  std::string saltedKey = encryptionKey;
  saltedKey.append(reinterpret_cast<const char*>(encrypted.salt.data()), SALT_SIZE);

  Crypto::cn_context ctx;
  Crypto::chacha8_key cipherKey;
  Crypto::generate_chacha8_key(ctx, saltedKey, cipherKey);
  std::fill(saltedKey.begin(), saltedKey.end(), '\0');

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
    SwapSecretEncryption::secureZero(cipherKey.data, sizeof(cipherKey));
    return false;
  }

  uint8_t diff = 0;
  for (size_t i = 0; i < TAG_SIZE; ++i) {
    diff |= computedTag[i] ^ encrypted.tag[i];
  }

  if (diff != 0) {
    SwapSecretEncryption::secureZero(cipherKey.data, sizeof(cipherKey));
    return false;
  }

  Crypto::chacha8_iv iv;
  std::memcpy(iv.data, encrypted.nonce.data(), CHACHA8_NONCE_SIZE);

  Crypto::chacha8(
    encrypted.ciphertext.data(), outLen,
    cipherKey, iv,
    reinterpret_cast<char*>(out)
  );

  SwapSecretEncryption::secureZero(cipherKey.data, sizeof(cipherKey));
  return true;
}

// ─── v2 (ChaCha20-Poly1305) ────────────────────────────────────────────────

namespace {

// Derive the cipher key from the maker secret + salt. Shared by both versions so
// the on-disk KDF does not change across the upgrade — a v1 record must still
// decrypt with the same derivation a v2 record uses.
void deriveKey(const std::string& encryptionKey,
               const uint8_t* salt,
               Crypto::chacha8_key& cipherKey) {
  std::string saltedKey = encryptionKey;
  saltedKey.append(reinterpret_cast<const char*>(salt), SALT_SIZE);

  Crypto::cn_context ctx;
  Crypto::generate_chacha8_key(ctx, saltedKey, cipherKey);

  // Zero the salted key material — std::string destructor does not zero memory
  std::fill(saltedKey.begin(), saltedKey.end(), '\0');
}

} // namespace

bool SwapSecretEncryption::encrypt(
    const Crypto::SecretKey& plaintext,
    const std::string& encryptionKey,
    EncryptedSecret& out) {
  return encrypt(plaintext.data, SECRET_PLAINTEXT, encryptionKey, out);
}

bool SwapSecretEncryption::encrypt(
    const uint8_t* plaintext,
    size_t plaintextLen,
    const std::string& encryptionKey,
    EncryptedSecret& out) {
  if (!plaintext || plaintextLen == 0 || encryptionKey.empty()) {
    return false;
  }

  out = EncryptedSecret{};
  out.version = SECRET_ENVELOPE_V2;

  Randomize::randomBytes(SALT_SIZE, out.salt.data());

  Crypto::chacha8_key cipherKey;
  deriveKey(encryptionKey, out.salt.data(), cipherKey);

  // 96-bit nonce, fresh per record. Random is sound here: each record is
  // encrypted exactly once under this key, so there is no counter to collide
  // with even under concurrent saves.
  if (RAND_bytes(out.nonce.data(), CHACHA20_NONCE_SIZE) != 1) {
    secureZero(cipherKey.data, sizeof(cipherKey));
    return false;
  }

  // Bind the version and salt as AAD so neither can be swapped between records:
  // without this, an attacker could rewrite a v2 header as v1 and steer the
  // decoder into the weaker path.
  uint8_t aad[1 + SALT_SIZE];
  aad[0] = SECRET_ENVELOPE_V2;
  std::memcpy(aad + 1, out.salt.data(), SALT_SIZE);

  // EVP output buffer holds ciphertext only; the tag is retrieved explicitly.
  out.ciphertext.resize(plaintextLen);

  EVP_CIPHER_CTX* cctx = EVP_CIPHER_CTX_new();
  if (!cctx) {
    secureZero(cipherKey.data, sizeof(cipherKey));
    return false;
  }

  int len = 0;
  int finalLen = 0;
  unsigned char tag[POLY1305_TAG_SIZE];
  std::memset(tag, 0, sizeof(tag));

  bool ok =
      EVP_EncryptInit_ex(cctx, EVP_chacha20_poly1305(), nullptr, nullptr, nullptr) == 1 &&
      EVP_CIPHER_CTX_ctrl(cctx, EVP_CTRL_AEAD_SET_IVLEN, CHACHA20_NONCE_SIZE, nullptr) == 1 &&
      EVP_EncryptInit_ex(cctx, nullptr, nullptr, cipherKey.data, out.nonce.data()) == 1 &&
      EVP_EncryptUpdate(cctx, nullptr, &len, aad, sizeof(aad)) == 1 &&
      EVP_EncryptUpdate(cctx, out.ciphertext.data(), &len, plaintext, (int)plaintextLen) == 1;

  if (ok) {
    ok = EVP_EncryptFinal_ex(cctx, out.ciphertext.data() + len, &finalLen) == 1;
  }

  // The Poly1305 tag is NOT written past the ciphertext by Final_ex on OpenSSL 3:
  // Final_ex reports finalLen == 0 and the tail of the buffer is left untouched.
  // It must be pulled out with EVP_CTRL_AEAD_GET_TAG. Verified experimentally --
  // assuming it was appended produced records that round-tripped "successfully"
  // while authenticating nothing, because the tag was never present to check.
  if (ok) {
    ok = EVP_CIPHER_CTX_ctrl(cctx, EVP_CTRL_AEAD_GET_TAG, POLY1305_TAG_SIZE, tag) == 1;
  }

  EVP_CIPHER_CTX_free(cctx);
  secureZero(cipherKey.data, sizeof(cipherKey));

  if (!ok || len != (int)plaintextLen) {
    out.ciphertext.clear();
    return false;
  }

  // Stored record is ciphertext || tag.
  out.ciphertext.insert(out.ciphertext.end(), tag, tag + POLY1305_TAG_SIZE);
  if (out.ciphertext.size() != plaintextLen + POLY1305_TAG_SIZE) {
    out.ciphertext.clear();
    return false;
  }
  return true;
}

bool SwapSecretEncryption::decrypt(
    const EncryptedSecret& encrypted,
    const std::string& encryptionKey,
    Crypto::SecretKey& out) {
  return decrypt(encrypted, encryptionKey, out.data, SECRET_PLAINTEXT);
}

bool SwapSecretEncryption::decrypt(
    const EncryptedSecret& encrypted,
    const std::string& encryptionKey,
    uint8_t* out,
    size_t outLen) {
  if (!out || outLen == 0 || encryptionKey.empty()) {
    return false;
  }

  // Dispatch strictly on the stored version byte. An earlier draft sniffed
  // nonce/ciphertext shape to decide, which let a malformed or tampered record
  // steer the decoder into the weaker v1 path. Only an explicit, first-class
  // version selects the legacy cipher.
  if (encrypted.version == SECRET_ENVELOPE_V1) {
    return decryptV1(encrypted, encryptionKey, out, outLen);
  }
  if (encrypted.version != SECRET_ENVELOPE_V2) {
    return false;  // unknown version — fail closed
  }

  if (encrypted.ciphertext.size() != outLen + POLY1305_TAG_SIZE) {
    return false;
  }

  Crypto::chacha8_key cipherKey;
  deriveKey(encryptionKey, encrypted.salt.data(), cipherKey);

  uint8_t aad[1 + SALT_SIZE];
  aad[0] = SECRET_ENVELOPE_V2;
  std::memcpy(aad + 1, encrypted.salt.data(), SALT_SIZE);

  EVP_CIPHER_CTX* dctx = EVP_CIPHER_CTX_new();
  if (!dctx) {
    secureZero(cipherKey.data, sizeof(cipherKey));
    return false;
  }

  int len = 0;
  int finalLen = 0;
  bool ok =
      EVP_DecryptInit_ex(dctx, EVP_chacha20_poly1305(), nullptr, nullptr, nullptr) == 1 &&
      EVP_CIPHER_CTX_ctrl(dctx, EVP_CTRL_AEAD_SET_IVLEN, CHACHA20_NONCE_SIZE, nullptr) == 1 &&
      EVP_DecryptInit_ex(dctx, nullptr, nullptr, cipherKey.data, encrypted.nonce.data()) == 1 &&
      EVP_DecryptUpdate(dctx, nullptr, &len, aad, sizeof(aad)) == 1 &&
      // DecryptUpdate consumes ciphertext only; the tag is supplied separately
      // below via SET_TAG, which is the mirror of GET_TAG on the encrypt side.
      EVP_DecryptUpdate(dctx, out, &len,
                        encrypted.ciphertext.data(), (int)outLen) == 1;

  if (ok) {
    ok = EVP_CIPHER_CTX_ctrl(dctx, EVP_CTRL_AEAD_SET_TAG, POLY1305_TAG_SIZE,
                             const_cast<uint8_t*>(encrypted.ciphertext.data() + outLen)) == 1;
  }

  if (ok) {
    // The tag check is the authority. A wrong key or a single flipped bit fails
    // here, and no plaintext is released.
    ok = EVP_DecryptFinal_ex(dctx, out + len, &finalLen) == 1;
  }

  EVP_CIPHER_CTX_free(dctx);
  secureZero(cipherKey.data, sizeof(cipherKey));

  if (!ok) {
    // Do not hand back unauthenticated plaintext.
    std::fill(out, out + outLen, 0);
    return false;
  }
  return true;
}

// ─── envelope serialisation ─────────────────────────────────────────────────

std::vector<uint8_t> SwapSecretEncryption::encode(const EncryptedSecret& s) {
  std::vector<uint8_t> out;
  out.reserve(1 + s.nonce.size() + s.salt.size() + s.ciphertext.size() +
              (s.version == SECRET_ENVELOPE_V1 ? TAG_SIZE : 0));

  out.push_back(s.version);
  out.insert(out.end(), s.nonce.begin(), s.nonce.end());
  out.insert(out.end(), s.salt.begin(), s.salt.end());
  out.insert(out.end(), s.ciphertext.begin(), s.ciphertext.end());
  if (s.version == SECRET_ENVELOPE_V1) {
    out.insert(out.end(), s.tag.begin(), s.tag.end());
  }
  return out;
}

bool SwapSecretEncryption::decode(const std::vector<uint8_t>& blob,
                                  size_t plaintextLen,
                                  EncryptedSecret& out) {
  if (blob.empty() || plaintextLen == 0) return false;

  const uint8_t version = blob[0];
  size_t nonceSize, tagSize;

  if (version == SECRET_ENVELOPE_V1) {
    nonceSize = CHACHA8_NONCE_SIZE;
    tagSize = TAG_SIZE;
  } else if (version == SECRET_ENVELOPE_V2) {
    nonceSize = CHACHA20_NONCE_SIZE;
    tagSize = 0;
  } else {
    return false;  // unknown version — fail closed, never guess
  }

  const size_t header = 1 + nonceSize + SALT_SIZE;
  // v1 stores ciphertext == plaintextLen and appends a separate tag.
  // v2 stores ciphertext == plaintextLen + Poly1305 tag.
  const size_t expectedCipher = (version == SECRET_ENVELOPE_V1)
      ? plaintextLen : plaintextLen + POLY1305_TAG_SIZE;

  if (blob.size() != header + expectedCipher + tagSize) {
    return false;
  }

  out = EncryptedSecret{};
  out.version = version;
  std::memcpy(out.nonce.data(), blob.data() + 1, nonceSize);
  std::memcpy(out.salt.data(), blob.data() + 1 + nonceSize, SALT_SIZE);
  out.ciphertext.assign(blob.begin() + header, blob.begin() + header + expectedCipher);
  if (tagSize) {
    std::memcpy(out.tag.data(), blob.data() + header + expectedCipher, tagSize);
  }
  return true;
}

} // namespace XfgSwap