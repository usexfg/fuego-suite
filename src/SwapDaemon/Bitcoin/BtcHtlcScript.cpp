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
#include "Crypto/Bip143Sighash.h"
#include "Crypto/Secp256k1Signer.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <openssl/sha.h>
#include <stdexcept>

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

    // P2WSH HTLC claim witness stack layouts:
    //   3-item: [<sig>, <preimage>, <redeemScript>]
    //   4-item: [<sig>, <preimage>, <OP_TRUE>, <redeemScript>] (SegWit v0)
    // The last item is always the witnessScript. We verify its SHA256
    // matches the expected hash from the P2WSH scriptPubKey.
    if (witnessStack.size() < 3) continue;

    const auto& lastItem = witnessStack.back();
    std::vector<uint8_t> hashOfScript(lastItem.begin(), lastItem.end());
    std::vector<uint8_t> computedHash = sha256(hashOfScript);

    if (computedHash != expectedHash) continue;

    // Found the HTLC witness stack. The preimage is always at index 1.
    const auto& preimageItem = witnessStack[1];
    return preimageItem;
  }

  return {};  // No matching witness stack found
}

// =============================================================================
// Base58 and Base58Check helpers
// =============================================================================

namespace {
  const char kBase58Alphabet[] =
      "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
}

std::vector<uint8_t> BtcHtlcScript::base58Decode(const std::string& s) {
  std::vector<uint8_t> result;
  for (char c : s) {
    const char* pos = strchr(kBase58Alphabet, c);
    if (!pos) return {};
    size_t digit = static_cast<size_t>(pos - kBase58Alphabet);
    uint32_t carry = static_cast<uint32_t>(digit);
    for (auto& b : result) {
      carry += static_cast<uint32_t>(b) * 58;
      b = static_cast<uint8_t>(carry & 0xFF);
      carry >>= 8;
    }
    while (carry > 0) {
      result.push_back(static_cast<uint8_t>(carry & 0xFF));
      carry >>= 8;
    }
  }
  size_t leading = 0;
  while (leading < s.size() && s[leading] == '1') {
    result.push_back(0);
    ++leading;
  }
  std::reverse(result.begin(), result.end());
  return result;
}

bool BtcHtlcScript::base58CheckDecode(const std::string& encoded, uint8_t& version,
                                      std::vector<uint8_t>& payload) {
  auto decoded = base58Decode(encoded);
  if (decoded.size() < 5) return false;
  std::vector<uint8_t> data(decoded.begin(), decoded.end() - 4);
  std::array<uint8_t, 4> checksum;
  std::copy(decoded.end() - 4, decoded.end(), checksum.begin());
  auto hash = doubleSha256(data);
  if (hash[0] != checksum[0] || hash[1] != checksum[1] ||
      hash[2] != checksum[2] || hash[3] != checksum[3]) {
    return false;
  }
  if (data.empty()) return false;
  version = data[0];
  payload.assign(data.begin() + 1, data.end());
  return true;
}

// =============================================================================
// WIF decode and signing
// =============================================================================

bool BtcHtlcScript::wifToPrivKey(const std::string& wif,
                                  std::array<uint8_t, 32>& privKey) {
  uint8_t version = 0;
  std::vector<uint8_t> payload;
  if (!base58CheckDecode(wif, version, payload)) return false;
  if (version != 0x80) return false;
  if (payload.size() == 33 && payload.back() == 0x01) {
    payload.pop_back();
  }
  if (payload.size() != 32) return false;
  std::copy(payload.begin(), payload.end(), privKey.begin());
  return true;
}

std::vector<uint8_t> BtcHtlcScript::signInput(
    const std::array<uint8_t, 32>& privKey,
    uint32_t txVersion,
    uint32_t nLocktime,
    uint32_t nSequence,
    const std::string& htlcTxid,
    uint32_t htlcVout,
    const std::vector<uint8_t>& witnessScript,
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
      witnessScript,
      htlcAmount,
      outputScript,
      outputAmount,
      /*sighashType=*/0x01);

  CryptoNote::SwapDaemon::Crypto::Secp256k1Signer signer;
  auto sig = signer.signRecoverable(sighash, privKey);

  auto& r = sig.r;
  auto& s = sig.s;

  size_t rStart = 0;
  while (rStart < 31 && r[rStart] == 0) ++rStart;
  size_t sStart = 0;
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
  der.push_back(0x01);  // SIGHASH_ALL (no fork ID for BTC)

  return der;
}

// =============================================================================
// Bech32 encoding helpers
// =============================================================================

static const char kBech32Charset[] = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";

static const uint32_t kBech32Gen[] = {
  0x3b6a57b2, 0x26508e6d, 0x1ea119fa, 0x3d4233dd, 0x2a1462b3
};

static uint32_t bech32Polymod(const std::vector<uint8_t>& values) {
  uint32_t chk = 1;
  for (uint8_t v : values) {
    uint32_t b = chk >> 25;
    chk = ((chk & 0x1ffffff) << 5) ^ v;
    for (size_t i = 0; i < 5; ++i) {
      if ((b >> i) & 1) chk ^= kBech32Gen[i];
    }
  }
  return chk;
}

static std::vector<uint8_t> hrpExpand(const std::string& hrp) {
  std::vector<uint8_t> exp;
  exp.reserve(hrp.size() * 2 + 1);
  for (char c : hrp) exp.push_back(static_cast<uint8_t>(c >> 5));
  exp.push_back(0);
  for (char c : hrp) exp.push_back(static_cast<uint8_t>(c & 31));
  return exp;
}

