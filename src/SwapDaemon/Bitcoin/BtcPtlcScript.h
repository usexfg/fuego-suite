// Copyright (c) 2017-2026 Fuego Developers
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include "crypto/crypto.h"
#include "crypto/secp_adaptor.h"

namespace XfgSwap {

// ── Taproot-inspired PTLC script for BTC/LTC P2WSH (scriptless-lite) ──
// Script commits to ptlcPoint T = t*G (32-byte x-only). Spend:
//   claim:  <sig> <1> <redeemScript>  -> IF branch, checks recipient sig, point was committed
//   refund: <sig> <0> <redeemScript>  -> ELSE branch, CLTV + sender sig
// On-chain verification of adaptor relation (s'*G == R+e*P+T) is done off-chain by the daemon
// via secp_adaptor_verify on the claim transaction's signature vs stored presig.
// The fixed script still guarantees timeout safety and point commitment.

class BtcPtlcScript {
public:
  // Create PTLC redeem script.
  // ptlcPoint: 32-byte x-only (from T compressed 33 -> x) or full 32 ed25519 repurposed as x.
  // For secp, pass x-only 32 derived from secp T via secp_secret_to_pubkey.
  static std::vector<uint8_t> createPtlcScript(
      const std::vector<uint8_t>& ptlcPointX32,
      uint32_t lockTime,
      const std::vector<uint8_t>& recipientPubKey,
      const std::vector<uint8_t>& senderPubKey,
      uint32_t timeoutBlock);

  // P2WSH helpers (reuse BtcHtlcScript style)
  static std::vector<uint8_t> redeemScriptToP2wshScriptPubKey(const std::vector<uint8_t>& redeemScript);
  static std::vector<uint8_t> witnessScriptHash(const std::vector<uint8_t>& redeemScript);
  static std::string witnessScriptToAddress(const std::vector<uint8_t>& witnessScript, const std::string& hrp = "bc");

  // Build claim witness: [<sig> <t 32> OP_1 <redeemScript>] (IF true, t reveals scalar off-chain verified)
  static std::vector<std::vector<uint8_t>> createClaimWitness(
      const std::vector<uint8_t>& signature,
      const std::vector<uint8_t>& adaptorSecret32,
      const std::vector<uint8_t>& witnessScript);

  // Build refund witness: [<sig> OP_0 <redeemScript>]
  static std::vector<std::vector<uint8_t>> createRefundWitness(
      const std::vector<uint8_t>& signature,
      const std::vector<uint8_t>& witnessScript);

  // Parse adaptor secret t (32 bytes) from claim transaction witness stack.
  static std::vector<uint8_t> parseClaimAdaptorSecret(
      const std::vector<uint8_t>& rawTx,
      const std::vector<uint8_t>& p2wshScriptPubKey);

  static std::vector<uint8_t> hexToBytes(const std::string& hex);
  static std::string bytesToHex(const std::vector<uint8_t>& bytes);
  static std::vector<uint8_t> sha256(const std::vector<uint8_t>& data);

private:
  static void pushData(std::vector<uint8_t>& script, const std::vector<uint8_t>& data);
  static void writeLE32(std::vector<uint8_t>& out, uint32_t v);
  static void writeVarInt(std::vector<uint8_t>& out, uint64_t n);
};

} // namespace XfgSwap
