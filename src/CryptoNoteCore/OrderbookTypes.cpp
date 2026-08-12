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

#include "OrderbookTypes.h"
#include "crypto/hash.h"
#include "CryptoNoteTools.h"
#include "CryptoNoteConfig.h"
#include "Common/Int128.h"
#include "../Serialization/SerializationOverloads.h"

#include <cstring>

namespace CryptoNote {

void OrderbookReceipt::Level::serialize(ISerializer& s) {
  KV_MEMBER(price)
  KV_MEMBER(depth)
}

void OrderbookReceipt::serialize(ISerializer& s) {
  KV_MEMBER(clearingPrice)
  KV_MEMBER(numBidLevels)
  KV_MEMBER(numAskLevels)
  KV_MEMBER(bidLevels)
  KV_MEMBER(askLevels)
}

std::vector<Order> generatePoolOrders(const PoolOrderParams& params) {
  std::vector<Order> orders;
  if (params.reserveXfg == 0 || params.reserveHeat == 0 || params.P_clear == 0)
    return orders;

  uint64_t sellDepth = static_cast<uint64_t>((static_cast<uint128_t>(params.reserveXfg) * params.bandPct) / 100);
  uint64_t buyDepth  = static_cast<uint64_t>((static_cast<uint128_t>(params.reserveHeat) * params.bandPct) / 100);
  uint64_t buyDepthXfg = static_cast<uint64_t>((static_cast<uint128_t>(buyDepth) * parameters::COIN) / params.P_clear);

  uint64_t spread = static_cast<uint64_t>((static_cast<uint128_t>(params.P_clear) * params.spreadBps) / 10000);
  if (spread == 0) spread = 1;

  auto makePoolId = [&](int tier, uint8_t side, uint64_t price) -> Crypto::Hash {
    // Deterministic but unique ID: hash of (P_clear || reserveXfg || reserveHeat || tier || side || price)
    // Prefix 0xF0 marks pool-generated; aggressive collision rejection in addOrder().
    uint8_t seed[56];
    for (int i = 0; i < 8; i++) {
      seed[i]      = static_cast<uint8_t>((params.P_clear      >> (8*i)) & 0xFF);
      seed[8 + i]  = static_cast<uint8_t>((params.reserveXfg   >> (8*i)) & 0xFF);
      seed[16 + i] = static_cast<uint8_t>((params.reserveHeat  >> (8*i)) & 0xFF);
      seed[24 + i]  = static_cast<uint8_t>((price              >> (8*i)) & 0xFF);
      seed[32 + i]  = static_cast<uint8_t>((uint64_t(tier)     >> (8*i)) & 0xFF);
      seed[40 + i]  = static_cast<uint8_t>((uint64_t(side)     >> (8*i)) & 0xFF);
    }
    Crypto::Hash id;
    Crypto::cn_fast_hash(seed, sizeof(seed), id);
    id.data[0] = 0xF0;
    return id;
  };

  uint64_t remainingSell = sellDepth;
  uint64_t sellPrice = params.P_clear + spread / 2;
  uint64_t chunkSize = sellDepth / 10;
  if (chunkSize < parameters::COIN / 100) chunkSize = parameters::COIN / 100;

  for (int tier = 0; tier < 10 && remainingSell > 0; tier++) {
    uint64_t tierAmount = std::min(chunkSize, remainingSell);
    if (tierAmount == 0) break;

    Order o;
    o.side = 1;
    o.amount = tierAmount;
    o.price = sellPrice;
    o.expiration = 0;
    o.addressHash = Crypto::Hash{};
    o.orderId = makePoolId(tier, 1, sellPrice);

    orders.push_back(o);
    remainingSell -= tierAmount;
    sellPrice += spread / 10;
  }

  uint64_t remainingBuy = buyDepthXfg;
  uint64_t bidPrice = params.P_clear - spread / 2;
  chunkSize = buyDepthXfg / 10;
  if (chunkSize < parameters::COIN / 100) chunkSize = parameters::COIN / 100;

  for (int tier = 0; tier < 10 && remainingBuy > 0; tier++) {
    uint64_t tierAmount = std::min(chunkSize, remainingBuy);
    if (tierAmount == 0 || bidPrice == 0) break;

    Order o;
    o.side = 0;
    o.amount = tierAmount;
    o.price = bidPrice;
    o.expiration = 0;
    o.addressHash = Crypto::Hash{};
    o.orderId = makePoolId(10 + tier, 0, bidPrice);

    orders.push_back(o);
    remainingBuy -= tierAmount;

    if (bidPrice > spread / 10)
      bidPrice -= spread / 10;
    else
      bidPrice = 1;
  }

  return orders;
}

} // namespace CryptoNote
