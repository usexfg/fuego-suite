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
#include "CryptoNoteCore/MarketOrderExecutor.h"
#include "CryptoNoteCore/HearthRebalance.h"
#include "CryptoNoteCore/AmmPool.h"
#include "Common/Int128.h"

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
  e.side = 0; e.price = price; e.amount = amount;
  e.expiration = expiration;
  e.spendKey = makePubKey(id); e.viewKey = makePubKey(id + 100);
  e.blockHeight = 1000;
  return e;
}

static OrderEntry makeAsk(uint8_t id, uint64_t price, uint64_t amount, uint32_t expiration = 0) {
  OrderEntry e;
  e.orderId = makeHash(id);
  e.side = 1; e.price = price; e.amount = amount;
  e.expiration = expiration;
  e.spendKey = makePubKey(id); e.viewKey = makePubKey(id + 100);
  e.blockHeight = 1000;
  return e;
}

int main() {
  // ======== HearthRebalance Tests ========

  // Pool at 1:1, P_clear also 1:1 — no rebalance needed
  {
    HearthRebalance rebalancer(30);
    uint64_t xfg = 100000000000;   // 10,000 XFG
    uint64_t heat = 100000000000;  // 10,000 HEAT
    uint64_t P_clear = 100000000;  // 1.00000000
    auto action = rebalancer.computeRebalance(xfg, heat, P_clear, 10);
    TEST(action.direction == RebalanceAction::NONE);
  }

  // Pool HEAT-heavy (XFG too cheap, ratio 0.5): should swap XFG→HEAT to increase ratio
  {
    HearthRebalance rebalancer(30);
    uint64_t xfg = 50000000000;   // 5,000 XFG
    uint64_t heat = 100000000000; // 10,000 HEAT (ratio = 0.5)
    uint64_t P_clear = 100000000; // target 1.0
    auto action = rebalancer.computeRebalance(xfg, heat, P_clear, 10);
    TEST(action.direction == RebalanceAction::SWAP_XFG_FOR_HEAT);
    TEST(action.inputAmount > 0);
  }

  // Pool XFG-heavy (HEAT too cheap, ratio 2.0): should swap HEAT→XFG to decrease ratio
  {
    HearthRebalance rebalancer(30);
    uint64_t xfg = 100000000000;  // 10,000 XFG
    uint64_t heat = 50000000000;  // 5,000 HEAT (ratio = 2.0)
    uint64_t P_clear = 100000000; // target 1.0
    auto action = rebalancer.computeRebalance(xfg, heat, P_clear, 10);
    TEST(action.direction == RebalanceAction::SWAP_HEAT_FOR_XFG);
    TEST(action.inputAmount > 0);
  }

  // Zero reserves — no crash
  {
    HearthRebalance rebalancer(30);
    auto action = rebalancer.computeRebalance(0, 0, 100000000, 10);
    TEST(action.direction == RebalanceAction::NONE);
  }

  // ======== MarketOrderExecutor Tests ========

  // Market buy within HEARTH band only
  {
    OrderbookIndex idx;
    MarketOrderExecutor exec(10, 5, 150, 30);

    uint64_t xfgReserve = 100000000000;  // 10,000 XFG
    uint64_t heatReserve = 100000000000; // 10,000 HEAT
    uint64_t P_clear = 100000000;        // 1.0

    auto result = exec.executeMarketBuy(
      500000000,   // want 50 XFG
      0,           // no cost limit
      P_clear,
      xfgReserve, heatReserve,
      idx);

    TEST(result.filledAmount > 0);
    TEST(result.hearthFilled == result.filledAmount);
    TEST(result.orderbookFilled == 0);
    TEST(!result.halted);
  }

  // Market buy cascades into orderbook when band exhausted
  {
    OrderbookIndex idx;
    idx.addOrder(makeAsk(1, 105000000, 10000000000ULL)); // ask 1000 XFG at 1.05

    MarketOrderExecutor exec(10, 5, 150, 30);

    uint64_t xfgReserve = 10000000000;   // only 1,000 XFG in pool
    uint64_t heatReserve = 10000000000;  // 1,000 HEAT
    uint64_t P_clear = 100000000;

    // Band depth = 10% of 1000 = 100 XFG. Want 500 XFG.
    auto result = exec.executeMarketBuy(
      5000000000ULL, 0, P_clear,
      xfgReserve, heatReserve, idx);

    TEST(result.filledAmount > 0);
    TEST(result.hearthFilled > 0);
    TEST(result.orderbookFilled > 0);  // cascaded
  }

  // Market buy hits price guard
  {
    OrderbookIndex idx;
    idx.addOrder(makeAsk(1, 300000000, 10000000000ULL)); // ask at 3.0 (200% above 1.0)

    MarketOrderExecutor exec(10, 5, 150, 30);  // 150% max deviation

    uint64_t xfgReserve = 1000000000;
    uint64_t heatReserve = 1000000000;
    uint64_t P_clear = 100000000;

    auto result = exec.executeMarketBuy(
      10000000000ULL, 0, P_clear,
      xfgReserve, heatReserve, idx);

    // Should halt at 150% guard before reaching ask at 3.0
    TEST(result.halted || result.orderbookFilled == 0);
  }

  // Market sell within band
  {
    OrderbookIndex idx;
    MarketOrderExecutor exec(10, 5, 150, 30);

    uint64_t xfgReserve = 100000000000;
    uint64_t heatReserve = 100000000000;
    uint64_t P_clear = 100000000;

    auto result = exec.executeMarketSell(
      500000000, 0, P_clear,
      xfgReserve, heatReserve, idx);

    TEST(result.filledAmount > 0);
    TEST(result.hearthFilled == result.filledAmount);
    TEST(!result.halted);
  }

  // Market sell on empty book — band only
  {
    OrderbookIndex idx;
    MarketOrderExecutor exec(10, 5, 150, 30);

    uint64_t xfgReserve = 100000000000;
    uint64_t heatReserve = 100000000000;
    uint64_t P_clear = 100000000;

    auto result = exec.executeMarketSell(
      50000000000ULL, 0, P_clear,  // sell 5,000 XFG
      xfgReserve, heatReserve, idx);

    // Should fill whatever the band allows
    TEST(result.filledAmount > 0);
    TEST(result.orderbookFilled == 0);
  }

  // Compute depth band correctly
  {
    TEST(computeDepthBand(100000000000, 10) == 10000000000ULL);  // 10% of 10k XFG
    TEST(computeDepthBand(100000000000, 5) == 5000000000ULL);    // 5%
    TEST(computeDepthBand(0, 10) == 0);
  }

  // ======== AMM Math Verification ========
  {
    // Constant product sanity: swap should preserve invariant
    uint64_t xfg = 100000000000;
    uint64_t heat = 100000000000;
    uint64_t k = (uint128_t)xfg * heat;

    uint64_t inXfg = 1000000000;  // 100 XFG in
    uint64_t outHeat = ammGetOutputAmount(inXfg, xfg, heat, 30);

    uint64_t newXfg = xfg + inXfg;
    uint64_t newHeat = heat - outHeat;
    uint128_t newK = (uint128_t)newXfg * newHeat;

    TEST(newK >= k);  // invariant holds (grows due to fee)
  }

  fprintf(stderr, "\n=== Phase 3 Tests ===\n");
  fprintf(stderr, "Passed: %d / %d\n", tests_passed, tests_run);
  return (tests_passed == tests_run) ? 0 : 1;
}
