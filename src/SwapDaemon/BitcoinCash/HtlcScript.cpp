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

#include "HtlcScript.h"
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <openssl/sha.h>
#include <openssl/ripemd.h>
#include <openssl/evp.h>

namespace XfgSwap {

// =============================================================================
// Cryptographic hash helpers (Bitcoin standard, NOT keccak)
// =============================================================================

std::vector<uint8_t> BchHtlcScript::sha256(const std::vector<uint8_t>& data) {
  std::vector<uint8_t> digest(SHA256_DIGEST_LENGTH);
  SHA256(data.data(), data.size(), digest.data());
  return digest;
}

std::vector<uint8_t> BchHtlcScript::doubleSha256(const std::vector<uint8_t>& data) {
  std::vector<uint8_t> first = sha256(data);
  return sha256(first);
}

std::vector<uint8_t> BchHtlcScript::ripemd160(const std::vector<uint8_t>& data) {
  std::vector<uint8_t> digest(RIPEMD160_DIGEST_LENGTH);
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  EVP_DigestInit_ex(ctx, EVP_ripemd160(), nullptr);
  EVP_DigestUpdate(ctx, data.data(), data.size());
  unsigned int len = 0;
  EVP_DigestFinal_ex(ctx, digest.data(), &len);
  EVP_MD_CTX_free(ctx);
  return digest;
}

std::vector<uint8_t> BchHtlcScript::hash160(const std::vector<uint8_t>& data) {
  return ripemd160(sha256(data));
}

// =============================================================================
// Hex conversion
// =============================================================================

std::vector<uint8_t> BchHtlcScript::hexToBytes(const std::string& hex) {
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

std::string BchHtlcScript::bytesToHex(const std::vector<uint8_t>& bytes) {
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

void BchHtlcScript::writeLE16(std::vector<uint8_t>& out, uint16_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void BchHtlcScript::writeLE32(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void BchHtlcScript::writeLE64(std::vector<uint8_t>& out, uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
  }
}

// =============================================================================
// CompactSize (varint) encoding
// =============================================================================

void BchHtlcScript::writeVarInt(std::vector<uint8_t>& out, uint64_t n) {
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

void BchHtlcScript::pushData(std::vector<uint8_t>& script, const std::vector<uint8_t>& data) {
  size_t len = data.size();
  if (len == 0) {
    script.push_back(OpCode::OP_FALSE);  // OP_0 pushes empty byte vector
  } else if (len <= 75) {
    // Direct push: single byte length prefix
    script.push_back(static_cast<uint8_t>(len));
    script.insert(script.end(), data.begin(), data.end());
  } else if (len <= 255) {
    // OP_PUSHDATA1 <1-byte-length> <data>
    script.push_back(OpCode::OP_PUSHDATA1);
    script.push_back(static_cast<uint8_t>(len));
    script.insert(script.end(), data.begin(), data.end());
  } else {
    // OP_PUSHDATA2 <2-byte-length-LE> <data>
    script.push_back(0x4D);  // OP_PUSHDATA2
    writeLE16(script, static_cast<uint16_t>(len));
    script.insert(script.end(), data.begin(), data.end());
  }
}

// =============================================================================
// CScriptNum serialization for lock time values
// =============================================================================

std::vector<uint8_t> BchHtlcScript::serializeScriptNum(uint32_t n) {
  // Bitcoin CScriptNum encoding: little-endian, minimal encoding,
  // with sign bit in the MSB of the last byte.
  // Since lock times are always positive, we just need minimal LE encoding
  // with an extra 0x00 byte if the MSB of the last byte is set (to avoid
  // being interpreted as negative).

  if (n == 0) {
    return {};
  }

  std::vector<uint8_t> result;
  uint32_t val = n;

  while (val > 0) {
    result.push_back(static_cast<uint8_t>(val & 0xFF));
    val >>= 8;
  }

  // If the MSB of the last byte is set, append 0x00 to indicate positive
  if (result.back() & 0x80) {
    result.push_back(0x00);
  }

  return result;
}

// =============================================================================
// Base58Check encoding
// =============================================================================

static const char kBase58Alphabet[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

std::string BchHtlcScript::base58CheckEncode(uint8_t version, const std::vector<uint8_t>& payload) {
  // Build versioned payload
  std::vector<uint8_t> vPayload;
  vPayload.reserve(1 + payload.size() + 4);
  vPayload.push_back(version);
  vPayload.insert(vPayload.end(), payload.begin(), payload.end());

  // Compute 4-byte checksum = first 4 bytes of double-SHA256
  std::vector<uint8_t> checksum = doubleSha256(vPayload);
  vPayload.push_back(checksum[0]);
  vPayload.push_back(checksum[1]);
  vPayload.push_back(checksum[2]);
  vPayload.push_back(checksum[3]);

  // Count leading zero bytes (they become '1' characters)
  size_t leadingZeros = 0;
  for (size_t i = 0; i < vPayload.size() && vPayload[i] == 0; ++i) {
    ++leadingZeros;
  }

  // Base58 encoding: repeatedly divide by 58
  // Work with a copy of the data as big-endian integer
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

  // Add '1' characters for leading zero bytes
  for (size_t i = 0; i < leadingZeros; ++i) {
    encoded.push_back('1');
  }

  // The result is in reverse order
  std::reverse(encoded.begin(), encoded.end());
  return encoded;
}

bool BchHtlcScript::base58CheckDecode(const std::string& encoded, uint8_t& version,
                                       std::vector<uint8_t>& payload) {
  if (encoded.empty()) return false;

  // Build reverse lookup table
  int8_t b58map[256];
  std::memset(b58map, -1, sizeof(b58map));
  for (int i = 0; i < 58; ++i) {
    b58map[static_cast<uint8_t>(kBase58Alphabet[i])] = static_cast<int8_t>(i);
  }

  // Count leading '1' chars (they represent 0x00 bytes)
  size_t leadingOnes = 0;
  for (size_t i = 0; i < encoded.size() && encoded[i] == '1'; ++i) {
    ++leadingOnes;
  }

  // Decode base58 to big-endian bytes
  // Allocate enough space (encoded.size() * 733 / 1000 + 1 is sufficient)
  size_t maxBytes = encoded.size() * 733 / 1000 + 1;
  std::vector<uint8_t> b256(maxBytes, 0);

  for (size_t i = 0; i < encoded.size(); ++i) {
    int8_t carry = b58map[static_cast<uint8_t>(encoded[i])];
    if (carry < 0) return false;  // invalid character

    uint32_t c = static_cast<uint32_t>(carry);
    for (auto it = b256.rbegin(); it != b256.rend(); ++it) {
      c += 58u * static_cast<uint32_t>(*it);
      *it = static_cast<uint8_t>(c & 0xFF);
      c >>= 8;
    }
  }

  // Find the first non-zero byte
  auto it = std::find_if(b256.begin(), b256.end(), [](uint8_t b) { return b != 0; });

  // Build result: leading zeros + decoded data
  std::vector<uint8_t> result;
  result.reserve(leadingOnes + static_cast<size_t>(std::distance(it, b256.end())));
  for (size_t i = 0; i < leadingOnes; ++i) {
    result.push_back(0x00);
  }
  result.insert(result.end(), it, b256.end());

  // Must have at least 5 bytes (1 version + 0 payload + 4 checksum minimum)
  if (result.size() < 5) return false;

  // Verify checksum (last 4 bytes)
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
// Address decode helper
// =============================================================================

bool BchHtlcScript::decodeAddress(const std::string& address, uint8_t& version,
                                   std::vector<uint8_t>& hash) {
  if (!base58CheckDecode(address, version, hash)) {
    return false;
  }
  // P2PKH and P2SH hashes are always 20 bytes
  return hash.size() == 20;
}

// =============================================================================
// ScriptPubKey builders
// =============================================================================

std::vector<uint8_t> BchHtlcScript::buildP2pkhScriptPubKey(const std::vector<uint8_t>& pubKeyHash) {
  // OP_DUP OP_HASH160 <20-byte-hash> OP_EQUALVERIFY OP_CHECKSIG
  std::vector<uint8_t> script;
  script.reserve(25);
  script.push_back(OpCode::OP_DUP);        // 0x76
  script.push_back(OpCode::OP_HASH160);     // 0xA9
  script.push_back(0x14);                   // push 20 bytes
  script.insert(script.end(), pubKeyHash.begin(), pubKeyHash.end());
  script.push_back(OpCode::OP_EQUALVERIFY); // 0x88
  script.push_back(OpCode::OP_CHECKSIG);    // 0xAC
  return script;
}

std::vector<uint8_t> BchHtlcScript::buildP2shScriptPubKey(const std::vector<uint8_t>& scriptHash) {
  // OP_HASH160 <20-byte-hash> OP_EQUAL
  std::vector<uint8_t> script;
  script.reserve(23);
  script.push_back(OpCode::OP_HASH160);  // 0xA9
  script.push_back(0x14);                // push 20 bytes
  script.insert(script.end(), scriptHash.begin(), scriptHash.end());
  script.push_back(OpCode::OP_EQUAL);    // 0x87
  return script;
}

// =============================================================================
// HTLC Redeem Script construction
// =============================================================================

std::vector<uint8_t> BchHtlcScript::createRedeemScript(
    const std::vector<uint8_t>& hashLockSha256,
    const std::vector<uint8_t>& recipientPubKey,
    const std::vector<uint8_t>& senderPubKey,
    uint32_t timeoutBlock) {

  if (hashLockSha256.size() != 32) {
    throw std::runtime_error("createRedeemScript: hashLock must be 32 bytes (SHA256)");
  }
  if (recipientPubKey.size() != 33) {
    throw std::runtime_error("createRedeemScript: recipientPubKey must be 33 bytes (compressed)");
  }
  if (senderPubKey.size() != 33) {
    throw std::runtime_error("createRedeemScript: senderPubKey must be 33 bytes (compressed)");
  }

  //
  // Script structure:
  //   OP_IF
  //     OP_SHA256 <32: hashLock> OP_EQUALVERIFY <33: recipientPubKey> OP_CHECKSIG
  //   OP_ELSE
  //     <N: timeoutBlock> OP_CHECKLOCKTIMEVERIFY OP_DROP <33: senderPubKey> OP_CHECKSIG
  //   OP_ENDIF
  //
  std::vector<uint8_t> script;
  script.reserve(1 + 1 + 1 + 32 + 1 + 1 + 33 + 1 + 1 + 5 + 1 + 1 + 1 + 33 + 1 + 1);

  // OP_IF
  script.push_back(OpCode::OP_IF);

  // OP_SHA256 (single SHA256 — consistent with other chains in this protocol)
  script.push_back(OpCode::OP_SHA256);

  // <hashLock> (32 bytes, direct push)
  pushData(script, hashLockSha256);

  // OP_EQUALVERIFY
  script.push_back(OpCode::OP_EQUALVERIFY);

  // <recipientPubKey> (33 bytes, direct push)
  pushData(script, recipientPubKey);

  // OP_CHECKSIG
  script.push_back(OpCode::OP_CHECKSIG);

  // OP_ELSE
  script.push_back(OpCode::OP_ELSE);

  // <timeoutBlock> (CScriptNum encoding)
  std::vector<uint8_t> lockTimeBytes = serializeScriptNum(timeoutBlock);
  pushData(script, lockTimeBytes);

  // OP_CHECKLOCKTIMEVERIFY
  script.push_back(OpCode::OP_CHECKLOCKTIMEVERIFY);

  // OP_DROP (remove the lock time value from the stack)
  script.push_back(OpCode::OP_DROP);

  // <senderPubKey> (33 bytes, direct push)
  pushData(script, senderPubKey);

  // OP_CHECKSIG
  script.push_back(OpCode::OP_CHECKSIG);

  // OP_ENDIF
  script.push_back(OpCode::OP_ENDIF);

  return script;
}

// =============================================================================
// P2SH address computation
// =============================================================================

std::string BchHtlcScript::computeP2shAddress(const std::vector<uint8_t>& redeemScript, bool testnet) {
  std::vector<uint8_t> scriptHash = hash160(redeemScript);
  uint8_t version = testnet ? 0xC4 : 0x05;
  return base58CheckEncode(version, scriptHash);
}

// =============================================================================
// ScriptSig construction for claiming and refunding
// =============================================================================

std::vector<uint8_t> BchHtlcScript::createClaimScriptSig(
    const std::vector<uint8_t>& signature,
    const std::vector<uint8_t>& preimage,
    const std::vector<uint8_t>& redeemScript) {
  //
  // To claim, the stack must contain (bottom to top):
  //   <signature> <preimage> OP_TRUE <serialized redeemScript>
  //
  // The OP_TRUE selects the OP_IF branch.
  // The serialized redeemScript is pushed as the last element (P2SH requirement).
  //
  std::vector<uint8_t> scriptSig;
  scriptSig.reserve(signature.size() + preimage.size() + redeemScript.size() + 10);

  // <signature> (DER-encoded + sighash byte)
  pushData(scriptSig, signature);

  // <preimage>
  pushData(scriptSig, preimage);

  // OP_TRUE (selects OP_IF branch)
  scriptSig.push_back(OpCode::OP_TRUE);

  // <redeemScript> (serialized, pushed as data)
  pushData(scriptSig, redeemScript);

  return scriptSig;
}

std::vector<uint8_t> BchHtlcScript::createRefundScriptSig(
    const std::vector<uint8_t>& signature,
    const std::vector<uint8_t>& redeemScript) {
  //
  // To refund, the stack must contain (bottom to top):
  //   <signature> OP_FALSE <serialized redeemScript>
  //
  // The OP_FALSE selects the OP_ELSE branch.
  //
  std::vector<uint8_t> scriptSig;
  scriptSig.reserve(signature.size() + redeemScript.size() + 5);

  // <signature> (DER-encoded + sighash byte)
  pushData(scriptSig, signature);

  // OP_FALSE (selects OP_ELSE branch)
  scriptSig.push_back(OpCode::OP_FALSE);

  // <redeemScript> (serialized, pushed as data)
  pushData(scriptSig, redeemScript);

  return scriptSig;
}

// =============================================================================
// Raw transaction builder
// =============================================================================

std::vector<uint8_t> BchHtlcScript::buildRawTransaction(
    const std::string& inputTxid,
    uint32_t inputVout,
    uint64_t inputAmount,
    const std::vector<uint8_t>& scriptSig,
    const std::string& outputAddress,
    uint64_t outputAmount,
    uint32_t nLockTime) {

  (void)inputAmount;  // Used for BIP143 sighash computation, not raw serialization

  // Decode the output address to determine script type
  uint8_t addrVersion = 0;
  std::vector<uint8_t> addrHash;
  if (!decodeAddress(outputAddress, addrVersion, addrHash)) {
    throw std::runtime_error("buildRawTransaction: invalid output address");
  }

  // Build the output scriptPubKey based on address version
  std::vector<uint8_t> scriptPubKey;
  if (addrVersion == 0x00 || addrVersion == 0x6F) {
    // P2PKH (mainnet 0x00, testnet 0x6F)
    scriptPubKey = buildP2pkhScriptPubKey(addrHash);
  } else if (addrVersion == 0x05 || addrVersion == 0xC4) {
    // P2SH (mainnet 0x05, testnet 0xC4)
    scriptPubKey = buildP2shScriptPubKey(addrHash);
  } else {
    throw std::runtime_error("buildRawTransaction: unsupported address version");
  }

  // Reverse the txid hex to get the internal byte order (little-endian)
  std::vector<uint8_t> txidBytes = hexToBytes(inputTxid);
  if (txidBytes.size() != 32) {
    throw std::runtime_error("buildRawTransaction: txid must be 32 bytes (64 hex chars)");
  }
  std::reverse(txidBytes.begin(), txidBytes.end());

  //
  // Serialize the raw transaction (Bitcoin wire format):
  //   version (4 bytes LE)
  //   vin_count (varint)
  //   vin[]:
  //     prev_txid (32 bytes, internal byte order = reversed hex)
  //     prev_vout (4 bytes LE)
  //     scriptSig_len (varint)
  //     scriptSig (variable)
  //     sequence (4 bytes LE)
  //   vout_count (varint)
  //   vout[]:
  //     value (8 bytes LE)
  //     scriptPubKey_len (varint)
  //     scriptPubKey (variable)
  //   locktime (4 bytes LE)
  //
  std::vector<uint8_t> tx;
  tx.reserve(4 + 1 + 32 + 4 + scriptSig.size() + 4 + 1 + 8 + scriptPubKey.size() + 4 + 10);

  // Version = 1 (BCH standard)
  writeLE32(tx, 1);

  // Number of inputs = 1
  writeVarInt(tx, 1);

  // Input: prev txid (32 bytes, reversed)
  tx.insert(tx.end(), txidBytes.begin(), txidBytes.end());

  // Input: prev vout
  writeLE32(tx, inputVout);

  // Input: scriptSig
  writeVarInt(tx, scriptSig.size());
  tx.insert(tx.end(), scriptSig.begin(), scriptSig.end());

  // Input: sequence
  // For refund (CLTV): must be < 0xFFFFFFFF so CHECKLOCKTIMEVERIFY is not bypassed
  // For claim: 0xFFFFFFFF is fine (no timelock constraint)
  uint32_t nSequence = (nLockTime > 0) ? 0xFFFFFFFE : 0xFFFFFFFF;
  writeLE32(tx, nSequence);

  // Number of outputs = 1
  writeVarInt(tx, 1);

  // Output: value
  writeLE64(tx, outputAmount);

  // Output: scriptPubKey
  writeVarInt(tx, scriptPubKey.size());
  tx.insert(tx.end(), scriptPubKey.begin(), scriptPubKey.end());

  // Locktime
  writeLE32(tx, nLockTime);

  return tx;
}

} // namespace XfgSwap
