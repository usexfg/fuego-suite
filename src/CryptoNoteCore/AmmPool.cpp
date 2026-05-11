// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See file labeled LICENSE for more details.

#include "AmmPool.h"
#include <cmath>
#include <algorithm>

namespace CryptoNote {

namespace {
  const uint64_t FEE_DIVISOR = 10000; // 1% = 100 bps

  // 128-bit integer sqrt via Newton's method
  uint64_t isqrt128(unsigned __int128 n) {
    if (n <= 1) return (uint64_t)n;
    unsigned __int128 x = n;
    unsigned __int128 y = (x + 1) >> 1;
    while (y < x) {
      x = y;
      y = (x + n / x) >> 1;
    }
    return (uint64_t)x;
  }
} // anonymous

uint64_t ammGetOutputAmount(uint64_t inputAmount,
                             uint64_t reserveIn,
                             uint64_t reserveOut,
                             uint32_t feeBps) {
  if (reserveIn == 0 || reserveOut == 0 || inputAmount == 0)
    return 0;

  uint64_t feeAdj = FEE_DIVISOR - feeBps;
  // output = reserveOut * input * feeAdj / (reserveIn * FEE_DIVISOR + input * feeAdj)
  unsigned __int128 num = (unsigned __int128)reserveOut * inputAmount * feeAdj;
  unsigned __int128 den = (unsigned __int128)reserveIn * FEE_DIVISOR + (unsigned __int128)inputAmount * feeAdj;
  return (uint64_t)(num / den);
}

uint64_t ammGetInputAmount(uint64_t outputAmount,
                            uint64_t reserveIn,
                            uint64_t reserveOut,
                            uint32_t feeBps) {
  if (reserveIn == 0 || reserveOut == 0 || outputAmount >= reserveOut)
    return 0;

  uint64_t feeAdj = FEE_DIVISOR - feeBps;
  // input = reserveIn * output * FEE_DIVISOR / ((reserveOut - output) * feeAdj)
  unsigned __int128 num = (unsigned __int128)reserveIn * outputAmount * FEE_DIVISOR;
  unsigned __int128 den = (unsigned __int128)(reserveOut - outputAmount) * feeAdj;
  return (uint64_t)(num / den);
}

uint64_t ammGetSpotPrice(uint64_t reserveA, uint64_t reserveB) {
  if (reserveB == 0) return 0;
  // price = reserveA / reserveB, scaled by 1e18
  unsigned __int128 scaled = (unsigned __int128)reserveA * 1000000000000000000ULL;
  return (uint64_t)(scaled / reserveB);
}

uint64_t ammMintLpShares(uint64_t amountA, uint64_t amountB,
                          uint64_t totalShares,
                          uint64_t reserveA, uint64_t reserveB) {
  if (totalShares == 0) {
    // Initial mint: sqrt(amountA * amountB) minus minimum liquidity
    unsigned __int128 product = (unsigned __int128)amountA * amountB;
    uint64_t shares = isqrt128(product);
    const uint64_t MIN_LIQUIDITY = 1000;
    return shares > MIN_LIQUIDITY ? shares - MIN_LIQUIDITY : 0;
  }

  // Proportional mint
  unsigned __int128 sharesA = (unsigned __int128)amountA * totalShares / reserveA;
  unsigned __int128 sharesB = (unsigned __int128)amountB * totalShares / reserveB;
  return (uint64_t)std::min(sharesA, sharesB);
}

void ammGetWithdrawalAmounts(uint64_t lpSharesBurned,
                              uint64_t totalShares,
                              uint64_t reserveA, uint64_t reserveB,
                              uint64_t& amountA, uint64_t& amountB) {
  if (totalShares == 0) {
    amountA = 0; amountB = 0;
    return;
  }
  amountA = (uint64_t)((unsigned __int128)lpSharesBurned * reserveA / totalShares);
  amountB = (uint64_t)((unsigned __int128)lpSharesBurned * reserveB / totalShares);
}

bool ammValidateSwap(uint64_t input, uint64_t output,
                     uint64_t reserveIn, uint64_t reserveOut,
                     uint32_t feeBps) {
  if (input == 0 || reserveIn == 0 || reserveOut == 0)
    return false;

  uint64_t expected = ammGetOutputAmount(input, reserveIn, reserveOut, feeBps);
  return output >= expected; // allow rounding tolerance in user's favor
}

bool ammValidateDepositRatio(uint64_t amountA, uint64_t amountB,
                              uint64_t reserveA, uint64_t reserveB,
                              uint32_t toleranceBps) {
  if (reserveA == 0 || reserveB == 0) {
    // Empty pool: any ratio accepted
    return amountA > 0 && amountB > 0;
  }

  // Check amountA / amountB is within tolerance of reserveA / reserveB
  unsigned __int128 expectedRatio = (unsigned __int128)amountA * reserveB;
  unsigned __int128 actualRatio   = (unsigned __int128)amountB * reserveA;
  unsigned __int128 delta = (expectedRatio > actualRatio) ?
    (expectedRatio - actualRatio) : (actualRatio - expectedRatio);

  // tolerance in basis points of expectedRatio
  unsigned __int128 maxDelta = expectedRatio * toleranceBps / FEE_DIVISOR;
  return delta <= maxDelta;
}

} // namespace CryptoNote
