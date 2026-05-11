// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.

#include "PiController.h"
#include "CryptoNoteConfig.h"
#include <algorithm>

namespace CryptoNote {

PiController::PiController() = default;

void PiController::calculate(FixedPoint64 marketPrice,
                              FixedPoint64& redemptionPrice,
                              FixedPoint64& integralError,
                              FixedPoint64& redemptionRate,
                              uint64_t blocksElapsed) const {
  if (redemptionPrice.isZero() || blocksElapsed == 0) {
    redemptionRate = FixedPoint64::zero();
    return;
  }

  // Error = (MarketPrice - RedemptionPrice) / RedemptionPrice
  FixedPoint64 error = marketPrice.sub(redemptionPrice).div(redemptionPrice);

  // Kp and Ki in Q64.64
  FixedPoint64 kp = FixedPoint64::fromRatio(parameters::PI_KP_NUM, parameters::PI_KP_DENOM);
  FixedPoint64 ki = FixedPoint64::fromRatio(parameters::PI_KI_NUM, parameters::PI_KI_DENOM);

  // Accumulate integral error with wind-up clamp
  integralError = integralError.add(error);
  FixedPoint64 clamp = FixedPoint64::fromRatio(parameters::PI_INTEGRAL_CLAMP, 100);
  FixedPoint64 negClamp = clamp.negate();
  if (integralError > clamp)  integralError = clamp;
  if (integralError < negClamp) integralError = negClamp;

  // RedemptionRate = Kp * Error + Ki * IntegralError
  redemptionRate = kp.mul(error).add(ki.mul(integralError));

  // Clamp rate to ±max rate
  FixedPoint64 maxRate = FixedPoint64::fromRatio(parameters::PI_MAX_RATE_NUM, parameters::PI_MAX_RATE_DENOM);
  if (redemptionRate > maxRate) redemptionRate = maxRate;
  FixedPoint64 negMax = maxRate.negate();
  if (redemptionRate < negMax) redemptionRate = negMax;

  // NewRedemptionPrice = RedemptionPrice * (1 + RedemptionRate * BlocksElapsed / BlocksPerYear)
  FixedPoint64 blocksFrac = FixedPoint64::fromRatio(blocksElapsed, parameters::BLOCKS_PER_YEAR);
  FixedPoint64 adjustment = redemptionRate.mul(blocksFrac);
  FixedPoint64 multiplier = FixedPoint64::one().add(adjustment);

  redemptionPrice = redemptionPrice.mul(multiplier);

  // Floor at minimum price
  FixedPoint64 minPrice = FixedPoint64::fromRatio(1, 1000000); // 1e-6
  if (redemptionPrice < minPrice) redemptionPrice = minPrice;
}

} // namespace CryptoNote
