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

#include "LtcChainClient.h"
#include "LtcHtlcScript.h"
#include "Bitcoin/BtcHtlcScript.h"
#include "Common/StringTools.h"
#include "../SwapHashLock.h"

#include <array>
#include <cstring>
#include <algorithm>
#include "Komodo/KmdHtlcScript.h"

namespace XfgSwap {

// Inline varint reader (duplicated from LtcHtlcScript.cpp for isolation)
static bool readVarInt(const uint8_t*& p, const uint8_t* end, uint64_t& out) {
  if (p >= end) return false;
  uint8_t first = *p++;
  if (first < 0xFD) {
    out = first;
    return true;
  } else if (first == 0xFD) {
    if (p + 2 > end) return false;
    out = static_cast<uint64_t>(p[0]) | (static_cast<uint64_t>(p[1]) << 8);
    p += 2;
    return true;
  } else if (first == 0xFE) {
    if (p + 4 > end) return false;
    out = static_cast<uint64_t>(p[0]) | (static_cast<uint64_t>(p[1]) << 8) |
          (static_cast<uint64_t>(p[2]) << 16) | (static_cast<uint64_t>(p[3]) << 24);
    p += 4;
    return true;
  } else {
    if (p + 8 > end) return false;
    out = 0;
    for (int i = 0; i < 8; ++i) {
      out |= static_cast<uint64_t>(p[i]) << (i * 8);
    }
    p += 8;
    return true;
  }
}

std::string LtcChainClient::getReceiveAddress() const {
  if (m_wif.empty()) return "";
  std::vector<uint8_t> h;
  if (!KmdHtlcScript::wifToPubkeyHash(m_wif, h)) return "";
  return KmdHtlcScript::base58CheckEncode(0x30, h);
}
static bool isZeroSecret(const Crypto::SecretKey& s) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&s);
  for (size_t i = 0; i < sizeof(Crypto::SecretKey); ++i) if (p[i]) return false;
  return true;
}

LtcChainClient::LtcChainClient(std::unique_ptr<LtcRpcClient> rpc, const std::string& wif)
  : m_rpc(std::move(rpc)), m_wif(wif) {}

LtcChainClient::LtcChainClient(std::shared_ptr<ISpvClient> spvClient, const std::string& wif)
  : m_spvClient(std::move(spvClient)), m_wif(wif) {}

