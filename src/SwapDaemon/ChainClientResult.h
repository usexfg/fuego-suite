#pragma once

#include <string>

namespace XfgSwap {

struct ChainClientResult {
  bool success = false;
  bool fatal = false;    // if true, swap should transition to FAILED (unrecoverable)
  std::string txId;
  std::string error;
  std::string chainState; // opaque blob returned by lock() (e.g. BCH redeem script hex)

  static ChainClientResult ok(const std::string& txId) {
    return {true, false, txId, "", ""};
  }
  static ChainClientResult okWithState(const std::string& txId, const std::string& state) {
    return {true, false, txId, "", state};
  }
  static ChainClientResult fail(const std::string& error) {
    return {false, false, "", error, ""};
  }
};

} // namespace XfgSwap
