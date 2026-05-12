// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.

#include "PiController.h"
#include "CryptoNoteConfig.h"
#include <algorithm>

namespace CryptoNote {

PiController::PiController() = default;

void PiController::calculate(FixedPoint64 marketPrice,
                              uint64_t xfgPerUsd,
                              FixedPoint64& redemptionPrice,
                              FixedPoint64& integralDeviation,
                              FixedPoint64& redemptionRate,
                              uint64_t blocksElapsed) const {
  if (redemptionPrice.isZero() || blocksElapsed == 0) {
    redemptionRate = FixedPoint64::zero();
    return;
  }

  FixedPoint64 deviation;

  if (xfgPerUsd > 0) {
    // USD-targeting mode:
    //   heatUsdPrice = marketPrice (XFG/HEAT) / xfgPerUsd (XFG/$1)
    //   → this gives HEAT price in USD (where $1 = ONE)
    //   deviation = (heatUsdPrice − $1.00) / $1.00
    FixedPoint64 xfgPerUsdFp = FixedPoint64::fromUint64(xfgPerUsd);
    FixedPoint64 heatUsdPrice = marketPrice.div(xfgPerUsdFp); // XFG/HEAT ÷ XFG/$1 = $/HEAT
    FixedPoint64 targetUsd = FixedPoint64::one();              // $1.00

    if (heatUsdPrice.isZero()) {
      redemptionRate = FixedPoint64::zero();
      return;
    }
    deviation = heatUsdPrice.sub(targetUsd).div(targetUsd);
  } else {
    // XFG-only mode (no oracle):
    //   deviation = (marketPrice − redemptionPrice) / redemptionPrice
    //   HEAT tracks XFG value
    deviation = marketPrice.sub(redemptionPrice).div(redemptionPrice);
  }

  // Kp and Ki in Q64.64
  FixedPoint64 kp = FixedPoint64::fromRatio(parameters::PI_KP_NUM, parameters::PI_KP_DENOM);
  FixedPoint64 ki = FixedPoint64::fromRatio(parameters::PI_KI_NUM, parameters::PI_KI_DENOM);

  // Accumulate integral deviation with wind-up clamp
  integralDeviation = integralDeviation.add(deviation);
  FixedPoint64 clamp = FixedPoint64::fromRatio(parameters::PI_INTEGRAL_CLAMP, 100);
  FixedPoint64 negClamp = clamp.negate();
  if (integralDeviation > clamp)  integralDeviation = clamp;
  if (integralDeviation < negClamp) integralDeviation = negClamp;

  // RedemptionRate = Kp * Deviation + Ki * IntegralDeviation
  redemptionRate = kp.mul(deviation).add(ki.mul(integralDeviation));

  // Clamp rate
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
