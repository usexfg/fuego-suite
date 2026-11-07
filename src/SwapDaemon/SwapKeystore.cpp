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

#include "SwapKeystore.h"

#include "Common/JsonValue.h"
#include "Common/StringTools.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace XfgSwap {

namespace {

constexpr int64_t KEYSTORE_VERSION = 1;
constexpr uint64_t SCRYPT_N = 1u << 15;
constexpr uint64_t SCRYPT_R = 8;
constexpr uint64_t SCRYPT_P = 1;
constexpr uint64_t SCRYPT_MAX_MEMORY = 64ull * 1024 * 1024;
constexpr size_t KEY_SIZE = 32;
constexpr size_t SALT_SIZE = 16;
constexpr size_t NONCE_SIZE = 12;
constexpr size_t TAG_SIZE = 16;
const char* const REFERENCE_PREFIX = "\"keystore:";

struct SecureBytes {
  std::vector<uint8_t> bytes;
  explicit SecureBytes(size_t size) : bytes(size) {}
  ~SecureBytes() { OPENSSL_cleanse(bytes.data(), bytes.size()); }
};

struct CipherContext {
  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  ~CipherContext() { EVP_CIPHER_CTX_free(ctx); }
};

// Associated data: everything in the header the decryptor trusts.
std::string associatedData(const std::string& saltHex, const std::string& nonceHex) {
  return "xfg-swapd-keystore/v" + std::to_string(KEYSTORE_VERSION) + "/scrypt/" +
         std::to_string(SCRYPT_N) + "/" + std::to_string(SCRYPT_R) + "/" +
         std::to_string(SCRYPT_P) + "/" + saltHex + "/" + nonceHex;
}

bool deriveKey(const std::string& passphrase, const std::vector<uint8_t>& salt, SecureBytes& key) {
  return EVP_PBE_scrypt(passphrase.data(), passphrase.size(), salt.data(), salt.size(),
                        SCRYPT_N, SCRYPT_R, SCRYPT_P, SCRYPT_MAX_MEMORY, key.bytes.data(),
                        key.bytes.size()) == 1;
}

bool isPlainSecret(const std::string& secret) {
  return !secret.empty() && std::all_of(secret.begin(), secret.end(), [](unsigned char c) {
    return c >= 0x21 && c <= 0x7e && c != '"' && c != '\\';
  });
}

bool isSecretName(const std::string& name) {
  return !name.empty() && name.size() <= 64 && std::all_of(name.begin(), name.end(), [](unsigned char c) {
    return std::isalnum(c) != 0 || c == '_' || c == '-' || c == '.';
  });
}

std::string serializeSecrets(const KeystoreSecrets& secrets) {
  Common::JsonValue object(Common::JsonValue::OBJECT);
  for (const auto& entry : secrets) object.insert(entry.first, entry.second);
  return object.toString();
}

bool parseSecrets(const std::string& text, KeystoreSecrets& secrets) {
  try {
    const Common::JsonValue object = Common::JsonValue::fromString(text);
    if (!object.isObject()) return false;
    KeystoreSecrets parsed;
    for (const auto& entry : object.getObject()) {
      if (!entry.second.isString() || !isSecretName(entry.first)) return false;
      parsed[entry.first] = entry.second.getString();
    }
    secrets = std::move(parsed);
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

bool readFile(const std::string& path, std::string& contents) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::stringstream buffer;
  buffer << in.rdbuf();
  contents = buffer.str();
  return true;
}

bool writeOwnerOnly(const std::string& path, const std::string& contents) {
  const std::string temporary = path + ".tmp";
#ifndef _WIN32
  const mode_t previous = umask(077);
#endif
  bool written = false;
  {
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    out << contents;
    written = static_cast<bool>(out.flush());
  }
#ifndef _WIN32
  umask(previous);
  written = written && chmod(temporary.c_str(), 0600) == 0;
#endif
  if (!written || std::rename(temporary.c_str(), path.c_str()) != 0) {
    std::remove(temporary.c_str());
    return false;
  }
  return true;
}

}  // namespace

bool saveKeystore(const std::string& path, const std::string& passphrase,
                  const KeystoreSecrets& secrets, std::string& error) {
  if (passphrase.size() < 12) {
    error = "keystore passphrase must be at least 12 characters";
    return false;
  }
  for (const auto& entry : secrets) {
    if (!isSecretName(entry.first) || !isPlainSecret(entry.second)) {
      error = "keystore entry '" + entry.first + "' has an invalid name or value";
      return false;
    }
  }
  std::vector<uint8_t> salt(SALT_SIZE), nonce(NONCE_SIZE), tag(TAG_SIZE);
  if (RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1 ||
      RAND_bytes(nonce.data(), static_cast<int>(nonce.size())) != 1) {
    error = "no randomness for the keystore";
    return false;
  }
  SecureBytes key(KEY_SIZE);
  if (!deriveKey(passphrase, salt, key)) {
    error = "scrypt key derivation failed";
    return false;
  }
  const std::string saltHex = Common::toHex(salt.data(), salt.size());
  const std::string nonceHex = Common::toHex(nonce.data(), nonce.size());
  const std::string aad = associatedData(saltHex, nonceHex);
  std::string plaintext = serializeSecrets(secrets);
  std::vector<uint8_t> ciphertext(plaintext.size());
  CipherContext cipher;
  int length = 0;
  const bool sealed =
      cipher.ctx &&
      EVP_EncryptInit_ex(cipher.ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
      EVP_CIPHER_CTX_ctrl(cipher.ctx, EVP_CTRL_GCM_SET_IVLEN, NONCE_SIZE, nullptr) == 1 &&
      EVP_EncryptInit_ex(cipher.ctx, nullptr, nullptr, key.bytes.data(), nonce.data()) == 1 &&
      EVP_EncryptUpdate(cipher.ctx, nullptr, &length, reinterpret_cast<const uint8_t*>(aad.data()),
                        static_cast<int>(aad.size())) == 1 &&
      EVP_EncryptUpdate(cipher.ctx, ciphertext.data(), &length,
                        reinterpret_cast<const uint8_t*>(plaintext.data()),
                        static_cast<int>(plaintext.size())) == 1 &&
      EVP_EncryptFinal_ex(cipher.ctx, ciphertext.data() + length, &length) == 1 &&
      EVP_CIPHER_CTX_ctrl(cipher.ctx, EVP_CTRL_GCM_GET_TAG, TAG_SIZE, tag.data()) == 1;
  OPENSSL_cleanse(&plaintext[0], plaintext.size());
  if (!sealed) {
    error = "keystore encryption failed";
    return false;
  }
  Common::JsonValue file(Common::JsonValue::OBJECT);
  file.insert("version", KEYSTORE_VERSION);
  file.insert("kdf", std::string("scrypt"));
  file.insert("salt", saltHex);
  file.insert("nonce", nonceHex);
  file.insert("ciphertext", Common::toHex(ciphertext.data(), ciphertext.size()));
  file.insert("tag", Common::toHex(tag.data(), tag.size()));
  if (!writeOwnerOnly(path, file.toString())) {
    error = "cannot write keystore " + path;
    return false;
  }
  return true;
}

bool loadKeystore(const std::string& path, const std::string& passphrase,
                  KeystoreSecrets& secrets, std::string& error) {
  std::string contents;
  if (!readFile(path, contents)) {
    error = "cannot read keystore " + path;
    return false;
  }
  std::string saltHex, nonceHex, ciphertextHex, tagHex;
  try {
    const Common::JsonValue file = Common::JsonValue::fromString(contents);
    if (!file.isObject() || !file.contains("version") || file("version").getInteger() != KEYSTORE_VERSION ||
        file("kdf").getString() != "scrypt") {
      error = "unsupported keystore format";
      return false;
    }
    saltHex = file("salt").getString();
    nonceHex = file("nonce").getString();
    ciphertextHex = file("ciphertext").getString();
    tagHex = file("tag").getString();
  } catch (const std::exception&) {
    error = "malformed keystore " + path;
    return false;
  }
  std::vector<uint8_t> salt, nonce, ciphertext, tag;
  if (!Common::fromHex(saltHex, salt) || !Common::fromHex(nonceHex, nonce) ||
      !Common::fromHex(ciphertextHex, ciphertext) || !Common::fromHex(tagHex, tag) ||
      salt.size() != SALT_SIZE || nonce.size() != NONCE_SIZE || tag.size() != TAG_SIZE) {
    error = "malformed keystore " + path;
    return false;
  }
  SecureBytes key(KEY_SIZE);
  if (!deriveKey(passphrase, salt, key)) {
    error = "scrypt key derivation failed";
    return false;
  }
  const std::string aad = associatedData(saltHex, nonceHex);
  std::string plaintext(ciphertext.size(), '\0');
  CipherContext cipher;
  int length = 0;
  const bool opened =
      cipher.ctx &&
      EVP_DecryptInit_ex(cipher.ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
      EVP_CIPHER_CTX_ctrl(cipher.ctx, EVP_CTRL_GCM_SET_IVLEN, NONCE_SIZE, nullptr) == 1 &&
      EVP_DecryptInit_ex(cipher.ctx, nullptr, nullptr, key.bytes.data(), nonce.data()) == 1 &&
      EVP_DecryptUpdate(cipher.ctx, nullptr, &length, reinterpret_cast<const uint8_t*>(aad.data()),
                        static_cast<int>(aad.size())) == 1 &&
      EVP_DecryptUpdate(cipher.ctx, reinterpret_cast<uint8_t*>(&plaintext[0]), &length,
                        ciphertext.data(), static_cast<int>(ciphertext.size())) == 1 &&
      EVP_CIPHER_CTX_ctrl(cipher.ctx, EVP_CTRL_GCM_SET_TAG, TAG_SIZE, tag.data()) == 1 &&
      EVP_DecryptFinal_ex(cipher.ctx, reinterpret_cast<uint8_t*>(&plaintext[0]) + length, &length) == 1;
  const bool parsed = opened && parseSecrets(plaintext, secrets);
  if (!plaintext.empty()) OPENSSL_cleanse(&plaintext[0], plaintext.size());
  if (!parsed) {
    error = "keystore " + path + " did not open: wrong passphrase or modified file";
    return false;
  }
  return true;
}

bool keystorePassphraseFromEnvironment(std::string& passphrase, std::string& error) {
  if (const char* direct = std::getenv("XFG_SWAPD_KEYSTORE_PASSPHRASE")) {
    passphrase = direct;
    return !passphrase.empty();
  }
  if (const char* file = std::getenv("XFG_SWAPD_KEYSTORE_PASSPHRASE_FILE")) {
    std::ifstream in(file);
    if (in && std::getline(in, passphrase) && !passphrase.empty()) return true;
    error = std::string("cannot read the keystore passphrase from ") + file;
    return false;
  }
  error = "set XFG_SWAPD_KEYSTORE_PASSPHRASE or XFG_SWAPD_KEYSTORE_PASSPHRASE_FILE";
  return false;
}

bool hasKeystoreReferences(const std::string& configJson) {
  return configJson.find(REFERENCE_PREFIX) != std::string::npos;
}

bool resolveKeystoreReferences(std::string& configJson, const KeystoreSecrets& secrets,
                               std::string& error) {
  const std::string prefix = REFERENCE_PREFIX;
  std::string resolved;
  size_t cursor = 0;
  for (size_t at = configJson.find(prefix); at != std::string::npos;
       at = configJson.find(prefix, cursor)) {
    const size_t nameStart = at + prefix.size();
    const size_t close = configJson.find('"', nameStart);
    if (close == std::string::npos) {
      error = "unterminated keystore reference";
      return false;
    }
    const std::string name = configJson.substr(nameStart, close - nameStart);
    const auto secret = secrets.find(name);
    if (secret == secrets.end()) {
      error = "keystore has no entry '" + name + "'";
      return false;
    }
    if (!isPlainSecret(secret->second)) {
      error = "keystore entry '" + name + "' is not a plain printable secret";
      return false;
    }
    resolved.append(configJson, cursor, at - cursor);
    resolved += "\"" + secret->second + "\"";
    cursor = close + 1;
  }
  resolved.append(configJson, cursor, std::string::npos);
  configJson.swap(resolved);
  return true;
}

}  // namespace XfgSwap
