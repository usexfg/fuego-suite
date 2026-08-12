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
#include <string>
#include <cstdint>
#include <vector>

namespace XfgSwap {

// Zcash HTLC script (P2SH, transparent t-addrs, Zcash v4 tx format).
//
// Zcash is a Bitcoin 1.0-era fork (pre-SegWit) with its own transaction
// serialization: version 4 transactions carry a version group id (0x03C48270)
// plus nExpiryHeight, valueBalance, and empty shielded spend/output/joinsplit
// sections. Transparent inputs are signed with the legacy (pre-SegWit) sighash
// over the v4 serialized preimage. Transparent address prefixes are TWO bytes:
//   P2PKH (t1...):  0x1CB8 (mainnet), 0x1D25 (testnet)
//   P2SH  (t3...):  0x1CBD (mainnet), 0x1CBA (testnet)
//   WIF   version:  0x80 (mainnet),   0xEF (testnet)
class ZecHtlcScript {
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
  // Zcash P2SH (t3...) address = Base58Check(0x1C 0xBD || RIPEMD160(SHA256(redeemScript)))
  // For Zcash testnet: prefix 0x1C 0xBA
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

  // Build a raw Zcash transaction (v4 format) spending from the HTLC P2SH address.
  // Serializes nVersion=4, nVersionGroupId=0x03C48270, transparent inputs/outputs,
  // nLockTime, nExpiryHeight=0, valueBalance=0, and empty shielded sections.
  static std::vector<uint8_t> buildRawTransaction(
      const std::string& inputTxid,     // UTXO to spend (hex, 64 chars)
      uint32_t inputVout,
      uint64_t inputAmount,              // zatoshis (ZEC 1e8)
      const std::vector<uint8_t>& scriptSig,
      const std::string& outputAddress,  // destination P2PKH or P2SH address
      uint64_t outputAmount,             // zatoshis (input - fee)
      uint32_t nLockTime);               // 0 for claim, timeoutBlock for refund

  // Helper: compute HASH160 (RIPEMD160(SHA256(data)))
  static std::vector<uint8_t> hash160(const std::vector<uint8_t>& data);

  // Helper: SHA256 (standard NIST, NOT keccak)
  static std::vector<uint8_t> sha256(const std::vector<uint8_t>& data);

  // Helper: double-SHA256 (SHA256(SHA256(data))), used for txid and checksum
  static std::vector<uint8_t> doubleSha256(const std::vector<uint8_t>& data);

  // Helper: RIPEMD160
  static std::vector<uint8_t> ripemd160(const std::vector<uint8_t>& data);

  // Helper: Base58Check encode (version bytes + payload + 4-byte checksum).
  // Zcash transparent prefixes are 2 bytes, so version is a byte vector.
  static std::string base58CheckEncode(const std::vector<uint8_t>& version,
                                       const std::vector<uint8_t>& payload);

  // Helper: Base58Check decode (returns false if checksum invalid).
  // Returns the complete decoded payload (version prefix + body, checksum
  // stripped) in `payload`. Callers split the 1-2 byte version prefix from the
  // body based on context (WIF vs transparent address).
  static bool base58CheckDecode(const std::string& encoded, std::vector<uint8_t>& version,
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

  // Convert a redeem script to its P2SH scriptPubKey.
  static std::vector<uint8_t> redeemScriptToP2shScriptPubKey(const std::vector<uint8_t>& redeemScript);

  // Extract the claim preimage from a raw spending transaction.
  // Returns empty vector if no matching input is found.
  static std::vector<uint8_t> parseClaimPreimage(
      const std::vector<uint8_t>& rawTx,
      const std::vector<uint8_t>& htlcP2shScriptPubKey);

  // WIF to private key (32 bytes). Zcash WIF version: 0x80 (mainnet)
  static bool wifToPrivKey(const std::string& wif,
                           std::array<uint8_t, 32>& privKey);

  // Sign a Zcash P2SH input using the Zcash v4 transparent sighash
  // (SIGHASH_ALL=0x01, signature hash over the v4 serialized preimage).
  // Returns DER-encoded signature with 0x01 sighash byte appended.
  static std::vector<uint8_t> signInput(
      const std::array<uint8_t, 32>& privKey,
      uint32_t txVersion,
      uint32_t nLocktime,
      uint32_t nSequence,
      const std::string& htlcTxid,
      uint32_t htlcVout,
      const std::vector<uint8_t>& redeemScript,
      uint64_t htlcAmount,
      const std::vector<uint8_t>& outputScript,
      uint64_t outputAmount);

private:
  // Push data onto script with correct length prefix
  static void pushData(std::vector<uint8_t>& script, const std::vector<uint8_t>& data);

  // Write uint16/uint32/uint64 as little-endian bytes
  static void writeLE16(std::vector<uint8_t>& out, uint16_t v);
  static void writeLE32(std::vector<uint8_t>& out, uint32_t v);
  static void writeLE64(std::vector<uint8_t>& out, uint64_t v);

  // Decode a Base58Check address to extract the hash (20 bytes) and version
  // prefix bytes (2 bytes for Zcash transparent addresses).
  static bool decodeAddress(const std::string& address, std::vector<uint8_t>& version,
                            std::vector<uint8_t>& hash);

  // Serialize a Bitcoin varint (CompactSize)
  static void writeVarInt(std::vector<uint8_t>& out, uint64_t n);
};

// Bitcoin Script opcodes used in HTLC
namespace ZecOpCode {
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
} // namespace ZecOpCode

} // namespace XfgSwap
