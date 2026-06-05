#pragma once

#include "../IChainClient.h"
#include "MoneroRpcClient.h"
#include <string>
#include <memory>

namespace XfgSwap {

class XmrChainClient : public IChainClient {
public:
  XmrChainClient(std::unique_ptr<MoneroRpcClient> rpc,
                 const std::string& spendKeyHex,
                 const std::string& viewKeyHex);

  std::string chainName() const override { return "XMR"; }
  ChainClientResult lock(const SwapParams& params) override;
  ChainClientResult verifyLock(const SwapParams& params) override;
  ChainClientResult claim(const SwapParams& params) override;
  ChainClientResult refund(const SwapParams& params) override;
  ChainClientResult verifyReserveProof(const std::string& expectedMessage,
                                       uint64_t minAmount,
                                       const std::string& proof) override;
  bool getCurrentHeight(uint64_t& height) override;

private:
  std::unique_ptr<MoneroRpcClient> m_rpc;
  std::string m_spendKeyHex;
  std::string m_viewKeyHex;
};

} // namespace XfgSwap
