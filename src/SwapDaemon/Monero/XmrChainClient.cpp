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

} // namespace XfgSwap
