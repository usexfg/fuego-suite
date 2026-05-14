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

#include "PoolP2P.h"
#include "Logging/LoggerRef.h"

#include <cstring>
#include <algorithm>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>

namespace XfgSwap {

PoolP2P::PoolP2P(uint16_t listenPort, Logging::LoggerRef& logger)
    : m_listenSocket(-1)
    , m_listenPort(listenPort)
    , m_running(false)
    , m_logger(logger) {}

PoolP2P::~PoolP2P() {
  stop();
}

bool PoolP2P::start() {
  m_listenSocket = socket(AF_INET, SOCK_STREAM, 0);
  if (m_listenSocket < 0) {
    return false;
  }

  int opt = 1;
  setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(m_listenPort);

  if (bind(m_listenSocket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    close(m_listenSocket);
    m_listenSocket = -1;
    return false;
  }

  if (listen(m_listenSocket, 16) < 0) {
    close(m_listenSocket);
    m_listenSocket = -1;
    return false;
  }

  m_running = true;
  m_acceptThread = std::thread(&PoolP2P::acceptLoop, this);

  return true;
}

void PoolP2P::stop() {
  if (!m_running.exchange(false)) {
    return;
  }

  if (m_listenSocket >= 0) {
    close(m_listenSocket);
    m_listenSocket = -1;
  }

  if (m_acceptThread.joinable()) {
    m_acceptThread.join();
  }
}

bool PoolP2P::sendMessage(const std::string& peerEndpoint, const PoolMessage& msg) {
  std::string host = peerEndpoint;
  uint16_t port = m_listenPort;

  size_t colon = peerEndpoint.find(':');
  if (colon != std::string::npos) {
    host = peerEndpoint.substr(0, colon);
    port = static_cast<uint16_t>(std::stoi(peerEndpoint.substr(colon + 1)));
  }

  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    return false;
  }

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = inet_addr(host.c_str());
  addr.sin_port = htons(port);

  if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    close(sock);
    return false;
  }

  std::vector<uint8_t> data = serializeMessage(msg);
  size_t sent = 0;
  while (sent < data.size()) {
    ssize_t n = send(sock, data.data() + sent, data.size() - sent, 0);
    if (n <= 0) {
      close(sock);
      return false;
    }
    sent += n;
  }

  close(sock);
  return true;
}

bool PoolP2P::waitForMessage(PoolMsgType expectedType, const std::string& poolId,
                              PoolMessage& outMsg, uint32_t timeoutMs) {
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

  std::unique_lock<std::mutex> lock(m_mutex);
  while (true) {
    for (auto it = m_pendingMessages.begin(); it != m_pendingMessages.end(); ++it) {
      if (it->type == expectedType && it->poolId == poolId) {
        outMsg = *it;
        m_pendingMessages.erase(it);
        return true;
      }
    }

    if (m_cv.wait_until(lock, deadline) == std::cv_status::timeout) {
      break;
    }
  }

  return false;
}

void PoolP2P::setMessageCallback(std::function<void(const PoolMessage&)> cb) {
  m_callback = std::move(cb);
}

void PoolP2P::acceptLoop() {
  while (m_running) {
    struct sockaddr_in clientAddr = {};
    socklen_t clientLen = sizeof(clientAddr);

    int clientSock = accept(m_listenSocket, (struct sockaddr*)&clientAddr, &clientLen);
    if (clientSock < 0) {
      if (!m_running) break;
      continue;
    }

    PoolMessage msg;
    if (readFramedMessage(clientSock, msg)) {
      std::unique_lock<std::mutex> lock(m_mutex);
      m_pendingMessages.push_back(msg);
      lock.unlock();
      m_cv.notify_all();

      if (m_callback) {
        m_callback(msg);
      }
    }

    close(clientSock);
  }
}

bool PoolP2P::parseMessage(const std::vector<uint8_t>& data, PoolMessage& msg) {
  if (data.size() < 1) {
    return false;
  }

  msg.type = static_cast<PoolMsgType>(data[0]);

  if (data.size() < 5) {
    return false;
  }

  uint32_t idLen = (data[1] << 24) | (data[2] << 16) | (data[3] << 8) | data[4];
  if (data.size() < 5 + idLen) {
    return false;
  }

  msg.poolId.assign(reinterpret_cast<const char*>(data.data() + 5), idLen);
  msg.payload.assign(reinterpret_cast<const char*>(data.data() + 5 + idLen),
                      data.size() - 5 - idLen);

  return true;
}

std::vector<uint8_t> PoolP2P::serializeMessage(const PoolMessage& msg) {
  std::vector<uint8_t> data;
  data.reserve(5 + msg.poolId.size() + msg.payload.size());

  data.push_back(static_cast<uint8_t>(msg.type));

  uint32_t idLen = static_cast<uint32_t>(msg.poolId.size());
  data.push_back((idLen >> 24) & 0xFF);
  data.push_back((idLen >> 16) & 0xFF);
  data.push_back((idLen >> 8) & 0xFF);
  data.push_back(idLen & 0xFF);

  data.insert(data.end(), msg.poolId.begin(), msg.poolId.end());
  data.insert(data.end(), msg.payload.begin(), msg.payload.end());

  return data;
}

bool PoolP2P::recvAll(int sock, uint8_t* buf, size_t len) {
  size_t received = 0;
  while (received < len) {
    ssize_t n = recv(sock, buf + received, len - received, 0);
    if (n <= 0) {
      return false;
    }
    received += n;
  }
  return true;
}

bool PoolP2P::readFramedMessage(int sock, PoolMessage& msg) {
  uint8_t lenBuf[4];
  if (!recvAll(sock, lenBuf, 4)) {
    return false;
  }

  uint32_t msgLen = (lenBuf[0] << 24) | (lenBuf[1] << 16) | (lenBuf[2] << 8) | lenBuf[3];
  if (msgLen == 0 || msgLen > 10 * 1024 * 1024) {
    return false;
  }

  std::vector<uint8_t> data(msgLen);
  if (!recvAll(sock, data.data(), msgLen)) {
    return false;
  }

  return parseMessage(data, msg);
}

} // namespace XfgSwap
