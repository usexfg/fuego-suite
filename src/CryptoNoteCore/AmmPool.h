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
  uint64_t accumulatedLpFees = 0;

  bool isEmpty() const { return reserveXfg == 0 && reserveHeat == 0; }

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

bool ammValidateDepositRatio(uint64_t amountA, uint64_t amountB,
                              uint64_t reserveA, uint64_t reserveB,
                              uint32_t toleranceBps);

} // namespace CryptoNote
