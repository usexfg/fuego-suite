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
//
// P2P message types for pool operations.
// Extends the SwapP2P message system with pool-specific messages.

#pragma once

#include "PoolTypes.h"
#include "Logging/LoggerRef.h"

#include <string>
#include <cstdint>
#include <vector>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <functional>

namespace XfgSwap {

enum class PoolMsgType : uint8_t {
  POOL_DEPOSIT_REQUEST   = 10,
  POOL_DEPOSIT_RESPONSE  = 11,
  POOL_DEPOSIT_CONFIRM   = 12,
  POOL_WITHDRAW_REQUEST  = 20,
  POOL_WITHDRAW_RESPONSE = 21,
  POOL_SWAP_ORDER        = 30,
  POOL_SWAP_EXECUTE      = 31,
  POOL_SWAP_CONFIRM      = 32,
  POOL_FEE_CLAIM         = 40,
  POOL_CHECKPOINT        = 50,
  POOL_STATE_REQUEST     = 60,
  POOL_STATE_RESPONSE    = 61,
  POOL_ERROR             = 0xFF
};

struct PoolMessage {
  PoolMsgType type;
  std::string poolId;     // hex-encoded pool identifier
  std::string payload;    // hex-encoded binary data
};

class PoolP2P {
public:
  PoolP2P(uint16_t listenPort, Logging::LoggerRef& logger);
  ~PoolP2P();

  // Bind and listen on the configured TCP port.
  bool start();

  // Close all connections and stop the accept thread.
  void stop();

  // Connect to a peer and send a pool protocol message.
  bool sendMessage(const std::string& peerEndpoint, const PoolMessage& msg);

  // Block until a message matching expectedType and poolId arrives,
  // or timeoutMs elapses. Returns true if a message was received.
  bool waitForMessage(PoolMsgType expectedType, const std::string& poolId,
                       PoolMessage& outMsg, uint32_t timeoutMs = 60000);

  // Register an asynchronous callback for every incoming message.
  void setMessageCallback(std::function<void(const PoolMessage&)> cb);

private:
  void acceptLoop();
  bool parseMessage(const std::vector<uint8_t>& data, PoolMessage& msg);
  std::vector<uint8_t> serializeMessage(const PoolMessage& msg);
  bool recvAll(int sock, uint8_t* buf, size_t len);
  bool readFramedMessage(int sock, PoolMessage& msg);

  int m_listenSocket;
  uint16_t m_listenPort;
  std::atomic<bool> m_running;
  std::thread m_acceptThread;

  std::deque<PoolMessage> m_pendingMessages;
  std::mutex m_mutex;
  std::condition_variable m_cv;

  std::function<void(const PoolMessage&)> m_callback;

  Logging::LoggerRef& m_logger;
};

} // namespace XfgSwap
