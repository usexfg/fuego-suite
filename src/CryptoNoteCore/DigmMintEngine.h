// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even
// the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See file labeled LICENSE for more details.

#pragma once

#include <cstdint>
#include <CryptoNote.h>

namespace CryptoNote {

class Transaction;

class DigmMintEngine {
public:
  DigmMintEngine();

  // Check if transaction contains DIGM commitment outputs
  bool isDigmMint(const Transaction& tx) const;

  // Validate DIGM mint: lock HEAT → mint DIGM at 0.10 HEAT per DIGM
  // Returns false if:
  // - No HEAT inputs
  // - No DIGM outputs
  // - HEAT input < DIGM output * DIGM_PEG_HEAT_ATOMIC
  bool validateMint(const Transaction& tx,
                    uint64_t fee,
                    uint64_t& heatLocked,
                    uint64_t& digmMinted) const;
};

} // namespace CryptoNote
