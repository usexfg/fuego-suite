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

#include "SwapOfferRelay.h"
#include "Core.h"
#include "P2p/LevinProtocol.h"
#include "Logging/LoggerRef.h"
#include "Common/Util.h"
#include "Common/StringTools.h"
#include <iostream>

namespace CryptoNote {

// ═══════════════════════════════════════════════════════════════════════════════
// Construction / lifecycle
// ═══════════════════════════════════════════════════════════════════════════════

SwapOfferRelay::SwapOfferRelay(core& ccore, NodeServer& p2psrv, IP2pEndpoint* p2pEndpoint)
  : m_core(ccore), m_p2p(p2psrv), m_p2pEndpoint(p2pEndpoint) {
}

SwapOfferRelay::~SwapOfferRelay() {
  stop();
}

void SwapOfferRelay::start() {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_running) return;
  m_running = true;
  m_cleanupThread = std::thread(&SwapOfferRelay::cleanupThread, this);
}

void SwapOfferRelay::stop() {
  m_running = false;
  if (m_cleanupThread.joinable()) {
    m_cleanupThread.join();
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Cleanup thread
// ═══════════════════════════════════════════════════════════════════════════════

void SwapOfferRelay::cleanupThread() {
  while (m_running) {
    try {
      cleanupLegacyOffers();
      cleanupExpiredOrders();
      cleanupExpiredReservations();
    } catch (const std::exception& e) {
      try { std::cerr << "SwapOfferRelay::cleanupThread: " << e.what() << std::endl; } catch (...) {}
    } catch (...) {
      try { std::cerr << "SwapOfferRelay::cleanupThread: unknown exception" << std::endl; } catch (...) {}
    }

    for (int i = 0; i < 30 && m_running; ++i) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }
}

void SwapOfferRelay::cleanupLegacyOffers() {
  uint32_t currentHeight = 0;
  Crypto::Hash topId;
  m_core.get_blockchain_top(currentHeight, topId);

  std::lock_guard<std::mutex> lock(m_mutex);
  for (auto it = m_offers.begin(); it != m_offers.end(); ) {
    if (currentHeight > it->second.postedHeight + it->second.ttlBlocks) {
      it = m_offers.erase(it);
    } else {
      ++it;
    }
  }
}

void SwapOfferRelay::cleanupExpiredOrders() {
  uint32_t currentHeight = 0;
  Crypto::Hash topId;
  m_core.get_blockchain_top(currentHeight, topId);

  std::lock_guard<std::mutex> lock(m_mutex);
  for (int pair = 0; pair <= SwapOfferRelay::MAX_PAIR_INDEX; ++pair) {
    auto& book = m_orderBooks[pair];
    auto cleanLadder = [&](std::map<uint64_t, PriceLevel>& ladder) {
      for (auto priceIt = ladder.begin(); priceIt != ladder.end(); ) {
        auto& q = priceIt->second;
        for (auto oit = q.orders.begin(); oit != q.orders.end(); ) {
          if (currentHeight > oit->postedHeight + oit->ttlBlocks) {
            tombstoneOrder(oit->orderId);
            m_allOrders.erase(oit->orderId);
            oit = q.orders.erase(oit);
          } else {
            ++oit;
          }
        }
        if (q.orders.empty()) {
          priceIt = ladder.erase(priceIt);
        } else {
          ++priceIt;
        }
      }
    };
    cleanLadder(book.bids);
    cleanLadder(book.asks);
  }
}

void SwapOfferRelay::cleanupExpiredReservations() {
  uint64_t now = static_cast<uint64_t>(std::time(nullptr));
  std::lock_guard<std::mutex> lock(m_mutex);
  for (auto it = m_reservations.begin(); it != m_reservations.end(); ) {
    if (now > it->second.expiresAt) {
      // Return reserved amount to the maker order's available depth
      auto rbm = m_reservedByMaker.find(it->second.makerOrderId);
      if (rbm != m_reservedByMaker.end()) {
        if (rbm->second <= it->second.amount) m_reservedByMaker.erase(rbm);
        else rbm->second -= it->second.amount;
      }
      it = m_reservations.erase(it);
    } else {
      ++it;
    }
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Legacy v1 implementation
// ═══════════════════════════════════════════════════════════════════════════════

// Canonical offer digest: all economic fields (not just offerId) so rebroadcast
// with mutated amount/rate/pair/isSoftOrder cannot reuse a valid signature.
static Crypto::Hash offerCanonicalHash(const SwapOfferMsg& offer) {
  std::string data;
  data.reserve(offer.offerId.size() + 64);
  data.append(offer.offerId);
  data.append(1, static_cast<char>(offer.pair));
  data.append(reinterpret_cast<const char*>(&offer.xfgAmount), sizeof(offer.xfgAmount));
  data.append(reinterpret_cast<const char*>(&offer.rateNum), sizeof(offer.rateNum));
  data.append(1, offer.isSoftOrder ? '\x01' : '\x00');
  data.append(reinterpret_cast<const char*>(&offer.ttlBlocks), sizeof(offer.ttlBlocks));
  data.append(1, static_cast<char>(offer.allowedSlippagePct));
  data.append(reinterpret_cast<const char*>(&offer.timestamp), sizeof(offer.timestamp));
  Crypto::Hash h;
  cn_fast_hash(data.data(), data.size(), h);
  return h;
}

bool SwapOfferRelay::validateOffer(const SwapOfferMsg& offer) const {
  if (offer.offerId.empty()) return false;
  if (offer.xfgAmount == 0 || offer.rateNum == 0) return false;
  if (offer.ttlBlocks == 0 || offer.ttlBlocks > 1080) return false;
  // pair must index a valid order book slot (0..11)
  if (!isValidPair(offer.pair)) return false;
  Crypto::Hash offerHash = offerCanonicalHash(offer);
  return Crypto::check_signature(offerHash, offer.makerPubKey, offer.signature);
}

void SwapOfferRelay::handleOfferMessage(const COMMAND_SWAP_OFFER::request& msg) {
  SwapOfferMsg offer;
  offer.offerId            = msg.offerId;
  offer.xfgAmount          = msg.xfgAmount;
  offer.rateNum            = msg.rateNum;
  offer.pair               = msg.pair;
  offer.makerPubKey        = msg.makerPubKey;
  offer.signature          = msg.signature;
  offer.timestamp          = msg.timestamp;
  offer.ttlBlocks          = msg.ttlBlocks;
  offer.postedHeight       = msg.postedHeight;
  offer.isSoftOrder        = msg.isSoftOrder;
  offer.allowedSlippagePct = msg.allowedSlippagePct;
  offer.isSell             = true;

  if (!validateOffer(offer)) return;
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_offers.find(offer.offerId) != m_offers.end()) return;
  if (m_offers.size() >= MAX_LEGACY_OFFERS) return;
  m_offers[offer.offerId] = std::move(offer);
}

void SwapOfferRelay::handleCancelMessage(const std::string& offerId,
                                         const Crypto::PublicKey& pubkey,
                                         const Crypto::Signature& sig,
                                         uint64_t timestamp) {
  // Cancel signature is bound to the offerId AND the cancellation timestamp.
  // This prevents an old captured cancel signature from being replayed
  // against a re-announced offer with the same offerId.
  std::string cancelData = "cancel:" + offerId + ":" + std::to_string(timestamp);
  Crypto::Hash cancelHash;
  cn_fast_hash(cancelData.data(), cancelData.size(), cancelHash);
  if (!Crypto::check_signature(cancelHash, pubkey, sig)) return;

  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_offers.find(offerId);
  if (it != m_offers.end() && it->second.makerPubKey == pubkey) {
    m_offers.erase(it);
  }
}

void SwapOfferRelay::handleSwapRequest(const std::string& offerId, uint64_t amount,
                                       const std::string& takerPubKey,
                                       const std::string& proofOfFunds) {
  std::lock_guard<std::mutex> lock(m_mutex);
  // Bound queue to prevent memory-exhaustion DoS from gossip floods.
  if (m_pendingRequests.size() >= MAX_PENDING_REQUESTS) {
    m_pendingRequests.erase(m_pendingRequests.begin());  // drop oldest
  }
  m_pendingRequests.push_back({offerId, amount, takerPubKey, proofOfFunds});
}

void SwapOfferRelay::submitSwapRequest(const std::string& offerId, uint64_t amount,
                                       const std::string& takerPubKey,
                                       const std::string& proofOfFunds) {
  handleSwapRequest(offerId, amount, takerPubKey, proofOfFunds);

  if (m_p2pEndpoint) {
    COMMAND_SWAP_REQUEST::request msg;
    msg.offerId = offerId;
    msg.amount = amount;
    msg.takerPubKey = takerPubKey;
    msg.proofOfFunds = proofOfFunds;
    auto buf = LevinProtocol::encode(msg);
    m_p2pEndpoint->externalRelayNotifyToAll(COMMAND_SWAP_REQUEST::ID, buf, nullptr);
  }
}

void SwapOfferRelay::handleSwapRequestResult(const std::string& takerPubKey,
                                             const std::string& offerId,
                                             const std::string& lockId,
                                             const std::string& makerEndpoint,
                                             const std::string& adaptorPoint,
                                             const std::string& hashLock,
                                             const std::string& preSig,
                                             const std::string& ctrAddress,
                                             uint64_t createdAt) {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto& vec = m_requestResults[takerPubKey];
  // Dedupe by lockId (single-hop gossip may deliver duplicates).
  for (const auto& r : vec) {
    if (r.lockId == lockId) return;
  }
  if (vec.size() >= MAX_REQUEST_RESULTS_PER_TAKER) {
    vec.erase(vec.begin());  // drop oldest
  }
  SwapRequestResult r;
  r.offerId = offerId;
  r.lockId = lockId;
  r.makerEndpoint = makerEndpoint;
  r.adaptorPoint = adaptorPoint;
  r.hashLock = hashLock;
  r.preSig = preSig;
  r.ctrAddress = ctrAddress;
  r.createdAt = static_cast<time_t>(createdAt);
  vec.push_back(r);
}

void SwapOfferRelay::recordSwapRequestResult(const std::string& takerPubKey,
                                             const SwapRequestResult& result) {
  handleSwapRequestResult(takerPubKey, result.offerId, result.lockId,
                          result.makerEndpoint, result.adaptorPoint,
                          result.hashLock, result.preSig, result.ctrAddress,
                          static_cast<uint64_t>(result.createdAt));

  if (m_p2pEndpoint) {
    COMMAND_SWAP_REQUEST_RESULT::request msg;
    msg.takerPubKey = takerPubKey;
    msg.offerId = result.offerId;
    msg.lockId = result.lockId;
    msg.makerEndpoint = result.makerEndpoint;
    msg.adaptorPoint = result.adaptorPoint;
    msg.hashLock = result.hashLock;
    msg.preSig = result.preSig;
    msg.ctrAddress = result.ctrAddress;
    msg.createdAt = static_cast<uint64_t>(result.createdAt);
    auto buf = LevinProtocol::encode(msg);
    m_p2pEndpoint->externalRelayNotifyToAll(COMMAND_SWAP_REQUEST_RESULT::ID, buf, nullptr);
  }
}

std::vector<SwapRequestResult> SwapOfferRelay::getSwapRequestResults(const std::string& takerPubKey) {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_requestResults.find(takerPubKey);
  if (it == m_requestResults.end()) {
    return {};
  }
  return it->second;
}

void SwapOfferRelay::handleTradeCompleted(const SwapTradeRecord& /*trade*/) {
  // Drop unsigned P2P gossip trades (audit: TWAP manipulation).
  // Peers could inject fake rates with no proof. Only recordLocalTrade is trusted.
}

void SwapOfferRelay::recordLocalTrade(const SwapTradeRecord& trade) {
  std::lock_guard<std::mutex> lock(m_mutex);

  m_trades.push_back(trade);
  if (m_trades.size() > MAX_TRADES) {
    m_trades.erase(m_trades.begin(), m_trades.begin() + (m_trades.size() - MAX_TRADES));
  }

  auto& ts = m_twap[trade.pair];
  ts.sumProduct += trade.rate * static_cast<double>(trade.xfgAmount);
  ts.sumVolume  += static_cast<double>(trade.xfgAmount);
}

std::vector<SwapOfferMsg> SwapOfferRelay::getOffers(uint8_t pair) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::vector<SwapOfferMsg> res;
  for (const auto& kv : m_offers) {
    if (kv.second.pair == pair) {
      res.push_back(kv.second);
    }
  }
  return res;
}

std::vector<SwapOfferMsg> SwapOfferRelay::getAllOffers() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::vector<SwapOfferMsg> res;
  res.reserve(m_offers.size());
  for (const auto& kv : m_offers) {
    res.push_back(kv.second);
  }
  return res;
}

bool SwapOfferRelay::submitOffer(const SwapOfferMsg& offer) {
  if (!validateOffer(offer)) return false;

  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_offers.find(offer.offerId) != m_offers.end()) return false;
    if (m_offers.size() >= MAX_LEGACY_OFFERS) return false;
    m_offers[offer.offerId] = offer;
  }

  if (m_p2pEndpoint) {
    COMMAND_SWAP_OFFER::request msg;
    msg.offerId            = offer.offerId;
    msg.xfgAmount          = offer.xfgAmount;
    msg.rateNum            = offer.rateNum;
    msg.pair               = offer.pair;
    msg.makerPubKey        = offer.makerPubKey;
    msg.signature          = offer.signature;
    msg.timestamp          = offer.timestamp;
    msg.ttlBlocks          = offer.ttlBlocks;
    msg.postedHeight       = offer.postedHeight;
    msg.isSoftOrder        = offer.isSoftOrder;
    msg.allowedSlippagePct = offer.allowedSlippagePct;
    auto buf = LevinProtocol::encode(msg);
    m_p2pEndpoint->externalRelayNotifyToAll(COMMAND_SWAP_OFFER::ID, buf, nullptr);
  }
  return true;
}

