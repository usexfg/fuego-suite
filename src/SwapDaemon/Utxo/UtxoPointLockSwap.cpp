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

#include "UtxoPointLockSwap.h"

#include <algorithm>
#include <cstring>
#include <limits>

#include "Common/StringTools.h"
#include "SwapDaemon/SwapTimelock.h"

namespace XfgSwap {
namespace {

bool xOnlyOf(const std::vector<uint8_t>& compressedKey, Bip340::XOnlyKey& key) {
  if (!isCompressedPublicKey(compressedKey)) return false;
  std::copy(compressedKey.begin() + 1, compressedKey.end(), key.begin());
  return true;
}

}  // namespace

bool isPointLockSwap(const SwapParams& params) { return params.lockType == SwapLockType::PTLC; }

bool adaptorPointOf(const SwapParams& params, Bip340::Point& adaptorPoint) {
  std::vector<uint8_t> bytes;
  if (params.secpPubHex.size() != 66 || !Common::fromHex(params.secpPubHex, bytes) || !isCompressedPublicKey(bytes))
    return false;
  std::copy(bytes.begin(), bytes.end(), adaptorPoint.begin());
  return true;
}

bool adaptorScalarOf(const SwapParams& params, Bip340::Scalar& adaptorSecret) {
  if (!hasAdaptorSecret(params)) return false;
  const uint8_t* little = reinterpret_cast<const uint8_t*>(&params.adaptorSecret);
  for (size_t i = 0; i < adaptorSecret.size(); ++i) adaptorSecret[i] = little[adaptorSecret.size() - 1 - i];
  return true;
}

std::string littleEndianSecretHex(const Bip340::Scalar& adaptorSecret) {
  std::vector<uint8_t> little(adaptorSecret.rbegin(), adaptorSecret.rend());
  return Common::toHex(little);
}

bool lockerTerms(const SwapParams& params, const std::vector<uint8_t>& aliceKey, PointLockTerms& terms,
                 std::vector<uint8_t>& bobKey, std::string& error) {
  if (!Common::fromHex(params.ctrPubKey, bobKey) || !xOnlyOf(bobKey, terms.bobKey)) {
    error = "Bob's claim public key has not been received";
    return false;
  }
  if (!xOnlyOf(aliceKey, terms.aliceKey)) {
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

PointLockSpend pointClaimSpend(const UtxoHtlcChain& chain, const SwapParams& params, uint64_t lockValue,
                               uint64_t fee, const std::vector<uint8_t>& bobKey) {
  PointLockSpend spend;
  spend.lock = lockOutpoint(params);
  spend.lockValue = lockValue;
  spend.fee = fee;
  spend.destinationScript = spendDestination(chain, true, params, bobKey);
  return spend;
}

std::string pointLockStateError(const UtxoHtlcChain& chain, const SwapParams& params,
                                const std::vector<uint8_t>& bobKey, uint64_t tip, uint64_t lockValue,
                                uint64_t currentFee, PointLockState& state) {
  Bip340::XOnlyKey ownKey;
  Bip340::Point adaptorPoint;
  if (!decodePointLockState(params.chainState, state)) return "no point-lock description received from Alice";
  if (!xOnlyOf(bobKey, ownKey) || state.terms.bobKey != ownKey) return "the lock does not pay our claim key";
  if (!adaptorPointOf(params, adaptorPoint)) return "no adaptor point T_secp for this swap";
  uint64_t window = 0;
  if (!minimumCounterpartyWindowBlocks(chain.pair, requiredConfirmations(params), window) ||
      tip > std::numeric_limits<uint64_t>::max() - window || state.terms.timeoutBlock < tip + window)
    return "lock timeout " + std::to_string(state.terms.timeoutBlock) + " leaves too few blocks to claim";
  for (const PointLockClaimOffer& offer : state.claimOffers) {
    if (offer.fee >= lockValue || offer.fee > spendFeeCeiling(lockValue))
      return "a claim fee of " + std::to_string(offer.fee) + " exceeds the ceiling for a lock of " +
             std::to_string(lockValue);
    Bip340::Message sighash;
    if (!pointLockClaimSighash(state.terms, pointClaimSpend(chain, params, lockValue, offer.fee, bobKey), sighash) ||
        !Bip340::verifyPresig(state.terms.aliceKey, sighash, adaptorPoint, offer.presig))
      return "Alice's claim pre-signature at fee " + std::to_string(offer.fee) + " does not open with t";
  }
  if (state.claimOffers.back().fee < currentFee)
    return "Alice's highest pre-signed claim fee " + std::to_string(state.claimOffers.back().fee) +
           " is below the " + std::to_string(currentFee) + " a claim needs now";
  return "";
}

const PointLockClaimOffer& claimOfferFor(const UtxoHtlcChain& chain, const PointLockState& state,
                                         uint64_t currentFee) {
  if (chain.network.feeBump == UtxoFeeBump::NONE) return state.claimOffers.back();
  for (const PointLockClaimOffer& offer : state.claimOffers)
    if (offer.fee >= currentFee) return offer;
  return state.claimOffers.back();
}

const PointLockClaimOffer* nextClaimOffer(const PointLockState& state, uint64_t paidFee) {
  for (const PointLockClaimOffer& offer : state.claimOffers)
    if (offer.fee > paidFee) return &offer;
  return nullptr;
}

bool buildPointClaim(const UtxoHtlcChain& chain, const SwapParams& params, const PointLockState& state,
                     const PointLockClaimOffer& offer, uint64_t lockValue, const std::vector<uint8_t>& bobKey,
                     const Bip340::Scalar& bobSecret, std::vector<uint8_t>& rawTx) {
  Bip340::Scalar adaptorSecret;
  Bip340::Message sighash;
  Bip340::Signature aliceSignature, bobSignature;
  const PointLockSpend claim = pointClaimSpend(chain, params, lockValue, offer.fee, bobKey);
  return adaptorScalarOf(params, adaptorSecret) && pointLockClaimSighash(state.terms, claim, sighash) &&
         Bip340::complete(offer.presig, adaptorSecret, aliceSignature) &&
         Bip340::verify(state.terms.aliceKey, sighash, aliceSignature) && Bip340::sign(bobSecret, sighash, bobSignature) &&
         pointLockClaimTransaction(state.terms, claim, bobSignature, aliceSignature, rawTx);
}

std::string pointClaimDeadlineError(const UtxoHtlcChain& chain, const PointLockState& state, uint64_t tip) {
  UtxoHtlcTerms terms;
  terms.timeoutBlock = state.terms.timeoutBlock;
  return claimDeadlineError(chain, terms, tip);
}

std::string secretFromPointClaim(const SwapParams& params, const PointLockState& state,
                                 const std::vector<uint8_t>& claimTx) {
  Bip340::Point adaptorPoint;
  Bip340::Signature aliceSignature;
  if (!adaptorPointOf(params, adaptorPoint) ||
      !pointLockClaimSignature(claimTx, lockOutpoint(params), state.terms, aliceSignature))
    return {};
  for (const PointLockClaimOffer& offer : state.claimOffers) {
    Bip340::Scalar adaptorSecret;
    if (Bip340::extract(offer.presig, aliceSignature, adaptorPoint, adaptorSecret))
      return littleEndianSecretHex(adaptorSecret);
  }
  return {};
}

bool traceClaimFeePackage(const std::string& claimTxid, const std::vector<uint8_t>& rawClaim,
                          const SpendingTransactionLookup& spenderOf, ClaimFeePackage& package, std::string& error) {
  package = ClaimFeePackage();
  package.tip = {claimTxid, 0};
  package.virtualSize = pointLockSpendVirtualSize(true);
  if (!readTransactionOutput(rawClaim, 0, package.tipValue, package.tipScript)) {
    error = "the claim has no output";
    return false;
  }
  std::string childTxid;
  std::vector<uint8_t> child;
  while (spenderOf(package.tip, childTxid, child)) {
    uint64_t childValue = 0;
    std::vector<uint8_t> childScript;
    if (++package.children > MAX_CLAIM_FEE_CHILDREN || !isTxidHex(childTxid) ||
        !readTransactionOutput(child, 0, childValue, childScript) || childValue >= package.tipValue) {
      error = "the claim output is spent by something other than a short chain of fee children";
      return false;
    }
    package.tip = {childTxid, 0};
    package.tipValue = childValue;
    package.tipScript = childScript;
    package.virtualSize += child.size();
  }
  return true;
}

uint64_t raisedClaimPackageFee(const ClaimFeePackage& package, uint64_t lockValue, uint64_t estimatedFee) {
  const uint64_t ceiling = spendFeeCeiling(lockValue);
  if (package.tipValue >= lockValue || package.virtualSize == 0) return ceiling;
  const uint64_t raisedSize = package.virtualSize + P2PKH_SELF_SPEND_SIZE;
  const uint64_t paidRate = (lockValue - package.tipValue) / package.virtualSize + 1;
  const uint64_t raisedRate = paidRate > ceiling / CLAIM_FEE_STEP ? ceiling : paidRate * CLAIM_FEE_STEP;
  const uint64_t raisedFee = raisedRate > ceiling / raisedSize ? ceiling : raisedRate * raisedSize;
  return std::min(std::max(raisedFee, estimatedFee), ceiling);
}

std::string buildClaimFeeChild(const UtxoHtlcChain& chain, const ClaimFeePackage& package, uint64_t lockValue,
                               uint64_t packageFee, const Bip340::Scalar& bobSecret,
                               const std::vector<uint8_t>& bobKey, std::vector<uint8_t>& rawChild) {
  if (package.tipScript != p2pkhScriptForPubKey(bobKey))
    return "the claim pays an address this key cannot spend, so no child can raise its fee";
  if (package.tipValue >= lockValue) return "the claim pays more than the lock holds";
  const uint64_t paidFee = lockValue - package.tipValue;
  if (packageFee <= paidFee)
    return "the claim and its children already pay " + std::to_string(paidFee) + ", the most this swap spends on fees";
  P2pkhSelfSpend child;
  child.input = package.tip;
  child.inputValue = package.tipValue;
  child.fee = packageFee - paidFee;
  child.signingKey = bobSecret;
  child.publicKey = bobKey;
  return buildP2pkhSelfSpend(chain.encoding, child, rawChild) ? std::string() : "cannot build the fee child";
}

}  // namespace XfgSwap
