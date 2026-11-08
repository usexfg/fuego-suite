#include "BtcChainClient.h"
#include "BtcHtlcScript.h"
#include "BtcPtlcScript.h"
#include "BtcTaprootPtlc.h"
#include "Common/StringTools.h"
#include "../SwapHashLock.h"
#include "../SwapPtlcLock.h"
#include "../utxo_claim_proof.h"
#include "../Crypto/Secp256k1Signer.h"

#include <array>
#include <cstring>
#include <algorithm>
#include <stdexcept>
#include "Komodo/KmdHtlcScript.h"

namespace XfgSwap {

std::string BtcChainClient::getReceiveAddress() const {
  if (m_fullNode) return m_fullNode->receiveAddress();
  return m_spvHtlc ? m_spvHtlc->receiveAddress() : std::string();
}

BtcChainClient::BtcChainClient(std::unique_ptr<BtcRpcClient> rpc, const std::string& wif,
                               UtxoNetwork network)
  : m_rpc(std::move(rpc)), m_wif(wif) {
  if (!m_rpc) return;
  BtcRpcClient* node = m_rpc.get();
  m_fullNode.reset(new UtxoHtlcFullNode(
      {"BTC", SwapPair::BTC, UtxoHtlcEncoding::P2WSH, bitcoinNetworkParams(network)},
      [node](const std::string& method, const std::string& paramsJson,
             Common::JsonValue& result, std::string& error) {
        return node->callJson(method, paramsJson, result, error);
      },
      wif));
}

BtcChainClient::BtcChainClient(std::shared_ptr<ISpvClient> spvClient, const std::string& wif,
                               UtxoNetwork network)
  : m_wif(wif), m_spvClient(std::move(spvClient)) {
  if (m_spvClient)
    m_spvHtlc.reset(new UtxoHtlcSpv(
        {"BTC", SwapPair::BTC, UtxoHtlcEncoding::P2WSH, bitcoinNetworkParams(network)}, m_spvClient, wif));
}

ChainClientResult BtcChainClient::lock(const SwapParams& params) {
  if (m_fullNode) return m_fullNode->lock(params);
  if (m_spvHtlc) return m_spvHtlc->lock(params);
  return ChainClientResult::fail("BTC lock: no RPC or SPV client available");
}

ChainClientResult BtcChainClient::verifyLock(const SwapParams& params) {
  if (m_spvHtlc) return m_spvHtlc->verifyLock(params);
  if (m_fullNode) return m_fullNode->verifyLock(params);
  return ChainClientResult::fail("BTC verifyLock: no RPC or SPV client available");
}

ChainClientResult BtcChainClient::claim(const SwapParams& params) {
  if (m_fullNode) return m_fullNode->claim(params);
  if (m_spvHtlc) return m_spvHtlc->claim(params);
  return ChainClientResult::notSent("BTC claim: no RPC or SPV client available");
}

ChainClientResult BtcChainClient::refund(const SwapParams& params) {
  if (m_fullNode) return m_fullNode->refund(params);
  if (m_spvHtlc) return m_spvHtlc->refund(params);
  return ChainClientResult::fail("BTC refund: no RPC or SPV client available");
}

ChainClientResult BtcChainClient::verifyReserveProof(const std::string& expectedMessage,
                                                     uint64_t minAmount,
                                                     const std::string& proof) {
  if (!m_rpc)
    return ChainClientResult::fail("BTC verifyReserveProof: RPC client not available");

  size_t c1 = proof.find(':');
  size_t c2 = proof.find(':', c1 + 1);
  if (c1 == std::string::npos || c2 == std::string::npos)
    return ChainClientResult::fail("BTC reserve proof: invalid format (expected address:signature:message)");

  std::string address   = proof.substr(0, c1);
  std::string signature = proof.substr(c1 + 1, c2 - c1 - 1);
  std::string message   = proof.substr(c2 + 1);

  bool sigValid = false;
  if (!m_rpc->verifyMessage(address, signature, message, sigValid))
    return ChainClientResult::fail("BTC reserve proof: verifymessage RPC failed");
  if (!sigValid)
    return ChainClientResult::fail("BTC reserve proof: invalid signature");

  if (!expectedMessage.empty() && message != expectedMessage)
    return ChainClientResult::fail("BTC reserve proof: message not bound to this offer");

  uint64_t balance = 0;
  if (!m_rpc->getBalance(address, balance))
    return ChainClientResult::fail("BTC reserve proof: balance check RPC failed");
  if (balance < minAmount)
    return ChainClientResult::fail("BTC reserve proof: insufficient balance (" +
                                   std::to_string(balance) + " < " + std::to_string(minAmount) + ")");

  return ChainClientResult::ok(address);
}

bool BtcChainClient::getCurrentHeight(uint64_t& height) {
  if (m_spvHtlc) return m_spvHtlc->currentHeight(height);
  if (m_rpc) return m_rpc->getBlockCount(height);
  return false;
}

ChainClientResult BtcChainClient::getTransactionDetails(const std::string& txId,
                                                        ChainClientResult& result) {
  if (m_spvHtlc) result = m_spvHtlc->transactionDetails(txId);
  else if (m_fullNode) result = m_fullNode->transactionDetails(txId);
  else result = ChainClientResult::fail("BTC: no RPC or SPV client available");
  return result;
}

std::string BtcChainClient::tryExtractClaimedSecret(const SwapParams& params) {
  if (m_fullNode) return m_fullNode->tryExtractClaimedSecret(params);
  return m_spvHtlc ? m_spvHtlc->tryExtractClaimedSecret(params) : std::string();
}

bool BtcChainClient::hasConfirmedClaim(const SwapParams& params) {
  if (m_fullNode) return m_fullNode->hasConfirmedClaim(params);
  return m_spvHtlc && m_spvHtlc->hasConfirmedClaim(params);
}

bool BtcChainClient::isReadyForNewSwap() {
  return readinessError().empty();
}

std::string BtcChainClient::readinessError() {
  return readinessErrorFor(SwapRole::ALICE);
}

std::string BtcChainClient::claimDeadlineError(const SwapParams& params) {
  if (m_fullNode) return m_fullNode->claimDeadlineError(params);
  return m_spvHtlc ? m_spvHtlc->claimDeadlineError(params) : std::string();
}

std::string BtcChainClient::readinessErrorFor(SwapRole role) {
  if (m_fullNode) return m_fullNode->readinessError();
  if (m_spvHtlc) return m_spvHtlc->readinessErrorFor(role);
  return "BTC: no RPC or SPV client available";
}

std::string BtcChainClient::getClaimPubKey() const {
  if (m_fullNode) return m_fullNode->claimPubKeyHex();
  return m_spvHtlc ? m_spvHtlc->claimPubKeyHex() : std::string();
}

bool BtcChainClient::supportsPurePtlc() const {
  if (m_fullNode) return m_fullNode->hasSigningKey() && m_fullNode->supportsPointLocks();
  return m_spvHtlc && m_spvHtlc->supportsPointLocks();
}

ChainClientResult BtcChainClient::bumpClaim(const SwapParams& params) {
  if (m_fullNode) return m_fullNode->bumpClaim(params);
  if (m_spvHtlc) return m_spvHtlc->bumpClaim(params);
  return ChainClientResult::notSent(chainName() + ": no RPC or SPV client available");
}

} // namespace XfgSwap
