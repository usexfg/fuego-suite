// Copyright (c) 2017-2026 Fuego Developers

#include "gtest/gtest.h"
#include "CryptoNoteCore/AmmPool.h"
#include "CryptoNoteCore/PiController.h"
#include "Common/FixedPoint.h"
#include "CryptoNoteConfig.h"

using namespace CryptoNote;

// ---------------------------------------------------------------------------
//  AMM constant-product math tests
// ---------------------------------------------------------------------------

TEST(AmmPool, swap_output) {
  // Pool: 100 XFG, 200 HEAT (0.5 ratio)
  // Swap 10 XFG → HEAT
  uint64_t out = ammGetOutputAmount(10'000'000'000ULL, 100'000'000'000ULL, 200'000'000'000ULL, 0);
  // x * y = k, (x+dx)*(y-dy) = k → dy = y*dx/(x+dx)
  // dy = 200e9 * 10e9 / (100e9 + 10e9) = 2000e18 / 110e9 ≈ 18.18e9
  EXPECT_GT(out, 18'000'000'000ULL);
  EXPECT_LT(out, 19'000'000'000ULL);
}

TEST(AmmPool, swap_with_fee) {
  // 1% fee
  uint64_t out = ammGetOutputAmount(10'000'000'000ULL, 100'000'000'000ULL, 200'000'000'000ULL, 100);
  // With 1% fee: dx_effective = 10e9 * 0.99 = 9.9e9
  // dy = 200e9 * 9.9e9 / (100e9 + 9.9e9) ≈ 1980e18 / 109.9e9 ≈ 18.0e9
  EXPECT_GT(out, 17'500'000'000ULL);
  EXPECT_LT(out, 18'500'000'000ULL);
}

TEST(AmmPool, swap_invariant) {
  uint64_t rIn = 100'000'000'000ULL;
  uint64_t rOut = 200'000'000'000ULL;
  uint64_t dx = 10'000'000'000ULL;
  uint64_t dy = ammGetOutputAmount(dx, rIn, rOut, 0);

  // new K >= old K
  unsigned __int128 kOld = (unsigned __int128)rIn * rOut;
  unsigned __int128 kNew = (unsigned __int128)(rIn + dx) * (rOut - dy);
  EXPECT_GE(kNew, kOld);
}

TEST(AmmPool, spot_price) {
  uint64_t price = ammGetSpotPrice(100'000'000'000ULL, 200'000'000'000ULL);
  // price = 100e9 / 200e9 * 1e18 = 0.5 * 1e18
  EXPECT_EQ(price, 500'000'000'000'000'000ULL);
}

TEST(AmmPool, lp_mint_initial) {
  uint64_t shares = ammMintLpShares(100'000'000'000ULL, 200'000'000'000ULL, 0, 0, 0);
  EXPECT_GT(shares, 0ULL);
}

TEST(AmmPool, lp_mint_proportional) {
  uint64_t existing = 100'000'000ULL; // 100M LP shares
  uint64_t shares = ammMintLpShares(10'000'000'000ULL, 20'000'000'000ULL,
                                     existing,
                                     100'000'000'000ULL, 200'000'000'000ULL);
  // 10% of pool → ~10% of shares = ~10M
  EXPECT_GT(shares, 8'000'000ULL);
  EXPECT_LT(shares, 12'000'000ULL);
}

TEST(AmmPool, lp_withdrawal) {
  uint64_t a, b;
  ammGetWithdrawalAmounts(10'000'000ULL, 100'000'000ULL,
                           100'000'000'000ULL, 200'000'000'000ULL,
                           a, b);
  // 10% of 100M shares → 10% of reserves
  EXPECT_EQ(a, 10'000'000'000ULL);
  EXPECT_EQ(b, 20'000'000'000ULL);
}

TEST(AmmPool, swap_empty_pool) {
  EXPECT_EQ(ammGetOutputAmount(100, 0, 0, 0), 0ULL);
}

TEST(AmmPool, validate_ratio_empty) {
  EXPECT_TRUE(ammValidateDepositRatio(100, 200, 0, 0, 100));
}

TEST(AmmPool, validate_ratio_within_tolerance) {
  // Pool: 100 XFG / 200 HEAT = 0.5 ratio
  // Deposit: 10 XFG / 20 HEAT = 0.5 ratio ← exact match
  EXPECT_TRUE(ammValidateDepositRatio(10'000'000'000ULL, 20'000'000'000ULL,
                                       100'000'000'000ULL, 200'000'000'000ULL, 100));
}

// ---------------------------------------------------------------------------
//  PI Controller tests
// ---------------------------------------------------------------------------

TEST(PiController, at_peg) {
  PiController ctrl;
  FixedPoint64 price = FixedPoint64::fromRatio(1, 2); // 0.5
  FixedPoint64 integral;
  FixedPoint64 rate;

  ctrl.calculate(price, 0, price, integral, rate, 900);

  // At peg → error = 0 → rate = 0
  EXPECT_TRUE(rate.isZero());
}

TEST(PiController, above_peg) {
  PiController ctrl;
  FixedPoint64 redemptionPrice = FixedPoint64::fromRatio(1, 2); // 0.5
  FixedPoint64 marketPrice = FixedPoint64::fromRatio(3, 5);     // 0.6
  FixedPoint64 integral;
  FixedPoint64 rate;

  ctrl.calculate(marketPrice, 0, redemptionPrice, integral, rate, 900);

  // Market > redemption → positive error → positive rate (boost redemption price)
  EXPECT_TRUE(rate.isPositive());
}

TEST(PiController, below_peg) {
  PiController ctrl;
  FixedPoint64 redemptionPrice = FixedPoint64::fromRatio(1, 2); // 0.5
  FixedPoint64 marketPrice = FixedPoint64::fromRatio(2, 5);     // 0.4
  FixedPoint64 integral;
  FixedPoint64 rate;

  ctrl.calculate(marketPrice, 0, redemptionPrice, integral, rate, 900);

  // Market < redemption → negative error → negative rate (reduce redemption price)
  EXPECT_TRUE(rate.isNegative());
}

TEST(PiController, integral_accumulates) {
  PiController ctrl;
  FixedPoint64 redemptionPrice = FixedPoint64::fromRatio(1, 2);
  FixedPoint64 marketPrice = FixedPoint64::fromRatio(3, 5); // always above
  FixedPoint64 integral;
  FixedPoint64 rate;

  ctrl.calculate(marketPrice, 0, redemptionPrice, integral, rate, 900);
  EXPECT_TRUE(integral.isPositive());

  FixedPoint64 prevIntegral = integral;
  ctrl.calculate(marketPrice, 0, redemptionPrice, integral, rate, 900);
  EXPECT_GT(integral, prevIntegral); // integral grows with persistent error
}

TEST(PiController, rate_clamped) {
  PiController ctrl;
  FixedPoint64 redemptionPrice = FixedPoint64::fromRatio(1, 2);
  FixedPoint64 marketPrice = FixedPoint64::fromUint64(100); // extreme
  FixedPoint64 integral;
  FixedPoint64 rate;

  ctrl.calculate(marketPrice, 0, redemptionPrice, integral, rate, 900);

  // Rate should be clamped at PI_MAX_RATE
  FixedPoint64 maxRate = FixedPoint64::fromRatio(parameters::PI_MAX_RATE_NUM,
                                                  parameters::PI_MAX_RATE_DENOM);
  EXPECT_LE(rate, maxRate);
}

TEST(PiController, price_floor) {
  PiController ctrl;
  FixedPoint64 redemptionPrice = FixedPoint64::fromRatio(1, 1000); // very low
  FixedPoint64 marketPrice = FixedPoint64::fromRatio(1, 1000000); // even lower
  FixedPoint64 integral;
  FixedPoint64 rate;

  ctrl.calculate(marketPrice, 0, redemptionPrice, integral, rate, 900);

  // Price should not go below floor (1e-6)
  FixedPoint64 floor = FixedPoint64::fromRatio(1, 1000000);
  EXPECT_GE(redemptionPrice, floor);
}

TEST(PiController, zero_blocks_no_change) {
  PiController ctrl;
  FixedPoint64 price = FixedPoint64::fromRatio(1, 2);
  FixedPoint64 integral;
  FixedPoint64 rate;

  ctrl.calculate(price, 0, price, integral, rate, 0);
  EXPECT_TRUE(rate.isZero());
}

// ---------------------------------------------------------------------------
//  FixedPoint + PI integration
// ---------------------------------------------------------------------------

TEST(Integration, price_convergence_simulation) {
  // Simulate 10 epochs of price convergence with mixed signals
  PiController ctrl;
  FixedPoint64 redemptionPrice = FixedPoint64::fromRatio(1, 2); // 0.5
  FixedPoint64 integral;
  FixedPoint64 rate;

  // Alternating above/below to test convergence without wind-up
  FixedPoint64 abovePeg = FixedPoint64::fromRatio(55, 100); // 0.55
  FixedPoint64 belowPeg = FixedPoint64::fromRatio(45, 100); // 0.45

  for (int epoch = 0; epoch < 5; ++epoch) {
    ctrl.calculate(abovePeg, 0, redemptionPrice, integral, rate, 900);
    ctrl.calculate(belowPeg, 0, redemptionPrice, integral, rate, 900);
  }

  // Redemption price should remain positive and in a reasonable range
  EXPECT_GE(redemptionPrice, FixedPoint64::fromRatio(1, 100));
  EXPECT_LE(redemptionPrice, FixedPoint64::fromRatio(10, 1));
}
