#include "DogeChainClient.h"
#include "DogeHtlcScript.h"
#include "Common/StringTools.h"
#include "../SwapHashLock.h"

#include <array>
#include <cstring>
#include <algorithm>

namespace XfgSwap {

static bool isZeroSecret(const Crypto::SecretKey& s) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&s);
  for (size_t i = 0; i < sizeof(Crypto::SecretKey); ++i) if (p[i]) return false;
  return true;
}

DogeChainClient::DogeChainClient(std::unique_ptr<DogeRpcClient> rpc, const std::string& wif)
  : m_rpc(std::move(rpc)), m_wif(wif) {}

ChainClientResult DogeChainClient::lock(const SwapParams& params) {
  if (!m_rpc)
    return ChainClientResult::fail("DOGE lock: RPC client not available");

  // Hashlock: Alice-locks uses published H(t)=SHA256(t); Bob-with-secret can derive.
  std::string hashHex;
  if (!isZeroSecret(params.adaptorSecret)) {
    hashHex = dogeHashLockHex(params.adaptorSecret);
  } else {
    bool nonzero = false;
    for (size_t i = 0; i < sizeof(params.hashLock); ++i)
      if (reinterpret_cast<const uint8_t*>(&params.hashLock)[i]) { nonzero = true; break; }
    if (!nonzero)
      return ChainClientResult::fail("DOGE lock: no adaptor secret or hashLock (need H(t) from Bob)");
    hashHex = Common::podToHex(params.hashLock);
  }

  // Recipient compressed pubkey for claim path. Prefer ctrPubKey; try getaddressinfo.
  std::string recipientKey = params.ctrPubKey;
  if (recipientKey.empty() && params.ctrAddress.size() == 66) {
    try {
      auto bytes = DogeHtlcScript::hexToBytes(params.ctrAddress);
      if (bytes.size() == 33 && (bytes[0] == 0x02 || bytes[0] == 0x03))
        recipientKey = params.ctrAddress;
    } catch (...) {}
  }
  if (recipientKey.size() != 66 && !params.ctrAddress.empty()) {
    std::string resolved;
    if (m_rpc->getAddressPubkey(params.ctrAddress, resolved) && resolved.size() == 66)
      recipientKey = resolved;
  }
  if (recipientKey.size() != 66)
    return ChainClientResult::fail(
        "DOGE lock: ctrPubKey must be 33-byte compressed pubkey hex (66 chars). "
        "A Doge address cannot be inverted to a pubkey; set ctrPubKey or use a wallet-known address.");

  std::string lockTxId;
  std::string redeemScriptHex;
  bool ok = m_rpc->lockHtlc(
      m_wif,
      recipientKey,
      hashHex,
      static_cast<uint32_t>(params.ctrTimeoutBlock),
      params.ctrAmount,
      lockTxId,
      redeemScriptHex);
  if (!ok) return ChainClientResult::fail("DOGE lockHtlc failed");
  return ChainClientResult::okWithState(lockTxId, redeemScriptHex);
}

ChainClientResult DogeChainClient::verifyLock(const SwapParams& params) {
  if (!m_rpc)
    return ChainClientResult::fail("DOGE verifyLock: no RPC client available");

  // ctrLockTxId is the funding txid; chainState holds redeem script hex when
  // available. Prefer P2SH address from redeem script (correct API for
  // listunspent) over treating txid as an address.
  std::string htlcAddress;
  if (!params.chainState.empty()) {
    auto redeem = DogeHtlcScript::hexToBytes(params.chainState);
    if (redeem.empty())
      return ChainClientResult::fail("DOGE verifyLock: invalid redeem script in chainState");
    htlcAddress = DogeHtlcScript::computeP2shAddress(redeem, /*testnet=*/false);
  } else if (!params.ctrAddress.empty() && params.ctrAddress.size() != 64) {
    // ctrAddress may already be the P2SH address (not a 64-char txid)
    htlcAddress = params.ctrAddress;
  } else {
    return ChainClientResult::fail(
        "DOGE verifyLock: need chainState (redeem script) or P2SH address — "
        "cannot listunspent by txid alone");
  }

  bool ok = m_rpc->verifyLock(htlcAddress, params.ctrAmount);
  if (!ok) return ChainClientResult::fail("DOGE lock not verified at " + htlcAddress);
  return ChainClientResult::ok(params.ctrLockTxId);
}

ChainClientResult DogeChainClient::claim(const SwapParams& params) {
  if (!m_rpc)
    return ChainClientResult::fail("DOGE claim: no RPC client available");

  std::string claimTxId;
  bool ok = m_rpc->claim(
      m_wif,
      params.ctrLockTxId, 0, params.ctrAmount,
      params.chainState,
      Common::podToHex(params.adaptorSecret),
      params.ctrAddress,
      claimTxId);
  if (!ok) return ChainClientResult::fail("DOGE claim failed");
  return ChainClientResult::ok(claimTxId);
}

