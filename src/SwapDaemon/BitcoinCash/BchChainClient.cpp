#include "BchChainClient.h"
#include "Common/StringTools.h"

namespace XfgSwap {

BchChainClient::BchChainClient(std::unique_ptr<BchRpcClient> rpc, const std::string& wif)
  : m_rpc(std::move(rpc)), m_wif(wif) {}

ChainClientResult BchChainClient::lock(const SwapParams& params) {
  std::string lockTxId;
  bool ok = m_rpc->lockHtlc(
      m_wif,
      params.ctrAddress,
      Common::podToHex(params.adaptorPoint),
      static_cast<uint32_t>(params.ctrTimeoutBlock),
      params.ctrAmount,
      lockTxId);
  if (!ok) return ChainClientResult::fail("BCH lockHtlc failed");
  return ChainClientResult::ok(lockTxId);
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

} // namespace XfgSwap
