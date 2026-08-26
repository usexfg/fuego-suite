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
  bool supportsPtlc() const override { return false; } // Phase 1: BRIDGE (HTLC on EVM, PTLC on XFG). Native Schnorr via EIP in Phase 4.
  // Honest gating (PTLC_PURE_PLAN P4.1): EVM stays BRIDGE until the EIP-6601
  // secp256k1 scalar-math precompile is live — PtlcTimelockPure's strict path
  // fails closed ("ECMUL_UNAVAILABLE") without it and the ecrecover fallback
  // is not cryptographic proof of the completed adaptor equation.
  bool supportsPurePtlc() const override { return false; }
  ChainClientResult lock(const SwapParams& params) override;
  ChainClientResult lockPtlc(const SwapParams& params) override { return lock(params); }
  ChainClientResult verifyLock(const SwapParams& params) override;
  ChainClientResult claim(const SwapParams& params) override;
  ChainClientResult refund(const SwapParams& params) override;

  std::string getReceiveAddress() const override { return m_address; }
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
