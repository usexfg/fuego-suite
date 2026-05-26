#pragma once

#include "../IChainClient.h"
#include "BchRpcClient.h"
#include <string>
#include <memory>

namespace XfgSwap {

class BchChainClient : public IChainClient {
public:
  BchChainClient(std::unique_ptr<BchRpcClient> rpc, const std::string& wif);

  std::string chainName() const override { return "BCH"; }
  ChainClientResult lock(const SwapParams& params) override;
  ChainClientResult verifyLock(const SwapParams& params) override;
  ChainClientResult claim(const SwapParams& params) override;
  ChainClientResult refund(const SwapParams& params) override;

private:
  std::unique_ptr<BchRpcClient> m_rpc;
  std::string m_wif;
};

} // namespace XfgSwap
