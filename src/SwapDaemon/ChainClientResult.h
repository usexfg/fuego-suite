#pragma once

#include <string>

namespace XfgSwap {

struct ChainClientResult {
  bool success = false;
  bool fatal = false;    // if true, swap should transition to FAILED (unrecoverable)
  std::string txId;
  std::string error;
  std::string chainState; // opaque blob returned by lock() (e.g. BCH redeem script hex)

  // SPV confirmation tracking (used by SPV-enabled chains)
  bool confirmed = false;        // true once tx is verified in a block
  bool spvVerified = false;      // true if verified via SPV (Merkle proof)
  uint64_t blockHeight = 0;      // block height where tx was confirmed
  uint32_t confirmations = 0;    // number of confirmations (tip - blockHeight + 1)

  static ChainClientResult ok(const std::string& txId) {
    return {true, false, txId, "", "", false, false, 0, 0};
  }
  static ChainClientResult okWithState(const std::string& txId, const std::string& state) {
    return {true, false, txId, "", state, false, false, 0, 0};
  }
  static ChainClientResult fail(const std::string& error) {
    return {false, false, "", error, "", false, false, 0, 0};
  }
};

} // namespace XfgSwap
