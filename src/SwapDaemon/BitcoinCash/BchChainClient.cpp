#include "BchChainClient.h"
#include "Common/StringTools.h"
#include "../SwapHashLock.h"

namespace XfgSwap {

static bool isZeroSecret(const Crypto::SecretKey& s) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&s);
  for (size_t i = 0; i < sizeof(Crypto::SecretKey); ++i) if (p[i]) return false;
  return true;
}

BchChainClient::BchChainClient(std::unique_ptr<BchRpcClient> rpc, const std::string& wif)
  : m_rpc(std::move(rpc)), m_wif(wif) {}

ChainClientResult BchChainClient::lock(const SwapParams& params) {
  // The BCH P2SH HTLC redeem script enforces OP_SHA256 <hash_lock>
  // OP_EQUALVERIFY, and claim() reveals the adaptor secret t as the preimage.
  // So the hashlock MUST be sha256(t) — NOT the adaptor point T = t*G. The old
  // code committed T, so OP_EQUALVERIFY always failed; funds could only refund.
  if (isZeroSecret(params.adaptorSecret))
    return ChainClientResult::fail("BCH lock: adaptor secret not set — cannot derive hashlock");

  std::string lockTxId;
  std::string redeemScriptHex;
  bool ok = m_rpc->lockHtlc(
      m_wif,
      params.ctrAddress,
      bchHashLockHex(params.adaptorSecret),
      static_cast<uint32_t>(params.ctrTimeoutBlock),
      params.ctrAmount,
      lockTxId,
      redeemScriptHex);
  if (!ok) return ChainClientResult::fail("BCH lockHtlc failed");
  return ChainClientResult::okWithState(lockTxId, redeemScriptHex);
}

ChainClientResult BchChainClient::verifyLock(const SwapParams& params) {
  bool ok = m_rpc->verifyLock(params.ctrLockTxId, params.ctrAmount);
  if (!ok) return ChainClientResult::fail("BCH lock not verified");
  return ChainClientResult::ok(params.ctrLockTxId);
}

ChainClientResult BchChainClient::claim(const SwapParams& params) {
  std::string claimTxId;
  bool ok = m_rpc->claim(
      m_wif,
      params.ctrLockTxId, 0, params.ctrAmount,
      params.chainState,
      Common::podToHex(params.adaptorSecret),
      params.ctrAddress,
      claimTxId);
  if (!ok) return ChainClientResult::fail("BCH claim failed");
  return ChainClientResult::ok(claimTxId);
}

ChainClientResult BchChainClient::refund(const SwapParams& params) {
  std::string refundTxId;
  bool ok = m_rpc->refundHtlc(
      m_wif,
      params.ctrLockTxId, 0, params.ctrAmount,
      params.chainState,
      params.ctrAddress,
      refundTxId);
  if (!ok) return ChainClientResult::fail("BCH refundHtlc failed");
  return ChainClientResult::ok(refundTxId);
}

ChainClientResult BchChainClient::verifyReserveProof(const std::string& expectedMessage,
    uint64_t minAmount, const std::string& proof) {
  size_t c1 = proof.find(':');
  size_t c2 = proof.find(':', c1 + 1);
  if (c1 == std::string::npos || c2 == std::string::npos)
    return ChainClientResult::fail("BCH reserve proof: invalid format (expected address:signature:message)");

  std::string address   = proof.substr(0, c1);
  std::string signature = proof.substr(c1 + 1, c2 - c1 - 1);
  std::string message   = proof.substr(c2 + 1);

  bool sigValid = false;
  if (!m_rpc->verifyMessage(address, signature, message, sigValid))
    return ChainClientResult::fail("BCH reserve proof: verifymessage RPC failed");
  if (!sigValid)
    return ChainClientResult::fail("BCH reserve proof: invalid signature");

  if (!expectedMessage.empty() && message != expectedMessage)
    return ChainClientResult::fail("BCH reserve proof: message not bound to this offer");

  uint64_t balance = 0;
  if (!m_rpc->getBalance(address, balance))
    return ChainClientResult::fail("BCH reserve proof: balance check RPC failed");
  if (balance < minAmount)
    return ChainClientResult::fail("BCH reserve proof: insufficient balance (" +
                                   std::to_string(balance) + " < " + std::to_string(minAmount) + ")");

  return ChainClientResult::ok(address);
}

bool BchChainClient::getCurrentHeight(uint64_t& height) {
  return m_rpc->getBlockCount(height);
}

} // namespace XfgSwap
