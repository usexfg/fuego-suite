#pragma once

#include "../IChainClient.h"
#include "EthRpcClient.h"
#include <string>
#include <memory>

namespace XfgSwap {

class EthChainClient : public IChainClient {
public:
  EthChainClient(std::unique_ptr<EthRpcClient> rpc, const std::string& address);

  std::string chainName() const override { return "ETH"; }
  ChainClientResult lock(const SwapParams& params) override;
  ChainClientResult verifyLock(const SwapParams& params) override;
  ChainClientResult claim(const SwapParams& params) override;
  ChainClientResult refund(const SwapParams& params) override;

private:
  std::unique_ptr<EthRpcClient> m_rpc;
  std::string m_address;
};

} // namespace XfgSwap
