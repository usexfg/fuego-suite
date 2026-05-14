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
#include <cmath>

namespace XfgSwap {

static constexpr uint64_t PRICE_SCALE = 1000000000000000000ULL; // 1e18

uint64_t poolGetOutputAmount(uint64_t inputAmount,
                              uint64_t reserveIn,
                              uint64_t reserveOut,
                              uint32_t feeBps) {
  if (inputAmount == 0 || reserveIn == 0 || reserveOut == 0) {
    return 0;
  }

  // inputWithFee = inputAmount * (10000 - feeBps)
  uint64_t inputWithFee = inputAmount * (10000 - feeBps);
  uint64_t numerator = inputWithFee * reserveOut;
  uint64_t denominator = reserveIn * 10000 + inputWithFee;

  if (denominator == 0) {
    return 0;
  }

  return numerator / denominator;
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

  // numerator = reserveIn * outputAmount * 10000
  uint64_t numerator = reserveIn * outputAmount * 10000;
  // denominator = (reserveOut - outputAmount) * (10000 - feeBps)
  uint64_t denominator = (reserveOut - outputAmount) * (10000 - feeBps);

  if (denominator == 0) {
    return 0;
  }

  // Add 1 to round up (input must be sufficient)
  return (numerator / denominator) + 1;
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
    // Initial liquidity: sqrt(amountA * amountB) - MIN_INITIAL_LIQUIDITY
    // Use integer sqrt approximation
    double product = static_cast<double>(amountA) * static_cast<double>(amountB);
    uint64_t liquidity = static_cast<uint64_t>(std::sqrt(product));

    if (liquidity <= MIN_INITIAL_LIQUIDITY) {
      return 0; // Dust protection
    }

    return liquidity - MIN_INITIAL_LIQUIDITY;
  }

  // Proportional minting: min of both ratios
  uint64_t sharesA = (amountA * totalShares) / reserveA;
  uint64_t sharesB = (amountB * totalShares) / reserveB;

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

  // Proportional share of reserves
  result.amountA = (burnAmount * reserveA) / totalShares;
  result.amountB = (burnAmount * reserveB) / totalShares;

  // Proportional share of accrued fees
  result.feeA = (burnAmount * feeAccumulatorA) / totalShares;
  result.feeB = (burnAmount * feeAccumulatorB) / totalShares;

  return result;
}

uint64_t poolGetSpotPrice(uint64_t reserveA, uint64_t reserveB) {
  if (reserveA == 0) {
    return 0;
  }

  return (reserveB * PRICE_SCALE) / reserveA;
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

  return (outputAmount * PRICE_SCALE) / inputAmount;
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

  // Verify constant product invariant:
  // (reserveIn + inputWithFee) * (reserveOut - output) >= reserveIn * reserveOut
  uint64_t inputWithFee = inputAmount * (10000 - feeBps);
  uint64_t newReserveIn = reserveIn * 10000 + inputWithFee;
  uint64_t newReserveOut = (reserveOut - outputAmount) * 10000;

  // Use 128-bit arithmetic to avoid overflow
  __uint128_t kBefore = static_cast<__uint128_t>(reserveIn) * reserveOut * 10000;
  __uint128_t kAfter = static_cast<__uint128_t>(newReserveIn) * newReserveOut;

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

  // Check if deposit ratio is within tolerance of pool ratio
  // poolRatio = reserveA / reserveB
  // depositRatio = amountA / amountB
  // Valid if |poolRatio - depositRatio| / poolRatio <= toleranceBps / 10000

  uint64_t poolRatioScaled = reserveA * 1000000 / reserveB;
  uint64_t depositRatioScaled = amountA * 1000000 / amountB;

  int64_t diff = static_cast<int64_t>(poolRatioScaled) - static_cast<int64_t>(depositRatioScaled);
  if (diff < 0) diff = -diff;

  uint64_t tolerance = (poolRatioScaled * toleranceBps) / 10000;

  return static_cast<uint64_t>(diff) <= tolerance;
}

} // namespace XfgSwap
