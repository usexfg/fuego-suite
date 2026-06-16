// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See file labeled LICENSE for more details.

#include "BancorCurve.h"
#include "../Serialization/SerializationOverloads.h"

namespace CryptoNote {

void DigmPrimaryPoolState::serialize(ISerializer& s) {
  s(reserveDigm, "reserveDigm");
  s(reserveHeat, "reserveHeat");
  s(totalLpShares, "totalLpShares");
  s(accumulatedLpFees, "accumulatedLpFees");
}

void DigmBancorPoolState::serialize(ISerializer& s) {
  s(reserveXfg, "reserveXfg");
  s(supplyDigm, "supplyDigm");
  s(virtualReserve, "virtualReserve");
  s(virtualSupply, "virtualSupply");
  s(cwNum, "cwNum");
  s(cwDenom, "cwDenom");
}

namespace {

// Fixed-point scale: 10^12 gives ~12 decimal digits of precision
const uint64_t  SCALE       = 1000000000000ULL;
const uint128_t LN2_SCALED  = 693147180560ULL;   // ln(2) * SCALE

// Natural logarithm: compute ln(actual_value) * SCALE
// val is scaled by SCALE (i.e., val = actual_value * SCALE)
// Requires val >= SCALE (actual_value >= 1)
uint128_t fixedLn(uint128_t val) {
  int n = 0;
  while (val >= 2 * SCALE) {
    val >>= 1;
    n++;
  }
  // val in [SCALE, 2*SCALE), actual in [1, 2)

  uint128_t y = val - SCALE;   // y in [0, SCALE)
  uint128_t term = y;           // term_1 = y
  uint128_t ln_part = 0;

  for (int k = 1; k <= 24; ++k) {
    if (k > 1) {
      // term_k = term_{k-1} * y * (k-1) / (k * SCALE)
      term = (term * y * (k - 1)) / ((uint128_t)k * SCALE);
    }
    if (term == 0) break;
    if (k & 1)  ln_part += term;
    else        ln_part -= term;
  }

  return (uint128_t)n * LN2_SCALED + ln_part;
}

// Natural exponent: compute exp(val / SCALE) * SCALE
// val is scaled by SCALE, val >= 0
uint128_t fixedExp(uint128_t val) {
  uint128_t n = val / LN2_SCALED;
  uint128_t r = val - n * LN2_SCALED;

  // return 0 for extremely small exp arguments (underflow)
  if (n == 0 && r == 0) return SCALE;

  uint128_t sum   = SCALE;   // term_0 = 1 * SCALE
  uint128_t term  = SCALE;   // term_0

  for (int k = 1; k <= 30; ++k) {
    term = (term * r) / ((uint128_t)k * SCALE);
    if (term == 0) break;
    sum += term;
  }

  // Multiply by 2^n (n is small in practice, < 10)
  if (n >= 64) return ~(uint128_t)0;
  return sum << (uint8_t)n;
}

// Power: compute base^(num/denom) * SCALE
// base is scaled by SCALE (base = actual_base * SCALE)
uint128_t fixedPow(uint128_t base, uint64_t num, uint64_t denom) {
  uint128_t ln_base = fixedLn(base);
  uint128_t exp_arg = (num * ln_base) / denom;
  return fixedExp(exp_arg);
}

} // namespace

uint64_t bancorComputeVirtualReserve(uint64_t virtualSupply,
                                     uint64_t cwNum,
                                     uint64_t cwDenom,
                                     uint64_t priceNum,
                                     uint64_t priceDenom) {
  // V_r = price * V_s * CW
  uint128_t num = (uint128_t)priceNum * virtualSupply * cwNum;
  uint128_t den = (uint128_t)priceDenom * cwDenom;
  if (den == 0) return 0;
  return (uint64_t)(num / den);
}

uint64_t bancorBuyOutput(uint64_t depositXfg,
                         uint64_t reserveXfg,
                         uint64_t supplyDigm,
                         uint64_t virtualReserve,
                         uint64_t virtualSupply,
                         uint64_t cwNum,
                         uint64_t cwDenom) {
  if (depositXfg == 0 || virtualReserve == 0 || virtualSupply == 0)
    return 0;

  uint64_t effReserve  = virtualReserve + reserveXfg;
  uint64_t effSupply   = virtualSupply + supplyDigm;
  uint64_t newEffRes   = effReserve + depositXfg;

  if (newEffRes == effReserve) return 0;

  // ratio = newEffRes / effReserve (scaled by SCALE)
  uint128_t ratio = ((uint128_t)newEffRes * SCALE) / effReserve;

  // pow_ratio = ratio^(cwNum/cwDenom) * SCALE
  uint128_t pow_res = fixedPow(ratio, cwNum, cwDenom);

  // new supply = effSupply * pow_res / SCALE
  uint128_t newEffSupply = ((uint128_t)effSupply * pow_res) / SCALE;

  if (newEffSupply <= effSupply) return 0;
  return (uint64_t)(newEffSupply - effSupply);
}

uint64_t bancorSellOutput(uint64_t burnDigm,
                          uint64_t reserveXfg,
                          uint64_t supplyDigm,
                          uint64_t virtualReserve,
                          uint64_t virtualSupply,
                          uint64_t cwNum,
                          uint64_t cwDenom) {
  if (burnDigm == 0 || virtualReserve == 0 || virtualSupply == 0)
    return 0;

  uint64_t effSupply  = virtualSupply + supplyDigm;
  uint64_t effReserve = virtualReserve + reserveXfg;

  if (burnDigm >= effSupply) return 0;

  uint64_t newEffSupply = effSupply - burnDigm;
  if (newEffSupply < virtualSupply) return 0;

  // ratio = newEffSupply / effSupply (scaled by SCALE)
  uint128_t ratio = ((uint128_t)newEffSupply * SCALE) / effSupply;

  // pow_ratio = ratio^(cwDenom/cwNum) * SCALE  (1/CW)
  uint128_t pow_res = fixedPow(ratio, cwDenom, cwNum);

  // new eff reserve = effReserve * pow_res / SCALE... wait
  // Actually: R_new = V_r * (S_new / V_s)^(1/CW)
  // But we already have effReserve = V_r * (effSupply/V_s)^(1/CW)
  // And we want R_new = V_r * (newEffSupply/V_s)^(1/CW)
  // = effReserve * (newEffSupply/effSupply)^(1/CW)
  // = effReserve * pow_res / SCALE

  uint128_t newEffReserve = ((uint128_t)effReserve * pow_res) / SCALE;

  if (newEffReserve >= effReserve || newEffReserve < virtualReserve) return 0;
  return (uint64_t)(effReserve - newEffReserve);
}

} // namespace CryptoNote
