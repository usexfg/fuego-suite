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

  // price: canonical scale — HEAT atomics per XFG atomic × COIN (v12+).
  bool validateMint(const Transaction& tx,
                    uint64_t fee,
                    uint64_t price,
                    uint64_t& xfgBurned,
                    uint64_t& heatMinted) const;

  // v12 auth-tag mint validation — math-only check on declared burn/mint amounts
  bool validateMintAuth(const Transaction& tx,
                        uint64_t fee,
                        uint64_t price,
                        uint64_t xfgBurned,
                        uint64_t heatMinted) const;

  // Legacy pre-v12 overloads (Q64.64 XFG-per-HEAT). Bit-identical to the
  // original implementation — required for historical block re-validation.
  bool validateMint(const Transaction& tx,
                    uint64_t fee,
                    FixedPoint64 redemptionPrice,
                    uint64_t& xfgBurned,
                    uint64_t& heatMinted) const;

  bool validateMintAuth(const Transaction& tx,
                        uint64_t fee,
                        FixedPoint64 redemptionPrice,
                        uint64_t xfgBurned,
                        uint64_t heatMinted) const;

  bool isHeatMint(const Transaction& tx) const;
};

} // namespace CryptoNote
