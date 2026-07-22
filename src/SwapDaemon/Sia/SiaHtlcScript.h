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

class SiaHtlcScript {
public:
  // Create the HTLC unlock conditions for Sia.
  //
  // Sia uses a different scripting model than Bitcoin:
  // - No Bitcoin Script opcodes
  // Instead, unlock conditions specify:
  //   - PublicKey (ed25519 pubkey, 32 bytes)
  //   - Timelock (block height after which refund is allowed)
  //   - RequiredSignatures (1 for single-sig)
  //
  // The HTLC is implemented via Sia's file contract mechanism:
  // - Lock: Create a file contract with the HTLC hash as the valid proof
  // - Claim: Submit a proof (preimage) that matches the hash
  // - Refund: After timelock, the locked funds return to sender
  //
  // For UTXO-based HTLCs (post-v2 hardfork):
  // - OP_SHA256 + OP_CHECKLOCKTIMEVERIFY are now available
  // - This allows standard Bitcoin-style HTLC scripts

  // Create the HTLC redeem script using Sia v2 opcodes.
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
      const std::vector<uint8_t>& recipientPubKey,     // 32 bytes: ed25519 public key
      const std::vector<uint8_t>& senderPubKey,        // 32 bytes: ed25519 public key
      uint32_t timeoutBlock);

  // Compute Sia address from ed25519 public key.
  // Sia address = 76-character base64 string starting with "a"
  // The unlock hash is the SHA256 of the public key
  static std::string computeAddress(const std::vector<uint8_t>& pubKey);

  // Decode a Sia address to extract the unlock hash (32 bytes)
  static bool decodeAddress(const std::string& address, std::vector<uint8_t>& unlockHash);

  // Create the claim condition (preimage path)
  static std::vector<uint8_t> createClaimCondition(
      const std::vector<uint8_t>& preimage,
      uint32_t currentBlockHeight);

  // Create the refund condition (timeout path)
  static std::vector<uint8_t> createRefundCondition(
      uint32_t timeoutBlock);

  // Build a raw Sia transaction spending from the HTLC.
  // Sia transactions are different from Bitcoin:
  // - Inputs reference SiacoinOutputs or FileContractOutputs
  // - Outputs can be SiacoinOutputs, FileContractOutputs, or SiafundOutputs
  // - No scriptPubKey — addresses are unlock hashes
  static std::vector<uint8_t> buildRawTransaction(
      const std::string& inputTxid,     // UTXO to spend (hex, 64 chars)
      uint32_t inputVout,
      uint64_t inputAmount,              // hastings (1 SC = 10^24 hastings)
      const std::vector<uint8_t>& unlockConditions,
      const std::string& outputAddress,  // destination Sia address
      uint64_t outputAmount,             // hastings (input - fee)
      uint32_t nLockTime);

  // Helper: SHA256 (standard NIST, NOT keccak)
  static std::vector<uint8_t> sha256(const std::vector<uint8_t>& data);

  // Helper: hex string to bytes and back
  static std::vector<uint8_t> hexToBytes(const std::string& hex);
  static std::string bytesToHex(const std::vector<uint8_t>& bytes);

  // Helper: Base64 encode/decode (Sia uses base64 for addresses)
  static std::string base64Encode(const std::vector<uint8_t>& data);
  static std::vector<uint8_t> base64Decode(const std::string& encoded);

  // Helper: compute unlock hash from public key (SHA256)
  static std::vector<uint8_t> computeUnlockHash(const std::vector<uint8_t>& pubKey);

  // Helper: serialize uint32 as Sia encoding (big-endian for some fields)
  static void writeBE32(std::vector<uint8_t>& out, uint32_t v);

  // Helper: serialize uint64 as big-endian
  static void writeBE64(std::vector<uint8_t>& out, uint64_t v);

private:
  // Push data onto script with correct length prefix
  static void pushData(std::vector<uint8_t>& script, const std::vector<uint8_t>& data);

  // Write uint16/uint32/uint64 as little-endian bytes
  static void writeLE16(std::vector<uint8_t>& out, uint16_t v);
  static void writeLE32(std::vector<uint8_t>& out, uint32_t v);
  static void writeLE64(std::vector<uint8_t>& out, uint64_t v);

  // Serialize a Bitcoin varint (CompactSize)
  static void writeVarInt(std::vector<uint8_t>& out, uint64_t n);

  // Serialize a CScriptNum for lock time values
  static std::vector<uint8_t> serializeScriptNum(uint32_t n);
};

// Sia-specific constants
namespace SiaConstants {
  // Sia address prefix (mainnet)
  constexpr uint8_t SIA_ADDRESS_PREFIX = 0x00;

  // Sia v2 hardfork opcodes (same as Bitcoin)
  constexpr uint8_t OP_FALSE     = 0x00;
  constexpr uint8_t OP_TRUE      = 0x51;  // OP_1
  constexpr uint8_t OP_IF        = 0x63;
  constexpr uint8_t OP_ELSE      = 0x67;
  constexpr uint8_t OP_ENDIF     = 0x68;
  constexpr uint8_t OP_DROP      = 0x75;
  constexpr uint8_t OP_EQUAL     = 0x87;
  constexpr uint8_t OP_EQUALVERIFY = 0x88;
  constexpr uint8_t OP_SHA256    = 0xA8;  // single SHA256
  constexpr uint8_t OP_CHECKSIG  = 0xAC;
  constexpr uint8_t OP_CHECKLOCKTIMEVERIFY = 0xB1;
  constexpr uint8_t OP_PUSHDATA1 = 0x4C;

  // Sia-specific opcodes (for file contracts)
  constexpr uint8_t OP_FILE_CONTRACT_REVISION = 0xF0;
  constexpr uint8_t OP_STORAGE_PROOF = 0xF1;
  constexpr uint8_t OP_FILE_CONTRACT_VALIDITY = 0xF2;
}

} // namespace XfgSwap
