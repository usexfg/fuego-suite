// Copyright (c) 2017-2026 Fuego Developers
#include "gtest/gtest.h"
#include "CryptoNoteCore/AmmPool.h"
#include "Common/Int128.h"

using namespace CryptoNote;

TEST(AmmPool, SwapBasic) {
  EXPECT_EQ(ammGetOutputAmount(100, 1000, 2000, 0), 181u);
}

TEST(AmmPool, SwapWithFee) {
  uint64_t out = ammGetOutputAmount(100, 1000, 2000, 30);
  uint64_t outNoFee = ammGetOutputAmount(100, 1000, 2000, 0);
  EXPECT_LT(out, outNoFee);
}

TEST(AmmPool, SwapInvariant) {
  // Constant product must hold: (R+i)*(H-o) ≈ R*H (slightly larger from fee)
  uint64_t R = 1000, H = 2000, input = 100;
  uint64_t output = ammGetOutputAmount(input, R, H, 30);
  uint128_t before = (uint128_t)R * H;
  uint128_t after  = (uint128_t)(R + input) * (H - output);
  EXPECT_GE(after, before);
}

TEST(AmmPool, SpotPrice) {
  uint64_t spot = ammGetSpotPrice(200000000ULL, 1000000000ULL); // 0.2 scaled by 1e18
  EXPECT_GT(spot, 0u);
  EXPECT_LT(spot, 1000000000000000000ULL);
}

TEST(AmmPool, LpMintInitial) {
  uint64_t shares = ammMintLpShares(1000, 5000, 0, 0, 0);
  EXPECT_GT(shares, 0u);
}

TEST(AmmPool, LpMintProportional) {
  uint64_t shares = ammMintLpShares(100, 500, 1000, 1000, 5000);
  EXPECT_GT(shares, 0u);
}

TEST(AmmPool, LpWithdrawal) {
  uint64_t a = 0, b = 0;
  ammGetWithdrawalAmounts(100, 1000, 5000, 25000, a, b);
  EXPECT_EQ(a, 500u);
  EXPECT_EQ(b, 2500u);
}

TEST(AmmPool, ValidateSwap) {
  EXPECT_TRUE(ammValidateSwap(10, 18, 100, 200, 0));
  EXPECT_FALSE(ammValidateSwap(10, 1, 100, 200, 0));
}

TEST(AmmPool, ValidateDepositRatio) {
  EXPECT_TRUE(ammValidateDepositRatio(100, 500, 1000, 5000, 100));
}

TEST(AmmPool, SingleSidedDeposit) {
  uint64_t shares = ammMintLpShares(0, 500, 1000, 1000, 5000);
  EXPECT_GT(shares, 0u);
  EXPECT_EQ(shares, 100u); // (500 * 1000) / 5000 = 100
}

TEST(AmmPool, SingleSidedZero) {
  uint64_t shares = ammMintLpShares(0, 500, 0, 0, 0);
  EXPECT_EQ(shares, 500u); // initial single-sided
}

TEST(AmmPool, OutputAmountMax) {
  // Near-max pool: should not overflow
  uint64_t out = ammGetOutputAmount(1, UINT64_MAX/2, UINT64_MAX/2, 0);
  EXPECT_GT(out, 0u);
}
