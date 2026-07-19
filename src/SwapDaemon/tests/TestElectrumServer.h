// Copyright (c) 2017-2026 Fuego Developers
//
// In-process test Electrum server. Binds to 127.0.0.1:0 (ephemeral port),
// accepts one connection on a background thread, reads newline-delimited
// JSON-RPC requests, replies with caller-registered canned responses keyed
// by JSON "method". Named Test… per repo convention (not "mock").

#pragma once

#include "Common/JsonValue.h"

#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <poll.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <sys/socket.h>

namespace XfgSwap {

class TestElectrumServer {
public:
  // handler: (method, paramsJson) -> result JSON string
  using Handler = std::function<std::string(const std::string& method, const std::string& paramsJson)>;

  TestElectrumServer() = default;

  ~TestElectrumServer() {
    stop();
  }

  void setCannedResponse(const std::string& method, const std::string& resultJson) {
    m_cannedResponses[method] = resultJson;
  }

  void setHandler(Handler h) {
    m_handler = std::move(h);
  }

  uint16_t start() {
    m_listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_listenFd < 0) {
      return 0;
    }

    int opt = 1;
    setsockopt(m_listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0; // ephemeral

    if (bind(m_listenFd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
      ::close(m_listenFd);
      m_listenFd = -1;
      return 0;
    }

    socklen_t addrLen = sizeof(addr);
    getsockname(m_listenFd, reinterpret_cast<struct sockaddr*>(&addr), &addrLen);
    m_port.store(ntohs(addr.sin_port));

    if (listen(m_listenFd, 1) < 0) {
      ::close(m_listenFd);
      m_listenFd = -1;
      m_port.store(0);
      return 0;
    }

    m_running.store(true);
    m_thread = std::thread(&TestElectrumServer::acceptLoop, this);

    return m_port.load();
  }

  void stop() {
    m_running.store(false);
    if (m_listenFd >= 0) {
      ::shutdown(m_listenFd, SHUT_RDWR);
      ::close(m_listenFd);
      m_listenFd = -1;
    }
    if (m_thread.joinable()) {
      m_thread.join();
    }
  }

  uint16_t port() const {
    return m_port.load();
  }

  bool receivedMethod(const std::string& method) const {
    std::lock_guard<std::mutex> lock(m_receivedMutex);
    auto it = m_receivedMethods.find(method);
    return it != m_receivedMethods.end() && it->second;
  }

private:
  void acceptLoop() {
    while (m_running.load()) {
      struct pollfd pfd {};
      pfd.fd = m_listenFd;
      pfd.events = POLLIN;
      int rc = poll(&pfd, 1, 200); // 200ms timeout for responsiveness

      if (rc <= 0) {
        continue;
      }

      struct sockaddr_in clientAddr {};
      socklen_t clientLen = sizeof(clientAddr);
      int clientFd = accept(m_listenFd, reinterpret_cast<struct sockaddr*>(&clientAddr), &clientLen);
      if (clientFd < 0) {
        continue;
      }

      // Handle exactly one connection then exit
      handleClient(clientFd);
      ::close(clientFd);
      break;
    }
  }

  void handleClient(int clientFd) {
    std::string buffer;

    while (m_running.load()) {
      char buf[4096];
      ssize_t n = recv(clientFd, buf, sizeof(buf) - 1, 0);
      if (n <= 0) {
        break;
      }
      buf[n] = '\0';
      buffer.append(buf, static_cast<size_t>(n));

      // Process complete lines
      while (true) {
        auto nl = buffer.find('\n');
        if (nl == std::string::npos) {
          break;
        }

        std::string line = buffer.substr(0, nl);
        buffer.erase(0, nl + 1);

        if (line.empty()) {
          continue;
        }

        processRequest(clientFd, line);
      }
    }
  }

  void processRequest(int clientFd, const std::string& line) {
    try {
      Common::JsonValue req = Common::JsonValue::fromString(line);

      std::string method;
      if (req.contains("method") && req("method").isString()) {
        method = req("method").getString();
      }

      std::string paramsJson = "[]";
      if (req.contains("params")) {
        paramsJson = req("params").toString();
      }

      // Track received method
      {
        std::lock_guard<std::mutex> lock(m_receivedMutex);
        m_receivedMethods[method] = true;
      }

      // Determine result
      std::string resultJson;
      if (m_handler) {
        resultJson = m_handler(method, paramsJson);
      } else {
        auto it = m_cannedResponses.find(method);
        if (it != m_cannedResponses.end()) {
          resultJson = it->second;
        } else {
          resultJson = "null";
        }
      }

      // Build JSON-RPC response
      int64_t id = 0;
      if (req.contains("id") && req("id").isInteger()) {
        id = req("id").getInteger();
      }

      std::string response = "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id)
          + ",\"result\":" + resultJson + "}\n";

      // Send response
      size_t sent = 0;
      while (sent < response.size()) {
        ssize_t n = ::send(clientFd, response.data() + sent, response.size() - sent, MSG_NOSIGNAL);
        if (n < 0) {
          return;
        }
        sent += static_cast<size_t>(n);
      }
    } catch (...) {
      // Ignore malformed JSON
    }
  }

  std::thread m_thread;
  std::atomic<uint16_t> m_port{0};
  std::atomic<bool> m_running{false};
  int m_listenFd = -1;
  Handler m_handler;
  std::map<std::string, std::string> m_cannedResponses;
  mutable std::mutex m_receivedMutex;
  std::map<std::string, bool> m_receivedMethods;
};

} // namespace XfgSwap
