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
#include <vector>

#include "../../include/CryptoNote.h"
#include "OrderbookIndex.h"

namespace CryptoNote {

struct FillRecord {
  Crypto::Hash bidOrderId;
  Crypto::Hash askOrderId;
  uint64_t amount;      // XFG amount matched (atomic units)
  uint64_t price;       // match price (the ask price)
};

struct RemainderRecord {
  Crypto::Hash orderId;
  uint64_t remainingAmount;    // atomic units
  Crypto::PublicKey spendKey;
  Crypto::PublicKey viewKey;
  uint64_t price;
  uint8_t side;
  uint32_t expiration;
};

struct MatchResult {
  std::vector<FillRecord> fills;
  std::vector<RemainderRecord> remainders;
  uint64_t P_clear = 0;
  uint32_t numMatches = 0;
  uint32_t numDistinctParties = 0;
  bool clearingValid = false;
};

class OrderbookMatcher {
public:
  OrderbookMatcher(uint32_t minDistinctParties, uint32_t maxOrdersPerBlock)
    : m_minDistinctParties(minDistinctParties), m_maxOrdersPerBlock(maxOrdersPerBlock) {}

  MatchResult match(OrderbookIndex& index, uint64_t prevPclear, uint32_t currentBlockHeight);

private:
  uint32_t m_minDistinctParties;
  uint32_t m_maxOrdersPerBlock;

  void expireOrders(OrderbookIndex& index, uint32_t currentBlockHeight,
                    std::vector<RemainderRecord>& remainders);
  uint32_t countDistinctParties(const std::vector<FillRecord>& fills,
                                const OrderbookIndex& index);
};

} // namespace CryptoNote
