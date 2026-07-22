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

#include "SiaHtlcScript.h"
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

namespace XfgSwap {

// =============================================================================
// Cryptographic hash helpers
// =============================================================================

std::vector<uint8_t> SiaHtlcScript::sha256(const std::vector<uint8_t>& data) {
  std::vector<uint8_t> digest(SHA256_DIGEST_LENGTH);
  SHA256(data.data(), data.size(), digest.data());
  return digest;
}

// =============================================================================
// Hex conversion
// =============================================================================

std::vector<uint8_t> SiaHtlcScript::hexToBytes(const std::string& hex) {
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

std::string SiaHtlcScript::bytesToHex(const std::vector<uint8_t>& bytes) {
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
// Base64 encode/decode (Sia uses base64 for addresses)
// =============================================================================

std::string SiaHtlcScript::base64Encode(const std::vector<uint8_t>& data) {
  BIO* bio = BIO_new(BIO_f_base64());
  BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
  BIO* bmem = BIO_new(BIO_s_mem());
  bio = BIO_push(bio, bmem);
  BIO_write(bio, data.data(), static_cast<int>(data.size()));
  BIO_flush(bio);

  BUF_MEM* bptr = nullptr;
  BIO_get_mem_ptr(bio, &bptr);
  std::string result(bptr->data, bptr->length);
  BIO_free_all(bio);
  return result;
}

std::vector<uint8_t> SiaHtlcScript::base64Decode(const std::string& encoded) {
  BIO* bio = BIO_new_mem_buf(encoded.data(), static_cast<int>(encoded.size()));
  BIO* b64 = BIO_new(BIO_f_base64());
  bio = BIO_push(b64, bio);
  BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);

  std::vector<uint8_t> result(encoded.size());
  int len = BIO_read(bio, result.data(), static_cast<int>(result.size()));
  BIO_free_all(bio);

  if (len < 0) {
    throw std::runtime_error("base64Decode: invalid base64 data");
  }
  result.resize(static_cast<size_t>(len));
  return result;
}

// =============================================================================
// Little-endian writers
// =============================================================================

void SiaHtlcScript::writeLE16(std::vector<uint8_t>& out, uint16_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void SiaHtlcScript::writeLE32(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void SiaHtlcScript::writeLE64(std::vector<uint8_t>& out, uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
  }
}

void SiaHtlcScript::writeBE32(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>(v & 0xFF));
}

void SiaHtlcScript::writeBE64(std::vector<uint8_t>& out, uint64_t v) {
  for (int i = 7; i >= 0; --i) {
    out.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
  }
}

// =============================================================================
// CompactSize (varint) encoding
// =============================================================================

void SiaHtlcScript::writeVarInt(std::vector<uint8_t>& out, uint64_t n) {
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

void SiaHtlcScript::pushData(std::vector<uint8_t>& script, const std::vector<uint8_t>& data) {
  size_t len = data.size();
  if (len == 0) {
    script.push_back(SiaConstants::OP_FALSE);  // OP_0 pushes empty byte vector
  } else if (len <= 75) {
    // Direct push: single byte length prefix
    script.push_back(static_cast<uint8_t>(len));
    script.insert(script.end(), data.begin(), data.end());
  } else if (len <= 255) {
    // OP_PUSHDATA1 <1-byte-length> <data>
    script.push_back(SiaConstants::OP_PUSHDATA1);
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
// Unlock hash computation (Sia address derivation)
// =============================================================================

std::vector<uint8_t> SiaHtlcScript::computeUnlockHash(const std::vector<uint8_t>& pubKey) {
  // Sia unlock hash = SHA256(pubKey)
  return sha256(pubKey);
}

std::string SiaHtlcScript::computeAddress(const std::vector<uint8_t>& pubKey) {
  std::vector<uint8_t> unlockHash = computeUnlockHash(pubKey);

  // Sia address format: base64(0x00 || unlockHash)
  // The 0x00 is the version byte (mainnet)
  std::vector<uint8_t> addressData;
  addressData.reserve(1 + unlockHash.size());
  addressData.push_back(SiaConstants::SIA_ADDRESS_PREFIX);
  addressData.insert(addressData.end(), unlockHash.begin(), unlockHash.end());

  return base64Encode(addressData);
}

bool SiaHtlcScript::decodeAddress(const std::string& address, std::vector<uint8_t>& unlockHash) {
  try {
    std::vector<uint8_t> decoded = base64Decode(address);

    // Must be at least 33 bytes (1 version + 32 unlock hash)
    if (decoded.size() < 33) {
      return false;
    }

    // Check version byte
    if (decoded[0] != SiaConstants::SIA_ADDRESS_PREFIX) {
      return false;
    }

    // Extract unlock hash (32 bytes)
    unlockHash.assign(decoded.begin() + 1, decoded.begin() + 33);
    return true;
  } catch (...) {
    return false;
  }
}

// =============================================================================
// HTLC Redeem Script construction (using Sia v2 opcodes)
// =============================================================================

std::vector<uint8_t> SiaHtlcScript::createRedeemScript(
    const std::vector<uint8_t>& hashLockSha256,
    const std::vector<uint8_t>& recipientPubKey,
    const std::vector<uint8_t>& senderPubKey,
    uint32_t timeoutBlock) {

  if (hashLockSha256.size() != 32) {
    throw std::runtime_error("createRedeemScript: hashLock must be 32 bytes (SHA256)");
  }
  if (recipientPubKey.size() != 32) {
    throw std::runtime_error("createRedeemScript: recipientPubKey must be 32 bytes (ed25519)");
  }
  if (senderPubKey.size() != 32) {
    throw std::runtime_error("createRedeemScript: senderPubKey must be 32 bytes (ed25519)");
  }

  //
  // Script structure (using Sia v2 opcodes):
  //   OP_IF
  //     OP_SHA256 <32: hashLock> OP_EQUALVERIFY <32: recipientPubKey> OP_CHECKSIG
  //   OP_ELSE
  //     <N: timeoutBlock> OP_CHECKLOCKTIMEVERIFY OP_DROP <32: senderPubKey> OP_CHECKSIG
  //   OP_ENDIF
  //
  std::vector<uint8_t> script;
  script.reserve(1 + 1 + 1 + 32 + 1 + 1 + 32 + 1 + 1 + 5 + 1 + 1 + 1 + 32 + 1 + 1);

  // OP_IF
  script.push_back(SiaConstants::OP_IF);

  // OP_SHA256 (single SHA256 — consistent with other chains in this protocol)
  script.push_back(SiaConstants::OP_SHA256);

  // <hashLock> (32 bytes, direct push)
  pushData(script, hashLockSha256);

  // OP_EQUALVERIFY
  script.push_back(SiaConstants::OP_EQUALVERIFY);

  // <recipientPubKey> (32 bytes, direct push)
  pushData(script, recipientPubKey);

  // OP_CHECKSIG
  script.push_back(SiaConstants::OP_CHECKSIG);

  // OP_ELSE
  script.push_back(SiaConstants::OP_ELSE);

  // <timeoutBlock> (CScriptNum encoding)
  std::vector<uint8_t> lockTimeBytes = serializeScriptNum(timeoutBlock);
  pushData(script, lockTimeBytes);

  // OP_CHECKLOCKTIMEVERIFY
  script.push_back(SiaConstants::OP_CHECKLOCKTIMEVERIFY);

  // OP_DROP (remove the lock time value from the stack)
  script.push_back(SiaConstants::OP_DROP);

  // <senderPubKey> (32 bytes, direct push)
  pushData(script, senderPubKey);

  // OP_CHECKSIG
  script.push_back(SiaConstants::OP_CHECKSIG);

  // OP_ENDIF
  script.push_back(SiaConstants::OP_ENDIF);

  return script;
}

// =============================================================================
// Claim and refund conditions
// =============================================================================

std::vector<uint8_t> SiaHtlcScript::createClaimCondition(
    const std::vector<uint8_t>& preimage,
    uint32_t currentBlockHeight) {

  // For claiming, we need to provide the preimage and sign with recipient key
  // The preimage must satisfy SHA256(preimage) == hashLock
  std::vector<uint8_t> condition;
  condition.reserve(preimage.size() + 4);

  // Push preimage
  pushData(condition, preimage);

  // Push current block height (for CLTV verification)
  std::vector<uint8_t> heightBytes;
  writeLE32(heightBytes, currentBlockHeight);
  pushData(condition, heightBytes);

  return condition;
}

std::vector<uint8_t> SiaHtlcScript::createRefundCondition(uint32_t timeoutBlock) {
  // For refunding, we need to provide the timeout block and sign with sender key
  std::vector<uint8_t> condition;
  condition.reserve(8);

  // Push timeout block (for CLTV verification)
  std::vector<uint8_t> timeoutBytes;
  writeLE32(timeoutBytes, timeoutBlock);
  pushData(condition, timeoutBytes);

  return condition;
}

// =============================================================================
// CScriptNum serialization for lock time values
// =============================================================================

std::vector<uint8_t> SiaHtlcScript::serializeScriptNum(uint32_t n) {
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
// Raw transaction builder (Sia format)
// =============================================================================

std::vector<uint8_t> SiaHtlcScript::buildRawTransaction(
    const std::string& inputTxid,
    uint32_t inputVout,
    uint64_t inputAmount,
    const std::vector<uint8_t>& unlockConditions,
    const std::string& outputAddress,
    uint64_t outputAmount,
    uint32_t nLockTime) {

  // Decode the output address to extract unlock hash
  std::vector<uint8_t> outputUnlockHash;
  if (!decodeAddress(outputAddress, outputUnlockHash)) {
    throw std::runtime_error("buildRawTransaction: invalid Sia address");
  }

  // Reverse the txid hex to get the internal byte order (little-endian)
  std::vector<uint8_t> txidBytes = hexToBytes(inputTxid);
  if (txidBytes.size() != 32) {
    throw std::runtime_error("buildRawTransaction: txid must be 32 bytes (64 hex chars)");
  }
  std::reverse(txidBytes.begin(), txidBytes.end());

  //
  // Sia transaction format (simplified for HTLC):
  //   version (8 bytes LE)
  //   siacoin_input_count (varint)
  //   siacoin_inputs[]:
  //     parent_id (32 bytes)
  //     unlock_conditions:
  //       timelock (4 bytes LE)
  //       public_keys_count (varint)
  //       public_keys[]:
  //         algorithm (2 bytes LE)
  //         key_length (varint)
  //         key (32 bytes)
  //       signatures_required (varint)
  //     input_signature (64 bytes)
  //   siacoin_output_count (varint)
  //   siacoin_outputs[]:
  //     value (16 bytes LE)
  //     unlock_hash (32 bytes)
  //   file_contract_count (varint)
  //   file_contracts[] (empty for HTLC)
  //   file_contract_revisions_count (varint)
  //   file_contract_revisions[] (empty for HTLC)
  //   storage_proofs_count (varint)
  //   storage_proofs[] (empty for HTLC)
  //   siafund_inputs_count (varint)
  //   siafund_inputs[] (empty for HTLC)
  //   siafund_outputs_count (varint)
  //   siafund_outputs[] (empty for HTLC)
  //   miner_fee_count (varint)
  //   miner_fees[] (empty for HTLC)
  //   arbitrary_data_count (varint)
  //   arbitrary_data[] (empty for HTLC)
  //   transaction_signatures_count (varint)
  //   transaction_signatures[] (empty for HTLC — signatures are embedded)
  //
  std::vector<uint8_t> tx;
  tx.reserve(8 + 1 + 32 + 100 + 1 + 33 + 10);

  // Version = 1 (Sia standard)
  writeLE64(tx, 1);

  // Number of siacoin inputs = 1
  writeVarInt(tx, 1);

  // Input: parent id (32 bytes, reversed)
  tx.insert(tx.end(), txidBytes.begin(), txidBytes.end());

  // Unlock conditions
  // Timelock
  writeLE32(tx, nLockTime);

  // Number of public keys = 1
  writeVarInt(tx, 1);

  // Public key: algorithm (2 bytes LE)
  writeLE16(tx, 1);  // Ed25519 algorithm

  // Public key: key length (varint)
  writeVarInt(tx, 32);

  // Public key: key (32 bytes) — placeholder, will be filled by signing
  tx.insert(tx.end(), 32, 0x00);

  // Signatures required
  writeVarInt(tx, 1);

  // Input signature (64 bytes) — placeholder, will be filled by signing
  tx.insert(tx.end(), 64, 0x00);

  // Number of siacoin outputs = 1
  writeVarInt(tx, 1);

  // Output: value (16 bytes LE)
  // Sia uses 128-bit values for hastings
  std::vector<uint8_t> valueBytes(16, 0);
  for (int i = 0; i < 8; ++i) {
    valueBytes[i] = static_cast<uint8_t>((outputAmount >> (i * 8)) & 0xFF);
  }
  tx.insert(tx.end(), valueBytes.begin(), valueBytes.end());

  // Output: unlock hash (32 bytes)
  tx.insert(tx.end(), outputUnlockHash.begin(), outputUnlockHash.end());

  // Empty arrays for other transaction types
  writeVarInt(tx, 0);  // file_contract_count
  writeVarInt(tx, 0);  // file_contract_revisions_count
  writeVarInt(tx, 0);  // storage_proofs_count
  writeVarInt(tx, 0);  // siafund_inputs_count
  writeVarInt(tx, 0);  // siafund_outputs_count
  writeVarInt(tx, 0);  // miner_fee_count
  writeVarInt(tx, 0);  // arbitrary_data_count
  writeVarInt(tx, 0);  // transaction_signatures_count

  return tx;
}

} // namespace XfgSwap
