#pragma once

#include "SwapDaemon/IChainClient.h"

namespace XfgSwap {

class TestChainClient : public IChainClient {
public:
  explicit TestChainClient(const std::string& name) : m_name(name) {}

  std::string chainName() const override { return m_name; }

  ChainClientResult lock(const SwapParams&) override {
    ++lockCalls;
    return lockResult;
  }
  ChainClientResult verifyLock(const SwapParams&) override {
    ++verifyLockCalls;
    return verifyLockResult;
  }
  ChainClientResult claim(const SwapParams&) override {
    ++claimCalls;
    return claimResult;
  }
  ChainClientResult refund(const SwapParams&) override {
    ++refundCalls;
    return refundResult;
  }

  ChainClientResult lockResult       = ChainClientResult::ok("test_lock_tx");
  ChainClientResult verifyLockResult = ChainClientResult::ok("test_verify");
  ChainClientResult claimResult      = ChainClientResult::ok("test_claim_tx");
  ChainClientResult refundResult     = ChainClientResult::ok("test_refund_tx");

  int lockCalls = 0;
  int verifyLockCalls = 0;
  int claimCalls = 0;
  int refundCalls = 0;

private:
  std::string m_name;
};

} // namespace XfgSwap
