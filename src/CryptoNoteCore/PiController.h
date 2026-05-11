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

// PI Controller: adjusts HEAT redemption price based on market price deviation.
// Runs at every epoch boundary. Uses Q64.64 arithmetic for determinism.

class PiController {
public:
  PiController();

  // Calculate new redemption rate and price.
  // marketPrice:     spot price from Hearth (XFG per HEAT, Q64.64)
  // redemptionPrice: current redemption price (Q64.64)
  // integralError:   accumulated error (in/out, Q64.64)
  // blocksElapsed:   blocks since last update
  // Outputs: updated integralError, redemptionRate, and new redemptionPrice.
  void calculate(FixedPoint64 marketPrice,
                 FixedPoint64& redemptionPrice,
                 FixedPoint64& integralError,
                 FixedPoint64& redemptionRate,
                 uint64_t blocksElapsed) const;
};

} // namespace CryptoNote
