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

#include "BchPointLockSwap.h"

#include <algorithm>
#include <limits>

#include "Common/StringTools.h"
#include "SwapDaemon/SwapTimelock.h"

namespace XfgSwap {
namespace {

bool compressedKeyOf(const std::vector<uint8_t>& bytes, BchSchnorr::Point& key) {
  if (!isCompressedPublicKey(bytes)) return false;
  std::copy(bytes.begin(), bytes.end(), key.begin());
  return true;
}

}  // namespace

bool bchLockerTerms(const SwapParams& params, const std::vector<uint8_t>& aliceKey, BchPointLockTerms& terms,
                    std::vector<uint8_t>& bobKey, std::string& error) {
  if (!Common::fromHex(params.ctrPubKey, bobKey) || !compressedKeyOf(bobKey, terms.bobKey)) {
    error = "Bob's claim public key has not been received";
    return false;
  }
  if (!compressedKeyOf(aliceKey, terms.aliceKey)) {
    error = "no signing key";
    return false;
  }
  if (params.ctrTimeoutBlock == 0 || params.ctrTimeoutBlock > std::numeric_limits<uint32_t>::max()) {
    error = "timeout height out of range";
    return false;
  }
  terms.timeoutBlock = static_cast<uint32_t>(params.ctrTimeoutBlock);
  return true;
}

std::string bchPointLockStateError(const UtxoHtlcChain& chain, const SwapParams& params,
                                   const std::vector<uint8_t>& bobKey, uint64_t tip, uint64_t lockValue,
                                   uint64_t currentFee, BchPointLockState& state) {
  BchSchnorr::Point ownKey;
  Bip340::Point adaptorPoint;
  if (!decodeBchPointLockState(params.chainState, state)) return "no point-lock description received from Alice";
  if (!compressedKeyOf(bobKey, ownKey) || state.terms.bobKey != ownKey) return "the lock does not pay our claim key";
  if (!adaptorPointOf(params, adaptorPoint)) return "no adaptor point T_secp for this swap";
  uint64_t window = 0;
  if (!minimumCounterpartyWindowBlocks(chain.pair, requiredConfirmations(params), window) ||
      tip > std::numeric_limits<uint64_t>::max() - window || state.terms.timeoutBlock < tip + window)
    return "lock timeout " + std::to_string(state.terms.timeoutBlock) + " leaves too few blocks to claim";
  for (const BchPointLockClaimOffer& offer : state.claimOffers) {
    if (offer.fee >= lockValue || offer.fee > spendFeeCeiling(lockValue))
      return "a claim fee of " + std::to_string(offer.fee) + " exceeds the ceiling for a lock of " +
             std::to_string(lockValue);
    BchSchnorr::Message sighash;
    if (!bchPointLockClaimSighash(state.terms, pointClaimSpend(chain, params, lockValue, offer.fee, bobKey),
                                  sighash) ||
        !BchSchnorr::verifyPresig(state.terms.aliceKey, sighash, adaptorPoint, offer.presig))
      return "Alice's claim pre-signature at fee " + std::to_string(offer.fee) + " does not open with t";
  }
  if (state.claimOffers.back().fee < currentFee)
    return "Alice's highest pre-signed claim fee " + std::to_string(state.claimOffers.back().fee) +
           " is below the " + std::to_string(currentFee) + " a claim needs now";
  return "";
}

bool buildBchPointClaim(const UtxoHtlcChain& chain, const SwapParams& params, const BchPointLockState& state,
                        uint64_t lockValue, const std::vector<uint8_t>& bobKey, const BchSchnorr::Scalar& bobSecret,
                        std::vector<uint8_t>& rawTx) {
  const BchPointLockClaimOffer& offer = state.claimOffers.back();
  const PointLockSpend claim = pointClaimSpend(chain, params, lockValue, offer.fee, bobKey);
  Bip340::Scalar adaptorSecret;
  BchSchnorr::Message sighash;
  BchSchnorr::Signature aliceSignature, bobSignature;
  return adaptorScalarOf(params, adaptorSecret) && bchPointLockClaimSighash(state.terms, claim, sighash) &&
         BchSchnorr::complete(offer.presig, adaptorSecret, aliceSignature) &&
         BchSchnorr::verify(state.terms.aliceKey, sighash, aliceSignature) &&
         BchSchnorr::sign(bobSecret, sighash, bobSignature) &&
         bchPointLockClaimTransaction(state.terms, claim, bobSignature, aliceSignature, rawTx);
}

std::string bchPointClaimDeadlineError(const UtxoHtlcChain& chain, const BchPointLockState& state, uint64_t tip) {
  UtxoHtlcTerms terms;
  terms.timeoutBlock = state.terms.timeoutBlock;
  return claimDeadlineError(chain, terms, tip);
}

std::string secretFromBchPointClaim(const SwapParams& params, const BchPointLockState& state,
                                    const std::vector<uint8_t>& claimTx) {
  Bip340::Point adaptorPoint;
  BchSchnorr::Signature aliceSignature;
  if (!adaptorPointOf(params, adaptorPoint) ||
      !bchPointLockClaimSignature(claimTx, lockOutpoint(params), state.terms, aliceSignature))
    return {};
  for (const BchPointLockClaimOffer& offer : state.claimOffers) {
    Bip340::Scalar adaptorSecret;
    if (BchSchnorr::extract(offer.presig, aliceSignature, adaptorPoint, adaptorSecret))
      return littleEndianSecretHex(adaptorSecret);
  }
  return {};
}

}  // namespace XfgSwap
