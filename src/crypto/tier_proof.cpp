// Copyright (c) 2017-2026 Fuego Developers
// Copyright (c) 2020-2026 Elderfire Privacy Group
//
// 1-of-N membership proof (Schnorr OR, Fiat-Shamir) for Fuego deposit outputs.
//
// Used for BOTH amount privacy  (proves C hides one of 4 tier amounts)
//       AND  term   privacy  (proves C hides one of 4 deposit terms).
//
// Protocol (Cramer-Damgard-Schoenmakers):
//   Given commitment C = V[j]*H + mask*G for secret index j,
//   define P[i] = C - V[i]*H for each value i.
//   Note: P[j] = mask*G  (prover knows the discrete log)
//         P[i] = (V[j]-V[i])*H + mask*G  for i≠j  (unknown DL)
//
//   Prover:
//     For i ≠ j: pick random e[i], s[i]; R[i] = s[i]*G + e[i]*P[i]
//     For i == j: pick random k;          R[j] = k*G
//     e_total = Hs("FuegoMemberProof" || C || R[0] || ... || R[N-1])
//     e[j]    = e_total − Σ(e[i], i≠j)
//     s[j]    = k − e[j]*mask              (sc_mulsub)
//
//   Verifier:
//     For each i: P[i] = C − V[i]*H;  R[i] = s[i]*G + e[i]*P[i]
//     e' = Hs("FuegoMemberProof" || C || R[0] || ... || R[N-1])
//     Check: Σ(e[i]) == e'

#include "tier_proof.h"
#include "pedersen.h"
#include <cstring>
#include <mutex>

extern "C" {
#include "crypto-ops.h"
#include "hash-ops.h"
#include "random.h"
}

