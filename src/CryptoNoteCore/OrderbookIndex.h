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

#include <map>
#include <unordered_map>
#include <vector>

#include <boost/functional/hash.hpp>

#include "../../include/CryptoNote.h"

namespace CryptoNote {

struct HashLess {
  bool operator()(const Crypto::Hash& a, const Crypto::Hash& b) const {
    return memcmp(a.data, b.data, sizeof(a.data)) < 0;
  }
};

struct HashHasher {
  size_t operator()(const Crypto::Hash& h) const {
    return boost::hash_range(h.data, h.data + sizeof(h.data));
  }
};

struct HashEqual {
  bool operator()(const Crypto::Hash& a, const Crypto::Hash& b) const {
    return memcmp(a.data, b.data, sizeof(a.data)) == 0;
  }
};

struct OrderEntry {
  Crypto::Hash orderId;
  uint8_t  side;         // 0 = BUY_XFG, 1 = SELL_XFG
  uint64_t price;        // XFG/HEAT ratio × 10^8
  uint64_t amount;       // atomic units
  uint32_t expiration;   // block height
  Crypto::PublicKey spendKey;
  Crypto::PublicKey viewKey;
  uint32_t blockHeight;  // block when order was placed (tiebreaker for time priority)
};

class OrderbookIndex {
public:
  OrderbookIndex() {}
  explicit OrderbookIndex(uint32_t maxOrdersPerBlock, uint32_t maxOrdersPerSender)
    : m_maxOrdersPerBlock(maxOrdersPerBlock), m_maxOrdersPerSender(maxOrdersPerSender) {}

  struct SenderKey {
    Crypto::PublicKey spendKey;
    Crypto::PublicKey viewKey;

    bool operator<(const SenderKey& other) const {
      int cmp = memcmp(spendKey.data, other.spendKey.data, sizeof(spendKey.data));
      if (cmp != 0) return cmp < 0;
      return memcmp(viewKey.data, other.viewKey.data, sizeof(viewKey.data)) < 0;
    }

    bool operator==(const SenderKey& other) const {
      return memcmp(spendKey.data, other.spendKey.data, sizeof(spendKey.data)) == 0 &&
             memcmp(viewKey.data, other.viewKey.data, sizeof(viewKey.data)) == 0;
    }
  };

  struct SenderKeyHash {
    size_t operator()(const SenderKey& k) const {
      size_t h1 = boost::hash_range(k.spendKey.data, k.spendKey.data + sizeof(k.spendKey.data));
      size_t h2 = boost::hash_range(k.viewKey.data, k.viewKey.data + sizeof(k.viewKey.data));
      return h1 ^ (h2 << 1);
    }
  };

  void addOrder(const OrderEntry& entry);
  void removeOrder(const Crypto::Hash& orderId);

  const std::map<uint64_t, std::vector<OrderEntry>, std::greater<uint64_t>>& getBidCurve() const { return m_bidCurve; }
  const std::map<uint64_t, std::vector<OrderEntry>>& getAskCurve() const { return m_askCurve; }

  uint32_t getSenderOpenOrderCount(const SenderKey& sender) const;
  size_t getTotalOpenOrders() const { return m_bidCount + m_askCount; }
  uint32_t getMaxOrdersPerBlock() const { return m_maxOrdersPerBlock; }

  bool canPlaceOrder(const SenderKey& sender) const;

  void clear();

private:
  std::map<uint64_t, std::vector<OrderEntry>, std::greater<uint64_t>> m_bidCurve;
  std::map<uint64_t, std::vector<OrderEntry>> m_askCurve;
  std::unordered_map<SenderKey, uint32_t, SenderKeyHash> m_perSenderCount;
  std::map<Crypto::Hash, uint64_t, HashLess> m_orderIdToPrice;
  std::map<Crypto::Hash, uint8_t, HashLess> m_orderIdToSide;

  size_t m_bidCount = 0;
  size_t m_askCount = 0;

  uint32_t m_maxOrdersPerBlock = 1000;
  uint32_t m_maxOrdersPerSender = 50;
};

} // namespace CryptoNote
