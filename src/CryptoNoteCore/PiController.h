// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See file labeled LICENSE for more details.

#pragma once

#include "Common/FixedPoint.h"

namespace CryptoNote {

// PI Controller: adjusts HEAT redemption price based on price deviation.
// Runs at every epoch boundary. Uses Q64.64 arithmetic for determinism.
//
// Two modes:
//   xfgPerUsd == 0: targets XFG parity (HEAT tracks XFG value)
//   xfgPerUsd  > 0: targets $1.00 USD (HEAT stable in fiat terms)

class PiController {
public:
  PiController();

  // marketPrice:   Hearth spot price (XFG per HEAT, Q64.64)
  // xfgPerUsd:     Oracle rate (XFG atomic units per $1, 0 = XFG-only mode)
  // redemptionPrice, integralDeviation, redemptionRate: in/out state
  void calculate(FixedPoint64 marketPrice,
                 uint64_t xfgPerUsd,
                 FixedPoint64& redemptionPrice,
                 FixedPoint64& integralDeviation,
                 FixedPoint64& redemptionRate,
                 uint64_t blocksElapsed) const;
};

} // namespace CryptoNote
