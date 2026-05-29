#include "CdOfferRelay.h"
#include "Core.h"
#include "P2p/LevinProtocol.h"

namespace CryptoNote {

CdOfferRelay::CdOfferRelay(core& ccore, NodeServer& p2psrv, IP2pEndpoint* p2pEndpoint)
  : m_core(ccore), m_p2p(p2psrv), m_p2pEndpoint(p2pEndpoint) {}

CdOfferRelay::~CdOfferRelay() { stop(); }

void CdOfferRelay::start() {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_running) return;
  m_running = true;
  m_cleanupThread = std::thread([this] { cleanupThread(); });
}

void CdOfferRelay::stop() {
  m_running = false;
  if (m_cleanupThread.joinable()) {
    m_cleanupThread.join();
  }
}

void CdOfferRelay::cleanupThread() {
  while (m_running) {
    try {
      uint32_t currentHeight = 0;
      Crypto::Hash topId;
      m_core.get_blockchain_top(currentHeight, topId);

      {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto it = m_offers.begin(); it != m_offers.end(); ) {
          if (currentHeight > it->second.postedHeight + it->second.ttlBlocks) {
            it = m_offers.erase(it);
          } else {
            ++it;
          }
        }
      }
    } catch (...) {}

    for (int i = 0; i < 30 && m_running; ++i) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }
}

bool CdOfferRelay::validateOffer(const COMMAND_CD_OFFER::request& offer) const {
  if (offer.offerId.empty()) return false;
  if (offer.cdAmount == 0 || offer.askPrice == 0) return false;
  if (offer.ttlBlocks == 0 || offer.ttlBlocks > 1080) return false;
  Crypto::Hash offerHash;
  cn_fast_hash(offer.offerId.data(), offer.offerId.size(), offerHash);
  return Crypto::check_signature(offerHash, offer.makerPubKey, offer.signature);
}

void CdOfferRelay::handleOfferMessage(const COMMAND_CD_OFFER::request& offer) {
  if (!validateOffer(offer)) return;
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_offers.find(offer.offerId) != m_offers.end()) return;
  if (m_offers.size() >= MAX_OFFERS) return; // DoS protection
  m_offers[offer.offerId] = offer;
}

void CdOfferRelay::handleCancelMessage(const COMMAND_CD_CANCEL::request& msg) {
  Crypto::Hash cancelHash;
  std::string cancelData = "cancel:" + msg.offerId;
  cn_fast_hash(cancelData.data(), cancelData.size(), cancelHash);
  if (!Crypto::check_signature(cancelHash, msg.makerPubKey, msg.signature)) return;

  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_offers.find(msg.offerId);
  if (it != m_offers.end() && it->second.makerPubKey == msg.makerPubKey) {
    m_offers.erase(it);
  }
}

std::vector<COMMAND_CD_OFFER::request> CdOfferRelay::getOffers(uint64_t amount) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::vector<COMMAND_CD_OFFER::request> res;
  for (const auto& kv : m_offers) {
    if (amount == 0 || kv.second.cdAmount == amount) {
      res.push_back(kv.second);
    }
  }
  return res;
}

bool CdOfferRelay::submitOffer(const COMMAND_CD_OFFER::request& offer) {
  if (!validateOffer(offer)) return false;
  handleOfferMessage(offer);
  if (m_p2pEndpoint) {
    auto buf = LevinProtocol::encode(offer);
    m_p2pEndpoint->externalRelayNotifyToAll(COMMAND_CD_OFFER::ID, buf, nullptr);
  }
  return true;
}

bool CdOfferRelay::cancelOffer(const std::string& offerId, const Crypto::PublicKey& pubkey, const Crypto::Signature& sig) {
  COMMAND_CD_CANCEL::request msg;
  msg.offerId = offerId;
  msg.makerPubKey = pubkey;
  msg.signature = sig;
  handleCancelMessage(msg);
  if (m_p2pEndpoint) {
    auto buf = LevinProtocol::encode(msg);
    m_p2pEndpoint->externalRelayNotifyToAll(COMMAND_CD_CANCEL::ID, buf, nullptr);
  }
  return true;
}

}
