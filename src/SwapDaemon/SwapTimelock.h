// Copyright (c) 2017-2026 Fuego Developers
//
// Wall-clock cross-chain timelock comparator.
// XFG block timing is DIFFICULTY_TARGET seconds (see CryptoNoteConfig.h);
// counterparty chains vary.
// The safety invariant: XFG refund window must outlast the
// counterparty timeout by a safety margin in wall-clock time.

#pragma once

#include "SwapTypes.h"
#include <cstdint>

namespace XfgSwap {

// Safety margin: XFG refund window must outlast counterparty by at least this.
constexpr uint64_t DEFAULT_SAFETY_MARGIN_SEC = 3600;
constexpr uint64_t MIN_CLAIM_RUNWAY_MS = 3600ULL * 1000ULL;
constexpr uint64_t MIN_CLAIM_RUNWAY_BLOCKS = 30;

// Conservative block-interval bounds in milliseconds.  The minimum is used
// when converting a required claim runway into blocks; the maximum is used
// when proving the counterparty deadline fits inside the XFG deadline.
uint64_t minMsPerBlock(SwapPair pair);
uint64_t maxMsPerBlock(SwapPair pair);

// Compatibility name for the ordering-side (maximum) estimate.
uint64_t msPerBlock(SwapPair pair);

// Number of blocks needed to cover at least one hour at the fastest catalog
// bound, never fewer than 30. Returns 0 for invalid catalog metadata.
uint64_t claimRunwayBlocks(SwapPair pair);

// Adds required confirmations to claimRunwayBlocks with overflow checking.
bool minimumCounterpartyWindowBlocks(SwapPair pair,
                                     uint64_t requiredConfirmations,
                                     uint64_t& out);

// Returns true if the XFG refund window (xfgTimeoutH - xfgCurH)*DIFFICULTY_TARGET
// outlasts the counterparty timeout by at least marginSec wall-clock seconds.
// Both current heights and timeout heights must be passed in native units.
bool timelockOrderingOk(SwapPair pair,
                        uint64_t xfgCurrentHeight,
                        uint64_t xfgTimeoutHeight,
                        uint64_t ctrCurrentHeight,
                        uint64_t ctrTimeoutHeight,
                        uint64_t marginSec = DEFAULT_SAFETY_MARGIN_SEC);

} // namespace XfgSwap
