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
    } catch (...) {}

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
  for (int pair = 0; pair < 8; ++pair) {
    auto& book = m_orderBooks[pair];
    auto cleanLadder = [&](std::map<uint64_t, PriceLevel>& ladder) {
      for (auto priceIt = ladder.begin(); priceIt != ladder.end(); ) {
        auto& q = priceIt->second;
        for (auto oit = q.orders.begin(); oit != q.orders.end(); ) {
          if (currentHeight > oit->postedHeight + oit->ttlBlocks) {
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
  // pair must index a valid order book slot (0..7)
  if (offer.pair >= 8) return false;
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
                                         const Crypto::Signature& sig) {
  std::string cancelData = "cancel:" + offerId;
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
  static constexpr size_t MAX_PENDING_REQUESTS = 256;
  if (m_pendingRequests.size() >= MAX_PENDING_REQUESTS) {
    m_pendingRequests.erase(m_pendingRequests.begin());  // drop oldest
  }
  m_pendingRequests.push_back({offerId, amount, takerPubKey, proofOfFunds});
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
                                 const Crypto::Signature& sig) {
  handleCancelMessage(offerId, pubkey, sig);

  if (m_p2pEndpoint) {
    COMMAND_SWAP_CANCEL::request msg;
    msg.offerId    = offerId;
    msg.makerPubKey = pubkey;
    msg.signature   = sig;
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

std::string SwapOfferRelay::generateOrderId(const SwapOrder& o) const {
  std::string data;
  data.append(reinterpret_cast<const char*>(&o.makerPubKey), sizeof(o.makerPubKey));
  data.append(reinterpret_cast<const char*>(&o.side), sizeof(o.side));
  data.append(reinterpret_cast<const char*>(&o.pair), sizeof(o.pair));
  data.append(reinterpret_cast<const char*>(&o.price), sizeof(o.price));
  data.append(reinterpret_cast<const char*>(&o.amount), sizeof(o.amount));
  data.append(reinterpret_cast<const char*>(&o.nonce), sizeof(o.nonce));
  data.append(reinterpret_cast<const char*>(&o.timestamp), sizeof(o.timestamp));

  Crypto::Hash hash;
  cn_fast_hash(data.data(), data.size(), hash);
  return Common::podToHex(hash);
}

bool SwapOfferRelay::validateOrderSignature(const SwapOrder& o) const {
  if (o.orderId.empty()) return false;
  if (o.amount == 0 || o.price == 0) return false;
  if (o.ttlBlocks == 0 || o.ttlBlocks > 1080) return false;

  // Sign canonical fields: orderId + side + pair + price + amount + nonce
  std::string data;
  data.append(o.orderId);
  data.append(reinterpret_cast<const char*>(&o.side), sizeof(o.side));
  data.append(reinterpret_cast<const char*>(&o.pair), sizeof(o.pair));
  data.append(reinterpret_cast<const char*>(&o.price), sizeof(o.price));
  data.append(reinterpret_cast<const char*>(&o.amount), sizeof(o.amount));
  data.append(reinterpret_cast<const char*>(&o.nonce), sizeof(o.nonce));

  Crypto::Hash hash;
  cn_fast_hash(data.data(), data.size(), hash);
  return Crypto::check_signature(hash, o.makerPubKey, o.signature);
}

void SwapOfferRelay::insertOrderIntoBook(SwapOrder&& order) {
  uint8_t pair = order.pair;
  if (pair >= 8) return;  // bounds: m_orderBooks has 8 slots
  uint64_t price = order.price;
  auto& book = m_orderBooks[pair];

  if (order.side == SwapOrder::Side::ASK) {
    book.asks[price].orders.push_back(std::move(order));
  } else {
    book.bids[price].orders.push_back(std::move(order));
  }
}

void SwapOfferRelay::handleOrderOpen(const COMMAND_ORDER_OPEN::request& msg) {
  if (msg.pair >= 8) return;  // reject OOB pair before book access
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

  if (m_allOrders.size() >= MAX_ORDERS_PER_PAIR) return;
  if (m_allOrders.find(order.orderId) != m_allOrders.end()) return;

  // Check if this order can match
  auto fills = matchOrder(order.side, order.pair, order.price, order.amount);

  if (!fills.empty()) {
    // Record fills
    for (const auto& fill : fills) {
      COMMAND_ORDER_FILL::request fillMsg;
      fillMsg.takerOrderId = order.orderId;
      fillMsg.makerOrderId = fill.makerOrderId;
      fillMsg.fillAmount   = fill.fillAmount;
      fillMsg.fillPrice    = fill.fillPrice;
      fillMsg.timestamp    = order.timestamp;
      fillMsg.blockHeight  = order.postedHeight;

      // Update maker in book
      auto makerIt = m_allOrders.find(fill.makerOrderId);
      if (makerIt != m_allOrders.end()) {
        makerIt->second.filled += fill.fillAmount;
        if (makerIt->second.filled >= makerIt->second.amount) {
          // Fully filled — remove from ladder
          uint8_t makerPair = makerIt->second.pair;
          uint64_t makerPrice = makerIt->second.price;
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
          m_allOrders.erase(makerIt);
        }
      }

      m_filledOrderIds.push_back(fillMsg.makerOrderId);
      broadcastOrderFill(fillMsg);
    }

    order.filled = 0;
    for (const auto& f : fills) order.filled += f.fillAmount;
  }

  // Insert remaining into book
  if (order.filled < order.amount) {
    order.amount -= order.filled;
    order.filled = 0;
    std::string oid = order.orderId;
    insertOrderIntoBook(std::move(order));
    m_allOrders[oid] = order;  // re-insert with reduced amount

    COMMAND_ORDER_OPEN::request relay = msg;
    broadcastOrderOpen(relay);
  }
}

void SwapOfferRelay::handleOrderCancel(const COMMAND_ORDER_CANCEL::request& msg) {
  std::lock_guard<std::mutex> lock(m_mutex);

  auto it = m_allOrders.find(msg.orderId);
  if (it == m_allOrders.end()) return;
  if (it->second.makerPubKey != msg.makerPubKey) return;

  // Verify maker signed cancel of this orderId (prevents anyone from cancelling
  // by replaying a public makerPubKey without the secret key).
  {
    std::string cancelData = "cancel:" + msg.orderId;
    Crypto::Hash cancelHash;
    cn_fast_hash(cancelData.data(), cancelData.size(), cancelHash);
    if (!Crypto::check_signature(cancelHash, msg.makerPubKey, msg.signature)) {
      return;
    }
  }

  // Remove from ladder
  uint8_t pair = it->second.pair;
  if (pair >= 8) {
    m_allOrders.erase(it);
    return;
  }
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

  m_allOrders.erase(it);
  broadcastOrderCancel(msg);
}

void SwapOfferRelay::handleOrderFill(const COMMAND_ORDER_FILL::request& msg) {
  std::lock_guard<std::mutex> lock(m_mutex);
  // Replay protection: ignore if we already processed this fill
  for (const auto& id : m_filledOrderIds) {
    if (id == msg.makerOrderId) return;
  }
  // Bound fill-id set (LRU: drop oldest half when over cap)
  static constexpr size_t MAX_FILLED_IDS = 10000;
  if (m_filledOrderIds.size() >= MAX_FILLED_IDS) {
    m_filledOrderIds.erase(m_filledOrderIds.begin(),
                           m_filledOrderIds.begin() + static_cast<long>(MAX_FILLED_IDS / 2));
  }
  m_filledOrderIds.push_back(msg.makerOrderId);
}

void SwapOfferRelay::handleOrderReserve(const COMMAND_ORDER_RESERVE::request& msg) {
  std::lock_guard<std::mutex> lock(m_mutex);

  auto makerIt = m_allOrders.find(msg.makerOrderId);
  if (makerIt == m_allOrders.end()) return;
  if (msg.amount == 0 || msg.amount > (makerIt->second.amount - makerIt->second.filled)) return;

  Reservation r;
  r.reservationId = msg.reservationId;
  r.takerOrderId  = msg.takerOrderId;
  r.makerOrderId  = msg.makerOrderId;
  r.amount        = msg.amount;
  r.takerPubKey   = msg.takerPubKey;
  r.expiresAt     = static_cast<uint64_t>(std::time(nullptr)) + RESERVATION_TTL_SECS;
  m_reservations[msg.reservationId] = std::move(r);
}

void SwapOfferRelay::handleOrderReserveAck(const COMMAND_ORDER_RESERVE_ACK::request& msg) {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_reservations.find(msg.reservationId);
  if (it == m_reservations.end()) return;
  if (it->second.makerOrderId != msg.makerOrderId) return;
  // Ack valid — taker can now proceed to SwapDaemon.initiate()
}

std::vector<SwapOfferRelay::Fill> SwapOfferRelay::matchOrder(
    SwapOrder::Side takerSide, uint8_t pair,
    uint64_t takerPrice, uint64_t takerAmount) {

  // NOTE: caller must hold m_mutex
  std::vector<Fill> fills;
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

bool SwapOfferRelay::placeOrder(SwapOrder::Side side, uint8_t pair, uint64_t price,
                                uint64_t amount, uint32_t ttlBlocks,
                                std::string& outOrderId) {
  if (pair > 7) return false;
  if (amount == 0 || price == 0) return false;

  // Generate a dummy order for ID computation (caller provides pubkey via RPC auth)
  // For daemon-generated orderId, we need the maker's pubkey from the RPC context
  // The caller must set orderId after receiving this

  uint32_t currentHeight = 0;
  Crypto::Hash topId;
  m_core.get_blockchain_top(currentHeight, topId);

  // Build order
  SwapOrder order;
  order.side         = side;
  order.pair         = pair;
  order.price        = price;
  order.amount       = amount;
  order.filled       = 0;
  order.ttlBlocks    = ttlBlocks;
  order.postedHeight = currentHeight;
  order.timestamp    = static_cast<uint64_t>(std::time(nullptr));
  order.nonce        = 0;  // will be set by caller

  // orderId is set by the caller (from RPC request, containing the signed canonical form)
  if (outOrderId.empty()) return false;
  order.orderId = outOrderId;

  std::lock_guard<std::mutex> lock(m_mutex);

  if (m_allOrders.size() >= MAX_ORDERS_PER_PAIR) return false;
  if (m_allOrders.find(order.orderId) != m_allOrders.end()) return false;

  // Try matching first
  auto fills = matchOrder(side, pair, price, amount);

  if (!fills.empty()) {
    for (const auto& fill : fills) {
      COMMAND_ORDER_FILL::request fillMsg;
      fillMsg.takerOrderId = order.orderId;
      fillMsg.makerOrderId = fill.makerOrderId;
      fillMsg.fillAmount   = fill.fillAmount;
      fillMsg.fillPrice    = fill.fillPrice;
      fillMsg.timestamp    = order.timestamp;
      fillMsg.blockHeight  = currentHeight;

      m_filledOrderIds.push_back(fillMsg.makerOrderId);
      broadcastOrderFill(fillMsg);
    }

    uint64_t totalFilled = 0;
    for (const auto& f : fills) totalFilled += f.fillAmount;

    // Insert remainder
    if (totalFilled < amount) {
      order.amount -= totalFilled;
      order.filled = 0;
      insertOrderIntoBook(std::move(order));
      m_allOrders[order.orderId] = order;
    }

    outOrderId = order.orderId;
  } else {
    insertOrderIntoBook(std::move(order));
    m_allOrders[order.orderId] = order;
    outOrderId = order.orderId;
  }

  return true;
}

bool SwapOfferRelay::cancelOrderByClient(const std::string& orderId) {
  std::lock_guard<std::mutex> lock(m_mutex);

  auto it = m_allOrders.find(orderId);
  if (it == m_allOrders.end()) return false;

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

  m_allOrders.erase(it);
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
