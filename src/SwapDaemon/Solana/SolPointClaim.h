// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free & open source software distributed in the hope
// it will be useful, but WITHOUT ANY WARRANTY; without even an
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You may redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.
//
// SOL point claim (CROSS_CURVE_PLAN X4). The xfg_htlc program's claim_ptlc
// pays the recipient when the claimer proves knowledge of t for the escrow's
// point T = t*G: it recomputes e = reduce(keccak256(N || T || context)) with
// context = keccak256(htlc || recipient || amount_le), requires
// N == s*G + e*T, and requires t = s - r_hat to satisfy t*G == T, storing t
// for the locker. SOL is ed25519 like XFG, so T is the XFG adaptor point
// itself and no cross-curve proof is involved.

#pragma once

#include <array>
#include <cstdint>

#include "crypto/crypto.h"

namespace XfgSwap {

struct SolEscrowBinding {
  std::array<uint8_t, 32> htlc{};       // escrow state account
  std::array<uint8_t, 32> recipient{};  // the account the escrow pays
  uint64_t amount = 0;                  // lamports
};

// The claim_ptlc arguments.
struct SolPointClaimProof {
  std::array<uint8_t, 64> completedSignature{};  // bytes 32..63 carry s; 0..31 are unused by the program
  std::array<uint8_t, 32> nonce{};               // N = n*G
  std::array<uint8_t, 32> secretOffset{};        // r_hat = s - t
};

std::array<uint8_t, 32> solClaimContext(const SolEscrowBinding& escrow);

bool proveSolPointClaim(const Crypto::SecretKey& t, const Crypto::PublicKey& T, const SolEscrowBinding& escrow,
                        SolPointClaimProof& proof);

// The program's acceptance rule, evaluated off chain.
bool solPointClaimAccepted(const Crypto::PublicKey& T, const SolEscrowBinding& escrow,
                           const SolPointClaimProof& proof);

}  // namespace XfgSwap
