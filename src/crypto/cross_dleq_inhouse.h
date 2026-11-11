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
// REFERENCE ONLY. No swap code calls this. The proof the daemon runs is
// sigma_fun's, reached through SwapDaemon/Crypto/CrossCurveDleq.h
// (docs/dexfg/CROSS_CURVE_PLAN.md X1). This is the in-house proof that X1
// first shipped, kept compiled and tested as a fallback. It has had no
// independent cryptographic review, and its 52,797-byte wire form is not
// sigma_fun's: the two cannot verify each other's proofs.
//
// Proves that one secret scalar x satisfies BOTH
//     A = x*G_ed25519      and      B = x*G_secp256k1
// without revealing x.
//
// Construction (Gugger 2020, "Bitcoin-Monero Cross-chain Atomic Swap", §4):
// commit to x bit by bit on both curves, prove every commitment opens to 0 or
// 1, and prove the two curves' commitments encode the SAME bit. Concretely,
// for each bit i:
//
//     C_i = b_i*G_ed   + r_i*H_ed        (Ed25519 Pedersen commitment)
//     D_i = b_i*G_secp + s_i*H_secp      (secp256k1 Pedersen commitment)
//
// A chained 2-ring OR-proof over the COMPOUND statement
//     (C_i opens to 0 AND D_i opens to 0) OR (C_i opens to 1 AND D_i opens to 1)
// forces the same bit on both curves: the ring's challenges are shared across
// the two groups, so a prover cannot answer with b on one curve and 1-b on the
// other. Challenges are 128-bit so they embed unambiguously in both scalar
// fields (both group orders exceed 2^128).
//
// Finally two Schnorr proofs tie the weighted sums back to A and B:
//     sum(2^i * C_i) - A = R*H_ed        with R = sum(2^i * r_i)
//     sum(2^i * D_i) - B = S*H_secp      with S = sum(2^i * s_i)
//
// x is restricted to 252 bits so it is a valid scalar in both fields
// (l_ed25519 > 2^252 and n_secp256k1 > 2^252).
//
// Pedersen generators: H_ed = 8 * hash_to_ec(keccak("Fuego/cross-dleq/v1/H_ed"));
// H_secp = the first x = SHA256("Fuego/cross-dleq/v1/H_secp" || counter_be32)
// that is a curve point with even y. Neither has a known discrete log.
// Points are checked on decode: ed25519 points must lie in the prime-order
// subgroup; responses must be canonical scalars.

#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

#include "../../include/CryptoTypes.h"
#include "secp_adaptor.h"

namespace Crypto {
namespace InHouseCrossDleq {

// x < 2^252, so bit index runs 0..251.
static constexpr size_t CROSS_DLEQ_BITS = 252;
// 128-bit ring challenges: below both group orders, 2^-128 soundness per ring.
static constexpr size_t CROSS_DLEQ_CHALLENGE_BYTES = 16;

// Per-bit ring proof plus the two commitments it binds together.
struct CrossDleqBit {
  uint8_t C_ed[32];                              // b_i*G_ed   + r_i*H_ed
  uint8_t D_secp[33];                            // b_i*G_secp + s_i*H_secp
  uint8_t e0[CROSS_DLEQ_CHALLENGE_BYTES];        // ring seed challenge
  uint8_t z_ed[2][32];                           // responses, branch 0 / 1
  uint8_t z_secp[2][32];
};

// ~52 KB. Heap-allocate; do not place on the stack.
struct CrossDleqProof {
  CrossDleqBit bits[CROSS_DLEQ_BITS];
  uint8_t R_ed[32];      // Schnorr commitment for R, base H_ed
  uint8_t z_R_ed[32];
  uint8_t R_secp[33];    // Schnorr commitment for S, base H_secp
  uint8_t z_S_secp[32];
};

// Prove A = x*G_ed and B = x*G_secp share x. A and B are outputs, derived from
// x, so a caller cannot accidentally prove a statement about points it did not
// generate. `context` binds the proof to one swap (see AUDIT 1.8).
// Returns false if x is zero or >= 2^252.
bool cross_dleq_prove(const SecretKey& x,
                      const Hash& context,
                      PublicKey& A_ed,
                      SecpPubKey& B_secp,
                      CrossDleqProof& proof);

// Verify. Returns false on any malformed point, out-of-range scalar, failed
// ring, or failed sum check.
bool cross_dleq_verify(const PublicKey& A_ed,
                       const SecpPubKey& B_secp,
                       const Hash& context,
                       const CrossDleqProof& proof);

// Wire helpers — fixed-size, endian-free (byte arrays only).
void cross_dleq_serialize(const CrossDleqProof& proof, std::vector<uint8_t>& out);
bool cross_dleq_deserialize(const std::vector<uint8_t>& in, CrossDleqProof& proof);
size_t cross_dleq_serialized_size();

}  // namespace InHouseCrossDleq
}  // namespace Crypto
