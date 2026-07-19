#include "BtcChainClient.h"
#include "BtcHtlcScript.h"

#include <cstring>
#include <algorithm>

namespace XfgSwap {

BtcChainClient::BtcChainClient(std::shared_ptr<ISpvClient> spvClient)
  : m_spvClient(std::move(spvClient)) {}

ChainClientResult BtcChainClient::lock(const SwapParams& params) {
  (void)params;
  return ChainClientResult::fail("BTC lock: RPC client not available (SPV mode)");
}

ChainClientResult BtcChainClient::verifyLock(const SwapParams& params) {
  if (m_spvClient) {
    return verifyLockSpv(params);
  }
  return ChainClientResult::fail("BTC verifyLock: no SPV client available");
}

ChainClientResult BtcChainClient::claim(const SwapParams& params) {
  (void)params;
  return ChainClientResult::fail("BTC claim: RPC client not available (SPV mode)");
}

ChainClientResult BtcChainClient::refund(const SwapParams& params) {
  (void)params;
  return ChainClientResult::fail("BTC refund: RPC client not available (SPV mode)");
}

ChainClientResult BtcChainClient::verifyReserveProof(const std::string& expectedMessage,
                                                     uint64_t minAmount,
                                                     const std::string& proof) {
  (void)expectedMessage;
  (void)minAmount;
  (void)proof;
  return ChainClientResult::fail("BTC verifyReserveProof: not implemented");
}

bool BtcChainClient::getCurrentHeight(uint64_t& height) {
  if (m_spvClient) {
    return m_spvClient->getTipHeight(height);
  }
  return false;
}

// =============================================================================
// SPV-mode verifyLock
// =============================================================================

ChainClientResult BtcChainClient::verifyLockSpv(const SwapParams& params) {
  // Fetch the raw locking tx
  std::vector<uint8_t> rawTx;
  if (!m_spvClient->getRawTx(params.ctrLockTxId, rawTx)) {
    return ChainClientResult::fail("BTC verifyLock SPV: getRawTx failed for " + params.ctrLockTxId);
  }

  // Parse the raw tx to find P2WSH outputs.
  // P2WSH scriptPubKey: OP_0 (0x00) PUSH32 (0x20) <32-byte-hash> = 34 bytes
  const uint8_t* p = rawTx.data();
  const uint8_t* end = rawTx.data() + rawTx.size();

  // Skip version (4 bytes)
  if (p + 4 > end)
    return ChainClientResult::fail("BTC verifyLock SPV: raw tx too short");
  p += 4;

  // Skip SegWit marker + flag if present
  if (p + 2 <= end && p[0] == 0x00 && p[1] == 0x01) {
    p += 2;
  }

  // Read vin count (inline varint reader)
  uint64_t vinCount = 0;
  if (p >= end) return ChainClientResult::fail("BTC verifyLock SPV: truncated tx");
  uint8_t first = *p++;
  if (first < 0xFD) {
    vinCount = first;
  } else if (first == 0xFD) {
    if (p + 2 > end) return ChainClientResult::fail("BTC verifyLock SPV: truncated tx");
    vinCount = static_cast<uint64_t>(p[0]) | (static_cast<uint64_t>(p[1]) << 8);
    p += 2;
  } else if (first == 0xFE) {
    if (p + 4 > end) return ChainClientResult::fail("BTC verifyLock SPV: truncated tx");
    vinCount = static_cast<uint64_t>(p[0]) | (static_cast<uint64_t>(p[1]) << 8) |
              (static_cast<uint64_t>(p[2]) << 16) | (static_cast<uint64_t>(p[3]) << 24);
    p += 4;
  } else {
    if (p + 8 > end) return ChainClientResult::fail("BTC verifyLock SPV: truncated tx");
    vinCount = 0;
    for (int i = 0; i < 8; ++i) {
      vinCount |= static_cast<uint64_t>(p[i]) << (i * 8);
    }
    p += 8;
  }

  // Skip inputs
  for (uint64_t i = 0; i < vinCount; ++i) {
    if (p + 36 > end) return ChainClientResult::fail("BTC verifyLock SPV: truncated tx inputs");
    p += 36;  // txid + vout
    uint64_t sigLen = 0;
    if (*p < 0xFD) {
      sigLen = *p++;
    } else if (*p == 0xFD) {
      ++p;
      if (p + 2 > end) return ChainClientResult::fail("BTC verifyLock SPV: truncated");
      sigLen = static_cast<uint64_t>(p[0]) | (static_cast<uint64_t>(p[1]) << 8);
      p += 2;
    } else {
      return ChainClientResult::fail("BTC verifyLock SPV: oversized scriptSig");
    }
    p += sigLen;
    if (p + 4 > end) return ChainClientResult::fail("BTC verifyLock SPV: truncated sequence");
    p += 4;  // sequence
  }

  // Read vout count
  uint64_t voutCount = 0;
  if (p >= end) return ChainClientResult::fail("BTC verifyLock SPV: no vouts");
  first = *p++;
  if (first < 0xFD) {
    voutCount = first;
  } else if (first == 0xFD) {
    if (p + 2 > end) return ChainClientResult::fail("BTC verifyLock SPV: truncated vout count");
    voutCount = static_cast<uint64_t>(p[0]) | (static_cast<uint64_t>(p[1]) << 8);
    p += 2;
  } else if (first == 0xFE) {
    if (p + 4 > end) return ChainClientResult::fail("BTC verifyLock SPV: truncated vout count");
    voutCount = static_cast<uint64_t>(p[0]) | (static_cast<uint64_t>(p[1]) << 8) |
               (static_cast<uint64_t>(p[2]) << 16) | (static_cast<uint64_t>(p[3]) << 24);
    p += 4;
  } else {
    if (p + 8 > end) return ChainClientResult::fail("BTC verifyLock SPV: truncated vout count");
    voutCount = 0;
    for (int i = 0; i < 8; ++i) {
      voutCount |= static_cast<uint64_t>(p[i]) << (i * 8);
    }
    p += 8;
  }

  bool foundP2wsh = false;
  for (uint64_t i = 0; i < voutCount; ++i) {
    if (p + 8 > end) return ChainClientResult::fail("BTC verifyLock SPV: truncated output value");
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
      if (p + 2 > end) return ChainClientResult::fail("BTC verifyLock SPV: truncated");
      spkLen = static_cast<uint64_t>(p[0]) | (static_cast<uint64_t>(p[1]) << 8);
      p += 2;
    } else {
      return ChainClientResult::fail("BTC verifyLock SPV: oversized scriptPubKey");
    }
    if (p + spkLen > end) return ChainClientResult::fail("BTC verifyLock SPV: truncated scriptPubKey");

    // Check if this is a P2WSH output: OP_0 (0x00) PUSH32 (0x20) <32-byte-hash>
    // Total: 34 bytes
    if (spkLen == 34 && p[0] == 0x00 && p[1] == 0x20) {
      if (value >= params.ctrAmount) {
        foundP2wsh = true;
      }
    }

    p += spkLen;
  }

