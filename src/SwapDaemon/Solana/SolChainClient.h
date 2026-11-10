#pragma once

#include "../IChainClient.h"
#include "SolRpcClient.h"
#include <memory>
#include <mutex>
#include <string>

namespace XfgSwap {

class SolChainClient : public IChainClient {
public:
  SolChainClient(std::unique_ptr<SolRpcClient> rpc, const std::string& keypairBase58);

  std::string chainName() const override { return "SOL"; }
  std::string readinessError() override;
  bool supportsPtlc() const override { return false; }
  // Point locks (CROSS_CURVE_PLAN X4) need the configured program to carry
  // lock_ptlc and claim_ptlc; a program without them keeps the hash lock and
  // an unreadable one blocks new swaps.
  bool supportsPurePtlc() const override { return pointLockSupport() == PointLockSupport::AVAILABLE; }
  PointLockSupport pointLockSupport() const override;
  std::string claimDeadlineError(const SwapParams& params) override;
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
  std::string openPointEscrowFor(const SwapParams& params, const std::string& pointHex);
  ChainClientResult verifyPointLock(const SwapParams& params);
  ChainClientResult claimPoint(const SwapParams& params);
  bool escrowOpenThroughRunway(const SolHtlcInfo& info, const SwapParams& params, std::string& error);

  std::unique_ptr<SolRpcClient> m_rpc;
  std::string m_keypairBase58;
  mutable std::mutex m_pointLockSupportMutex;
  mutable bool m_pointLockSupportKnown = false;
  mutable PointLockSupport m_pointLockSupport = PointLockSupport::UNKNOWN;
};

} // namespace XfgSwap
