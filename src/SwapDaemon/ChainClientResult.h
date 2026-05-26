#pragma once

#include <string>

namespace XfgSwap {

struct ChainClientResult {
  bool success = false;
  bool fatal = false;    // if true, swap should transition to FAILED (unrecoverable)
  std::string txId;
  std::string error;

  static ChainClientResult ok(const std::string& txId) {
    return {true, false, txId, ""};
  }
  static ChainClientResult fail(const std::string& error) {
    return {false, false, "", error};
  }
};

} // namespace XfgSwap