bool SwapOfferRelay::cancelOffer(const std::string& offerId,
                                 const Crypto::PublicKey& pubkey,
                                 const Crypto::Signature& sig,
                                 uint64_t timestamp) {
  handleCancelMessage(offerId, pubkey, sig, timestamp);

  if (m_p2pEndpoint) {
    COMMAND_SWAP_CANCEL::request msg;
    msg.offerId    = offerId;
    msg.makerPubKey = pubkey;
    msg.signature   = sig;
    msg.timestamp   = timestamp;
    auto buf = LevinProtocol::encode(msg);
    m_p2pEndpoint->externalRelayNotifyToAll(COMMAND_SWAP_CANCEL::ID, buf, nullptr);
  }
  return true;
}

bool SwapOfferRelay::updateOfferAmount(const std::string& offerId, uint64_t newRemaining) {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_offers.find(offerId);
  if (it == m_offers.end()) return false;
  it->second.filledAmount = it->second.xfgAmount - newRemaining;
  if (newRemaining == 0) {
    m_offers.erase(it);
  }
  return true;
}

double SwapOfferRelay::getTwap(uint8_t pair) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_twap.find(pair);
  if (it == m_twap.end() || it->second.sumVolume == 0.0) {
    return getSeedRate(pair);
  }
  return it->second.sumProduct / it->second.sumVolume;
}

