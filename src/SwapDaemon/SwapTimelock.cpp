// Copyright (c) 2017-2026 Fuego Developers

#include "SwapTimelock.h"
#include "CryptoNoteConfig.h"
#include <limits>

namespace XfgSwap {

uint64_t minMsPerBlock(SwapPair pair) {
  const auto* descriptor = swapPairDescriptor(pair);
  return descriptor ? descriptor->minBlockTimeMs : 600000;
}

uint64_t maxMsPerBlock(SwapPair pair) {
  const auto* descriptor = swapPairDescriptor(pair);
  return descriptor ? descriptor->maxBlockTimeMs : 600000;
}

uint64_t msPerBlock(SwapPair pair) {
  return maxMsPerBlock(pair);
}

uint64_t claimRunwayBlocks(SwapPair pair) {
  const uint64_t minBlockMs = minMsPerBlock(pair);
  if (minBlockMs == 0) return 0;
  uint64_t blocks = MIN_CLAIM_RUNWAY_MS / minBlockMs;
  if (MIN_CLAIM_RUNWAY_MS % minBlockMs != 0) ++blocks;
  return blocks < MIN_CLAIM_RUNWAY_BLOCKS ? MIN_CLAIM_RUNWAY_BLOCKS : blocks;
}

bool minimumCounterpartyWindowBlocks(SwapPair pair,
                                     uint64_t requiredConfirmations,
                                     uint64_t& out) {
  const uint64_t runway = claimRunwayBlocks(pair);
  if (runway == 0 ||
      requiredConfirmations > std::numeric_limits<uint64_t>::max() - runway) {
    return false;
  }
  out = requiredConfirmations + runway;
  return true;
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

  const uint64_t xfgBlocks = xfgTimeoutHeight - xfgCurrentHeight;
  const uint64_t xfgBlockMs = CryptoNote::parameters::DIFFICULTY_TARGET * 1000ULL;
  const uint64_t ctrBlocks = ctrTimeoutHeight - ctrCurrentHeight;
  const uint64_t ctrBlockMs = maxMsPerBlock(pair);
  if (xfgBlockMs == 0 || ctrBlockMs == 0) return false;

  const uint64_t max = std::numeric_limits<uint64_t>::max();
  if (xfgBlocks > max / xfgBlockMs ||
      ctrBlocks > max / ctrBlockMs ||
      marginSec > max / 1000ULL) {
    return false;
  }
  const uint64_t xfgDeadlineMs = xfgBlocks * xfgBlockMs;
  const uint64_t ctrDeadlineMs = ctrBlocks * ctrBlockMs;
  const uint64_t marginMs = marginSec * 1000ULL;
  if (ctrDeadlineMs > max - marginMs) return false;

  return xfgDeadlineMs >= ctrDeadlineMs + marginMs;
}

} // namespace XfgSwap
