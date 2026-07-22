// Copyright (c) 2017-2026 Fuego Developers

#include "SwapTimelock.h"

namespace XfgSwap {

uint64_t msPerBlock(SwapPair pair) {
  switch (pair) {
    case SwapPair::SOL: return 400;      // ~0.4s/slot
    case SwapPair::ETH: return 12000;    // 12s/block
    case SwapPair::XMR: return 120000;   // 120s/block
    case SwapPair::BCH: return 600000;   // 600s/block
    case SwapPair::ARB:  return 250;     // ~0.25s/block
    case SwapPair::BASE: return 2000;    // ~2s/block
    case SwapPair::KMD_SPV: return 60000; // ~60s/block
    case SwapPair::BNB: return 3000;     // ~3s/block
    case SwapPair::DCR: return 300000;   // ~5min/block
    default:             return 600000;  // conservative default
  }
}

bool timelockOrderingOk(SwapPair pair,
                        uint64_t xfgCurrentHeight,
                        uint64_t xfgTimeoutHeight,
                        uint64_t ctrCurrentHeight,
                        uint64_t ctrTimeoutHeight,
                        uint64_t marginSec) {
  // Guard underflow
  if (xfgTimeoutHeight <= xfgCurrentHeight ||
      ctrTimeoutHeight <= ctrCurrentHeight) {
    return false;
  }

  uint64_t xfgBlocks   = xfgTimeoutHeight - xfgCurrentHeight;
  uint64_t xfgDeadlineMs = xfgBlocks * 480000ULL; // 480s per XFG block

  uint64_t ctrBlocks   = ctrTimeoutHeight - ctrCurrentHeight;
  uint64_t ctrDeadlineMs = ctrBlocks * msPerBlock(pair);

  uint64_t marginMs = marginSec * 1000ULL;

  return xfgDeadlineMs >= ctrDeadlineMs + marginMs;
}

} // namespace XfgSwap