  if (!foundP2wsh) {
    return ChainClientResult::fail("BTC verifyLock SPV: no P2WSH output with expected amount " +
                                   std::to_string(params.ctrAmount));
  }

  // Verify inclusion via SPV
  SpvTxInclusion inclusion;
  if (!m_spvClient->verifyTxInclusion(params.ctrLockTxId, inclusion)) {
    return ChainClientResult::fail("BTC verifyLock SPV: verifyTxInclusion failed");
  }

  ChainClientResult result = ChainClientResult::ok(params.ctrLockTxId);
  result.confirmed = inclusion.included;
  result.spvVerified = inclusion.merkleVerified;
  result.blockHeight = inclusion.blockHeight;
  result.confirmations = inclusion.depth;
  return result;
}

// =============================================================================
// SPV-mode extractSecret
// =============================================================================

std::string BtcChainClient::extractSecret(const std::string& spendingTxid,
                                           const std::string& htlcRedeemScriptHex) {
  std::vector<uint8_t> redeemScript = BtcHtlcScript::hexToBytes(htlcRedeemScriptHex);
  std::vector<uint8_t> p2wshScriptPubKey = BtcHtlcScript::redeemScriptToP2wshScriptPubKey(redeemScript);

  if (m_spvClient) {
    return extractSecretSpv(spendingTxid, p2wshScriptPubKey);
  }

  return {};
}

std::string BtcChainClient::extractSecretSpv(const std::string& spendingTxid,
                                              const std::vector<uint8_t>& htlcP2wshScriptPubKey) {
  std::vector<uint8_t> rawSpendingTx;
  if (!m_spvClient->getRawTx(spendingTxid, rawSpendingTx)) {
    return {};
  }

  std::vector<uint8_t> preimage = BtcHtlcScript::parseClaimPreimage(rawSpendingTx, htlcP2wshScriptPubKey);
  if (preimage.empty()) {
    return {};
  }

  return BtcHtlcScript::bytesToHex(preimage);
}

} // namespace XfgSwap
