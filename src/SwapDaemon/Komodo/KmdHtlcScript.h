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

// Komodo HTLC script helpers.
//
// KMD is a Zcash/Bitcoin fork — standard UTXO model with Bitcoin-like scripts.
// Same HTLC redeem script as BTC/BCH (OP_SHA256 hash lock + OP_CHECKLOCKTIMEVERIFY refund).
// KMD-specific address prefixes: P2PKH (0x3C), P2SH (0x55), WIF (0xBC).
// Electrum protocol works (KMD uses ElectrumX-compatible servers).
// nValue field in raw tx outputs is 8 bytes LE (same as BTC/BCH).
class KmdHtlcScript {
public:
  // Create the HTLC redeem script (identical structure to BCH).
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
      const std::vector<uint8_t>& hashLockSha256,     // 32 bytes: SHA256(preimage)
      uint32_t lockTime,
      const std::vector<uint8_t>& recipientPubKey,     // 33 bytes: compressed public key
      const std::vector<uint8_t>& senderPubKey,        // 33 bytes: compressed public key
      uint32_t timeoutBlock);

  // Convert a redeem script to its P2SH scriptPubKey.
  // Equivalent to: buildP2shScriptPubKey(hash160(redeemScript))
  static std::vector<uint8_t> redeemScriptToP2shScriptPubKey(const std::vector<uint8_t>& redeemScript);

  // P2PKH address from pubkey hash (version byte 0x3C for KMD mainnet)
  static std::string pubkeyHashToAddress(const std::vector<uint8_t>& pubkeyHash);

  // P2SH address from script hash (version byte 0x55 for KMD mainnet)
  static std::string scriptHashToAddress(const std::vector<uint8_t>& scriptHash);

  // Extract the claim preimage from a raw spending transaction.
  //
  // The claim scriptSig for KMD P2SH HTLC is the same as BCH:
  //   <signature> <preimage> OP_TRUE <redeemScript>
  //
  // Returns empty vector if no matching input is found.
  static std::vector<uint8_t> parseClaimPreimage(
      const std::vector<uint8_t>& rawTx,
      const std::vector<uint8_t>& htlcP2shScriptPubKey);

  // Helper conversions
  static std::vector<uint8_t> hexToBytes(const std::string& hex);
  static std::string bytesToHex(const std::vector<uint8_t>& bytes);

  // Cryptographic hash helpers (Bitcoin standard, NOT keccak)
  static std::vector<uint8_t> sha256(const std::vector<uint8_t>& data);
  static std::vector<uint8_t> doubleSha256(const std::vector<uint8_t>& data);
  static std::vector<uint8_t> ripemd160(const std::vector<uint8_t>& data);
  static std::vector<uint8_t> hash160(const std::vector<uint8_t>& data);

  // Base58Check encode (version byte + payload + 4-byte checksum)
  static std::string base58CheckEncode(uint8_t version, const std::vector<uint8_t>& payload);

  // Build a P2PKH scriptPubKey: OP_DUP OP_HASH160 <hash> OP_EQUALVERIFY OP_CHECKSIG
  static std::vector<uint8_t> buildP2pkhScriptPubKey(const std::vector<uint8_t>& pubKeyHash);

  // Build a P2SH scriptPubKey: OP_HASH160 <hash> OP_EQUAL
  static std::vector<uint8_t> buildP2shScriptPubKey(const std::vector<uint8_t>& scriptHash);

private:
  static void pushData(std::vector<uint8_t>& script, const std::vector<uint8_t>& data);
  static void writeLE16(std::vector<uint8_t>& out, uint16_t v);
  static void writeLE32(std::vector<uint8_t>& out, uint32_t v);
  static void writeVarInt(std::vector<uint8_t>& out, uint64_t n);
  static std::vector<uint8_t> serializeScriptNum(uint32_t n);
};

} // namespace XfgSwap
