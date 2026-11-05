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
  // premiumHeat is the HEAT the mint premium buys at `price`: it is minted to
  // the AuntPoL HEAT leg (treasury reserve), not to the minter, so the
  // minter's cap is expectedHeat - premiumHeat.
  bool validateMint(const Transaction& tx,
                    uint64_t fee,
                    uint64_t price,
                    uint64_t& xfgBurned,
                    uint64_t& heatMinted,
                    uint8_t blockMajorVersion,
                    uint64_t& premiumHeat) const;

  // v12 auth-tag mint validation — math-only check on declared burn/mint amounts
  bool validateMintAuth(const Transaction& tx,
                        uint64_t fee,
                        uint64_t price,
                        uint64_t xfgBurned,
                        uint64_t heatMinted,
                        uint8_t blockMajorVersion,
                        uint64_t& premiumHeat) const;

  // HEAT the mint premium buys at `price`. Exposed so the connect path can
  // credit the reserve with the value settlement already priced.
  uint64_t premiumHeatFor(uint64_t xfgBurned, uint64_t price,
                          uint8_t blockMajorVersion) const;

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
