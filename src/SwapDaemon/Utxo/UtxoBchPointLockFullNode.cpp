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
// BCH P2SH point locks over a full node (CROSS_CURVE_PLAN X7).

#include "BchPointLockSwap.h"
#include "UtxoHtlcFullNode.h"

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

ChainClientResult UtxoHtlcFullNode::lockBchPoint(const SwapParams& params) {
  if (!m_hasKey) return ChainClientResult::fail(failurePrefix() + "no valid WIF key");
  BchPointLockState state;
  std::vector<uint8_t> bobKey;
  Bip340::Point adaptorPoint;
  std::string error;
  if (!bchLockerTerms(params, m_pubKey, state.terms, bobKey, error))
    return ChainClientResult::fail(failurePrefix() + error);
  if (!adaptorPointOf(params, adaptorPoint)) return ChainClientResult::fail(failurePrefix() + "no adaptor point T_secp");
  const std::vector<uint8_t> lockScriptPubKey = bchPointLockScriptPubKey(state.terms);

  const uint64_t amount = params.ctrAmount64();
  std::string txid;
  if (!findPriorLockSend(lockScriptPubKey, amount, txid)) {
    const std::string address = bchPointLockAddress(m_chain.network, state.terms);
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
  for (const uint64_t fee :
       claimFeeLadder(estimateFeeForSize(m_rpc, bchPointLockSpendVirtualSize(true), ceiling), ceiling)) {
    BchPointLockClaimOffer offer;
    offer.fee = fee;
    BchSchnorr::Message sighash;
    if (!bchPointLockClaimSighash(state.terms, pointClaimSpend(m_chain, locked, amount, fee, bobKey), sighash) ||
        !BchSchnorr::presign(m_key, sighash, adaptorPoint, offer.presig))
      return ChainClientResult::fail(failurePrefix() + "cannot pre-sign Bob's claim (lock " + txid + " is funded)");
    state.claimOffers.push_back(offer);
  }
  if (state.claimOffers.empty())
    return ChainClientResult::fail(failurePrefix() + "no claim fee fits the lock (lock " + txid + " is funded)");
  ChainClientResult result = ChainClientResult::okWithState(txid, encodeBchPointLockState(state));
  result.outputIndex = vout;
  return result;
}

ChainClientResult UtxoHtlcFullNode::verifyBchPointLock(const SwapParams& params) {
  if (!m_hasKey) return ChainClientResult::fail(failurePrefix() + "no valid WIF key");
  BchPointLockState state;
  UtxoLockExpectation expectation;
  uint64_t tip = 0;
  uint64_t lockValue = 0;
  std::string error;
  if (!decodeBchPointLockState(params.chainState, state))
    return ChainClientResult::fail(failurePrefix() + "no point-lock description received from Alice");
  expectation.scriptPubKey = bchPointLockScriptPubKey(state.terms);
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
      bchPointLockStateError(m_chain, params, m_pubKey, tip, lockValue, currentBchClaimFee(lockValue), state);
  return stateError.empty() ? verified : ChainClientResult::fail(failurePrefix() + stateError);
}

ChainClientResult UtxoHtlcFullNode::claimBchPoint(const SwapParams& params) {
  if (!m_hasKey) return ChainClientResult::notSent(failurePrefix() + "no valid WIF key");
  BchPointLockState state;
  uint64_t tip = 0;
  uint64_t lockValue = 0;
  std::string error;
  if (!fetchChainTip(m_rpc, tip, error) || !lockedValue(lockOutpoint(params), lockValue, error))
    return ChainClientResult::notSent(failurePrefix() + error);
  const std::string stateError =
      bchPointLockStateError(m_chain, params, m_pubKey, tip, lockValue, currentBchClaimFee(lockValue), state);
  if (!stateError.empty()) return ChainClientResult::notSent(failurePrefix() + stateError);
  const std::string late = bchPointClaimDeadlineError(m_chain, state, tip);
  if (!late.empty()) return ChainClientResult::notSent(failurePrefix() + late);
  std::vector<uint8_t> rawTx;
  if (!buildBchPointClaim(m_chain, params, state, lockValue, m_pubKey, m_key, rawTx))
    return ChainClientResult::notSent(failurePrefix() + "cannot build the claim");
  std::string txid;
  if (!broadcastRawTransaction(m_rpc, rawTx, txid, error))
    return ChainClientResult::fail(failurePrefix() + "broadcast failed: " + error);
  return ChainClientResult::ok(txid);
}

ChainClientResult UtxoHtlcFullNode::refundBchPoint(const SwapParams& params) {
  if (!m_hasKey) return ChainClientResult::notSent(failurePrefix() + "no valid WIF key");
  BchPointLockState state;
  uint64_t tip = 0;
  uint64_t lockValue = 0;
  std::string error;
  if (!decodeBchPointLockState(params.chainState, state))
    return ChainClientResult::notSent(failurePrefix() + "no point-lock state");
  if (!fetchChainTip(m_rpc, tip, error) || !lockedValue(lockOutpoint(params), lockValue, error))
    return ChainClientResult::notSent(failurePrefix() + error);
  if (tip < state.terms.timeoutBlock)
    return ChainClientResult::notSent(failurePrefix() + "refund locked until height " +
                                      std::to_string(state.terms.timeoutBlock));
  PointLockSpend refund;
  refund.lock = lockOutpoint(params);
  refund.lockValue = lockValue;
  refund.fee = estimateFeeForSize(m_rpc, bchPointLockSpendVirtualSize(false), spendFeeCeiling(lockValue));
  refund.destinationScript = spendDestination(m_chain, false, params, m_pubKey);
  BchSchnorr::Message sighash;
  BchSchnorr::Signature aliceSignature;
  std::vector<uint8_t> rawTx;
  if (!bchPointLockRefundSighash(state.terms, refund, sighash) || !BchSchnorr::sign(m_key, sighash, aliceSignature) ||
      !bchPointLockRefundTransaction(state.terms, refund, aliceSignature, rawTx))
    return ChainClientResult::notSent(failurePrefix() + "cannot build the refund");
  std::string txid;
  if (!broadcastRawTransaction(m_rpc, rawTx, txid, error))
    return ChainClientResult::fail(failurePrefix() + "broadcast failed: " + error);
  return ChainClientResult::ok(txid);
}

std::string UtxoHtlcFullNode::bchPointClaimDeadline(const SwapParams& params) {
  BchPointLockState state;
  uint64_t tip = 0;
  std::string error;
  if (!decodeBchPointLockState(params.chainState, state)) return failurePrefix() + "no point-lock state";
  if (!fetchChainTip(m_rpc, tip, error)) return failurePrefix() + "chain tip unavailable: " + error;
  const std::string late = bchPointClaimDeadlineError(m_chain, state, tip);
  return late.empty() ? late : failurePrefix() + late;
}

std::string UtxoHtlcFullNode::bchPointSecret(const SwapParams& params) {
  BchPointLockState state;
  UtxoSpend spend;
  if (!decodeBchPointLockState(params.chainState, state) || !findClaimSpend(params, spend)) return {};
  return secretFromBchPointClaim(params, state, spend.rawTx);
}

bool UtxoHtlcFullNode::hasConfirmedBchPointClaim(const SwapParams& params) {
  BchPointLockState state;
  UtxoSpend spend;
  const UtxoOutpoint lock = lockOutpoint(params);
  uint64_t lockHeight = 0;
  std::string error;
  return decodeBchPointLockState(params.chainState, state) &&
         lookupWalletTxBlockHeight(m_rpc, lock.txid, lockHeight, error) &&
         m_spends.findSpend(m_rpc, lock, lockHeight, spend, error) &&
         spend.confirmations >= requiredConfirmations(params) &&
         !secretFromBchPointClaim(params, state, spend.rawTx).empty();
}

uint64_t UtxoHtlcFullNode::currentBchClaimFee(uint64_t lockValue) {
  return estimateFeeForSize(m_rpc, bchPointLockSpendVirtualSize(true), spendFeeCeiling(lockValue));
}

}  // namespace XfgSwap
