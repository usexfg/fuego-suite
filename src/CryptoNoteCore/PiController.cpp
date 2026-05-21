// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.

#include "PiController.h"
#include "CryptoNoteConfig.h"
#include "../Serialization/SerializationOverloads.h"

namespace CryptoNote {

// ---------------------------------------------------------------------------
// Basin discovery — market-validated equilibrium
// ---------------------------------------------------------------------------
static void updateBasin(PiControllerState& state, FixedPoint64 currentSpot) {
  switch (state.basinPhase) {

  case BASIN_BOOTSTRAP: {
    if (state.basinObservedEpochs == 0) {
      state.basinMinSeen = currentSpot;
      state.basinMaxSeen = currentSpot;
    } else {
      if (currentSpot < state.basinMinSeen) state.basinMinSeen = currentSpot;
      if (currentSpot > state.basinMaxSeen) state.basinMaxSeen = currentSpot;
    }
    state.basinObservedEpochs++;
    if (state.basinObservedEpochs >= parameters::BASIN_BOOTSTRAP_EPOCHS) {
      state.basinPhase = BASIN_OBSERVING;
      state.basinStableEpochs = 0;
    }
    break;
  }

  case BASIN_OBSERVING: {
    if (currentSpot < state.basinMinSeen) state.basinMinSeen = currentSpot;
    if (currentSpot > state.basinMaxSeen) state.basinMaxSeen = currentSpot;

    FixedPoint64 avgSpot = state.basinMinSeen.add(state.basinMaxSeen)
                           .div(FixedPoint64::fromUint64(2));
    FixedPoint64 range = state.basinMaxSeen.sub(state.basinMinSeen);
    FixedPoint64 threshold = avgSpot.mul(
      FixedPoint64::fromRatio(parameters::BASIN_STABILITY_RANGE, 100));

    if (!avgSpot.isZero() && range < threshold) {
      state.basinStableEpochs++;
    } else {
      state.basinStableEpochs = 0;
    }

    state.basinObservedEpochs++;

    if (state.basinStableEpochs >= parameters::BASIN_STABLE_REQUIRED) {
      state.basinCenter    = avgSpot;
      state.basinHalfWidth = range.div(FixedPoint64::fromUint64(2));
      if (state.basinHalfWidth.isZero())
        state.basinHalfWidth = avgSpot.mul(
          FixedPoint64::fromRatio(parameters::BASIN_STABILITY_RANGE, 200));
      state.basinPhase     = BASIN_LOCKED;
      state.basinExitEpochs = 0;
    }
    break;
  }

  case BASIN_LOCKED: {
    FixedPoint64 upper = state.basinCenter.add(state.basinHalfWidth);
    FixedPoint64 lower = state.basinCenter.sub(state.basinHalfWidth);

    if (currentSpot > upper || currentSpot < lower) {
      state.basinExitEpochs++;
      if (state.basinExitEpochs >= parameters::BASIN_EXIT_THRESHOLD) {
        state.basinPhase   = BASIN_REOBSERVE;
        state.basinMinSeen = currentSpot;
        state.basinMaxSeen = currentSpot;
        state.basinObservedEpochs = 0;
        state.basinStableEpochs  = 0;
      }
    } else {
      state.basinExitEpochs = 0;
    }
    break;
  }

  case BASIN_REOBSERVE: {
    state.basinPhase  = BASIN_OBSERVING;
    state.basinMinSeen = currentSpot < state.basinMinSeen ? currentSpot : state.basinMinSeen;
    state.basinMaxSeen = currentSpot > state.basinMaxSeen ? currentSpot : state.basinMaxSeen;
    state.basinObservedEpochs  = 1;
    state.basinStableEpochs   = 0;
    state.basinExitEpochs      = 0;
    break;
  }

  }
}

// ---------------------------------------------------------------------------
// Target ratio — dynamic priority model
// Auto-selects best anchor based on available data:
//   1. Basin locked    → discovered equilibrium (market-validated)
//   2. Oracle active   → value-band with swapxfg data
//   3. Launch TWAP     → self-referencing formula (0.2 × launch / current)
//   4. Bootstrap       → fixed 0.2
// Mode 0 = launch-ratio only (skip 1+2). Mode 1 = oracle only. Mode 2 = full auto.
// ---------------------------------------------------------------------------
FixedPoint64 computeTargetRatio(
    PiControllerState& state,
    uint8_t  stabilityMode,
    FixedPoint64 launchTwap,
    FixedPoint64 currentTwap,
    uint64_t xfgMarketValue) {

  updateBasin(state, currentTwap);

  switch (stabilityMode) {

  case 0: // CPI-adjusted purchasing power + EUR display
    // Band: $1.50-$2.50 × CPI_current/CPI_launch. CPI auto-inflation drifts band upward.
    // Falls back to bootstrap 5:1 when CPI inactive or XFG below threshold.
    if (state.cpiOracleActive
        && xfgMarketValue >= parameters::XFG_PRICE_ACTIVATION_THRESHOLD) {
      FixedPoint64 cpiRatio = state.cpiCurrentValue.div(state.cpiLaunchValue);
      FixedPoint64 baseFloor = FixedPoint64::fromRatio(parameters::HEAT_CPI_BASE_FLOOR, parameters::VALUE_SCALE);
      FixedPoint64 baseCeil  = FixedPoint64::fromRatio(parameters::HEAT_CPI_BASE_CEIL,  parameters::VALUE_SCALE);
      FixedPoint64 floorCPI = baseFloor.mul(cpiRatio);
      FixedPoint64 ceilCPI  = baseCeil.mul(cpiRatio);
      FixedPoint64 xfgPrice = FixedPoint64::fromRatio(xfgMarketValue, parameters::VALUE_SCALE);
      if (!xfgPrice.isZero()) {
        FixedPoint64 heatValue = currentTwap.mul(xfgPrice);
        FixedPoint64 targetValue;
        if      (heatValue < floorCPI) targetValue = floorCPI;
        else if (heatValue > ceilCPI)  targetValue = ceilCPI;
        else                            targetValue = heatValue;
        return targetValue.div(xfgPrice);
      }
    }
    // Mode 0 bootstrap: 5:1 launch ratio
    return FixedPoint64::fromRatio(
      parameters::HEAT_LAUNCH_RATIO_NUM, parameters::HEAT_LAUNCH_RATIO_DENOM);

  case 1: // 5:1 self-sovereign (fixed $1.50-$2.50 band, activate at XFG ≥ $5)
    if (xfgMarketValue >= parameters::XFG_PRICE_ACTIVATION_THRESHOLD) {
      FixedPoint64 xfgPrice = FixedPoint64::fromRatio(xfgMarketValue, parameters::VALUE_SCALE);
      if (!xfgPrice.isZero()) {
        FixedPoint64 heatValue = currentTwap.mul(xfgPrice);
        FixedPoint64 floorV  = FixedPoint64::fromRatio(parameters::HEAT_VALUE_FLOOR,   parameters::VALUE_SCALE);
        FixedPoint64 ceilV   = FixedPoint64::fromRatio(parameters::HEAT_VALUE_CEILING, parameters::VALUE_SCALE);
        FixedPoint64 targetValue;
        if      (heatValue < floorV) targetValue = floorV;
        else if (heatValue > ceilV)  targetValue = ceilV;
        else                          targetValue = heatValue;
        return targetValue.div(xfgPrice);
      }
    } else if (!launchTwap.isZero() && !currentTwap.isZero()) {
      // Self-referencing: 0.2 × launch_twap / current_twap
      FixedPoint64 launchRatio = FixedPoint64::fromRatio(
        parameters::HEAT_LAUNCH_RATIO_NUM, parameters::HEAT_LAUNCH_RATIO_DENOM);
      return launchRatio.mul(launchTwap.div(currentTwap));
    }
    // Bootstrap: 5:1 launch ratio
    return FixedPoint64::fromRatio(
      parameters::HEAT_LAUNCH_RATIO_NUM, parameters::HEAT_LAUNCH_RATIO_DENOM);

  case 2: // 8:1 full float (PI-only, no band — best APY per Monte Carlo)
    // Self-referencing: 0.125 × launch_twap / current_twap
    if (!launchTwap.isZero() && !currentTwap.isZero()) {
      FixedPoint64 launch8 = FixedPoint64::fromRatio(
        parameters::HEAT_LAUNCH_RATIO_8X_NUM, parameters::HEAT_LAUNCH_RATIO_8X_DENOM);
      return launch8.mul(launchTwap.div(currentTwap));
    }
    // Bootstrap: 8:1 launch ratio
    return FixedPoint64::fromRatio(
      parameters::HEAT_LAUNCH_RATIO_8X_NUM, parameters::HEAT_LAUNCH_RATIO_8X_DENOM);

  default:
    return FixedPoint64::fromRatio(
      parameters::HEAT_LAUNCH_RATIO_NUM, parameters::HEAT_LAUNCH_RATIO_DENOM);
  }
}

// ---------------------------------------------------------------------------
// PI controller — adjusts redemption price toward target
// ---------------------------------------------------------------------------
FixedPoint64 computeNewRedemptionPrice(
    PiControllerState& state,
    FixedPoint64 marketPrice,
    FixedPoint64 targetRatio,
    uint32_t blocksElapsed,
    uint8_t  stabilityMode) {

  if (state.redemptionPrice.isZero() || blocksElapsed == 0 || targetRatio.isZero())
    return state.redemptionPrice;

  FixedPoint64 kp = FixedPoint64::fromRatio(parameters::PI_KP_NUM, parameters::PI_KP_DENOM);
  FixedPoint64 ki = FixedPoint64::fromRatio(parameters::PI_KI_NUM, parameters::PI_KI_DENOM);

  FixedPoint64 deviation = marketPrice.sub(targetRatio).div(targetRatio);
  FixedPoint64 blocksFrac = FixedPoint64::fromRatio(blocksElapsed, parameters::BLOCKS_PER_YEAR);

  FixedPoint64 newIntegral = state.integralDeviation.add(deviation.mul(blocksFrac));
  FixedPoint64 integralClamp = FixedPoint64::fromRatio(parameters::PI_INTEGRAL_CLAMP, 100);
  FixedPoint64 negIntegralClamp = integralClamp.negate();
  if (newIntegral > integralClamp)  newIntegral = integralClamp;
  if (newIntegral < negIntegralClamp) newIntegral = negIntegralClamp;
  state.integralDeviation = newIntegral;

  FixedPoint64 rate = kp.mul(deviation).add(ki.mul(state.integralDeviation));

  // Hill damping (sigmoid-like soft-band for float modes):
  // damp = (|dev|/M)^n / (1 + (|dev|/M)^n)
  // PI runs at full power near target, fades smoothly at extremes.
  // Silent under normal conditions — only activates during black swan events.
  if (parameters::HEAT_PI_USE_DAMP && stabilityMode != 0) {
    FixedPoint64 absDev = deviation.isNegative() ? deviation.negate() : deviation;
    FixedPoint64 M = FixedPoint64::fromRatio(parameters::HEAT_PI_DAMP_M, 100);
    FixedPoint64 ratio = absDev.div(M);
    FixedPoint64 ratioN = ratio;
    for (uint32_t i = 1; i < parameters::HEAT_PI_DAMP_N; ++i)
      ratioN = ratioN.mul(ratio);
    FixedPoint64 one = FixedPoint64::one();
    FixedPoint64 damp = ratioN.div(one.add(ratioN));
    rate = rate.mul(one.sub(damp));
  }

  // Adaptive rate clamp: scales with |deviation| up to absolute ceiling.
  // Basin locked (small dev): clamp = ±50%/yr. Regime change (large dev): up to ±1000%/yr.
  FixedPoint64 baseRate = FixedPoint64::fromRatio(
    parameters::PI_BASE_RATE_NUM, parameters::PI_BASE_RATE_DENOM);
  FixedPoint64 absMaxRate = FixedPoint64::fromRatio(
    parameters::PI_ABS_MAX_RATE, 100);
  FixedPoint64 devMagnitude = deviation.isNegative() ? deviation.negate() : deviation;
  FixedPoint64 adaptiveCap = devMagnitude.mul(FixedPoint64::fromRatio(5, 10));
  if (adaptiveCap > absMaxRate) adaptiveCap = absMaxRate;
  if (adaptiveCap < baseRate)   adaptiveCap = baseRate;
  FixedPoint64 maxRate    = adaptiveCap;
  FixedPoint64 negMaxRate = maxRate.negate();

  if (rate > maxRate) rate = maxRate;
  if (rate < negMaxRate) rate = negMaxRate;
  state.redemptionRate = rate;

  FixedPoint64 adjustment = rate.mul(blocksFrac);
  FixedPoint64 multiplier = FixedPoint64::one().sub(adjustment);
  FixedPoint64 newPrice = targetRatio.mul(multiplier);

  FixedPoint64 minPrice = FixedPoint64::fromRatio(1, 1000000);
  if (newPrice < minPrice) newPrice = minPrice;

  state.redemptionPrice = newPrice;
  return newPrice;
}

// ---------------------------------------------------------------------------
// Pool rebalancing — protocol-only single-sided LP add
// Only when pool spot deviates beyond basin rebalance band
// ---------------------------------------------------------------------------
FixedPoint64 computeRebalanceAmount(
    const PiControllerState& state,
    FixedPoint64 currentSpot,
    FixedPoint64 reserveRatio,
    FixedPoint64 treasuryAvailable,
    uint64_t totalLpShares,
    bool& poolIsXfgHeavy) {

  poolIsXfgHeavy = false;
  FixedPoint64 zero;

  if (state.basinPhase != BASIN_LOCKED || state.basinCenter.isZero())
    return zero;

  // Rebalance band: basinCenter ± halfWidth × REBALANCE_MULT
  FixedPoint64 rebalanceBand = state.basinHalfWidth.mul(
    FixedPoint64::fromRatio(parameters::BASIN_REBALANCE_MULT, 100));
  FixedPoint64 upperBand = state.basinCenter.add(rebalanceBand);
  FixedPoint64 lowerBand = state.basinCenter.sub(rebalanceBand).isNegative() ?
    FixedPoint64::zero() : state.basinCenter.sub(rebalanceBand);

  // Within float zone — no rebalance
  if (currentSpot >= lowerBand && currentSpot <= upperBand)
    return zero;

  // Cap: don't exceed protocol LP fraction or treasury fraction
  FixedPoint64 maxFraction = FixedPoint64::fromRatio(
    parameters::PROTOCOL_REBALANCE_MAX, 100);
  FixedPoint64 maxDeposit = treasuryAvailable.mul(maxFraction);
  FixedPoint64 protocolLpCap = FixedPoint64::fromUint64(totalLpShares).mul(
    FixedPoint64::fromRatio(parameters::PROTOCOL_LP_MAX_FRACTION, 100));

  // Push strength proportional to distance from band
  FixedPoint64 deviation;
  if (currentSpot > upperBand) {
    // XFG-heavy pool: protocol adds HEAT single-sided
    poolIsXfgHeavy = true;
    deviation = currentSpot.sub(upperBand).div(upperBand);
  } else {
    // HEAT-heavy pool: protocol adds XFG single-sided
    deviation = lowerBand.sub(currentSpot).div(lowerBand);
  }

  // clamp push to [0,1]
  if (deviation.isNegative()) deviation = zero;
  FixedPoint64 one = FixedPoint64::one();
  if (deviation > one) deviation = one;

  FixedPoint64 amount = maxDeposit.mul(deviation);
  if (amount.isNegative()) amount = zero;

  return amount;
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------
void PiControllerState::serialize(ISerializer& s) {
  int128_t rp = redemptionPrice.raw();
  int128_t id = integralDeviation.raw();
  int128_t rr = redemptionRate.raw();
  int128_t bc = basinCenter.raw();
  int128_t bw = basinHalfWidth.raw();
  int128_t bmin = basinMinSeen.raw();
  int128_t bmax = basinMaxSeen.raw();
  s.binary(&rp, sizeof(rp), "redemptionPrice");
  s.binary(&id, sizeof(id), "integralDeviation");
  s.binary(&rr, sizeof(rr), "redemptionRate");
  s.binary(&bc, sizeof(bc), "basinCenter");
  s.binary(&bw, sizeof(bw), "basinHalfWidth");
  s.binary(&bmin, sizeof(bmin), "basinMinSeen");
  s.binary(&bmax, sizeof(bmax), "basinMaxSeen");
  s(basinPhase, "basinPhase");
  s(basinObservedEpochs, "basinObservedEpochs");
  s(basinStableEpochs, "basinStableEpochs");
  s(basinExitEpochs, "basinExitEpochs");
  s(launchOracleValue, "launchOracleValue");
  int128_t cv = cpiLaunchValue.raw();
  int128_t cw = cpiCurrentValue.raw();
  s.binary(&cv, sizeof(cv), "cpiLaunchValue");
  s.binary(&cw, sizeof(cw), "cpiCurrentValue");
  s(cpiUpdateHeight, "cpiUpdateHeight");
  s(cpiOracleActive, "cpiOracleActive");
  if (s.type() == ISerializer::INPUT) {
    cpiLaunchValue  = FixedPoint64::fromRaw(cv);
    cpiCurrentValue = FixedPoint64::fromRaw(cw);
    redemptionPrice   = FixedPoint64::fromRaw(rp);
    integralDeviation = FixedPoint64::fromRaw(id);
    redemptionRate    = FixedPoint64::fromRaw(rr);
    basinCenter       = FixedPoint64::fromRaw(bc);
    basinHalfWidth    = FixedPoint64::fromRaw(bw);
    basinMinSeen      = FixedPoint64::fromRaw(bmin);
    basinMaxSeen      = FixedPoint64::fromRaw(bmax);
  }
}

} // namespace CryptoNote
