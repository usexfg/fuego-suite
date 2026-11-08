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
// Taproot point locks over a full node (CROSS_CURVE_PLAN X6).

#include "UtxoHtlcFullNode.h"
#include "UtxoPointLockSwap.h"

namespace XfgSwap {

namespace {

std::string quoted(const std::string& s) { return "\"" + s + "\""; }

SwapParams withLock(const SwapParams& params, const std::string& txid, uint32_t vout) {
  SwapParams locked = params;
  locked.ctrLockTxId = txid;
  locked.ctrLockVout = vout;
  return locked;
}

}  // namespace

ChainClientResult UtxoHtlcFullNode::lockPoint(const SwapParams& params) {
  if (!m_hasKey) return ChainClientResult::fail(failurePrefix() + "no valid WIF key");
  if (!supportsPointLocks()) return ChainClientResult::fail(failurePrefix() + "no point lock on this chain");
  PointLockState state;
  std::vector<uint8_t> bobKey;
  Bip340::Point adaptorPoint;
  std::vector<uint8_t> lockScriptPubKey;
  std::string error;
  if (!lockerTerms(params, m_pubKey, state.terms, bobKey, error)) return ChainClientResult::fail(failurePrefix() + error);
  if (!adaptorPointOf(params, adaptorPoint)) return ChainClientResult::fail(failurePrefix() + "no adaptor point T_secp");
  if (!pointLockScriptPubKey(state.terms, lockScriptPubKey))
    return ChainClientResult::fail(failurePrefix() + "invalid point-lock terms");

  const uint64_t amount = params.ctrAmount64();
  std::string txid;
  if (!findPriorLockSend(lockScriptPubKey, amount, txid)) {
    const std::string address = pointLockAddress(state.terms, m_chain.network.bech32Hrp);
    Common::JsonValue result;
    if (address.empty() ||
        !m_rpc("sendtoaddress", "[" + quoted(address) + "," + formatCoinAmount(amount) + "]", result, error))
      return ChainClientResult::fail(failurePrefix() + "sendtoaddress failed: " + error);
    if (!result.isString() || !isTxidHex(result.getString()))
      return ChainClientResult::fail(failurePrefix() + "sendtoaddress returned no txid");
    txid = result.getString();
  }
  uint32_t vout = 0;
  if (!findWalletOutputIndex(m_rpc, txid, lockScriptPubKey, vout, error))
    return ChainClientResult::fail(failurePrefix() + error);

  const SwapParams locked = withLock(params, txid, vout);
  const uint64_t ceiling = spendFeeCeiling(amount);
  for (const uint64_t fee : claimFeeLadder(estimateFeeForSize(m_rpc, pointLockSpendVirtualSize(true), ceiling), ceiling)) {
    PointLockClaimOffer offer;
    offer.fee = fee;
    Bip340::Message sighash;
    if (!pointLockClaimSighash(state.terms, pointClaimSpend(m_chain, locked, amount, fee, bobKey), sighash) ||
        !Bip340::presign(m_key, sighash, adaptorPoint, offer.presig))
      return ChainClientResult::fail(failurePrefix() + "cannot pre-sign Bob's claim (lock " + txid + " is funded)");
    state.claimOffers.push_back(offer);
  }
  if (state.claimOffers.empty())
    return ChainClientResult::fail(failurePrefix() + "no claim fee fits the lock (lock " + txid + " is funded)");
  ChainClientResult result = ChainClientResult::okWithState(txid, encodePointLockState(state));
  result.outputIndex = vout;
  return result;
}

ChainClientResult UtxoHtlcFullNode::verifyPointLock(const SwapParams& params) {
  if (!m_hasKey) return ChainClientResult::fail(failurePrefix() + "no valid WIF key");
  PointLockState state;
  UtxoLockExpectation expectation;
  uint64_t tip = 0;
  uint64_t lockValue = 0;
  std::string error;
  if (!decodePointLockState(params.chainState, state) ||
      !pointLockScriptPubKey(state.terms, expectation.scriptPubKey))
    return ChainClientResult::fail(failurePrefix() + "no point-lock description received from Alice");
  expectation.minValue = params.ctrAmount64();
  expectation.minConfirmations = requiredConfirmations(params);
  ChainClientResult verified = verifyLockedOutput(m_rpc, lockOutpoint(params), expectation);
  if (!verified.success) {
    verified.error = failurePrefix() + verified.error;
    return verified;
  }
  if (!fetchChainTip(m_rpc, tip, error) || !lockedValue(lockOutpoint(params), lockValue, error))
    return ChainClientResult::fail(failurePrefix() + error);
  const std::string stateError =
      pointLockStateError(m_chain, params, m_pubKey, tip, lockValue, currentClaimFee(lockValue), state);
  return stateError.empty() ? verified : ChainClientResult::fail(failurePrefix() + stateError);
}

ChainClientResult UtxoHtlcFullNode::claimPoint(const SwapParams& params) {
  if (!m_hasKey) return ChainClientResult::notSent(failurePrefix() + "no valid WIF key");
  PointLockState state;
  uint64_t tip = 0;
  uint64_t lockValue = 0;
  std::string error;
  if (!fetchChainTip(m_rpc, tip, error) || !lockedValue(lockOutpoint(params), lockValue, error))
    return ChainClientResult::notSent(failurePrefix() + error);
  const uint64_t currentFee = currentClaimFee(lockValue);
  const std::string stateError = pointLockStateError(m_chain, params, m_pubKey, tip, lockValue, currentFee, state);
  if (!stateError.empty()) return ChainClientResult::notSent(failurePrefix() + stateError);
  const std::string late = pointClaimDeadlineError(m_chain, state, tip);
  if (!late.empty()) return ChainClientResult::notSent(failurePrefix() + late);
  return broadcastPointClaim(params, state, claimOfferFor(state, currentFee), lockValue);
}

ChainClientResult UtxoHtlcFullNode::refundPoint(const SwapParams& params) {
  if (!m_hasKey) return ChainClientResult::notSent(failurePrefix() + "no valid WIF key");
  PointLockState state;
  uint64_t tip = 0;
  uint64_t lockValue = 0;
  std::string error;
  if (!decodePointLockState(params.chainState, state))
    return ChainClientResult::notSent(failurePrefix() + "no point-lock state");
  if (!fetchChainTip(m_rpc, tip, error) || !lockedValue(lockOutpoint(params), lockValue, error))
    return ChainClientResult::notSent(failurePrefix() + error);
  if (tip < state.terms.timeoutBlock)
    return ChainClientResult::notSent(failurePrefix() + "refund locked until height " +
                                      std::to_string(state.terms.timeoutBlock));
  PointLockSpend refund;
  refund.lock = lockOutpoint(params);
  refund.lockValue = lockValue;
  refund.fee = estimateFeeForSize(m_rpc, pointLockSpendVirtualSize(false), spendFeeCeiling(lockValue));
  refund.destinationScript = spendDestination(m_chain, false, params, m_pubKey);
  Bip340::Message sighash;
  Bip340::Signature aliceSignature;
  std::vector<uint8_t> rawTx;
  if (!pointLockRefundSighash(state.terms, refund, sighash) || !Bip340::sign(m_key, sighash, aliceSignature) ||
      !pointLockRefundTransaction(state.terms, refund, aliceSignature, rawTx))
    return ChainClientResult::notSent(failurePrefix() + "cannot build the refund");
  std::string txid;
  if (!broadcastRawTransaction(m_rpc, rawTx, txid, error))
    return ChainClientResult::fail(failurePrefix() + "broadcast failed: " + error);
  return ChainClientResult::ok(txid);
}

std::string UtxoHtlcFullNode::pointClaimDeadline(const SwapParams& params) {
  PointLockState state;
  uint64_t tip = 0;
  std::string error;
  if (!decodePointLockState(params.chainState, state)) return failurePrefix() + "no point-lock state";
  if (!fetchChainTip(m_rpc, tip, error)) return failurePrefix() + "chain tip unavailable: " + error;
  const std::string late = pointClaimDeadlineError(m_chain, state, tip);
  return late.empty() ? late : failurePrefix() + late;
}

std::string UtxoHtlcFullNode::pointSecret(const SwapParams& params) {
  PointLockState state;
  UtxoSpend spend;
  if (!decodePointLockState(params.chainState, state) || !findClaimSpend(params, spend)) return {};
  return secretFromPointClaim(params, state, spend.rawTx);
}

bool UtxoHtlcFullNode::hasConfirmedPointClaim(const SwapParams& params) {
  PointLockState state;
  UtxoSpend spend;
  const UtxoOutpoint lock = lockOutpoint(params);
  uint64_t lockHeight = 0;
  std::string error;
  return decodePointLockState(params.chainState, state) &&
         lookupWalletTxBlockHeight(m_rpc, lock.txid, lockHeight, error) &&
         m_spends.findSpend(m_rpc, lock, lockHeight, spend, error) &&
         spend.confirmations >= requiredConfirmations(params) &&
         !secretFromPointClaim(params, state, spend.rawTx).empty();
}

uint64_t UtxoHtlcFullNode::currentClaimFee(uint64_t lockValue) {
  return estimateFeeForSize(m_rpc, pointLockSpendVirtualSize(true), spendFeeCeiling(lockValue));
}

ChainClientResult UtxoHtlcFullNode::broadcastPointClaim(const SwapParams& params, const PointLockState& state,
                                                        const PointLockClaimOffer& offer, uint64_t lockValue) {
  std::vector<uint8_t> rawTx;
  if (!buildPointClaim(m_chain, params, state, offer, lockValue, m_pubKey, m_key, rawTx))
    return ChainClientResult::notSent(failurePrefix() + "cannot build the claim");
  std::string txid;
  std::string error;
  if (!broadcastRawTransaction(m_rpc, rawTx, txid, error))
    return ChainClientResult::fail(failurePrefix() + "broadcast failed: " + error);
  return ChainClientResult::ok(txid);
}

ChainClientResult UtxoHtlcFullNode::bumpClaim(const SwapParams& params) {
  if (!isPointLockSwap(params)) return ChainClientResult::notSent(failurePrefix() + "only point-lock claims carry a fee ladder");
  PointLockState state;
  Common::JsonValue claimTx;
  std::vector<uint8_t> rawClaim, paidTo;
  uint64_t lockValue = 0;
  uint64_t claimOutput = 0;
  std::string hex, error;
  if (!m_hasKey || !decodePointLockState(params.chainState, state) || !isTxidHex(params.ctrClaimTxId))
    return ChainClientResult::notSent(failurePrefix() + "no claim to replace");
  if (!m_rpc("getrawtransaction", "[" + quoted(params.ctrClaimTxId) + ",true]", claimTx, error) ||
      !claimTx.isObject() || !claimTx.contains("hex") || !claimTx("hex").isString() ||
      !decodeHex(claimTx("hex").getString(), rawClaim) || !readTransactionOutput(rawClaim, 0, claimOutput, paidTo))
    return ChainClientResult::notSent(failurePrefix() + "our claim is not known to the node");
  if (claimTx.contains("confirmations") && claimTx("confirmations").isInteger() &&
      claimTx("confirmations").getInteger() > 0)
    return ChainClientResult::notSent(failurePrefix() + "the claim is already confirmed");
  if (!lockedValue(lockOutpoint(params), lockValue, error) || claimOutput >= lockValue)
    return ChainClientResult::notSent(failurePrefix() + error);
  const PointLockClaimOffer* next = nextClaimOffer(state, lockValue - claimOutput);
  if (next == nullptr) return ChainClientResult::notSent(failurePrefix() + "the highest pre-signed fee is already in use");
  return broadcastPointClaim(params, state, *next, lockValue);
}

}  // namespace XfgSwap
