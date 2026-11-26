// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.

#include "WalletRecordCrypto.h"

#include <cstring>
#include <limits>
#include <stdexcept>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>

namespace CryptoNote {

namespace {

const size_t TAG_SIZE = 32;

struct MacKey {
  uint8_t bytes[32];
  ~MacKey() { OPENSSL_cleanse(bytes, sizeof(bytes)); }
};

void deriveMacKey(const Crypto::chacha8_key& key, MacKey& macKey) {
  static const char label[] = "fuego-wallet-hmac";
  unsigned int length = sizeof(macKey.bytes);
  if (!HMAC(EVP_sha256(), key.data, sizeof(key.data), reinterpret_cast<const unsigned char*>(label),
            sizeof(label), macKey.bytes, &length) || length != sizeof(macKey.bytes)) {
    throw std::runtime_error("MAC key derivation failed");
  }
}

void computeTag(const Crypto::chacha8_key& key, const Crypto::chacha8_iv& iv, const uint8_t* data, size_t size,
                uint8_t tag[TAG_SIZE]) {
  MacKey macKey;
  deriveMacKey(key, macKey);
  unsigned int length = TAG_SIZE;
  HMAC_CTX* context = HMAC_CTX_new();
  const bool computed = context != nullptr &&
      HMAC_Init_ex(context, macKey.bytes, sizeof(macKey.bytes), EVP_sha256(), nullptr) &&
      HMAC_Update(context, reinterpret_cast<const unsigned char*>(&iv), sizeof(iv)) &&
      HMAC_Update(context, data, size) &&
      HMAC_Final(context, tag, &length) && length == TAG_SIZE;
  HMAC_CTX_free(context);
  if (!computed) {
    throw std::runtime_error("Key record tag computation failed");
  }
}

template <typename Record>
void seal(Record& record, const uint8_t* plaintext, const Crypto::chacha8_key& key, const Crypto::chacha8_iv& iv) {
  record.iv = iv;
  Crypto::chacha8(plaintext, sizeof(record.data), key, iv, reinterpret_cast<char*>(record.data));
  computeTag(key, iv, record.data, sizeof(record.data), record.tag);
}

template <typename Record>
void authenticate(const Record& record, const Crypto::chacha8_key& key) {
  uint8_t expected[TAG_SIZE];
  computeTag(key, record.iv, record.data, sizeof(record.data), expected);
  if (CRYPTO_memcmp(expected, record.tag, TAG_SIZE) != 0) {
    throw std::runtime_error("Key record integrity check failed — data may be tampered");
  }
}

template <typename Record>
void open(const Record& record, const Crypto::chacha8_key& key, uint8_t* plaintext) {
  Crypto::chacha8(record.data, sizeof(record.data), key, record.iv, reinterpret_cast<char*>(plaintext));
}

size_t writeKeyPair(const KeyPairRecord& keys, uint8_t* out) {
  std::memcpy(out, &keys.publicKey, sizeof(keys.publicKey));
  std::memcpy(out + sizeof(keys.publicKey), &keys.secretKey, sizeof(keys.secretKey));
  std::memcpy(out + sizeof(keys.publicKey) + sizeof(keys.secretKey), &keys.creationTimestamp,
              sizeof(keys.creationTimestamp));
  return sizeof(keys.publicKey) + sizeof(keys.secretKey) + sizeof(keys.creationTimestamp);
}

size_t readKeyPair(const uint8_t* in, KeyPairRecord& keys) {
  std::memcpy(&keys.publicKey, in, sizeof(keys.publicKey));
  std::memcpy(&keys.secretKey, in + sizeof(keys.publicKey), sizeof(keys.secretKey));
  std::memcpy(&keys.creationTimestamp, in + sizeof(keys.publicKey) + sizeof(keys.secretKey),
              sizeof(keys.creationTimestamp));
  return sizeof(keys.publicKey) + sizeof(keys.secretKey) + sizeof(keys.creationTimestamp);
}

void writeU32LE(uint32_t value, uint8_t* out) {
  for (size_t i = 0; i < sizeof(value); ++i) out[i] = static_cast<uint8_t>(value >> (8 * i));
}

uint32_t readU32LE(const uint8_t* in) {
  uint32_t value = 0;
  for (size_t i = 0; i < sizeof(value); ++i) value |= static_cast<uint32_t>(in[i]) << (8 * i);
  return value;
}

StoredAddressKind checkedKind(uint8_t stored) {
  const auto kind = static_cast<StoredAddressKind>(stored);
  if (kind != StoredAddressKind::Standard && kind != StoredAddressKind::SubaddressV2 &&
      kind != StoredAddressKind::Unrecorded) {
    throw std::runtime_error("Key record has an unknown address kind");
  }
  return kind;
}

} // namespace

EncryptedWalletRecord sealKeyPair(const KeyPairRecord& keys, const Crypto::chacha8_key& key,
                                  const Crypto::chacha8_iv& iv) {
  EncryptedWalletRecord record;
  uint8_t plaintext[sizeof(record.data)];
  writeKeyPair(keys, plaintext);
  seal(record, plaintext, key, iv);
  OPENSSL_cleanse(plaintext, sizeof(plaintext));
  return record;
}

KeyPairRecord openKeyPair(const EncryptedWalletRecord& record, const Crypto::chacha8_key& key, uint8_t fileVersion) {
  if (fileVersion >= FIRST_AUTHENTICATED_RECORD_VERSION) {
    authenticate(record, key);
  }
  uint8_t plaintext[sizeof(record.data)];
  open(record, key, plaintext);
  KeyPairRecord keys;
  readKeyPair(plaintext, keys);
  OPENSSL_cleanse(plaintext, sizeof(plaintext));
  return keys;
}

EncryptedAddressRecord sealAddress(const StoredAddress& address, const Crypto::chacha8_key& key,
                                   const Crypto::chacha8_iv& iv) {
  EncryptedAddressRecord record;
  uint8_t plaintext[sizeof(record.data)];
  size_t offset = writeKeyPair(address.keys, plaintext);
  plaintext[offset++] = static_cast<uint8_t>(address.kind);
  writeU32LE(address.subaddrMajor, plaintext + offset);
  writeU32LE(address.subaddrMinor, plaintext + offset + sizeof(uint32_t));
  seal(record, plaintext, key, iv);
  OPENSSL_cleanse(plaintext, sizeof(plaintext));
  return record;
}

StoredAddress openAddress(const EncryptedAddressRecord& record, const Crypto::chacha8_key& key) {
  authenticate(record, key);
  uint8_t plaintext[sizeof(record.data)];
  open(record, key, plaintext);
  StoredAddress address;
  size_t offset = readKeyPair(plaintext, address.keys);
  const uint8_t kind = plaintext[offset++];
  address.subaddrMajor = readU32LE(plaintext + offset);
  address.subaddrMinor = readU32LE(plaintext + offset + sizeof(uint32_t));
  OPENSSL_cleanse(plaintext, sizeof(plaintext));
  address.kind = checkedKind(kind);
  return address;
}

void advanceIv(Crypto::chacha8_iv& iv) {
  static_assert(sizeof(uint64_t) == sizeof(Crypto::chacha8_iv), "Bad Crypto::chacha8_iv size");
  uint64_t counter;
  std::memcpy(&counter, &iv, sizeof(counter));
  if (counter == std::numeric_limits<uint64_t>::max()) {
    throw std::runtime_error("ChaCha8 IV overflow — keystream reuse risk. Reset wallet container.");
  }
  ++counter;
  std::memcpy(&iv, &counter, sizeof(counter));
}

} // namespace CryptoNote
