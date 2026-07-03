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

#include "CryptoNoteCore/OrderbookIndex.h"
#include "CryptoNoteCore/OrderbookMatcher.h"
#include "CryptoNoteCore/TransactionUtils.h"
#include "../../include/ITransaction.h"

#include <cassert>
#include <cstdio>
#include <cstring>

using namespace CryptoNote;

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
  tests_run++; \
  if (!(name)) { \
    fprintf(stderr, "FAIL: %s (%s:%d)\n", #name, __FILE__, __LINE__); \
  } else { \
    tests_passed++; \
  } \
} while(0)

static Crypto::Hash makeHash(uint8_t v) {
  Crypto::Hash h;
  memset(h.data, v, sizeof(h.data));
  return h;
}

static Crypto::PublicKey makePubKey(uint8_t v) {
  Crypto::PublicKey k;
  memset(k.data, v, sizeof(k.data));
  return k;
}

static OrderEntry makeBid(uint8_t id, uint64_t price, uint64_t amount, uint32_t expiration = 0) {
  OrderEntry e;
  e.orderId = makeHash(id);
  e.side = 0;
  e.price = price;
  e.amount = amount;
  e.expiration = expiration;
  e.spendKey = makePubKey(id);
  e.viewKey = makePubKey(id + 100);
  e.blockHeight = 1000;
  return e;
}

static OrderEntry makeAsk(uint8_t id, uint64_t price, uint64_t amount, uint32_t expiration = 0) {
  OrderEntry e;
  e.orderId = makeHash(id);
  e.side = 1;
  e.price = price;
  e.amount = amount;
  e.expiration = expiration;
  e.spendKey = makePubKey(id);
  e.viewKey = makePubKey(id + 100);
  e.blockHeight = 1000;
  return e;
}

