#include "SolChainClient.h"
#include "Common/StringTools.h"

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

} // namespace XfgSwap
