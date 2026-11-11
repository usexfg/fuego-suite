// Copyright (c) 2017-2026 Fuego Developers
//
// TON IChainClient — SHA-256 HTLC hashlock, toncenter HTTP API.
// Full claim/refund BOC building is staged; verifyLock + extract paths work
// against a pre-deployed contract with readable state.

#pragma once

#include "../IChainClient.h"
#include "TonRpcClient.h"
#include <memory>
#include <string>

namespace XfgSwap {

class TonChainClient : public IChainClient {
public:
  TonChainClient(std::unique_ptr<TonRpcClient> rpc, const std::string& walletKeyHex);

  std::string chainName() const override { return "TON"; }
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
  std::unique_ptr<TonRpcClient> m_rpc;
  std::string m_walletKeyHex;
};

} // namespace XfgSwap
