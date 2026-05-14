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

#pragma once
#include <string>
#include <cstdint>
#include <vector>

namespace XfgSwap {

class BchHtlcScript {
public:
  // Create the HTLC redeem script.
  //
  // Claim path: provide preimage where SHA256(preimage) == hashLock, sign with recipientPubKey
  // Refund path: after timeoutBlock, sign with senderPubKey
  //
  // Script:
  //   OP_IF
  //     OP_SHA256 <hash_lock_sha256> OP_EQUALVERIFY <recipient_pubkey> OP_CHECKSIG
  //   OP_ELSE
  //     <timeout_block> OP_CHECKLOCKTIMEVERIFY OP_DROP <sender_pubkey> OP_CHECKSIG
  //   OP_ENDIF
  static std::vector<uint8_t> createRedeemScript(
      const std::vector<uint8_t>& hashLockSha256,     // 32 bytes: SHA256(preimage)
      const std::vector<uint8_t>& recipientPubKey,     // 33 bytes: compressed public key
      const std::vector<uint8_t>& senderPubKey,        // 33 bytes: compressed public key
      uint32_t timeoutBlock);

  // Compute P2SH address from redeem script.
  // P2SH address = Base58Check(0x05 || RIPEMD160(SHA256(redeemScript)))
  // For BCH testnet: prefix 0xC4
  static std::string computeP2shAddress(const std::vector<uint8_t>& redeemScript, bool testnet = false);

  // Create the scriptSig for CLAIMING (preimage path).
  // scriptSig: <signature> <preimage> OP_TRUE <redeemScript>
  static std::vector<uint8_t> createClaimScriptSig(
      const std::vector<uint8_t>& signature,
      const std::vector<uint8_t>& preimage,
      const std::vector<uint8_t>& redeemScript);

  // Create the scriptSig for REFUNDING (timeout path).
  // scriptSig: <signature> OP_FALSE <redeemScript>
  static std::vector<uint8_t> createRefundScriptSig(
      const std::vector<uint8_t>& signature,
      const std::vector<uint8_t>& redeemScript);

  // Build a raw BCH transaction spending from the HTLC P2SH address.
  // Creates a version 1 transaction with nLockTime set appropriately.
  // Note: For refund transactions, nSequence must be < 0xFFFFFFFF for CLTV to work.
  // BCH uses BIP143 sighash with SIGHASH_FORKID (0x40) for replay protection.
  static std::vector<uint8_t> buildRawTransaction(
      const std::string& inputTxid,     // UTXO to spend (hex, 64 chars)
      uint32_t inputVout,
      uint64_t inputAmount,              // satoshis (needed for BCH BIP143 sighash)
      const std::vector<uint8_t>& scriptSig,
      const std::string& outputAddress,  // destination P2PKH or P2SH address
      uint64_t outputAmount,             // satoshis (input - fee)
      uint32_t nLockTime);               // 0 for claim, timeoutBlock for refund

  // Helper: compute HASH160 (RIPEMD160(SHA256(data)))
  // Note: This is NOT the same as Fuego's cn_fast_hash (keccak).
  // BCH uses standard Bitcoin hash functions.
  static std::vector<uint8_t> hash160(const std::vector<uint8_t>& data);

  // Helper: SHA256 (standard NIST, NOT keccak)
  static std::vector<uint8_t> sha256(const std::vector<uint8_t>& data);

  // Helper: double-SHA256 (SHA256(SHA256(data))), used for txid and checksum
  static std::vector<uint8_t> doubleSha256(const std::vector<uint8_t>& data);

  // Helper: RIPEMD160
  static std::vector<uint8_t> ripemd160(const std::vector<uint8_t>& data);

  // Helper: Base58Check encode (version byte + payload + 4-byte checksum)
  static std::string base58CheckEncode(uint8_t version, const std::vector<uint8_t>& payload);

  // Helper: Base58Check decode (returns false if checksum invalid)
  static bool base58CheckDecode(const std::string& encoded, uint8_t& version,
                                std::vector<uint8_t>& payload);

  // Helper: serialize uint32 as Bitcoin CScriptNum encoding for lock times
  static std::vector<uint8_t> serializeScriptNum(uint32_t n);

  // Helper: hex string to bytes and back
  static std::vector<uint8_t> hexToBytes(const std::string& hex);
  static std::string bytesToHex(const std::vector<uint8_t>& bytes);

  // Build a P2PKH scriptPubKey: OP_DUP OP_HASH160 <hash> OP_EQUALVERIFY OP_CHECKSIG
  static std::vector<uint8_t> buildP2pkhScriptPubKey(const std::vector<uint8_t>& pubKeyHash);

  // Build a P2SH scriptPubKey: OP_HASH160 <hash> OP_EQUAL
  static std::vector<uint8_t> buildP2shScriptPubKey(const std::vector<uint8_t>& scriptHash);

private:
  // Push data onto script with correct length prefix
  static void pushData(std::vector<uint8_t>& script, const std::vector<uint8_t>& data);

  // Write uint16/uint32/uint64 as little-endian bytes
  static void writeLE16(std::vector<uint8_t>& out, uint16_t v);
  static void writeLE32(std::vector<uint8_t>& out, uint32_t v);
  static void writeLE64(std::vector<uint8_t>& out, uint64_t v);

  // Decode a Base58Check address to extract the hash (20 bytes) and version byte
  static bool decodeAddress(const std::string& address, uint8_t& version,
                            std::vector<uint8_t>& hash);

  // Serialize a Bitcoin varint (CompactSize)
  static void writeVarInt(std::vector<uint8_t>& out, uint64_t n);
};

// Bitcoin Script opcodes used in HTLC
namespace OpCode {
  constexpr uint8_t OP_FALSE     = 0x00;
  constexpr uint8_t OP_TRUE      = 0x51;  // OP_1
  constexpr uint8_t OP_IF        = 0x63;
  constexpr uint8_t OP_ELSE      = 0x67;
  constexpr uint8_t OP_ENDIF     = 0x68;
  constexpr uint8_t OP_DROP      = 0x75;
  constexpr uint8_t OP_DUP       = 0x76;
  constexpr uint8_t OP_EQUAL     = 0x87;
  constexpr uint8_t OP_EQUALVERIFY = 0x88;
  constexpr uint8_t OP_SHA256    = 0xA8;  // single SHA256 — used in HTLC hash lock
  constexpr uint8_t OP_HASH160   = 0xA9;  // RIPEMD160(SHA256) — used in P2PKH/P2SH only
  constexpr uint8_t OP_CHECKSIG  = 0xAC;
  constexpr uint8_t OP_CHECKLOCKTIMEVERIFY = 0xB1;
  constexpr uint8_t OP_PUSHDATA1 = 0x4C;
} // namespace OpCode

} // namespace XfgSwap
