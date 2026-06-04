// Copyright (c) 2017-2026 Fuego Developers
//
// Wall-clock cross-chain timelock comparator.
// XFG block timing is ~480s; counterparty chains vary.
// The safety invariant: XFG refund window must outlast the
// counterparty timeout by a safety margin in wall-clock time.

#pragma once

#include "SwapTypes.h"
#include <cstdint>

namespace XfgSwap {

// Safety margin: XFG refund window must outlast counterparty by at least this.
constexpr uint64_t DEFAULT_SAFETY_MARGIN_SEC = 3600;

// Block/interval times in milliseconds (to avoid floating 0 for fast chains).
uint64_t msPerBlock(SwapPair pair);

// Returns true if the XFG refund window (xfgTimeoutH - xfgCurH)*480s
// outlasts the counterparty timeout by at least marginSec wall-clock seconds.
// Both current heights and timeout heights must be passed in native units.
bool timelockOrderingOk(SwapPair pair,
                        uint64_t xfgCurrentHeight,
                        uint64_t xfgTimeoutHeight,
                        uint64_t ctrCurrentHeight,
                        uint64_t ctrTimeoutHeight,
                        uint64_t marginSec = DEFAULT_SAFETY_MARGIN_SEC);

} // namespace XfgSwap
