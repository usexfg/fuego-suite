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

  // Pool order management (treasury/HEARTH)
  void setPoolOrders(const std::vector<Order>& orders);
  void clearPoolOrders();

  // Matching support
  struct SenderKey {
    Crypto::PublicKey spendKey;
    Crypto::PublicKey viewKey;
    bool operator<(const SenderKey& o) const {
      int c = memcmp(spendKey.data, o.spendKey.data, sizeof(spendKey.data));
      if (c) return c < 0;
      return memcmp(viewKey.data, o.viewKey.data, sizeof(viewKey.data)) < 0;
    }
    bool operator==(const SenderKey& o) const {
      return memcmp(spendKey.data, o.spendKey.data, sizeof(spendKey.data)) == 0 &&
             memcmp(viewKey.data, o.viewKey.data, sizeof(viewKey.data)) == 0;
    }
  };

  // Curves for matching (bids = buy XFG, asks = sell XFG)
  struct AggregatedLevel {
    uint64_t price;
    uint64_t depth;
    uint32_t orderCount;
  };

  std::vector<AggregatedLevel> getBidCurve(uint32_t maxLevels = 50) const;
  std::vector<AggregatedLevel> getAskCurve(uint32_t maxLevels = 50) const;

  size_t totalOrders() const { return m_orders.size(); }
  size_t poolOrders() const { return m_poolOrders.size(); }

  // Expiry: remove orders where expiration <= currentHeight. Returns count removed.
  uint32_t expireOrders(uint32_t currentHeight);

  // Persistence: produce a receipt for embedding in settlement blocks
  OrderbookReceipt generateReceipt() const;

  // Restore from receipt (on chain restart)
  void restoreFromReceipt(const OrderbookReceipt& receipt);

  void clear();

private:
  // Bid = buy XFG with HEAT. Sorted descending by price, then ascending by time.
  struct OrderPtr {
    const Order* order;
    bool isPool;  // pool orders fill last at same price
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
};

} // namespace CryptoNote
