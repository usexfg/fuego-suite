#include "SolChainClient.h"
#include "Common/StringTools.h"
#include "../Crypto/Base58Std.h"
#include "../Crypto/Ed25519Verify.h"

namespace XfgSwap {

SolChainClient::SolChainClient(std::unique_ptr<SolRpcClient> rpc, const std::string& keypairBase58)
  : m_rpc(std::move(rpc)), m_keypairBase58(keypairBase58) {}

ChainClientResult SolChainClient::lock(const SwapParams& params) {
  SolTxResult solResult;
  bool ok = m_rpc->lock(
      m_keypairBase58,
      params.ctrAddress,
      Common::podToHex(params.adaptorPoint),
      params.ctrTimeoutBlock,
      params.ctrAmount,
      solResult);
  if (!ok || !solResult.confirmed)
    return ChainClientResult::fail("SOL lock failed: " + solResult.error);
  return ChainClientResult::ok(solResult.signature);
}

ChainClientResult SolChainClient::verifyLock(const SwapParams& params) {
  bool ok = m_rpc->verifyLock(params.ctrLockTxId, params.ctrAmount);
  if (!ok) return ChainClientResult::fail("SOL lock not verified");
  return ChainClientResult::ok(params.ctrLockTxId);
}

ChainClientResult SolChainClient::claim(const SwapParams& params) {
  SolTxResult solResult;
  bool ok = m_rpc->claim(
      m_keypairBase58,
      params.ctrLockTxId,
      Common::podToHex(params.adaptorSecret),
      solResult);
  if (!ok || !solResult.confirmed)
    return ChainClientResult::fail("SOL claim failed: " + solResult.error);
  return ChainClientResult::ok(solResult.signature);
}

ChainClientResult SolChainClient::refund(const SwapParams& params) {
  SolTxResult solResult;
  bool ok = m_rpc->refund(
      m_keypairBase58,
      params.ctrLockTxId,
      solResult);
  if (!ok || !solResult.confirmed)
    return ChainClientResult::fail("SOL refund failed: " + solResult.error);
  return ChainClientResult::ok(solResult.signature);
}

ChainClientResult SolChainClient::verifyReserveProof(const std::string& expectedMessage,
    uint64_t minAmount, const std::string& proof) {
  size_t c1 = proof.find(':');
  size_t c2 = proof.find(':', c1 + 1);
  if (c1 == std::string::npos || c2 == std::string::npos)
    return ChainClientResult::fail("SOL reserve proof: invalid format (expected address:signature:message)");

  std::string address   = proof.substr(0, c1);
  std::string sigB58    = proof.substr(c1 + 1, c2 - c1 - 1);
  std::string message   = proof.substr(c2 + 1);

  std::vector<uint8_t> pubkeyBytes = Base58Std::decode(address);
  if (pubkeyBytes.size() != 32)
    return ChainClientResult::fail("SOL reserve proof: invalid pubkey length (" +
                                   std::to_string(pubkeyBytes.size()) + ")");

  std::vector<uint8_t> sigBytes = Base58Std::decode(sigB58);
  if (sigBytes.size() != 64)
    return ChainClientResult::fail("SOL reserve proof: invalid signature length (" +
                                   std::to_string(sigBytes.size()) + ")");

  if (!Ed25519Verify::verify(pubkeyBytes.data(), message, sigBytes.data()))
    return ChainClientResult::fail("SOL reserve proof: invalid signature");

  if (!expectedMessage.empty() && message != expectedMessage)
    return ChainClientResult::fail("SOL reserve proof: message not bound to this offer");

  uint64_t balance = 0;
  if (!m_rpc->getBalance(address, balance))
    return ChainClientResult::fail("SOL reserve proof: balance check RPC failed");
  if (balance < minAmount)
    return ChainClientResult::fail("SOL reserve proof: insufficient balance (" +
                                   std::to_string(balance) + " < " + std::to_string(minAmount) + ")");

  return ChainClientResult::ok(address);
}

bool SolChainClient::getCurrentHeight(uint64_t& height) {
  return m_rpc->getSlot(height);
}

} // namespace XfgSwap
