#pragma once
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include "crypto/crypto.h"
#include "crypto/hash.h"
#include "P2p/P2pProtocolDefinitions.h"

namespace CryptoNote {
class core;
class NodeServer;
class IP2pEndpoint;

class CdOfferRelay {
public:
  CdOfferRelay(core& ccore, NodeServer& p2psrv, IP2pEndpoint* p2pEndpoint = nullptr);
  ~CdOfferRelay();

  void start();
  void stop();

  void handleOfferMessage(const COMMAND_CD_OFFER::request& msg);
  void handleCancelMessage(const COMMAND_CD_CANCEL::request& msg);

  std::vector<COMMAND_CD_OFFER::request> getOffers(uint64_t amount) const;
  bool submitOffer(const COMMAND_CD_OFFER::request& offer);
  bool cancelOffer(const std::string& offerId, const Crypto::PublicKey& pubkey, const Crypto::Signature& sig);

private:
  core& m_core;
  NodeServer& m_p2p;
  IP2pEndpoint* m_p2pEndpoint;
  mutable std::mutex m_mutex;
  std::atomic<bool> m_running{false};
  std::thread m_cleanupThread;
  void cleanupThread();

  static constexpr size_t MAX_OFFERS = 10000;
  std::map<std::string, COMMAND_CD_OFFER::request> m_offers;
  bool validateOffer(const COMMAND_CD_OFFER::request& offer) const;
};
}