CompositePrice SwapOfferRelay::getCompositePrice(uint8_t pair) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_composite.find(pair);
  if (it == m_composite.end()) {
    CompositePrice cp;
    cp.rate = getSeedRate(pair);
    cp.sourceCount = 0;
    return cp;
  }
  CompositePrice cp;
  cp.rate        = it->second.rate;
  cp.sourceCount = it->second.sources.size();
  cp.sources     = it->second.sources;
  return cp;
}

NativeXfgPriceRange SwapOfferRelay::getNativeXfgPrice() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_nativeXfgPrice;
}

std::vector<SwapTradeRecord> SwapOfferRelay::getRecentTrades(uint8_t pair, uint32_t limit) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::vector<SwapTradeRecord> res;
  for (auto it = m_trades.rbegin(); it != m_trades.rend() && res.size() < limit; ++it) {
    if (it->pair == pair) {
      res.push_back(*it);
    }
  }
  return res;
}

double SwapOfferRelay::getSeedRate(uint8_t pair) {
  switch (pair) {
    case 0: return 145.0;   // XMR
    case 1: return 3500.0;  // ETH
    case 2: return 450.0;   // BCH
    case 3: return 170.0;   // SOL
    case 4: return 1800.0;  // ARB
    case 5: return 1800.0;  // BASE
    case 6: return 380.0;   // BNB
    default: return 0.0;
  }
}

