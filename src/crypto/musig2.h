// Copyright (c) 2017-2026 Fuego Developers
// Copyright (c) 2020-2026 Elderfire Privacy Group
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
//   J. Nick, T. Ruffing, and Y. Seurin, 2021
//   BIP-327: MuSig2 for BIP-340-compatible multi-signatures
//   Ed25519 SUPERCOP ref10 D.J. Bernstein et al
//
//
// Musig2 two-of-two multi-signature scheme for Ed25519.
//
// Implements the 2-round Musig2 protocol (Schnorr-based, ν=2 nonces)
// for exactly 2 signers.  The aggregated public key P is indistinguishable
// from a normal Ed25519 public key, and the final signature is a standard
// Fuego Schnorr signature verifiable by check_signature().
//
// Used for atomic swap escrow: both parties contribute keys to form a
// 2-of-2 joint address.  Funds sent to this address require cooperation
// of both signers (or knowledge of the adaptor secret) to spend.
//
// Protocol:
//   1. Key aggregation: P = a1*P1 + a2*P2
//   2. Nonce generation: each signer creates 2 nonce pairs
//   3. Nonce aggregation: combine public nonces from both signers
//   4. Partial signing: each signer creates a partial signature
//   5. Aggregation: combine partial sigs into final Schnorr signature
//
// Adaptor variant: one partial signature is offset by adaptor point T,
// requiring knowledge of t to complete the aggregated signature.

#pragma once

#include <cstdint>
#include <cstring>
#include "../../include/CryptoTypes.h"

extern "C" {
#include "crypto-ops.h"
}

namespace Crypto {

// Number of nonces per signer (ν = 2, per Musig2 spec).
static const size_t MUSIG2_V = 2;

// Aggregated key context for 2-of-2.
struct Musig2KeyAgg {
  PublicKey agg_pubkey;           // P = a1*P1 + a2*P2
  EllipticCurveScalar coeff[2];  // a1, a2 (key aggregation coefficients)
};

// Secret nonce state (MUST be kept private, used exactly once).
struct Musig2SecNonce {
  EllipticCurveScalar k[MUSIG2_V];  // secret nonce scalars
};

// Public nonce (sent to counterparty).
struct Musig2PubNonce {
  PublicKey R[MUSIG2_V];  // R[j] = k[j]*G
};

// Aggregated public nonce (computed from both signers' nonces).
struct Musig2AggNonce {
  PublicKey R_agg[MUSIG2_V];  // R_agg[j] = R1[j] + R2[j]
};

// Signing session context (computed once, shared by both signers).
struct Musig2Session {
  EllipticCurveScalar b;        // nonce binding factor
  PublicKey R_combined;          // effective nonce: R_agg[0] + b*R_agg[1] [+ T]
  EllipticCurveScalar challenge; // c = Hs(prefix_hash || agg_pubkey || R_combined)
  bool nonceSigned = false;      // true after musig2_partial_sign; guards against nonce reuse
};

// A single signer's partial signature.
struct Musig2PartialSig {
  EllipticCurveScalar s;
};

// Aggregate two public keys into a Musig2 2-of-2 key.
//
// The order matters: signer 0 provides pub0, signer 1 provides pub1.
// Both signers must agree on the same ordering.
// Returns false if either key is invalid.
bool musig2_key_agg(
    const PublicKey &pub0,
    const PublicKey &pub1,
    Musig2KeyAgg &agg);

// Generate per-session nonces (MUST be called fresh for every signing session).
//
// Writes random secret nonces to sec_nonce and corresponding public
// nonces to pub_nonce.  The sec_nonce MUST be used exactly once and
// then securely erased — reuse leaks the private key.
void musig2_nonce_gen(
    Musig2SecNonce &sec_nonce,
    Musig2PubNonce &pub_nonce);

// Aggregate public nonces from both signers.
// Returns false if any nonce point is invalid.
bool musig2_nonce_agg(
    const Musig2PubNonce &nonce0,
    const Musig2PubNonce &nonce1,
    Musig2AggNonce &agg_nonce);

// Initialize a signing session.
//
// Computes the binding factor b, combined nonce R, and challenge c.
// If adaptor_point is non-NULL, the adaptor T is added to the combined
// nonce: R = R_agg[0] + b*R_agg[1] + T.
// Returns false on invalid input.
bool musig2_session_init(
    const Hash &prefix_hash,
    const Musig2KeyAgg &key_agg,
    const Musig2AggNonce &agg_nonce,
    const PublicKey *adaptor_point,  // NULL for non-adapted signing
    Musig2Session &session);

// Create a partial signature.
//
// signer_index: 0 or 1 (must match the order used in key_agg).
// sec_nonce is consumed and zeroed after use.
// session.nonceSigned is set to true; calling this twice on the same session
// is rejected with a false return to prevent nonce reuse.
bool musig2_partial_sign(
    Musig2Session &session,      // nonceSigned flag is checked and set
    const Musig2KeyAgg &key_agg,
    Musig2SecNonce &sec_nonce,   // zeroed after use
    const SecretKey &sec_key,
    unsigned int signer_index,
    Musig2PartialSig &partial_sig);

// Verify a partial signature from one signer.
//
// Checks that partial_sig is consistent with the signer's public nonce
// and public key within the session context.
bool musig2_partial_verify(
    const Musig2Session &session,
    const Musig2KeyAgg &key_agg,
    const Musig2PubNonce &signer_nonce,
    const PublicKey &signer_pub,
    unsigned int signer_index,
    const Musig2PartialSig &partial_sig);

// Aggregate two partial signatures into a final Schnorr signature.
//
// The result is a standard Fuego Signature verifiable with check_signature()
// against the aggregated public key (key_agg.agg_pubkey).
//
// For adapted sessions: call this AFTER adapting one partial sig
// (add adaptor_secret to it via sc_add).
void musig2_aggregate(
    const Musig2Session &session,
    const Musig2PartialSig &sig0,
    const Musig2PartialSig &sig1,
    Signature &final_sig);

} // namespace Crypto
