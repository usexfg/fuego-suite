// Copyright (c) 2017-2026, Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You can redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation. Fuego includes elements written
// by third parties. See file labeled LICENSE for more details.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

// Regression tests for the four confirmed PriceOracle defects:
//   1. OPTIMISM was seeded from the OP token instead of native ETH.
//   2. ctrDivisor() invented 1e8 for a pair with no descriptor.
//   2b. recordCompletedSwap accepted an uncomputed rate of 0, which getTwap then
//       weighted as rate*volume and dragged the average toward zero.
//   3. OfferManager's no-composite fallback used the static seed, never the live price.
//   4. The rate band was asymmetric (ref/0.8 up, ref*0.20 down) while the header
//      documented 0.50.

#include "PriceOracle.h"
#include "PriceFeed.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace XfgSwap;

static int g_failures = 0;
static int g_checks = 0;

static void check(bool cond, const std::string& what) {
  ++g_checks;
  if (!cond) { ++g_failures; std::printf("FAIL  %s\n", what.c_str()); }
}

static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

int main() {
  // ---- Defect 1: Optimism locks native ETH, so it must price off ETH.
  {
    const double op = PriceOracle::getSeedRate(SwapPair::OPTIMISM);
    const double eth = PriceOracle::getSeedRate(SwapPair::ETH);
    check(near(op, eth, 1.0), "OPTIMISM seed now equals ETH, not the OP token");

    // Every ETH-settling pair must share one rate.
    const SwapPair ethSettled[] = {SwapPair::OPTIMISM, SwapPair::ARB, SwapPair::BASE,
                                   SwapPair::BOB, SwapPair::UNICHAIN, SwapPair::ROBINHOOD};
    bool allSame = true;
    for (SwapPair p : ethSettled) {
      if (!near(PriceOracle::getSeedRate(p), eth, 1.0)) allSame = false;
    }
    check(allSame, "all six ETH-settling pairs share the ETH seed");
  }

  // ---- Defect 2: an unknown pair must yield 0.0, never a fabricated 1e8.
  {
    check(PriceOracle::ctrDivisor(SwapPair::ETH) == 1e18, "known pair keeps its real divisor");
    check(PriceOracle::ctrDivisor(static_cast<SwapPair>(250)) == 0.0,
          "out-of-range pair reports 0.0 instead of inventing 1e8");
  }

  // ---- Defect 2b: a zero-rate trade must not enter the TWAP.
  {
    PriceOracle oracle;
    CompletedSwapTrade bad;
    bad.pair = SwapPair::ETH;
    bad.xfgAmount = 500000000;  // 50 XFG
    bad.ctrAmount = AtomicAmount(1);
    bad.rate = 0.0;             // could not be computed
    bad.timestamp = std::time(nullptr);
    oracle.recordCompletedSwap(bad);
    check(oracle.getTradeCount(SwapPair::ETH) == 0, "a zero-rate trade is not recorded");
    check(oracle.getTwap(SwapPair::ETH) == 0.0, "and it contributes nothing to the TWAP");

    CompletedSwapTrade good = bad;
    good.rate = 13500.0;
    oracle.recordCompletedSwap(good);
    check(oracle.getTradeCount(SwapPair::ETH) == 1, "a real trade is still recorded");
    check(oracle.getTwap(SwapPair::ETH) == 0.0, "one trade is below TWAP_MIN_TRADES");
  }

  // ---- Defect 3: the live XFG price must move the effective rate.
  {
    PriceOracle oracle;
    const double atSeed = oracle.getEffectiveRate(SwapPair::ETH);
    check(atSeed > 0.0, "effective rate exists before any live price");

    // Halving XFG/USD doubles the XFG-per-ETH rate.
    const double live = PriceOracle::getSeedXfgUsd() / 2.0;
    oracle.setLiveXfgUsd(live);
    const double after = oracle.getEffectiveRate(SwapPair::ETH);
    check(near(after / atSeed, 2.0, 0.01), "halving XFG/USD doubles the effective rate");
    check(near(oracle.getLiveXfgUsd(), live, 1e-9), "live XFG/USD reads back");

    // A nonsense live price must be rejected, not adopted.
    oracle.setLiveXfgUsd(-1.0);
    check(oracle.getLiveXfgUsd() == live, "a negative live price is refused");
    oracle.setLiveXfgUsd(0.0);
    check(oracle.getLiveXfgUsd() == live, "a zero live price is refused");
  }

  // ---- Defect 4: symmetric +/-20% band.
  {
    PriceOracle oracle;
    const double ref = PriceOracle::getSeedRate(SwapPair::ETH);
    // The band is only reached once a trade exists; with zero trades validateRate
    // returns from the bootstrap branch first.
    CompletedSwapTrade seed;
    seed.pair = SwapPair::ETH; seed.xfgAmount = 100000000; seed.ctrAmount = AtomicAmount(1);
    seed.rate = ref; seed.timestamp = std::time(nullptr);
    oracle.recordCompletedSwap(seed);

    check(oracle.validateRate(SwapPair::ETH, ref) == RateCheck::OK, "exactly at reference is OK");

    // The guide's band: ref/1.20 .. ref/0.80.
    check(oracle.validateRate(SwapPair::ETH, ref / 1.2) == RateCheck::OK, "lower edge of +/-(20%) is OK");
    check(oracle.validateRate(SwapPair::ETH, ref / 0.8) == RateCheck::OK, "upper edge of +/-(20%) is OK");
    check(oracle.validateRate(SwapPair::ETH, ref * 1.26) == RateCheck::BELOW_FLOOR, "just outside the top is rejected");
    check(oracle.validateRate(SwapPair::ETH, ref / 1.26) == RateCheck::ABOVE_MARKET, "just outside the bottom is rejected");

    // The old -80% tail must no longer be admissible.
    check(oracle.validateRate(SwapPair::ETH, ref * 0.5) == RateCheck::ABOVE_MARKET,
          "the old ref*0.20 tail is closed");
    // The old code's midpoint is now inside the band, not below the floor.
    check(oracle.validateRate(SwapPair::ETH, ref * 1.10) == RateCheck::OK,
          "a 10% drift is inside the band");
  }

  // setFloorThreshold is a tolerance, and it refuses to disable the band.
  {
    PriceOracle oracle;
    const double ref = PriceOracle::getSeedRate(SwapPair::ETH);
    CompletedSwapTrade seed;
    seed.pair = SwapPair::ETH; seed.xfgAmount = 100000000; seed.ctrAmount = AtomicAmount(1);
    seed.rate = ref; seed.timestamp = std::time(nullptr);
    oracle.recordCompletedSwap(seed);
    oracle.setFloorThreshold(0.10);
    check(oracle.validateRate(SwapPair::ETH, ref / 1.05) == RateCheck::OK, "a 10% tolerance admits 5% drift");
    check(oracle.validateRate(SwapPair::ETH, ref / 1.15) == RateCheck::ABOVE_MARKET, "and still rejects 15%");

    oracle.setFloorThreshold(0.0);
    oracle.setFloorThreshold(1.0);
    oracle.setFloorThreshold(-0.5);
    // The three rejected calls must have left the tolerance at 0.10, so 15% is still out.
    check(oracle.validateRate(SwapPair::ETH, ref / 1.15) == RateCheck::ABOVE_MARKET,
          "a rejected config cannot re-widen the band past 10%");
  }

  // The bootstrap window must no longer be wider than the band.
  {
    PriceOracle fresh;  // zero recorded trades -> bootstrap branch
    const double seed = PriceOracle::getSeedRate(SwapPair::ETH);
    check(fresh.validateRate(SwapPair::ETH, seed * 1.10) == RateCheck::RATE_NO_DATA,
          "a 10% drift is inside the bootstrap window");
    check(fresh.validateRate(SwapPair::ETH, seed * 1.30) == RateCheck::ABOVE_MARKET,
          "the old +/-50% bootstrap tail is now closed");
    check(fresh.validateRate(SwapPair::ETH, seed * 0.70) == RateCheck::BELOW_FLOOR,
          "and on the low side too");
  }

  // The legacy band and the live policy must agree.
  {
    PriceOracle oracle;
    const double ref = PriceOracle::getSeedRate(SwapPair::ETH);
    CompletedSwapTrade seed;
    seed.pair = SwapPair::ETH; seed.xfgAmount = 100000000; seed.ctrAmount = AtomicAmount(1);
    seed.rate = ref; seed.timestamp = std::time(nullptr);
    oracle.recordCompletedSwap(seed);
    const double probe = ref * 0.84;
    const bool legacyOk = oracle.validateRate(SwapPair::ETH, probe) == RateCheck::OK;
    const bool liveOk = withinRateGuard(probe, ref);
    check(legacyOk == liveOk, "legacy and live paths agree at the same probe");
  }

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}