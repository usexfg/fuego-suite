#pragma once

#include <string>
#include <cstdint>
#include <vector>

namespace XfgSwap {

// JSON-RPC client for dcrd (Decred full node).
// Supports dcrd's cookie-based and user:pass authentication.
class DcrRpcClient {
public:
  DcrRpcClient(const std::string& host, uint16_t port,
               const std::string& rpcUser = "",
               const std::string& rpcPass = "");

  // Basic queries
  bool getBlockCount(uint64_t& height);
  bool getRawTransaction(const std::string& txid, std::string& rawTxHex);
  bool getRawTransactionBytes(const std::string& txid, std::vector<uint8_t>& rawTx);
  bool getTxOut(const std::string& txid, uint32_t vout, uint64_t& amount);

  // Transaction operations
  bool sendRawTransaction(const std::string& rawTxHex, std::string& txid);
  bool createRawTransaction(const std::string& inputsJson,
                            const std::string& outputsJson,
                            uint32_t locktime,
                            std::string& rawTxHex);
  bool signRawTransaction(const std::string& rawTxHex, std::string& signedTxHex);

  // HTLC operations
  bool claim(const std::string& claimerWif,
             const std::string& htlcTxid,
             uint32_t htlcVout,
             uint64_t htlcAmount,
             const std::string& redeemScriptHex,
             const std::string& preimageHex,
             const std::string& destAddress,
             std::string& claimTxId);

  bool refundHtlc(const std::string& senderWif,
                  const std::string& htlcTxid,
                  uint32_t htlcVout,
                  uint64_t htlcAmount,
                  const std::string& redeemScriptHex,
                  uint32_t timeoutBlock,
                  const std::string& destAddress,
                  std::string& refundTxId);

  // Local sighash computation for DCR P2SH HTLC signing.
  static std::vector<uint8_t> computeLegacySighash(
      uint32_t txVersion,
      const std::string& inputTxid,
      uint32_t inputVout,
      const std::vector<uint8_t>& scriptCode,
      uint32_t inputAmount,
      uint32_t nSequence,
      const std::vector<std::vector<uint8_t>>& outputScripts,
      const std::vector<uint64_t>& outputAmounts,
      uint32_t nLocktime,
      uint32_t expiry,
      uint8_t sighashType);

  // Address monitoring
  bool importAddress(const std::string& address, const std::string& label, bool rescan);
  bool listUnspent(const std::string& address, std::vector<std::pair<std::string, uint64_t>>& utxos);

  const std::string& host() const { return m_host; }
  uint16_t port() const { return m_port; }

private:
  std::string rpcCall(const std::string& method, const std::string& params);
  std::string httpPost(const std::string& body);
  std::string base64Encode(const std::string& input);

  std::string m_host;
  uint16_t m_port;
  std::string m_rpcUser;
  std::string m_rpcPass;
  std::string m_authHeader;
};

} // namespace XfgSwap
