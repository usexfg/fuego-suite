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

namespace CryptoNote {

// Hearth: consensus-embedded constant-product AMM (X * Y = K)
// Single XFG/HEAT pool providing on-chain price feed for the PI controller.

struct AmmPoolState {
  uint64_t reserveXfg  = 0;
  uint64_t reserveHeat = 0;
  uint64_t totalLpShares = 0;
  uint64_t feeAccumulator = 0;   // XFG fees for epoch CD pool

  bool isEmpty() const { return reserveXfg == 0 && reserveHeat == 0; }
};

// AMM math — pure constant product, deterministic integer arithmetic.
// All amounts in atomic units. Fee in basis points (1% = 100 bps).

// Compute swap output: input → output via constant product
// output = reserveOut * (input * (10000 - feeBps)) / (reserveIn * 10000 + input * (10000 - feeBps))
uint64_t ammGetOutputAmount(uint64_t inputAmount,
                            uint64_t reserveIn,
                            uint64_t reserveOut,
                            uint32_t feeBps);

// Compute input needed for desired output
uint64_t ammGetInputAmount(uint64_t outputAmount,
                           uint64_t reserveIn,
                           uint64_t reserveOut,
                           uint32_t feeBps);

// Spot price: reserveIn per reserveOut, scaled by 1e18
uint64_t ammGetSpotPrice(uint64_t reserveA, uint64_t reserveB);

// Mint LP shares: sqrt(depositA * depositB), proportional to existing
uint64_t ammMintLpShares(uint64_t amountA, uint64_t amountB,
                          uint64_t totalShares,
                          uint64_t reserveA, uint64_t reserveB);

// Withdrawal amounts: proportional return of reserves
void ammGetWithdrawalAmounts(uint64_t lpSharesBurned,
                              uint64_t totalShares,
                              uint64_t reserveA, uint64_t reserveB,
                              uint64_t& amountA, uint64_t& amountB);

// Validate invariant: newK >= oldK
bool ammValidateSwap(uint64_t input, uint64_t output,
                     uint64_t reserveIn, uint64_t reserveOut,
                     uint32_t feeBps);

// Validate deposit ratio is within tolerance
bool ammValidateDepositRatio(uint64_t amountA, uint64_t amountB,
                              uint64_t reserveA, uint64_t reserveB,
                              uint32_t toleranceBps);

} // namespace CryptoNote
