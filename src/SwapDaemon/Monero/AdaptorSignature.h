// Copyright (c) 2017-2026 Fuego Developers
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

// Adaptor signatures for XMR atomic swaps (COMIT protocol).
// DLEQ-based: proves discrete log equality across two base points,
// enabling secret extraction from on-chain signature publication.

#pragma once

#include "crypto/crypto.h"
#include "crypto/hash.h"

namespace XfgSwap {

// Discrete Log Equality Proof
// Proves log_G(P1) == log_H(P2) without revealing the scalar
struct DleqProof {
  Crypto::EllipticCurveScalar s;  // response scalar
  Crypto::EllipticCurveScalar e;  // challenge scalar
};

// Adaptor Signature
// A signature "encrypted" under a public key T.
// Given the secret t where T = t*G, the signature can be "adapted" (completed).
struct AdaptorSignature {
  Crypto::EllipticCurvePoint R_hat;   // shifted nonce point (R + T = k*G + t*G)
  Crypto::EllipticCurveScalar s_hat;  // partial signature scalar
  DleqProof proof;                     // proves adaptor is well-formed (Fiat-Shamir)
  Crypto::EllipticCurvePoint R_H;     // k*H — second DLEQ point for proof verification
};

// Key pair for swap protocol
struct SwapKeyPair {
  Crypto::EllipticCurveScalar secretKey;
  Crypto::EllipticCurvePoint publicKey;
};

class AdaptorSigScheme {
public:
  // Generate a fresh keypair for the swap
  static SwapKeyPair generateKeyPair();

  // Alice: Create an adaptor signature that Bob can complete with the preimage
  // The adaptor "encrypts" a signature under adaptorPoint (= secret * G)
  static AdaptorSignature createAdaptor(
      const Crypto::EllipticCurveScalar& signerSecret,
      const Crypto::EllipticCurvePoint& adaptorPoint,    // T = t*G
      const Crypto::Hash& message);

  // Bob: Verify an adaptor signature is well-formed
  // (does NOT verify the final signature, just that it CAN be adapted)
  static bool verifyAdaptor(
      const AdaptorSignature& adaptor,
      const Crypto::EllipticCurvePoint& signerPubKey,
      const Crypto::EllipticCurvePoint& adaptorPoint,
      const Crypto::Hash& message);

  // Alice: Complete ("adapt") the signature using the adaptor secret
  // sig.s = adaptor.s_hat + adaptorSecret
  static Crypto::Signature adaptSignature(
      const AdaptorSignature& adaptor,
      const Crypto::EllipticCurveScalar& adaptorSecret);

  // Bob: Extract the adaptor secret from the completed signature and the adaptor
  // adaptorSecret = sig.s - adaptor.s_hat
  static Crypto::EllipticCurveScalar extractSecret(
      const Crypto::Signature& completedSig,
      const AdaptorSignature& adaptor);

  // Standard Schnorr verify (for the completed/adapted signature)
  static bool verifySignature(
      const Crypto::Signature& sig,
      const Crypto::EllipticCurvePoint& pubKey,
      const Crypto::Hash& message);

  // Create a DLEQ proof: proves log_G(P1) == log_H(P2)
  static DleqProof createDleqProof(
      const Crypto::EllipticCurveScalar& secret,
      const Crypto::EllipticCurvePoint& G,
      const Crypto::EllipticCurvePoint& H);

  // Verify a DLEQ proof
  static bool verifyDleqProof(
      const DleqProof& proof,
      const Crypto::EllipticCurvePoint& P1,
      const Crypto::EllipticCurvePoint& P2,
      const Crypto::EllipticCurvePoint& G,
      const Crypto::EllipticCurvePoint& H);

  // Combine three Ed25519 scalars via addition mod ℓ
  // Used to construct the full spend key for sweeping a shared XMR address
  // in adaptor swaps: combined = alice + bob + adaptor (mod ℓ)
  // Returns false if the result is zero (would brick the sweep)
  static bool combineSpendKeys(
      const std::array<uint8_t, 32>& alice,
      const std::array<uint8_t, 32>& bob,
      const std::array<uint8_t, 32>& adaptor,
      std::array<uint8_t, 32>& outCombined);
};

// XMR swap protocol helper that uses adaptor signatures
class MoneroSwapProtocol {
public:
  // Alice (XMR holder, wants XFG) generates swap keys and secret
  struct AliceInit {
    SwapKeyPair swapKeys;          // (a, A = a*G)
    Crypto::Hash secret;            // preimage s
    Crypto::Hash hashLock;          // H(s)
    Crypto::EllipticCurvePoint adaptorPoint;  // s*G
  };
  static AliceInit aliceInitialize();

  // Bob (XFG holder, wants XMR) generates swap keys
  struct BobInit {
    SwapKeyPair swapKeys;          // (b, B = b*G)
  };
  static BobInit bobInitialize();

  // Compute the shared XMR spend public key: A + B
  static Crypto::EllipticCurvePoint computeSharedSpendPub(
      const Crypto::EllipticCurvePoint& A,
      const Crypto::EllipticCurvePoint& B);

  // After Alice claims XFG (revealing preimage s on-chain),
  // Bob extracts the full XMR spend key: a + b
  // Bob knows b (his own secret) and extracts a from the adapted signature
  static Crypto::EllipticCurveScalar computeFullSpendKey(
      const Crypto::EllipticCurveScalar& bobSecret,
      const Crypto::EllipticCurveScalar& extractedAlicePartial);
};

} // namespace XfgSwap
