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

#include "KmdHtlcScript.h"
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <openssl/sha.h>
#include <openssl/ripemd.h>
#include <openssl/evp.h>
#include "Crypto/Secp256k1Signer.h"
#include "Crypto/Bip143Sighash.h"

namespace XfgSwap {

// =============================================================================
// KMD address version bytes
// =============================================================================

static constexpr uint8_t KMD_P2PKH_VERSION = 0x3C;
static constexpr uint8_t KMD_P2SH_VERSION  = 0x55;

// =============================================================================
// Bitcoin Script opcodes (same as BCH/BTC)
// =============================================================================

namespace KmdOpCode {
  constexpr uint8_t OP_FALSE     = 0x00;
  constexpr uint8_t OP_TRUE      = 0x51;  // OP_1
  constexpr uint8_t OP_IF        = 0x63;
  constexpr uint8_t OP_ELSE      = 0x67;
  constexpr uint8_t OP_ENDIF     = 0x68;
  constexpr uint8_t OP_DROP      = 0x75;
  constexpr uint8_t OP_DUP       = 0x76;
  constexpr uint8_t OP_EQUAL     = 0x87;
  constexpr uint8_t OP_EQUALVERIFY = 0x88;
  constexpr uint8_t OP_SHA256    = 0xA8;
  constexpr uint8_t OP_HASH160   = 0xA9;
  constexpr uint8_t OP_CHECKSIG  = 0xAC;
  constexpr uint8_t OP_CHECKLOCKTIMEVERIFY = 0xB1;
  constexpr uint8_t OP_PUSHDATA1 = 0x4C;
} // namespace KmdOpCode

// =============================================================================
// Cryptographic hash helpers
// =============================================================================

std::vector<uint8_t> KmdHtlcScript::sha256(const std::vector<uint8_t>& data) {
  std::vector<uint8_t> digest(SHA256_DIGEST_LENGTH);
  SHA256(data.data(), data.size(), digest.data());
  return digest;
}

std::vector<uint8_t> KmdHtlcScript::doubleSha256(const std::vector<uint8_t>& data) {
  std::vector<uint8_t> first = sha256(data);
  return sha256(first);
}

std::vector<uint8_t> KmdHtlcScript::ripemd160(const std::vector<uint8_t>& data) {
  std::vector<uint8_t> digest(RIPEMD160_DIGEST_LENGTH);
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  EVP_DigestInit_ex(ctx, EVP_ripemd160(), nullptr);
  EVP_DigestUpdate(ctx, data.data(), data.size());
  unsigned int len = 0;
  EVP_DigestFinal_ex(ctx, digest.data(), &len);
  EVP_MD_CTX_free(ctx);
  return digest;
}

std::vector<uint8_t> KmdHtlcScript::hash160(const std::vector<uint8_t>& data) {
  return ripemd160(sha256(data));
}

// =============================================================================
// Hex conversion
// =============================================================================

std::vector<uint8_t> KmdHtlcScript::hexToBytes(const std::string& hex) {
  if (hex.size() % 2 != 0) {
    throw std::runtime_error("hexToBytes: odd-length hex string");
  }
  std::vector<uint8_t> bytes;
  bytes.reserve(hex.size() / 2);
  for (size_t i = 0; i < hex.size(); i += 2) {
    uint8_t hi = 0, lo = 0;
    char c = hex[i];
    if (c >= '0' && c <= '9') hi = static_cast<uint8_t>(c - '0');
    else if (c >= 'a' && c <= 'f') hi = static_cast<uint8_t>(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') hi = static_cast<uint8_t>(c - 'A' + 10);
    else throw std::runtime_error("hexToBytes: invalid hex character");

    c = hex[i + 1];
    if (c >= '0' && c <= '9') lo = static_cast<uint8_t>(c - '0');
    else if (c >= 'a' && c <= 'f') lo = static_cast<uint8_t>(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') lo = static_cast<uint8_t>(c - 'A' + 10);
    else throw std::runtime_error("hexToBytes: invalid hex character");

    bytes.push_back(static_cast<uint8_t>((hi << 4) | lo));
  }
  return bytes;
}

std::string KmdHtlcScript::bytesToHex(const std::vector<uint8_t>& bytes) {
  static const char hexChars[] = "0123456789abcdef";
  std::string hex;
  hex.reserve(bytes.size() * 2);
  for (uint8_t b : bytes) {
    hex.push_back(hexChars[b >> 4]);
    hex.push_back(hexChars[b & 0x0F]);
  }
  return hex;
}

// =============================================================================
// Little-endian writers
// =============================================================================

void KmdHtlcScript::writeLE16(std::vector<uint8_t>& out, uint16_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void KmdHtlcScript::writeLE32(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

// =============================================================================
// CompactSize (varint) encoding
// =============================================================================

void KmdHtlcScript::writeVarInt(std::vector<uint8_t>& out, uint64_t n) {
  if (n < 0xFD) {
    out.push_back(static_cast<uint8_t>(n));
  } else if (n <= 0xFFFF) {
    out.push_back(0xFD);
    writeLE16(out, static_cast<uint16_t>(n));
  } else if (n <= 0xFFFFFFFF) {
    out.push_back(0xFE);
    writeLE32(out, static_cast<uint32_t>(n));
  } else {
    out.push_back(0xFF);
    for (int i = 0; i < 8; ++i) {
      out.push_back(static_cast<uint8_t>((n >> (i * 8)) & 0xFF));
    }
  }
}

// =============================================================================
// Script data push
// =============================================================================

void KmdHtlcScript::pushData(std::vector<uint8_t>& script, const std::vector<uint8_t>& data) {
  size_t len = data.size();
  if (len == 0) {
    script.push_back(KmdOpCode::OP_FALSE);
  } else if (len <= 75) {
    script.push_back(static_cast<uint8_t>(len));
    script.insert(script.end(), data.begin(), data.end());
  } else if (len <= 255) {
    script.push_back(KmdOpCode::OP_PUSHDATA1);
    script.push_back(static_cast<uint8_t>(len));
    script.insert(script.end(), data.begin(), data.end());
  } else {
    script.push_back(0x4D);  // OP_PUSHDATA2
    writeLE16(script, static_cast<uint16_t>(len));
    script.insert(script.end(), data.begin(), data.end());
  }
}

// =============================================================================
// CScriptNum serialization for lock time values
// =============================================================================

std::vector<uint8_t> KmdHtlcScript::serializeScriptNum(uint32_t n) {
  if (n == 0) {
    return {};
  }

  std::vector<uint8_t> result;
  uint32_t val = n;

  while (val > 0) {
    result.push_back(static_cast<uint8_t>(val & 0xFF));
    val >>= 8;
  }

  if (result.back() & 0x80) {
    result.push_back(0x00);
  }

  return result;
}

// =============================================================================
// Base58Check encoding
// =============================================================================

static const char kBase58Alphabet[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

std::string KmdHtlcScript::base58CheckEncode(uint8_t version, const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> vPayload;
  vPayload.reserve(1 + payload.size() + 4);
  vPayload.push_back(version);
  vPayload.insert(vPayload.end(), payload.begin(), payload.end());

  std::vector<uint8_t> checksum = doubleSha256(vPayload);
  vPayload.push_back(checksum[0]);
  vPayload.push_back(checksum[1]);
  vPayload.push_back(checksum[2]);
  vPayload.push_back(checksum[3]);

  size_t leadingZeros = 0;
  for (size_t i = 0; i < vPayload.size() && vPayload[i] == 0; ++i) {
    ++leadingZeros;
  }

  std::vector<uint8_t> input(vPayload.begin(), vPayload.end());
  std::string encoded;
  encoded.reserve(vPayload.size() * 138 / 100 + 1);

  while (!input.empty()) {
    uint32_t remainder = 0;
    std::vector<uint8_t> quotient;
    quotient.reserve(input.size());

    for (size_t i = 0; i < input.size(); ++i) {
      uint32_t accumulator = remainder * 256 + input[i];
      uint8_t digit = static_cast<uint8_t>(accumulator / 58);
      remainder = accumulator % 58;

      if (!quotient.empty() || digit > 0) {
        quotient.push_back(digit);
      }
    }

    encoded.push_back(kBase58Alphabet[remainder]);
    input = std::move(quotient);
  }

  for (size_t i = 0; i < leadingZeros; ++i) {
    encoded.push_back('1');
  }

  std::reverse(encoded.begin(), encoded.end());
  return encoded;
}

// =============================================================================
// ScriptPubKey builders
// =============================================================================

std::vector<uint8_t> KmdHtlcScript::buildP2pkhScriptPubKey(const std::vector<uint8_t>& pubKeyHash) {
  std::vector<uint8_t> script;
  script.reserve(25);
  script.push_back(KmdOpCode::OP_DUP);
  script.push_back(KmdOpCode::OP_HASH160);
  script.push_back(0x14);
  script.insert(script.end(), pubKeyHash.begin(), pubKeyHash.end());
  script.push_back(KmdOpCode::OP_EQUALVERIFY);
  script.push_back(KmdOpCode::OP_CHECKSIG);
  return script;
}

std::vector<uint8_t> KmdHtlcScript::buildP2shScriptPubKey(const std::vector<uint8_t>& scriptHash) {
  std::vector<uint8_t> script;
  script.reserve(23);
  script.push_back(KmdOpCode::OP_HASH160);
  script.push_back(0x14);
  script.insert(script.end(), scriptHash.begin(), scriptHash.end());
  script.push_back(KmdOpCode::OP_EQUAL);
  return script;
}

// =============================================================================
// HTLC Redeem Script construction
// =============================================================================

std::vector<uint8_t> KmdHtlcScript::createHashTimeLockScript(
    const std::vector<uint8_t>& hashLockSha256,
    uint32_t lockTime,
    const std::vector<uint8_t>& recipientPubKey,
    const std::vector<uint8_t>& senderPubKey,
    uint32_t timeoutBlock) {

  (void)lockTime;

  if (hashLockSha256.size() != 32) {
    throw std::runtime_error("createHashTimeLockScript: hashLock must be 32 bytes (SHA256)");
  }
  if (recipientPubKey.size() != 33) {
    throw std::runtime_error("createHashTimeLockScript: recipientPubKey must be 33 bytes (compressed)");
  }
  if (senderPubKey.size() != 33) {
    throw std::runtime_error("createHashTimeLockScript: senderPubKey must be 33 bytes (compressed)");
  }

  std::vector<uint8_t> script;
  script.reserve(1 + 1 + 1 + 32 + 1 + 1 + 33 + 1 + 1 + 5 + 1 + 1 + 1 + 33 + 1 + 1);

  // OP_IF
  script.push_back(KmdOpCode::OP_IF);

  // OP_SHA256
  script.push_back(KmdOpCode::OP_SHA256);

  // <hashLock> (32 bytes)
  pushData(script, hashLockSha256);

  // OP_EQUALVERIFY
  script.push_back(KmdOpCode::OP_EQUALVERIFY);

  // <recipientPubKey> (33 bytes)
  pushData(script, recipientPubKey);

  // OP_CHECKSIG
  script.push_back(KmdOpCode::OP_CHECKSIG);

  // OP_ELSE
  script.push_back(KmdOpCode::OP_ELSE);

  // <timeoutBlock> (CScriptNum encoding)
  std::vector<uint8_t> lockTimeBytes = serializeScriptNum(timeoutBlock);
  pushData(script, lockTimeBytes);

  // OP_CHECKLOCKTIMEVERIFY
  script.push_back(KmdOpCode::OP_CHECKLOCKTIMEVERIFY);

  // OP_DROP
  script.push_back(KmdOpCode::OP_DROP);

  // <senderPubKey> (33 bytes)
  pushData(script, senderPubKey);

  // OP_CHECKSIG
  script.push_back(KmdOpCode::OP_CHECKSIG);

  // OP_ENDIF
  script.push_back(KmdOpCode::OP_ENDIF);

  return script;
}

// =============================================================================
// Address helpers (KMD-specific version bytes)
// =============================================================================

std::string KmdHtlcScript::pubkeyHashToAddress(const std::vector<uint8_t>& pubkeyHash) {
  if (pubkeyHash.size() != 20) {
    throw std::runtime_error("pubkeyHashToAddress: pubkeyHash must be 20 bytes");
  }
  return base58CheckEncode(KMD_P2PKH_VERSION, pubkeyHash);
}

std::string KmdHtlcScript::scriptHashToAddress(const std::vector<uint8_t>& scriptHash) {
  if (scriptHash.size() != 20) {
    throw std::runtime_error("scriptHashToAddress: scriptHash must be 20 bytes");
  }
  return base58CheckEncode(KMD_P2SH_VERSION, scriptHash);
}

// =============================================================================
// P2SH scriptPubKey from redeem script
// =============================================================================

std::vector<uint8_t> KmdHtlcScript::redeemScriptToP2shScriptPubKey(
    const std::vector<uint8_t>& redeemScript) {
  return buildP2shScriptPubKey(hash160(redeemScript));
}

// =============================================================================
// Raw transaction parser: extract claim preimage from a spending tx
// =============================================================================

static bool readVarInt(const uint8_t*& p, const uint8_t* end, uint64_t& out) {
  if (p >= end) return false;
  uint8_t first = *p++;
  if (first < 0xFD) {
    out = first;
    return true;
  } else if (first == 0xFD) {
    if (p + 2 > end) return false;
    out = static_cast<uint64_t>(p[0]) | (static_cast<uint64_t>(p[1]) << 8);
    p += 2;
    return true;
  } else if (first == 0xFE) {
    if (p + 4 > end) return false;
    out = static_cast<uint64_t>(p[0]) | (static_cast<uint64_t>(p[1]) << 8) |
          (static_cast<uint64_t>(p[2]) << 16) | (static_cast<uint64_t>(p[3]) << 24);
    p += 4;
    return true;
  } else {
    if (p + 8 > end) return false;
    out = 0;
    for (int i = 0; i < 8; ++i) {
      out |= static_cast<uint64_t>(p[i]) << (i * 8);
    }
    p += 8;
    return true;
  }
}

std::vector<uint8_t> KmdHtlcScript::parseClaimPreimage(
    const std::vector<uint8_t>& rawTx,
    const std::vector<uint8_t>& htlcP2shScriptPubKey) {

  if (htlcP2shScriptPubKey.size() != 23) return {};
  if (htlcP2shScriptPubKey[0] != KmdOpCode::OP_HASH160) return {};
  if (htlcP2shScriptPubKey[1] != 0x14) return {};
  if (htlcP2shScriptPubKey[22] != KmdOpCode::OP_EQUAL) return {};
  std::vector<uint8_t> expectedHash(htlcP2shScriptPubKey.begin() + 2,
                                     htlcP2shScriptPubKey.begin() + 22);

  const uint8_t* p = rawTx.data();
  const uint8_t* end = rawTx.data() + rawTx.size();

  // Skip version (4 bytes LE)
  if (p + 4 > end) return {};
  p += 4;

  // Read number of inputs
  uint64_t vinCount = 0;
  if (!readVarInt(p, end, vinCount)) return {};

  // Parse each input
  for (uint64_t i = 0; i < vinCount; ++i) {
    // Skip prev txid (32 bytes) + prev vout (4 bytes)
    if (p + 36 > end) return {};
    p += 36;

    // Read scriptSig
    uint64_t scriptSigLen = 0;
    if (!readVarInt(p, end, scriptSigLen)) return {};
    if (p + scriptSigLen > end) return {};
    const uint8_t* scriptSigStart = p;
    const uint8_t* scriptSigEnd = p + scriptSigLen;
    p = scriptSigEnd;

    // Skip sequence (4 bytes)
    if (p + 4 > end) return {};
    p += 4;

    // Parse the scriptSig for push-data items
    const uint8_t* sp = scriptSigStart;
    std::vector<std::pair<const uint8_t*, size_t>> pushItems;

    while (sp < scriptSigEnd) {
      uint8_t opcode = *sp;
      if (opcode == KmdOpCode::OP_TRUE || opcode == KmdOpCode::OP_FALSE ||
          opcode == KmdOpCode::OP_IF || opcode == KmdOpCode::OP_ELSE ||
          opcode == KmdOpCode::OP_ENDIF || opcode == KmdOpCode::OP_DROP ||
          opcode == KmdOpCode::OP_EQUAL || opcode == KmdOpCode::OP_EQUALVERIFY ||
          opcode == KmdOpCode::OP_CHECKSIG || opcode == KmdOpCode::OP_CHECKLOCKTIMEVERIFY ||
          opcode == KmdOpCode::OP_SHA256 || opcode == KmdOpCode::OP_HASH160 ||
          opcode == KmdOpCode::OP_DUP) {
        ++sp;
        continue;
      }

      // Data push
      uint64_t pushLen = 0;
      if (opcode == KmdOpCode::OP_PUSHDATA1) {
        ++sp;
        if (sp >= scriptSigEnd) break;
        pushLen = *sp++;
      } else if (opcode == 0x4D) {  // OP_PUSHDATA2
        ++sp;
        if (sp + 2 > scriptSigEnd) break;
        pushLen = static_cast<uint64_t>(sp[0]) | (static_cast<uint64_t>(sp[1]) << 8);
        sp += 2;
      } else if (opcode <= 75) {
        pushLen = opcode;
        ++sp;
      } else {
        ++sp;
        continue;
      }

      if (sp + pushLen > scriptSigEnd) break;
      pushItems.emplace_back(sp, pushLen);
      sp += pushLen;
    }

    // Need at least 3 push items: <sig> <preimage> OP_TRUE <redeemScript>
    if (pushItems.size() < 3) continue;

    // The last push item is the redeemScript
    const auto& redeemScriptPush = pushItems.back();
    std::vector<uint8_t> redeemScript(redeemScriptPush.first,
                                      redeemScriptPush.first + redeemScriptPush.second);
    std::vector<uint8_t> p2shHash = hash160(redeemScript);

    if (p2shHash != expectedHash) continue;

    // Found the HTLC input — preimage is the second push item
    const auto& preimagePush = pushItems[1];
    return std::vector<uint8_t>(preimagePush.first,
                                preimagePush.first + preimagePush.second);
  }

  return {};
}

// =============================================================================
// Base58Check decoding
// =============================================================================

bool KmdHtlcScript::base58CheckDecode(const std::string& encoded, uint8_t& version,
                                       std::vector<uint8_t>& payload) {
  if (encoded.empty()) return false;

  int8_t b58map[256];
  std::memset(b58map, -1, sizeof(b58map));
  for (int i = 0; i < 58; ++i) {
    b58map[static_cast<uint8_t>(kBase58Alphabet[i])] = static_cast<int8_t>(i);
  }

  size_t leadingZeros = 0;
  for (size_t i = 0; i < encoded.size() && encoded[i] == '1'; ++i) {
    ++leadingZeros;
  }

  size_t maxBytes = encoded.size() * 733 / 1000 + 1;
  std::vector<uint8_t> b256(maxBytes, 0);

  for (size_t i = 0; i < encoded.size(); ++i) {
    int8_t carry = b58map[static_cast<uint8_t>(encoded[i])];
    if (carry < 0) return false;

    uint32_t c = static_cast<uint32_t>(carry);
    for (auto it = b256.rbegin(); it != b256.rend(); ++it) {
      c += 58u * static_cast<uint32_t>(*it);
      *it = static_cast<uint8_t>(c & 0xFF);
      c >>= 8;
    }
  }

  auto it = std::find_if(b256.begin(), b256.end(), [](uint8_t b) { return b != 0; });

  std::vector<uint8_t> result;
  result.reserve(leadingZeros + static_cast<size_t>(std::distance(it, b256.end())));
  for (size_t i = 0; i < leadingZeros; ++i) {
    result.push_back(0x00);
  }
  result.insert(result.end(), it, b256.end());

  if (result.size() < 5) return false;

  size_t dataLen = result.size() - 4;
  std::vector<uint8_t> dataForChecksum(result.begin(), result.begin() + static_cast<ptrdiff_t>(dataLen));
  std::vector<uint8_t> expectedChecksum = doubleSha256(dataForChecksum);

  if (result[dataLen] != expectedChecksum[0] ||
      result[dataLen + 1] != expectedChecksum[1] ||
      result[dataLen + 2] != expectedChecksum[2] ||
      result[dataLen + 3] != expectedChecksum[3]) {
    return false;
  }

  version = result[0];
  payload.assign(result.begin() + 1, result.begin() + static_cast<ptrdiff_t>(dataLen));
  return true;
}

// =============================================================================
// WIF to private key
// =============================================================================

bool KmdHtlcScript::wifToPubkeyHash(const std::string& wif,
                                    std::vector<uint8_t>& pubkeyHash) {
  uint8_t version = 0;
  std::vector<uint8_t> payload;
  if (!base58CheckDecode(wif, version, payload)) return false;
  if (payload.size() == 33 && payload.back() == 0x01) {
    payload.pop_back();
  }
  if (payload.size() != 32) return false;

  std::array<uint8_t, 32> privKey{};
  std::copy(payload.begin(), payload.end(), privKey.begin());
  CryptoNote::SwapDaemon::Crypto::Secp256k1Signer signer;
  auto pubKey = signer.derivePublicKeyCompressed(privKey);
  pubkeyHash = hash160(std::vector<uint8_t>(pubKey.begin(), pubKey.end()));
  return true;
}

bool KmdHtlcScript::wifToPrivKey(const std::string& wif,
                                  std::array<uint8_t, 32>& privKey) {
  uint8_t version = 0;
  std::vector<uint8_t> payload;
  if (!base58CheckDecode(wif, version, payload)) return false;
  if (version != 0xBC) return false;
  if (payload.size() == 33 && payload.back() == 0x01) {
    payload.pop_back();
  }
  if (payload.size() != 32) return false;
  std::copy(payload.begin(), payload.end(), privKey.begin());
  return true;
}

// =============================================================================
// ScriptSig builders for HTLC spending
// =============================================================================

std::vector<uint8_t> KmdHtlcScript::createClaimScriptSig(
    const std::vector<uint8_t>& signature,
    const std::vector<uint8_t>& preimage,
    const std::vector<uint8_t>& redeemScript) {
  std::vector<uint8_t> scriptSig;
  scriptSig.reserve(signature.size() + preimage.size() + redeemScript.size() + 10);
  pushData(scriptSig, signature);
  pushData(scriptSig, preimage);
  scriptSig.push_back(KmdOpCode::OP_TRUE);
  pushData(scriptSig, redeemScript);
  return scriptSig;
}

std::vector<uint8_t> KmdHtlcScript::createRefundScriptSig(
    const std::vector<uint8_t>& signature,
    const std::vector<uint8_t>& redeemScript) {
  std::vector<uint8_t> scriptSig;
  scriptSig.reserve(signature.size() + redeemScript.size() + 5);
  pushData(scriptSig, signature);
  scriptSig.push_back(KmdOpCode::OP_FALSE);
  pushData(scriptSig, redeemScript);
  return scriptSig;
}

// =============================================================================
// Raw transaction builder
// =============================================================================

std::vector<uint8_t> KmdHtlcScript::buildRawTransaction(
    const std::string& inputTxid,
    uint32_t inputVout,
    uint64_t inputAmount,
    const std::vector<uint8_t>& scriptSig,
    const std::string& outputAddress,
    uint64_t outputAmount,
    uint32_t nLockTime) {

  (void)inputAmount;

  uint8_t addrVersion = 0;
  std::vector<uint8_t> addrHash;
  if (!decodeAddress(outputAddress, addrVersion, addrHash)) {
    throw std::runtime_error("buildRawTransaction: invalid output address");
  }

  std::vector<uint8_t> scriptPubKey;
  if (addrVersion == 0x3C) {
    scriptPubKey = buildP2pkhScriptPubKey(addrHash);
  } else if (addrVersion == 0x55) {
    scriptPubKey = buildP2shScriptPubKey(addrHash);
  } else {
    throw std::runtime_error("buildRawTransaction: unsupported address version");
  }

  std::vector<uint8_t> txidBytes = hexToBytes(inputTxid);
  if (txidBytes.size() != 32) {
    throw std::runtime_error("buildRawTransaction: txid must be 32 bytes (64 hex chars)");
  }
  std::reverse(txidBytes.begin(), txidBytes.end());

  std::vector<uint8_t> tx;
  tx.reserve(4 + 1 + 32 + 4 + scriptSig.size() + 4 + 1 + 8 + scriptPubKey.size() + 4 + 10);

  writeLE32(tx, 1);
  writeVarInt(tx, 1);
  tx.insert(tx.end(), txidBytes.begin(), txidBytes.end());
  writeLE32(tx, inputVout);
  writeVarInt(tx, scriptSig.size());
  tx.insert(tx.end(), scriptSig.begin(), scriptSig.end());
  uint32_t nSequence = (nLockTime > 0) ? 0xFFFFFFFE : 0xFFFFFFFD;
  writeLE32(tx, nSequence);
  writeVarInt(tx, 1);
  writeLE64(tx, outputAmount);
  writeVarInt(tx, scriptPubKey.size());
  tx.insert(tx.end(), scriptPubKey.begin(), scriptPubKey.end());
  writeLE32(tx, nLockTime);

  return tx;
}

// =============================================================================
// BIP143 signing
// =============================================================================

std::vector<uint8_t> KmdHtlcScript::signInput(
    const std::array<uint8_t, 32>& privKey,
    uint32_t txVersion,
    uint32_t nLocktime,
    uint32_t nSequence,
    const std::string& htlcTxid,
    uint32_t htlcVout,
    const std::vector<uint8_t>& redeemScript,
    uint64_t htlcAmount,
    const std::vector<uint8_t>& outputScript,
    uint64_t outputAmount) {

  auto txidBytes = hexToBytes(htlcTxid);
  if (txidBytes.size() != 32) return {};
  std::reverse(txidBytes.begin(), txidBytes.end());
  std::array<uint8_t, 32> txidLE;
  std::copy(txidBytes.begin(), txidBytes.end(), txidLE.begin());

  CryptoNote::SwapDaemon::Crypto::Bip143Sighash bip143;
  auto sighash = bip143.computeForP2sh(
      txVersion, nLocktime, nSequence,
      txidLE, htlcVout,
      redeemScript,
      htlcAmount,
      outputScript,
      outputAmount,
      0x01);

  CryptoNote::SwapDaemon::Crypto::Secp256k1Signer signer;
  auto sig = signer.signRecoverable(sighash, privKey);

  auto& r = sig.r;
  auto& s = sig.s;

  size_t rStart = 0, sStart = 0;
  while (rStart < 31 && r[rStart] == 0) ++rStart;
  while (sStart < 31 && s[sStart] == 0) ++sStart;

  bool rPad = (r[rStart] & 0x80) != 0;
  bool sPad = (s[sStart] & 0x80) != 0;

  size_t rLen = 32 - rStart + (rPad ? 1 : 0);
  size_t sLen = 32 - sStart + (sPad ? 1 : 0);
  size_t seqLen = 2 + rLen + 2 + sLen;

  std::vector<uint8_t> der;
  der.push_back(0x30);
  der.push_back(static_cast<uint8_t>(seqLen));
  der.push_back(0x02);
  der.push_back(static_cast<uint8_t>(rLen));
  if (rPad) der.push_back(0x00);
  der.insert(der.end(), r.begin() + rStart, r.end());
  der.push_back(0x02);
  der.push_back(static_cast<uint8_t>(sLen));
  if (sPad) der.push_back(0x00);
  der.insert(der.end(), s.begin() + sStart, s.end());
  der.push_back(0x01);

  return der;
}

// =============================================================================
// Address decoding wrapper
// =============================================================================

bool KmdHtlcScript::decodeAddress(const std::string& address, uint8_t& version,
                                   std::vector<uint8_t>& hash) {
  if (!base58CheckDecode(address, version, hash)) {
    return false;
  }
  return hash.size() == 20;
}

// =============================================================================
// writeLE64
// =============================================================================

void KmdHtlcScript::writeLE64(std::vector<uint8_t>& out, uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
  }
}

} // namespace XfgSwap