std::vector<PendingSwapRequest> SwapOfferRelay::getPendingSwapRequests() {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::vector<PendingSwapRequest> result;
  result.swap(m_pendingRequests);
  return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// v2 Orderbook implementation
// ═══════════════════════════════════════════════════════════════════════════════

// Canonical orderId (must match wallet sign_order):
//   orderId = hex(cn_fast_hash(pubkey || side || pair || price || amount || nonce))
// Signature: check_signature(orderIdHash, makerPubKey, signature)
// where orderIdHash is the raw 32-byte hash (not the hex string).
std::string SwapOfferRelay::generateOrderId(const SwapOrder& o) const {
  std::string data;
  data.append(reinterpret_cast<const char*>(&o.makerPubKey), sizeof(o.makerPubKey));
  uint8_t side = static_cast<uint8_t>(o.side);
  data.append(reinterpret_cast<const char*>(&side), sizeof(side));
  data.append(reinterpret_cast<const char*>(&o.pair), sizeof(o.pair));
  data.append(reinterpret_cast<const char*>(&o.price), sizeof(o.price));
  data.append(reinterpret_cast<const char*>(&o.amount), sizeof(o.amount));
  data.append(reinterpret_cast<const char*>(&o.nonce), sizeof(o.nonce));

  Crypto::Hash hash;
  cn_fast_hash(data.data(), data.size(), hash);
  return Common::podToHex(hash);
}

bool SwapOfferRelay::validateOrderSignature(const SwapOrder& o) const {
  if (o.orderId.empty() || o.orderId.size() != 64) return false;
  if (o.amount == 0 || o.price == 0) return false;
  if (o.ttlBlocks == 0 || o.ttlBlocks > 1080) return false;
  if (!isValidPair(o.pair)) return false;
  if (o.side != SwapOrder::Side::BID && o.side != SwapOrder::Side::ASK) return false;

  // orderId must be the canonical hash of maker fields
  if (o.orderId != generateOrderId(o)) return false;

  Crypto::Hash orderIdHash;
  if (!Common::podFromHex(o.orderId, orderIdHash)) return false;
  return Crypto::check_signature(orderIdHash, o.makerPubKey, o.signature);
}

bool SwapOfferRelay::validateCancelSignature(const std::string& orderId,
                                             const Crypto::PublicKey& makerPubKey,
                                             const Crypto::Signature& signature,
                                             uint64_t timestamp) const {
  if (orderId.empty()) return false;
  // Cancel signature bound to orderId AND timestamp (anti-replay).
  std::string cancelData = "cancel:" + orderId + ":" + std::to_string(timestamp);
  Crypto::Hash cancelHash;
  cn_fast_hash(cancelData.data(), cancelData.size(), cancelHash);
  return Crypto::check_signature(cancelHash, makerPubKey, signature);
}

bool SwapOfferRelay::isTombstoned(const std::string& orderId) const {
  return m_tombstones.find(orderId) != m_tombstones.end();
}

void SwapOfferRelay::tombstoneOrder(const std::string& orderId) {
  if (orderId.empty()) return;
  m_tombstones[orderId] = static_cast<uint64_t>(std::time(nullptr));
  while (m_tombstones.size() > MAX_TOMBSTONES) {
    m_tombstones.erase(m_tombstones.begin());
  }
}

std::string SwapOfferRelay::makeFillReplayKey(const COMMAND_ORDER_FILL::request& msg) const {
  std::string data;
  data.append(msg.takerOrderId);
  data.push_back('|');
  data.append(msg.makerOrderId);
  data.push_back('|');
  data.append(reinterpret_cast<const char*>(&msg.fillAmount), sizeof(msg.fillAmount));
  data.append(reinterpret_cast<const char*>(&msg.fillPrice), sizeof(msg.fillPrice));
  data.append(reinterpret_cast<const char*>(&msg.blockHeight), sizeof(msg.blockHeight));
  Crypto::Hash h;
  cn_fast_hash(data.data(), data.size(), h);
  return Common::podToHex(h);
}

void SwapOfferRelay::insertOrderIntoBook(SwapOrder order) {
  if (!isValidPair(order.pair)) return;
  uint8_t pair = order.pair;
  if (!isValidPair(pair)) return;  // bounds: m_orderBooks has 12 slots
  uint64_t price = order.price;
  auto& book = m_orderBooks[pair];

  if (order.side == SwapOrder::Side::ASK) {
    book.asks[price].orders.push_back(std::move(order));
  } else {
    book.bids[price].orders.push_back(std::move(order));
  }
}

void SwapOfferRelay::handleOrderOpen(const COMMAND_ORDER_OPEN::request& msg) {
  // CRITICAL: pair bounds before any book access
  if (!isValidPair(msg.pair)) return;
  if (msg.side > 1) return;

  SwapOrder order;
  order.orderId      = msg.orderId;
  order.side         = static_cast<SwapOrder::Side>(msg.side);
  order.pair         = msg.pair;
  order.price        = msg.price;
  order.amount       = msg.amount;
  order.makerPubKey  = msg.makerPubKey;
  order.signature    = msg.signature;
  order.nonce        = msg.nonce;
  order.timestamp    = msg.timestamp;
  order.ttlBlocks    = msg.ttlBlocks;
  order.postedHeight = msg.postedHeight;
  order.filled       = 0;

  if (!validateOrderSignature(order)) return;

  std::lock_guard<std::mutex> lock(m_mutex);

  if (isTombstoned(order.orderId)) return;
  if (m_allOrders.size() >= MAX_ORDERS_PER_PAIR) return;
  if (m_allOrders.find(order.orderId) != m_allOrders.end()) return;

  auto fills = matchOrder(order.side, order.pair, order.price, order.amount);

  if (!fills.empty()) {
    for (const auto& fill : fills) {
      COMMAND_ORDER_FILL::request fillMsg;
      fillMsg.takerOrderId = order.orderId;
      fillMsg.makerOrderId = fill.makerOrderId;
      fillMsg.fillAmount   = fill.fillAmount;
      fillMsg.fillPrice    = fill.fillPrice;
      fillMsg.timestamp    = order.timestamp;
      fillMsg.blockHeight  = order.postedHeight;

      auto makerIt = m_allOrders.find(fill.makerOrderId);
      if (makerIt != m_allOrders.end()) {
        makerIt->second.filled += fill.fillAmount;
        if (makerIt->second.filled >= makerIt->second.amount) {
          uint8_t makerPair = makerIt->second.pair;
          uint64_t makerPrice = makerIt->second.price;
          if (isValidPair(makerPair)) {
            auto& ladder = (makerIt->second.side == SwapOrder::Side::ASK)
                           ? m_orderBooks[makerPair].asks
                           : m_orderBooks[makerPair].bids;
            auto priceIt = ladder.find(makerPrice);
            if (priceIt != ladder.end()) {
              auto& q = priceIt->second.orders;
              for (auto oit = q.begin(); oit != q.end(); ++oit) {
                if (oit->orderId == fill.makerOrderId) {
                  q.erase(oit);
                  break;
                }
              }
              if (q.empty()) ladder.erase(priceIt);
            }
          }
          tombstoneOrder(fill.makerOrderId);
          m_allOrders.erase(makerIt);
        }
      }

      m_fillReplay[makeFillReplayKey(fillMsg)] = static_cast<uint64_t>(std::time(nullptr));
      while (m_fillReplay.size() > MAX_FILL_REPLAY) {
        m_fillReplay.erase(m_fillReplay.begin());
      }
      broadcastOrderFill(fillMsg);
    }

    order.filled = 0;
    for (const auto& f : fills) order.filled += f.fillAmount;
  }

  if (order.filled < order.amount) {
    order.amount -= order.filled;
    order.filled = 0;
    std::string oid = order.orderId;
    // Copy into map first, then insert a copy into the ladder (no use-after-move)
    m_allOrders[oid] = order;
    insertOrderIntoBook(order);

    COMMAND_ORDER_OPEN::request relay = msg;
    broadcastOrderOpen(relay);
  } else {
    tombstoneOrder(order.orderId);
  }
}

void SwapOfferRelay::handleOrderCancel(const COMMAND_ORDER_CANCEL::request& msg) {
  // HIGH: require cancel signature (same scheme as legacy v1 cancel)
  if (!validateCancelSignature(msg.orderId, msg.makerPubKey, msg.signature, msg.timestamp)) return;

  std::lock_guard<std::mutex> lock(m_mutex);

  auto it = m_allOrders.find(msg.orderId);
  if (it == m_allOrders.end()) return;
  if (it->second.makerPubKey != msg.makerPubKey) return;
  if (!isValidPair(it->second.pair)) {
    m_allOrders.erase(it);
    return;
  }

  // Remove from ladder (signature already verified via validateCancelSignature)
  uint8_t pair = it->second.pair;
  uint64_t price = it->second.price;
  auto& ladder = (it->second.side == SwapOrder::Side::ASK)
                 ? m_orderBooks[pair].asks
                 : m_orderBooks[pair].bids;
  auto priceIt = ladder.find(price);
  if (priceIt != ladder.end()) {
    auto& q = priceIt->second.orders;
    for (auto oit = q.begin(); oit != q.end(); ++oit) {
      if (oit->orderId == msg.orderId) {
        q.erase(oit);
        break;
      }
    }
    if (q.empty()) ladder.erase(priceIt);
  }

  tombstoneOrder(msg.orderId);
  m_allOrders.erase(it);
  broadcastOrderCancel(msg);
}

void SwapOfferRelay::handleOrderFill(const COMMAND_ORDER_FILL::request& msg) {
  std::lock_guard<std::mutex> lock(m_mutex);
  // Replay protection keyed by full fill identity (not maker alone)
  std::string key = makeFillReplayKey(msg);
  if (m_fillReplay.find(key) != m_fillReplay.end()) return;
  m_fillReplay[key] = static_cast<uint64_t>(std::time(nullptr));
  while (m_fillReplay.size() > MAX_FILL_REPLAY) {
    m_fillReplay.erase(m_fillReplay.begin());
  }
}

void SwapOfferRelay::handleOrderReserve(const COMMAND_ORDER_RESERVE::request& msg) {
  std::lock_guard<std::mutex> lock(m_mutex);

  if (msg.reservationId.empty() || msg.makerOrderId.empty()) return;
  if (m_reservations.find(msg.reservationId) != m_reservations.end()) return;

  auto makerIt = m_allOrders.find(msg.makerOrderId);
  if (makerIt == m_allOrders.end()) return;

  uint64_t available = makerIt->second.amount - makerIt->second.filled;
  uint64_t alreadyReserved = 0;
  auto resIt = m_reservedByMaker.find(msg.makerOrderId);
  if (resIt != m_reservedByMaker.end()) alreadyReserved = resIt->second;
  if (msg.amount == 0 || msg.amount > available - alreadyReserved) return;

  // Taker pubkey must be present (hex-encoded). Full signed reserve is preferred;
  // reject empty / clearly invalid taker keys to reduce spam reservations.
  if (msg.takerPubKey.empty() || msg.takerPubKey.size() < 64) return;

  Reservation r;
  r.reservationId = msg.reservationId;
  r.takerOrderId  = msg.takerOrderId;
  r.makerOrderId  = msg.makerOrderId;
  r.amount        = msg.amount;
  r.takerPubKey   = msg.takerPubKey;
  r.expiresAt     = static_cast<uint64_t>(std::time(nullptr)) + RESERVATION_TTL_SECS;
  m_reservations[msg.reservationId] = std::move(r);
  m_reservedByMaker[msg.makerOrderId] = alreadyReserved + msg.amount;
}

void SwapOfferRelay::handleOrderReserveAck(const COMMAND_ORDER_RESERVE_ACK::request& msg) {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_reservations.find(msg.reservationId);
  if (it == m_reservations.end()) return;
  if (it->second.makerOrderId != msg.makerOrderId) return;

  // Verify maker signature over "reserve_ack:"+reservationId
  auto makerIt = m_allOrders.find(msg.makerOrderId);
  if (makerIt == m_allOrders.end()) return;
  if (makerIt->second.makerPubKey != msg.makerPubKey) return;

  std::string ackData = "reserve_ack:" + msg.reservationId;
  Crypto::Hash ackHash;
  cn_fast_hash(ackData.data(), ackData.size(), ackHash);
  if (!Crypto::check_signature(ackHash, msg.makerPubKey, msg.signature)) {
    // Invalid ack — drop reservation (do not proceed to initiate)
    uint64_t amt = it->second.amount;
    auto rbm = m_reservedByMaker.find(msg.makerOrderId);
    if (rbm != m_reservedByMaker.end()) {
      if (rbm->second <= amt) m_reservedByMaker.erase(rbm);
      else rbm->second -= amt;
    }
    m_reservations.erase(it);
    return;
  }
  // Ack valid — taker can now proceed to SwapDaemon.initiate()
}

std::vector<SwapOfferRelay::Fill> SwapOfferRelay::matchOrder(
    SwapOrder::Side takerSide, uint8_t pair,
    uint64_t takerPrice, uint64_t takerAmount) {

  // NOTE: caller must hold m_mutex
  std::vector<Fill> fills;
  if (!isValidPair(pair)) return fills;

  auto& oppositeLadder = (takerSide == SwapOrder::Side::ASK)
                         ? m_orderBooks[pair].bids
                         : m_orderBooks[pair].asks;

  uint64_t remaining = takerAmount;

  if (takerSide == SwapOrder::Side::ASK) {
    // Taker selling XFG for CTR: match against bids (highest first)
    for (auto priceIt = oppositeLadder.rbegin();
         priceIt != oppositeLadder.rend() && remaining > 0; ) {
      if (priceIt->first < takerPrice) break;  // bid price too low
      auto& q = priceIt->second;
      while (!q.orders.empty() && remaining > 0) {
        auto& maker = q.orders.front();
        uint64_t available = maker.amount - maker.filled;
        uint64_t fillAmt = std::min(remaining, available);
        fills.push_back({maker.orderId, priceIt->first, fillAmt});
        remaining -= fillAmt;
        maker.filled += fillAmt;
        if (maker.filled >= maker.amount) {
          m_allOrders.erase(maker.orderId);
          q.orders.pop_front();
        }
      }
      if (q.orders.empty()) {
        oppositeLadder.erase(priceIt.base());
        priceIt = oppositeLadder.rbegin();  // restart after erase
      } else {
        ++priceIt;
      }
    }
  } else {
    // Taker buying XFG with CTR: match against asks (lowest first)
    for (auto priceIt = oppositeLadder.begin();
         priceIt != oppositeLadder.end() && remaining > 0; ) {
      if (priceIt->first > takerPrice) break;  // ask price too high
      auto& q = priceIt->second;
      while (!q.orders.empty() && remaining > 0) {
        auto& maker = q.orders.front();
        uint64_t available = maker.amount - maker.filled;
        uint64_t fillAmt = std::min(remaining, available);
        fills.push_back({maker.orderId, priceIt->first, fillAmt});
        remaining -= fillAmt;
        maker.filled += fillAmt;
        if (maker.filled >= maker.amount) {
          m_allOrders.erase(maker.orderId);
          q.orders.pop_front();
        }
      }
      if (q.orders.empty()) {
        priceIt = oppositeLadder.erase(priceIt);
      } else {
        ++priceIt;
      }
    }
  }

  return fills;
}

// ── RPC handlers ──

OrderBookSnapshot SwapOfferRelay::getOrderBookSnapshot(uint8_t pair, int depth) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  OrderBookSnapshot snap;
  snap.height = m_core.get_current_blockchain_height();
  if (!isValidPair(pair) || depth <= 0) return snap;

  // Cap the depth to bound the response size (an unbounded depth would let a
  // caller serialize the entire book — memory/bandwidth DoS).
  static const int MAX_DEPTH = 500;
  if (depth > MAX_DEPTH) depth = MAX_DEPTH;

  const auto& book = m_orderBooks[pair];

  // Bids: highest first
  int count = 0;
  for (auto it = book.bids.rbegin(); it != book.bids.rend() && count < depth; ++it, ++count) {
    OrderBookSnapshot::LevelJson lvl;
    lvl.price      = it->first;
    lvl.amount     = it->second.totalDepth();
    lvl.orderCount = static_cast<int>(it->second.orders.size());
    snap.bids.push_back(std::move(lvl));
  }

  // Asks: lowest first
  count = 0;
  for (auto it = book.asks.begin(); it != book.asks.end() && count < depth; ++it, ++count) {
    OrderBookSnapshot::LevelJson lvl;
    lvl.price      = it->first;
    lvl.amount     = it->second.totalDepth();
    lvl.orderCount = static_cast<int>(it->second.orders.size());
    snap.asks.push_back(std::move(lvl));
  }

  if (!snap.bids.empty() && !snap.asks.empty()) {
    snap.spread = snap.asks.front().price - snap.bids.front().price;
  }

  return snap;
}

