// Copyright (c) 2017-2026 Fuego Developers

#include "StatusServer.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <sstream>

namespace XfgSwap {

StatusServer::StatusServer(uint16_t port, StatusFn statusFn, Logging::ILogger& logger)
  : m_port(port), m_statusFn(std::move(statusFn)), m_logger(logger, "StatusServer") {}

StatusServer::~StatusServer() {
  stop();
}

bool StatusServer::start() {
  m_listenSocket = socket(AF_INET, SOCK_STREAM, 0);
  if (m_listenSocket < 0) {
    m_logger(Logging::ERROR) << "Failed to create status socket";
    return false;
  }

  int opt = 1;
  setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");
  addr.sin_port = htons(m_port);

  if (bind(m_listenSocket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    m_logger(Logging::ERROR) << "Failed to bind status socket on port " << m_port;
    close(m_listenSocket);
    m_listenSocket = -1;
    return false;
  }

  if (listen(m_listenSocket, 8) < 0) {
    close(m_listenSocket);
    m_listenSocket = -1;
    return false;
  }

  m_running = true;
  m_acceptThread = std::thread(&StatusServer::acceptLoop, this);
  m_logger(Logging::INFO) << "Status server listening on 127.0.0.1:" << m_port;
  return true;
}

void StatusServer::stop() {
  if (!m_running.exchange(false)) return;

  if (m_listenSocket >= 0) {
    close(m_listenSocket);
    m_listenSocket = -1;
  }

  if (m_acceptThread.joinable()) {
    m_acceptThread.join();
  }
}

void StatusServer::acceptLoop() {
  while (m_running) {
    struct sockaddr_in clientAddr = {};
    socklen_t clientLen = sizeof(clientAddr);
    int clientSock = accept(m_listenSocket, (struct sockaddr*)&clientAddr, &clientLen);
    if (clientSock < 0) {
      if (!m_running) break;
      continue;
    }

    std::string json = m_statusFn();

    // Emit a proper HTTP/1.1 response. Without a status line and headers, modern
    // HTTP clients (curl ≥ 7.x without --http0.9) refuse the response and report
    // "Received HTTP/0.9 when not allowed", breaking standard monitoring tooling.
    // We do NOT parse the request — any method/path returns the dashboard JSON.
    std::ostringstream resp;
    resp << "HTTP/1.1 200 OK\r\n"
         << "Content-Type: application/json\r\n"
         << "Content-Length: " << json.size() << "\r\n"
         << "Access-Control-Allow-Origin: *\r\n"
         << "Connection: close\r\n"
         << "\r\n"
         << json;
    const std::string out = resp.str();
    send(clientSock, out.data(), out.size(), 0);

    close(clientSock);
  }
}

} // namespace XfgSwap
