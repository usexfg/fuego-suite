// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.

#pragma once

#include <cstdint>

#include "crypto/chacha8.h"
#include "crypto/crypto.h"

namespace CryptoNote {

#pragma pack(push, 1)
// View keys, and address records of files before version 10.
struct EncryptedWalletRecord {
  Crypto::chacha8_iv iv;
  uint8_t data[sizeof(Crypto::PublicKey) + sizeof(Crypto::SecretKey) + sizeof(uint64_t)];
  uint8_t tag[32];
};

// Address records from version 10: the key pair plus the address kind and index.
struct EncryptedAddressRecord {
  Crypto::chacha8_iv iv;
  uint8_t data[sizeof(Crypto::PublicKey) + sizeof(Crypto::SecretKey) + sizeof(uint64_t) +
               sizeof(uint8_t) + 2 * sizeof(uint32_t)];
  uint8_t tag[32];
};

// Start of every wallet file: the file version, the IV the next sealed record
// takes, and the view keys. The address records follow, then the cache.
struct ContainerStoragePrefix {
  uint8_t version;
  Crypto::chacha8_iv nextIv;
  EncryptedWalletRecord encryptedViewKeys;
};
#pragma pack(pop)

const uint8_t FIRST_AUTHENTICATED_RECORD_VERSION = 8;
const uint8_t FIRST_ADDRESS_KIND_RECORD_VERSION = 10;

enum class StoredAddressKind : uint8_t {
  Standard = 0,
  SubaddressV2 = 2,
  Unrecorded = 0xFF,
};

struct KeyPairRecord {
  Crypto::PublicKey publicKey;
  Crypto::SecretKey secretKey;
  uint64_t creationTimestamp = 0;
};

struct StoredAddress {
  KeyPairRecord keys;
  StoredAddressKind kind = StoredAddressKind::Standard;
  uint32_t subaddrMajor = 0;
  uint32_t subaddrMinor = 0;
};

EncryptedWalletRecord sealKeyPair(const KeyPairRecord& keys, const Crypto::chacha8_key& key,
                                  const Crypto::chacha8_iv& iv);
// Files before version 8 carry no tag; every later record must authenticate.
KeyPairRecord openKeyPair(const EncryptedWalletRecord& record, const Crypto::chacha8_key& key,
                          uint8_t fileVersion);

EncryptedAddressRecord sealAddress(const StoredAddress& address, const Crypto::chacha8_key& key,
                                   const Crypto::chacha8_iv& iv);
StoredAddress openAddress(const EncryptedAddressRecord& record, const Crypto::chacha8_key& key);

// Each sealed record takes the current IV; the next one must use a new IV.
void advanceIv(Crypto::chacha8_iv& iv);

} // namespace CryptoNote
