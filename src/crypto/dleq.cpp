// Copyright (c) 2017-2026 Fuego Developers
// Copyright (c) 2020-2026 Elderfire Privacy Group
//
//  D. Chaum and T.P. Pedersen, 1992
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


#include "dleq.h"
#include "crypto.h"

extern "C" {
#include "crypto-ops.h"
#include "random.h"
}

#include <mutex>
#include <cstring>

namespace Crypto {

// Domain separator for DLEQ challenge hashing.
static const char DLEQ_DOMAIN[] = "FuegoDLEQ";

// Hash buffer for DLEQ challenge: domain || P || A || B || R1 || R2
// Total: 9 + 32*5 = 169 bytes
struct dleq_hash_buf {
  char domain[sizeof(DLEQ_DOMAIN) - 1];  // "FuegoDLEQ" (no null)
  unsigned char base_point[32];            // P
  unsigned char point_G[32];               // A = x*G
  unsigned char point_P[32];               // B = x*P
  unsigned char R1[32];                    // k*G
  unsigned char R2[32];                    // k*P
};

static inline void dleq_random_scalar(EllipticCurveScalar &res) {
  unsigned char tmp[64];
  generate_random_bytes(64, tmp);
  sc_reduce(tmp);
  memcpy(&res, tmp, 32);
}

bool generate_dleq_proof(
    const PublicKey &base_point,
    const PublicKey &point_G,
    const PublicKey &point_P,
    const SecretKey &secret,
    DLEQProof &proof)
{
  std::lock_guard<std::mutex> lock(random_lock);

  // Decode P (second generator)
  ge_p3 P_p3;
  if (ge_frombytes_vartime(&P_p3,
      reinterpret_cast<const unsigned char*>(&base_point)) != 0) {
    return false;
  }

  // Random nonce k
  EllipticCurveScalar k;
  dleq_random_scalar(k);

  // R1 = k*G
  ge_p3 R1_p3;
  ge_scalarmult_base(&R1_p3, reinterpret_cast<const unsigned char*>(&k));

  // R2 = k*P
  ge_p2 R2_p2;
  ge_scalarmult(&R2_p2,
                reinterpret_cast<const unsigned char*>(&k),
                &P_p3);

  // Build hash buffer
  dleq_hash_buf buf;
  memcpy(buf.domain, DLEQ_DOMAIN, sizeof(DLEQ_DOMAIN) - 1);
  memcpy(buf.base_point, &base_point, 32);
  memcpy(buf.point_G, &point_G, 32);
  memcpy(buf.point_P, &point_P, 32);
  ge_p3_tobytes(buf.R1, &R1_p3);
  ge_tobytes(buf.R2, &R2_p2);

  // e = Hs(buf)
  hash_to_scalar(&buf, sizeof(dleq_hash_buf), proof.challenge);

  // s = k - e*x
  sc_mulsub(reinterpret_cast<unsigned char*>(&proof.response),
            reinterpret_cast<const unsigned char*>(&proof.challenge),
            reinterpret_cast<const unsigned char*>(&secret),
            reinterpret_cast<const unsigned char*>(&k));

  return true;
}

bool check_dleq_proof(
    const PublicKey &base_point,
    const PublicKey &point_G,
    const PublicKey &point_P,
    const DLEQProof &proof)
{
  // Decode all points
  ge_p3 P_p3, A_p3, B_p3;
  if (ge_frombytes_vartime(&P_p3,
      reinterpret_cast<const unsigned char*>(&base_point)) != 0) {
    return false;
  }
  if (ge_frombytes_vartime(&A_p3,
      reinterpret_cast<const unsigned char*>(&point_G)) != 0) {
    return false;
  }
  if (ge_frombytes_vartime(&B_p3,
      reinterpret_cast<const unsigned char*>(&point_P)) != 0) {
    return false;
  }

  // Validate proof scalars
  if (sc_check(reinterpret_cast<const unsigned char*>(&proof.challenge)) != 0 ||
      sc_check(reinterpret_cast<const unsigned char*>(&proof.response)) != 0) {
    return false;
  }

  const unsigned char *e = reinterpret_cast<const unsigned char*>(&proof.challenge);
  const unsigned char *s = reinterpret_cast<const unsigned char*>(&proof.response);

  // R1' = s*G + e*A  (should equal k*G)
  // ge_double_scalarmult_base_vartime(r, a, A, b) computes a*A + b*G
  ge_p2 R1_p2;
  ge_double_scalarmult_base_vartime(&R1_p2, e, &A_p3, s);

  // R2' = s*P + e*B  (should equal k*P)
  // Need: s*P + e*B
  // Use precomputed table for B: ge_dsm_precomp
  ge_dsmp B_precomp;
  ge_dsm_precomp(B_precomp, &B_p3);
  ge_p2 R2_p2;
  ge_double_scalarmult_precomp_vartime(&R2_p2, s, &P_p3, e, B_precomp);

  // Build hash buffer and recompute challenge
  dleq_hash_buf buf;
  memcpy(buf.domain, DLEQ_DOMAIN, sizeof(DLEQ_DOMAIN) - 1);
  memcpy(buf.base_point, &base_point, 32);
  memcpy(buf.point_G, &point_G, 32);
  memcpy(buf.point_P, &point_P, 32);
  ge_tobytes(buf.R1, &R1_p2);
  ge_tobytes(buf.R2, &R2_p2);

  EllipticCurveScalar e_prime;
  hash_to_scalar(&buf, sizeof(dleq_hash_buf), e_prime);

  // Verify e == e'
  EllipticCurveScalar diff;
  sc_sub(reinterpret_cast<unsigned char*>(&diff),
         reinterpret_cast<const unsigned char*>(&e_prime),
         e);
  return sc_isnonzero(reinterpret_cast<const unsigned char*>(&diff)) == 0;
}

} // namespace Crypto
