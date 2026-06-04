#include "XmrChainClient.h"
#include "Common/StringTools.h"

namespace XfgSwap {

XmrChainClient::XmrChainClient(std::unique_ptr<MoneroRpcClient> rpc,
                               const std::string& spendKeyHex,
                               const std::string& viewKeyHex)
  : m_rpc(std::move(rpc)), m_spendKeyHex(spendKeyHex), m_viewKeyHex(viewKeyHex) {}

ChainClientResult XmrChainClient::lock(const SwapParams& params) {
  MoneroTransferResult xmrResult;
  bool ok = m_rpc->lockAdaptor(
      params.ctrAddress,
      params.ctrAmount,
      xmrResult);
  if (!ok || !xmrResult.success)
    return ChainClientResult::fail("XMR lockAdaptor failed: " + xmrResult.error);
  return ChainClientResult::ok(xmrResult.txHash);
}

ChainClientResult XmrChainClient::verifyLock(const SwapParams& params) {
  bool ok = m_rpc->verifyLock(params.ctrAddress, params.ctrAmount);
  if (!ok) return ChainClientResult::fail("XMR lock not verified");
  return ChainClientResult::ok(params.ctrAddress);
}

ChainClientResult XmrChainClient::claim(const SwapParams& params) {
  MoneroTransferResult xmrResult;
  bool ok = m_rpc->claimAdaptor(
      m_spendKeyHex,
      Common::podToHex(params.peerSwapPubKey),
      Common::podToHex(params.adaptorSecret),
      m_viewKeyHex,
      params.ctrAddress,
      xmrResult);
  if (!ok || !xmrResult.success)
    return ChainClientResult::fail("XMR claimAdaptor failed: " + xmrResult.error);
  return ChainClientResult::ok(xmrResult.txHash);
}

ChainClientResult XmrChainClient::refund(const SwapParams& params) {
  MoneroTransferResult xmrResult;
  bool ok = m_rpc->refundAdaptor(
      m_spendKeyHex,
      Common::podToHex(params.peerSwapPubKey),
      m_viewKeyHex,
      params.ctrAddress,
      xmrResult);
  if (!ok || !xmrResult.success)
    return ChainClientResult::fail("XMR refundAdaptor failed: " + xmrResult.error);
  return ChainClientResult::ok(xmrResult.txHash);
}

ChainClientResult XmrChainClient::verifyReserveProof(const std::string& ctrAddress,
    uint64_t minAmount, const std::string& proof) {
  size_t c1 = proof.find(':');
  size_t c2 = proof.find(':', c1 + 1);
  if (c1 == std::string::npos || c2 == std::string::npos)
    return ChainClientResult::fail("XMR reserve proof: invalid format (expected address:message:signature)");

  std::string address  = proof.substr(0, c1);
  std::string message  = proof.substr(c1 + 1, c2 - c1 - 1);
  std::string signature = proof.substr(c2 + 1);

  bool good = false;
  uint64_t total = 0;
  if (!m_rpc->checkReserveProof(address, message, signature, good, total))
    return ChainClientResult::fail("XMR reserve proof: RPC call failed");
  if (!good)
    return ChainClientResult::fail("XMR reserve proof: invalid signature");
  if (total < minAmount)
    return ChainClientResult::fail("XMR reserve proof: insufficient balance (" +
                                   std::to_string(total) + " < " + std::to_string(minAmount) + ")");

  return ChainClientResult::ok(ctrAddress);
}

bool XmrChainClient::getCurrentHeight(uint64_t& height) {
  return m_rpc->getHeight(height);
}

} // namespace XfgSwap
