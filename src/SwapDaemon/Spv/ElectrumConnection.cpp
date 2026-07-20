// Copyright (c) 2017-2026 Fuego Developers
//
// TCP JSON-RPC client for the Electrum protocol.

#include "SwapDaemon/Spv/ElectrumConnection.h"

#include "Common/JsonValue.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <sstream>

namespace XfgSwap {

ElectrumConnection::ElectrumConnection() {}

ElectrumConnection::~ElectrumConnection() {
  disconnect();
}

bool ElectrumConnection::connect(const std::string& host, uint16_t port) {
  disconnect();

  struct addrinfo hints {};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  std::string portStr = std::to_string(port);
  struct addrinfo* res = nullptr;
  int rc = getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res);
  if (rc != 0 || !res) {
    return false;
  }

  m_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (m_fd < 0) {
    freeaddrinfo(res);
    return false;
  }

  // Set non-blocking for connect timeout
  int flags = fcntl(m_fd, F_GETFL, 0);
  fcntl(m_fd, F_SETFL, flags | O_NONBLOCK);

  rc = ::connect(m_fd, res->ai_addr, res->ai_addrlen);
  freeaddrinfo(res);

  if (rc < 0 && errno != EINPROGRESS) {
    ::close(m_fd);
    m_fd = -1;
    return false;
  }

  if (rc < 0) {
    // EINPROGRESS — wait with poll
    struct pollfd pfd {};
    pfd.fd = m_fd;
    pfd.events = POLLOUT;
    int pollRc = poll(&pfd, 1, static_cast<int>(m_connectTimeout * 1000));
    if (pollRc <= 0) {
      ::close(m_fd);
      m_fd = -1;
      return false;
    }
    int sockErr = 0;
    socklen_t errLen = sizeof(sockErr);
    getsockopt(m_fd, SOL_SOCKET, SO_ERROR, &sockErr, &errLen);
    if (sockErr != 0) {
      ::close(m_fd);
      m_fd = -1;
      return false;
    }
  }

  // Restore blocking mode
  fcntl(m_fd, F_SETFL, flags);

  // Set read/write timeouts
  struct timeval tv {};
  tv.tv_sec = m_readTimeout;
  setsockopt(m_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(m_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  return true;
}

void ElectrumConnection::disconnect() {
  if (m_fd >= 0) {
    ::shutdown(m_fd, SHUT_RDWR);
    ::close(m_fd);
    m_fd = -1;
  }
}

bool ElectrumConnection::isConnected() const {
  return m_fd >= 0;
}

std::string ElectrumConnection::call(const std::string& method, const std::string& paramsJson) {
  if (m_fd < 0) {
    return "";
  }

  ++m_callId;

  // Build JSON-RPC request
  std::ostringstream req;
  req << "{\"jsonrpc\":\"2.0\",\"id\":" << m_callId
      << ",\"method\":\"" << method << "\""
      << ",\"params\":" << paramsJson << "}\n";

  std::string request = req.str();

  // Send request
  size_t sent = 0;
  while (sent < request.size()) {
    ssize_t n = ::send(m_fd, request.data() + sent, request.size() - sent, MSG_NOSIGNAL);
    if (n < 0) {
      return "";
    }
    sent += static_cast<size_t>(n);
  }

  // Read response until newline (with 1MB size limit to prevent OOM)
  static constexpr size_t MAX_RESPONSE_SIZE = 1024 * 1024;
  std::string response;
  while (true) {
    char buf[4096];
    ssize_t n = ::recv(m_fd, buf, sizeof(buf) - 1, 0);
    if (n < 0) {
      return "";
    }
    if (n == 0) {
      // Connection closed
      return "";
    }
    buf[n] = '\0';
    response.append(buf, static_cast<size_t>(n));
    if (response.size() > MAX_RESPONSE_SIZE) {
      return "";
    }

    // Check if we have a complete line
    auto nl = response.find('\n');
    if (nl != std::string::npos) {
      response.resize(nl);
      break;
    }
  }

  // Parse JSON response
  try {
    Common::JsonValue json = Common::JsonValue::fromString(response);

    if (!json.contains("result")) {
      return "";
    }

    const auto& result = json("result");
    if (result.isString()) {
      return result.getString();
    }

    // For non-string results (objects, arrays, etc.), re-serialize
    return result.toString();
  } catch (...) {
    return "";
  }
}

void ElectrumConnection::setConnectTimeout(uint32_t seconds) {
  m_connectTimeout = seconds;
}

void ElectrumConnection::setReadTimeout(uint32_t seconds) {
  m_readTimeout = seconds;
}

} // namespace XfgSwap
