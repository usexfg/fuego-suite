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

#pragma once

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

enum class SwapMsgType : uint8_t {
  KEY_EXCHANGE   = 0,
  NONCE_EXCHANGE = 1,
  PRESIG_EXCHANGE = 2,
  SECRET_REVEAL  = 3,
  ERROR          = 0xFF
};

struct SwapMessage {
  SwapMsgType type;
  std::string swapId;   // hex swap identifier
  std::string payload;  // hex-encoded binary data
};

class SwapP2P {
public:
  SwapP2P(uint16_t listenPort, Logging::LoggerRef& logger);
  ~SwapP2P();

  // Bind and listen on the configured TCP port.
  bool start();

  // Close all connections and stop the accept thread.
  void stop();

  // Connect to a peer and send a swap protocol message.
  // peerEndpoint is "host:port".
  bool sendMessage(const std::string& peerEndpoint, const SwapMessage& msg);

  // Block until a message matching expectedType and swapId arrives,
  // or timeoutMs elapses.  Returns true if a message was received.
  bool waitForMessage(SwapMsgType expectedType, const std::string& swapId,
                      SwapMessage& outMsg, uint32_t timeoutMs = 60000);

  // Register an asynchronous callback for every incoming message.
  void setMessageCallback(std::function<void(const SwapMessage&)> cb);

private:
  // Accept incoming connections in a dedicated thread.
  void acceptLoop();

  // Deserialize raw bytes into a SwapMessage.
  bool parseMessage(const std::vector<uint8_t>& data, SwapMessage& msg);

  // Serialize a SwapMessage to wire format bytes.
  std::vector<uint8_t> serializeMessage(const SwapMessage& msg);

  // Read exactly `len` bytes from a socket, handling partial reads.
  bool recvAll(int sock, uint8_t* buf, size_t len);

  // Read one complete framed message from a connected socket.
  bool readFramedMessage(int sock, SwapMessage& msg);

  // Wire format:
  //   [4 bytes big-endian length of remaining data]
  //   [1 byte  SwapMsgType]
  //   [4 bytes big-endian swapId length]
  //   [N bytes swapId (UTF-8)]
  //   [remaining bytes: payload]

  int m_listenSocket;
  uint16_t m_listenPort;
  std::atomic<bool> m_running;
  std::thread m_acceptThread;

  std::deque<SwapMessage> m_pendingMessages;
  std::mutex m_mutex;
  std::condition_variable m_cv;

  std::function<void(const SwapMessage&)> m_callback;

  Logging::LoggerRef& m_logger;
};

} // namespace XfgSwap
