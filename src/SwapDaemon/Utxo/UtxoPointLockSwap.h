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

#include <functional>
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

// The lowest rung paying at least `currentFee`, else the top rung. A chain
// with no way to raise a claim's fee later (UtxoFeeBump::NONE) takes the top rung.
const PointLockClaimOffer& claimOfferFor(const UtxoHtlcChain& chain, const PointLockState& state,
                                         uint64_t currentFee);
// The first rung paying more than `paidFee`; nullptr when none does.
const PointLockClaimOffer* nextClaimOffer(const PointLockState& state, uint64_t paidFee);

// Bob: the claim transaction for one rung, with Alice's signature completed by t.
bool buildPointClaim(const UtxoHtlcChain& chain, const SwapParams& params, const PointLockState& state,
                     const PointLockClaimOffer& offer, uint64_t lockValue, const std::vector<uint8_t>& bobKey,
                     const Bip340::Scalar& bobSecret, std::vector<uint8_t>& rawTx);

std::string pointClaimDeadlineError(const UtxoHtlcChain& chain, const PointLockState& state, uint64_t tip);

// Bob's unconfirmed claim and the fee children chained onto it: each child
// spends output 0 of the transaction before it and pays Bob's key.
struct ClaimFeePackage {
  UtxoOutpoint tip;  // the unspent end of the chain
  uint64_t tipValue = 0;
  std::vector<uint8_t> tipScript;
  uint64_t virtualSize = 0;
  size_t children = 0;
};

constexpr size_t MAX_CLAIM_FEE_CHILDREN = 8;

// The transaction spending `outpoint`; false while it is unspent.
using SpendingTransactionLookup = std::function<bool(const UtxoOutpoint& outpoint, std::string& spendingTxid,
                                                     std::vector<uint8_t>& spendingTx)>;

bool traceClaimFeePackage(const std::string& claimTxid, const std::vector<uint8_t>& rawClaim,
                          const SpendingTransactionLookup& spenderOf, ClaimFeePackage& package, std::string& error);

// The package's total fee once one more child joins it: the fee estimated for
// its size or CLAIM_FEE_STEP times its current fee rate, whichever is higher,
// capped at the swap's fee ceiling.
uint64_t raisedClaimPackageFee(const ClaimFeePackage& package, uint64_t lockValue, uint64_t estimatedFee);

// Bob: the child bringing the package to `packageFee`. Empty on success, else why not.
std::string buildClaimFeeChild(const UtxoHtlcChain& chain, const ClaimFeePackage& package, uint64_t lockValue,
                               uint64_t packageFee, const Bip340::Scalar& bobSecret,
                               const std::vector<uint8_t>& bobKey, std::vector<uint8_t>& rawChild);

// Alice: t recovered from a transaction that claims the lock, as the
// little-endian hex scalar the XFG adaptor consumes.
std::string secretFromPointClaim(const SwapParams& params, const PointLockState& state,
                                 const std::vector<uint8_t>& claimTx);

}  // namespace XfgSwap
