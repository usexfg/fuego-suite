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
#include "../utxo_claim_proof.h"
#include "LtcHtlcScript.h"
#include "Bitcoin/BtcHtlcScript.h"
#include "Bitcoin/BtcPtlcScript.h"
#include "Bitcoin/BtcTaprootPtlc.h"
#include "Common/StringTools.h"
#include "../SwapHashLock.h"
#include "../SwapPtlcLock.h"
#include "../Crypto/Secp256k1Signer.h"

#include <array>
#include <cstring>
#include <algorithm>
#include <stdexcept>
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
  if (m_fullNode) return m_fullNode->receiveAddress();
  return m_spvHtlc ? m_spvHtlc->receiveAddress() : std::string();
}

static bool isZeroSecret(const Crypto::SecretKey& s) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&s);
  for (size_t i = 0; i < sizeof(Crypto::SecretKey); ++i) if (p[i]) return false;
  return true;
}

LtcChainClient::LtcChainClient(std::unique_ptr<LtcRpcClient> rpc, const std::string& wif,
                               UtxoNetwork network)
  : m_rpc(std::move(rpc)), m_wif(wif) {
  if (!m_rpc) return;
  LtcRpcClient* node = m_rpc.get();
  m_fullNode.reset(new UtxoHtlcFullNode(
      {"LTC", SwapPair::LTC, UtxoHtlcEncoding::P2WSH, litecoinNetworkParams(network)},
      [node](const std::string& method, const std::string& paramsJson,
             Common::JsonValue& result, std::string& error) {
        return node->callJson(method, paramsJson, result, error);
      },
      wif));
}

LtcChainClient::LtcChainClient(std::shared_ptr<ISpvClient> spvClient, const std::string& wif,
                               UtxoNetwork network)
  : m_wif(wif), m_spvClient(std::move(spvClient)) {
  if (m_spvClient)
    m_spvHtlc.reset(new UtxoHtlcSpv(
        {"LTC", SwapPair::LTC, UtxoHtlcEncoding::P2WSH, litecoinNetworkParams(network)}, m_spvClient, wif));
}

ChainClientResult LtcChainClient::lock(const SwapParams& params) {
  if (m_fullNode) return m_fullNode->lock(params);
  if (m_spvHtlc) return m_spvHtlc->lock(params);
  return ChainClientResult::fail("LTC lock: no RPC or SPV client available");
}

ChainClientResult LtcChainClient::verifyLock(const SwapParams& params) {
  if (m_spvHtlc) return m_spvHtlc->verifyLock(params);
  if (m_fullNode) return m_fullNode->verifyLock(params);
  return ChainClientResult::fail("LTC verifyLock: no RPC or SPV client available");
}

ChainClientResult LtcChainClient::claim(const SwapParams& params) {
  if (m_fullNode) return m_fullNode->claim(params);
  if (m_spvHtlc) return m_spvHtlc->claim(params);
  return ChainClientResult::notSent("LTC claim: no RPC or SPV client available");
}

ChainClientResult LtcChainClient::refund(const SwapParams& params) {
  if (m_fullNode) return m_fullNode->refund(params);
  if (m_spvHtlc) return m_spvHtlc->refund(params);
  return ChainClientResult::fail("LTC refund: no RPC or SPV client available");
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
  if (m_spvHtlc) return m_spvHtlc->currentHeight(height);
  if (m_rpc) return m_rpc->getBlockCount(height);
  return false;
}

ChainClientResult LtcChainClient::getTransactionDetails(const std::string& txId,
                                                        ChainClientResult& result) {
  if (m_spvHtlc) result = m_spvHtlc->transactionDetails(txId);
  else if (m_fullNode) result = m_fullNode->transactionDetails(txId);
  else result = ChainClientResult::fail("LTC: no RPC or SPV client available");
  return result;
}

std::string LtcChainClient::tryExtractClaimedSecret(const SwapParams& params) {
  if (m_fullNode) return m_fullNode->tryExtractClaimedSecret(params);
  return m_spvHtlc ? m_spvHtlc->tryExtractClaimedSecret(params) : std::string();
}

bool LtcChainClient::hasConfirmedClaim(const SwapParams& params) {
  if (m_fullNode) return m_fullNode->hasConfirmedClaim(params);
  return m_spvHtlc && m_spvHtlc->hasConfirmedClaim(params);
}

bool LtcChainClient::isReadyForNewSwap() {
  return readinessError().empty();
}

std::string LtcChainClient::readinessError() {
  return readinessErrorFor(SwapRole::ALICE);
}

std::string LtcChainClient::claimDeadlineError(const SwapParams& params) {
  if (m_fullNode) return m_fullNode->claimDeadlineError(params);
  return m_spvHtlc ? m_spvHtlc->claimDeadlineError(params) : std::string();
}

std::string LtcChainClient::readinessErrorFor(SwapRole role) {
  if (m_fullNode) return m_fullNode->readinessError();
  if (m_spvHtlc) return m_spvHtlc->readinessErrorFor(role);
  return "LTC: no RPC or SPV client available";
}

std::string LtcChainClient::getClaimPubKey() const {
  if (m_fullNode) return m_fullNode->claimPubKeyHex();
  return m_spvHtlc ? m_spvHtlc->claimPubKeyHex() : std::string();
}

bool LtcChainClient::supportsPurePtlc() const {
  if (m_fullNode) return m_fullNode->hasSigningKey() && m_fullNode->supportsPointLocks();
  return m_spvHtlc && m_spvHtlc->supportsPointLocks();
}

ChainClientResult LtcChainClient::bumpClaim(const SwapParams& params) {
  if (m_fullNode) return m_fullNode->bumpClaim(params);
  if (m_spvHtlc) return m_spvHtlc->bumpClaim(params);
  return ChainClientResult::notSent(chainName() + ": no RPC or SPV client available");
}

} // namespace XfgSwap
