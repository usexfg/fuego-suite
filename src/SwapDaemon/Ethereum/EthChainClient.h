#pragma once

#include "../IChainClient.h"
#include "EthRpcClient.h"
#include <string>
#include <memory>

namespace XfgSwap {

class EthChainClient : public IChainClient {
public:
  EthChainClient(std::unique_ptr<EthRpcClient> rpc, const std::string& address,
                 const std::string& chainName = "ETH");

  std::string chainName() const override { return m_chainName; }
  ChainClientResult lock(const SwapParams& params) override;
  ChainClientResult verifyLock(const SwapParams& params) override;
  ChainClientResult claim(const SwapParams& params) override;
  ChainClientResult refund(const SwapParams& params) override;
  ChainClientResult verifyReserveProof(const std::string& expectedMessage,
                                       uint64_t minAmount,
                                       const std::string& proof) override;
  bool getCurrentHeight(uint64_t& height) override;
  std::string tryExtractClaimedSecret(const SwapParams& params) override;

private:
  std::unique_ptr<EthRpcClient> m_rpc;
  std::string m_address;
  std::string m_chainName;
};

} // namespace XfgSwap
