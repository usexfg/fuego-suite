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

#pragma once

#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>
#include <mutex>

#include "../../include/CryptoNote.h"
#include "OrderbookIndex.h"
#include "OrderbookTypes.h"

namespace CryptoNote {

class OrderbookMempool {
public:
  OrderbookMempool(uint32_t maxOrdersPerSender = 100, uint32_t maxOrdersTotal = 10000);

  bool addOrder(const Order& order);
  bool cancelOrder(const Crypto::Hash& orderId);
  bool hasOrder(const Crypto::Hash& orderId) const;
  const Order* getOrder(const Crypto::Hash& orderId) const;

  void setPoolOrders(const std::vector<Order>& orders);
  void clearPoolOrders();

  struct AggregatedLevel {
    uint64_t price;
    uint64_t depth;
    uint32_t orderCount;
  };

  std::vector<AggregatedLevel> getBidCurve(uint32_t maxLevels = 50) const;
  std::vector<AggregatedLevel> getAskCurve(uint32_t maxLevels = 50) const;

  void copyToIndex(OrderbookIndex& idx) const;

  struct OutOfBandOrder {
    Crypto::Hash orderId;
    uint8_t  side;
    uint64_t amount;
    uint64_t targetPrice;
  };

  void fillOrder(const Crypto::Hash& orderId, uint64_t fillAmount);
  std::vector<OutOfBandOrder> getOutOfBandOrders() const;

  size_t totalOrders() const { return m_orders.size(); }
  size_t poolOrders() const { return m_poolOrders.size(); }
  std::vector<Order> getAllPoolOrders() const;
  std::vector<Order> getAllUserOrders() const;

  uint32_t expireOrders(uint32_t currentHeight);

  OrderbookReceipt generateReceipt() const;

  void restoreFromReceipt(const OrderbookReceipt& receipt);

  void clear();

private:
  struct OrderPtr {
    const Order* order;
    bool isPool;
  };

  std::map<uint64_t, std::vector<OrderPtr>, std::greater<uint64_t>> m_bids;
  std::map<uint64_t, std::vector<OrderPtr>> m_asks;
  std::unordered_map<Crypto::Hash, Order, HashHasher, HashEqual> m_orders;
  std::unordered_map<Crypto::Hash, Order, HashHasher, HashEqual> m_poolOrders;
  std::map<SenderKey, uint32_t> m_senderCounts;

  uint32_t m_maxOrdersPerSender;
  uint32_t m_maxOrdersTotal;

  uint64_t m_lastClearingPrice = 0;

  mutable std::mutex m_mutex;

  void insertBid(const Order& order, bool isPool);
  void insertAsk(const Order& order, bool isPool);
  void removeFromIndex(const Order& order);

  std::vector<AggregatedLevel> getBidCurveLocked(uint32_t maxLevels) const;
  std::vector<AggregatedLevel> getAskCurveLocked(uint32_t maxLevels) const;
};

} // namespace CryptoNote
