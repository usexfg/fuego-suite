#pragma once

#include "../IChainClient.h"
#include "SolRpcClient.h"
#include <string>
#include <memory>

namespace XfgSwap {

class SolChainClient : public IChainClient {
public:
  SolChainClient(std::unique_ptr<SolRpcClient> rpc, const std::string& keypairBase58);

  std::string chainName() const override { return "SOL"; }
  std::string readinessError() override;
  // Point locks for SOL arrive with CROSS_CURVE_PLAN X4.
  bool supportsPtlc() const override { return false; }
  std::string getReceiveAddress() const override;
  ChainClientResult lock(const SwapParams& params) override;
  ChainClientResult lockPtlc(const SwapParams& params) override;
  ChainClientResult verifyLock(const SwapParams& params) override;
  ChainClientResult claim(const SwapParams& params) override;
  ChainClientResult refund(const SwapParams& params) override;
  ChainClientResult getTransactionDetails(const std::string& txId,
                                          ChainClientResult& result) override;
  ChainClientResult verifyReserveProof(const std::string& expectedMessage,
                                       uint64_t minAmount,
                                       const std::string& proof) override;
  std::string tryExtractClaimedSecret(const SwapParams& params) override;
  bool hasConfirmedClaim(const SwapParams& params) override;
  bool getCurrentHeight(uint64_t& height) override;

private:
  std::unique_ptr<SolRpcClient> m_rpc;
  std::string m_keypairBase58;
};

} // namespace XfgSwap
