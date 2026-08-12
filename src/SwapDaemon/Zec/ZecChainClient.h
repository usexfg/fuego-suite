#pragma once

#include "../IChainClient.h"
#include "ZecRpcClient.h"
#include <string>
#include <memory>

namespace XfgSwap {

class ZecChainClient : public IChainClient {
public:
  // Full-node mode. Zeccoin has no SegWit and the codebase has no Zec SPV
  // client, so lock/claim/refund all go through the node RPC (like other
  // UTXO chains' full-node mode).
  ZecChainClient(std::unique_ptr<ZecRpcClient> rpc, const std::string& wif);

  std::string chainName() const override { return "ZEC"; }
  ChainClientResult lock(const SwapParams& params) override;
  ChainClientResult verifyLock(const SwapParams& params) override;
  ChainClientResult claim(const SwapParams& params) override;
  ChainClientResult refund(const SwapParams& params) override;
  ChainClientResult verifyReserveProof(const std::string& expectedMessage,
                                       uint64_t minAmount,
                                       const std::string& proof) override;
  ChainClientResult getTransactionDetails(const std::string& txId,
                                          ChainClientResult& result) override;
  bool getCurrentHeight(uint64_t& height) override;

  // Extract the HTLC claim preimage from a spending transaction (raw tx hex).
  // Returns lowercase hex of the 32-byte preimage, or empty on failure.
  std::string extractSecret(const std::string& spendingTxid,
                            const std::string& htlcRedeemScriptHex);

  std::string tryExtractClaimedSecret(const SwapParams& params) override;

private:
  std::unique_ptr<ZecRpcClient> m_rpc;
  std::string m_wif;
};

} // namespace XfgSwap
