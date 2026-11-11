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

class BtcHtlcScript {
public:
  // Create the HTLC redeem script (identical structure to BCH, but used inside P2WSH).
  //
  // Script:
  //   OP_IF
  //     OP_SHA256 <hash_lock> OP_EQUALVERIFY <recipient_pubkey> OP_CHECKSIG
  //   OP_ELSE
  //     <timeout_block> OP_CHECKLOCKTIMEVERIFY OP_DROP <sender_pubkey> OP_CHECKSIG
  //   OP_ENDIF
  static std::vector<uint8_t> createHashTimeLockScript(
      const std::vector<uint8_t>& hashLockSha256,
      uint32_t lockTime,
      const std::vector<uint8_t>& recipientPubKey,
      const std::vector<uint8_t>& senderPubKey,
      uint32_t timeoutBlock);

  // Compute P2WSH scriptPubKey: OP_0 <32-byte-SHA256(redeemScript)>
  // Total: 34 bytes
  static std::vector<uint8_t> redeemScriptToP2wshScriptPubKey(
      const std::vector<uint8_t>& redeemScript);

  // Compute witness script hash: SHA256(redeemScript)
  static std::vector<uint8_t> witnessScriptHash(
      const std::vector<uint8_t>& redeemScript);

  // Parse claim preimage from a raw SegWit spending transaction.
  //
  // For P2WSH, the witness stack for a claim is:
  //   [<signature>, <preimage>, <redeemScript>]
  //
  // The node validates SHA256(last_witness_item) == hashInScriptPubKey.
  // This function finds witness stacks whose last item hashes to the
  // expected P2WSH hash, and returns the second-to-last item (preimage).
  static std::vector<uint8_t> parseClaimPreimage(
      const std::vector<uint8_t>& rawTx,
      const std::vector<uint8_t>& p2wshScriptPubKey);

  // Helper conversions
  static std::vector<uint8_t> hexToBytes(const std::string& hex);
  static std::string bytesToHex(const std::vector<uint8_t>& bytes);

  // SHA256 (standard NIST, NOT keccak)
  static std::vector<uint8_t> sha256(const std::vector<uint8_t>& data);

  // Double-SHA256 (for txid checksums)
  static std::vector<uint8_t> doubleSha256(const std::vector<uint8_t>& data);

  // Build a P2PKH scriptPubKey: OP_DUP OP_HASH160 <hash> OP_EQUALVERIFY OP_CHECKSIG
  static std::vector<uint8_t> buildP2pkhScriptPubKey(const std::vector<uint8_t>& pubKeyHash);

  // Compute P2WSH bech32 address from a witness script.
  // hrp: "bc" for BTC mainnet, "ltc" for LTC mainnet, "tb"/"tltc" for testnet.
  static std::string witnessScriptToAddress(const std::vector<uint8_t>& witnessScript,
                                             const std::string& hrp = "bc");

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

  // Base58Check decode (returns false if checksum invalid)
  static bool base58CheckDecode(const std::string& encoded, uint8_t& version,
                                std::vector<uint8_t>& payload);

  // WIF to private key (32 bytes). BTC WIF version: 0x80 (mainnet)
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

private:
  // Base58 decode (no checksum)
  static std::vector<uint8_t> base58Decode(const std::string& s);
  // Push data onto script with correct length prefix
  static void pushData(std::vector<uint8_t>& script, const std::vector<uint8_t>& data);

  // Write little-endian integers
  static void writeLE32(std::vector<uint8_t>& out, uint32_t v);

  // Serialize a Bitcoin CompactSize (varint)
  static void writeVarInt(std::vector<uint8_t>& out, uint64_t n);
};

// Bitcoin Script opcodes used in HTLC
namespace BtcOpCode {
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
} // namespace BtcOpCode

} // namespace XfgSwap
