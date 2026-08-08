#pragma once
#include <string>
#include <functional>
#include <vector>

namespace XfgSwap {

// Scale-encoded RPC type aliases
using ScaleBytes = std::vector<uint8_t>;
using ScaleBlockHash = std::string; // hex-encoded

struct SubstrateCallResult {
  bool success;
  std::string error;
  ScaleBytes data;
};

// Substrate JSON-RPC client for Polkadot relay chain and parachains.
// Uses SCALE encoding for request/response serialization.
class SubstrateRpcClient {
public:
  SubstrateRpcClient(const std::string& host, uint16_t port);

  // ── Chain state queries ──
  bool getBlockHeight(uint64_t& height);
  bool getBlockHash(uint64_t height, ScaleBlockHash& hash);
  bool getRuntimeVersion(std::string& specName, uint32_t& specVersion);

  // ── Transaction operations ──
  bool submitExtrinsic(const ScaleBytes& encodedTx, std::string& txHash);
  bool getPaymentInfo(const ScaleBytes& encodedTx, uint64_t& partialFee);

  // ── HTLC-specific calls (requires pallet-hashTimeLock) ──
  bool createHashLock(const std::string& secretHash, uint64_t amount,
                      uint64_t timeout, const std::string& receiver,
                      std::string& lockTxHash);
  bool releaseHashLock(const std::string& secretPreimage,
                       const std::string& lockTxHash,
                       std::string& releaseTxHash);
  bool refundHashLock(const std::string& lockTxHash,
                      std::string& refundTxHash);
  bool getHashLockInfo(const std::string& lockTxHash,
                       bool& isLocked, uint64_t& amount, uint64_t& timeout);

  // ── Balance queries ──
  bool getBalance(const std::string& accountId, uint64_t& balance);
  bool getAccountNonce(const std::string& accountId, uint32_t& nonce);

private:
  std::string m_host;
  uint16_t m_port;
  std::string m_genesisHash;

  // Low-level SCALE-encoded JSON-RPC call
  bool rpcCall(const std::string& method, const std::string& params,
               SubstrateCallResult& result);
};

} // namespace XfgSwap
