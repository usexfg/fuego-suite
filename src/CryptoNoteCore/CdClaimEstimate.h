// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful- but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You are encouraged to redistribute it and/or modify it
// under the terms of the GNU General Public License v3 or later
// versions as published by the Free Software Foundation.
// You should receive a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>

#pragma once

#include <cstdint>

namespace CryptoNote {

// What a CD could claim right now, computed the way consensus validates a
// CommitmentSpend claim (checkCommitmentSpendInput). One implementation
// (Blockchain::estimateCdClaim) serves the daemon RPC and in-process wallets,
// which had each re-derived it and drifted apart.
struct CdClaimEstimate {
  uint64_t baseInterest = 0;       // calculateCdInterest with the CD's term
  uint64_t bonusInterest = 0;      // calculateCdBonus (v11+ yield floor), else 0
  uint64_t claimableBase = 0;      // base capped by the fee pool and CD_APY_POOL
  uint64_t claimableBonus = 0;     // bonus capped by the Bonus Vault backing
  uint64_t feePoolBalance = 0;
  uint64_t cdApyVaultBalance = 0;
  uint64_t bonusVaultBacking = 0;  // min(BV counter, BONUS_VAULT UTXOs)
  uint32_t creditedEpochs = 0;     // epochs paid so far: the credited window
                                   // [c/D, (c+T)/D] ∩ epochs with a recorded rate
};

} // namespace CryptoNote
