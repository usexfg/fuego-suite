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
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace XfgSwap {

class LtcHtlcScript {
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
  static std::vector<uint8_t> createHashTimeLockScript(
      const std::vector<uint8_t>& hashLockSha256,
      const std::vector<uint8_t>& recipientPubKey,
      const std::vector<uint8_t>& senderPubKey,
      uint32_t timeoutBlock);

  // Compute P2WSH scriptPubKey from redeem script.
  // P2WSH: OP_0 <32-byte-SHA256(redeemScript)>
  static std::vector<uint8_t> redeemScriptToP2wshScriptPubKey(
      const std::vector<uint8_t>& redeemScript);

  // Compute the SHA256 witness script hash (32 bytes).
  static std::vector<uint8_t> witnessScriptHash(
      const std::vector<uint8_t>& redeemScript);

  // Extract the claim preimage from a raw SegWit spending transaction.
  //
  // For P2WSH spending, the witness stack contains:
  //   <signature> <preimage> OP_TRUE <redeemScript>
  //
  // This function parses the raw tx (must be SegWit format with marker 0x00),
  // finds inputs whose witness last item hashes to the expected witness script
  // hash, and returns the preimage (second witness item).
  //
  // Returns empty vector if no matching input is found.
  static std::vector<uint8_t> parseClaimPreimage(
      const std::vector<uint8_t>& rawTx,
      const std::vector<uint8_t>& p2wshScriptPubKey);

  // Helper: SHA256 (standard NIST, NOT keccak)
  static std::vector<uint8_t> sha256(const std::vector<uint8_t>& data);

  // Helper: double-SHA256 (SHA256(SHA256(data))), used for txid and checksum
  static std::vector<uint8_t> doubleSha256(const std::vector<uint8_t>& data);

  // Helper: hex string to bytes and back
  static std::vector<uint8_t> hexToBytes(const std::string& hex);
  static std::string bytesToHex(const std::vector<uint8_t>& bytes);

  // Build a P2PKH scriptPubKey: OP_DUP OP_HASH160 <hash> OP_EQUALVERIFY OP_CHECKSIG
  static std::vector<uint8_t> buildP2pkhScriptPubKey(const std::vector<uint8_t>& pubKeyHash);

  // Base58Check encode/decode
  static std::string base58CheckEncode(uint8_t version, const std::vector<uint8_t>& payload);
  static bool base58CheckDecode(const std::string& encoded, uint8_t& version,
                                std::vector<uint8_t>& payload);

  // WIF to private key (32 bytes). LTC WIF version: 0xB0 (mainnet)
  static bool wifToPrivKey(const std::string& wif,
                           std::array<uint8_t, 32>& privKey);

  // Sign a P2WSH SegWit input using BIP143 sighash (sighashType=0x01).
  // Returns DER-encoded signature with 0x01 sighash byte appended.
  static std::vector<uint8_t> signInput(
      const std::array<uint8_t, 32>& privKey,
      uint32_t txVersion,
      uint32_t nLocktime,
      uint32_t nSequence,
      const std::string& htlcTxid,
      uint32_t htlcVout,
      const std::vector<uint8_t>& witnessScript,
      uint64_t htlcAmount,
      const std::vector<uint8_t>& outputScript,
      uint64_t outputAmount);

  // Build raw SegWit transaction spending from a P2WSH HTLC.
  static std::vector<uint8_t> buildRawSegWitTx(
      const std::string& inputTxid,
      uint32_t inputVout,
      uint64_t inputAmount,
      const std::vector<uint8_t>& scriptSig,
      const std::vector<std::vector<uint8_t>>& witnessStack,
      const std::string& outputAddress,
      uint64_t outputAmount,
      uint32_t nLockTime);

  // Create witness stack for CLAIMING: <sig> <preimage> OP_1 <witnessScript>
  static std::vector<std::vector<uint8_t>> createClaimWitness(
      const std::vector<uint8_t>& signature,
      const std::vector<uint8_t>& preimage,
      const std::vector<uint8_t>& witnessScript);

  // Create witness stack for REFUNDING: <sig> OP_0 <witnessScript>
  static std::vector<std::vector<uint8_t>> createRefundWitness(
      const std::vector<uint8_t>& signature,
      const std::vector<uint8_t>& witnessScript);

private:
  // Push data onto script with correct length prefix
  static void pushData(std::vector<uint8_t>& script, const std::vector<uint8_t>& data);

  // Write uint16/uint32/uint64 as little-endian bytes
  static void writeLE16(std::vector<uint8_t>& out, uint16_t v);
  static void writeLE32(std::vector<uint8_t>& out, uint32_t v);
  static void writeLE64(std::vector<uint8_t>& out, uint64_t v);

  // Serialize a Bitcoin CScriptNum encoding for lock times
  static std::vector<uint8_t> serializeScriptNum(uint32_t n);

  // Serialize a Bitcoin varint (CompactSize)
  static void writeVarInt(std::vector<uint8_t>& out, uint64_t n);
};

// Bitcoin Script opcodes used in HTLC
namespace LtcOpCode {
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
} // namespace LtcOpCode

} // namespace XfgSwap
