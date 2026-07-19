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

#include "BtcHtlcScript.h"
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <openssl/sha.h>

namespace XfgSwap {

// =============================================================================
// Cryptographic hash helpers
// =============================================================================

std::vector<uint8_t> BtcHtlcScript::sha256(const std::vector<uint8_t>& data) {
  std::vector<uint8_t> digest(SHA256_DIGEST_LENGTH);
  SHA256(data.data(), data.size(), digest.data());
  return digest;
}

std::vector<uint8_t> BtcHtlcScript::doubleSha256(const std::vector<uint8_t>& data) {
  return sha256(sha256(data));
}

// =============================================================================
// Hex conversion
// =============================================================================

std::vector<uint8_t> BtcHtlcScript::hexToBytes(const std::string& hex) {
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

std::string BtcHtlcScript::bytesToHex(const std::vector<uint8_t>& bytes) {
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

void BtcHtlcScript::writeLE32(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

// =============================================================================
// CompactSize (varint) encoding
// =============================================================================

void BtcHtlcScript::writeVarInt(std::vector<uint8_t>& out, uint64_t n) {
  if (n < 0xFD) {
    out.push_back(static_cast<uint8_t>(n));
  } else if (n <= 0xFFFF) {
    out.push_back(0xFD);
    out.push_back(static_cast<uint8_t>(n & 0xFF));
    out.push_back(static_cast<uint8_t>((n >> 8) & 0xFF));
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

void BtcHtlcScript::pushData(std::vector<uint8_t>& script, const std::vector<uint8_t>& data) {
  size_t len = data.size();
  if (len == 0) {
    script.push_back(BtcOpCode::OP_FALSE);
  } else if (len <= 75) {
    script.push_back(static_cast<uint8_t>(len));
    script.insert(script.end(), data.begin(), data.end());
  } else if (len <= 255) {
    script.push_back(BtcOpCode::OP_PUSHDATA1);
    script.push_back(static_cast<uint8_t>(len));
    script.insert(script.end(), data.begin(), data.end());
  } else {
    script.push_back(0x4D);  // OP_PUSHDATA2
    script.push_back(static_cast<uint8_t>(len & 0xFF));
    script.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
    script.insert(script.end(), data.begin(), data.end());
  }
}

// =============================================================================
// CScriptNum serialization for lock time values
// =============================================================================

static std::vector<uint8_t> serializeScriptNum(uint32_t n) {
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
// HTLC Redeem Script construction
// =============================================================================

std::vector<uint8_t> BtcHtlcScript::createHashTimeLockScript(
    const std::vector<uint8_t>& hashLockSha256,
    uint32_t lockTime,
    const std::vector<uint8_t>& recipientPubKey,
    const std::vector<uint8_t>& senderPubKey,
    uint32_t timeoutBlock) {

  (void)lockTime;  // reserved for future use

  if (hashLockSha256.size() != 32) {
    throw std::runtime_error("createHashTimeLockScript: hashLock must be 32 bytes (SHA256)");
  }
  if (recipientPubKey.size() != 33) {
    throw std::runtime_error("createHashTimeLockScript: recipientPubKey must be 33 bytes (compressed)");
  }
  if (senderPubKey.size() != 33) {
    throw std::runtime_error("createHashTimeLockScript: senderPubKey must be 33 bytes (compressed)");
  }

  //
  // Script structure (same as BCH, used inside P2WSH):
  //   OP_IF
  //     OP_SHA256 <32: hashLock> OP_EQUALVERIFY <33: recipientPubKey> OP_CHECKSIG
  //   OP_ELSE
  //     <N: timeoutBlock> OP_CHECKLOCKTIMEVERIFY OP_DROP <33: senderPubKey> OP_CHECKSIG
  //   OP_ENDIF
  //
  std::vector<uint8_t> script;
  script.reserve(1 + 1 + 1 + 32 + 1 + 1 + 33 + 1 + 1 + 5 + 1 + 1 + 1 + 33 + 1 + 1);

  script.push_back(BtcOpCode::OP_IF);
  script.push_back(BtcOpCode::OP_SHA256);
  pushData(script, hashLockSha256);
  script.push_back(BtcOpCode::OP_EQUALVERIFY);
  pushData(script, recipientPubKey);
  script.push_back(BtcOpCode::OP_CHECKSIG);

  script.push_back(BtcOpCode::OP_ELSE);

  std::vector<uint8_t> lockTimeBytes = serializeScriptNum(timeoutBlock);
  pushData(script, lockTimeBytes);
  script.push_back(BtcOpCode::OP_CHECKLOCKTIMEVERIFY);
  script.push_back(BtcOpCode::OP_DROP);
  pushData(script, senderPubKey);
  script.push_back(BtcOpCode::OP_CHECKSIG);

  script.push_back(BtcOpCode::OP_ENDIF);

  return script;
}

// =============================================================================
// P2WSH construction
// =============================================================================

std::vector<uint8_t> BtcHtlcScript::witnessScriptHash(const std::vector<uint8_t>& redeemScript) {
  return sha256(redeemScript);
}

std::vector<uint8_t> BtcHtlcScript::redeemScriptToP2wshScriptPubKey(
    const std::vector<uint8_t>& redeemScript) {
  // P2WSH scriptPubKey: OP_0 (0x00) + push32 (0x20) + SHA256(redeemScript)
  // Total: 34 bytes
  std::vector<uint8_t> hash = witnessScriptHash(redeemScript);
  std::vector<uint8_t> scriptPubKey;
  scriptPubKey.reserve(34);
  scriptPubKey.push_back(BtcOpCode::OP_FALSE);  // OP_0 = 0x00
  scriptPubKey.push_back(0x20);                  // push 32 bytes
  scriptPubKey.insert(scriptPubKey.end(), hash.begin(), hash.end());
  return scriptPubKey;
}

// =============================================================================
// P2PKH scriptPubKey (for output building)
// =============================================================================

std::vector<uint8_t> BtcHtlcScript::buildP2pkhScriptPubKey(const std::vector<uint8_t>& pubKeyHash) {
  // OP_DUP OP_HASH160 <20-byte-hash> OP_EQUALVERIFY OP_CHECKSIG
  std::vector<uint8_t> script;
  script.reserve(25);
  script.push_back(BtcOpCode::OP_DUP);
  script.push_back(BtcOpCode::OP_HASH160);
  script.push_back(0x14);  // push 20 bytes
  script.insert(script.end(), pubKeyHash.begin(), pubKeyHash.end());
  script.push_back(BtcOpCode::OP_EQUALVERIFY);
  script.push_back(BtcOpCode::OP_CHECKSIG);
  return script;
}

// =============================================================================
// Raw transaction parser: extract claim preimage from a SegWit spending tx
// =============================================================================

// Helper: read a Bitcoin CompactSize (varint) from a byte range.
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

std::vector<uint8_t> BtcHtlcScript::parseClaimPreimage(
    const std::vector<uint8_t>& rawTx,
    const std::vector<uint8_t>& p2wshScriptPubKey) {

  // P2WSH scriptPubKey format: OP_0 (0x00) PUSH32 (0x20) <32-byte-SHA256> 
  // Total length: 34 bytes
  if (p2wshScriptPubKey.size() != 34) return {};
  if (p2wshScriptPubKey[0] != BtcOpCode::OP_FALSE) return {};  // OP_0
  if (p2wshScriptPubKey[1] != 0x20) return {};                 // push 32 bytes

  // Extract the expected witness script hash (bytes 2..34)
  std::vector<uint8_t> expectedHash(p2wshScriptPubKey.begin() + 2,
                                    p2wshScriptPubKey.begin() + 34);

  const uint8_t* p = rawTx.data();
  const uint8_t* end = rawTx.data() + rawTx.size();

  // Skip version (4 bytes LE)
  if (p + 4 > end) return {};
  p += 4;

  // Detect SegWit: check for marker (0x00) + flag (0x01)
  bool isSegWit = false;
  if (p + 2 <= end && p[0] == 0x00 && p[1] == 0x01) {
    isSegWit = true;
    p += 2;  // skip marker + flag
  }

  if (!isSegWit) {
    // Non-SegWit transaction — no witness data to parse
    return {};
  }

  // Read vin count
  uint64_t vinCount = 0;
  if (!readVarInt(p, end, vinCount)) return {};

  // Skip inputs: for each input, skip 32-byte txid + 4-byte vout + varint scriptSig + scriptSig + 4-byte sequence
  for (uint64_t i = 0; i < vinCount; ++i) {
    if (p + 36 > end) return {};
    p += 36;  // txid + vout

    uint64_t sigLen = 0;
    if (!readVarInt(p, end, sigLen)) return {};
    if (p + sigLen > end) return {};
    p += sigLen;  // scriptSig

    if (p + 4 > end) return {};
    p += 4;  // sequence
  }

  // Read vout count
  uint64_t voutCount = 0;
  if (!readVarInt(p, end, voutCount)) return {};

  // Skip outputs: for each output, skip 8-byte value + varint scriptPubKeyLen + scriptPubKey
  for (uint64_t i = 0; i < voutCount; ++i) {
    if (p + 8 > end) return {};
    p += 8;  // value

    uint64_t spkLen = 0;
    if (!readVarInt(p, end, spkLen)) return {};
    if (p + spkLen > end) return {};
    p += spkLen;  // scriptPubKey
  }

  // Parse witness data for each input
  for (uint64_t i = 0; i < vinCount; ++i) {
    uint64_t witnessItemCount = 0;
    if (!readVarInt(p, end, witnessItemCount)) return {};

    // Read all witness stack items
    std::vector<std::vector<uint8_t>> witnessStack;
    witnessStack.reserve(witnessItemCount);
    for (uint64_t j = 0; j < witnessItemCount; ++j) {
      uint64_t itemLen = 0;
      if (!readVarInt(p, end, itemLen)) return {};
      if (p + itemLen > end) return {};
      witnessStack.emplace_back(p, p + itemLen);
      p += itemLen;
    }

    // For a P2WSH HTLC claim, the witness stack is:
    //   [<signature>, <preimage>, <redeemScript>]
    //
    // The last item is the witnessScript (redeemScript). We verify its SHA256
    // matches the expected hash from the P2WSH scriptPubKey.
    // If it matches, the second-to-last item is the preimage.
    if (witnessStack.size() < 3) continue;

    const auto& lastItem = witnessStack.back();
    std::vector<uint8_t> hashOfScript(lastItem.begin(), lastItem.end());
    std::vector<uint8_t> computedHash = sha256(hashOfScript);

    if (computedHash != expectedHash) continue;

    // Found the HTLC witness stack. The preimage is the second-to-last item.
    const auto& preimageItem = witnessStack[witnessStack.size() - 2];
    return preimageItem;
  }

  return {};  // No matching witness stack found
}

} // namespace XfgSwap
