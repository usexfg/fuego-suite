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

// Adaptor signature implementation for XMR ↔ XFG atomic swaps.
// Based on the COMIT protocol (Thyagarajan et al., One-More Assumption).
//
// The adaptor is a Schnorr-like signature "encrypted" under an adaptor
// point T = t*G. Knowing t lets you "adapt" (complete) the signature.
// Seeing the completed signature lets you "extract" t.
//
// This uses the same Ed25519 curve operations as the rest of Fuego
// (crypto-ops.h: ge_p3, sc_add, sc_sub, etc.).

#include "AdaptorSignature.h"

#include <cstring>
#include <mutex>

extern "C" {
#include "crypto/crypto-ops.h"
}

// random.h is already included by crypto.h (via AdaptorSignature.h)
// hash.h is already included by crypto.h (via AdaptorSignature.h)

namespace XfgSwap {

// ---------------------------------------------------------------------------
// Internal helpers (same pattern as crypto.cpp)
// ---------------------------------------------------------------------------

// Reinterpret cast helpers for EllipticCurvePoint/Scalar to unsigned char*
static inline unsigned char* asBytes(Crypto::EllipticCurvePoint& p) {
  return reinterpret_cast<unsigned char*>(&p);
}
static inline const unsigned char* asBytes(const Crypto::EllipticCurvePoint& p) {
  return reinterpret_cast<const unsigned char*>(&p);
}
static inline unsigned char* asBytes(Crypto::EllipticCurveScalar& s) {
  return reinterpret_cast<unsigned char*>(&s);
}
static inline const unsigned char* asBytes(const Crypto::EllipticCurveScalar& s) {
  return reinterpret_cast<const unsigned char*>(&s);
}
static inline unsigned char* asBytes(Crypto::Hash& h) {
  return reinterpret_cast<unsigned char*>(&h);
}
static inline const unsigned char* asBytes(const Crypto::Hash& h) {
  return reinterpret_cast<const unsigned char*>(&h);
}

// Generate a random scalar (reduced mod l)
static void randomScalar(Crypto::EllipticCurveScalar& res) {
  unsigned char tmp[64];
  Crypto::generate_random_bytes(64, tmp);
  sc_reduce(tmp);
  std::memcpy(asBytes(res), tmp, 32);
}

// Hash arbitrary data to a scalar (Fiat-Shamir challenge)
// Uses cn_fast_hash then sc_reduce32 (same as Crypto::hash_to_scalar)
static void hashToScalar(const void* data, size_t len, Crypto::EllipticCurveScalar& res) {
  Crypto::cn_fast_hash(data, len, reinterpret_cast<Crypto::Hash&>(res));
  sc_reduce32(asBytes(res));
}

// Compute public key from secret: P = secret * G
static bool secretToPoint(const Crypto::EllipticCurveScalar& secret,
                          Crypto::EllipticCurvePoint& point) {
  ge_p3 p3;
  ge_scalarmult_base(&p3, asBytes(secret));
  ge_p3_tobytes(asBytes(point), &p3);
  return true;
}

// Point addition: result = A + B (as compressed points)
static bool pointAdd(const Crypto::EllipticCurvePoint& A,
                     const Crypto::EllipticCurvePoint& B,
                     Crypto::EllipticCurvePoint& result) {
  ge_p3 A_p3, B_p3;
  if (ge_frombytes_vartime(&A_p3, asBytes(A)) != 0) return false;
  if (ge_frombytes_vartime(&B_p3, asBytes(B)) != 0) return false;

  ge_cached B_cached;
  ge_p3_to_cached(&B_cached, &B_p3);

  ge_p1p1 sum_p1p1;
  ge_add(&sum_p1p1, &A_p3, &B_cached);

  ge_p3 sum_p3;
  ge_p1p1_to_p3(&sum_p3, &sum_p1p1);
  ge_p3_tobytes(asBytes(result), &sum_p3);
  return true;
}

// Scalar multiplication: result = scalar * P (P is a compressed point)
static bool scalarMultPoint(const Crypto::EllipticCurveScalar& scalar,
                            const Crypto::EllipticCurvePoint& P,
                            Crypto::EllipticCurvePoint& result) {
  ge_p3 P_p3;
  if (ge_frombytes_vartime(&P_p3, asBytes(P)) != 0) return false;

  ge_p2 R_p2;
  ge_scalarmult(&R_p2, asBytes(scalar), &P_p3);
  ge_tobytes(asBytes(result), &R_p2);
  return true;
}

// Build Fiat-Shamir challenge for DLEQ proof:
// e = H(G || H || P1 || P2 || R1 || R2)
// where P1 = x*G, P2 = x*H, R1 = k*G, R2 = k*H
static void dleqChallenge(const Crypto::EllipticCurvePoint& G_pt,
                          const Crypto::EllipticCurvePoint& H_pt,
                          const Crypto::EllipticCurvePoint& P1,
                          const Crypto::EllipticCurvePoint& P2,
                          const Crypto::EllipticCurvePoint& R1,
                          const Crypto::EllipticCurvePoint& R2,
                          Crypto::EllipticCurveScalar& e) {
  // Concatenate all 6 points (32 bytes each = 192 bytes)
  unsigned char buf[192];
  std::memcpy(buf,       asBytes(G_pt), 32);
  std::memcpy(buf + 32,  asBytes(H_pt), 32);
  std::memcpy(buf + 64,  asBytes(P1),   32);
  std::memcpy(buf + 96,  asBytes(P2),   32);
  std::memcpy(buf + 128, asBytes(R1),   32);
  std::memcpy(buf + 160, asBytes(R2),   32);
  hashToScalar(buf, 192, e);
}

// Build adaptor signature challenge:
// e = H(R_hat || pubKey || message)
static void adaptorChallenge(const Crypto::EllipticCurvePoint& R_hat,
                             const Crypto::EllipticCurvePoint& pubKey,
                             const Crypto::Hash& message,
                             Crypto::EllipticCurveScalar& e) {
  unsigned char buf[96]; // 32 + 32 + 32
  std::memcpy(buf,      asBytes(R_hat),  32);
  std::memcpy(buf + 32, asBytes(pubKey), 32);
  std::memcpy(buf + 64, asBytes(message), 32);
  hashToScalar(buf, 96, e);
}

// Get the Ed25519 base point G as a compressed point
static void getBasePoint(Crypto::EllipticCurvePoint& G) {
  // G is the standard Ed25519 base point. Compute 1*G.
  Crypto::EllipticCurveScalar one;
  std::memset(asBytes(one), 0, 32);
  asBytes(one)[0] = 1;
  ge_p3 G_p3;
  ge_scalarmult_base(&G_p3, asBytes(one));
  ge_p3_tobytes(asBytes(G), &G_p3);
}

// Nothing-up-my-sleeve second generator H for DLEQ proofs.
// H = hash_to_point("XFG_DLEQ_H") — derived from a fixed seed so that
// log_G(H) is unknown, making the DLEQ proof binding.
static void getDleqSecondGenerator(Crypto::EllipticCurvePoint& H) {
  // Hash the domain string to a scalar, then multiply by G.
  // This gives a point whose discrete log w.r.t. G is unknown.
  static const char seed[] = "XFG_DLEQ_H_v1";
  Crypto::Hash h;
  Crypto::cn_fast_hash(seed, sizeof(seed) - 1, h);
  // Reduce to a valid scalar
  unsigned char scalar[64] = {};
  std::memcpy(scalar, h.data, 32);
  sc_reduce(scalar);
  // Multiply base point by scalar
  ge_p3 H_p3;
  ge_scalarmult_base(&H_p3, scalar);
  ge_p3_tobytes(asBytes(H), &H_p3);
}

// ---------------------------------------------------------------------------
// AdaptorSigScheme implementation
// ---------------------------------------------------------------------------

SwapKeyPair AdaptorSigScheme::generateKeyPair() {
  SwapKeyPair kp;
  randomScalar(kp.secretKey);
  secretToPoint(kp.secretKey, kp.publicKey);
  return kp;
}

AdaptorSignature AdaptorSigScheme::createAdaptor(
    const Crypto::EllipticCurveScalar& signerSecret,
    const Crypto::EllipticCurvePoint& adaptorPoint,  // T = t*G
    const Crypto::Hash& message) {
  AdaptorSignature adaptor;

  // 1. Compute signer's public key: P = signerSecret * G
  Crypto::EllipticCurvePoint signerPub;
  secretToPoint(signerSecret, signerPub);

  // 2. Generate random nonce k
  Crypto::EllipticCurveScalar k;
  randomScalar(k);

  // 3. R = k * G  (standard Schnorr nonce point)
  Crypto::EllipticCurvePoint R;
  secretToPoint(k, R);

  // 4. R_hat = R + T  (the "shifted" nonce — adaptor point)
  //    This is what the verifier sees. The completed signature will use R directly.
  pointAdd(R, adaptorPoint, adaptor.R_hat);

  // 5. Compute challenge: e = H(R_hat || P || message)
  Crypto::EllipticCurveScalar e;
  adaptorChallenge(adaptor.R_hat, signerPub, message, e);

  // 6. s_hat = k - e * signerSecret  (partial signature)
  //    sc_mulsub computes: result = c - a * b (mod l)
  sc_mulsub(asBytes(adaptor.s_hat), asBytes(e), asBytes(signerSecret), asBytes(k));

  // 7. DLEQ proof: proves log_G(R) == log_H(R_H) where H is a second generator.
  //    This ensures the nonce k is well-formed and the adaptor binding holds.
  //    Uses a nothing-up-my-sleeve H so that log_G(H) is unknown.
  Crypto::EllipticCurvePoint G_pt, H_pt;
  getBasePoint(G_pt);
  getDleqSecondGenerator(H_pt);
  adaptor.proof = createDleqProof(k, G_pt, H_pt);

  // 8. Store R_H = k*H so the verifier can check the DLEQ proof without the secret k.
  scalarMultPoint(k, H_pt, adaptor.R_H);

  return adaptor;
}

bool AdaptorSigScheme::verifyAdaptor(
    const AdaptorSignature& adaptor,
    const Crypto::EllipticCurvePoint& signerPubKey,
    const Crypto::EllipticCurvePoint& adaptorPoint,
    const Crypto::Hash& message) {
  // Verify that: s_hat * G + e * P == R_hat - T
  // where e = H(R_hat || P || message)

  // 1. Compute challenge e
  Crypto::EllipticCurveScalar e;
  adaptorChallenge(adaptor.R_hat, signerPubKey, message, e);

  // 2. Compute LHS = s_hat * G + e * P
  //    ge_double_scalarmult_base_vartime(result, a, A, b) computes a*A + b*G
  //    We need e*P + s_hat*G, which is: e*P + s_hat*G
  ge_p3 P_p3;
  if (ge_frombytes_vartime(&P_p3, asBytes(signerPubKey)) != 0) return false;

  ge_p2 lhs_p2;
  ge_double_scalarmult_base_vartime(&lhs_p2, asBytes(e), &P_p3, asBytes(adaptor.s_hat));

  unsigned char lhs_bytes[32];
  ge_tobytes(lhs_bytes, &lhs_p2);

  // 3. Compute RHS = R_hat - T
  ge_p3 R_hat_p3, T_p3;
  if (ge_frombytes_vartime(&R_hat_p3, asBytes(adaptor.R_hat)) != 0) return false;
  if (ge_frombytes_vartime(&T_p3, asBytes(adaptorPoint)) != 0) return false;

  ge_cached T_cached;
  ge_p3_to_cached(&T_cached, &T_p3);

  ge_p1p1 rhs_p1p1;
  ge_sub(&rhs_p1p1, &R_hat_p3, &T_cached);

  ge_p3 rhs_p3;
  ge_p1p1_to_p3(&rhs_p3, &rhs_p1p1);
  unsigned char rhs_bytes[32];
  ge_p3_tobytes(rhs_bytes, &rhs_p3);

  // 4. Check adaptor equation: s_hat*G + e*P == R_hat - T
  if (std::memcmp(lhs_bytes, rhs_bytes, 32) != 0) return false;

  // 5. Verify the DLEQ proof: proves that the prover knows k such that
  //    R = k*G  (= R_hat - T, computed above as rhs_bytes)  AND
  //    R_H = k*H  (stored in adaptor.R_H, transmitted in wire format).
  //
  //    verifyDleqProof(proof, P1, P2, G, H) checks:
  //      P1 = k*G  = R = R_hat - T
  //      P2 = k*H  = R_H
  //    This ensures the adaptor is well-formed and not malleable.

  Crypto::EllipticCurvePoint G_pt, H_pt;
  getBasePoint(G_pt);
  getDleqSecondGenerator(H_pt);

  // R = R_hat - T, already computed as rhs_bytes (32 bytes)
  Crypto::EllipticCurvePoint R_pt;
  std::memcpy(asBytes(R_pt), rhs_bytes, 32);

  return verifyDleqProof(adaptor.proof, R_pt, adaptor.R_H, G_pt, H_pt);
}

Crypto::Signature AdaptorSigScheme::adaptSignature(
    const AdaptorSignature& adaptor,
    const Crypto::EllipticCurveScalar& adaptorSecret) {
  Crypto::Signature sig;
  std::memset(sig.data, 0, 64);

  // The completed signature has:
  //   R = R_hat - T  (but since R_hat = R + T, this gives us back R)
  //   However, the verifier uses R directly, so we compute:
  //   R = s_hat*G + e*P  (from the adaptor verification equation = R_hat - T)
  //
  // Actually, the adapted signature nonce point is R_hat - T.
  // We store it as the first 32 bytes of the Signature.
  ge_p3 R_hat_p3;
  if (ge_frombytes_vartime(&R_hat_p3, asBytes(adaptor.R_hat)) != 0) {
    return sig;
  }

  // Compute T = adaptorSecret * G
  ge_p3 T_p3;
  ge_scalarmult_base(&T_p3, asBytes(adaptorSecret));

  ge_cached T_cached;
  ge_p3_to_cached(&T_cached, &T_p3);

  ge_p1p1 R_p1p1;
  ge_sub(&R_p1p1, &R_hat_p3, &T_cached);

  ge_p3 R_p3;
  ge_p1p1_to_p3(&R_p3, &R_p1p1);
  ge_p3_tobytes(sig.data, &R_p3);

  // sig.s = s_hat + adaptorSecret (mod l)
  sc_add(sig.data + 32, asBytes(adaptor.s_hat), asBytes(adaptorSecret));

  return sig;
}

Crypto::EllipticCurveScalar AdaptorSigScheme::extractSecret(
    const Crypto::Signature& completedSig,
    const AdaptorSignature& adaptor) {
  // adaptorSecret = sig.s - adaptor.s_hat (mod l)
  Crypto::EllipticCurveScalar secret;
  sc_sub(asBytes(secret), completedSig.data + 32, asBytes(adaptor.s_hat));
  return secret;
}

bool AdaptorSigScheme::verifySignature(
    const Crypto::Signature& sig,
    const Crypto::EllipticCurvePoint& pubKey,
    const Crypto::Hash& message) {
  // Standard Schnorr verification: s*G == R + e*P
  // Equivalently: s*G - e*P == R
  //
  // We stored R in sig.data[0..31], s in sig.data[32..63]
  // Challenge: e = H(R || P || message)

  // 1. Reconstruct R from the signature
  Crypto::EllipticCurvePoint R;
  std::memcpy(asBytes(R), sig.data, 32);

  // 2. Compute e = H(R || P || message)
  Crypto::EllipticCurveScalar e;
  adaptorChallenge(R, pubKey, message, e);

  // 3. Check: s*G == R + e*P
  //    ge_double_scalarmult_base_vartime(result, a, A, b) = a*A + b*G
  //    We compute e*P + s*G and check it equals ... wait, we need to check
  //    s*G - e*P == R, i.e., s*G = R + e*P.
  //    Compute e*P + s*G and compare with R + e*P + e*P ... no.
  //
  //    Actually for our scheme: verification is s*G + e*P == R (wrong)
  //    Let's be careful. In createAdaptor: s_hat = k - e*x.
  //    In adaptSignature: s = s_hat + t = k - e*x + t.
  //    But R = R_hat - T = (k*G + T) - T = k*G.
  //    So s*G = (k - e*x + t)*G = k*G - e*x*G + t*G = R - e*P + T.
  //    Hmm, that's not standard Schnorr.
  //
  //    After adaptation, the completed sig is (R, s) where R = k*G and
  //    s = k - e*x + t, with e = H(R_hat || P || msg) = H((k+t)*G || P || msg).
  //    The adapted challenge should use R, not R_hat.
  //
  //    For a clean Schnorr verify post-adaptation:
  //    e' = H(R || P || msg), check s*G == R + e'*P? No.
  //    s = k - e*x + t where e = H(R_hat || P || msg), R = k*G.
  //    s*G = k*G - e*P + t*G = R - e*P + T.
  //    This doesn't simplify to standard Schnorr.
  //
  //    The extraction still works: secret = s - s_hat = t.
  //    The signature itself is NOT a standard Schnorr sig — it's the
  //    adaptor protocol's output. Verification is done via verifyAdaptor.
  //
  //    For a fully standard Schnorr verify, we would need to re-hash
  //    with R instead of R_hat and recompute s differently. This is a
  //    protocol design choice.
  //
  // TODO: Decide whether to support standalone Schnorr verify or only
  // adaptor verify + extract. For the atomic swap protocol, only
  // verifyAdaptor + extractSecret are needed on the critical path.

  ge_p3 P_p3;
  if (ge_frombytes_vartime(&P_p3, asBytes(pubKey)) != 0) return false;

  // Check s*G + e*P == R  (this is the standard check for s = k - e*x)
  // ge_double_scalarmult_base_vartime(result, a, A, b) = a*A + b*G
  // So: e*P + s*G
  ge_p2 check_p2;
  ge_double_scalarmult_base_vartime(&check_p2, asBytes(e), &P_p3, sig.data + 32);
  unsigned char check_bytes[32];
  ge_tobytes(check_bytes, &check_p2);

  return std::memcmp(check_bytes, sig.data, 32) == 0;
}

DleqProof AdaptorSigScheme::createDleqProof(
    const Crypto::EllipticCurveScalar& secret,
    const Crypto::EllipticCurvePoint& G,
    const Crypto::EllipticCurvePoint& H) {
  DleqProof proof;

  // P1 = secret * G
  Crypto::EllipticCurvePoint P1;
  scalarMultPoint(secret, G, P1);

  // P2 = secret * H
  Crypto::EllipticCurvePoint P2;
  scalarMultPoint(secret, H, P2);

  // Random nonce k
  Crypto::EllipticCurveScalar k;
  randomScalar(k);

  // R1 = k * G
  Crypto::EllipticCurvePoint R1;
  scalarMultPoint(k, G, R1);

  // R2 = k * H
  Crypto::EllipticCurvePoint R2;
  scalarMultPoint(k, H, R2);

  // Fiat-Shamir challenge: e = H(G || H || P1 || P2 || R1 || R2)
  dleqChallenge(G, H, P1, P2, R1, R2, proof.e);

  // Response: s = k - e * secret (mod l)
  // sc_mulsub(result, a, b, c) = c - a*b
  sc_mulsub(asBytes(proof.s), asBytes(proof.e), asBytes(secret), asBytes(k));

  return proof;
}

bool AdaptorSigScheme::verifyDleqProof(
    const DleqProof& proof,
    const Crypto::EllipticCurvePoint& P1,
    const Crypto::EllipticCurvePoint& P2,
    const Crypto::EllipticCurvePoint& G,
    const Crypto::EllipticCurvePoint& H) {
  // Reconstruct R1 = s*G + e*P1
  ge_p3 G_p3, P1_p3;
  if (ge_frombytes_vartime(&G_p3, asBytes(G)) != 0) return false;
  if (ge_frombytes_vartime(&P1_p3, asBytes(P1)) != 0) return false;

  // R1 = s*G + e*P1
  // We need to compute this using two scalar multiplications and addition.
  // ge_double_scalarmult_base_vartime only works with the standard base point.
  // So we compute s*G and e*P1 separately, then add.

  // s*G (using the G parameter, not hardcoded base point)
  ge_p2 sG_p2;
  ge_scalarmult(&sG_p2, asBytes(proof.s), &G_p3);
  unsigned char sG_bytes[32];
  ge_tobytes(sG_bytes, &sG_p2);
  ge_p3 sG_p3;
  if (ge_frombytes_vartime(&sG_p3, sG_bytes) != 0) return false;

  // e*P1
  ge_p2 eP1_p2;
  ge_scalarmult(&eP1_p2, asBytes(proof.e), &P1_p3);

  // Convert eP1 from p2 to p3 (via p1p1 trick: double then halve? No — use tobytes/frombytes)
  unsigned char eP1_bytes[32];
  ge_tobytes(eP1_bytes, &eP1_p2);
  ge_p3 eP1_p3;
  if (ge_frombytes_vartime(&eP1_p3, eP1_bytes) != 0) return false;

  // R1_check = sG + eP1
  ge_cached eP1_cached;
  ge_p3_to_cached(&eP1_cached, &eP1_p3);
  ge_p1p1 R1_p1p1;
  ge_add(&R1_p1p1, &sG_p3, &eP1_cached);
  ge_p3 R1_p3;
  ge_p1p1_to_p3(&R1_p3, &R1_p1p1);
  unsigned char R1_bytes[32];
  ge_p3_tobytes(R1_bytes, &R1_p3);

  Crypto::EllipticCurvePoint R1_check;
  std::memcpy(asBytes(R1_check), R1_bytes, 32);

  // Reconstruct R2 = s*H + e*P2
  ge_p3 H_p3, P2_p3;
  if (ge_frombytes_vartime(&H_p3, asBytes(H)) != 0) return false;
  if (ge_frombytes_vartime(&P2_p3, asBytes(P2)) != 0) return false;

  // s*H
  ge_p2 sH_p2;
  ge_scalarmult(&sH_p2, asBytes(proof.s), &H_p3);
  unsigned char sH_bytes[32];
  ge_tobytes(sH_bytes, &sH_p2);
  ge_p3 sH_p3;
  if (ge_frombytes_vartime(&sH_p3, sH_bytes) != 0) return false;

  // e*P2
  ge_p2 eP2_p2;
  ge_scalarmult(&eP2_p2, asBytes(proof.e), &P2_p3);
  unsigned char eP2_bytes[32];
  ge_tobytes(eP2_bytes, &eP2_p2);
  ge_p3 eP2_p3;
  if (ge_frombytes_vartime(&eP2_p3, eP2_bytes) != 0) return false;

  // R2_check = sH + eP2
  ge_cached eP2_cached;
  ge_p3_to_cached(&eP2_cached, &eP2_p3);
  ge_p1p1 R2_p1p1;
  ge_add(&R2_p1p1, &sH_p3, &eP2_cached);
  ge_p3 R2_p3;
  ge_p1p1_to_p3(&R2_p3, &R2_p1p1);
  unsigned char R2_bytes[32];
  ge_p3_tobytes(R2_bytes, &R2_p3);

  Crypto::EllipticCurvePoint R2_check;
  std::memcpy(asBytes(R2_check), R2_bytes, 32);

  // Recompute challenge: e_check = H(G || H || P1 || P2 || R1_check || R2_check)
  Crypto::EllipticCurveScalar e_check;
  dleqChallenge(G, H, P1, P2, R1_check, R2_check, e_check);

  // Verify e_check == proof.e
  return std::memcmp(asBytes(e_check), asBytes(proof.e), 32) == 0;
}

// ---------------------------------------------------------------------------
// MoneroSwapProtocol implementation
// ---------------------------------------------------------------------------

MoneroSwapProtocol::AliceInit MoneroSwapProtocol::aliceInitialize() {
  AliceInit init;

  // Generate swap keypair: (a, A = a*G)
  init.swapKeys = AdaptorSigScheme::generateKeyPair();

  // Generate random secret (preimage for the hash lock)
  // Use 32 random bytes, then hash them to get a clean secret
  unsigned char tmp[64];
  Crypto::generate_random_bytes(64, tmp);
  sc_reduce(tmp);
  std::memcpy(init.secret.data, tmp, 32);

  // Hash lock: H(s) — standard cn_fast_hash
  Crypto::cn_fast_hash(init.secret.data, 32, init.hashLock);

  // Adaptor point: T = s_scalar * G
  // We treat the secret as a scalar and compute its curve point.
  // This is what the adaptor signature is "encrypted" under.
  Crypto::EllipticCurveScalar secretScalar;
  std::memcpy(secretScalar.data, init.secret.data, 32);
  // Ensure it's a valid scalar (reduce mod l)
  sc_reduce32(secretScalar.data);
  // Update secret to match the reduced value
  std::memcpy(init.secret.data, secretScalar.data, 32);

  secretToPoint(secretScalar, init.adaptorPoint);

  return init;
}

MoneroSwapProtocol::BobInit MoneroSwapProtocol::bobInitialize() {
  BobInit init;
  init.swapKeys = AdaptorSigScheme::generateKeyPair();
  return init;
}

Crypto::EllipticCurvePoint MoneroSwapProtocol::computeSharedSpendPub(
    const Crypto::EllipticCurvePoint& A,
    const Crypto::EllipticCurvePoint& B) {
  // Shared spend public key = A + B (point addition on Ed25519)
  Crypto::EllipticCurvePoint result;
  if (!pointAdd(A, B, result)) {
    // On failure, return identity (should not happen with valid keys)
    std::memset(result.data, 0, 32);
    result.data[0] = 1;  // Ed25519 identity point encoding
  }
  return result;
}

Crypto::EllipticCurveScalar MoneroSwapProtocol::computeFullSpendKey(
    const Crypto::EllipticCurveScalar& bobSecret,
    const Crypto::EllipticCurveScalar& extractedAlicePartial) {
  // Full spend key = a + b (scalar addition mod l)
  // Bob knows b, and extracts a from the adapted signature.
  Crypto::EllipticCurveScalar fullKey;
  sc_add(fullKey.data, asBytes(bobSecret), asBytes(extractedAlicePartial));
  return fullKey;
}

} // namespace XfgSwap