bool SwapOfferRelay::placeSignedOrder(const SwapOrder& inOrder, uint64_t* outFilled) {
  if (outFilled) *outFilled = 0;
  if (!validateOrderSignature(inOrder)) return false;

  uint32_t currentHeight = 0;
  Crypto::Hash topId;
  m_core.get_blockchain_top(currentHeight, topId);

  SwapOrder order = inOrder;
  order.filled = 0;
  if (order.postedHeight == 0) order.postedHeight = currentHeight;
  if (order.timestamp == 0) order.timestamp = static_cast<uint64_t>(std::time(nullptr));

  std::lock_guard<std::mutex> lock(m_mutex);

  if (isTombstoned(order.orderId)) return false;
  if (m_allOrders.size() >= MAX_ORDERS_PER_PAIR) return false;
  if (m_allOrders.find(order.orderId) != m_allOrders.end()) return false;

  auto fills = matchOrder(order.side, order.pair, order.price, order.amount);
  uint64_t totalFilled = 0;
  for (const auto& fill : fills) {
    COMMAND_ORDER_FILL::request fillMsg;
    fillMsg.takerOrderId = order.orderId;
    fillMsg.makerOrderId = fill.makerOrderId;
    fillMsg.fillAmount   = fill.fillAmount;
    fillMsg.fillPrice    = fill.fillPrice;
    fillMsg.timestamp    = order.timestamp;
    fillMsg.blockHeight  = currentHeight;

    m_fillReplay[makeFillReplayKey(fillMsg)] = static_cast<uint64_t>(std::time(nullptr));
    broadcastOrderFill(fillMsg);
    totalFilled += fill.fillAmount;
  }
  if (outFilled) *outFilled = totalFilled;

  if (totalFilled < order.amount) {
    order.amount -= totalFilled;
    order.filled = 0;
    m_allOrders[order.orderId] = order;
    insertOrderIntoBook(order);

    COMMAND_ORDER_OPEN::request openMsg;
    openMsg.orderId = order.orderId;
    openMsg.side = static_cast<uint8_t>(order.side);
    openMsg.pair = order.pair;
    openMsg.price = order.price;
    openMsg.amount = order.amount;
    openMsg.makerPubKey = order.makerPubKey;
    openMsg.signature = order.signature;
    openMsg.nonce = order.nonce;
    openMsg.timestamp = order.timestamp;
    openMsg.ttlBlocks = order.ttlBlocks;
    openMsg.postedHeight = order.postedHeight;
    openMsg.dandelion_stem = 0;
    openMsg.hop_count = 0;
    broadcastOrderOpen(openMsg);
  } else {
    tombstoneOrder(order.orderId);
  }
  return true;
}

