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
// Swap-level rules for Taproot point locks, shared by the full-node and SPV
// transports (CROSS_CURVE_PLAN X6).

#pragma once

#include <string>
#include <vector>

#include "UtxoHtlcSwap.h"
#include "UtxoPointLock.h"

namespace XfgSwap {

bool isPointLockSwap(const SwapParams& params);
bool adaptorPointOf(const SwapParams& params, Bip340::Point& adaptorPoint);
bool adaptorScalarOf(const SwapParams& params, Bip340::Scalar& adaptorSecret);
std::string littleEndianSecretHex(const Bip340::Scalar& adaptorSecret);

// Alice: terms and Bob's compressed claim key for a new lock.
bool lockerTerms(const SwapParams& params, const std::vector<uint8_t>& aliceKey, PointLockTerms& terms,
                 std::vector<uint8_t>& bobKey, std::string& error);

PointLockSpend pointClaimSpend(const UtxoHtlcChain& chain, const SwapParams& params, uint64_t lockValue,
                               uint64_t fee, const std::vector<uint8_t>& bobKey);

// Bob: the lock Alice described pays our key, leaves the claim window, and
// every rung of its fee ladder stays within the ceiling and opens with t; the
// top rung pays at least `currentFee`, what a claim needs to confirm now.
std::string pointLockStateError(const UtxoHtlcChain& chain, const SwapParams& params,
                                const std::vector<uint8_t>& bobKey, uint64_t tip, uint64_t lockValue,
                                uint64_t currentFee, PointLockState& state);

// The lowest rung paying at least `currentFee`, else the top rung.
const PointLockClaimOffer& claimOfferFor(const PointLockState& state, uint64_t currentFee);
// The first rung paying more than `paidFee`; nullptr when none does.
const PointLockClaimOffer* nextClaimOffer(const PointLockState& state, uint64_t paidFee);

// Bob: the claim transaction for one rung, with Alice's signature completed by t.
bool buildPointClaim(const UtxoHtlcChain& chain, const SwapParams& params, const PointLockState& state,
                     const PointLockClaimOffer& offer, uint64_t lockValue, const std::vector<uint8_t>& bobKey,
                     const Bip340::Scalar& bobSecret, std::vector<uint8_t>& rawTx);

std::string pointClaimDeadlineError(const UtxoHtlcChain& chain, const PointLockState& state, uint64_t tip);

// Alice: t recovered from a transaction that claims the lock, as the
// little-endian hex scalar the XFG adaptor consumes.
std::string secretFromPointClaim(const SwapParams& params, const PointLockState& state,
                                 const std::vector<uint8_t>& claimTx);

}  // namespace XfgSwap
