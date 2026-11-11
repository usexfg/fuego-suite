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

#include "UtxoHtlcFullNode.h"
#include "UtxoPointLockSwap.h"


#include <algorithm>
#include <limits>

namespace XfgSwap {

namespace {

constexpr size_t RECENT_WALLET_TRANSACTIONS = 1000;

std::string quoted(const std::string& s) { return "\"" + s + "\""; }

// listtransactions reports a send as a negative amount.
bool parseWalletSendAmount(const Common::JsonValue& value, uint64_t& units) {
  if (value.isReal()) return parseCoinAmount(Common::JsonValue(-value.getReal()), units);
  if (value.isInteger()) return parseCoinAmount(Common::JsonValue(-value.getInteger()), units);
  return false;
}

}  // namespace

UtxoHtlcFullNode::UtxoHtlcFullNode(UtxoHtlcChain chain, UtxoRpcCall rpc, const std::string& wif)
    : m_chain(std::move(chain)), m_rpc(std::move(rpc)) {
  m_hasKey = !wif.empty() && decodeWif(m_chain.network, wif, m_key) &&
             compressedPublicKey(m_key, m_pubKey);
}

std::string UtxoHtlcFullNode::claimPubKeyHex() const {
  return m_hasKey ? encodeHex(m_pubKey) : std::string();
}

std::string UtxoHtlcFullNode::receiveAddress() const {
  return m_hasKey ? p2pkhAddress(m_chain.network, m_pubKey) : std::string();
}

std::string UtxoHtlcFullNode::readinessError() {
  if (!m_hasKey)
    return failurePrefix() + "no WIF key valid for the '" + m_chain.network.nodeChainName +
           "' network is configured";
  const UtxoNodeHealth health = checkNodeHealth(m_rpc, m_chain.network.nodeChainName);
  return health.ready ? std::string() : failurePrefix() + health.reason;
}

bool UtxoHtlcFullNode::currentHeight(uint64_t& height) {
  std::string error;
  return fetchChainTip(m_rpc, height, error);
}

bool UtxoHtlcFullNode::findPriorLockSend(const std::vector<uint8_t>& lockScriptPubKey, uint64_t amount,
                                         std::string& txid) {
  // A lost sendtoaddress response must not fund the same lock twice: look for
  // a wallet send whose transaction pays exactly this amount to the lock
  // script. Outputs are compared by script, since nodes report addresses in
  // their own format (BCHN: CashAddr).
  Common::JsonValue sends;
  std::string error;
  if (!m_rpc("listtransactions",
             "[\"*\"," + std::to_string(RECENT_WALLET_TRANSACTIONS) + ",0,true]", sends, error) ||
      !sends.isArray())
    return false;
  for (size_t i = 0; i < sends.size(); ++i) {
    const Common::JsonValue& entry = sends[i];
    uint64_t sent = 0;
    if (!entry.isObject() || !entry.contains("category") || !entry("category").isString() ||
        entry("category").getString() != "send" || !entry.contains("amount") ||
        !parseWalletSendAmount(entry("amount"), sent) || sent != amount || !entry.contains("txid") ||
        !entry("txid").isString() || !isTxidHex(entry("txid").getString()))
      continue;
    if (walletTransactionPays(m_rpc, entry("txid").getString(), lockScriptPubKey, amount)) {
      txid = entry("txid").getString();
      return true;
    }
  }
  return false;
}

ChainClientResult UtxoHtlcFullNode::lock(const SwapParams& params) {
  if (isPointLockSwap(params)) return lockPoint(params);
  if (!m_hasKey) return ChainClientResult::fail(failurePrefix() + "no valid WIF key");
  UtxoHtlcTerms terms;
  if (!agreedHashLock(params, terms.hashLock))
    return ChainClientResult::fail(failurePrefix() + "no hash lock H(t) from Bob");
  if (!decodeHex(params.ctrPubKey, terms.recipientPubKey) ||
      !isCompressedPublicKey(terms.recipientPubKey))
    return ChainClientResult::fail(failurePrefix() +
                                   "Bob's claim public key has not been received");
  if (params.ctrTimeoutBlock > std::numeric_limits<uint32_t>::max())
    return ChainClientResult::fail(failurePrefix() + "timeout height out of range");
  terms.senderPubKey = m_pubKey;
  terms.timeoutBlock = static_cast<uint32_t>(params.ctrTimeoutBlock);
  const std::vector<uint8_t> script = buildHtlcScript(terms);
  if (script.empty()) return ChainClientResult::fail(failurePrefix() + "invalid lock terms");

  const std::string address = htlcFundingAddress(m_chain.encoding, m_chain.network, script);
  const std::vector<uint8_t> lockScriptPubKey = htlcScriptPubKey(m_chain.encoding, script);
  const uint64_t amount = params.ctrAmount64();
  std::string txid;
  if (!findPriorLockSend(lockScriptPubKey, amount, txid)) {
    Common::JsonValue result;
    std::string error;
    if (!m_rpc("sendtoaddress", "[" + quoted(address) + "," + formatCoinAmount(amount) + "]",
               result, error))
      return ChainClientResult::fail(failurePrefix() + "sendtoaddress failed: " + error);
    if (!result.isString() || !isTxidHex(result.getString()))
      return ChainClientResult::fail(failurePrefix() + "sendtoaddress returned no txid");
    txid = result.getString();
  }

  uint32_t vout = 0;
  std::string error;
  if (!findWalletOutputIndex(m_rpc, txid, lockScriptPubKey, vout, error))
    return ChainClientResult::fail(failurePrefix() + error);
  ChainClientResult locked = ChainClientResult::okWithState(txid, encodeHex(script));
  locked.outputIndex = vout;
  return locked;
}

ChainClientResult UtxoHtlcFullNode::verifyLock(const SwapParams& params) {
  if (isPointLockSwap(params)) return verifyPointLock(params);
  if (!m_hasKey) return ChainClientResult::fail(failurePrefix() + "no valid WIF key");
  uint64_t tip = 0;
  std::string error;
  if (!fetchChainTip(m_rpc, tip, error))
    return ChainClientResult::fail(failurePrefix() + "chain tip unavailable: " + error);
  const std::string termsError = lockTermsError(m_chain, params, m_pubKey, tip);
  if (!termsError.empty()) return ChainClientResult::fail(failurePrefix() + termsError);

  std::vector<uint8_t> script;
  UtxoHtlcTerms terms;
  lockScriptFromState(params, script, terms);
  UtxoLockExpectation expectation;
  expectation.scriptPubKey = htlcScriptPubKey(m_chain.encoding, script);
  expectation.minValue = params.ctrAmount64();
  expectation.minConfirmations = requiredConfirmations(params);
  ChainClientResult verified = verifyLockedOutput(m_rpc, lockOutpoint(params), expectation);
  if (!verified.success) verified.error = failurePrefix() + verified.error;
  return verified;
}

bool UtxoHtlcFullNode::lockedValue(const UtxoOutpoint& lock, uint64_t& value, std::string& error) {
  UtxoUnspentOutput output;
  if (lookupUnspentOutput(m_rpc, lock, false, output, error) != UtxoOutputLookup::UNSPENT) {
    if (error.empty()) error = "lock output " + lock.key() + " is already spent or unconfirmed";
    return false;
  }
  value = output.value;
  return true;
}

ChainClientResult UtxoHtlcFullNode::broadcastSpend(bool claimPath, const SwapParams& params) {
  if (!m_hasKey) return ChainClientResult::notSent(failurePrefix() + "no valid WIF key");
  UtxoHtlcSpend spend;
  UtxoHtlcTerms terms;
  if (!lockScriptFromState(params, spend.htlcScript, terms))
    return ChainClientResult::notSent(failurePrefix() + "no lock script in swap state");
  spend.lock = lockOutpoint(params);
  spend.signingKey = m_key;
  std::string error;
  if (!lockedValue(spend.lock, spend.lockValue, error))
    return ChainClientResult::notSent(failurePrefix() + error);

  spend.destinationScript = spendDestination(m_chain, claimPath, params, m_pubKey);
  spend.fee = estimateFeeForSize(m_rpc, htlcSpendVirtualSize(m_chain.encoding, claimPath),
                                 spendFeeCeiling(spend.lockValue));

  uint64_t tip = 0;
  if (!fetchChainTip(m_rpc, tip, error))
    return ChainClientResult::notSent(failurePrefix() + "chain tip unavailable: " + error);
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
  if (!broadcastRawTransaction(m_rpc, rawTx, txid, error))
    return ChainClientResult::fail(failurePrefix() + "broadcast failed: " + error);
  return ChainClientResult::ok(txid);
}

ChainClientResult UtxoHtlcFullNode::claim(const SwapParams& params) {
  if (isPointLockSwap(params)) return claimPoint(params);
  if (!hasAdaptorSecret(params))
    return ChainClientResult::notSent(failurePrefix() + "claim needs the adaptor secret");
  return broadcastSpend(true, params);
}

std::string UtxoHtlcFullNode::claimDeadlineError(const SwapParams& params) {
  if (isPointLockSwap(params)) return pointClaimDeadline(params);
  std::vector<uint8_t> script;
  UtxoHtlcTerms terms;
  uint64_t tip = 0;
  std::string error;
  if (!lockScriptFromState(params, script, terms)) return failurePrefix() + "no lock script in swap state";
  if (!fetchChainTip(m_rpc, tip, error)) return failurePrefix() + "chain tip unavailable: " + error;
  const std::string late = XfgSwap::claimDeadlineError(m_chain, terms, tip);
  return late.empty() ? late : failurePrefix() + late;
}

ChainClientResult UtxoHtlcFullNode::refund(const SwapParams& params) {
  if (isPointLockSwap(params)) return refundPoint(params);
  return broadcastSpend(false, params);
}

ChainClientResult UtxoHtlcFullNode::transactionDetails(const std::string& txid) {
  if (!isTxidHex(txid)) return ChainClientResult::fail(failurePrefix() + "malformed txid");
  // Claims and refunds built here have exactly one output, at index 0.
  UtxoUnspentOutput output;
  std::string error;
  if (lookupUnspentOutput(m_rpc, {txid, 0}, true, output, error) == UtxoOutputLookup::UNSPENT) {
    ChainClientResult found = ChainClientResult::ok(txid);
    found.confirmed = output.confirmations > 0;
    found.confirmations = output.confirmations;
    return found;
  }
  for (const char* method : {"gettransaction", "getrawtransaction"}) {
    Common::JsonValue tx;
    const std::string params = std::string(method) == "gettransaction"
                                   ? "[" + quoted(txid) + "]"
                                   : "[" + quoted(txid) + ",true]";
    if (!m_rpc(method, params, tx, error) || !tx.isObject()) continue;
    ChainClientResult found = ChainClientResult::ok(txid);
    const int64_t confirmations = tx.contains("confirmations") && tx("confirmations").isInteger()
                                      ? tx("confirmations").getInteger()
                                      : 0;
    found.confirmations = confirmations > 0 ? static_cast<uint32_t>(confirmations) : 0;
    found.confirmed = found.confirmations > 0;
    return found;
  }
  return ChainClientResult::fail(failurePrefix() + "transaction " + txid + " not found");
}

bool UtxoHtlcFullNode::claimHintSpend(const SwapParams& params, UtxoSpend& spend) {
  if (!isTxidHex(params.ctrClaimTxId)) return false;
  Common::JsonValue raw;
  std::string error;
  if (!m_rpc("getrawtransaction", "[" + quoted(params.ctrClaimTxId) + ",false]", raw, error) ||
      !raw.isString() || !decodeHex(raw.getString(), spend.rawTx))
    return false;
  spend.spendingTxid = params.ctrClaimTxId;
  return true;
}

bool UtxoHtlcFullNode::findClaimSpend(const SwapParams& params, UtxoSpend& spend) {
  const UtxoOutpoint lock = lockOutpoint(params);
  uint64_t lockHeight = 0;
  std::string error;
  if (lookupWalletTxBlockHeight(m_rpc, lock.txid, lockHeight, error) &&
      m_spends.findSpend(m_rpc, lock, lockHeight, spend, error))
    return true;
  // The claimer's announced txid is only a hint: extraction below re-checks
  // that it spends this exact outpoint and opens this exact hash lock.
  return claimHintSpend(params, spend);
}

std::string UtxoHtlcFullNode::tryExtractClaimedSecret(const SwapParams& params) {
  if (isPointLockSwap(params)) return pointSecret(params);
  std::vector<uint8_t> script;
  UtxoHtlcTerms terms;
  UtxoSpend spend;
  std::vector<uint8_t> preimage;
  if (!lockScriptFromState(params, script, terms) || !findClaimSpend(params, spend) ||
      !extractHtlcPreimage(m_chain.encoding, spend.rawTx, lockOutpoint(params), script, preimage))
    return {};
  return encodeHex(preimage);
}

bool UtxoHtlcFullNode::hasConfirmedClaim(const SwapParams& params) {
  if (isPointLockSwap(params)) return hasConfirmedPointClaim(params);
  std::vector<uint8_t> script;
  UtxoHtlcTerms terms;
  UtxoSpend spend;
  std::vector<uint8_t> preimage;
  const UtxoOutpoint lock = lockOutpoint(params);
  uint64_t lockHeight = 0;
  std::string error;
  return lockScriptFromState(params, script, terms) &&
         lookupWalletTxBlockHeight(m_rpc, lock.txid, lockHeight, error) &&
         m_spends.findSpend(m_rpc, lock, lockHeight, spend, error) &&
         spend.confirmations >= requiredConfirmations(params) &&
         extractHtlcPreimage(m_chain.encoding, spend.rawTx, lock, script, preimage);
}

}  // namespace XfgSwap
