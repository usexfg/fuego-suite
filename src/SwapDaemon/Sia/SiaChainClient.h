// Copyright (c) 2017-2026 Fuego Developers
//
// Sia (SC) IChainClient — Blake2b-256 hashlock, siad wallet RPC.

#pragma once

#include "../IChainClient.h"
#include "SiaRpcClient.h"
#include <memory>
#include <string>

namespace XfgSwap {

class SiaChainClient : public IChainClient {
public:
  SiaChainClient(std::unique_ptr<SiaRpcClient> rpc);

  std::string chainName() const override { return "SIA"; }
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
  std::unique_ptr<SiaRpcClient> m_rpc;
};

} // namespace XfgSwap
