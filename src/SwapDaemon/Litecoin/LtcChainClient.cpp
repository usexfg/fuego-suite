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

#include <cstring>
#include <algorithm>

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

LtcChainClient::LtcChainClient(std::shared_ptr<ISpvClient> spvClient)
  : m_spvClient(std::move(spvClient)) {}

ChainClientResult LtcChainClient::lock(const SwapParams& params) {
  (void)params;
  return ChainClientResult::fail("LTC lock: not implemented in SPV mode (use full-node RPC)");
}

ChainClientResult LtcChainClient::verifyLock(const SwapParams& params) {
  if (m_spvClient) {
    return verifyLockSpv(params);
  }
  return ChainClientResult::fail("LTC verifyLock: no SPV client available");
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
    if (spkLen == 34 && p[0] == 0x00 && p[1] == 0x20) {
      if (value >= params.ctrAmount) {
        foundOutput = true;
      }
    }

    p += spkLen;
  }

  if (!foundOutput) {
    return ChainClientResult::fail("LTC verifyLock SPV: no P2WSH output with expected amount " +
                                   std::to_string(params.ctrAmount));
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
  (void)params;
  return ChainClientResult::fail("LTC claim: not implemented in SPV mode");
}

ChainClientResult LtcChainClient::refund(const SwapParams& params) {
  (void)params;
  return ChainClientResult::fail("LTC refund: not implemented in SPV mode");
}

ChainClientResult LtcChainClient::verifyReserveProof(const std::string& expectedMessage,
                                                     uint64_t minAmount,
                                                     const std::string& proof) {
  (void)expectedMessage; (void)minAmount; (void)proof;
  return ChainClientResult::fail("LTC verifyReserveProof: not implemented (needs RPC client)");
}

bool LtcChainClient::getCurrentHeight(uint64_t& height) {
  if (m_spvClient) {
    return m_spvClient->getTipHeight(height);
  }
  return false;
}

std::string LtcChainClient::extractSecret(const std::string& spendingTxid,
                                           const std::string& htlcRedeemScriptHex) {
  std::vector<uint8_t> redeemScript = LtcHtlcScript::hexToBytes(htlcRedeemScriptHex);
  std::vector<uint8_t> p2wshScriptPubKey = LtcHtlcScript::redeemScriptToP2wshScriptPubKey(redeemScript);

  if (m_spvClient) {
    return extractSecretSpv(spendingTxid, p2wshScriptPubKey);
  }

  return {};
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

} // namespace XfgSwap
