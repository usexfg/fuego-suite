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
  FixedPoint64 basinCenter;         // discovered equilibrium (XFG/HEAT)
  FixedPoint64 basinHalfWidth;      // stability half-range
  FixedPoint64 basinMinSeen;        // lowest spot during observation
  FixedPoint64 basinMaxSeen;        // highest spot during observation
  uint32_t     basinObservedEpochs; // epochs in current phase
  uint32_t     basinStableEpochs;   // consecutive stable epochs
  uint32_t     basinExitEpochs;     // consecutive epochs outside basin

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
