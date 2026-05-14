// Copyright (c) 2017-2026 Fuego Developers
// Copyright (c) 2020-2026 Elderfire Privacy Group
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You can redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation. Fuego includes elements written
// by third parties. See file labeled LICENSE for more details.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.
//
// Adaptor signature atomic swap helpers for the XFG side.
//
// These functions wire the crypto primitives (adaptor.h, dleq.h, musig2.h)
// into the swap lifecycle.  Each step produces or consumes data from
// SwapParams, so the caller (SimpleWallet or SwapDaemon) can persist
// state between protocol rounds.

#pragma once

#include "SwapTypes.h"
#include "crypto/crypto.h"
#include "crypto/adaptor.h"
#include "crypto/dleq.h"
#include "crypto/musig2.h"

namespace XfgSwap {

// ─── Step 1: Key generation ──────────────────────────────────────────
//
// Generate a fresh Ed25519 keypair for this swap.
// Writes ourSwapSecKey / ourSwapPubKey into params.
void adaptor_generate_keys(SwapParams& params);

// ─── Step 2: Key aggregation ─────────────────────────────────────────
//
// Given our key and the peer's key, compute the Musig2 aggregated
// escrow public key.  The escrow looks like a normal Ed25519 pubkey.
//
// Signer ordering: Alice is signer 0, Bob is signer 1.
// Returns false if either key is invalid.
bool adaptor_key_aggregate(SwapParams& params);

// ─── Step 3: Adaptor point (Bob only) ────────────────────────────────
//
// Bob generates adaptor secret t, publishes T = t*G with DLEQ proof.
// Writes adaptorSecret, adaptorPoint, adaptorDleqProof.
// base_point is used as the second DLEQ generator (can be any known point).
bool adaptor_generate_adaptor(SwapParams& params,
                              const Crypto::PublicKey& dleq_base_point);

// ─── Step 3b: Verify adaptor point (Alice only) ─────────────────────
//
// Alice checks Bob's DLEQ proof that T is well-formed.
// dleq_peer_Q is Bob's Q = t * base_point (sent alongside T and proof).
bool adaptor_verify_adaptor(const SwapParams& params,
                            const Crypto::PublicKey& dleq_base_point,
                            const Crypto::PublicKey& dleq_peer_Q);

// ─── Step 4: Nonce generation ────────────────────────────────────────
//
// Generate Musig2 per-session nonces.  Must be called once per signing
// session.  Writes musig2.ourSecNonce / ourPubNonce.
void adaptor_nonce_generate(SwapParams& params);

// ─── Step 5: Nonce aggregation + session init ────────────────────────
//
// After exchanging pub nonces, aggregate them and initialize the Musig2
// session with the escrow-spend tx prefix hash and (optional) adaptor point.
//
// use_adaptor: if true, the session includes the adaptor point T
// (used for the "Bob claims" tx).  False for refund tx signing.
//
// Returns false on invalid nonces.
bool adaptor_session_init(SwapParams& params,
                          const Crypto::Hash& tx_prefix_hash,
                          bool use_adaptor);

// ─── Step 6: Partial signing ─────────────────────────────────────────
//
// Create our Musig2 partial signature.  Consumes and zeroes our sec nonce.
// Returns false if the session nonce has already been used (nonce reuse guard).
bool adaptor_partial_sign(SwapParams& params);

// ─── Step 7: Partial signature verification ──────────────────────────
//
// Verify the peer's partial signature against their public nonce and key.
bool adaptor_partial_verify(const SwapParams& params);

// ─── Step 8: Aggregate + adapt ───────────────────────────────────────
//
// Aggregate both partial sigs into a final Schnorr signature.
// If the session was adaptor-enabled, the caller must first adapt
// one partial sig by adding the adaptor secret t.
//
// adapted: if true, add adaptorSecret to peer's partial sig before
// aggregation (Bob does this when he learns t).
//
// Returns the completed Fuego Signature.
Crypto::Signature adaptor_aggregate(SwapParams& params, bool adapted);

// ─── Extract adaptor secret ──────────────────────────────────────────
//
// Alice sees the broadcast tx with the adapted aggregate sig.  She
// knows the unadapted peer partial sig.  She extracts t.
//
// on_chain_sig: the signature from the broadcast claim tx
// Returns false if challenges don't match.
bool adaptor_extract_secret(SwapParams& params,
                            const Crypto::Signature& on_chain_sig);

} // namespace XfgSwap
