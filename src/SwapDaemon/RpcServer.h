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

#pragma once

#include <string>
#include <cstdint>
#include <thread>
#include <atomic>
#include <memory>
#include "../Logging/LoggerRef.h"

// Forward-declare httplib to avoid pulling in the massive header.
namespace httplib { class Server; class Request; }

namespace XfgSwap {

class SwapDaemon;

// Lightweight JSON-RPC server for the Flutter wallet.
// Binds to 127.0.0.1 and exposes swap management methods.
//
// Methods:
//   initiate_swap  {pair, xfg_amount, ctr_amount, peer, expected_peer_pubkey}
//                  → {swap_id, our_swap_pubkey}
//   list_swaps     {}                                     → {swaps: [...]}
//   swap_status    {swap_id}                              → {swap: {...}}
//   refund         {swap_id}                              → {success}
//   check_timeouts {}                                     → {refunded: [...]}
class RpcServer {
public:
  // controlToken: when non-empty, every mutating RPC requires header
  // X-Swap-Token: <token> (or Authorization: Bearer <token>).
  RpcServer(SwapDaemon& daemon, Logging::ILogger& logger,
            const std::string& controlToken = {});
  ~RpcServer();

  // Start listening on the given port (127.0.0.1 only).
  // Returns false if the port is already in use.
  bool start(uint16_t port);

  // Stop the server and join the thread.
  void stop();

  bool isRunning() const { return m_running.load(); }

private:
  void registerRoutes();
  bool authorize(const httplib::Request& req) const;

  // JSON-RPC dispatcher: parses body, routes to handler, returns JSON response.
  std::string dispatch(const std::string& body);

  // Individual method handlers — return the "result" portion as a JSON string.
  std::string handleInitiateSwap(const std::string& params);
  std::string handleListSwaps(const std::string& params);
  std::string handleSwapStatus(const std::string& params);
  std::string handleRefund(const std::string& params);
  std::string handleCheckTimeouts(const std::string& params);

  // Build a JSON-RPC error response.
  static std::string rpcError(int code, const std::string& message, int id = 0);
  static std::string rpcSuccess(const std::string& result, int id = 0);

  SwapDaemon& m_daemon;
  Logging::LoggerRef m_logger;
  std::string m_controlToken;
  std::unique_ptr<httplib::Server> m_server;
  std::thread m_thread;
  std::atomic<bool> m_running{false};
};

} // namespace XfgSwap
