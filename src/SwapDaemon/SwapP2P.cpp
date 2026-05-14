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

#include "SwapP2P.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <cstring>
#include <chrono>
#include <algorithm>

namespace XfgSwap {

// ---------------------------------------------------------------------------
// Helpers for big-endian 32-bit integer encoding (wire format)
// ---------------------------------------------------------------------------

static void writeBE32(uint8_t* dst, uint32_t val) {
  dst[0] = static_cast<uint8_t>((val >> 24) & 0xFF);
  dst[1] = static_cast<uint8_t>((val >> 16) & 0xFF);
  dst[2] = static_cast<uint8_t>((val >>  8) & 0xFF);
  dst[3] = static_cast<uint8_t>((val      ) & 0xFF);
}

static uint32_t readBE32(const uint8_t* src) {
  return (static_cast<uint32_t>(src[0]) << 24) |
         (static_cast<uint32_t>(src[1]) << 16) |
         (static_cast<uint32_t>(src[2]) <<  8) |
         (static_cast<uint32_t>(src[3]));
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

SwapP2P::SwapP2P(uint16_t listenPort, Logging::LoggerRef& logger)
  : m_listenSocket(-1)
  , m_listenPort(listenPort)
  , m_running(false)
  , m_logger(logger) {
}

SwapP2P::~SwapP2P() {
  stop();
}

// ---------------------------------------------------------------------------
// start / stop
// ---------------------------------------------------------------------------

bool SwapP2P::start() {
  m_listenSocket = socket(AF_INET, SOCK_STREAM, 0);
  if (m_listenSocket < 0) {
    m_logger(Logging::ERROR) << "SwapP2P: failed to create listen socket";
    return false;
  }

  int optval = 1;
  setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

  struct sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(m_listenPort);

  if (bind(m_listenSocket, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
    m_logger(Logging::ERROR) << "SwapP2P: bind failed on port " << m_listenPort;
    close(m_listenSocket);
    m_listenSocket = -1;
    return false;
  }

  if (listen(m_listenSocket, 5) < 0) {
    m_logger(Logging::ERROR) << "SwapP2P: listen failed on port " << m_listenPort;
    close(m_listenSocket);
    m_listenSocket = -1;
    return false;
  }

  m_running = true;
  m_acceptThread = std::thread(&SwapP2P::acceptLoop, this);

  m_logger(Logging::INFO) << "SwapP2P: listening on port " << m_listenPort;
  return true;
}

void SwapP2P::stop() {
  if (!m_running.exchange(false)) {
    return;
  }

  if (m_listenSocket >= 0) {
    shutdown(m_listenSocket, SHUT_RDWR);
    close(m_listenSocket);
    m_listenSocket = -1;
  }

  m_cv.notify_all();

  if (m_acceptThread.joinable()) {
    m_acceptThread.join();
  }

  m_logger(Logging::INFO) << "SwapP2P: stopped";
}

// ---------------------------------------------------------------------------
// acceptLoop — runs in its own thread
// ---------------------------------------------------------------------------

void SwapP2P::acceptLoop() {
  while (m_running) {
    struct sockaddr_in peerAddr;
    socklen_t peerLen = sizeof(peerAddr);

    int clientSock = accept(m_listenSocket,
                            reinterpret_cast<struct sockaddr*>(&peerAddr),
                            &peerLen);
    if (clientSock < 0) {
      if (m_running) {
        m_logger(Logging::WARNING) << "SwapP2P: accept() failed, errno=" << errno;
      }
      continue;
    }

    // Set a receive timeout so we don't block forever on a misbehaving peer.
    struct timeval tv;
    tv.tv_sec = 30;
    tv.tv_usec = 0;
    setsockopt(clientSock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    SwapMessage msg;
    if (readFramedMessage(clientSock, msg)) {
      char peerIp[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &peerAddr.sin_addr, peerIp, sizeof(peerIp));
      m_logger(Logging::DEBUGGING) << "SwapP2P: received msg type="
        << static_cast<int>(msg.type) << " swapId=" << msg.swapId
        << " from " << peerIp << ":" << ntohs(peerAddr.sin_port);

      // Deliver to callback if registered.
      std::function<void(const SwapMessage&)> cb;
      {
        std::lock_guard<std::mutex> lock(m_mutex);
        cb = m_callback;
      }
      if (cb) {
        cb(msg);
      }

      // Always enqueue for waitForMessage callers.
      {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pendingMessages.push_back(msg);
      }
      m_cv.notify_all();
    } else {
      m_logger(Logging::WARNING) << "SwapP2P: failed to read message from peer";
    }

    close(clientSock);
  }
}

// ---------------------------------------------------------------------------
// sendMessage — connect to peer, send one framed message, close
// ---------------------------------------------------------------------------

bool SwapP2P::sendMessage(const std::string& peerEndpoint, const SwapMessage& msg) {
  // Parse "host:port"
  size_t colonPos = peerEndpoint.rfind(':');
  if (colonPos == std::string::npos || colonPos == 0 || colonPos == peerEndpoint.size() - 1) {
    m_logger(Logging::ERROR) << "SwapP2P: invalid peer endpoint: " << peerEndpoint;
    return false;
  }

  std::string host = peerEndpoint.substr(0, colonPos);
  std::string portStr = peerEndpoint.substr(colonPos + 1);

  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    m_logger(Logging::ERROR) << "SwapP2P: failed to create socket for send";
    return false;
  }

  // Connect timeout via SO_SNDTIMEO
  struct timeval tv;
  tv.tv_sec = 10;
  tv.tv_usec = 0;
  setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  // Resolve host
  struct addrinfo hints, *result;
  std::memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  int gai = getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result);
  if (gai != 0) {
    m_logger(Logging::ERROR) << "SwapP2P: cannot resolve " << host;
    close(sock);
    return false;
  }

  int ret = connect(sock, result->ai_addr, result->ai_addrlen);
  freeaddrinfo(result);
  if (ret < 0) {
    m_logger(Logging::ERROR) << "SwapP2P: connect failed to " << peerEndpoint;
    close(sock);
    return false;
  }

  std::vector<uint8_t> wire = serializeMessage(msg);
  const uint8_t* ptr = wire.data();
  size_t remaining = wire.size();

  while (remaining > 0) {
    ssize_t sent = send(sock, ptr, remaining, MSG_NOSIGNAL);
    if (sent <= 0) {
      m_logger(Logging::ERROR) << "SwapP2P: send failed to " << peerEndpoint;
      close(sock);
      return false;
    }
    ptr += sent;
    remaining -= static_cast<size_t>(sent);
  }

  close(sock);

  m_logger(Logging::DEBUGGING) << "SwapP2P: sent msg type="
    << static_cast<int>(msg.type) << " swapId=" << msg.swapId
    << " to " << peerEndpoint;

  return true;
}

// ---------------------------------------------------------------------------
// waitForMessage — blocking wait with timeout
// ---------------------------------------------------------------------------

bool SwapP2P::waitForMessage(SwapMsgType expectedType, const std::string& swapId,
                             SwapMessage& outMsg, uint32_t timeoutMs) {
  auto deadline = std::chrono::steady_clock::now()
                + std::chrono::milliseconds(timeoutMs);

  std::unique_lock<std::mutex> lock(m_mutex);

  while (true) {
    // Scan the pending queue for a matching message.
    for (auto it = m_pendingMessages.begin(); it != m_pendingMessages.end(); ++it) {
      if (it->type == expectedType && it->swapId == swapId) {
        outMsg = *it;
        m_pendingMessages.erase(it);
        return true;
      }
    }

    // Wait until notified or timeout.
    if (m_cv.wait_until(lock, deadline) == std::cv_status::timeout) {
      m_logger(Logging::WARNING) << "SwapP2P: waitForMessage timed out after "
        << timeoutMs << "ms (type=" << static_cast<int>(expectedType)
        << " swapId=" << swapId << ")";
      return false;
    }

    if (!m_running) {
      return false;
    }
  }
}

// ---------------------------------------------------------------------------
// setMessageCallback
// ---------------------------------------------------------------------------

void SwapP2P::setMessageCallback(std::function<void(const SwapMessage&)> cb) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_callback = std::move(cb);
}

// ---------------------------------------------------------------------------
// Wire format serialization
// ---------------------------------------------------------------------------

std::vector<uint8_t> SwapP2P::serializeMessage(const SwapMessage& msg) {
  // body = [1 type][4 swapId-len][swapId bytes][payload bytes]
  uint32_t swapIdLen = static_cast<uint32_t>(msg.swapId.size());
  uint32_t bodyLen = 1 + 4 + swapIdLen + static_cast<uint32_t>(msg.payload.size());

  std::vector<uint8_t> out(4 + bodyLen);
  uint8_t* p = out.data();

  // Frame length (excludes the 4-byte length prefix itself)
  writeBE32(p, bodyLen);
  p += 4;

  // Message type
  *p++ = static_cast<uint8_t>(msg.type);

  // Swap ID length + data
  writeBE32(p, swapIdLen);
  p += 4;
  std::memcpy(p, msg.swapId.data(), swapIdLen);
  p += swapIdLen;

  // Payload
  std::memcpy(p, msg.payload.data(), msg.payload.size());

  return out;
}

// ---------------------------------------------------------------------------
// Wire format deserialization
// ---------------------------------------------------------------------------

bool SwapP2P::parseMessage(const std::vector<uint8_t>& data, SwapMessage& msg) {
  // Minimum: 1 (type) + 4 (swapId length) = 5 bytes
  if (data.size() < 5) {
    return false;
  }

  const uint8_t* p = data.data();
  size_t remaining = data.size();

  // Type
  uint8_t rawType = *p++;
  remaining--;

  if (rawType > static_cast<uint8_t>(SwapMsgType::SECRET_REVEAL) &&
      rawType != static_cast<uint8_t>(SwapMsgType::ERROR)) {
    return false;
  }
  msg.type = static_cast<SwapMsgType>(rawType);

  // Swap ID length
  if (remaining < 4) return false;
  uint32_t swapIdLen = readBE32(p);
  p += 4;
  remaining -= 4;

  // Sanity: swap IDs are hex hashes, never more than 256 bytes
  if (swapIdLen > 256 || swapIdLen > remaining) {
    return false;
  }

  msg.swapId.assign(reinterpret_cast<const char*>(p), swapIdLen);
  p += swapIdLen;
  remaining -= swapIdLen;

  // Remaining bytes are payload
  msg.payload.assign(reinterpret_cast<const char*>(p), remaining);

  return true;
}

// ---------------------------------------------------------------------------
// Low-level socket helpers
// ---------------------------------------------------------------------------

bool SwapP2P::recvAll(int sock, uint8_t* buf, size_t len) {
  size_t received = 0;
  while (received < len) {
    ssize_t n = recv(sock, buf + received, len - received, 0);
    if (n <= 0) {
      return false;
    }
    received += static_cast<size_t>(n);
  }
  return true;
}

bool SwapP2P::readFramedMessage(int sock, SwapMessage& msg) {
  // Read 4-byte length prefix
  uint8_t lenBuf[4];
  if (!recvAll(sock, lenBuf, 4)) {
    return false;
  }

  uint32_t bodyLen = readBE32(lenBuf);

  // Sanity: reject messages larger than 1 MB
  if (bodyLen == 0 || bodyLen > 1048576) {
    m_logger(Logging::WARNING) << "SwapP2P: rejecting message with body length " << bodyLen;
    return false;
  }

  std::vector<uint8_t> body(bodyLen);
  if (!recvAll(sock, body.data(), bodyLen)) {
    return false;
  }

  return parseMessage(body, msg);
}

} // namespace XfgSwap
