#pragma once

#include <string>
#include <cstdint>
#include <vector>

namespace XfgSwap {

// Decred HTLC script helpers.
//
// DCR uses Bitcoin-like scripts with P2SH HTLCs.
// Address prefixes: P2PKH 0x073F, P2SH 0x071A (mainnet).
// Same HTLC redeem script structure as BTC/BCH/KMD.
class DcrHtlcScript {
public:
  // Create the HTLC redeem script.
  //
  // Script:
  //   OP_IF
  //     OP_SHA256 <hash_lock_sha256> OP_EQUALVERIFY <recipient_pubkey> OP_CHECKSIG
  //   OP_ELSE
  //     <timeout_block> OP_CHECKLOCKTIMEVERIFY OP_DROP <sender_pubkey> OP_CHECKSIG
  //   OP_ENDIF
  static std::vector<uint8_t> createRedeemScript(
      const std::vector<uint8_t>& hashLockSha256,     // 32 bytes
      const std::vector<uint8_t>& recipientPubKey,     // 33 bytes compressed
      const std::vector<uint8_t>& senderPubKey,        // 33 bytes compressed
      uint32_t timeoutBlock);

  // Convert redeem script to P2SH scriptPubKey: OP_HASH160 <20-byte hash> OP_EQUAL
  static std::vector<uint8_t> redeemScriptToP2shScriptPubKey(
      const std::vector<uint8_t>& redeemScript);

  // P2SH address from script hash (version 0x071A for mainnet)
  static std::string scriptHashToAddress(const std::vector<uint8_t>& scriptHash,
                                         bool testnet = false);

  // P2PKH address from pubkey hash (version 0x073F for mainnet)
  static std::string pubkeyHashToAddress(const std::vector<uint8_t>& pubkeyHash,
                                         bool testnet = false);

  // Extract claim preimage from raw spending tx.
  static std::vector<uint8_t> parseClaimPreimage(
      const std::vector<uint8_t>& rawTx,
      const std::vector<uint8_t>& htlcP2shScriptPubKey);

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

  // Build raw DCR transaction.
  static std::vector<uint8_t> buildRawTransaction(
      const std::string& inputTxid,
      uint32_t inputVout,
      uint64_t inputAmount,
      const std::vector<uint8_t>& scriptSig,
      const std::string& outputAddress,
      uint64_t outputAmount,
      uint32_t lockTime);

  // Hash functions (same as Bitcoin)
  static std::vector<uint8_t> sha256(const std::vector<uint8_t>& data);
  static std::vector<uint8_t> hash160(const std::vector<uint8_t>& data);

  // Hex conversion
  static std::vector<uint8_t> hexToBytes(const std::string& hex);
  static std::string bytesToHex(const std::vector<uint8_t>& bytes);

  // Base58Check encode/decode
  static std::string base58CheckEncode(uint8_t version, const std::vector<uint8_t>& payload);
  static bool base58CheckDecode(const std::string& encoded, uint8_t& version,
                                std::vector<uint8_t>& payload);

  // Script opcodes
  static void pushData(std::vector<uint8_t>& script, const std::vector<uint8_t>& data);
  static void writeCompactSize(std::vector<uint8_t>& out, uint64_t n);

private:
  static void writeLE32(std::vector<uint8_t>& out, uint32_t v);
  static void writeLE64(std::vector<uint8_t>& out, uint64_t v);
};

} // namespace XfgSwap