bool SwapOfferRelay::cancelOrderByClient(const std::string& orderId,
                                         const Crypto::PublicKey& makerPubKey,
                                         const Crypto::Signature& signature,
                                         uint64_t timestamp) {
  if (!validateCancelSignature(orderId, makerPubKey, signature, timestamp)) return false;

  std::lock_guard<std::mutex> lock(m_mutex);

  auto it = m_allOrders.find(orderId);
  if (it == m_allOrders.end()) return false;
  if (it->second.makerPubKey != makerPubKey) return false;
  if (!isValidPair(it->second.pair)) {
    tombstoneOrder(orderId);
    m_allOrders.erase(it);
    return true;
  }

  uint8_t pair = it->second.pair;
  uint64_t price = it->second.price;
  auto& ladder = (it->second.side == SwapOrder::Side::ASK)
                 ? m_orderBooks[pair].asks
                 : m_orderBooks[pair].bids;
  auto priceIt = ladder.find(price);
  if (priceIt != ladder.end()) {
    auto& q = priceIt->second.orders;
    for (auto oit = q.begin(); oit != q.end(); ++oit) {
      if (oit->orderId == orderId) {
        q.erase(oit);
        break;
      }
    }
    if (q.empty()) ladder.erase(priceIt);
  }

  tombstoneOrder(orderId);
  m_allOrders.erase(it);

  COMMAND_ORDER_CANCEL::request cancelMsg;
  cancelMsg.orderId = orderId;
  cancelMsg.makerPubKey = makerPubKey;
  cancelMsg.signature = signature;
  // Broadcast the SAME verified timestamp the cancel was signed with, so
  // remote peers reconstruct the identical cancel digest and accept it.
  cancelMsg.timestamp = timestamp;
  broadcastOrderCancel(cancelMsg);
  return true;
}

