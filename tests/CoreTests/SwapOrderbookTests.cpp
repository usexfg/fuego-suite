// Copyright (c) 2026 Fuego Developers
//
// Unit tests for P2P orderbook data structures (SwapOrder, PriceLevel,
// PairOrderBook, OrderBookSnapshot) and matching algorithm.
//
// These tests exercise the data structures directly without requiring
// the full SwapOfferRelay core/p2p dependencies.

#include "CryptoNoteCore/SwapOfferRelay.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <deque>

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

static Crypto::PublicKey makePubKey(uint8_t v) {
  Crypto::PublicKey k;
  memset(k.data, v, sizeof(k.data));
  return k;
}

static Crypto::Signature makeSig(uint8_t v) {
  Crypto::Signature s;
  memset(s.data, v, sizeof(s.data));
  return s;
}

static SwapOrder makeOrder(SwapOrder::Side side, uint8_t pair,
                           uint64_t price, uint64_t amount,
                           uint8_t idByte = 1) {
  SwapOrder o;
  o.orderId = "order_" + std::to_string(idByte);
  o.side = side;
  o.pair = pair;
  o.price = price;
  o.amount = amount;
  o.filled = 0;
  o.makerPubKey = makePubKey(idByte);
  o.signature = makeSig(idByte);
  o.nonce = idByte;
  o.timestamp = 1000000 + idByte;
  o.ttlBlocks = 1440;
  o.postedHeight = 100;
  return o;
}

// ── Standalone matching function (mirrors SwapOfferRelay::matchOrder logic) ──

struct MatchFill {
  std::string makerOrderId;
  uint64_t    fillPrice;
  uint64_t    fillAmount;
};

