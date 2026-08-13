// Copyright (c) 2017-2022 Fuego Developers
// Copyright (c) 2018-2019 Conceal Network & Conceal Devs
// Copyright (c) 2016-2019 The Karbowanec developers
// Copyright (c) 2012-2018 The CryptoNote developers
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

#include "WalletLegacySerializer.h"

#include <stdexcept>

#include "Common/MemoryInputStream.h"
#include "Common/StdInputStream.h"
#include "Common/StdOutputStream.h"
#include "Serialization/BinaryOutputStreamSerializer.h"
#include "Serialization/BinaryInputStreamSerializer.h"
#include "CryptoNoteCore/Account.h"
#include "CryptoNoteCore/CryptoNoteSerialization.h"
#include "WalletLegacy/WalletUserTransactionsCache.h"
#include "Wallet/WalletErrors.h"
#include "WalletLegacy/KeysStorage.h"
#include "crypto/chacha8.h"

using namespace Common;

namespace {

const uint32_t WALLET_SERIALIZATION_VERSION = 2;

bool verifyKeys(const Crypto::SecretKey& sec, const Crypto::PublicKey& expected_pub) {
  Crypto::PublicKey pub;
  bool r = Crypto::secret_key_to_public_key(sec, pub);
  return r && expected_pub == pub;
}

void throwIfKeysMissmatch(const Crypto::SecretKey& sec, const Crypto::PublicKey& expected_pub) {
  if (!verifyKeys(sec, expected_pub))
    throw std::system_error(make_error_code(CryptoNote::error::WRONG_PASSWORD));
}

}

