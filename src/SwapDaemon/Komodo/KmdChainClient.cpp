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

#include "KmdChainClient.h"
#include "KmdHtlcScript.h"

#include <cstring>
#include <algorithm>

namespace XfgSwap {

KmdChainClient::KmdChainClient(std::shared_ptr<ISpvClient> spvClient)
  : m_spvClient(std::move(spvClient)) {}

ChainClientResult KmdChainClient::lock(const SwapParams& params) {
  (void)params;
  return ChainClientResult::fail("KMD lock: SPV mode does not support lock — use a KMD full node");
}

ChainClientResult KmdChainClient::verifyLock(const SwapParams& params) {
  return verifyLockSpv(params);
}

ChainClientResult KmdChainClient::verifyLockSpv(const SwapParams& params) {
  std::vector<uint8_t> rawTx;
  if (!m_spvClient->getRawTx(params.ctrLockTxId, rawTx)) {
    return ChainClientResult::fail("KMD verifyLock SPV: getRawTx failed for " + params.ctrLockTxId);
  }

  const uint8_t* p = rawTx.data();
  const uint8_t* end = rawTx.data() + rawTx.size();

  // Skip version (4 bytes)
  if (p + 4 > end)
    return ChainClientResult::fail("KMD verifyLock SPV: raw tx too short");
  p += 4;

  // Inline varint reader
  auto readVarIntLocal = [&](uint64_t& out) -> bool {
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
  };

  // Read vin count
  uint64_t vinCount = 0;
  if (!readVarIntLocal(vinCount))
    return ChainClientResult::fail("KMD verifyLock SPV: truncated tx");

  // Skip inputs
  for (uint64_t i = 0; i < vinCount; ++i) {
    if (p + 36 > end) return ChainClientResult::fail("KMD verifyLock SPV: truncated tx inputs");
    p += 36;  // txid + vout
    uint64_t sigLen = 0;
    if (*p < 0xFD) {
      sigLen = *p++;
    } else if (*p == 0xFD) {
      ++p;
      if (p + 2 > end) return ChainClientResult::fail("KMD verifyLock SPV: truncated");
      sigLen = static_cast<uint64_t>(p[0]) | (static_cast<uint64_t>(p[1]) << 8);
      p += 2;
    } else {
      return ChainClientResult::fail("KMD verifyLock SPV: oversized scriptSig");
    }
    p += sigLen;
    if (p + 4 > end) return ChainClientResult::fail("KMD verifyLock SPV: truncated sequence");
    p += 4;
  }

  // Read vout count
  uint64_t voutCount = 0;
  if (!readVarIntLocal(voutCount))
    return ChainClientResult::fail("KMD verifyLock SPV: no vouts");

  bool foundP2sh = false;
  for (uint64_t i = 0; i < voutCount; ++i) {
    if (p + 8 > end) return ChainClientResult::fail("KMD verifyLock SPV: truncated output value");
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
      if (p + 2 > end) return ChainClientResult::fail("KMD verifyLock SPV: truncated");
      spkLen = static_cast<uint64_t>(p[0]) | (static_cast<uint64_t>(p[1]) << 8);
      p += 2;
    } else {
      return ChainClientResult::fail("KMD verifyLock SPV: oversized scriptPubKey");
    }
    if (p + spkLen > end) return ChainClientResult::fail("KMD verifyLock SPV: truncated scriptPubKey");

    // Check if this is a P2SH output: OP_HASH160 <20 bytes> OP_EQUAL (23 bytes)
    if (spkLen == 23 && p[0] == 0xA9 && p[1] == 0x14 && p[22] == 0x87) {
      if (value >= params.ctrAmount) {
        foundP2sh = true;
      }
    }

    p += spkLen;
  }

  if (!foundP2sh) {
    return ChainClientResult::fail("KMD verifyLock SPV: no P2SH output with expected amount " +
                                   std::to_string(params.ctrAmount));
  }

  // Verify inclusion via SPV
  SpvTxInclusion inclusion;
  if (!m_spvClient->verifyTxInclusion(params.ctrLockTxId, inclusion)) {
    return ChainClientResult::fail("KMD verifyLock SPV: verifyTxInclusion failed");
  }

  ChainClientResult result = ChainClientResult::ok(params.ctrLockTxId);
  result.confirmed = inclusion.included;
  result.spvVerified = inclusion.merkleVerified;
  result.blockHeight = inclusion.blockHeight;
  result.confirmations = inclusion.depth;
  return result;
}

ChainClientResult KmdChainClient::claim(const SwapParams& params) {
  (void)params;
  return ChainClientResult::fail("KMD claim: SPV mode does not support claim — use a KMD full node");
}

ChainClientResult KmdChainClient::refund(const SwapParams& params) {
  (void)params;
  return ChainClientResult::fail("KMD refund: SPV mode does not support refund — use a KMD full node");
}

ChainClientResult KmdChainClient::verifyReserveProof(const std::string& expectedMessage,
    uint64_t minAmount, const std::string& proof) {
  (void)expectedMessage;
  (void)minAmount;
  (void)proof;
  return ChainClientResult::fail("KMD verifyReserveProof: not implemented");
}

bool KmdChainClient::getCurrentHeight(uint64_t& height) {
  if (m_spvClient) {
    return m_spvClient->getTipHeight(height);
  }
  return false;
}

std::string KmdChainClient::extractSecret(const std::string& spendingTxid,
                                           const std::string& htlcRedeemScriptHex) {
  std::vector<uint8_t> redeemScript = KmdHtlcScript::hexToBytes(htlcRedeemScriptHex);
  std::vector<uint8_t> p2shScriptPubKey = KmdHtlcScript::redeemScriptToP2shScriptPubKey(redeemScript);

  if (m_spvClient) {
    return extractSecretSpv(spendingTxid, p2shScriptPubKey);
  }

  return {};
}

std::string KmdChainClient::extractSecretSpv(const std::string& spendingTxid,
                                              const std::vector<uint8_t>& htlcP2shScriptPubKey) {
  std::vector<uint8_t> rawSpendingTx;
  if (!m_spvClient->getRawTx(spendingTxid, rawSpendingTx)) {
    return {};
  }

  std::vector<uint8_t> preimage = KmdHtlcScript::parseClaimPreimage(rawSpendingTx, htlcP2shScriptPubKey);
  if (preimage.empty()) {
    return {};
  }

  return KmdHtlcScript::bytesToHex(preimage);
}

} // namespace XfgSwap
