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

#include "LtcHtlcScript.h"
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <openssl/sha.h>
#include <openssl/evp.h>

namespace XfgSwap {

// =============================================================================
// Cryptographic hash helpers (Bitcoin standard, NOT keccak)
// =============================================================================

std::vector<uint8_t> LtcHtlcScript::sha256(const std::vector<uint8_t>& data) {
  std::vector<uint8_t> digest(SHA256_DIGEST_LENGTH);
  SHA256(data.data(), data.size(), digest.data());
  return digest;
}

std::vector<uint8_t> LtcHtlcScript::doubleSha256(const std::vector<uint8_t>& data) {
  std::vector<uint8_t> first = sha256(data);
  return sha256(first);
}

// =============================================================================
// Hex conversion
// =============================================================================

std::vector<uint8_t> LtcHtlcScript::hexToBytes(const std::string& hex) {
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

std::string LtcHtlcScript::bytesToHex(const std::vector<uint8_t>& bytes) {
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

void LtcHtlcScript::writeLE16(std::vector<uint8_t>& out, uint16_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void LtcHtlcScript::writeLE32(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void LtcHtlcScript::writeLE64(std::vector<uint8_t>& out, uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
  }
}

// =============================================================================
// CompactSize (varint) encoding
// =============================================================================

void LtcHtlcScript::writeVarInt(std::vector<uint8_t>& out, uint64_t n) {
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
    writeLE64(out, n);
  }
}

// =============================================================================
// Script data push (Bitcoin length-prefixed encoding)
// =============================================================================

void LtcHtlcScript::pushData(std::vector<uint8_t>& script, const std::vector<uint8_t>& data) {
  size_t len = data.size();
  if (len == 0) {
    script.push_back(LtcOpCode::OP_FALSE);
  } else if (len <= 75) {
    script.push_back(static_cast<uint8_t>(len));
    script.insert(script.end(), data.begin(), data.end());
  } else if (len <= 255) {
    script.push_back(LtcOpCode::OP_PUSHDATA1);
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

std::vector<uint8_t> LtcHtlcScript::serializeScriptNum(uint32_t n) {
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

std::string LtcHtlcScript::base58CheckEncode(uint8_t version, const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> vPayload;
  vPayload.reserve(1 + payload.size() + 4);
  vPayload.push_back(version);
  vPayload.insert(vPayload.end(), payload.begin(), payload.end());

  std::vector<uint8_t> checksum = LtcHtlcScript::doubleSha256(vPayload);
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

bool LtcHtlcScript::base58CheckDecode(const std::string& encoded, uint8_t& version,
                                       std::vector<uint8_t>& payload) {
  if (encoded.empty()) return false;

  int8_t b58map[256];
  std::memset(b58map, -1, sizeof(b58map));
  for (int i = 0; i < 58; ++i) {
    b58map[static_cast<uint8_t>(kBase58Alphabet[i])] = static_cast<int8_t>(i);
  }

  size_t leadingOnes = 0;
  for (size_t i = 0; i < encoded.size() && encoded[i] == '1'; ++i) {
    ++leadingOnes;
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
  result.reserve(leadingOnes + static_cast<size_t>(std::distance(it, b256.end())));
  for (size_t i = 0; i < leadingOnes; ++i) {
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
// ScriptPubKey builders
// =============================================================================

std::vector<uint8_t> LtcHtlcScript::buildP2pkhScriptPubKey(const std::vector<uint8_t>& pubKeyHash) {
  std::vector<uint8_t> script;
  script.reserve(25);
  script.push_back(LtcOpCode::OP_DUP);
  script.push_back(LtcOpCode::OP_HASH160);
  script.push_back(0x14);
  script.insert(script.end(), pubKeyHash.begin(), pubKeyHash.end());
  script.push_back(LtcOpCode::OP_EQUALVERIFY);
  script.push_back(LtcOpCode::OP_CHECKSIG);
  return script;
}

// =============================================================================
// HTLC Redeem Script construction
// =============================================================================

std::vector<uint8_t> LtcHtlcScript::createHashTimeLockScript(
    const std::vector<uint8_t>& hashLockSha256,
    const std::vector<uint8_t>& recipientPubKey,
    const std::vector<uint8_t>& senderPubKey,
    uint32_t timeoutBlock) {

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

  script.push_back(LtcOpCode::OP_IF);
  script.push_back(LtcOpCode::OP_SHA256);
  pushData(script, hashLockSha256);
  script.push_back(LtcOpCode::OP_EQUALVERIFY);
  pushData(script, recipientPubKey);
  script.push_back(LtcOpCode::OP_CHECKSIG);
  script.push_back(LtcOpCode::OP_ELSE);

  std::vector<uint8_t> lockTimeBytes = serializeScriptNum(timeoutBlock);
  pushData(script, lockTimeBytes);

  script.push_back(LtcOpCode::OP_CHECKLOCKTIMEVERIFY);
  script.push_back(LtcOpCode::OP_DROP);
  pushData(script, senderPubKey);
  script.push_back(LtcOpCode::OP_CHECKSIG);
  script.push_back(LtcOpCode::OP_ENDIF);

  return script;
}

// =============================================================================
// P2WSH: witness script hash and scriptPubKey
// =============================================================================

std::vector<uint8_t> LtcHtlcScript::witnessScriptHash(
    const std::vector<uint8_t>& redeemScript) {
  return sha256(redeemScript);
}

std::vector<uint8_t> LtcHtlcScript::redeemScriptToP2wshScriptPubKey(
    const std::vector<uint8_t>& redeemScript) {
  // P2WSH scriptPubKey: OP_0 <32-byte-SHA256(redeemScript)>
  // OP_0 = 0x00, push 32 bytes = 0x20
  std::vector<uint8_t> hash = sha256(redeemScript);
  std::vector<uint8_t> script;
  script.reserve(2 + 32);
  script.push_back(LtcOpCode::OP_FALSE);  // OP_0
  script.push_back(0x20);                 // push 32 bytes
  script.insert(script.end(), hash.begin(), hash.end());
  return script;
}

// =============================================================================
// Helper: read a Bitcoin CompactSize (varint)
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

// =============================================================================
// Parse claim preimage from a SegWit transaction
// =============================================================================

std::vector<uint8_t> LtcHtlcScript::parseClaimPreimage(
    const std::vector<uint8_t>& rawTx,
    const std::vector<uint8_t>& p2wshScriptPubKey) {

  // P2WSH scriptPubKey format: OP_0 (0x00) PUSH32 (0x20) <32-byte-hash>
  // Total length: 34 bytes. The hash is at bytes [2..34).
  if (p2wshScriptPubKey.size() != 34) return {};
  if (p2wshScriptPubKey[0] != LtcOpCode::OP_FALSE) return {};  // OP_0
  if (p2wshScriptPubKey[1] != 0x20) return {};                // push 32 bytes
  std::vector<uint8_t> expectedHash(p2wshScriptPubKey.begin() + 2,
                                     p2wshScriptPubKey.begin() + 34);

  const uint8_t* p = rawTx.data();
  const uint8_t* end = rawTx.data() + rawTx.size();

  // Skip version (4 bytes LE)
  if (p + 4 > end) return {};
  p += 4;

  // Detect SegWit: marker byte must be 0x00, flag byte must be 0x01
  if (p + 2 > end) return {};
  bool isSegWit = (*p == 0x00 && *(p + 1) == 0x01);
  if (!isSegWit) return {};  // Not a SegWit transaction

  p += 2;  // skip marker + flag

  // Read number of inputs
  uint64_t vinCount = 0;
  if (!readVarInt(p, end, vinCount)) return {};

  // Skip all inputs (each: prev_txid(32) + prev_vout(4) + scriptSig_len(varint) + scriptSig + sequence(4))
  for (uint64_t i = 0; i < vinCount; ++i) {
    if (p + 36 > end) return {};
    p += 36;  // txid + vout

    uint64_t scriptSigLen = 0;
    if (!readVarInt(p, end, scriptSigLen)) return {};
    if (p + scriptSigLen > end) return {};
    p += scriptSigLen;

    if (p + 4 > end) return {};
    p += 4;  // sequence
  }

  // Read number of outputs
  uint64_t voutCount = 0;
  if (!readVarInt(p, end, voutCount)) return {};

  // Skip all outputs (each: value(8) + scriptPubKey_len(varint) + scriptPubKey)
  for (uint64_t i = 0; i < voutCount; ++i) {
    if (p + 8 > end) return {};
    p += 8;  // value

    uint64_t spkLen = 0;
    if (!readVarInt(p, end, spkLen)) return {};
    if (p + spkLen > end) return {};
    p += spkLen;
  }

  // Now parse witness data for each input
  for (uint64_t i = 0; i < vinCount; ++i) {
    uint64_t witnessItemCount = 0;
    if (!readVarInt(p, end, witnessItemCount)) return {};

    // Read all witness items
    std::vector<std::pair<const uint8_t*, size_t>> witnessItems;
    witnessItems.reserve(witnessItemCount);
    for (uint64_t j = 0; j < witnessItemCount; ++j) {
      uint64_t itemLen = 0;
      if (!readVarInt(p, end, itemLen)) return {};
      if (p + itemLen > end) return {};
      witnessItems.emplace_back(p, itemLen);
      p += itemLen;
    }

    // A P2WSH HTLC claim witness has at least 3 items: <sig> <preimage> <redeemScript>
    // The last item is the witnessScript (redeemScript).
    // The items before it are the stack: [sig, preimage, 1] for claim.
    if (witnessItems.size() < 3) continue;

    const auto& witnessScriptItem = witnessItems.back();
    std::vector<uint8_t> witnessScript(witnessScriptItem.first,
                                        witnessScriptItem.first + witnessScriptItem.second);

    // Check if SHA256(witnessScript) matches the expected hash
    std::vector<uint8_t> actualHash = sha256(witnessScript);
    if (actualHash != expectedHash) continue;

    // Found the HTLC input. The preimage is the second-to-last witness item
    // (second item in the stack, index 1).
    const auto& preimageItem = witnessItems[1];
    return std::vector<uint8_t>(preimageItem.first,
                                 preimageItem.first + preimageItem.second);
  }

  return {};  // No matching input found
}

} // namespace XfgSwap
