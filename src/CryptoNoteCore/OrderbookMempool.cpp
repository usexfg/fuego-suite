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
  if (order.orderId.data[0] == 0xF0) return false; // reserved for pool-generated orders

  SenderKey sk{order.addressHash};
  auto it = m_senderCounts.find(sk);
  if (it != m_senderCounts.end() && it->second >= m_maxOrdersPerSender)
    return false;

  Order& stored = m_orders[order.orderId];
  stored = order;
  m_senderCounts[sk]++;

  if (stored.side == 0) insertBid(stored, false);
  else insertAsk(stored, false);

  return true;
}

bool OrderbookMempool::cancelOrder(const Crypto::Hash& orderId) {
  std::lock_guard<std::mutex> lock(m_mutex);

  auto it = m_orders.find(orderId);
  if (it == m_orders.end()) return false;

  removeFromIndex(it->second);
  SenderKey sk{it->second.addressHash};
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
    Order& stored = m_poolOrders[o.orderId];
    stored = o;
    if (stored.side == 0) insertBid(stored, true);
    else insertAsk(stored, true);
  }
}

void OrderbookMempool::clearPoolOrders() {
  std::lock_guard<std::mutex> lock(m_mutex);

  for (const auto& o : m_poolOrders) removeFromIndex(o.second);
  m_poolOrders.clear();
}

std::vector<OrderbookMempool::AggregatedLevel> OrderbookMempool::getBidCurve(uint32_t maxLevels) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return getBidCurveLocked(maxLevels);
}

std::vector<OrderbookMempool::AggregatedLevel> OrderbookMempool::getAskCurve(uint32_t maxLevels) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return getAskCurveLocked(maxLevels);
}

std::vector<OrderbookMempool::AggregatedLevel> OrderbookMempool::getBidCurveLocked(uint32_t maxLevels) const {
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

std::vector<OrderbookMempool::AggregatedLevel> OrderbookMempool::getAskCurveLocked(uint32_t maxLevels) const {
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
      SenderKey sk{it->second.addressHash};
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
  auto bids = getBidCurveLocked(50);
  auto asks = getAskCurveLocked(50);

  r.numBidLevels = static_cast<uint32_t>(bids.size());
  r.numAskLevels = static_cast<uint32_t>(asks.size());
  for (const auto& b : bids) r.bidLevels.push_back({b.price, b.depth});
  for (const auto& a : asks) r.askLevels.push_back({a.price, a.depth});

  return r;
}

void OrderbookMempool::restoreFromReceipt(const OrderbookReceipt& receipt) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (receipt.clearingPrice > 0)
    m_lastClearingPrice = receipt.clearingPrice;
}

void OrderbookMempool::clear() {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_bids.clear();
  m_asks.clear();
  m_orders.clear();
  m_poolOrders.clear();
  m_senderCounts.clear();
}

void OrderbookMempool::copyToIndex(OrderbookIndex& idx) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  for (const auto& [price, ptrs] : m_bids) {
    for (const auto& ptr : ptrs) {
      OrderEntry e;
      e.orderId     = ptr.order->orderId;
      e.side        = ptr.order->side;
      e.price       = ptr.order->price;
      e.targetPrice = ptr.order->targetPrice;
      e.amount      = ptr.order->amount;
      e.expiration  = ptr.order->expiration;
      e.addressHash = ptr.order->addressHash;
      e.blockHeight = 0;
      idx.addOrder(e);
    }
  }
  for (const auto& [price, ptrs] : m_asks) {
    for (const auto& ptr : ptrs) {
      OrderEntry e;
      e.orderId     = ptr.order->orderId;
      e.side        = ptr.order->side;
      e.price       = ptr.order->price;
      e.targetPrice = ptr.order->targetPrice;
      e.amount      = ptr.order->amount;
      e.expiration  = ptr.order->expiration;
      e.addressHash = ptr.order->addressHash;
      e.blockHeight = 0;
      idx.addOrder(e);
    }
  }
  // Filter out-of-band orders from standard matching
  idx.removeOutOfBandOrders();
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

void OrderbookMempool::fillOrder(const Crypto::Hash& orderId, uint64_t fillAmount) {
  if (fillAmount == 0) return;
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_orders.find(orderId);
  if (it == m_orders.end()) return;

  Order& order = it->second;
  if (order.amount <= fillAmount) {
    removeFromIndex(order);
    SenderKey sk{order.addressHash};
    auto sc = m_senderCounts.find(sk);
    if (sc != m_senderCounts.end()) {
      if (sc->second <= 1) m_senderCounts.erase(sc);
      else sc->second--;
    }
    m_orders.erase(it);
  } else {
    order.amount -= fillAmount;
  }
}

std::vector<OrderbookMempool::OutOfBandOrder> OrderbookMempool::getOutOfBandOrders() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::vector<OutOfBandOrder> result;
  for (const auto& [id, order] : m_orders) {
    if (order.targetPrice != 0) {
      result.push_back({order.orderId, order.side, order.amount, order.targetPrice});
    }
  }
  return result;
}

std::vector<Order> OrderbookMempool::getAllPoolOrders() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::vector<Order> result;
  result.reserve(m_poolOrders.size());
  for (const auto& [id, order] : m_poolOrders)
    result.push_back(order);
  return result;
}

std::vector<Order> OrderbookMempool::getAllUserOrders() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::vector<Order> result;
  result.reserve(m_orders.size());
  for (const auto& [id, order] : m_orders)
    result.push_back(order);
  return result;
}

} // namespace CryptoNote