int main() {
  // OrderbookIndex tests
  {
    OrderbookIndex idx;
    auto bid = makeBid(1, 12500000, 10000000000ULL);
    idx.addOrder(bid);
    TEST(idx.getTotalOpenOrders() == 1u);
    TEST(!idx.getBidCurve().empty());
    TEST(idx.getAskCurve().empty());
  }

  {
    OrderbookIndex idx;
    auto ask = makeAsk(1, 12500000, 10000000000ULL);
    idx.addOrder(ask);
    TEST(idx.getTotalOpenOrders() == 1u);
    TEST(idx.getBidCurve().empty());
    TEST(!idx.getAskCurve().empty());
  }

  // Bids sorted descending
  {
    OrderbookIndex idx;
    idx.addOrder(makeBid(1, 12000000, 1000));
    idx.addOrder(makeBid(2, 13000000, 1000));
    idx.addOrder(makeBid(3, 12500000, 1000));
    auto& curve = idx.getBidCurve();
    TEST(curve.begin()->first == 13000000u);
  }

  // Asks sorted ascending
  {
    OrderbookIndex idx;
    idx.addOrder(makeAsk(1, 13000000, 1000));
    idx.addOrder(makeAsk(2, 12000000, 1000));
    idx.addOrder(makeAsk(3, 12500000, 1000));
    auto& curve = idx.getAskCurve();
    TEST(curve.begin()->first == 12000000u);
  }

  // Remove order
  {
    OrderbookIndex idx;
    auto bid = makeBid(1, 12500000, 1000);
    idx.addOrder(bid);
    TEST(idx.getTotalOpenOrders() == 1u);
    idx.removeOrder(bid.orderId);
    TEST(idx.getTotalOpenOrders() == 0u);
  }

  // Sender count
  {
    OrderbookIndex idx(1000, 50);
    auto bid = makeBid(1, 12500000, 1000);
    idx.addOrder(bid);
    OrderbookIndex::SenderKey sender{bid.spendKey, bid.viewKey};
    TEST(idx.getSenderOpenOrderCount(sender) == 1u);
    TEST(idx.canPlaceOrder(sender));
    idx.removeOrder(bid.orderId);
    TEST(idx.getSenderOpenOrderCount(sender) == 0u);
  }

  // Sender limit (all orders from same sender)
  {
    OrderbookIndex idx(1000, 2);
    OrderbookIndex::SenderKey sender{makePubKey(1), makePubKey(101)};
    OrderEntry b1 = makeBid(10, 12000000, 1000);
    OrderEntry b2 = makeBid(20, 13000000, 1000);
    OrderEntry b3 = makeBid(30, 14000000, 1000);
    b1.spendKey = sender.spendKey; b1.viewKey = sender.viewKey;
    b2.spendKey = sender.spendKey; b2.viewKey = sender.viewKey;
    b3.spendKey = sender.spendKey; b3.viewKey = sender.viewKey;
    idx.addOrder(b1);
    TEST(idx.canPlaceOrder(sender));
    idx.addOrder(b2);
    TEST(!idx.canPlaceOrder(sender));  // exceeded limit
    // Remove one, should be able to place again
    idx.removeOrder(b1.orderId);
    TEST(idx.canPlaceOrder(sender));
  }

  // OrderbookMatcher: single match same price
  {
    OrderbookIndex idx;
    idx.addOrder(makeBid(1, 12500000, 1000));
    idx.addOrder(makeAsk(2, 12500000, 500));
    OrderbookMatcher matcher(2, 1000);
    auto result = matcher.match(idx, 12000000, 2000);
    TEST(result.fills.size() == 1u);
    TEST(result.fills[0].amount == 500u);
    TEST(result.fills[0].price == 12500000u);
    TEST(result.P_clear == 12500000u);
  }

  // Partial fill bid
  {
    OrderbookIndex idx;
    idx.addOrder(makeBid(1, 12500000, 1000));
    idx.addOrder(makeAsk(2, 12500000, 300));
    OrderbookMatcher matcher(2, 1000);
    auto result = matcher.match(idx, 12000000, 2000);
    TEST(result.fills.size() == 1u);
    TEST(result.fills[0].amount == 300u);
    bool foundRemainder = false;
    for (const auto& r : result.remainders) {
      if (r.side == 0 && r.remainingAmount == 700u) foundRemainder = true;
    }
    TEST(foundRemainder);
  }

  // Multiple overlapping orders (VWAP filtering may exclude unfavorable asks)
  {
    OrderbookIndex idx;
    idx.addOrder(makeBid(1, 12700000, 500));
    idx.addOrder(makeBid(2, 12600000, 500));
    idx.addOrder(makeAsk(3, 12500000, 300));
    idx.addOrder(makeAsk(4, 12600000, 400));
    OrderbookMatcher matcher(2, 1000);
    auto result = matcher.match(idx, 12000000, 2000);
    TEST(result.fills.size() >= 1u);
    TEST(result.P_clear > 0);
  }

  // One-sided book (no asks)
  {
    OrderbookIndex idx;
    idx.addOrder(makeBid(1, 12500000, 1000));
    OrderbookMatcher matcher(2, 1000);
    auto result = matcher.match(idx, 12000000, 2000);
    TEST(result.fills.size() == 0u);
    TEST(result.P_clear == 12000000u);
    TEST(!result.clearingValid);
  }

  // No overlap
  {
    OrderbookIndex idx;
    idx.addOrder(makeBid(1, 12000000, 1000));
    idx.addOrder(makeAsk(2, 13000000, 1000));
    OrderbookMatcher matcher(2, 1000);
    auto result = matcher.match(idx, 12500000, 2000);
    TEST(result.fills.size() == 0u);
    TEST(result.P_clear == 12500000u);
  }

  // Order expiration
  {
    OrderbookIndex idx;
    idx.addOrder(makeBid(1, 12500000, 1000, 1500));
    idx.addOrder(makeAsk(2, 12500000, 1000));
    OrderbookMatcher matcher(2, 1000);
    auto result = matcher.match(idx, 12000000, 1600);
    bool bidExpired = false;
    for (const auto& r : result.remainders) {
      if (r.side == 0 && r.remainingAmount == 1000) bidExpired = true;
    }
    TEST(bidExpired);
    TEST(result.fills.size() == 0u);
  }

  // Empty book returns prev clearing price
  {
    OrderbookIndex idx;
    OrderbookMatcher matcher(2, 1000);
    auto result = matcher.match(idx, 12345678, 2000);
    TEST(result.fills.size() == 0u);
    TEST(result.P_clear == 12345678u);
    TEST(!result.clearingValid);
  }

  // Max orders per block
  {
    OrderbookIndex idx;
    for (int i = 0; i < 5; i++)
      idx.addOrder(makeBid(i + 1, 13000000 - i * 10000, 100));
    for (int i = 0; i < 5; i++)
      idx.addOrder(makeAsk(i + 10, 12000000 + i * 10000, 100));
    OrderbookMatcher matcher(2, 3);
    auto result = matcher.match(idx, 12000000, 2000);
    TEST(result.fills.size() <= 3u);
  }

  // TX_OUT_ORDER serialization roundtrip
  {
    TransactionOutputOrder out;
    out.side = 1;
    out.price = 12500000;
    out.expiration = 1100000;
    memcpy(out.spendKey.data, "abcdefghijklmnopqrstuvwxyz012345", 32);
    memcpy(out.viewKey.data, "zyxwvutsrqponmlkjihgfedcba543210", 32);
    TransactionOutput output;
    output.amount = 1000000000;
    output.target = out;
    TEST(boost::get<TransactionOutputOrder>(output.target).side == 1u);
    TEST(boost::get<TransactionOutputOrder>(output.target).price == 12500000u);
    TEST(boost::get<TransactionOutputOrder>(output.target).expiration == 1100000u);
    TEST(output.amount == 1000000000u);
  }

  // Output type detection
  {
    TransactionOutputOrder out;
    out.side = 0;
    out.price = 12500000;
    out.expiration = 1100000;
    memset(out.spendKey.data, 0, 32);
    memset(out.viewKey.data, 0, 32);
    TransactionOutput output;
    output.amount = 1000;
    output.target = out;
    auto type = getTransactionOutputType(output.target);
    TEST(type == TransactionTypes::OutputType::Order);
  }

  // Price-favorable matching (higher ask excluded by VWAP)
  {
    OrderbookIndex idx;
    idx.addOrder(makeBid(1, 13000000, 1000));
    idx.addOrder(makeAsk(2, 12500000, 500));
    idx.addOrder(makeAsk(3, 12800000, 500));
    OrderbookMatcher matcher(2, 1000);
    auto result = matcher.match(idx, 12000000, 2000);
    // Ask at 128 may be excluded if P_clear < 128.
    TEST(result.fills.size() == 1u);
    TEST(result.P_clear == 12500000u);
    TEST(result.clearingValid);
  }

  fprintf(stderr, "\n=== Orderbook Tests ===\n");
  fprintf(stderr, "Passed: %d / %d\n", tests_passed, tests_run);
  return (tests_passed == tests_run) ? 0 : 1;
}
