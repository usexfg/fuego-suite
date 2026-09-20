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

class ISerializer;

struct AmmPoolState {
  uint64_t reserveXfg  = 0;
  uint64_t reserveHeat = 0;
  uint64_t totalLpShares = 0;
  uint64_t cdHearthFeeAccumulator = 0;  // flat fee → CD yield pool (not LPs)
  uint64_t pendingXfg  = 0;
  uint64_t pendingHeat = 0;

  bool isEmpty() const { return reserveXfg == 0 && reserveHeat == 0; }
  uint64_t totalReserve() const { return reserveXfg + reserveHeat; }

  void serialize(ISerializer& s);
};

uint64_t ammGetOutputAmount(uint64_t inputAmount,
                            uint64_t reserveIn,
                            uint64_t reserveOut,
                            uint32_t feeBps);

uint64_t ammGetInputAmount(uint64_t outputAmount,
                           uint64_t reserveIn,
                           uint64_t reserveOut,
                           uint32_t feeBps);

uint64_t ammGetSpotPrice(uint64_t reserveA, uint64_t reserveB);

/// Fixed launch mint price, on the canonical scale (HEAT atomics per XFG
/// atomic x COIN), derived from HEAT_LAUNCH_RATIO_NUM/DENOM.
///
/// The Hearth pool cannot hold HEAT before any HEAT exists, and HEAT cannot
/// be minted without a price, so at launch there is no pool price to mint
/// against. This breaks that deadlock: mint validation falls back to it
/// until the pool carries a price of its own.
uint64_t heatLaunchMintPrice();

uint64_t ammMintLpShares(uint64_t amountA, uint64_t amountB,
                          uint64_t totalShares,
                          uint64_t reserveA, uint64_t reserveB);

void ammGetWithdrawalAmounts(uint64_t lpSharesBurned,
                              uint64_t totalShares,
                              uint64_t reserveA, uint64_t reserveB,
                              uint64_t& amountA, uint64_t& amountB);

bool ammValidateSwap(uint64_t input, uint64_t output,
                     uint64_t reserveIn, uint64_t reserveOut,
                     uint32_t feeBps);

// Constant-product invariant check: the product of the two reserves must not
// decrease as a result of a swap (the fee ensures it strictly increases).
// Uses 128-bit math to avoid overflow. Returns false if the invariant would
// be violated (indicates a logic/rounding regression that could drain the
// pool). feeBps must be < 10000 for swaps.
bool ammValidateInvariant(uint64_t reserveAIn, uint64_t reserveBIn,
                          uint64_t reserveAOut, uint64_t reserveBOut);

bool ammValidateDepositRatio(uint64_t amountA, uint64_t amountB,
                              uint64_t reserveA, uint64_t reserveB,
                              uint32_t toleranceBps);

} // namespace CryptoNote
