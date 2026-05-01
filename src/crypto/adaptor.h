// Copyright (c) 2017-2026 Fuego Developers
// Copyright (c) 2020-2026 Elderfire Privacy Group
//
// L. Fournier 2019.
// A. Aumayr 2021.
// D.J. Bernstein et al
//
// This file is part of Fuego.
//
// Fuego is free & open source software distributed in the hope
// it will be useful, but WITHOUT ANY WARRANTY; without even an
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You may redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation. Fuego includes elements written
// by third parties. See file labeled LICENSE for more details.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.
//
//
// Ed25519 adaptor signatures for atomic swaps.
//
// An adaptor (pre-)signature is a Schnorr signature offset by an adaptor
// point T = t*G.  The pre-signature is publicly verifiable but NOT a valid
// Schnorr signature.  When adapted with the secret scalar t it becomes a
// standard Fuego Schnorr signature verifiable by check_signature().
//
//   (Alice sells XFG, Bob sells SOL):
//   1. Bob picks secret t, publishes T = t*G
//   2. Both create Musig2 joint key P for XFG escrow
//   3. Alice sends XFG -> P
//   4. Alice creates adaptor pre-sig on "P -> Bob" tx, adapted with T
//   5. Bob verifies pre-sig
//   6. Bob locks SOL in HTLC(Keccak256(t))
//   7. Alice claims SOL by revealing t
//   8. Bob extracts t, adapts pre-sig -> valid sig, claims XFG
//
// The adapted signature is indistinguishable from a normal transaction
// signature, giving swaps the same privacy as regular transfers.

#pragma once

#include <cstdint>
#include <cstring>
#include "../../include/CryptoTypes.h"

extern "C" {
#include "crypto-ops.h"
}

namespace Crypto {

// Adaptor pre-signature: same 64-byte layout as Signature (c, r_hat)
// but r_hat does NOT satisfy the standard verification equation.
// Bytes 0-31: challenge c = Hs(prefix_hash || pub || R+T)
// Bytes 32-63: r_hat = k - c*x  (using original nonce k, not k+t)
struct AdaptorSignature {
  uint8_t data[64];
};

struct AFKLockData {
  EllipticCurveScalar secret;
  PublicKey adaptor_point;
  AdaptorSignature pre_sig;
};

// Generate an adaptor pre-signature.
//
// Signs prefix_hash under key (pub, sec) with adaptor point T.
// The pre-signature becomes a valid Schnorr signature when adapted
// with the discrete log t of T (i.e. T = t*G).
//
// Returns false if adaptor_point is not a valid curve point.
bool generate_adaptor_signature(
    const Hash &prefix_hash,
    const PublicKey &pub,
    const SecretKey &sec,
    const PublicKey &adaptor_point,
    AdaptorSignature &pre_sig);

// Verify an adaptor pre-signature.
//
// Checks that pre_sig would become a valid Schnorr signature on
// (prefix_hash, pub) if adapted with the discrete log of adaptor_point.
//
// Does NOT verify that the signer knows the adaptor secret.
bool check_adaptor_signature(
    const Hash &prefix_hash,
    const PublicKey &pub,
    const PublicKey &adaptor_point,
    const AdaptorSignature &pre_sig);

// AFK-specific primitives for non-interactive locks
bool generate_afk_lock_data(
    const Hash &prefix_hash,
    const PublicKey &pub,
    const SecretKey &sec,
    AFKLockData &out);

void complete_afk_signature(
    const AdaptorSignature &pre_sig,
    const EllipticCurveScalar &adaptor_secret,
    Signature &sig);

bool extract_afk_secret(
    const AdaptorSignature &pre_sig,
    const Signature &sig,
    EllipticCurveScalar &adaptor_secret);

// Adapt a pre-signature into a valid Schnorr signature.
//
// Given the adaptor secret t (where T = t*G was the adaptor point),
// computes sig = (c, r_hat + t).  The result is a standard Fuego
// Signature verifiable by check_signature(prefix_hash, pub, sig).
void adapt_signature(
    const AdaptorSignature &pre_sig,
    const EllipticCurveScalar &adaptor_secret,
    Signature &sig);

// Extract the adaptor secret from a pre-signature and its adapted form.
//
// Given pre_sig and the completed sig (observed on-chain), recovers
// t = sig.r - pre_sig.r_hat.  Returns false if the challenges don't
// match (signatures not from the same adaptor session).
bool extract_adaptor_secret(
    const AdaptorSignature &pre_sig,
    const Signature &sig,
    EllipticCurveScalar &adaptor_secret);

} // namespace Crypto