static std::vector<MatchFill> matchOrders(
    PairOrderBook& book,
    SwapOrder::Side takerSide, uint8_t pair,
    uint64_t takerPrice, uint64_t takerAmount) {

  std::vector<MatchFill> fills;
  auto& oppositeLadder = (takerSide == SwapOrder::Side::ASK)
                         ? book.bids : book.asks;

  uint64_t remaining = takerAmount;

  if (takerSide == SwapOrder::Side::ASK) {
    // Taker selling: match against bids (highest first)
    // Collect prices to process in descending order to avoid iterator invalidation
    std::vector<uint64_t> bidPrices;
    for (auto it = oppositeLadder.rbegin(); it != oppositeLadder.rend(); ++it) {
      if (it->first < takerPrice) break;
      bidPrices.push_back(it->first);
    }
    for (auto price : bidPrices) {
      if (remaining == 0) break;
      auto priceIt = oppositeLadder.find(price);
      if (priceIt == oppositeLadder.end()) continue;
      auto& q = priceIt->second;
      while (!q.orders.empty() && remaining > 0) {
        auto& maker = q.orders.front();
        uint64_t available = maker.amount - maker.filled;
        uint64_t fillAmt = std::min(remaining, available);
        fills.push_back({maker.orderId, price, fillAmt});
        remaining -= fillAmt;
        maker.filled += fillAmt;
        if (maker.filled >= maker.amount) {
          q.orders.pop_front();
        }
      }
      if (q.orders.empty()) {
        oppositeLadder.erase(priceIt);
      }
    }
  } else {
    // Taker buying: match against asks (lowest first)
    std::vector<uint64_t> askPrices;
    for (auto it = oppositeLadder.begin(); it != oppositeLadder.end(); ++it) {
      if (it->first > takerPrice) break;
      askPrices.push_back(it->first);
    }
    for (auto price : askPrices) {
      if (remaining == 0) break;
      auto priceIt = oppositeLadder.find(price);
      if (priceIt == oppositeLadder.end()) continue;
      auto& q = priceIt->second;
      while (!q.orders.empty() && remaining > 0) {
        auto& maker = q.orders.front();
        uint64_t available = maker.amount - maker.filled;
        uint64_t fillAmt = std::min(remaining, available);
        fills.push_back({maker.orderId, price, fillAmt});
        remaining -= fillAmt;
        maker.filled += fillAmt;
        if (maker.filled >= maker.amount) {
          q.orders.pop_front();
        }
      }
      if (q.orders.empty()) {
        oppositeLadder.erase(priceIt);
      }
    }
  }

  return fills;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Tests
// ═══════════════════════════════════════════════════════════════════════════════

int main() {

  // ── PriceLevel::totalDepth ──

  // Empty level
  {
    PriceLevel lvl;
    TEST(lvl.totalDepth() == 0);
  }

  // Single order
  {
    PriceLevel lvl;
    lvl.orders.push_back(makeOrder(SwapOrder::Side::ASK, 0, 15000000, 1000));
    TEST(lvl.totalDepth() == 1000);
  }

  // Multiple orders
  {
    PriceLevel lvl;
    lvl.orders.push_back(makeOrder(SwapOrder::Side::ASK, 0, 15000000, 1000, 1));
    lvl.orders.push_back(makeOrder(SwapOrder::Side::ASK, 0, 15000000, 2000, 2));
    lvl.orders.push_back(makeOrder(SwapOrder::Side::ASK, 0, 15000000, 500, 3));
    TEST(lvl.totalDepth() == 3500);
  }

  // Partial fill accounted
  {
    PriceLevel lvl;
    auto o = makeOrder(SwapOrder::Side::ASK, 0, 15000000, 1000);
    o.filled = 300;
    lvl.orders.push_back(o);
    TEST(lvl.totalDepth() == 700);
  }

  // ── PairOrderBook bestBid / bestAsk ──

  // Empty book
  {
    PairOrderBook book;
    TEST(book.bestBid() == 0);
    TEST(book.bestAsk() == 0);
  }

  // Single bid
  {
    PairOrderBook book;
    book.bids[15000000].orders.push_back(
        makeOrder(SwapOrder::Side::BID, 0, 15000000, 1000));
    TEST(book.bestBid() == 15000000);
    TEST(book.bestAsk() == 0);
  }

  // Multiple bids — best is highest
  {
    PairOrderBook book;
    book.bids[14000000].orders.push_back(
        makeOrder(SwapOrder::Side::BID, 0, 14000000, 1000, 1));
    book.bids[16000000].orders.push_back(
        makeOrder(SwapOrder::Side::BID, 0, 16000000, 1000, 2));
    book.bids[15000000].orders.push_back(
        makeOrder(SwapOrder::Side::BID, 0, 15000000, 1000, 3));
    TEST(book.bestBid() == 16000000);
  }

  // Multiple asks — best is lowest
  {
    PairOrderBook book;
    book.asks[16000000].orders.push_back(
        makeOrder(SwapOrder::Side::ASK, 0, 16000000, 1000, 1));
    book.asks[14000000].orders.push_back(
        makeOrder(SwapOrder::Side::ASK, 0, 14000000, 1000, 2));
    book.asks[15000000].orders.push_back(
        makeOrder(SwapOrder::Side::ASK, 0, 15000000, 1000, 3));
    TEST(book.bestAsk() == 14000000);
  }

  // Clear
  {
    PairOrderBook book;
    book.bids[15000000].orders.push_back(
        makeOrder(SwapOrder::Side::BID, 0, 15000000, 1000));
    book.asks[16000000].orders.push_back(
        makeOrder(SwapOrder::Side::ASK, 0, 16000000, 1000));
    book.clear();
    TEST(book.bestBid() == 0);
    TEST(book.bestAsk() == 0);
  }

  // ── Matching: bid takes ask (exact price match) ──

  {
    PairOrderBook book;
    book.asks[15000000].orders.push_back(
        makeOrder(SwapOrder::Side::ASK, 0, 15000000, 1000, 1));

    auto fills = matchOrders(book, SwapOrder::Side::BID, 0, 15000000, 500);
    TEST(fills.size() == 1);
    TEST(fills[0].fillPrice == 15000000);
    TEST(fills[0].fillAmount == 500);
    // Maker partially filled
    TEST(book.asks[15000000].orders.front().filled == 500);
  }

  // ── Matching: bid takes ask (bid price > ask price) ──

  {
    PairOrderBook book;
    book.asks[15000000].orders.push_back(
        makeOrder(SwapOrder::Side::ASK, 0, 15000000, 1000, 1));

    auto fills = matchOrders(book, SwapOrder::Side::BID, 0, 16000000, 1000);
    TEST(fills.size() == 1);
    TEST(fills[0].fillAmount == 1000);
    TEST(book.asks.empty());  // fully filled
  }

  // ── Matching: bid below ask — no fill ──

  {
    PairOrderBook book;
    book.asks[16000000].orders.push_back(
        makeOrder(SwapOrder::Side::ASK, 0, 16000000, 1000, 1));

    auto fills = matchOrders(book, SwapOrder::Side::BID, 0, 15000000, 1000);
    TEST(fills.empty());
    TEST(book.asks[16000000].orders.size() == 1);
  }

  // ── Matching: ask takes bid (exact price match) ──

  {
    PairOrderBook book;
    book.bids[15000000].orders.push_back(
        makeOrder(SwapOrder::Side::BID, 0, 15000000, 1000, 1));

    auto fills = matchOrders(book, SwapOrder::Side::ASK, 0, 15000000, 500);
    TEST(fills.size() == 1);
    TEST(fills[0].fillPrice == 15000000);
    TEST(fills[0].fillAmount == 500);
    TEST(book.bids[15000000].orders.front().filled == 500);
  }

  // ── Matching: ask below bid — no fill ──

  {
    PairOrderBook book;
    book.bids[14000000].orders.push_back(
        makeOrder(SwapOrder::Side::BID, 0, 14000000, 1000, 1));

    auto fills = matchOrders(book, SwapOrder::Side::ASK, 0, 15000000, 1000);
    TEST(fills.empty());
  }

  // ── Matching: price-time priority (FIFO at same price) ──

  {
    PairOrderBook book;
    book.asks[15000000].orders.push_back(
        makeOrder(SwapOrder::Side::ASK, 0, 15000000, 500, 1));
    book.asks[15000000].orders.push_back(
        makeOrder(SwapOrder::Side::ASK, 0, 15000000, 500, 2));

    auto fills = matchOrders(book, SwapOrder::Side::BID, 0, 15000000, 500);
    TEST(fills.size() == 1);
    // First order filled, second untouched
    TEST(fills[0].makerOrderId == "order_1");
    TEST(book.asks[15000000].orders.size() == 1);
    TEST(book.asks[15000000].orders.front().orderId == "order_2");
  }

  // ── Matching: fill crosses multiple levels ──

  {
    PairOrderBook book;
    book.asks[15000000].orders.push_back(
        makeOrder(SwapOrder::Side::ASK, 0, 15000000, 300, 1));
    book.asks[16000000].orders.push_back(
        makeOrder(SwapOrder::Side::ASK, 0, 16000000, 300, 2));
    book.asks[17000000].orders.push_back(
        makeOrder(SwapOrder::Side::ASK, 0, 17000000, 300, 3));

    // Taker bids 17M for 700 — should eat first two levels fully + 100 from third
    auto fills = matchOrders(book, SwapOrder::Side::BID, 0, 17000000, 700);
    TEST(fills.size() == 3);
    TEST(fills[0].fillAmount == 300);
    TEST(fills[1].fillAmount == 300);
    TEST(fills[2].fillAmount == 100);
    // Third level has 200 remaining
    TEST(book.asks.size() == 1);
    TEST(book.asks[17000000].orders.front().filled == 100);
  }

  // ── Matching: empty book → no fills ──

  {
    PairOrderBook book;
    auto fills = matchOrders(book, SwapOrder::Side::BID, 0, 15000000, 1000);
    TEST(fills.empty());
  }

  // ── Matching: taker amount 0 → no fills ──

  {
    PairOrderBook book;
    book.asks[15000000].orders.push_back(
        makeOrder(SwapOrder::Side::ASK, 0, 15000000, 1000, 1));
    auto fills = matchOrders(book, SwapOrder::Side::BID, 0, 15000000, 0);
    TEST(fills.empty());
  }

  // ── Matching: ask sweeps multiple bids (highest first) ──

  {
    PairOrderBook book;
    book.bids[17000000].orders.push_back(
        makeOrder(SwapOrder::Side::BID, 0, 17000000, 400, 1));
    book.bids[16000000].orders.push_back(
        makeOrder(SwapOrder::Side::BID, 0, 16000000, 400, 2));
    book.bids[15000000].orders.push_back(
        makeOrder(SwapOrder::Side::BID, 0, 15000000, 400, 3));

    // Taker asks 15M for 600 — should eat 17M (400) + 16M (200)
    auto fills = matchOrders(book, SwapOrder::Side::ASK, 0, 15000000, 600);
    TEST(fills.size() == 2);
    TEST(fills[0].fillPrice == 17000000);  // highest bid first
    TEST(fills[0].fillAmount == 400);
    TEST(fills[1].fillPrice == 16000000);
    TEST(fills[1].fillAmount == 200);
  }

  // ── Matching: FIFO at same price level (bid side) ──

  {
    PairOrderBook book;
    book.bids[15000000].orders.push_back(
        makeOrder(SwapOrder::Side::BID, 0, 15000000, 500, 1));
    book.bids[15000000].orders.push_back(
        makeOrder(SwapOrder::Side::BID, 0, 15000000, 500, 2));

    auto fills = matchOrders(book, SwapOrder::Side::ASK, 0, 15000000, 500);
    TEST(fills.size() == 1);
    TEST(fills[0].makerOrderId == "order_1");  // first in, first out
  }

  // ── Matching: separate PairOrderBook per pair (pair isolation) ──

  {
    PairOrderBook book0, book1;
    book0.asks[15000000].orders.push_back(
        makeOrder(SwapOrder::Side::ASK, 0, 15000000, 1000, 1));
    book1.asks[20000000].orders.push_back(
        makeOrder(SwapOrder::Side::ASK, 1, 20000000, 1000, 2));

    // Match on book0 at 15M — fills from book0
    auto fills0 = matchOrders(book0, SwapOrder::Side::BID, 0, 15000000, 500);
    TEST(fills0.size() == 1);
    TEST(fills0[0].fillPrice == 15000000);

    // book1 untouched
    TEST(book1.asks[20000000].orders.size() == 1);
    TEST(book1.asks[20000000].orders.front().filled == 0);
  }

  // ── OrderBookSnapshot: empty book ──

  {
    PairOrderBook book;
    OrderBookSnapshot snap;
    snap.height = 100;
    // Empty → no bids/asks, spread = 0
    TEST(snap.bids.empty());
    TEST(snap.asks.empty());
    TEST(snap.spread == 0);
  }

  // ── SwapOrder defaults ──

  {
    SwapOrder o;
    TEST(o.filled == 0);
    TEST(o.amount == 0);
    TEST(o.price == 0);
    TEST(o.ttlBlocks == 0);
  }

  // ── Multiple pairs independent ──

  {
    PairOrderBook book0, book1;
    book0.asks[15000000].orders.push_back(
        makeOrder(SwapOrder::Side::ASK, 0, 15000000, 1000, 1));
    book1.asks[20000000].orders.push_back(
        makeOrder(SwapOrder::Side::ASK, 1, 20000000, 2000, 2));

    TEST(book0.bestAsk() == 15000000);
    TEST(book1.bestAsk() == 20000000);
  }

  // ── Full fill removes price level ──

  {
    PairOrderBook book;
    book.asks[15000000].orders.push_back(
        makeOrder(SwapOrder::Side::ASK, 0, 15000000, 1000, 1));

    auto fills = matchOrders(book, SwapOrder::Side::BID, 0, 15000000, 1000);
    TEST(fills.size() == 1);
    TEST(fills[0].fillAmount == 1000);
    TEST(book.asks.find(15000000) == book.asks.end());
  }

  fprintf(stderr, "\n=== P2P SwapOrderbook Tests ===\n");
  fprintf(stderr, "Passed: %d / %d\n", tests_passed, tests_run);
  return (tests_passed == tests_run) ? 0 : 1;
}
