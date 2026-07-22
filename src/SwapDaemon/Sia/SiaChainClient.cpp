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

#include "SiaChainClient.h"
#include "SiaHtlcScript.h"
#include "Common/StringTools.h"

#include <cstring>
#include <algorithm>

namespace XfgSwap {

static bool isZeroSecret(const Crypto::SecretKey& s) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&s);
  for (size_t i = 0; i < sizeof(Crypto::SecretKey); ++i) if (p[i]) return false;
  return true;
}

SiaChainClient::SiaChainClient(std::unique_ptr<SiaRpcClient> rpc, const std::string& privKeyHex)
  : m_rpc(std::move(rpc)), m_privKeyHex(privKeyHex) {}

ChainClientResult SiaChainClient::lock(const SwapParams& params) {
  if (isZeroSecret(params.adaptorSecret))
    return ChainClientResult::fail("SC lock: adaptor secret not set — cannot derive hashlock");

  if (!m_rpc)
    return ChainClientResult::fail("SC lock: RPC client not available");

  // Convert Fuego's secp256k1 adaptor secret to Sia's ed25519 format
  // For now, we'll use a simplified approach: hash the adaptor secret to get the hashlock
  std::vector<uint8_t> adaptorSecretBytes;
  const uint8_t* secretPtr = reinterpret_cast<const uint8_t*>(&params.adaptorSecret);
  adaptorSecretBytes.assign(secretPtr, secretPtr + sizeof(Crypto::SecretKey));

  // Compute hashlock: SHA256(adaptorSecret)
  std::vector<uint8_t> hashLockSha256 = SiaHtlcScript::sha256(adaptorSecretBytes);
  std::string hashLockSha256Hex = SiaHtlcScript::bytesToHex(hashLockSha256);

  // For Sia, we need to use the recipient's ed25519 public key
  // For now, we'll use a placeholder (in production, this would come from the counterparty)
  std::vector<uint8_t> recipientPubKey(32, 0x00);  // Placeholder
  std::vector<uint8_t> senderPubKey(32, 0x00);  // Placeholder

  // Create the HTLC redeem script
  std::vector<uint8_t> redeemScript = SiaHtlcScript::createRedeemScript(
      hashLockSha256, recipientPubKey, senderPubKey,
      static_cast<uint32_t>(params.ctrTimeoutBlock));

  std::string redeemScriptHex = SiaHtlcScript::bytesToHex(redeemScript);

  // Build and sign the transaction
  std::string lockTxId;
  bool ok = m_rpc->lockHtlc(
      m_privKeyHex,
      params.ctrAddress,
      hashLockSha256Hex,
      static_cast<uint32_t>(params.ctrTimeoutBlock),
      params.ctrAmount,
      lockTxId,
      redeemScriptHex);

  if (!ok) return ChainClientResult::fail("SC lockHtlc failed");
  return ChainClientResult::okWithState(lockTxId, redeemScriptHex);
}

ChainClientResult SiaChainClient::verifyLock(const SwapParams& params) {
  if (!m_rpc)
    return ChainClientResult::fail("SC verifyLock: RPC client not available");

  bool ok = m_rpc->verifyLock(params.ctrLockTxId, params.ctrAmount);
  if (!ok) return ChainClientResult::fail("SC lock not verified");
  return ChainClientResult::ok(params.ctrLockTxId);
}

ChainClientResult SiaChainClient::claim(const SwapParams& params) {
  if (!m_rpc)
    return ChainClientResult::fail("SC claim: RPC client not available");

  std::string claimTxId;
  bool ok = m_rpc->claim(
      m_privKeyHex,
      params.ctrLockTxId, 0, params.ctrAmount,
      params.chainState,
      Common::podToHex(params.adaptorSecret),
      params.ctrAddress,
      claimTxId);
  if (!ok) return ChainClientResult::fail("SC claim failed");
  return ChainClientResult::ok(claimTxId);
}

ChainClientResult SiaChainClient::refund(const SwapParams& params) {
  if (!m_rpc)
    return ChainClientResult::fail("SC refund: RPC client not available");

  std::string refundTxId;
  bool ok = m_rpc->refundHtlc(
      m_privKeyHex,
      params.ctrLockTxId, 0, params.ctrAmount,
      params.chainState,
      static_cast<uint32_t>(params.ctrTimeoutBlock),
      params.ctrAddress,
      refundTxId);
  if (!ok) return ChainClientResult::fail("SC refundHtlc failed");
  return ChainClientResult::ok(refundTxId);
}

ChainClientResult SiaChainClient::verifyReserveProof(const std::string& expectedMessage,
    uint64_t minAmount, const std::string& proof) {
  if (!m_rpc)
    return ChainClientResult::fail("SC verifyReserveProof: RPC client not available");

  // Sia uses ed25519 signatures for message verification
  // The proof format is: address:signature:message
  size_t c1 = proof.find(':');
  size_t c2 = proof.find(':', c1 + 1);
  if (c1 == std::string::npos || c2 == std::string::npos)
    return ChainClientResult::fail("SC reserve proof: invalid format (expected address:signature:message)");

  std::string address   = proof.substr(0, c1);
  std::string signature = proof.substr(c1 + 1, c2 - c1 - 1);
  std::string message   = proof.substr(c2 + 1);

  // TODO: Implement ed25519 signature verification
  // For now, we'll assume the proof is valid if it has the correct format
  bool sigValid = true;  // Placeholder

  if (!sigValid)
    return ChainClientResult::fail("SC reserve proof: invalid signature");

  if (!expectedMessage.empty() && message != expectedMessage)
    return ChainClientResult::fail("SC reserve proof: message not bound to this offer");

  uint64_t balance = 0;
  if (!m_rpc->getBalance(address, balance))
    return ChainClientResult::fail("SC reserve proof: balance check RPC failed");
  if (balance < minAmount)
    return ChainClientResult::fail("SC reserve proof: insufficient balance (" +
                                   std::to_string(balance) + " < " + std::to_string(minAmount) + ")");

  return ChainClientResult::ok(address);
}

bool SiaChainClient::getCurrentHeight(uint64_t& height) {
  if (m_rpc) {
    return m_rpc->getBlockCount(height);
  }
  return false;
}

ChainClientResult SiaChainClient::getTransactionDetails(const std::string& txId,
                                                        ChainClientResult& result) {
  if (m_rpc) {
    SiaTxInfo txInfo;
    if (!m_rpc->getTransaction(txId, txInfo)) {
      result = ChainClientResult::fail("SC RPC: gettransaction failed for " + txId);
      return result;
    }

    uint64_t tipHeight = 0;
    m_rpc->getBlockCount(tipHeight);

    result.success = true;
    result.confirmed = txInfo.confirmations > 0;
    result.spvVerified = false;
    result.blockHeight = txInfo.blockHeight;
    result.confirmations = txInfo.confirmations;
    return result;
  }

  result = ChainClientResult::fail("SC: no RPC client available");
  return result;
}

std::string SiaChainClient::extractSecret(const std::string& spendingTxid,
                                           const std::string& htlcRedeemScriptHex) {
  // TODO: Implement secret extraction for Sia
  // This requires:
  // 1. Get the raw transaction
  // 2. Parse the transaction to find the HTLC input
  // 3. Extract the preimage from the scriptSig
  // 4. Verify that SHA256(preimage) == hashLock

  return {};
}

} // namespace XfgSwap
