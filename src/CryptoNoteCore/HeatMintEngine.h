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
#include <CryptoNote.h>
#include "Common/FixedPoint.h"

namespace CryptoNote {

class Transaction;

class HeatMintEngine {
public:
  HeatMintEngine();

  bool validateMint(const Transaction& tx,
                    uint64_t fee,
                    FixedPoint64 redemptionPrice,
                    uint64_t& xfgBurned,
                    uint64_t& heatMinted) const;

  // v12 auth-tag mint validation — math-only check on declared burn/mint amounts
  bool validateMintAuth(const Transaction& tx,
                        uint64_t fee,
                        FixedPoint64 redemptionPrice,
                        uint64_t xfgBurned,
                        uint64_t heatMinted) const;

  bool isHeatMint(const Transaction& tx) const;
};

} // namespace CryptoNote
