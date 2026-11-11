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
#include "AmmPool.h"
#include "crypto/hash.h"
#include "CryptoNoteTools.h"
#include "CryptoNoteConfig.h"
#include "Common/Int128.h"
#include "../Serialization/SerializationOverloads.h"

#include <algorithm>
#include <cstring>

namespace CryptoNote {

void Order::serialize(ISerializer& s) {
  KV_MEMBER(side)
  KV_MEMBER(amount)
  KV_MEMBER(price)
  KV_MEMBER(targetPrice)
  KV_MEMBER(expiration)
  KV_MEMBER(orderId)
  KV_MEMBER(utxoTxHash)
  KV_MEMBER(outputIndex)
  KV_MEMBER(addressHash)
  KV_MEMBER(partialSigs)
}

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

namespace {

const int POOL_TIERS_PER_SIDE = 10;
const uint64_t POOL_MIN_TIER_XFG = parameters::COIN / 100;

Crypto::Hash makePoolOrderId(const PoolOrderParams& params, int tier, uint8_t side, uint64_t price) {
  uint8_t seed[56] = {0};
  for (int i = 0; i < 8; i++) {
    seed[i]      = static_cast<uint8_t>((params.P_clear     >> (8*i)) & 0xFF);
    seed[8 + i]  = static_cast<uint8_t>((params.reserveXfg  >> (8*i)) & 0xFF);
    seed[16 + i] = static_cast<uint8_t>((params.reserveHeat >> (8*i)) & 0xFF);
    seed[24 + i] = static_cast<uint8_t>((price              >> (8*i)) & 0xFF);
    seed[32 + i] = static_cast<uint8_t>((uint64_t(tier)     >> (8*i)) & 0xFF);
    seed[40 + i] = static_cast<uint8_t>((uint64_t(side)     >> (8*i)) & 0xFF);
  }
  Crypto::Hash id;
  Crypto::cn_fast_hash(seed, sizeof(seed), id);
  id.data[0] = 0xF0;
  return id;
}

Order makePoolOrder(const PoolOrderParams& params, int tier, uint8_t side, uint64_t amount, uint64_t price) {
  Order o;
  o.side = side;
  o.amount = amount;
  o.price = price;
  o.targetPrice = price;
  o.expiration = 0;
  o.outputIndex = 0;
  o.utxoTxHash = Crypto::Hash{};
  o.addressHash = Crypto::Hash{};
  o.orderId = makePoolOrderId(params, tier, side, price);
  return o;
}

uint64_t halfSpreadOf(uint64_t price, uint64_t spreadBps) {
  const uint64_t half = static_cast<uint64_t>(((uint128_t)price * spreadBps) / 20000);
  return half == 0 ? 1 : half;
}

uint64_t tierSize(uint64_t depth, uint64_t remaining) {
  uint64_t chunk = depth / POOL_TIERS_PER_SIDE;
  if (chunk < POOL_MIN_TIER_XFG) chunk = POOL_MIN_TIER_XFG;
  return std::min(chunk, remaining);
}

// Each ask tier is priced at what the constant-product curve charges for that
// tier once the cheaper tiers are gone, rounded up, plus half the spread. A
// fill of any part of it therefore never pays the pool less than a swap of
// the same size would, so the product of the reserves cannot fall.
void appendPoolAsks(const PoolOrderParams& params, std::vector<Order>& orders) {
  uint64_t curveXfg = params.reserveXfg;
  uint64_t curveHeat = params.reserveHeat;
  const uint64_t depth = static_cast<uint64_t>(((uint128_t)params.reserveXfg * params.bandPct) / 100);
  uint64_t remaining = depth;
  for (int tier = 0; tier < POOL_TIERS_PER_SIDE && remaining > 0; tier++) {
    const uint64_t amount = tierSize(depth, remaining);
    const uint64_t heatCost = ammCostToTake(amount, curveHeat, curveXfg);
    if (heatCost == 0 || curveHeat > UINT64_MAX - heatCost) break;
    const uint128_t curvePrice = ((uint128_t)heatCost * parameters::COIN + amount - 1) / amount;
    if (curvePrice > UINT64_MAX / 2) break;
    const uint64_t price = static_cast<uint64_t>(curvePrice) +
                           halfSpreadOf(static_cast<uint64_t>(curvePrice), params.spreadBps);
    orders.push_back(makePoolOrder(params, tier, 1, amount, price));
    remaining -= amount;
    curveXfg -= amount;
    curveHeat += heatCost;
  }
}

// Each bid tier is priced at what the curve pays for that tier once the
// dearer tiers are filled, rounded down, less half the spread.
void appendPoolBids(const PoolOrderParams& params, std::vector<Order>& orders) {
  uint64_t curveXfg = params.reserveXfg;
  uint64_t curveHeat = params.reserveHeat;
  const uint64_t heatBudget = static_cast<uint64_t>(((uint128_t)params.reserveHeat * params.bandPct) / 100);
  if (heatBudget == 0 || heatBudget >= params.reserveHeat) return;
  const uint128_t depthWide = ((uint128_t)params.reserveXfg * heatBudget) / (params.reserveHeat - heatBudget);
  if (depthWide > UINT64_MAX - params.reserveXfg) return;
  const uint64_t depth = static_cast<uint64_t>(depthWide);
  uint64_t remaining = depth;
  for (int tier = 0; tier < POOL_TIERS_PER_SIDE && remaining > 0; tier++) {
    const uint64_t amount = tierSize(depth, remaining);
    const uint64_t heatPaid = ammGetOutputAmount(amount, curveXfg, curveHeat, 0);
    const uint64_t curvePrice = static_cast<uint64_t>(((uint128_t)heatPaid * parameters::COIN) / amount);
    const uint64_t halfSpread = halfSpreadOf(curvePrice, params.spreadBps);
    if (heatPaid == 0 || curvePrice <= halfSpread) break;
    orders.push_back(makePoolOrder(params, POOL_TIERS_PER_SIDE + tier, 0, amount, curvePrice - halfSpread));
    remaining -= amount;
    curveXfg += amount;
    curveHeat -= heatPaid;
  }
}

} // anonymous namespace

std::vector<Order> generatePoolOrders(const PoolOrderParams& params) {
  std::vector<Order> orders;
  if (params.reserveXfg == 0 || params.reserveHeat == 0)
    return orders;
  appendPoolAsks(params, orders);
  appendPoolBids(params, orders);
  return orders;
}

} // namespace CryptoNote
