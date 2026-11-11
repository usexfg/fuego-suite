// Copyright (c) 2017-2026 Fuego Developers
//
// StatusServer — read-only JSON status endpoint for monitoring.
// Binds to loopback, writes buildStatusJson on each connection, closes.

#pragma once

#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <cstdint>
#include "../Logging/ILogger.h"
#include "../Logging/LoggerRef.h"

namespace XfgSwap {

class StatusServer {
public:
  using StatusFn = std::function<std::string()>;

  StatusServer(uint16_t port, StatusFn statusFn, Logging::ILogger& logger);
  ~StatusServer();

  bool start();
  void stop();

private:
  void acceptLoop();

  uint16_t m_port;
  StatusFn m_statusFn;
  Logging::LoggerRef m_logger;
  int m_listenSocket = -1;
  std::atomic<bool> m_running{false};
  std::thread m_acceptThread;
};

} // namespace XfgSwap