// ── P2P broadcast helpers ──

void SwapOfferRelay::broadcastOrderOpen(const COMMAND_ORDER_OPEN::request& msg) {
  if (!m_p2pEndpoint) return;
  auto buf = LevinProtocol::encode(msg);
  m_p2pEndpoint->externalRelayNotifyToAll(COMMAND_ORDER_OPEN::ID, buf, nullptr);
}

void SwapOfferRelay::broadcastOrderCancel(const COMMAND_ORDER_CANCEL::request& msg) {
  if (!m_p2pEndpoint) return;
  auto buf = LevinProtocol::encode(msg);
  m_p2pEndpoint->externalRelayNotifyToAll(COMMAND_ORDER_CANCEL::ID, buf, nullptr);
}

void SwapOfferRelay::broadcastOrderFill(const COMMAND_ORDER_FILL::request& msg) {
  if (!m_p2pEndpoint) return;
  auto buf = LevinProtocol::encode(msg);
  m_p2pEndpoint->externalRelayNotifyToAll(COMMAND_ORDER_FILL::ID, buf, nullptr);
}

void SwapOfferRelay::broadcastReserveAck(const COMMAND_ORDER_RESERVE_ACK::request& msg) {
  if (!m_p2pEndpoint) return;
  auto buf = LevinProtocol::encode(msg);
  m_p2pEndpoint->externalRelayNotifyToAll(COMMAND_ORDER_RESERVE_ACK::ID, buf, nullptr);
}

} // namespace CryptoNote