static std::vector<uint8_t> convertBits8to5(const std::vector<uint8_t>& data) {
  uint32_t acc = 0;
  int bits = 0;
  const uint8_t maxv = 31;
  std::vector<uint8_t> result;
  for (uint8_t v : data) {
    acc = (acc << 8) | v;
    bits += 8;
    while (bits >= 5) {
      bits -= 5;
      result.push_back(static_cast<uint8_t>((acc >> bits) & maxv));
    }
  }
  if (bits > 0) {
    result.push_back(static_cast<uint8_t>((acc << (5 - bits)) & maxv));
  }
  return result;
}

static std::string bech32Encode(const std::string& hrp,
                                 const std::vector<uint8_t>& data5) {
  auto hrpExp = hrpExpand(hrp);
  std::vector<uint8_t> combined = hrpExp;
  combined.insert(combined.end(), data5.begin(), data5.end());
  combined.insert(combined.end(), 6, 0);
  uint32_t checksum = bech32Polymod(combined) ^ 1;
  std::string result = hrp + "1";
  for (uint8_t v : data5) result += kBech32Charset[v];
  for (int i = 0; i < 6; ++i)
    result += kBech32Charset[(checksum >> (5 * (5 - i))) & 31];
  return result;
}

// =============================================================================
// witnessScriptToAddress: compute bech32 P2WSH address
// =============================================================================

std::string BtcHtlcScript::witnessScriptToAddress(
    const std::vector<uint8_t>& witnessScript, const std::string& hrp) {
  auto hash = sha256(witnessScript);
  std::vector<uint8_t> witnessProgram;
  witnessProgram.push_back(0x00);
  witnessProgram.insert(witnessProgram.end(), hash.begin(), hash.end());
  auto data5 = convertBits8to5(witnessProgram);
  return bech32Encode(hrp, data5);
}

// =============================================================================
// buildRawSegWitTx: build a raw SegWit transaction wire format
// =============================================================================

static void writeLE64(std::vector<uint8_t>& out, uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    v >>= 8;
  }
}

std::vector<uint8_t> BtcHtlcScript::buildRawSegWitTx(
    const std::string& inputTxid, uint32_t inputVout, uint64_t inputAmount,
    const std::vector<uint8_t>& scriptSig,
    const std::vector<std::vector<uint8_t>>& witnessStack,
    const std::string& outputAddress, uint64_t outputAmount,
    uint32_t nLockTime) {
  (void)inputAmount;

  std::vector<uint8_t> tx;

  // Version (4 bytes LE)
  writeLE32(tx, 2);

  // SegWit marker + flag
  tx.push_back(0x00);
  tx.push_back(0x01);

  // Input count
  writeVarInt(tx, 1);

  // Input txid (little-endian: reverse the hex bytes)
  auto txidBytes = hexToBytes(inputTxid);
  std::reverse(txidBytes.begin(), txidBytes.end());
  tx.insert(tx.end(), txidBytes.begin(), txidBytes.end());

  // Input vout
  writeLE32(tx, inputVout);

  // scriptSig
  writeVarInt(tx, scriptSig.size());
  tx.insert(tx.end(), scriptSig.begin(), scriptSig.end());

  // Sequence
  writeLE32(tx, 0xFFFFFFFE);

  // Output count
  writeVarInt(tx, 1);

  // Output value
  writeLE64(tx, outputAmount);

  // Output scriptPubKey (P2PKH)
  uint8_t addrVersion = 0;
  std::vector<uint8_t> pubKeyHash;
  if (!base58CheckDecode(outputAddress, addrVersion, pubKeyHash) ||
      pubKeyHash.size() != 20) {
    throw std::runtime_error("buildRawSegWitTx: invalid P2PKH output address");
  }
  if (addrVersion != 0x00) {
    throw std::runtime_error("buildRawSegWitTx: not a P2PKH address");
  }
  auto scriptPubKey = buildP2pkhScriptPubKey(pubKeyHash);
  writeVarInt(tx, scriptPubKey.size());
  tx.insert(tx.end(), scriptPubKey.begin(), scriptPubKey.end());

  // Witness data
  writeVarInt(tx, witnessStack.size());
  for (const auto& item : witnessStack) {
    writeVarInt(tx, item.size());
    tx.insert(tx.end(), item.begin(), item.end());
  }

  // nLockTime
  writeLE32(tx, nLockTime);

  return tx;
}

// =============================================================================
// createClaimWitness: <sig> <preimage> OP_1 <witnessScript>
// =============================================================================

std::vector<std::vector<uint8_t>> BtcHtlcScript::createClaimWitness(
    const std::vector<uint8_t>& signature,
    const std::vector<uint8_t>& preimage,
    const std::vector<uint8_t>& witnessScript) {
  std::vector<std::vector<uint8_t>> witness;
  witness.reserve(4);
  witness.push_back(signature);
  witness.push_back(preimage);
  witness.push_back({BtcOpCode::OP_TRUE});  // OP_1 = 0x51
  witness.push_back(witnessScript);
  return witness;
}

// =============================================================================
// createRefundWitness: <sig> OP_0 <witnessScript>
// =============================================================================

std::vector<std::vector<uint8_t>> BtcHtlcScript::createRefundWitness(
    const std::vector<uint8_t>& signature,
    const std::vector<uint8_t>& witnessScript) {
  std::vector<std::vector<uint8_t>> witness;
  witness.reserve(3);
  witness.push_back(signature);
  witness.push_back({BtcOpCode::OP_FALSE});  // OP_0 = 0x00
  witness.push_back(witnessScript);
  return witness;
}

} // namespace XfgSwap
