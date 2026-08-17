// Copyright (c) 2017-2026 Fuego Developers
//
// TCP JSON-RPC client for the Electrum protocol. Connects to an Electrum
// server over plaintext TCP and performs synchronous newline-delimited
// JSON-RPC calls.

#pragma once

#include "Common/WinCompat.h"

#include <cstdint>
#include <string>

namespace XfgSwap {

class ElectrumConnection {
public:
  ElectrumConnection();
  ~ElectrumConnection();

  ElectrumConnection(const ElectrumConnection&) = delete;
  ElectrumConnection& operator=(const ElectrumConnection&) = delete;

  // Connect to an Electrum server. Returns true on success.
  // useTls: when true, wrap the socket in OpenSSL TLS (Electrum SSL ports, e.g. 50002).
  // Default false preserves historical plaintext Electrum (roadmap TLS deferred → optional now).
  bool connect(const std::string& host, uint16_t port, bool useTls = false);

  // Disconnect.
  void disconnect();

  // Check if connected.
  bool isConnected() const;

  // Send a JSON-RPC call and wait for the response.
  // Returns the "result" field as a string, or empty on error.
  std::string call(const std::string& method, const std::string& paramsJson);

  // Set connect timeout in seconds (default 10).
  void setConnectTimeout(uint32_t seconds);

  // Set read timeout in seconds (default 30).
  void setReadTimeout(uint32_t seconds);

private:
  int m_fd = -1;
  void* m_ssl = nullptr;      // SSL* when TLS enabled
  void* m_sslCtx = nullptr;   // SSL_CTX*
  bool m_useTls = false;
  uint32_t m_connectTimeout = 10;
  uint32_t m_readTimeout = 30;
  uint64_t m_callId = 0;

  bool sslHandshake();
  ssize_t sslWrite(const void* buf, size_t len);
  ssize_t sslRead(void* buf, size_t len);
};

} // namespace XfgSwap
