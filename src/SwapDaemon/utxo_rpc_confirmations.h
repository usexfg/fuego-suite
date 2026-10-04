#pragma once

#include <cstdint>
#include <limits>

namespace XfgSwap {

// Bitcoin-family RPCs return negative confirmations for conflicted
// transactions. Never narrow that signed value to an unsigned depth.
inline uint32_t verified_rpc_confirmations(int64_t reported) {
  return reported > 0 &&
         reported <= static_cast<int64_t>(std::numeric_limits<uint32_t>::max())
      ? static_cast<uint32_t>(reported) : 0;
}

} // namespace XfgSwap
