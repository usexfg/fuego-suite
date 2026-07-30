// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even
// the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You can redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation. Fuego includes elements written
// by third parties. See file labeled LICENSE for more details.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

#include "RpcServer.h"
#include "SwapDaemon.h"
#include "SwapTypes.h"
#include "Common/JsonValue.h"
#include "Common/StringTools.h"
#include <HTTP/httplib.h>
#include <sstream>
#include <mutex>

namespace XfgSwap {

RpcServer::RpcServer(SwapDaemon& daemon, Logging::ILogger& logger)
  : m_daemon(daemon)
  , m_logger(logger, "RpcServer") {
}

RpcServer::~RpcServer() {
  stop();
}

bool RpcServer::start(uint16_t port) {
  m_server = std::make_unique<httplib::Server>();
  registerRoutes();

  // Bind to loopback only
  if (!m_server->bind_to_port("127.0.0.1", port)) {
    m_logger(Logging::ERROR) << "Failed to bind RPC server to port " << port;
    return false;
  }

  m_running = true;
  m_thread = std::thread([this, port]() {
    m_logger(Logging::INFO) << "JSON-RPC server listening on 127.0.0.1:" << port;
    m_server->listen_after_bind();
    m_running = false;
  });

  // Give the server a moment to start
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  return m_running.load();
}

void RpcServer::stop() {
  if (m_server) {
    m_server->stop();
  }
  if (m_thread.joinable()) {
    m_thread.join();
  }
  m_running = false;
}

void RpcServer::registerRoutes() {
  m_server->Post("/", [this](const httplib::Request& req, httplib::Response& res) {
    std::string response = dispatch(req.body);
    res.set_content(response, "application/json");
    res.set_header("Access-Control-Allow-Origin", "*");
  });

  m_server->Options(".*", [](const httplib::Request&, httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type");
    res.status = 204;
  });

  // Health check
  m_server->Get("/health", [](const httplib::Request&, httplib::Response& res) {
    res.set_content(R"({"status":"ok"})", "application/json");
    res.set_header("Access-Control-Allow-Origin", "*");
  });
}

std::string RpcServer::dispatch(const std::string& body) {
  int id = 0;
  std::string method;
  std::string params;

  try {
    auto root = Common::JsonValue::fromString(body);
    if (!root.isObject()) {
      return rpcError(-32600, "Invalid Request");
    }

    if (root.contains("id")) {
      id = static_cast<int>(root("id").getInteger());
    }

    if (!root.contains("method") || !root("method").isString()) {
      return rpcError(-32600, "Missing method", id);
    }
    method = root("method").getString();

    if (root.contains("params")) {
      params = root("params").toString();
    } else {
      params = "{}";
    }
  } catch (...) {
    return rpcError(-32700, "Parse error", id);
  }

  m_logger(Logging::INFO) << "RPC method: " << method;

  std::string result;
  if (method == "initiate_swap") {
    result = handleInitiateSwap(params);
  } else if (method == "list_swaps") {
    result = handleListSwaps(params);
  } else if (method == "swap_status") {
    result = handleSwapStatus(params);
  } else if (method == "refund") {
    result = handleRefund(params);
  } else if (method == "check_timeouts") {
    result = handleCheckTimeouts(params);
  } else {
    return rpcError(-32601, "Method not found: " + method, id);
  }

  // If the handler returned an error (starts with '{"error"'), pass through
  if (result.find("\"error\"") != std::string::npos) {
    return result;
  }

  return rpcSuccess(result, id);
}

std::string RpcServer::handleInitiateSwap(const std::string& params) {
  try {
    auto root = Common::JsonValue::fromString(params);
    if (!root.isObject()) {
      return rpcError(-32602, "Invalid params");
    }

    SwapPair pair = SwapPair::SOL;
    uint64_t xfgAmount = 0;
    uint64_t ctrAmount = 0;
    std::string peer;

    if (root.contains("pair")) {
      std::string pairStr = root("pair").getString();
      if (!swapPairFromString(pairStr, pair)) {
        return rpcError(-32602, "Invalid pair: " + pairStr);
      }
    }
    if (root.contains("xfg_amount")) {
      xfgAmount = static_cast<uint64_t>(root("xfg_amount").getInteger());
    }
    if (root.contains("ctr_amount")) {
      ctrAmount = static_cast<uint64_t>(root("ctr_amount").getInteger());
    }
    if (root.contains("peer")) {
      peer = root("peer").getString();
    }

    if (xfgAmount == 0) {
      return rpcError(-32602, "xfg_amount must be > 0");
    }
    if (ctrAmount == 0) {
      return rpcError(-32602, "ctr_amount must be > 0");
    }
    if (peer.empty()) {
      return rpcError(-32602, "peer endpoint is required");
    }

    SwapParams swapParams;
    swapParams.pair = pair;
    swapParams.role = SwapRole::BOB;
    swapParams.xfgAmount = xfgAmount;
    swapParams.ctrAmount = ctrAmount;
    swapParams.peerEndpoint = peer;

    // Zero-init crypto fields
    std::memset(&swapParams.aliceXfgPubKey, 0, sizeof(swapParams.aliceXfgPubKey));
    std::memset(&swapParams.bobXfgPubKey, 0, sizeof(swapParams.bobXfgPubKey));
    std::memset(&swapParams.ourSwapSecKey, 0, sizeof(swapParams.ourSwapSecKey));
    std::memset(&swapParams.ourSwapPubKey, 0, sizeof(swapParams.ourSwapPubKey));
    std::memset(&swapParams.peerSwapPubKey, 0, sizeof(swapParams.peerSwapPubKey));
    std::memset(&swapParams.escrowPubKey, 0, sizeof(swapParams.escrowPubKey));
    std::memset(&swapParams.adaptorPoint, 0, sizeof(swapParams.adaptorPoint));
    std::memset(&swapParams.adaptorSecret, 0, sizeof(swapParams.adaptorSecret));
    std::memset(&swapParams.adaptorDleqQ, 0, sizeof(swapParams.adaptorDleqQ));
    std::memset(&swapParams.escrowTxHash, 0, sizeof(swapParams.escrowTxHash));
    std::memset(&swapParams.hashLock, 0, sizeof(swapParams.hashLock));
    std::memset(&swapParams.preimage, 0, sizeof(swapParams.preimage));
    swapParams.xfgTimeoutHeight = 0;
    swapParams.ctrTimeoutBlock = 0;
    swapParams.escrowOutputIndex = 0;
    swapParams.htlcOutputIndex = 0;

    if (!m_daemon.initiate(swapParams)) {
      return rpcError(-32000, "Failed to initiate swap");
    }

    std::ostringstream oss;
    oss << R"({"swap_id": ")" << swapParams.swapId << R"("})";
    return oss.str();
  } catch (const std::exception& e) {
    return rpcError(-32602, std::string("Parameter error: ") + e.what());
  }
}

std::string RpcServer::handleListSwaps(const std::string& /*params*/) {
  auto ids = m_daemon.database().listSwaps();

  std::ostringstream oss;
  oss << R"({"swaps": [)";
  bool first = true;
  for (const auto& id : ids) {
    SwapStateMachine sm;
    if (m_daemon.database().loadSwap(id, sm)) {
      if (!first) oss << ",";
      first = false;
      sm.setEncryptionKey("");
      oss << sm.serialize();
    }
  }
  oss << R"(]})";
  return oss.str();
}

std::string RpcServer::handleSwapStatus(const std::string& params) {
  try {
    auto root = Common::JsonValue::fromString(params);
    if (!root.isObject() || !root.contains("swap_id")) {
      return rpcError(-32602, "Missing swap_id");
    }
    std::string swapId = root("swap_id").getString();

    SwapStateMachine sm;
    if (!m_daemon.database().loadSwap(swapId, sm)) {
      return rpcError(-32000, "Swap not found: " + swapId);
    }

    sm.setEncryptionKey("");
    return R"({"swap": )" + sm.serialize() + "}";
  } catch (const std::exception& e) {
    return rpcError(-32602, std::string("Parameter error: ") + e.what());
  }
}

std::string RpcServer::handleRefund(const std::string& params) {
  try {
    auto root = Common::JsonValue::fromString(params);
    if (!root.isObject() || !root.contains("swap_id")) {
      return rpcError(-32602, "Missing swap_id");
    }
    std::string swapId = root("swap_id").getString();

    if (!m_daemon.refund(swapId)) {
      return rpcError(-32000, "Refund failed for swap: " + swapId);
    }

    return R"({"success": true, "swap_id": ")" + swapId + R"("})";
  } catch (const std::exception& e) {
    return rpcError(-32602, std::string("Parameter error: ") + e.what());
  }
}

std::string RpcServer::handleCheckTimeouts(const std::string& /*params*/) {
  // Process all active swaps and refund any that timed out
  auto ids = m_daemon.database().listSwaps();
  std::vector<std::string> refunded;

  for (const auto& id : ids) {
    SwapStateMachine sm;
    if (m_daemon.database().loadSwap(id, sm)) {
      if (!sm.isTerminal()) {
        m_daemon.processSwap(sm);
        // Check if it transitioned to REFUNDED
        if (sm.currentState() == SwapState::ADAPTOR_REFUNDED ||
            sm.currentState() == SwapState::XFG_REFUNDED) {
          refunded.push_back(id);
        }
      }
    }
  }

  // Also run the dedicated timeout check
  m_daemon.checkTimeouts();

  std::ostringstream oss;
  oss << R"({"processed": )" << ids.size()
      << R"(, "refunded": [)";
  bool first = true;
  for (const auto& id : refunded) {
    if (!first) oss << ",";
    first = false;
    oss << R"(")" << id << R"(")";
  }
  oss << R"(]})";
  return oss.str();
}

std::string RpcServer::rpcError(int code, const std::string& message, int id) {
  std::ostringstream oss;
  oss << R"({"jsonrpc":"2.0","error":{"code":)" << code
      << R"(,"message":")" << message << R"("},"id":)" << id << "}";
  return oss.str();
}

std::string RpcServer::rpcSuccess(const std::string& result, int id) {
  std::ostringstream oss;
  oss << R"({"jsonrpc":"2.0","result":)" << result
      << R"(,"id":)" << id << "}";
  return oss.str();
}

} // namespace XfgSwap
