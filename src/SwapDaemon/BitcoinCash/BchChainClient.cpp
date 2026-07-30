#include "BchChainClient.h"
#include "HtlcScript.h"
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

BchChainClient::BchChainClient(std::unique_ptr<BchRpcClient> rpc, const std::string& wif)
  : m_rpc(std::move(rpc)), m_wif(wif) {}

BchChainClient::BchChainClient(std::shared_ptr<ISpvClient> spvClient, const std::string& wif)
  : m_spvClient(std::move(spvClient)), m_wif(wif) {}

ChainClientResult BchChainClient::lock(const SwapParams& params) {
  if (!m_rpc)
    return ChainClientResult::fail("BCH lock: RPC client not available (SPV mode does not support lock)");

  // Hashlock: Alice-locks uses published H(t)=SHA256(t); Bob-with-secret can derive.
  std::string hashHex;
  if (!isZeroSecret(params.adaptorSecret)) {
    hashHex = bchHashLockHex(params.adaptorSecret);
  } else {
    bool nonzero = false;
    for (size_t i = 0; i < sizeof(params.hashLock); ++i)
      if (reinterpret_cast<const uint8_t*>(&params.hashLock)[i]) { nonzero = true; break; }
    if (!nonzero)
      return ChainClientResult::fail("BCH lock: no adaptor secret or hashLock (need H(t) from Bob)");
    hashHex = Common::podToHex(params.hashLock);
  }

  // Recipient compressed pubkey for claim path. Prefer ctrPubKey; try getaddressinfo.
  std::string recipientKey = params.ctrPubKey;
  if (recipientKey.empty() && params.ctrAddress.size() == 66) {
    try {
      auto bytes = BchHtlcScript::hexToBytes(params.ctrAddress);
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
        "BCH lock: ctrPubKey must be 33-byte compressed pubkey hex (66 chars). "
        "Cashaddr cannot be inverted to a pubkey; set ctrPubKey or use a wallet-known address.");

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
  if (!ok) return ChainClientResult::fail("BCH lockHtlc failed");
  return ChainClientResult::okWithState(lockTxId, redeemScriptHex);
}

ChainClientResult BchChainClient::verifyLock(const SwapParams& params) {
  if (m_spvClient) {
    return verifyLockSpv(params);
  }

  if (!m_rpc)
    return ChainClientResult::fail("BCH verifyLock: no RPC or SPV client available");

  // ctrLockTxId is the funding txid; chainState holds redeemScript hex when
  // available. Prefer P2SH address from redeem script (correct API for
  // listunspent) over treating txid as an address.
  std::string htlcAddress;
  if (!params.chainState.empty()) {
    auto redeem = BchHtlcScript::hexToBytes(params.chainState);
    if (redeem.empty())
      return ChainClientResult::fail("BCH verifyLock: invalid redeem script in chainState");
    htlcAddress = BchHtlcScript::computeP2shAddress(redeem, /*testnet=*/false);
  } else if (!params.ctrAddress.empty() && params.ctrAddress.size() != 64) {
    // ctrAddress may already be the P2SH cashaddr/base58 (not a 64-char txid)
    htlcAddress = params.ctrAddress;
  } else {
    return ChainClientResult::fail(
        "BCH verifyLock: need chainState (redeem script) or P2SH address — "
        "cannot listunspent by txid alone");
  }

  bool ok = m_rpc->verifyLock(htlcAddress, params.ctrAmount);
  if (!ok) return ChainClientResult::fail("BCH lock not verified at " + htlcAddress);
  return ChainClientResult::ok(params.ctrLockTxId);
}

ChainClientResult BchChainClient::verifyLockSpv(const SwapParams& params) {
  // Fetch the raw locking tx
  std::vector<uint8_t> rawTx;
  if (!m_spvClient->getRawTx(params.ctrLockTxId, rawTx)) {
    return ChainClientResult::fail("BCH verifyLock SPV: getRawTx failed for " + params.ctrLockTxId);
  }

  // Parse the raw tx to find outputs paying to a P2SH address.
  // We look for any P2SH output (OP_HASH160 <20-byte-hash> OP_EQUAL) whose
  // value matches the expected amount.
  const uint8_t* p = rawTx.data();
  const uint8_t* end = rawTx.data() + rawTx.size();

  // Skip version (4 bytes)
  if (p + 4 > end)
    return ChainClientResult::fail("BCH verifyLock SPV: raw tx too short");
  p += 4;

  // Read vin count
  uint64_t vinCount = 0;
  // Inline varint reader
  if (p >= end) return ChainClientResult::fail("BCH verifyLock SPV: truncated tx");
  uint8_t first = *p++;
  if (first < 0xFD) {
    vinCount = first;
  } else if (first == 0xFD) {
    if (p + 2 > end) return ChainClientResult::fail("BCH verifyLock SPV: truncated tx");
    vinCount = static_cast<uint64_t>(p[0]) | (static_cast<uint64_t>(p[1]) << 8);
    p += 2;
  } else if (first == 0xFE) {
    if (p + 4 > end) return ChainClientResult::fail("BCH verifyLock SPV: truncated tx");
    vinCount = static_cast<uint64_t>(p[0]) | (static_cast<uint64_t>(p[1]) << 8) |
              (static_cast<uint64_t>(p[2]) << 16) | (static_cast<uint64_t>(p[3]) << 24);
    p += 4;
  } else {
    if (p + 8 > end) return ChainClientResult::fail("BCH verifyLock SPV: truncated tx");
    vinCount = 0;
    for (int i = 0; i < 8; ++i) {
      vinCount |= static_cast<uint64_t>(p[i]) << (i * 8);
    }
    p += 8;
  }

  // Skip inputs (each: 32-byte txid + 4-byte vout + varint scriptSigLen + scriptSig + 4-byte sequence)
  for (uint64_t i = 0; i < vinCount; ++i) {
    if (p + 36 > end) return ChainClientResult::fail("BCH verifyLock SPV: truncated tx inputs");
    p += 36;  // txid + vout
    uint64_t sigLen = 0;
    if (*p < 0xFD) {
      sigLen = *p++;
    } else if (*p == 0xFD) {
      ++p;
      if (p + 2 > end) return ChainClientResult::fail("BCH verifyLock SPV: truncated");
      sigLen = static_cast<uint64_t>(p[0]) | (static_cast<uint64_t>(p[1]) << 8);
      p += 2;
    } else {
      return ChainClientResult::fail("BCH verifyLock SPV: oversized scriptSig");
    }
    p += sigLen;
    if (p + 4 > end) return ChainClientResult::fail("BCH verifyLock SPV: truncated sequence");
    p += 4;  // sequence
  }

  // Read vout count
  uint64_t voutCount = 0;
  if (p >= end) return ChainClientResult::fail("BCH verifyLock SPV: no vouts");
  first = *p++;
  if (first < 0xFD) {
    voutCount = first;
  } else if (first == 0xFD) {
    if (p + 2 > end) return ChainClientResult::fail("BCH verifyLock SPV: truncated vout count");
    voutCount = static_cast<uint64_t>(p[0]) | (static_cast<uint64_t>(p[1]) << 8);
    p += 2;
  } else if (first == 0xFE) {
    if (p + 4 > end) return ChainClientResult::fail("BCH verifyLock SPV: truncated vout count");
    voutCount = static_cast<uint64_t>(p[0]) | (static_cast<uint64_t>(p[1]) << 8) |
               (static_cast<uint64_t>(p[2]) << 16) | (static_cast<uint64_t>(p[3]) << 24);
    p += 4;
  } else {
    if (p + 8 > end) return ChainClientResult::fail("BCH verifyLock SPV: truncated vout count");
    voutCount = 0;
    for (int i = 0; i < 8; ++i) {
      voutCount |= static_cast<uint64_t>(p[i]) << (i * 8);
    }
    p += 8;
  }

  // Extract the expected P2SH script hash from swap params for verification.
  // The redeem script hex is stored in params.chainState from the lock() call.
  std::vector<uint8_t> expectedScriptHash;
  bool haveExpectedHash = false;
  if (!params.chainState.empty()) {
    auto redeemScript = BchHtlcScript::hexToBytes(params.chainState);
    if (!redeemScript.empty()) {
      expectedScriptHash = BchHtlcScript::hash160(redeemScript);
      haveExpectedHash = true;
    }
  }

  bool foundP2sh = false;
  std::vector<uint8_t> onChainScriptHash;
  for (uint64_t i = 0; i < voutCount; ++i) {
    if (p + 8 > end) return ChainClientResult::fail("BCH verifyLock SPV: truncated output value");
    uint64_t value = 0;
    for (int j = 0; j < 8; ++j) {
      value |= static_cast<uint64_t>(p[j]) << (j * 8);
    }
    p += 8;

    // Read scriptPubKey
    uint64_t spkLen = 0;
    if (*p < 0xFD) {
      spkLen = *p++;
    } else if (*p == 0xFD) {
      ++p;
      if (p + 2 > end) return ChainClientResult::fail("BCH verifyLock SPV: truncated");
      spkLen = static_cast<uint64_t>(p[0]) | (static_cast<uint64_t>(p[1]) << 8);
      p += 2;
    } else {
      return ChainClientResult::fail("BCH verifyLock SPV: oversized scriptPubKey");
    }
    if (p + spkLen > end) return ChainClientResult::fail("BCH verifyLock SPV: truncated scriptPubKey");

    // Check if this is a P2SH output: OP_HASH160 <20 bytes> OP_EQUAL (23 bytes)
    if (spkLen == 23 && p[0] == 0xA9 && p[1] == 0x14 && p[22] == 0x87) {
      if (value >= params.ctrAmount) {
        foundP2sh = true;
        onChainScriptHash.assign(p + 2, p + 22);
      }
    }

    p += spkLen;
  }

  if (!foundP2sh) {
    return ChainClientResult::fail("BCH verifyLock SPV: no P2SH output with expected amount " +
                                   std::to_string(params.ctrAmount));
  }

  // Verify that the P2SH script hash matches the expected HTLC contract.
  if (haveExpectedHash && !expectedScriptHash.empty()) {
    if (onChainScriptHash != expectedScriptHash) {
      return ChainClientResult::fail(
          "BCH verifyLock SPV: P2SH script hash does not match expected HTLC contract");
    }
  }

  // Verify inclusion via SPV
  SpvTxInclusion inclusion;
  if (!m_spvClient->verifyTxInclusion(params.ctrLockTxId, inclusion)) {
    return ChainClientResult::fail("BCH verifyLock SPV: verifyTxInclusion failed");
  }

  uint64_t tipHeight = 0;
  m_spvClient->getTipHeight(tipHeight);

  ChainClientResult result = ChainClientResult::ok(params.ctrLockTxId);
  result.confirmed = inclusion.included;
  result.spvVerified = inclusion.merkleVerified;
  result.blockHeight = inclusion.blockHeight;
  result.confirmations = inclusion.depth;
  return result;
}

ChainClientResult BchChainClient::claim(const SwapParams& params) {
  if (!m_rpc) {
    if (m_wif.empty())
      return ChainClientResult::fail("BCH claim: no RPC or WIF available (SPV mode needs WIF for local signing)");

    std::array<uint8_t, 32> privKey{};
    if (!BchHtlcScript::wifToPrivKey(m_wif, privKey))
      return ChainClientResult::fail("BCH claim: invalid WIF");

    auto redeemScript = BchHtlcScript::hexToBytes(params.chainState);
    auto preimageBytes = BchHtlcScript::hexToBytes(Common::podToHex(params.adaptorSecret));

    uint8_t addrVersion = 0;
    std::vector<uint8_t> pubKeyHash;
    if (!BchHtlcScript::base58CheckDecode(params.ctrAddress, addrVersion, pubKeyHash))
      return ChainClientResult::fail("BCH claim: invalid destination address");
    auto outputScript = BchHtlcScript::buildP2pkhScriptPubKey(pubKeyHash);

    uint64_t fee = 1000;
    if (params.ctrAmount <= fee)
      return ChainClientResult::fail("BCH claim: amount too small for fee");
    uint64_t outputAmount = params.ctrAmount - fee;

    const uint32_t nSequence = 0xFFFFFFFD;

    auto der = BchHtlcScript::signInput(privKey, 1, 0, nSequence,
        params.ctrLockTxId, 0, redeemScript, params.ctrAmount,
        outputScript, outputAmount);
    if (der.empty())
      return ChainClientResult::fail("BCH claim SPV: signing failed");

    auto scriptSig = BchHtlcScript::createClaimScriptSig(der, preimageBytes, redeemScript);

    auto rawTx = BchHtlcScript::buildRawTransaction(
        params.ctrLockTxId, 0, params.ctrAmount,
        scriptSig, params.ctrAddress, outputAmount, 0);

    std::string txid;
    if (!m_spvClient->broadcastTx(rawTx, txid))
      return ChainClientResult::fail("BCH claim SPV: broadcast failed");
    return ChainClientResult::ok(txid);
  }

  std::string claimTxId;
  bool ok = m_rpc->claim(
      m_wif,
      params.ctrLockTxId, 0, params.ctrAmount,
      params.chainState,
      Common::podToHex(params.adaptorSecret),
      params.ctrAddress,
      claimTxId);
  if (!ok) return ChainClientResult::fail("BCH claim failed");
  return ChainClientResult::ok(claimTxId);
}

ChainClientResult BchChainClient::refund(const SwapParams& params) {
  if (!m_rpc) {
    if (m_wif.empty())
      return ChainClientResult::fail("BCH refund: no RPC or WIF available (SPV mode needs WIF for local signing)");

    std::array<uint8_t, 32> privKey{};
    if (!BchHtlcScript::wifToPrivKey(m_wif, privKey))
      return ChainClientResult::fail("BCH refund: invalid WIF");

    auto redeemScript = BchHtlcScript::hexToBytes(params.chainState);

    uint32_t nLocktime = static_cast<uint32_t>(params.ctrTimeoutBlock);

    uint8_t addrVersion = 0;
    std::vector<uint8_t> pubKeyHash;
    if (!BchHtlcScript::base58CheckDecode(params.ctrAddress, addrVersion, pubKeyHash))
      return ChainClientResult::fail("BCH refund: invalid destination address");
    auto outputScript = BchHtlcScript::buildP2pkhScriptPubKey(pubKeyHash);

    uint64_t fee = 1000;
    if (params.ctrAmount <= fee)
      return ChainClientResult::fail("BCH refund: amount too small for fee");
    uint64_t outputAmount = params.ctrAmount - fee;

    auto der = BchHtlcScript::signInput(privKey, 1, nLocktime,
        0xFFFFFFFE,
        params.ctrLockTxId, 0, redeemScript, params.ctrAmount,
        outputScript, outputAmount);
    if (der.empty())
      return ChainClientResult::fail("BCH refund SPV: signing failed");

    auto scriptSig = BchHtlcScript::createRefundScriptSig(der, redeemScript);

    auto rawTx = BchHtlcScript::buildRawTransaction(
        params.ctrLockTxId, 0, params.ctrAmount,
        scriptSig, params.ctrAddress, outputAmount, nLocktime);

    std::string txid;
    if (!m_spvClient->broadcastTx(rawTx, txid))
      return ChainClientResult::fail("BCH refund SPV: broadcast failed");
    return ChainClientResult::ok(txid);
  }

  std::string refundTxId;
  bool ok = m_rpc->refundHtlc(
      m_wif,
      params.ctrLockTxId, 0, params.ctrAmount,
      params.chainState,
      static_cast<uint32_t>(params.ctrTimeoutBlock),
      params.ctrAddress,
      refundTxId);
  if (!ok) return ChainClientResult::fail("BCH refundHtlc failed");
  return ChainClientResult::ok(refundTxId);
}

ChainClientResult BchChainClient::verifyReserveProof(const std::string& expectedMessage,
    uint64_t minAmount, const std::string& proof) {
  if (!m_rpc)
    return ChainClientResult::fail("BCH verifyReserveProof: RPC client not available");

  size_t c1 = proof.find(':');
  size_t c2 = proof.find(':', c1 + 1);
  if (c1 == std::string::npos || c2 == std::string::npos)
    return ChainClientResult::fail("BCH reserve proof: invalid format (expected address:signature:message)");

  std::string address   = proof.substr(0, c1);
  std::string signature = proof.substr(c1 + 1, c2 - c1 - 1);
  std::string message   = proof.substr(c2 + 1);

  bool sigValid = false;
  if (!m_rpc->verifyMessage(address, signature, message, sigValid))
    return ChainClientResult::fail("BCH reserve proof: verifymessage RPC failed");
  if (!sigValid)
    return ChainClientResult::fail("BCH reserve proof: invalid signature");

  if (!expectedMessage.empty() && message != expectedMessage)
    return ChainClientResult::fail("BCH reserve proof: message not bound to this offer");

  uint64_t balance = 0;
  if (!m_rpc->getBalance(address, balance))
    return ChainClientResult::fail("BCH reserve proof: balance check RPC failed");
  if (balance < minAmount)
    return ChainClientResult::fail("BCH reserve proof: insufficient balance (" +
                                   std::to_string(balance) + " < " + std::to_string(minAmount) + ")");

  return ChainClientResult::ok(address);
}

bool BchChainClient::getCurrentHeight(uint64_t& height) {
  if (m_spvClient) {
    return m_spvClient->getTipHeight(height);
  }
  if (m_rpc) {
    return m_rpc->getBlockCount(height);
  }
  return false;
}

ChainClientResult BchChainClient::getTransactionDetails(const std::string& txId,
                                                        ChainClientResult& result) {
  if (m_spvClient) {
    // SPV mode: check if tx is confirmed via header chain
    uint64_t tipHeight = 0;
    if (!m_spvClient->getTipHeight(tipHeight)) {
      result = ChainClientResult::fail("BCH SPV: cannot get tip height");
      return result;
    }

    // Try to verify tx inclusion (fetches header + merkle proof)
    SpvTxInclusion inclusion;
    if (!m_spvClient->verifyTxInclusion(txId, inclusion)) {
      // Tx not yet found — may still be unconfirmed or not relayed
      result = ChainClientResult::fail("BCH SPV: tx not found or not yet included in a block");
      result.confirmed = false;
      result.confirmations = 0;
      return result;
    }

    result.success = true;
    result.confirmed = true;
    result.spvVerified = true;
    result.blockHeight = inclusion.blockHeight;
    result.confirmations = (tipHeight >= inclusion.blockHeight)
        ? (tipHeight - inclusion.blockHeight + 1) : 1;
    return result;
  }

  if (m_rpc) {
    // Full-node RPC mode: use gettransaction
    BchTxInfo txInfo;
    if (!m_rpc->getTransaction(txId, txInfo)) {
      result = ChainClientResult::fail("BCH RPC: gettransaction failed for " + txId);
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

  result = ChainClientResult::fail("BCH: no RPC or SPV client available");
  return result;
}

std::string BchChainClient::extractSecret(const std::string& spendingTxid,
                                           const std::string& htlcRedeemScriptHex) {
  std::vector<uint8_t> redeemScript = BchHtlcScript::hexToBytes(htlcRedeemScriptHex);
  std::vector<uint8_t> p2shScriptPubKey = BchHtlcScript::redeemScriptToP2shScriptPubKey(redeemScript);

  if (m_spvClient) {
    return extractSecretSpv(spendingTxid, p2shScriptPubKey);
  }

  // RPC mode: fetch raw tx via getrawtransaction, then parse
  if (!m_rpc) {
    return {};
  }

  std::string rawTxHex;
  if (!m_rpc->getRawTransaction(spendingTxid, rawTxHex)) {
    return {};
  }

  std::vector<uint8_t> rawTx = BchHtlcScript::hexToBytes(rawTxHex);
  std::vector<uint8_t> preimage = BchHtlcScript::parseClaimPreimage(rawTx, p2shScriptPubKey);
  if (preimage.empty()) {
    return {};
  }

  return BchHtlcScript::bytesToHex(preimage);
}

std::string BchChainClient::extractSecretSpv(const std::string& spendingTxid,
                                              const std::vector<uint8_t>& htlcP2shScriptPubKey) {
  std::vector<uint8_t> rawSpendingTx;
  if (!m_spvClient->getRawTx(spendingTxid, rawSpendingTx)) {
    return {};
  }

  std::vector<uint8_t> preimage = BchHtlcScript::parseClaimPreimage(rawSpendingTx, htlcP2shScriptPubKey);
  if (preimage.empty()) {
    return {};
  }

  return BchHtlcScript::bytesToHex(preimage);
}

std::string BchChainClient::tryExtractClaimedSecret(const SwapParams& params) {
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

  // Prefer SPV findSpend on the lock tx (vout 0 is typical for single-output HTLC fund)
  if (m_spvClient) {
    for (uint32_t vout = 0; vout < 4; ++vout) {
      SpvSpend spend;
      if (!m_spvClient->findSpend(params.ctrLockTxId, vout, spend) || !spend.spent)
        continue;
      if (spend.spendingTxid.empty()) continue;
      std::string secret = extractSecret(spend.spendingTxid, redeemHex);
      if (!secret.empty()) return secret;
    }
  }

  // Full-node: if we already know the claim txid (P2P or chainState suffix), parse it.
  if (m_rpc && !knownClaimTxid.empty()) {
    std::string secret = extractSecret(knownClaimTxid, redeemHex);
    if (!secret.empty()) return secret;
  }

  return {};
}

} // namespace XfgSwap
