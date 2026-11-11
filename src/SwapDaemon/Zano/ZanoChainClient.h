#pragma once

#include "../IChainClient.h"
#include "ZanoRpcClient.h"
#include <string>
#include <memory>

namespace XfgSwap {

class ZanoChainClient : public IChainClient {
public:
  ZanoChainClient(std::unique_ptr<ZanoRpcClient> rpc,
                 const std::string& spendKeyHex,
                 const std::string& viewKeyHex);

  std::string chainName() const override { return "ZANO"; }
  ChainClientResult lock(const SwapParams& params) override;
  ChainClientResult verifyLock(const SwapParams& params) override;
  ChainClientResult claim(const SwapParams& params) override;
  ChainClientResult refund(const SwapParams& params) override;
  ChainClientResult verifyReserveProof(const std::string& expectedMessage,
                                       uint64_t minAmount,
                                       const std::string& proof) override;
  bool getCurrentHeight(uint64_t& height) override;

private:
  std::unique_ptr<ZanoRpcClient> m_rpc;
  std::string m_spendKeyHex;
  std::string m_viewKeyHex;
};

} // namespace XfgSwap