namespace CryptoNote {

WalletLegacySerializer::WalletLegacySerializer(CryptoNote::AccountBase& account, WalletUserTransactionsCache& transactionsCache,
                                               std::map<std::string, AfkLockSecret>& afkLocks) :
  account(account),
  transactionsCache(transactionsCache),
  afkLocks(&afkLocks),
  walletSerializationVersion(WALLET_SERIALIZATION_VERSION)
{
}

WalletLegacySerializer::WalletLegacySerializer(CryptoNote::AccountBase& account, WalletUserTransactionsCache& transactionsCache) :
  account(account),
  transactionsCache(transactionsCache),
  afkLocks(nullptr),
  walletSerializationVersion(WALLET_SERIALIZATION_VERSION)
{
}

void WalletLegacySerializer::serialize(std::ostream& stream, const std::string& password, bool saveDetailed, const std::string& cache) {
  std::stringstream plainArchive;
  StdOutputStream plainStream(plainArchive);
  CryptoNote::BinaryOutputStreamSerializer serializer(plainStream);
  saveKeys(serializer);

  serializer(saveDetailed, "has_details");

  if (saveDetailed) {
    serializer(transactionsCache, "details");
  }

  serializer.binary(const_cast<std::string&>(cache), "cache");

  // Optional trailing AFK-lock section (inside the encrypted payload).
  std::string afkBlob;
  saveAfkLocks(afkBlob);
  if (!afkBlob.empty()) {
    serializer.binary(afkBlob, "afk");
  }

  std::string plain = plainArchive.str();
  std::string cipher;

  Crypto::chacha8_iv iv = encrypt(plain, password, cipher);

  uint32_t version = walletSerializationVersion;
  StdOutputStream output(stream);
  CryptoNote::BinaryOutputStreamSerializer s(output);
  s.beginObject("wallet");
  s(version, "version");
  s(iv, "iv");
  s(cipher, "data");
  s.endObject();

  stream.flush();
}

void WalletLegacySerializer::saveKeys(CryptoNote::ISerializer& serializer) {
  CryptoNote::KeysStorage keys;
  CryptoNote::AccountKeys acc = account.getAccountKeys();

  keys.creationTimestamp = account.get_createtime();
  keys.spendPublicKey = acc.address.spendPublicKey;
  keys.spendSecretKey = acc.spendSecretKey;
  keys.viewPublicKey = acc.address.viewPublicKey;
  keys.viewSecretKey = acc.viewSecretKey;

  keys.serialize(serializer, "keys");
}

Crypto::chacha8_iv WalletLegacySerializer::encrypt(const std::string& plain, const std::string& password, std::string& cipher) {
  Crypto::chacha8_key key;
  Crypto::cn_context context;
  Crypto::generate_chacha8_key(context, password, key);

  cipher.resize(plain.size());

  Crypto::chacha8_iv iv = Crypto::rand<Crypto::chacha8_iv>();
  Crypto::chacha8(plain.data(), plain.size(), key, iv, &cipher[0]);

  return iv;
}


void WalletLegacySerializer::deserialize(std::istream& stream, const std::string& password, std::string& cache) {
  StdInputStream stdStream(stream);
  CryptoNote::BinaryInputStreamSerializer serializerEncrypted(stdStream);

  serializerEncrypted.beginObject("wallet");

  uint32_t version;
  serializerEncrypted(version, "version");

  Crypto::chacha8_iv iv;
  serializerEncrypted(iv, "iv");

  std::string cipher;
  serializerEncrypted(cipher, "data");

  serializerEncrypted.endObject();

  std::string plain;
  decrypt(cipher, plain, iv, password);

  MemoryInputStream decryptedStream(plain.data(), plain.size()); 
  CryptoNote::BinaryInputStreamSerializer serializer(decryptedStream);

  loadKeys(serializer);
  throwIfKeysMissmatch(account.getAccountKeys().viewSecretKey, account.getAccountKeys().address.viewPublicKey);

  if (account.getAccountKeys().spendSecretKey != NULL_SECRET_KEY) {
    throwIfKeysMissmatch(account.getAccountKeys().spendSecretKey, account.getAccountKeys().address.spendPublicKey);
  } else {
    if (!Crypto::check_key(account.getAccountKeys().address.spendPublicKey)) {
      throw std::system_error(make_error_code(CryptoNote::error::WRONG_PASSWORD));
    }
  }

  bool detailsSaved;

  serializer(detailsSaved, "has_details");

  if (detailsSaved) {
    if (version == 1) {
      transactionsCache.deserializeLegacyV1(serializer);
    } else {
      serializer(transactionsCache, "details");
    }
  }

  serializer.binary(cache, "cache");

  // Optional trailing AFK-lock section: absent in wallets saved by older
  // binaries (read throws at end-of-stream → caught → empty blob).
  std::string afkBlob;
  try {
    serializer.binary(afkBlob, "afk");
  } catch (const std::exception&) {
    afkBlob.clear();
  }
  loadAfkLocks(afkBlob);
}

void WalletLegacySerializer::saveAfkLocks(std::string& blob) {
  blob.clear();
  if (!afkLocks) return;
  // Prune expired locks (maker can refund the timelock output; the secret is
  // only needed while the lock window is live).
  const time_t now = std::time(nullptr);
  for (auto it = afkLocks->begin(); it != afkLocks->end(); ) {
    const time_t expiry = it->second.timestamp + static_cast<time_t>(it->second.timeout_hours) * 3600;
    if (expiry < now) {
      it = afkLocks->erase(it);
    } else {
      ++it;
    }
  }
  if (afkLocks->empty()) return;

  // Format: u32 count, then per entry:
  //   u32 keyLen | key bytes | 32B secret | 64B preSig | u64 amount
  //   | u32 timeout_hours | u8 pair | i64 timestamp
  auto appendU32 = [](std::string& s, uint32_t v) {
    for (int i = 0; i < 4; ++i) s.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
  };
  auto appendU64 = [](std::string& s, uint64_t v) {
    for (int i = 0; i < 8; ++i) s.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
  };

  appendU32(blob, static_cast<uint32_t>(afkLocks->size()));
  for (const auto& kv : *afkLocks) {
    const auto& rec = kv.second;
    appendU32(blob, static_cast<uint32_t>(kv.first.size()));
    blob.append(kv.first);
    blob.append(reinterpret_cast<const char*>(&rec.secret), sizeof(Crypto::SecretKey));
    blob.append(reinterpret_cast<const char*>(&rec.preSig), sizeof(Crypto::Signature));
    appendU64(blob, rec.amount);
    appendU32(blob, rec.timeout_hours);
    blob.push_back(static_cast<char>(rec.pair));
    uint64_t ts = static_cast<uint64_t>(rec.timestamp);
    appendU64(blob, ts);
  }
}

void WalletLegacySerializer::loadAfkLocks(const std::string& blob) {
  if (!afkLocks) return;
  afkLocks->clear();
  if (blob.size() < 4) return;

  auto readU32 = [&](size_t& off) -> uint32_t {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(static_cast<uint8_t>(blob[off + i])) << (8 * i);
    off += 4;
    return v;
  };
  auto readU64 = [&](size_t& off) -> uint64_t {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(static_cast<uint8_t>(blob[off + i])) << (8 * i);
    off += 8;
    return v;
  };

  try {
    size_t off = 0;
    uint32_t count = readU32(off);
    for (uint32_t i = 0; i < count; ++i) {
      if (off + 4 > blob.size()) return;
      uint32_t keyLen = readU32(off);
      if (off + keyLen + sizeof(Crypto::SecretKey) + sizeof(Crypto::Signature) + 21 > blob.size()) return;
      std::string key = blob.substr(off, keyLen);
      off += keyLen;
      AfkLockSecret rec;
      std::memcpy(&rec.secret, blob.data() + off, sizeof(Crypto::SecretKey));
      off += sizeof(Crypto::SecretKey);
      std::memcpy(&rec.preSig, blob.data() + off, sizeof(Crypto::Signature));
      off += sizeof(Crypto::Signature);
      rec.amount = readU64(off);
      rec.timeout_hours = readU32(off);
      rec.pair = static_cast<uint8_t>(blob[off++]);
      rec.timestamp = static_cast<time_t>(readU64(off));
      (*afkLocks)[key] = rec;
    }
  } catch (const std::exception&) {
    afkLocks->clear();
  }
}

bool WalletLegacySerializer::deserialize(std::istream& stream, const std::string& password) {
  try {
    StdInputStream stdStream(stream);
    CryptoNote::BinaryInputStreamSerializer serializerEncrypted(stdStream);

    serializerEncrypted.beginObject("wallet");

    uint32_t version;
    serializerEncrypted(version, "version");

    Crypto::chacha8_iv iv;
    serializerEncrypted(iv, "iv");

    std::string cipher;
    serializerEncrypted(cipher, "data");

    serializerEncrypted.endObject();

    std::string plain;
    decrypt(cipher, plain, iv, password);

    MemoryInputStream decryptedStream(plain.data(), plain.size());
    CryptoNote::BinaryInputStreamSerializer serializer(decryptedStream);

    CryptoNote::KeysStorage keys;
    try {
      keys.serialize(serializer, "keys");
    }
    catch (const std::runtime_error&) {
      return false;
    }
    CryptoNote::AccountKeys acc;
    acc.address.spendPublicKey = keys.spendPublicKey;
    acc.spendSecretKey = keys.spendSecretKey;
    acc.address.viewPublicKey = keys.viewPublicKey;
    acc.viewSecretKey = keys.viewSecretKey;

    Crypto::PublicKey pub;
    bool r = Crypto::secret_key_to_public_key(acc.viewSecretKey, pub);
    if (!r || acc.address.viewPublicKey != pub) {
      return false;
    }

    if (acc.spendSecretKey != NULL_SECRET_KEY) {
      Crypto::PublicKey pub;
      bool r = Crypto::secret_key_to_public_key(acc.spendSecretKey, pub);
      if (!r || acc.address.spendPublicKey != pub) {
        return false;
      }
    }
    else {
      if (!Crypto::check_key(acc.address.spendPublicKey)) {
        return false;
      }
    }
  }
  catch (std::system_error&) {
    return false;
  }
  catch (std::exception&) {
    return false;
  }

  return true;
}




void WalletLegacySerializer::decrypt(const std::string& cipher, std::string& plain, Crypto::chacha8_iv iv, const std::string& password) {
  Crypto::chacha8_key key;
  Crypto::cn_context context;
  Crypto::generate_chacha8_key(context, password, key);

  plain.resize(cipher.size());

  Crypto::chacha8(cipher.data(), cipher.size(), key, iv, &plain[0]);
}

void WalletLegacySerializer::loadKeys(CryptoNote::ISerializer& serializer) {
  CryptoNote::KeysStorage keys;

  try {
    keys.serialize(serializer, "keys");
  } catch (const std::runtime_error&) {
    throw std::system_error(make_error_code(CryptoNote::error::WRONG_PASSWORD));
  }

  CryptoNote::AccountKeys acc;
  acc.address.spendPublicKey = keys.spendPublicKey;
  acc.spendSecretKey = keys.spendSecretKey;
  acc.address.viewPublicKey = keys.viewPublicKey;
  acc.viewSecretKey = keys.viewSecretKey;

  account.setAccountKeys(acc);
  account.set_createtime(keys.creationTimestamp);
}

}
