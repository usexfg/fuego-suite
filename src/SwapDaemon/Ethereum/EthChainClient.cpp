#include "EthChainClient.h"
#include "Common/StringTools.h"
#include <stdexcept>

namespace XfgSwap {

EthChainClient::EthChainClient(std::unique_ptr<EthRpcClient> rpc, const std::string& address)
  : m_rpc(std::move(rpc)), m_address(address) {}

ChainClientResult EthChainClient::lock(const SwapParams& params) {
  try {
    std::string contractAddress;
    bool ok = m_rpc->deployHtlc(
        m_address,
        params.ctrAddress,
        Common::podToHex(params.adaptorPoint),
        params.ctrTimeoutBlock,
        params.ctrAmount,
        contractAddress);
    if (!ok) return ChainClientResult::fail("ETH deployHtlc failed");
    return ChainClientResult::ok(contractAddress);
  } catch (const std::runtime_error& e) {
    auto r = ChainClientResult::fail(std::string("ETH lock error: ") + e.what());
    r.fatal = true;
    return r;
  }
}

ChainClientResult EthChainClient::verifyLock(const SwapParams& params) {
  bool ok = m_rpc->verifyLock(params.ctrLockTxId, params.ctrAmount);
  if (!ok) return ChainClientResult::fail("ETH lock not verified");
  return ChainClientResult::ok(params.ctrLockTxId);
}

ChainClientResult EthChainClient::claim(const SwapParams& params) {
  try {
    std::string claimTxHash;
    bool ok = m_rpc->claimHtlc(
        m_address,
        params.ctrLockTxId,
        Common::podToHex(params.adaptorSecret),
        claimTxHash);
    if (!ok) return ChainClientResult::fail("ETH claimHtlc failed");
    return ChainClientResult::ok(claimTxHash);
  } catch (const std::runtime_error& e) {
    auto r = ChainClientResult::fail(std::string("ETH claim error: ") + e.what());
    r.fatal = true;
    return r;
  }
}

ChainClientResult EthChainClient::refund(const SwapParams& params) {
  try {
    std::string refundTxHash;
    bool ok = m_rpc->refundHtlc(
        m_address,
        params.ctrLockTxId,
        refundTxHash);
    if (!ok) return ChainClientResult::fail("ETH refundHtlc failed");
    return ChainClientResult::ok(refundTxHash);
  } catch (const std::runtime_error& e) {
    auto r = ChainClientResult::fail(std::string("ETH refund error: ") + e.what());
    r.fatal = true;
    return r;
  }
}

} // namespace XfgSwap
