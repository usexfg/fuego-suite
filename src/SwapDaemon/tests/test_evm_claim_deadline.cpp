// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You can redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation. Fuego includes elements written
// by third parties. See file labeled LICENSE for more details.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

// EVM claim deadline: Bob does not publish t when the on-chain lock no longer
// leaves the claim runway before its timeout, against a scripted JSON-RPC node.

#include "Common/JsonValue.h"
#include "Common/StringTools.h"
#include "SwapDaemon/Ethereum/EthChainClient.h"
#include "SwapDaemon/Ethereum/EthRpcClient.h"
#include "SwapDaemon/SwapHashLock.h"
#include "SwapDaemon/SwapTimelock.h"
#include <HTTP/httplib.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

using namespace XfgSwap;

static int g_failures = 0;
static int g_checks = 0;

static void check(bool condition, const std::string& label) {
  ++g_checks;
  if (!condition) {
    ++g_failures;
    std::printf("FAIL: %s\n", label.c_str());
  }
}

static std::string word(uint64_t value) {
  std::ostringstream out;
  out << std::hex << value;
  const std::string digits = out.str();
  return std::string(64 - digits.size(), '0') + digits;
}

static std::string addressWord(const std::string& address) {
  return std::string(24, '0') + address.substr(2);
}

static const char* const REGISTRY = CANONICAL_HTLC_REGISTRY_ADDRESS;
static const char* const RECIPIENT = "0x00000000000000000000000000000000000000aa";
static const uint64_t AMOUNT = 1000000;

// A node whose HashedTimelock lock times out at `timeoutBlock`.
class ScriptedEvmNode {
public:
  ScriptedEvmNode(const std::string& hashLockHex) : m_hashLock(hashLockHex) {
    m_server.Post("/", [this](const httplib::Request& request, httplib::Response& response) {
      const Common::JsonValue call = Common::JsonValue::fromString(request.body);
      const std::string method = call("method").getString();
      std::string result = "null";
      if (method == "eth_blockNumber") result = "\"0x" + hexOf(tip.load()) + "\"";
      if (method == "eth_call") result = "\"0x" + contract() + "\"";
      response.set_content("{\"jsonrpc\":\"2.0\",\"id\":" + call("id").toString() + ",\"result\":" + result + "}",
                           "application/json");
    });
    m_port = m_server.bind_to_any_port("127.0.0.1");
    m_thread = std::thread([this] { m_server.listen_after_bind(); });
  }
  ~ScriptedEvmNode() {
    m_server.stop();
    if (m_thread.joinable()) m_thread.join();
  }
  std::string url() const { return "http://127.0.0.1:" + std::to_string(m_port); }

  std::atomic<uint64_t> tip{1000000};
  std::atomic<uint64_t> timeoutBlock{0};
  std::atomic<bool> claimed{false};

private:
  static std::string hexOf(uint64_t value) {
    std::ostringstream out;
    out << std::hex << value;
    return out.str();
  }
  std::string contract() const {
    return addressWord("0x00000000000000000000000000000000000000bb") + addressWord(RECIPIENT) + word(AMOUNT) +
           m_hashLock + word(timeoutBlock.load()) + word(claimed.load() ? 1 : 0) + word(0) + std::string(64, '0');
  }

  httplib::Server m_server;
  std::thread m_thread;
  int m_port = 0;
  std::string m_hashLock;
};

int main() {
  Crypto::SecretKey secret;
  std::memset(&secret, 0x5a, sizeof(secret));
  ScriptedEvmNode node(ethHashLockHex(secret));
  auto rpc = std::make_unique<EthRpcClient>(node.url(), 0);
  rpc->setHtlcRegistry(REGISTRY);
  EthChainClient bob(std::move(rpc), RECIPIENT, "ETH");

  SwapParams params{};
  params.pair = SwapPair::ETH;
  params.ctrLockTxId = std::string(64, 'c');
  params.ctrAmount = AtomicAmount(std::to_string(AMOUNT));
  params.ctrAddress = RECIPIENT;
  params.adaptorSecret = secret;

  const uint64_t runway = claimRunwayBlocks(SwapPair::ETH);
  node.timeoutBlock = node.tip + runway;
  check(bob.claimDeadlineError(params).empty(), "a lock with exactly the claim runway left may be claimed");
  node.tip = node.tip + 1;
  const std::string late = bob.claimDeadlineError(params);
  check(late.find("claim runway") != std::string::npos, "one block inside the runway, t is not published (" + late + ")");
  node.timeoutBlock = node.tip + runway + 100;
  node.claimed = true;
  check(!bob.claimDeadlineError(params).empty(), "a lock that is no longer open is not claimed");
  node.claimed = false;
  Crypto::SecretKey other;
  std::memset(&other, 0x5b, sizeof(other));
  params.adaptorSecret = other;
  check(!bob.claimDeadlineError(params).empty(), "a lock for another hash is not claimed");

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