namespace Crypto {

extern std::mutex random_lock;

static const char PROOF_DOMAIN[] = "FuegoMemberProof";
static const size_t DOMAIN_LEN = 16;

// Hash layout for Fiat-Shamir challenge:
//   domain(16) + C(32) + R[0..N-1](N*32) = 16 + 32 + 4*32 = 176 bytes
struct ProofHashData {
  char domain[16];
  unsigned char C[32];
  unsigned char R[FUEGO_MEMBERSHIP_N][32];
};

// Little-endian uint64 → 32-byte scalar.
// Safe without sc_reduce32 since uint64 max < group order l (~2^252).
static void u64_to_scalar(unsigned char out[32], uint64_t v) {
  memset(out, 0, 32);
  out[0] = (unsigned char)(v);
  out[1] = (unsigned char)(v >> 8);
  out[2] = (unsigned char)(v >> 16);
  out[3] = (unsigned char)(v >> 24);
  out[4] = (unsigned char)(v >> 32);
  out[5] = (unsigned char)(v >> 40);
  out[6] = (unsigned char)(v >> 48);
  out[7] = (unsigned char)(v >> 56);
}

static void compute_challenge(EllipticCurveScalar &result,
                              const unsigned char *C,
                              const unsigned char R[][32]) {
  ProofHashData buf;
  std::memcpy(buf.domain, PROOF_DOMAIN, DOMAIN_LEN); 
  std::memcpy(buf.C, C, 32); 
  std::memcpy(buf.R, R, FUEGO_MEMBERSHIP_N * 32); 

  unsigned char hash[32];
  cn_fast_hash(&buf, sizeof(buf), reinterpret_cast<char*>(hash));
  sc_reduce32(hash);
  memcpy(&result, hash, 32);
}

// Compute P = C − value*H as ge_p3.  P[j] = mask*G when value == committed value.
static bool difference_point(ge_p3 &P, const ge_p3 &C_p3, uint64_t value) {
  unsigned char scalar[32];
  u64_to_scalar(scalar, value);

  ge_p2 vH_p2;
  ge_scalarmult(&vH_p2, scalar, &pedersen_H());

  unsigned char vH_bytes[32];
  ge_tobytes(vH_bytes, &vH_p2);

  ge_p3 vH_p3;
  if (ge_frombytes_vartime(&vH_p3, vH_bytes) != 0)
    return false;

  ge_cached vH_cached;
  ge_p3_to_cached(&vH_cached, &vH_p3);
  ge_p1p1 P_p1p1;
  ge_sub(&P_p1p1, &C_p3, &vH_cached);
  ge_p1p1_to_p3(&P, &P_p1p1);
  return true;
}

// Generate random Ed25519 scalar (caller must hold random_lock).
static void rand_scalar(EllipticCurveScalar &out) {
  unsigned char tmp[64];
  generate_random_bytes(64, tmp);
  sc_reduce(tmp);
  memcpy(&out, tmp, 32);
}

bool generate_membership_proof(MembershipProof &proof,
                               const EllipticCurvePoint &commitment,
                               uint64_t real_value,
                               const EllipticCurveScalar &mask,
                               const uint64_t *values,
                               size_t n) {
  if (n != FUEGO_MEMBERSHIP_N) return false;

  pedersen_init();

  int real_idx = -1;
  for (size_t i = 0; i < n; i++) {
    if (values[i] == real_value) { real_idx = (int)i; break; }
  }
  if (real_idx < 0) return false;

  ge_p3 C_p3;
  if (ge_frombytes_vartime(&C_p3, reinterpret_cast<const unsigned char*>(&commitment)) != 0)
    return false;

  ge_p3 P[FUEGO_MEMBERSHIP_N];
  for (size_t i = 0; i < n; i++) {
    if (!difference_point(P[i], C_p3, values[i]))
      return false;
  }

  memset(&proof, 0, sizeof(proof));

  EllipticCurveScalar k;
  {
    std::lock_guard<std::mutex> lock(random_lock);
    rand_scalar(k);
    for (size_t i = 0; i < n; i++) {
      if ((int)i != real_idx) {
        rand_scalar(proof.e[i]);
        rand_scalar(proof.s[i]);
      }
    }
  }

  // Compute R[i] for each member
  unsigned char R_bytes[FUEGO_MEMBERSHIP_N][32];
  for (size_t i = 0; i < n; i++) {
    if ((int)i == real_idx) {
      // R[j] = k*G
      ge_p3 kG;
      ge_scalarmult_base(&kG, reinterpret_cast<const unsigned char*>(&k));
      ge_p3_tobytes(R_bytes[i], &kG);
    } else {
      // R[i] = s[i]*G + e[i]*P[i]
      ge_p2 R_p2;
      ge_double_scalarmult_base_vartime(&R_p2,
        reinterpret_cast<const unsigned char*>(&proof.e[i]),
        &P[i],
        reinterpret_cast<const unsigned char*>(&proof.s[i]));
      ge_tobytes(R_bytes[i], &R_p2);
    }
  }

  // Fiat-Shamir challenge
  EllipticCurveScalar e_total;
  compute_challenge(e_total,
    reinterpret_cast<const unsigned char*>(&commitment),
    R_bytes);

  // e[j] = e_total − Σ(e[i], i≠j)
  unsigned char e_sum[32];
  sc_0(e_sum);
  for (size_t i = 0; i < n; i++) {
    if ((int)i != real_idx)
      sc_add(e_sum, e_sum, reinterpret_cast<const unsigned char*>(&proof.e[i]));
  }
  sc_sub(reinterpret_cast<unsigned char*>(&proof.e[real_idx]),
         reinterpret_cast<unsigned char*>(&e_total),
         e_sum);

  // s[j] = k − e[j]*mask  (sc_mulsub: s = c − a*b)
  sc_mulsub(reinterpret_cast<unsigned char*>(&proof.s[real_idx]),
            reinterpret_cast<const unsigned char*>(&proof.e[real_idx]),
            reinterpret_cast<const unsigned char*>(&mask),
            reinterpret_cast<const unsigned char*>(&k));

  return true;
}

bool check_membership_proof(const MembershipProof &proof,
                            const EllipticCurvePoint &commitment,
                            const uint64_t *values,
                            size_t n) {
  if (n != FUEGO_MEMBERSHIP_N) return false;

  pedersen_init();

  ge_p3 C_p3;
  if (ge_frombytes_vartime(&C_p3, reinterpret_cast<const unsigned char*>(&commitment)) != 0)
    return false;

  // Validate all proof scalars
  for (size_t i = 0; i < n; i++) {
    if (sc_check(reinterpret_cast<const unsigned char*>(&proof.e[i])) != 0) return false;
    if (sc_check(reinterpret_cast<const unsigned char*>(&proof.s[i])) != 0) return false;
  }

  // Reconstruct R[i] = s[i]*G + e[i]*P[i]
  unsigned char R_bytes[FUEGO_MEMBERSHIP_N][32];
  for (size_t i = 0; i < n; i++) {
    ge_p3 P;
    if (!difference_point(P, C_p3, values[i]))
      return false;

    ge_p2 R_p2;
    ge_double_scalarmult_base_vartime(&R_p2,
      reinterpret_cast<const unsigned char*>(&proof.e[i]),
      &P,
      reinterpret_cast<const unsigned char*>(&proof.s[i]));
    ge_tobytes(R_bytes[i], &R_p2);
  }

  // Recompute challenge
  EllipticCurveScalar e_expected;
  compute_challenge(e_expected,
    reinterpret_cast<const unsigned char*>(&commitment),
    R_bytes);

  // Verify: Σ(e[i]) == e'
  unsigned char e_sum[32];
  sc_0(e_sum);
  for (size_t i = 0; i < n; i++)
    sc_add(e_sum, e_sum, reinterpret_cast<const unsigned char*>(&proof.e[i]));

  sc_sub(e_sum, e_sum, reinterpret_cast<unsigned char*>(&e_expected));
  return sc_isnonzero(e_sum) == 0;
}

} // namespace Crypto
