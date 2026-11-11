// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free & open source software distributed in the hope
// it will be useful, but WITHOUT ANY WARRANTY; without even an
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You may redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.
//
// Swap-level rules for BCH P2SH point locks, shared by the full-node and SPV
// transports (CROSS_CURVE_PLAN X7). The fee ladder and claim destination
// follow the Taproot rules in UtxoPointLockSwap.h.

#pragma once

#include <string>
#include <vector>

#include "BchPointLock.h"
#include "UtxoPointLockSwap.h"

namespace XfgSwap {

// Alice: terms and Bob's compressed claim key for a new lock.
bool bchLockerTerms(const SwapParams& params, const std::vector<uint8_t>& aliceKey, BchPointLockTerms& terms,
                    std::vector<uint8_t>& bobKey, std::string& error);

// Bob: the lock Alice described pays our key, leaves the claim window, and
// every rung stays within the ceiling and opens with t; the top rung, the one
// Bob claims with, pays at least `currentFee`.
std::string bchPointLockStateError(const UtxoHtlcChain& chain, const SwapParams& params,
                                   const std::vector<uint8_t>& bobKey, uint64_t tip, uint64_t lockValue,
                                   uint64_t currentFee, BchPointLockState& state);

// Bob: the claim at the top rung, Alice's signature completed with t.
bool buildBchPointClaim(const UtxoHtlcChain& chain, const SwapParams& params, const BchPointLockState& state,
                        uint64_t lockValue, const std::vector<uint8_t>& bobKey, const BchSchnorr::Scalar& bobSecret,
                        std::vector<uint8_t>& rawTx);

std::string bchPointClaimDeadlineError(const UtxoHtlcChain& chain, const BchPointLockState& state, uint64_t tip);

// Alice: t from a transaction that claims the lock, as the little-endian hex
// scalar the XFG adaptor consumes.
std::string secretFromBchPointClaim(const SwapParams& params, const BchPointLockState& state,
                                    const std::vector<uint8_t>& claimTx);

}  // namespace XfgSwap
