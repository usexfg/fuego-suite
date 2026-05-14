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

  // ── Priority 1: Basin locked → discovered equilibrium ──
  if (stabilityMode != 0 && state.basinPhase == BASIN_LOCKED && !state.basinCenter.isZero()) {
    return state.basinCenter;
  }

  // ── Priority 2: Oracle active → value-band ──
  if (stabilityMode >= 1 && xfgMarketValue > 0) {
    FixedPoint64 xfgMarket = FixedPoint64::fromRatio(xfgMarketValue, parameters::VALUE_SCALE);
    if (!xfgMarket.isZero()) {
      FixedPoint64 heatValue = currentTwap.mul(xfgMarket);
      FixedPoint64 floorV    = FixedPoint64::fromRatio(parameters::HEAT_VALUE_FLOOR,   parameters::VALUE_SCALE);
      FixedPoint64 ceilV     = FixedPoint64::fromRatio(parameters::HEAT_VALUE_CEILING, parameters::VALUE_SCALE);
      FixedPoint64 targetV;
      if      (heatValue < floorV) targetV = floorV;
      else if (heatValue > ceilV)  targetV = ceilV;
      else                          targetV = heatValue;
      return targetV.div(xfgMarket);
    }
    if (stabilityMode == 1)
      return FixedPoint64::fromRatio(
        parameters::HEAT_LAUNCH_RATIO_NUM, parameters::HEAT_LAUNCH_RATIO_DENOM);
  }

  // ── Priority 2b: Activity-anchored (Mode 3) — on-chain metrics, no oracle ──
  if (stabilityMode == 3 && xfgMarketValue > 0) {
    FixedPoint64 activityRatio = FixedPoint64::fromRatio(xfgMarketValue, parameters::VALUE_SCALE);
    if (!activityRatio.isZero() && !currentTwap.isZero()) {
      FixedPoint64 launchRatio = FixedPoint64::fromRatio(
        parameters::HEAT_LAUNCH_RATIO_NUM, parameters::HEAT_LAUNCH_RATIO_DENOM);
      return launchRatio.mul(activityRatio);
    }
  }

  // ── Priority 3: Launch TWAP → self-referencing formula ──
  if (!currentTwap.isZero() && !launchTwap.isZero()) {
    FixedPoint64 launchRatio = FixedPoint64::fromRatio(
      parameters::HEAT_LAUNCH_RATIO_NUM, parameters::HEAT_LAUNCH_RATIO_DENOM);
    return launchRatio.mul(launchTwap).div(currentTwap);
  }

  // ── Priority 4: Bootstrap ──
  return FixedPoint64::fromRatio(
    parameters::HEAT_LAUNCH_RATIO_NUM, parameters::HEAT_LAUNCH_RATIO_DENOM);
}

// ---------------------------------------------------------------------------
// PI controller — adjusts redemption price toward target
// ---------------------------------------------------------------------------
FixedPoint64 computeNewRedemptionPrice(
    PiControllerState& state,
    FixedPoint64 marketPrice,
    FixedPoint64 targetRatio,
    uint32_t blocksElapsed) {

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
  FixedPoint64 multiplier = FixedPoint64::one().add(adjustment);
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
  if (s.type() == ISerializer::INPUT) {
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
