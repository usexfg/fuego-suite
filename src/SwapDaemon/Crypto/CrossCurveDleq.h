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
// Proof that the XFG adaptor point T = t*G_ed and T_secp = t*G_secp share t
// (CROSS_CURVE_PLAN X1). The proof is sigma_fun's secp256k1/ed25519
// discrete-log equality proof, reached through the C interface of the Rust
// library in cross_curve_dleq/; nothing of the proof is implemented here.

#pragma once

#include <cstdint>
#include <vector>

#include "CryptoTypes.h"
#include "crypto/secp_adaptor.h"

namespace XfgSwap {
namespace CrossCurveDleq {

// The library's bound on an encoded proof; the wire parser refuses more.
constexpr size_t MAX_PROOF_BYTES = 60000;
size_t maxProofSize();

// Proves for t (nonzero, below 2^252; see Crypto::cross_dleq_generate_scalar)
// and returns the two points the proof is about, derived from t by the library.
bool prove(const Crypto::SecretKey& t, Crypto::PublicKey& edPoint, Crypto::SecpPubKey& secpPoint,
           std::vector<uint8_t>& proof);

// False for any malformed proof or point, an ed25519 point outside the
// prime-order subgroup, or points that do not share one discrete logarithm.
bool verify(const Crypto::PublicKey& edPoint, const Crypto::SecpPubKey& secpPoint,
            const std::vector<uint8_t>& proof);

}  // namespace CrossCurveDleq
}  // namespace XfgSwap
