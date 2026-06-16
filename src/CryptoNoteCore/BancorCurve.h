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
#include "Common/Int128.h"

namespace CryptoNote {

class ISerializer;

struct DigmPrimaryPoolState {
  uint64_t reserveDigm = 0;
  uint64_t reserveHeat = 0;
  uint64_t totalLpShares = 0;
  uint64_t accumulatedLpFees = 0;

  bool isEmpty() const { return reserveDigm == 0 && reserveHeat == 0; }
  void serialize(ISerializer& s);
};

struct DigmBancorPoolState {
  uint64_t reserveXfg  = 0;   // real XFG reserve (starts at 0)
  uint64_t supplyDigm  = 0;   // real DIGM supply (starts at 0)
  uint64_t virtualReserve = 0; // V_r: virtual XFG for zero-start (pump.fun style)
  uint64_t virtualSupply = 0;  // V_s: virtual DIGM supply baseline
  uint64_t cwNum = 8236;       // connector weight numerator (82.36%)
  uint64_t cwDenom = 10000;     // connector weight denominator

  uint64_t effectiveReserve() const { return virtualReserve + reserveXfg; }
  uint64_t effectiveSupply() const { return virtualSupply + supplyDigm; }
  bool isEmpty() const { return virtualReserve == 0; }
  void serialize(ISerializer& s);
};

// Bancor curve formulas (integer arithmetic, deterministic)
// All atomic units (COIN = 10^7)

// Buy DIGM from Bancor curve: deposit XFG, receive newly minted DIGM
// Returns DIGM atomic units minted (0 on error)
uint64_t bancorBuyOutput(uint64_t depositXfg,
                         uint64_t reserveXfg,
                         uint64_t supplyDigm,
                         uint64_t virtualReserve,
                         uint64_t virtualSupply,
                         uint64_t cwNum,
                         uint64_t cwDenom);

// Sell DIGM to Bancor curve: burn DIGM, receive XFG from reserve
// Returns XFG atomic units returned (0 on error)
uint64_t bancorSellOutput(uint64_t burnDigm,
                          uint64_t reserveXfg,
                          uint64_t supplyDigm,
                          uint64_t virtualReserve,
                          uint64_t virtualSupply,
                          uint64_t cwNum,
                          uint64_t cwDenom);

// Compute virtual reserve needed for zero-start at target price
// priceNum/priceDenom = DIGM price in XFG (e.g. 10/158 for $0.10 / $1.58)
uint64_t bancorComputeVirtualReserve(uint64_t virtualSupply,
                                     uint64_t cwNum,
                                     uint64_t cwDenom,
                                     uint64_t priceNum,
                                     uint64_t priceDenom);

} // namespace CryptoNote
