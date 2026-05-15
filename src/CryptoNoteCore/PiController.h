// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See file labeled LICENSE for more details.

#pragma once

#include <cstdint>
#include "Common/FixedPoint.h"

namespace CryptoNote {

class ISerializer;

enum BasinPhase : uint8_t {
  BASIN_BOOTSTRAP  = 0,  // collecting initial data, no PI
  BASIN_OBSERVING  = 1,  // watching for stability convergence
  BASIN_LOCKED     = 2,  // basin discovered, PI targets center
  BASIN_REOBSERVE  = 3,  // market exited basin, re-observing
};

struct PiControllerState {
  FixedPoint64 redemptionPrice;
  FixedPoint64 integralDeviation;
  FixedPoint64 redemptionRate;

  // Basin discovery — market-validated equilibrium
  uint8_t      basinPhase = BASIN_BOOTSTRAP;
  FixedPoint64 basinCenter;
  FixedPoint64 basinHalfWidth;
  FixedPoint64 basinMinSeen;
  FixedPoint64 basinMaxSeen;
  uint32_t     basinObservedEpochs;
  uint32_t     basinStableEpochs;
  uint32_t     basinExitEpochs;

  // Oracle anchor — snapshot of first valid swapxfg data
  uint64_t     launchOracleValue = 0;

  void serialize(ISerializer& s);
};

FixedPoint64 computeTargetRatio(
    PiControllerState& state,
    uint8_t  stabilityMode,
    FixedPoint64 launchTwap,
    FixedPoint64 currentTwap,
    uint64_t xfgMarketValue);

FixedPoint64 computeNewRedemptionPrice(
    PiControllerState& state,
    FixedPoint64 marketPrice,
    FixedPoint64 targetRatio,
    uint32_t blocksElapsed);

FixedPoint64 computeRebalanceAmount(
    const PiControllerState& state,
    FixedPoint64 currentSpot,
    FixedPoint64 reserveRatio,
    FixedPoint64 treasuryAvailable,
    uint64_t totalLpShares,
    bool& poolIsXfgHeavy);

} // namespace CryptoNote
