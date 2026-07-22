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
  ChainClientResult lock(const SwapParams& params) override;
  ChainClientResult verifyLock(const SwapParams& params) override;
  ChainClientResult claim(const SwapParams& params) override;
  ChainClientResult refund(const SwapParams& params) override;
  ChainClientResult verifyReserveProof(const std::string& expectedMessage,
                                       uint64_t minAmount,
                                       const std::string& proof) override;
  std::string tryExtractClaimedSecret(const SwapParams& params) override;
  bool getCurrentHeight(uint64_t& height) override;

private:
  std::unique_ptr<SolRpcClient> m_rpc;
  std::string m_keypairBase58;
};

} // namespace XfgSwap
