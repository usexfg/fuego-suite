#include "DcrChainClient.h"
#include "SwapDaemon/Crypto/Secp256k1Signer.h"
#include <cstring>

namespace XfgSwap {

DcrChainClient::DcrChainClient(std::unique_ptr<DcrRpcClient> rpc)
  : m_rpc(std::move(rpc)) {}

ChainClientResult DcrChainClient::lock(const SwapParams& params) {
  if (!m_rpc) {
    return ChainClientResult::fail("DCR lock: RPC client not available");
  }

  // DCR lock not yet implemented — requires building a P2SH HTLC tx
  // with Decred's transaction format (version, locktime, expiry).
  return ChainClientResult::fail("DCR lock: not yet implemented");
}

ChainClientResult DcrChainClient::verifyLock(const SwapParams& params) {
  if (!m_rpc) {
    return ChainClientResult::fail("DCR verifyLock: RPC client not available");
  }

  if (params.ctrLockTxId.empty()) {
    return ChainClientResult::fail("DCR verifyLock: no lock txid provided");
  }

  // Fetch tx output to verify the P2SH HTLC output exists with correct amount
  uint64_t amount = 0;
  if (!m_rpc->getTxOut(params.ctrLockTxId, 0, amount)) {
    return ChainClientResult::fail("DCR verifyLock: tx output not found: " + params.ctrLockTxId);
  }

  if (amount < params.ctrAmount) {
    return ChainClientResult::fail("DCR verifyLock: output amount " +
                                   std::to_string(amount) + " < required " +
                                   std::to_string(params.ctrAmount));
  }

  ChainClientResult result;
  result.success = true;
  result.txId = params.ctrLockTxId;
  return result;
}

ChainClientResult DcrChainClient::claim(const SwapParams& params) {
  if (!m_rpc) {
    return ChainClientResult::fail("DCR claim: RPC client not available");
  }

  // DCR claim not yet implemented — requires building a spending tx
  // that reveals the HTLC preimage.
  return ChainClientResult::fail("DCR claim: not yet implemented");
}

ChainClientResult DcrChainClient::refund(const SwapParams& params) {
  if (!m_rpc) {
    return ChainClientResult::fail("DCR refund: RPC client not available");
  }

  // DCR refund not yet implemented — requires building a timelocked spending tx.
  return ChainClientResult::fail("DCR refund: not yet implemented");
}

ChainClientResult DcrChainClient::verifyReserveProof(const std::string& expectedMessage,
                                                     uint64_t minAmount,
                                                     const std::string& proof) {
  // DCR reserve proof verification not yet implemented
  return ChainClientResult::fail("DCR verifyReserveProof: not yet implemented");
}

ChainClientResult DcrChainClient::getTransactionDetails(const std::string& txId,
                                                        ChainClientResult& result) {
  if (!m_rpc) {
    result = ChainClientResult::fail("DCR: RPC client not available");
    return result;
  }

  uint64_t height = 0;
  if (!m_rpc->getBlockCount(height)) {
    result = ChainClientResult::fail("DCR: cannot get block count");
    return result;
  }

  // TODO: fetch confirmations from getrawtransaction when implemented
  result = ChainClientResult::fail("DCR getTransactionDetails: not yet implemented");
  return result;
}

bool DcrChainClient::getCurrentHeight(uint64_t& height) {
  if (m_rpc) {
    return m_rpc->getBlockCount(height);
  }
  return false;
}

std::string DcrChainClient::extractSecret(const std::string& spendingTxid,
                                           const std::string& htlcRedeemScriptHex) {
  // DCR extractSecret not yet implemented — needs raw tx decode + script parsing
  return {};
}

} // namespace XfgSwap
