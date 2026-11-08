// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You can redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation. Fuego includes elements written
// by third parties. See file labeled LICENSE for more details.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

#include "UtxoHtlcSpv.h"
#include "UtxoPointLockSwap.h"

#include <algorithm>

namespace XfgSwap {

namespace {

constexpr uint64_t MIN_RELAY_FEE_PER_KILOBYTE = 1000;

bool quorumIncluded(const SpvTxInclusion& inclusion) {
  return inclusion.included && inclusion.merkleVerified && inclusion.blockHeight > 0;
}

}  // namespace

UtxoHtlcSpv::UtxoHtlcSpv(UtxoHtlcChain chain, std::shared_ptr<ISpvClient> spv, const std::string& wif)
    : m_chain(std::move(chain)), m_spv(std::move(spv)) {
  m_hasKey = m_spv && !wif.empty() && decodeWif(m_chain.network, wif, m_key) &&
             compressedPublicKey(m_key, m_pubKey);
}

std::string UtxoHtlcSpv::claimPubKeyHex() const {
  return m_hasKey ? encodeHex(m_pubKey) : std::string();
}

std::string UtxoHtlcSpv::receiveAddress() const {
  return m_hasKey ? p2pkhAddress(m_chain.network, m_pubKey) : std::string();
}

std::string UtxoHtlcSpv::readinessErrorFor(SwapRole role) {
  if (!m_hasKey)
    return failurePrefix() + "no WIF key valid for the '" + m_chain.network.nodeChainName +
           "' network is configured";
  if (role == SwapRole::ALICE) return failurePrefix() + "cannot fund a lock; Alice needs a full node";
  const std::string trustError = m_spv->newSwapTrustError();
  return trustError.empty() ? std::string() : failurePrefix() + trustError;
}

bool UtxoHtlcSpv::syncedTip(uint64_t& tip, std::string& error) {
  if (!m_spv->syncHeaders()) {
    error = "header sync failed";
    return false;
  }
  if (!m_spv->getTipHeight(tip)) {
    error = "servers do not agree on the chain tip";
    return false;
  }
  return true;
}

bool UtxoHtlcSpv::currentHeight(uint64_t& height) {
  std::string error;
  return syncedTip(height, error);
}

ChainClientResult UtxoHtlcSpv::lock(const SwapParams&) {
  return ChainClientResult::fail(failurePrefix() + "cannot fund a lock; Alice needs a full node");
}

bool UtxoHtlcSpv::lockOutput(const SwapParams& params, const std::vector<uint8_t>& expectedScriptPubKey,
                             uint64_t& value, std::string& error) {
  const UtxoOutpoint lock = lockOutpoint(params);
  std::vector<uint8_t> rawTx;
  std::vector<uint8_t> scriptPubKey;
  if (!isTxidHex(lock.txid) || !m_spv->getRawTx(lock.txid, rawTx)) {
    error = "lock transaction " + lock.txid + " is unavailable";
    return false;
  }
  if (!readTransactionOutput(rawTx, lock.vout, value, scriptPubKey)) {
    error = "lock transaction has no output " + std::to_string(lock.vout);
    return false;
  }
  if (scriptPubKey != expectedScriptPubKey) {
    error = "lock output " + lock.key() + " does not commit to the lock script";
    return false;
  }
  return true;
}

ChainClientResult UtxoHtlcSpv::verifyLock(const SwapParams& params) {
  if (isPointLockSwap(params)) return verifyPointLock(params);
  if (!m_hasKey) return ChainClientResult::fail(failurePrefix() + "no valid WIF key");
  uint64_t tip = 0;
  std::string error;
  if (!syncedTip(tip, error)) return ChainClientResult::fail(failurePrefix() + error);
  const std::string termsError = lockTermsError(m_chain, params, m_pubKey, tip);
  if (!termsError.empty()) return ChainClientResult::fail(failurePrefix() + termsError);

  std::vector<uint8_t> script;
  UtxoHtlcTerms terms;
  uint64_t value = 0;
  lockScriptFromState(params, script, terms);
  if (!lockOutput(params, htlcScriptPubKey(m_chain.encoding, script), value, error))
    return ChainClientResult::fail(failurePrefix() + error);
  if (value < params.ctrAmount64())
    return ChainClientResult::fail(failurePrefix() + "lock output holds " + std::to_string(value) +
                                   ", expected at least " + std::to_string(params.ctrAmount64()));

  const UtxoOutpoint lock = lockOutpoint(params);
  SpvTxInclusion inclusion;
  if (!m_spv->verifyTxInclusion(lock.txid, inclusion) || !quorumIncluded(inclusion))
    return ChainClientResult::fail(failurePrefix() + "lock transaction is not in a quorum-verified block");
  if (inclusion.depth < requiredConfirmations(params))
    return ChainClientResult::fail(failurePrefix() + "lock has " + std::to_string(inclusion.depth) + " of " +
                                   std::to_string(requiredConfirmations(params)) + " required confirmations");
  ChainClientResult verified = ChainClientResult::ok(lock.txid);
  verified.confirmed = true;
  verified.spvVerified = true;
  verified.blockHeight = inclusion.blockHeight;
  verified.confirmations = inclusion.depth;
  verified.outputIndex = lock.vout;
  return verified;
}

bool UtxoHtlcSpv::spendFee(bool claimPath, uint64_t lockValue, uint64_t& fee, std::string& error) {
  uint64_t relayFloor = 0;
  uint64_t nextBlocks = 0;
  if (!m_spv->feeRatesPerKilobyte(relayFloor, nextBlocks)) {
    error = "servers report no fee rate";
    return false;
  }
  fee = boundedFee(std::max(MIN_RELAY_FEE_PER_KILOBYTE, relayFloor), nextBlocks,
                   htlcSpendVirtualSize(m_chain.encoding, claimPath), spendFeeCeiling(lockValue));
  return true;
}

ChainClientResult UtxoHtlcSpv::broadcastSpend(bool claimPath, const SwapParams& params) {
  if (!m_hasKey) return ChainClientResult::notSent(failurePrefix() + "no valid WIF key");
  UtxoHtlcSpend spend;
  UtxoHtlcTerms terms;
  if (!lockScriptFromState(params, spend.htlcScript, terms))
    return ChainClientResult::notSent(failurePrefix() + "no lock script in swap state");
  spend.lock = lockOutpoint(params);
  spend.signingKey = m_key;
  uint64_t tip = 0;
  std::string error;
  if (!syncedTip(tip, error) ||
      !lockOutput(params, htlcScriptPubKey(m_chain.encoding, spend.htlcScript), spend.lockValue, error))
    return ChainClientResult::notSent(failurePrefix() + error);
  // A claim publishes t; it goes out only against a lock a quorum has buried,
  // so a server cannot harvest t from a claim of an output that does not exist.
  SpvTxInclusion inclusion;
  if (claimPath && (!m_spv->verifyTxInclusion(spend.lock.txid, inclusion) || !quorumIncluded(inclusion) ||
                    inclusion.depth < requiredConfirmations(params)))
    return ChainClientResult::notSent(failurePrefix() + "lock is not buried in a quorum-verified block");
  spend.destinationScript = spendDestination(m_chain, claimPath, params, m_pubKey);
  if (!spendFee(claimPath, spend.lockValue, spend.fee, error))
    return ChainClientResult::notSent(failurePrefix() + error);

  std::vector<uint8_t> rawTx;
  if (claimPath) {
    const std::string late = XfgSwap::claimDeadlineError(m_chain, terms, tip);
    if (!late.empty()) return ChainClientResult::notSent(failurePrefix() + late);
    if (!buildHtlcClaim(m_chain.encoding, spend, adaptorSecretBytes(params), rawTx, error))
      return ChainClientResult::notSent(failurePrefix() + "claim: " + error);
  } else {
    if (tip < terms.timeoutBlock)
      return ChainClientResult::notSent(failurePrefix() + "refund locked until height " +
                                        std::to_string(terms.timeoutBlock));
    if (!buildHtlcRefund(m_chain.encoding, spend, rawTx, error))
      return ChainClientResult::notSent(failurePrefix() + "refund: " + error);
  }
  std::string txid;
  if (!m_spv->broadcastTx(rawTx, txid))
    return ChainClientResult::fail(failurePrefix() + "no server accepted the transaction");
  return ChainClientResult::ok(txid);
}

ChainClientResult UtxoHtlcSpv::claim(const SwapParams& params) {
  if (isPointLockSwap(params)) return claimPoint(params);
  if (!hasAdaptorSecret(params))
    return ChainClientResult::notSent(failurePrefix() + "claim needs the adaptor secret");
  return broadcastSpend(true, params);
}

std::string UtxoHtlcSpv::claimDeadlineError(const SwapParams& params) {
  if (isPointLockSwap(params)) return pointClaimDeadline(params);
  std::vector<uint8_t> script;
  UtxoHtlcTerms terms;
  uint64_t tip = 0;
  std::string error;
  if (!lockScriptFromState(params, script, terms)) return failurePrefix() + "no lock script in swap state";
  if (!syncedTip(tip, error)) return failurePrefix() + error;
  const std::string late = XfgSwap::claimDeadlineError(m_chain, terms, tip);
  return late.empty() ? late : failurePrefix() + late;
}

ChainClientResult UtxoHtlcSpv::refund(const SwapParams& params) {
  if (isPointLockSwap(params))
    return ChainClientResult::notSent(failurePrefix() + "point locks are refunded by Alice's full node");
  return broadcastSpend(false, params);
}

ChainClientResult UtxoHtlcSpv::transactionDetails(const std::string& txid) {
  if (!isTxidHex(txid)) return ChainClientResult::fail(failurePrefix() + "malformed txid");
  uint64_t tip = 0;
  std::string error;
  SpvTxInclusion inclusion;
  if (!syncedTip(tip, error)) return ChainClientResult::fail(failurePrefix() + error);
  if (!m_spv->verifyTxInclusion(txid, inclusion))
    return ChainClientResult::fail(failurePrefix() + "no server answered for " + txid);
  ChainClientResult found = ChainClientResult::ok(txid);
  found.confirmed = quorumIncluded(inclusion);
  found.spvVerified = found.confirmed;
  found.blockHeight = found.confirmed ? inclusion.blockHeight : 0;
  found.confirmations = found.confirmed ? inclusion.depth : 0;
  return found;
}

bool UtxoHtlcSpv::claimPreimage(const SwapParams& params, const SpvSpend& spend,
                                std::vector<uint8_t>& preimage) const {
  std::vector<uint8_t> script;
  UtxoHtlcTerms terms;
  return lockScriptFromState(params, script, terms) &&
         extractHtlcPreimage(m_chain.encoding, spend.rawSpendingTx, lockOutpoint(params), script, preimage);
}

std::string UtxoHtlcSpv::tryExtractClaimedSecret(const SwapParams& params) {
  if (isPointLockSwap(params)) return {};
  // The preimage authenticates itself (its SHA256 must equal the hash lock),
  // so extraction proceeds even when the header quorum is unavailable.
  m_spv->syncHeaders();
  const UtxoOutpoint lock = lockOutpoint(params);
  SpvSpend spend;
  std::vector<uint8_t> preimage;
  if (isTxidHex(lock.txid) && m_spv->findSpend(lock.txid, lock.vout, spend) && spend.spent &&
      claimPreimage(params, spend, preimage))
    return encodeHex(preimage);
  SpvSpend announced;
  if (isTxidHex(params.ctrClaimTxId) && m_spv->getRawTx(params.ctrClaimTxId, announced.rawSpendingTx) &&
      claimPreimage(params, announced, preimage))
    return encodeHex(preimage);
  return {};
}

bool UtxoHtlcSpv::hasConfirmedClaim(const SwapParams& params) {
  if (isPointLockSwap(params)) return hasConfirmedPointClaim(params);
  uint64_t tip = 0;
  std::string error;
  const UtxoOutpoint lock = lockOutpoint(params);
  SpvSpend spend;
  std::vector<uint8_t> preimage;
  return syncedTip(tip, error) && isTxidHex(lock.txid) && m_spv->findSpend(lock.txid, lock.vout, spend) &&
         isVerifiedSpvSpend(spend, requiredConfirmations(params)) && claimPreimage(params, spend, preimage);
}

// ── Taproot point locks: Bob verifies and claims (CROSS_CURVE_PLAN X6) ──

ChainClientResult UtxoHtlcSpv::verifyPointLock(const SwapParams& params) {
  if (!m_hasKey) return ChainClientResult::fail(failurePrefix() + "no valid WIF key");
  PointLockState state;
  std::vector<uint8_t> lockScriptPubKey;
  uint64_t tip = 0;
  uint64_t lockValue = 0;
  std::string error;
  if (m_chain.network.bech32Hrp.empty() || !decodePointLockState(params.chainState, state) ||
      !pointLockScriptPubKey(state.terms, lockScriptPubKey))
    return ChainClientResult::fail(failurePrefix() + "no point-lock description received from Alice");
  if (!syncedTip(tip, error) || !lockOutput(params, lockScriptPubKey, lockValue, error))
    return ChainClientResult::fail(failurePrefix() + error);
  if (lockValue < params.ctrAmount64())
    return ChainClientResult::fail(failurePrefix() + "lock output holds " + std::to_string(lockValue) +
                                   ", expected at least " + std::to_string(params.ctrAmount64()));
  uint64_t currentFee = 0;
  if (!spendFee(true, lockValue, currentFee, error)) return ChainClientResult::fail(failurePrefix() + error);
  const std::string stateError = pointLockStateError(m_chain, params, m_pubKey, tip, lockValue, currentFee, state);
  if (!stateError.empty()) return ChainClientResult::fail(failurePrefix() + stateError);
  const UtxoOutpoint lock = lockOutpoint(params);
  SpvTxInclusion inclusion;
  if (!m_spv->verifyTxInclusion(lock.txid, inclusion) || !quorumIncluded(inclusion))
    return ChainClientResult::fail(failurePrefix() + "lock transaction is not in a quorum-verified block");
  if (inclusion.depth < requiredConfirmations(params))
    return ChainClientResult::fail(failurePrefix() + "lock has " + std::to_string(inclusion.depth) + " of " +
                                   std::to_string(requiredConfirmations(params)) + " required confirmations");
  ChainClientResult verified = ChainClientResult::ok(lock.txid);
  verified.confirmed = true;
  verified.spvVerified = true;
  verified.blockHeight = inclusion.blockHeight;
  verified.confirmations = inclusion.depth;
  verified.outputIndex = lock.vout;
  return verified;
}

ChainClientResult UtxoHtlcSpv::claimPoint(const SwapParams& params) {
  const ChainClientResult verified = verifyPointLock(params);
  if (!verified.success) return ChainClientResult::notSent(verified.error);
  PointLockState state;
  std::vector<uint8_t> lockScriptPubKey;
  uint64_t tip = 0;
  uint64_t lockValue = 0;
  uint64_t currentFee = 0;
  std::string error;
  decodePointLockState(params.chainState, state);
  pointLockScriptPubKey(state.terms, lockScriptPubKey);
  if (!syncedTip(tip, error) || !lockOutput(params, lockScriptPubKey, lockValue, error) ||
      !spendFee(true, lockValue, currentFee, error))
    return ChainClientResult::notSent(failurePrefix() + error);
  const std::string late = pointClaimDeadlineError(m_chain, state, tip);
  if (!late.empty()) return ChainClientResult::notSent(failurePrefix() + late);
  return broadcastPointClaim(params, state, claimOfferFor(state, currentFee), lockValue);
}

std::string UtxoHtlcSpv::pointClaimDeadline(const SwapParams& params) {
  PointLockState state;
  uint64_t tip = 0;
  std::string error;
  if (!decodePointLockState(params.chainState, state)) return failurePrefix() + "no point-lock state";
  if (!syncedTip(tip, error)) return failurePrefix() + error;
  const std::string late = pointClaimDeadlineError(m_chain, state, tip);
  return late.empty() ? late : failurePrefix() + late;
}

bool UtxoHtlcSpv::hasConfirmedPointClaim(const SwapParams& params) {
  PointLockState state;
  uint64_t tip = 0;
  std::string error;
  const UtxoOutpoint lock = lockOutpoint(params);
  SpvSpend spend;
  return decodePointLockState(params.chainState, state) && syncedTip(tip, error) && isTxidHex(lock.txid) &&
         m_spv->findSpend(lock.txid, lock.vout, spend) && isVerifiedSpvSpend(spend, requiredConfirmations(params)) &&
         !secretFromPointClaim(params, state, spend.rawSpendingTx).empty();
}

ChainClientResult UtxoHtlcSpv::broadcastPointClaim(const SwapParams& params, const PointLockState& state,
                                                   const PointLockClaimOffer& offer, uint64_t lockValue) {
  std::vector<uint8_t> rawTx;
  if (!buildPointClaim(m_chain, params, state, offer, lockValue, m_pubKey, m_key, rawTx))
    return ChainClientResult::notSent(failurePrefix() + "cannot build the claim");
  std::string txid;
  if (!m_spv->broadcastTx(rawTx, txid))
    return ChainClientResult::fail(failurePrefix() + "no server accepted the transaction");
  return ChainClientResult::ok(txid);
}

ChainClientResult UtxoHtlcSpv::bumpClaim(const SwapParams& params) {
  if (!isPointLockSwap(params)) return ChainClientResult::notSent(failurePrefix() + "only point-lock claims carry a fee ladder");
  PointLockState state;
  std::vector<uint8_t> lockScriptPubKey, rawClaim, paidTo;
  uint64_t lockValue = 0;
  uint64_t claimOutput = 0;
  std::string error;
  if (!m_hasKey || !decodePointLockState(params.chainState, state) || !isTxidHex(params.ctrClaimTxId) ||
      !pointLockScriptPubKey(state.terms, lockScriptPubKey))
    return ChainClientResult::notSent(failurePrefix() + "no claim to replace");
  SpvTxInclusion inclusion;
  if (m_spv->verifyTxInclusion(params.ctrClaimTxId, inclusion) && inclusion.included)
    return ChainClientResult::notSent(failurePrefix() + "the claim is already confirmed");
  if (!m_spv->getRawTx(params.ctrClaimTxId, rawClaim) || !readTransactionOutput(rawClaim, 0, claimOutput, paidTo) ||
      !lockOutput(params, lockScriptPubKey, lockValue, error) || claimOutput >= lockValue)
    return ChainClientResult::notSent(failurePrefix() + "our claim or the lock is not available from the servers");
  const PointLockClaimOffer* next = nextClaimOffer(state, lockValue - claimOutput);
  if (next == nullptr) return ChainClientResult::notSent(failurePrefix() + "the highest pre-signed fee is already in use");
  return broadcastPointClaim(params, state, *next, lockValue);
}

}  // namespace XfgSwap
