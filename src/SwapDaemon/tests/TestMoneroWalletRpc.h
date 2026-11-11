// Copyright (c) 2017-2026 Fuego Developers
//
// In-process test double for monero-wallet-rpc. Overrides the walletRpc seam
// to serve canned responses and record the (method, params) call sequence, so
// MoneroRpcClient's real verifyLock/sweepSharedAddress logic can be unit-tested
// without a live monerod. Named Test… per repo convention (not "mock").

#pragma once

#include "SwapDaemon/Monero/MoneroRpcClient.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace XfgSwap {

class TestMoneroWalletRpc : public MoneroRpcClient {
public:
  TestMoneroWalletRpc() : MoneroRpcClient("127.0.0.1", 0, "127.0.0.1", 0) {}

  // Ordered record of every wallet call.
  std::vector<std::pair<std::string, std::string>> calls;
  // Per-method FIFO of canned responses.
  std::map<std::string, std::vector<std::string>> queued;
  // Returned when a method has no queued response left.
  std::string defaultResp = "{\"result\":{}}";

  void queue(const std::string& method, const std::string& response) {
    queued[method].push_back(response);
  }

  std::vector<std::string> methodSeq() const {
    std::vector<std::string> s;
    s.reserve(calls.size());
    for (const auto& c : calls) s.push_back(c.first);
    return s;
  }

  std::string paramsFor(const std::string& method) const {
    for (const auto& c : calls) if (c.first == method) return c.second;
    return "";
  }

protected:
  std::string walletRpc(const std::string& method, const std::string& params) override {
    calls.emplace_back(method, params);
    auto it = queued.find(method);
    if (it != queued.end() && !it->second.empty()) {
      std::string r = it->second.front();
      it->second.erase(it->second.begin());
      return r;
    }
    return defaultResp;
  }

  // No real sleeping in unit tests.
  void syncPollDelay() override {}
};

} // namespace XfgSwap
