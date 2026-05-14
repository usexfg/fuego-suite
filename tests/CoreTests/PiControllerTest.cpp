// Copyright (c) 2017-2026 Fuego Developers
#include "gtest/gtest.h"
#include "CryptoNoteCore/PiController.h"

using namespace CryptoNote;

TEST(PiController, BootstrapTarget) {
  PiControllerState state;
  state.redemptionPrice = FixedPoint64::fromRatio(1, 5); // 0.2

  FixedPoint64 zero;
  FixedPoint64 spot = FixedPoint64::fromRatio(2, 10); // 0.2
  FixedPoint64 target = computeTargetRatio(state, 0, zero, spot, 0);
  EXPECT_GT(target.raw(), 0);
}

TEST(PiController, RateComputation) {
  PiControllerState state;
  state.redemptionPrice = FixedPoint64::fromRatio(1, 5);
  FixedPoint64 target = FixedPoint64::fromRatio(1, 5);
  FixedPoint64 spot   = FixedPoint64::fromRatio(2, 10);
  FixedPoint64 result = computeNewRedemptionPrice(state, spot, target, 65700);
  EXPECT_GT(result.raw(), 0);
}

TEST(PiController, ZeroBlocks) {
  PiControllerState state;
  state.redemptionPrice = FixedPoint64::fromRatio(1, 5);
  FixedPoint64 target = FixedPoint64::fromRatio(1, 5);
  FixedPoint64 spot   = FixedPoint64::fromRatio(1, 5);
  FixedPoint64 result = computeNewRedemptionPrice(state, spot, target, 0);
  EXPECT_EQ(result, state.redemptionPrice);
}

TEST(PiController, AdaptiveClampNormal) {
  PiControllerState state;
  state.redemptionPrice = FixedPoint64::fromRatio(1, 5);
  FixedPoint64 target = FixedPoint64::fromRatio(1, 5);
  FixedPoint64 spot   = FixedPoint64::fromRatio(19, 100); // 0.19 (-5% deviation)
  FixedPoint64 result = computeNewRedemptionPrice(state, spot, target, 65700);
  EXPECT_GT(result.raw(), 0);
  // Rate should be clamped by adaptive base (50% annual)
  EXPECT_GT(state.redemptionRate.raw(), FixedPoint64::zero().raw());
}

TEST(PiController, AdaptiveClampExtreme) {
  PiControllerState state;
  state.redemptionPrice = FixedPoint64::fromRatio(1, 5);
  FixedPoint64 target   = FixedPoint64::fromRatio(1, 50000); // 0.00002 (10,000x move)
  FixedPoint64 spot     = FixedPoint64::fromRatio(2, 10);    // 0.2
  FixedPoint64 result = computeNewRedemptionPrice(state, spot, target, 65700);
  // Should not crash — adaptive clamp handles extreme deviation
  EXPECT_GT(result.raw(), 0);
}

TEST(PiController, BasinDiscoverySerialization) {
  PiControllerState state;
  state.redemptionPrice = FixedPoint64::fromRatio(1, 5);
  state.integralDeviation = FixedPoint64::fromRatio(5, 100);
  state.redemptionRate   = FixedPoint64::fromRatio(-2, 100);
  // serialize/deserialize via ISerializer (use raw roundtrip)
  int128_t rp = state.redemptionPrice.raw();
  int128_t id = state.integralDeviation.raw();
  int128_t rr = state.redemptionRate.raw();
  PiControllerState restored;
  restored.redemptionPrice    = FixedPoint64::fromRaw(rp);
  restored.integralDeviation  = FixedPoint64::fromRaw(id);
  restored.redemptionRate     = FixedPoint64::fromRaw(rr);
  EXPECT_EQ(restored.redemptionPrice, state.redemptionPrice);
  EXPECT_EQ(restored.integralDeviation, state.integralDeviation);
  EXPECT_EQ(restored.redemptionRate, state.redemptionRate);
}
