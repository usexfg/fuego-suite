// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You can redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation. Fuego includes elements written
// by third parties. See file labeled LICENSE for more details.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

#include "PoolAMM.h"
#include <algorithm>

namespace XfgSwap {

static constexpr uint64_t PRICE_SCALE = 1000000000000000000ULL; // 1e18
static constexpr uint64_t FEE_DIVISOR = 10000;

namespace {
  // Integer square root for 128-bit values — deterministic, no floating point.
  // Matches the consensus-layer isqrt128 in AmmPool.cpp.
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
}

uint64_t poolGetOutputAmount(uint64_t inputAmount,
                              uint64_t reserveIn,
                              uint64_t reserveOut,
                              uint32_t feeBps) {
  if (inputAmount == 0 || reserveIn == 0 || reserveOut == 0) {
    return 0;
  }

  uint64_t feeAdj = FEE_DIVISOR - feeBps;
  unsigned __int128 num = (unsigned __int128)reserveOut * inputAmount * feeAdj;
  unsigned __int128 den = (unsigned __int128)reserveIn * FEE_DIVISOR + (unsigned __int128)inputAmount * feeAdj;

  if (den == 0) {
    return 0;
  }

  return (uint64_t)(num / den);
}

uint64_t poolGetInputAmount(uint64_t outputAmount,
                             uint64_t reserveIn,
                             uint64_t reserveOut,
                             uint32_t feeBps) {
  if (outputAmount == 0 || reserveIn == 0 || reserveOut == 0) {
    return 0;
  }

  if (outputAmount >= reserveOut) {
    return 0; // Cannot output more than reserve
  }

  uint64_t feeAdj = FEE_DIVISOR - feeBps;
  unsigned __int128 num = (unsigned __int128)reserveIn * outputAmount * FEE_DIVISOR;
  unsigned __int128 den = (unsigned __int128)(reserveOut - outputAmount) * feeAdj;

  if (den == 0) {
    return 0;
  }

  // Add 1 to round up (input must be sufficient)
  return (uint64_t)(num / den) + 1;
}

uint64_t poolMintLPShares(uint64_t amountA,
                           uint64_t amountB,
                           uint64_t totalShares,
                           uint64_t reserveA,
                           uint64_t reserveB) {
  if (amountA == 0 || amountB == 0) {
    return 0;
  }

  if (totalShares == 0) {
    // Initial liquidity: isqrt(amountA * amountB) - MIN_INITIAL_LIQUIDITY
    // Uses deterministic integer sqrt — no floating point.
    unsigned __int128 product = (unsigned __int128)amountA * amountB;
    uint64_t liquidity = isqrt128(product);

    if (liquidity <= MIN_INITIAL_LIQUIDITY) {
      return 0; // Dust protection
    }

    return liquidity - MIN_INITIAL_LIQUIDITY;
  }

  // Proportional minting: min of both ratios (128-bit intermediates)
  uint64_t sharesA = (uint64_t)((unsigned __int128)amountA * totalShares / reserveA);
  uint64_t sharesB = (uint64_t)((unsigned __int128)amountB * totalShares / reserveB);

  return std::min(sharesA, sharesB);
}

WithdrawalAmounts poolGetWithdrawalAmounts(uint64_t burnAmount,
                                            uint64_t totalShares,
                                            uint64_t reserveA,
                                            uint64_t reserveB,
                                            uint64_t feeAccumulatorA,
                                            uint64_t feeAccumulatorB) {
  WithdrawalAmounts result = {};

  if (burnAmount == 0 || totalShares == 0) {
    return result;
  }

  if (burnAmount > totalShares) {
    burnAmount = totalShares;
  }

  // Proportional share of reserves (128-bit intermediates)
  result.amountA = (uint64_t)((unsigned __int128)burnAmount * reserveA / totalShares);
  result.amountB = (uint64_t)((unsigned __int128)burnAmount * reserveB / totalShares);

  // Proportional share of accrued fees (128-bit intermediates)
  result.feeA = (uint64_t)((unsigned __int128)burnAmount * feeAccumulatorA / totalShares);
  result.feeB = (uint64_t)((unsigned __int128)burnAmount * feeAccumulatorB / totalShares);

  return result;
}

uint64_t poolGetSpotPrice(uint64_t reserveA, uint64_t reserveB) {
  if (reserveA == 0) {
    return 0;
  }

  return (uint64_t)((unsigned __int128)reserveB * PRICE_SCALE / reserveA);
}

uint64_t poolGetEffectivePrice(uint64_t inputAmount,
                                uint64_t reserveIn,
                                uint64_t reserveOut,
                                uint32_t feeBps) {
  if (inputAmount == 0 || reserveIn == 0) {
    return 0;
  }

  uint64_t outputAmount = poolGetOutputAmount(inputAmount, reserveIn, reserveOut, feeBps);

  if (outputAmount == 0) {
    return 0;
  }

  return (uint64_t)((unsigned __int128)outputAmount * PRICE_SCALE / inputAmount);
}

bool poolValidateSwap(uint64_t inputAmount,
                       uint64_t outputAmount,
                       uint64_t reserveIn,
                       uint64_t reserveOut,
                       uint32_t feeBps) {
  if (inputAmount == 0 || outputAmount == 0) {
    return false;
  }

  if (outputAmount >= reserveOut) {
    return false; // Cannot drain pool
  }

  // Verify constant product invariant (all 128-bit):
  // (reserveIn * FEE_DIVISOR + inputAmount * feeAdj) * (reserveOut - outputAmount)
  //   >= reserveIn * reserveOut * FEE_DIVISOR
  uint64_t feeAdj = FEE_DIVISOR - feeBps;
  unsigned __int128 newReserveIn = (unsigned __int128)reserveIn * FEE_DIVISOR
                                 + (unsigned __int128)inputAmount * feeAdj;
  unsigned __int128 newReserveOut = reserveOut - outputAmount;

  unsigned __int128 kBefore = (unsigned __int128)reserveIn * reserveOut * FEE_DIVISOR;
  unsigned __int128 kAfter = newReserveIn * newReserveOut;

  return kAfter >= kBefore;
}

bool poolHasSufficientLiquidity(uint64_t outputAmount, uint64_t reserveOut) {
  return outputAmount < reserveOut; // Must leave at least 1 unit
}

bool poolValidateDepositRatio(uint64_t amountA,
                               uint64_t amountB,
                               uint64_t reserveA,
                               uint64_t reserveB,
                               uint32_t toleranceBps) {
  if (reserveA == 0 || reserveB == 0) {
    // First deposit: any ratio is valid
    return amountA > 0 && amountB > 0;
  }

  // Cross-multiply to compare ratios without division (128-bit):
  // amountA / amountB ~= reserveA / reserveB
  // => amountA * reserveB ~= amountB * reserveA
  unsigned __int128 expectedRatio = (unsigned __int128)amountA * reserveB;
  unsigned __int128 actualRatio   = (unsigned __int128)amountB * reserveA;
  unsigned __int128 delta = (expectedRatio > actualRatio)
    ? (expectedRatio - actualRatio) : (actualRatio - expectedRatio);

  unsigned __int128 maxDelta = expectedRatio * toleranceBps / FEE_DIVISOR;
  return delta <= maxDelta;
}

} // namespace XfgSwap
