#include "BchChainClient.h"

namespace XfgSwap {

BchChainClient::BchChainClient(std::unique_ptr<BchRpcClient> rpc, const std::string& wif,
                               UtxoNetwork network)
  : m_rpc(std::move(rpc)) {
  if (!m_rpc) return;
  BchRpcClient* node = m_rpc.get();
  m_fullNode.reset(new UtxoHtlcFullNode(
      {"BCH", SwapPair::BCH, UtxoHtlcEncoding::P2SH_FORKID, bitcoinCashNetworkParams(network)},
      [node](const std::string& method, const std::string& paramsJson,
             Common::JsonValue& result, std::string& error) {
        return node->callJson(method, paramsJson, result, error);
      },
      wif));
}

BchChainClient::BchChainClient(std::shared_ptr<ISpvClient> spvClient, const std::string& wif,
                               UtxoNetwork network) {
  if (spvClient)
    m_spvHtlc.reset(new UtxoHtlcSpv(
        {"BCH", SwapPair::BCH, UtxoHtlcEncoding::P2SH_FORKID, bitcoinCashNetworkParams(network)},
        std::move(spvClient), wif));
}

std::string BchChainClient::getReceiveAddress() const {
  if (m_fullNode) return m_fullNode->receiveAddress();
  return m_spvHtlc ? m_spvHtlc->receiveAddress() : std::string();
}

std::string BchChainClient::getClaimPubKey() const {
  if (m_fullNode) return m_fullNode->claimPubKeyHex();
  return m_spvHtlc ? m_spvHtlc->claimPubKeyHex() : std::string();
}

bool BchChainClient::isReadyForNewSwap() {
  return readinessError().empty();
}

std::string BchChainClient::readinessError() {
  return readinessErrorFor(SwapRole::ALICE);
}

std::string BchChainClient::claimDeadlineError(const SwapParams& params) {
  if (m_fullNode) return m_fullNode->claimDeadlineError(params);
  return m_spvHtlc ? m_spvHtlc->claimDeadlineError(params) : std::string();
}

std::string BchChainClient::readinessErrorFor(SwapRole role) {
  if (m_fullNode) return m_fullNode->readinessError();
  if (m_spvHtlc) return m_spvHtlc->readinessErrorFor(role);
  return "BCH: no RPC or SPV client available";
}

ChainClientResult BchChainClient::lock(const SwapParams& params) {
  if (m_fullNode) return m_fullNode->lock(params);
  if (m_spvHtlc) return m_spvHtlc->lock(params);
  return ChainClientResult::fail("BCH lock: no RPC or SPV client available");
}

ChainClientResult BchChainClient::verifyLock(const SwapParams& params) {
  if (m_fullNode) return m_fullNode->verifyLock(params);
  if (m_spvHtlc) return m_spvHtlc->verifyLock(params);
  return ChainClientResult::fail("BCH verifyLock: no RPC or SPV client available");
}

ChainClientResult BchChainClient::claim(const SwapParams& params) {
  if (m_fullNode) return m_fullNode->claim(params);
  if (m_spvHtlc) return m_spvHtlc->claim(params);
  return ChainClientResult::notSent("BCH claim: no RPC or SPV client available");
}

ChainClientResult BchChainClient::refund(const SwapParams& params) {
  if (m_fullNode) return m_fullNode->refund(params);
  if (m_spvHtlc) return m_spvHtlc->refund(params);
  return ChainClientResult::fail("BCH refund: no RPC or SPV client available");
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
  if (m_fullNode) return m_fullNode->currentHeight(height);
  return m_spvHtlc && m_spvHtlc->currentHeight(height);
}

ChainClientResult BchChainClient::getTransactionDetails(const std::string& txId,
                                                        ChainClientResult& result) {
  if (m_fullNode) result = m_fullNode->transactionDetails(txId);
  else if (m_spvHtlc) result = m_spvHtlc->transactionDetails(txId);
  else result = ChainClientResult::fail("BCH: no RPC or SPV client available");
  return result;
}

std::string BchChainClient::tryExtractClaimedSecret(const SwapParams& params) {
  if (m_fullNode) return m_fullNode->tryExtractClaimedSecret(params);
  return m_spvHtlc ? m_spvHtlc->tryExtractClaimedSecret(params) : std::string();
}

bool BchChainClient::hasConfirmedClaim(const SwapParams& params) {
  if (m_fullNode) return m_fullNode->hasConfirmedClaim(params);
  return m_spvHtlc && m_spvHtlc->hasConfirmedClaim(params);
}

}  // namespace XfgSwap
