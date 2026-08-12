#include "ZanoChainClient.h"
#include "Common/StringTools.h"

namespace XfgSwap {

ZanoChainClient::ZanoChainClient(std::unique_ptr<ZanoRpcClient> rpc,
                               const std::string& spendKeyHex,
                               const std::string& viewKeyHex)
  : m_rpc(std::move(rpc)), m_spendKeyHex(spendKeyHex), m_viewKeyHex(viewKeyHex) {}

ChainClientResult ZanoChainClient::lock(const SwapParams& params) {
  ZanoTransferResult zanoResult;
  bool ok = m_rpc->lockAdaptor(
      params.ctrAddress,
      params.ctrAmount,
      zanoResult);
  if (!ok || !zanoResult.success)
    return ChainClientResult::fail("ZANO lockAdaptor failed: " + zanoResult.error);
  return ChainClientResult::ok(zanoResult.txHash);
}

ChainClientResult ZanoChainClient::verifyLock(const SwapParams& params) {
  // Must open a watch-only wallet for the negotiated shared address using the
  // shared view key — never trust whichever wallet is currently open on RPC.
  if (m_viewKeyHex.empty())
    return ChainClientResult::fail("ZANO verifyLock: shared view key not configured");
  bool ok = m_rpc->verifyLock(params.ctrAddress, m_viewKeyHex, params.ctrAmount);
  if (!ok) return ChainClientResult::fail("ZANO lock not verified for shared address");
  return ChainClientResult::ok(params.ctrAddress);
}

ChainClientResult ZanoChainClient::claim(const SwapParams& params) {
  ZanoTransferResult zanoResult;
  bool ok = m_rpc->claimAdaptor(
      m_spendKeyHex,
      Common::podToHex(params.peerSwapPubKey),
      Common::podToHex(params.adaptorSecret),
      m_viewKeyHex,
      params.ctrAddress,
      zanoResult);
  if (!ok || !zanoResult.success)
    return ChainClientResult::fail("ZANO claimAdaptor failed: " + zanoResult.error);
  return ChainClientResult::ok(zanoResult.txHash);
}

ChainClientResult ZanoChainClient::refund(const SwapParams& params) {
  ZanoTransferResult zanoResult;
  bool ok = m_rpc->refundAdaptor(
      m_spendKeyHex,
      Common::podToHex(params.peerSwapPubKey),
      m_viewKeyHex,
      params.ctrAddress,
      zanoResult);
  if (!ok || !zanoResult.success)
    return ChainClientResult::fail("ZANO refundAdaptor failed: " + zanoResult.error);
  return ChainClientResult::ok(zanoResult.txHash);
}

ChainClientResult ZanoChainClient::verifyReserveProof(const std::string& expectedMessage,
    uint64_t minAmount, const std::string& proof) {
  size_t c1 = proof.find(':');
  size_t c2 = proof.find(':', c1 + 1);
  if (c1 == std::string::npos || c2 == std::string::npos)
    return ChainClientResult::fail("ZANO reserve proof: invalid format (expected address:message:signature)");

  std::string address  = proof.substr(0, c1);
  std::string message  = proof.substr(c1 + 1, c2 - c1 - 1);
  std::string signature = proof.substr(c2 + 1);

  bool good = false;
  uint64_t total = 0;
  if (!m_rpc->checkReserveProof(address, message, signature, good, total))
    return ChainClientResult::fail("ZANO reserve proof: RPC call failed");
  if (!good)
    return ChainClientResult::fail("ZANO reserve proof: invalid signature");
  if (!expectedMessage.empty() && message != expectedMessage)
    return ChainClientResult::fail("ZANO reserve proof: message not bound to this offer");
  if (total < minAmount)
    return ChainClientResult::fail("ZANO reserve proof: insufficient balance (" +
                                   std::to_string(total) + " < " + std::to_string(minAmount) + ")");

  return ChainClientResult::ok(address);
}

bool ZanoChainClient::getCurrentHeight(uint64_t& height) {
  return m_rpc->getHeight(height);
}

} // namespace XfgSwap
