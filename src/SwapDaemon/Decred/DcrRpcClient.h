#pragma once

#include <string>
#include <cstdint>
#include <vector>

namespace XfgSwap {

// Minimal JSON-RPC client for Decred dcrd.
class DcrRpcClient {
public:
  DcrRpcClient(const std::string& host, uint16_t port,
               const std::string& rpcUser = "",
               const std::string& rpcPass = "");

  bool getBlockCount(uint64_t& height);
  bool getRawTransaction(const std::string& txid, std::vector<uint8_t>& rawTx);
  bool getTxOut(const std::string& txid, uint32_t vout, uint64_t& amount);

  const std::string& host() const { return m_host; }
  uint16_t port() const { return m_port; }

private:
  std::string m_host;
  uint16_t m_port;
  std::string m_rpcUser;
  std::string m_rpcPass;
};

} // namespace XfgSwap
