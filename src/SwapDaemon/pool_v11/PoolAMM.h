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
//
// Constant product AMM math for LP pools.
//
// Formula: (reserveA + inputWithFee) * (reserveB - output) = reserveA * reserveB
// Where inputWithFee = inputAmount * (10000 - feeBps) / 10000

#pragma once

#include "PoolTypes.h"
#include <cstdint>

namespace XfgSwap {

// ─── Swap calculations ───────────────────────────────────────────────

// Calculate output amount for a swap given input and reserves.
// Uses constant product formula with fee deduction.
// Returns 0 if reserves are invalid or input is zero.
uint64_t poolGetOutputAmount(uint64_t inputAmount,
                              uint64_t reserveIn,
                              uint64_t reserveOut,
                              uint32_t feeBps);

// Calculate input amount required for a desired output.
// Returns 0 if reserves are invalid or output exceeds reserve.
uint64_t poolGetInputAmount(uint64_t outputAmount,
                             uint64_t reserveIn,
                             uint64_t reserveOut,
                             uint32_t feeBps);

// Calculate fee amount from input.
inline uint64_t poolGetFeeAmount(uint64_t inputAmount, uint32_t feeBps) {
  return (inputAmount * feeBps) / 10000;
}

// ─── LP share calculations ───────────────────────────────────────────

// Calculate LP shares to mint for a deposit.
// For initial deposit: sqrt(amountA * amountB) - MIN_INITIAL_LIQUIDITY
// For subsequent: min((amountA * totalShares) / reserveA, (amountB * totalShares) / reserveB)
uint64_t poolMintLPShares(uint64_t amountA,
                           uint64_t amountB,
                           uint64_t totalShares,
                           uint64_t reserveA,
                           uint64_t reserveB);

// Calculate output amounts for a withdrawal (burn LP shares).
// Returns pair of (amountA, amountB) proportional to share of pool.
struct WithdrawalAmounts {
  uint64_t amountA;
  uint64_t amountB;
  uint64_t feeA;
  uint64_t feeB;
};

WithdrawalAmounts poolGetWithdrawalAmounts(uint64_t burnAmount,
                                            uint64_t totalShares,
                                            uint64_t reserveA,
                                            uint64_t reserveB,
                                            uint64_t feeAccumulatorA,
                                            uint64_t feeAccumulatorB);

// ─── Price calculations ──────────────────────────────────────────────

// Get spot price: how much of asset B per 1 unit of asset A.
// Returns price * 1e18 (fixed point) to avoid floating point.
uint64_t poolGetSpotPrice(uint64_t reserveA, uint64_t reserveB);

// Get effective price after a swap (includes slippage).
// Returns price * 1e18 (fixed point).
uint64_t poolGetEffectivePrice(uint64_t inputAmount,
                                uint64_t reserveIn,
                                uint64_t reserveOut,
                                uint32_t feeBps);

// ─── Validation ──────────────────────────────────────────────────────

// Validate that a swap would maintain the constant product invariant.
// Returns true if the swap is valid.
bool poolValidateSwap(uint64_t inputAmount,
                       uint64_t outputAmount,
                       uint64_t reserveIn,
                       uint64_t reserveOut,
                       uint32_t feeBps);

// Check if pool has sufficient liquidity for a swap.
bool poolHasSufficientLiquidity(uint64_t outputAmount, uint64_t reserveOut);

// Check if deposit amounts are within acceptable ratio of pool ratio.
// Returns true if amounts are within tolerance (e.g., 1%).
bool poolValidateDepositRatio(uint64_t amountA,
                               uint64_t amountB,
                               uint64_t reserveA,
                               uint64_t reserveB,
                               uint32_t toleranceBps);

} // namespace XfgSwap