ChainClientResult LtcChainClient::lock(const SwapParams& params) {
  if (!m_rpc)
    return ChainClientResult::fail("LTC lock: RPC client not available (SPV mode does not support lock)");

  std::string hashHex;
  if (!isZeroSecret(params.adaptorSecret)) {
    hashHex = bchHashLockHex(params.adaptorSecret);
  } else {
    bool nonzero = false;
    for (size_t i = 0; i < sizeof(params.hashLock); ++i)
      if (reinterpret_cast<const uint8_t*>(&params.hashLock)[i]) { nonzero = true; break; }
    if (!nonzero)
      return ChainClientResult::fail("LTC lock: no adaptor secret or hashLock (need H(t) from Bob)");
    hashHex = Common::podToHex(params.hashLock);
  }

  std::string recipientKey = params.ctrPubKey;
  if (recipientKey.empty() && params.ctrAddress.size() == 66) {
    try {
      auto bytes = BtcHtlcScript::hexToBytes(params.ctrAddress);
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
        "LTC lock: ctrPubKey must be 33-byte compressed pubkey hex (66 chars). "
        "Cannot derive pubkey from address; set ctrPubKey or use a wallet-known address.");

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
  if (!ok) return ChainClientResult::fail("LTC lockHtlc failed");
  return ChainClientResult::okWithState(lockTxId, redeemScriptHex);
}

ChainClientResult LtcChainClient::verifyLock(const SwapParams& params) {
  if (m_spvClient) {
    return verifyLockSpv(params);
  }

  if (!m_rpc)
    return ChainClientResult::fail("LTC verifyLock: no RPC or SPV client available");

  std::string htlcAddress;
  if (!params.chainState.empty()) {
    auto redeem = BtcHtlcScript::hexToBytes(params.chainState);
    if (redeem.empty())
      return ChainClientResult::fail("LTC verifyLock: invalid redeem script in chainState");
    htlcAddress = BtcHtlcScript::witnessScriptToAddress(redeem, "ltc");
  } else {
    return ChainClientResult::fail(
        "LTC verifyLock: need chainState (redeem script) — "
        "cannot listunspent by txid alone");
  }

  bool ok = m_rpc->verifyLock(htlcAddress, params.ctrAmount);
  if (!ok) return ChainClientResult::fail("LTC lock not verified at " + htlcAddress);
  return ChainClientResult::ok(params.ctrLockTxId);
}

ChainClientResult LtcChainClient::verifyLockSpv(const SwapParams& params) {
  // Fetch the raw locking tx
  std::vector<uint8_t> rawTx;
  if (!m_spvClient->getRawTx(params.ctrLockTxId, rawTx)) {
    return ChainClientResult::fail("LTC verifyLock SPV: getRawTx failed for " + params.ctrLockTxId);
  }

  // Parse the raw tx to find outputs paying to a P2WSH or P2SH address.
  // P2WSH scriptPubKey: OP_0 (0x00) PUSH32 (0x20) <32-byte-hash>  — 34 bytes
  // P2SH scriptPubKey:  OP_HASH160 (0xA9) PUSH20 (0x14) <20-byte-hash> OP_EQUAL (0x87) — 23 bytes
  const uint8_t* p = rawTx.data();
  const uint8_t* end = rawTx.data() + rawTx.size();

  // Skip version (4 bytes)
  if (p + 4 > end)
    return ChainClientResult::fail("LTC verifyLock SPV: raw tx too short");
  p += 4;

  // Detect SegWit (marker + flag)
  bool isSegWit = false;
  if (p + 2 <= end && *p == 0x00 && *(p + 1) == 0x01) {
    isSegWit = true;
    p += 2;
  }

  // Read vin count
  uint64_t vinCount = 0;
  if (!readVarInt(p, end, vinCount)) return ChainClientResult::fail("LTC verifyLock SPV: truncated tx");

  // Skip inputs
  for (uint64_t i = 0; i < vinCount; ++i) {
    if (p + 36 > end) return ChainClientResult::fail("LTC verifyLock SPV: truncated tx inputs");
    p += 36;

    uint64_t sigLen = 0;
    if (!readVarInt(p, end, sigLen)) return ChainClientResult::fail("LTC verifyLock SPV: truncated");
    if (p + sigLen > end) return ChainClientResult::fail("LTC verifyLock SPV: truncated");
    p += sigLen;

    if (p + 4 > end) return ChainClientResult::fail("LTC verifyLock SPV: truncated sequence");
    p += 4;
  }

  // Read vout count
  uint64_t voutCount = 0;
  if (!readVarInt(p, end, voutCount)) return ChainClientResult::fail("LTC verifyLock SPV: no vouts");

  bool foundOutput = false;
  for (uint64_t i = 0; i < voutCount; ++i) {
    if (p + 8 > end) return ChainClientResult::fail("LTC verifyLock SPV: truncated output value");
    uint64_t value = 0;
    for (int j = 0; j < 8; ++j) {
      value |= static_cast<uint64_t>(p[j]) << (j * 8);
    }
    p += 8;

    uint64_t spkLen = 0;
    if (!readVarInt(p, end, spkLen)) return ChainClientResult::fail("LTC verifyLock SPV: truncated scriptPubKey length");
    if (p + spkLen > end) return ChainClientResult::fail("LTC verifyLock SPV: truncated scriptPubKey");

    // Check for P2WSH output (34 bytes): OP_0 PUSH32 <32-byte-hash>
    // Require chainState redeem script so we bind to the negotiated HTLC.
    if (spkLen == 34 && p[0] == 0x00 && p[1] == 0x20 && value >= params.ctrAmount) {
      if (!params.chainState.empty()) {
        auto redeemScript = LtcHtlcScript::hexToBytes(params.chainState);
        auto expectedHash = LtcHtlcScript::sha256(redeemScript);
        if (expectedHash.size() == 32 && std::memcmp(p + 2, expectedHash.data(), 32) == 0) {
          foundOutput = true;
        }
      }
      // Fail closed without chainState — never accept any P2WSH of matching amount.
    }

    p += spkLen;
  }

  if (!foundOutput) {
    return ChainClientResult::fail("LTC verifyLock SPV: no matching P2WSH HTLC output (need chainState redeem script)");
  }

  // Verify inclusion via SPV
  SpvTxInclusion inclusion;
  if (!m_spvClient->verifyTxInclusion(params.ctrLockTxId, inclusion)) {
    return ChainClientResult::fail("LTC verifyLock SPV: verifyTxInclusion failed");
  }

  ChainClientResult result = ChainClientResult::ok(params.ctrLockTxId);
  result.confirmed = inclusion.included;
  result.spvVerified = inclusion.merkleVerified;
  result.blockHeight = inclusion.blockHeight;
  result.confirmations = inclusion.depth;
  return result;
}

ChainClientResult LtcChainClient::claim(const SwapParams& params) {
  if (!m_rpc) {
    if (m_wif.empty())
      return ChainClientResult::fail("LTC claim: no RPC or WIF available (SPV mode needs WIF for local signing)");

    std::array<uint8_t, 32> privKey{};
    if (!LtcHtlcScript::wifToPrivKey(m_wif, privKey))
      return ChainClientResult::fail("LTC claim: invalid WIF");

    auto witnessScript = LtcHtlcScript::hexToBytes(params.chainState);
    auto preimageBytes = LtcHtlcScript::hexToBytes(Common::podToHex(params.adaptorSecret));

    uint8_t addrVersion = 0;
    std::vector<uint8_t> pubKeyHash;
    if (!LtcHtlcScript::base58CheckDecode(params.ctrAddress, addrVersion, pubKeyHash))
      return ChainClientResult::fail("LTC claim: invalid destination address");
    auto outputScript = LtcHtlcScript::buildP2pkhScriptPubKey(pubKeyHash);

    uint64_t fee = 1000;
    if (params.ctrAmount <= fee)
      return ChainClientResult::fail("LTC claim: amount too small for fee");
    uint64_t outputAmount = params.ctrAmount - fee;

    const uint32_t nSequence = 0xFFFFFFFD;

    auto der = LtcHtlcScript::signInput(privKey, 2, 0, nSequence,
        params.ctrLockTxId, 0, witnessScript, params.ctrAmount,
        outputScript, outputAmount);
    if (der.empty())
      return ChainClientResult::fail("LTC claim SPV: signing failed");

    std::vector<uint8_t> emptyScriptSig;
    auto witnessStack = LtcHtlcScript::createClaimWitness(der, preimageBytes, witnessScript);

    auto rawTx = LtcHtlcScript::buildRawSegWitTx(
        params.ctrLockTxId, 0, params.ctrAmount,
        emptyScriptSig, witnessStack, params.ctrAddress, outputAmount, 0);

    std::string txid;
    if (!m_spvClient->broadcastTx(rawTx, txid))
      return ChainClientResult::fail("LTC claim SPV: broadcast failed");
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
  if (!ok) return ChainClientResult::fail("LTC claim failed");
  return ChainClientResult::ok(claimTxId);
}

ChainClientResult LtcChainClient::refund(const SwapParams& params) {
  if (!m_rpc) {
    if (m_wif.empty())
      return ChainClientResult::fail("LTC refund: no RPC or WIF available (SPV mode needs WIF for local signing)");

    std::array<uint8_t, 32> privKey{};
    if (!LtcHtlcScript::wifToPrivKey(m_wif, privKey))
      return ChainClientResult::fail("LTC refund: invalid WIF");

    auto witnessScript = LtcHtlcScript::hexToBytes(params.chainState);

    uint32_t nLocktime = static_cast<uint32_t>(params.ctrTimeoutBlock);

    uint8_t addrVersion = 0;
    std::vector<uint8_t> pubKeyHash;
    if (!LtcHtlcScript::base58CheckDecode(params.ctrAddress, addrVersion, pubKeyHash))
      return ChainClientResult::fail("LTC refund: invalid destination address");
    auto outputScript = LtcHtlcScript::buildP2pkhScriptPubKey(pubKeyHash);

    uint64_t fee = 1000;
    if (params.ctrAmount <= fee)
      return ChainClientResult::fail("LTC refund: amount too small for fee");
    uint64_t outputAmount = params.ctrAmount - fee;

    auto der = LtcHtlcScript::signInput(privKey, 2, nLocktime,
        0xFFFFFFFE,
        params.ctrLockTxId, 0, witnessScript, params.ctrAmount,
        outputScript, outputAmount);
    if (der.empty())
      return ChainClientResult::fail("LTC refund SPV: signing failed");

    std::vector<uint8_t> emptyScriptSig;
    auto witnessStack = LtcHtlcScript::createRefundWitness(der, witnessScript);

    auto rawTx = LtcHtlcScript::buildRawSegWitTx(
        params.ctrLockTxId, 0, params.ctrAmount,
        emptyScriptSig, witnessStack, params.ctrAddress, outputAmount, nLocktime);

    std::string txid;
    if (!m_spvClient->broadcastTx(rawTx, txid))
      return ChainClientResult::fail("LTC refund SPV: broadcast failed");
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
  if (!ok) return ChainClientResult::fail("LTC refundHtlc failed");
  return ChainClientResult::ok(refundTxId);
}

ChainClientResult LtcChainClient::verifyReserveProof(const std::string& expectedMessage,
                                                     uint64_t minAmount,
                                                     const std::string& proof) {
  if (!m_rpc)
    return ChainClientResult::fail("LTC verifyReserveProof: RPC client not available");

  size_t c1 = proof.find(':');
  size_t c2 = proof.find(':', c1 + 1);
  if (c1 == std::string::npos || c2 == std::string::npos)
    return ChainClientResult::fail("LTC reserve proof: invalid format (expected address:signature:message)");

  std::string address   = proof.substr(0, c1);
  std::string signature = proof.substr(c1 + 1, c2 - c1 - 1);
  std::string message   = proof.substr(c2 + 1);

  bool sigValid = false;
  if (!m_rpc->verifyMessage(address, signature, message, sigValid))
    return ChainClientResult::fail("LTC reserve proof: verifymessage RPC failed");
  if (!sigValid)
    return ChainClientResult::fail("LTC reserve proof: invalid signature");

  if (!expectedMessage.empty() && message != expectedMessage)
    return ChainClientResult::fail("LTC reserve proof: message not bound to this offer");

  uint64_t balance = 0;
  if (!m_rpc->getBalance(address, balance))
    return ChainClientResult::fail("LTC reserve proof: balance check RPC failed");
  if (balance < minAmount)
    return ChainClientResult::fail("LTC reserve proof: insufficient balance (" +
                                   std::to_string(balance) + " < " + std::to_string(minAmount) + ")");

  return ChainClientResult::ok(address);
}

bool LtcChainClient::getCurrentHeight(uint64_t& height) {
  if (m_spvClient) {
    return m_spvClient->getTipHeight(height);
  }
  if (m_rpc) {
    return m_rpc->getBlockCount(height);
  }
  return false;
}

ChainClientResult LtcChainClient::getTransactionDetails(const std::string& txId,
                                                        ChainClientResult& result) {
  if (m_spvClient) {
    uint64_t tipHeight = 0;
    if (!m_spvClient->getTipHeight(tipHeight)) {
      result = ChainClientResult::fail("LTC SPV: cannot get tip height");
      return result;
    }

    SpvTxInclusion inclusion;
    if (!m_spvClient->verifyTxInclusion(txId, inclusion)) {
      result = ChainClientResult::fail("LTC SPV: tx not found or not yet included in a block");
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
    LtcTxInfo txInfo;
    if (!m_rpc->getTransaction(txId, txInfo)) {
      result = ChainClientResult::fail("LTC RPC: gettransaction failed for " + txId);
      return result;
    }

    uint64_t tipHeight = 0;
    m_rpc->getBlockCount(tipHeight);

    result.success = true;
    result.confirmed = txInfo.confirmations > 0;
    result.spvVerified = false;
    result.blockHeight = txInfo.blockHeight;
    result.confirmations = txInfo.confirmations;
    return result;
  }

  result = ChainClientResult::fail("LTC: no RPC or SPV client available");
  return result;
}

std::string LtcChainClient::extractSecret(const std::string& spendingTxid,
                                           const std::string& htlcRedeemScriptHex) {
  std::vector<uint8_t> redeemScript = LtcHtlcScript::hexToBytes(htlcRedeemScriptHex);
  std::vector<uint8_t> p2wshScriptPubKey = LtcHtlcScript::redeemScriptToP2wshScriptPubKey(redeemScript);

  if (m_spvClient) {
    return extractSecretSpv(spendingTxid, p2wshScriptPubKey);
  }

  if (!m_rpc) {
    return {};
  }

  std::string rawTxHex;
  if (!m_rpc->getRawTransaction(spendingTxid, rawTxHex)) {
    return {};
  }

  std::vector<uint8_t> rawTx = LtcHtlcScript::hexToBytes(rawTxHex);
  std::vector<uint8_t> preimage = LtcHtlcScript::parseClaimPreimage(rawTx, p2wshScriptPubKey);
  if (preimage.empty()) {
    return {};
  }

  return LtcHtlcScript::bytesToHex(preimage);
}

std::string LtcChainClient::extractSecretSpv(const std::string& spendingTxid,
                                              const std::vector<uint8_t>& htlcP2wshScriptPubKey) {
  std::vector<uint8_t> rawSpendingTx;
  if (!m_spvClient->getRawTx(spendingTxid, rawSpendingTx)) {
    return {};
  }

  std::vector<uint8_t> preimage = LtcHtlcScript::parseClaimPreimage(rawSpendingTx, htlcP2wshScriptPubKey);
  if (preimage.empty()) {
    return {};
  }

  return LtcHtlcScript::bytesToHex(preimage);
}

std::string LtcChainClient::tryExtractClaimedSecret(const SwapParams& params) {
  if (params.chainState.empty() || params.ctrLockTxId.empty()) return {};

  std::string redeemHex = params.chainState;
  std::string knownClaimTxid;
  auto colon = params.chainState.find(':');
  if (colon != std::string::npos && colon + 1 < params.chainState.size()) {
    std::string left = params.chainState.substr(0, colon);
    std::string right = params.chainState.substr(colon + 1);
    if (!left.empty() && (left.size() % 2) == 0 && right.size() == 64) {
      redeemHex = left;
      knownClaimTxid = right;
    }
  }

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

  if (m_rpc && !knownClaimTxid.empty()) {
    std::string secret = extractSecret(knownClaimTxid, redeemHex);
    if (!secret.empty()) return secret;
  }

  return {};
}

} // namespace XfgSwap
