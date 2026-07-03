// Copyright (c) 2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You can redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

#include "OrderbookMempool.h"
#include "OrderbookTypes.h"
#include "CryptoNoteConfig.h"

#include <algorithm>
#include <cstring>

namespace CryptoNote {

OrderbookMempool::OrderbookMempool(uint32_t maxOrdersPerSender, uint32_t maxOrdersTotal)
  : m_maxOrdersPerSender(maxOrdersPerSender)
  , m_maxOrdersTotal(maxOrdersTotal) {}

bool OrderbookMempool::addOrder(const Order& order) {
  std::lock_guard<std::mutex> lock(m_mutex);

  if (m_orders.size() >= m_maxOrdersTotal) return false;
  if (order.amount == 0 || order.price == 0) return false;

  SenderKey sk{order.spendKey, order.viewKey};
  auto it = m_senderCounts.find(sk);
  if (it != m_senderCounts.end() && it->second >= m_maxOrdersPerSender)
    return false;

  m_orders[order.orderId] = order;
  m_senderCounts[sk]++;

  if (order.side == 0) insertBid(order, false);
  else insertAsk(order, false);

  return true;
}

bool OrderbookMempool::cancelOrder(const Crypto::Hash& orderId) {
  std::lock_guard<std::mutex> lock(m_mutex);

  auto it = m_orders.find(orderId);
  if (it == m_orders.end()) return false;

  removeFromIndex(it->second);
  SenderKey sk{it->second.spendKey, it->second.viewKey};
  auto sc = m_senderCounts.find(sk);
  if (sc != m_senderCounts.end()) {
    if (sc->second <= 1) m_senderCounts.erase(sc);
    else sc->second--;
  }
  m_orders.erase(it);
  return true;
}

bool OrderbookMempool::hasOrder(const Crypto::Hash& orderId) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_orders.count(orderId) || m_poolOrders.count(orderId);
}

const Order* OrderbookMempool::getOrder(const Crypto::Hash& orderId) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_orders.find(orderId);
  if (it != m_orders.end()) return &it->second;
  auto pit = m_poolOrders.find(orderId);
  return (pit != m_poolOrders.end()) ? &pit->second : nullptr;
}

void OrderbookMempool::setPoolOrders(const std::vector<Order>& orders) {
  std::lock_guard<std::mutex> lock(m_mutex);

  for (const auto& o : m_poolOrders) removeFromIndex(o.second);
  m_poolOrders.clear();

  for (const auto& o : orders) {
    m_poolOrders[o.orderId] = o;
    if (o.side == 0) insertBid(o, true);
    else insertAsk(o, true);
  }
}

void OrderbookMempool::clearPoolOrders() {
  std::lock_guard<std::mutex> lock(m_mutex);

  for (const auto& o : m_poolOrders) removeFromIndex(o.second);
  m_poolOrders.clear();
}

std::vector<OrderbookMempool::AggregatedLevel> OrderbookMempool::getBidCurve(uint32_t maxLevels) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::vector<AggregatedLevel> result;
  uint32_t count = 0;
  for (const auto& [price, entries] : m_bids) {
    if (count >= maxLevels) break;
    uint64_t depth = 0;
    for (const auto& e : entries) depth += e.order->amount;
    result.push_back({price, depth, static_cast<uint32_t>(entries.size())});
    count++;
  }
  return result;
}

std::vector<OrderbookMempool::AggregatedLevel> OrderbookMempool::getAskCurve(uint32_t maxLevels) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::vector<AggregatedLevel> result;
  uint32_t count = 0;
  for (const auto& [price, entries] : m_asks) {
    if (count >= maxLevels) break;
    uint64_t depth = 0;
    for (const auto& e : entries) depth += e.order->amount;
    result.push_back({price, depth, static_cast<uint32_t>(entries.size())});
    count++;
  }
  return result;
}

uint32_t OrderbookMempool::expireOrders(uint32_t currentHeight) {
  std::lock_guard<std::mutex> lock(m_mutex);
  uint32_t removed = 0;
  auto it = m_orders.begin();
  while (it != m_orders.end()) {
    if (it->second.expiration > 0 && currentHeight >= it->second.expiration) {
      removeFromIndex(it->second);
      SenderKey sk{it->second.spendKey, it->second.viewKey};
      auto sc = m_senderCounts.find(sk);
      if (sc != m_senderCounts.end()) {
        if (sc->second <= 1) m_senderCounts.erase(sc);
        else sc->second--;
      }
      it = m_orders.erase(it);
      removed++;
    } else {
      ++it;
    }
  }
  return removed;
}

OrderbookReceipt OrderbookMempool::generateReceipt() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  OrderbookReceipt r;
  r.clearingPrice = m_lastClearingPrice;
  auto bids = getBidCurve(50);
  auto asks = getAskCurve(50);

  r.numBidLevels = static_cast<uint32_t>(bids.size());
  r.numAskLevels = static_cast<uint32_t>(asks.size());
  for (const auto& b : bids) r.bidLevels.push_back({b.price, b.depth});
  for (const auto& a : asks) r.askLevels.push_back({a.price, a.depth});

  return r;
}

void OrderbookMempool::restoreFromReceipt(const OrderbookReceipt& receipt) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_lastClearingPrice = receipt.clearingPrice;
  // Individual orders are re-gossiped on reconnect.
  // The receipt provides the aggregate state for checkpointing.
}

void OrderbookMempool::clear() {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_bids.clear();
  m_asks.clear();
  m_orders.clear();
  m_poolOrders.clear();
  m_senderCounts.clear();
}

void OrderbookMempool::insertBid(const Order& order, bool isPool) {
  OrderPtr ptr{&order, isPool};
  // Pool orders at same price level go AFTER user orders
  auto& vec = m_bids[order.price];
  if (isPool) vec.push_back(ptr);
  else vec.insert(vec.begin(), ptr);
}

void OrderbookMempool::insertAsk(const Order& order, bool isPool) {
  OrderPtr ptr{&order, isPool};
  auto& vec = m_asks[order.price];
  if (isPool) vec.push_back(ptr);
  else vec.insert(vec.begin(), ptr);
}

void OrderbookMempool::removeFromIndex(const Order& order) {
  auto removeFrom = [&](auto& curve) {
    auto it = curve.find(order.price);
    if (it != curve.end()) {
      auto& vec = it->second;
      vec.erase(std::remove_if(vec.begin(), vec.end(),
        [&](const OrderPtr& p) {
          return memcmp(p.order->orderId.data, order.orderId.data, sizeof(order.orderId.data)) == 0;
        }), vec.end());
      if (vec.empty()) curve.erase(it);
    }
  };

  if (order.side == 0) removeFrom(m_bids);
  else removeFrom(m_asks);
}

} // namespace CryptoNote
