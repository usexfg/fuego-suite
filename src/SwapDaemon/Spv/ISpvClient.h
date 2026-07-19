// src/SwapDaemon/Spv/ISpvClient.h
#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace XfgSwap {

struct SpvTxInclusion {
  bool included = false;        // tx found in a block (not just mempool)
  uint64_t blockHeight = 0;
  uint32_t depth = 0;           // tipHeight - blockHeight + 1
  bool merkleVerified = false;  // Merkle proof checked against our header store
};

struct SpvSpend {
  bool spent = false;
  std::string spendingTxid;
  std::vector<uint8_t> rawSpendingTx;  // for scriptSig parsing
  SpvTxInclusion inclusion;
};

class ISpvClient {
public:
  virtual ~ISpvClient() = default;
  virtual std::string protocolName() const = 0;               // "electrum"
  virtual bool syncHeaders() = 0;                             // advance + cross-check to tip
  virtual bool getTipHeight(uint64_t& height) = 0;
  virtual bool verifyTxInclusion(const std::string& txid, SpvTxInclusion& out) = 0;
  // Derives the watched scripthash from the funding output internally
  // (getRawTx(txid) -> output[vout].scriptPubKey -> scripthash -> get_history).
  virtual bool findSpend(const std::string& txid, uint32_t vout, SpvSpend& out) = 0;
  virtual bool getRawTx(const std::string& txid, std::vector<uint8_t>& rawTx) = 0;
};

} // namespace XfgSwap
