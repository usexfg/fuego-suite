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

  uint64_t sellDepth = (static_cast<uint128_t>(params.reserveXfg) * params.bandPct) / 100;
  uint64_t buyDepth  = (static_cast<uint128_t>(params.reserveHeat) * params.bandPct) / 100;
  // Convert HEAT depth to XFG-equivalent for bid side
  uint64_t buyDepthXfg = (static_cast<uint128_t>(buyDepth) * parameters::COIN) / params.P_clear;

  uint64_t spread = (static_cast<uint128_t>(params.P_clear) * params.spreadBps) / 10000;
  if (spread == 0) spread = 1;

  // Generate pool sell orders (asks — sell XFG, receive HEAT)
  // Tiered: larger chunks near P_clear, smaller at extremes
  uint64_t remainingSell = sellDepth;
  uint64_t sellPrice = params.P_clear + spread / 2;
  uint64_t chunkSize = sellDepth / 10;
  if (chunkSize < parameters::COIN / 100) chunkSize = parameters::COIN / 100; // min 0.01 XFG

  for (int tier = 0; tier < 10 && remainingSell > 0; tier++) {
    uint64_t tierAmount = std::min(chunkSize, remainingSell);
    if (tierAmount == 0) break;

    Order o;
    o.side = 1;  // sell XFG
    o.amount = tierAmount;
    o.price = sellPrice;
    o.expiration = 0; // pool orders auto-refresh, no expiry
    o.spendKey = Crypto::PublicKey{};
    o.viewKey = Crypto::PublicKey{};
    // Pool orders use a synthetic order ID based on the tier
    memset(o.orderId.data, 0, sizeof(o.orderId.data));
    o.orderId.data[0] = 0xF0;  // marker for pool-generated
    o.orderId.data[1] = static_cast<uint8_t>(tier);
    o.orderId.data[2] = 0; // ask side
    // Deterministic pool order ID from tier + price
    o.orderId.data[0] = 0xF0;
    o.orderId.data[1] = static_cast<uint8_t>(tier);
    o.orderId.data[2] = 0; // ask side
    for (int i = 3; i < 10; i++) o.orderId.data[i] = static_cast<uint8_t>((sellPrice >> (8*(i-3))) & 0xFF);

    orders.push_back(o);
    remainingSell -= tierAmount;
    sellPrice += spread / 10;
  }

  // Generate pool buy orders (bids — buy XFG, pay HEAT)
  uint64_t remainingBuy = buyDepthXfg;
  uint64_t bidPrice = params.P_clear - spread / 2;
  chunkSize = buyDepthXfg / 10;
  if (chunkSize < parameters::COIN / 100) chunkSize = parameters::COIN / 100;

  for (int tier = 0; tier < 10 && remainingBuy > 0; tier++) {
    uint64_t tierAmount = std::min(chunkSize, remainingBuy);
    if (tierAmount == 0 || bidPrice == 0) break;

    Order o;
    o.side = 0;  // buy XFG
    o.amount = tierAmount;
    o.price = bidPrice;
    o.expiration = 0;
    o.spendKey = Crypto::PublicKey{};
    o.viewKey = Crypto::PublicKey{};
    memset(o.orderId.data, 0, sizeof(o.orderId.data));
    o.orderId.data[0] = 0xF0;
    o.orderId.data[1] = static_cast<uint8_t>(10 + tier);
    o.orderId.data[2] = 1; // bid side
    for (int i = 3; i < 10; i++) o.orderId.data[i] = static_cast<uint8_t>((bidPrice >> (8*(i-3))) & 0xFF);

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