ChainClientResult DogeChainClient::refund(const SwapParams& params) {
  if (!m_rpc)
    return ChainClientResult::fail("DOGE refund: no RPC client available");

  std::string refundTxId;
  bool ok = m_rpc->refundHtlc(
      m_wif,
      params.ctrLockTxId, 0, params.ctrAmount,
      params.chainState,
      static_cast<uint32_t>(params.ctrTimeoutBlock),
      params.ctrAddress,
      refundTxId);
  if (!ok) return ChainClientResult::fail("DOGE refundHtlc failed");
  return ChainClientResult::ok(refundTxId);
}

ChainClientResult DogeChainClient::verifyReserveProof(const std::string& expectedMessage,
    uint64_t minAmount, const std::string& proof) {
  if (!m_rpc)
    return ChainClientResult::fail("DOGE verifyReserveProof: RPC client not available");

  size_t c1 = proof.find(':');
  size_t c2 = proof.find(':', c1 + 1);
  if (c1 == std::string::npos || c2 == std::string::npos)
    return ChainClientResult::fail("DOGE reserve proof: invalid format (expected address:signature:message)");

  std::string address   = proof.substr(0, c1);
  std::string signature = proof.substr(c1 + 1, c2 - c1 - 1);
  std::string message   = proof.substr(c2 + 1);

  bool sigValid = false;
  if (!m_rpc->verifyMessage(address, signature, message, sigValid))
    return ChainClientResult::fail("DOGE reserve proof: verifymessage RPC failed");
  if (!sigValid)
    return ChainClientResult::fail("DOGE reserve proof: invalid signature");

  if (!expectedMessage.empty() && message != expectedMessage)
    return ChainClientResult::fail("DOGE reserve proof: message not bound to this offer");

  uint64_t balance = 0;
  if (!m_rpc->getBalance(address, balance))
    return ChainClientResult::fail("DOGE reserve proof: balance check RPC failed");
  if (balance < minAmount)
    return ChainClientResult::fail("DOGE reserve proof: insufficient balance (" +
                                   std::to_string(balance) + " < " + std::to_string(minAmount) + ")");

  return ChainClientResult::ok(address);
}

bool DogeChainClient::getCurrentHeight(uint64_t& height) {
  if (m_rpc) {
    return m_rpc->getBlockCount(height);
  }
  return false;
}

ChainClientResult DogeChainClient::getTransactionDetails(const std::string& txId,
                                                         ChainClientResult& result) {
  if (!m_rpc) {
    result = ChainClientResult::fail("DOGE: no RPC client available");
    return result;
  }

  DogeTxInfo txInfo;
  if (!m_rpc->getTransaction(txId, txInfo)) {
    result = ChainClientResult::fail("DOGE RPC: gettransaction failed for " + txId);
    return result;
  }

  uint64_t tipHeight = 0;
  m_rpc->getBlockCount(tipHeight);

  result.success = true;
  result.confirmed = txInfo.confirmations > 0;
  result.spvVerified = false;  // RPC mode: no SPV proof
  result.blockHeight = txInfo.blockHeight;
  result.confirmations = txInfo.confirmations;
  return result;
}

std::string DogeChainClient::extractSecret(const std::string& spendingTxid,
                                           const std::string& htlcRedeemScriptHex) {
  std::vector<uint8_t> redeemScript = DogeHtlcScript::hexToBytes(htlcRedeemScriptHex);
  std::vector<uint8_t> p2shScriptPubKey = DogeHtlcScript::redeemScriptToP2shScriptPubKey(redeemScript);

  if (!m_rpc) {
    return {};
  }

  std::string rawTxHex;
  if (!m_rpc->getRawTransaction(spendingTxid, rawTxHex)) {
    return {};
  }

  std::vector<uint8_t> rawTx = DogeHtlcScript::hexToBytes(rawTxHex);
  std::vector<uint8_t> preimage = DogeHtlcScript::parseClaimPreimage(rawTx, p2shScriptPubKey);
  if (preimage.empty()) {
    return {};
  }

  return DogeHtlcScript::bytesToHex(preimage);
}

std::string DogeChainClient::tryExtractClaimedSecret(const SwapParams& params) {
  if (params.chainState.empty() || params.ctrLockTxId.empty()) return {};

  // chainState is redeem-script hex; optional suffix ":<claimTxid>" if Bob told us the claim.
  std::string redeemHex = params.chainState;
  std::string knownClaimTxid;
  auto colon = params.chainState.find(':');
  if (colon != std::string::npos && colon + 1 < params.chainState.size()) {
    // Only treat as claim suffix if left part looks like hex redeem (even length)
    std::string left = params.chainState.substr(0, colon);
    std::string right = params.chainState.substr(colon + 1);
    if (!left.empty() && (left.size() % 2) == 0 && right.size() == 64) {
      redeemHex = left;
      knownClaimTxid = right;
    }
  }

  if (knownClaimTxid.empty() && !params.ctrClaimTxId.empty())
    knownClaimTxid = params.ctrClaimTxId;

  // Full-node: if we already know the claim txid (P2P SECRET_REVEAL or chainState), parse it.
  if (m_rpc && !knownClaimTxid.empty()) {
    std::string secret = extractSecret(knownClaimTxid, redeemHex);
    if (!secret.empty()) return secret;
  }

  return {};
}

} // namespace XfgSwap
