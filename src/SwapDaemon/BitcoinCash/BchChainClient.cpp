#include "BchChainClient.h"
#include "HtlcScript.h"
#include "Common/StringTools.h"
#include "../SwapHashLock.h"

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

BchChainClient::BchChainClient(std::shared_ptr<ISpvClient> spvClient)
  : m_spvClient(std::move(spvClient)) {}

ChainClientResult BchChainClient::lock(const SwapParams& params) {
  if (isZeroSecret(params.adaptorSecret))
    return ChainClientResult::fail("BCH lock: adaptor secret not set — cannot derive hashlock");

  if (!m_rpc)
    return ChainClientResult::fail("BCH lock: RPC client not available (SPV mode does not support lock)");

  std::string lockTxId;
  std::string redeemScriptHex;
  bool ok = m_rpc->lockHtlc(
      m_wif,
      params.ctrAddress,
      bchHashLockHex(params.adaptorSecret),
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

  bool ok = m_rpc->verifyLock(params.ctrLockTxId, params.ctrAmount);
  if (!ok) return ChainClientResult::fail("BCH lock not verified");
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
  if (!m_rpc)
    return ChainClientResult::fail("BCH claim: RPC client not available (SPV mode does not support claim)");

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
  if (!m_rpc)
    return ChainClientResult::fail("BCH refund: RPC client not available (SPV mode does not support refund)");

  std::string refundTxId;
  bool ok = m_rpc->refundHtlc(
      m_wif,
      params.ctrLockTxId, 0, params.ctrAmount,
      params.chainState,
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
    // TODO: implement via BchRpcClient when available
    result = ChainClientResult::fail("BCH RPC: getTransactionDetails not yet implemented");
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

  // Full-node mode not implemented for extractSecret (needs raw tx decode)
  // Fall back to SPV if available, otherwise fail
  return {};
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

} // namespace XfgSwap
