#include "DcrRpcClient.h"

namespace XfgSwap {

DcrRpcClient::DcrRpcClient(const std::string& host, uint16_t port,
                             const std::string& rpcUser,
                             const std::string& rpcPass)
  : m_host(host)
  , m_port(port)
  , m_rpcUser(rpcUser)
  , m_rpcPass(rpcPass) {}

bool DcrRpcClient::getBlockCount(uint64_t& height) {
  // TODO: implement JSON-RPC call to dcrd getblockcount
  (void)height;
  return false;
}

bool DcrRpcClient::getRawTransaction(const std::string& txid, std::vector<uint8_t>& rawTx) {
  // TODO: implement JSON-RPC call to dcrd getrawtransaction
  (void)txid;
  (void)rawTx;
  return false;
}

bool DcrRpcClient::getTxOut(const std::string& txid, uint32_t vout, uint64_t& amount) {
  // TODO: implement JSON-RPC call to dcrd gettxout
  (void)txid;
  (void)vout;
  (void)amount;
  return false;
}

} // namespace XfgSwap
