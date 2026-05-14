// Copyright (c) 2017-2026 Fuego Developers
// Copyright (c) 2020-2026 Elderfire Privacy Group
// Copyright (c) 2013-2026 The Monero Project
// Copyright (c) Shen Noether, Adam Mackenzie, Monero Research Lab
//
// This file is part of Fuego.
//
// Fuego is free & open source software distributed in the hope
// that it will be useful, but WITHOUT ANY WARRANTY; without even
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You may redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation. Fuego includes elements written
// by third parties. See file labeled LICENSE for more details.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.
//
//
// 2-layer: layer 0 = spend key, layer 1 = commitment difference (balance proof).

#include "mlsag.h"
#include "crypto.h"

#include <alloca.h>
#include <cassert>
#include <cstring>

extern "C" {
#include "crypto-ops.h"
#include "random.h"
}

namespace Crypto {

// ---- helpers (duplicated from crypto.cpp where static) ----

static inline void mlsag_random_scalar(EllipticCurveScalar& res) {
  unsigned char tmp[64];
  generate_random_bytes(64, tmp);
  sc_reduce(tmp);
  memcpy(&res, tmp, 32);
}

static void mlsag_hash_to_ec(const PublicKey& key, ge_p3& res) {
  Hash h;
  ge_p2 point;
  ge_p1p1 point2;
  cn_fast_hash(std::addressof(key), sizeof(PublicKey), h);
  ge_fromfe_frombytes_vartime(&point, reinterpret_cast<const unsigned char*>(&h));
  ge_mul8(&point2, &point);
  ge_p1p1_to_p3(&res, &point2);
}

// ---- MLSAG per-round hash buffer ----
// Hashed to produce the next challenge scalar c_{i+1}.

#ifdef _MSC_VER
#pragma warning(disable : 4200)
#endif

struct mlsag_round {
  Hash prefix;            // 32 bytes: transaction prefix hash
  EllipticCurvePoint L0;  // 32 bytes: layer 0 L
  EllipticCurvePoint R0;  // 32 bytes: layer 0 R (key image linkage)
  EllipticCurvePoint L1;  // 32 bytes: layer 1 L
};

// Compute D = commitment - pseudo_commitment as ge_p3.
// D[i] = C[i] - C_pseudo; for real member, discrete log is (mask - z_pseudo).
static bool commitment_diff(ge_p3& result,
                            const EllipticCurvePoint& commitment,
                            const ge_p3& pseudo_unp) {
  ge_p3 c_unp;
  if (ge_frombytes_vartime(&c_unp, reinterpret_cast<const unsigned char*>(&commitment)) != 0)
    return false;

  ge_cached pseudo_cached;
  ge_p3_to_cached(&pseudo_cached, &pseudo_unp);

  ge_p1p1 diff_p1p1;
  ge_sub(&diff_p1p1, &c_unp, &pseudo_cached);
  ge_p1p1_to_p3(&result, &diff_p1p1);
  return true;
}

// ---- generate_mlsag ----

bool generate_mlsag(
    const Hash& prefix_hash,
    const PublicKey* pubs,
    const EllipticCurvePoint* commitments,
    const EllipticCurvePoint& pseudo_commitment,
    const KeyImage& key_image,
    const SecretKey& sec_key,
    const EllipticCurveScalar& sec_commit,
    size_t sec_index,
    size_t ring_size,
    EllipticCurveScalar& sig_c0,
    EllipticCurveScalar* sig_s) {

  if (ring_size == 0 || sec_index >= ring_size)
    return false;

  // Unpack and precompute key image for R0 computation
  ge_p3 image_unp;
  if (ge_frombytes_vartime(&image_unp, reinterpret_cast<const unsigned char*>(&key_image)) != 0)
    return false;
  ge_dsmp image_pre;
  ge_dsm_precomp(image_pre, &image_unp);

  // Unpack pseudo_commitment once
  ge_p3 pseudo_unp;
  if (ge_frombytes_vartime(&pseudo_unp, reinterpret_cast<const unsigned char*>(&pseudo_commitment)) != 0)
    return false;

  // Precompute commitment differences D[i] = commitments[i] - pseudo_commitment
  if (ring_size > 256) {
    return false; // refuse excessive ring sizes
  }
  ge_p3* D = reinterpret_cast<ge_p3*>(alloca(ring_size * sizeof(ge_p3)));
  for (size_t i = 0; i < ring_size; i++) {
    if (!commitment_diff(D[i], commitments[i], pseudo_unp))
      return false;
  }

  mlsag_round buf;
  buf.prefix = prefix_hash;

  // Random nonces for the real member
  EllipticCurveScalar alpha0, alpha1;
  mlsag_random_scalar(alpha0);
  mlsag_random_scalar(alpha1);

  // --- Round for real member (sec_index) ---
  {
    ge_p3 tmp3;
    ge_p2 tmp2;

    // L0 = alpha0 * G
    ge_scalarmult_base(&tmp3, reinterpret_cast<const unsigned char*>(&alpha0));
    ge_p3_tobytes(reinterpret_cast<unsigned char*>(&buf.L0), &tmp3);

    // R0 = alpha0 * H_p(K[pi])
    mlsag_hash_to_ec(pubs[sec_index], tmp3);
    ge_scalarmult(&tmp2, reinterpret_cast<const unsigned char*>(&alpha0), &tmp3);
    ge_tobytes(reinterpret_cast<unsigned char*>(&buf.R0), &tmp2);

    // L1 = alpha1 * G
    ge_scalarmult_base(&tmp3, reinterpret_cast<const unsigned char*>(&alpha1));
    ge_p3_tobytes(reinterpret_cast<unsigned char*>(&buf.L1), &tmp3);
  }

  // c[(pi+1) % N]
  EllipticCurveScalar c_cur;
  hash_to_scalar(&buf, sizeof(buf), c_cur);

  // Store c_0 if this is index 0
  if ((sec_index + 1) % ring_size == 0)
    sig_c0 = c_cur;

  // --- Rounds for all fake members ---
  for (size_t j = 1; j < ring_size; j++) {
    size_t i = (sec_index + j) % ring_size;

    // Random response scalars for this fake member
    mlsag_random_scalar(sig_s[i * 2]);      // s[i][0]
    mlsag_random_scalar(sig_s[i * 2 + 1]);  // s[i][1]

    ge_p2 tmp2;
    ge_p3 tmp3;

    // Unpack K[i]
    if (ge_frombytes_vartime(&tmp3, reinterpret_cast<const unsigned char*>(&pubs[i])) != 0)
      return false;

    // L0 = s[i][0]*G + c*K[i]
    ge_double_scalarmult_base_vartime(&tmp2,
        reinterpret_cast<const unsigned char*>(&c_cur), &tmp3,
        reinterpret_cast<const unsigned char*>(&sig_s[i * 2]));
    ge_tobytes(reinterpret_cast<unsigned char*>(&buf.L0), &tmp2);

    // R0 = s[i][0]*H_p(K[i]) + c*I
    mlsag_hash_to_ec(pubs[i], tmp3);
    ge_double_scalarmult_precomp_vartime(&tmp2,
        reinterpret_cast<const unsigned char*>(&sig_s[i * 2]), &tmp3,
        reinterpret_cast<const unsigned char*>(&c_cur), image_pre);
    ge_tobytes(reinterpret_cast<unsigned char*>(&buf.R0), &tmp2);

    // L1 = s[i][1]*G + c*D[i]
    ge_double_scalarmult_base_vartime(&tmp2,
        reinterpret_cast<const unsigned char*>(&c_cur), &D[i],
        reinterpret_cast<const unsigned char*>(&sig_s[i * 2 + 1]));
    ge_tobytes(reinterpret_cast<unsigned char*>(&buf.L1), &tmp2);

    // c_next
    hash_to_scalar(&buf, sizeof(buf), c_cur);

    // Store c_0 when we reach index 0
    if ((i + 1) % ring_size == 0)
      sig_c0 = c_cur;
  }

  // c_cur is now c[pi] — the challenge for the real member.
  // Close the ring by computing response scalars.

  // s[pi][0] = alpha0 - c[pi] * sec_key
  sc_mulsub(reinterpret_cast<unsigned char*>(&sig_s[sec_index * 2]),
            reinterpret_cast<const unsigned char*>(&c_cur),
            reinterpret_cast<const unsigned char*>(&sec_key),
            reinterpret_cast<const unsigned char*>(&alpha0));

  // s[pi][1] = alpha1 - c[pi] * sec_commit
  sc_mulsub(reinterpret_cast<unsigned char*>(&sig_s[sec_index * 2 + 1]),
            reinterpret_cast<const unsigned char*>(&c_cur),
            reinterpret_cast<const unsigned char*>(&sec_commit),
            reinterpret_cast<const unsigned char*>(&alpha1));

  return true;
}

// ---- check_mlsag ----

bool check_mlsag(
    const Hash& prefix_hash,
    const PublicKey* pubs,
    const EllipticCurvePoint* commitments,
    const EllipticCurvePoint& pseudo_commitment,
    const KeyImage& key_image,
    size_t ring_size,
    const EllipticCurveScalar& sig_c0,
    const EllipticCurveScalar* sig_s) {

  if (ring_size == 0)
    return false;

  // Validate all signature scalars are in range
  if (sc_check(reinterpret_cast<const unsigned char*>(&sig_c0)) != 0)
    return false;
  for (size_t i = 0; i < ring_size * 2; i++) {
    if (sc_check(reinterpret_cast<const unsigned char*>(&sig_s[i])) != 0)
      return false;
  }

  // Unpack and precompute key image
  ge_p3 image_unp;
  if (ge_frombytes_vartime(&image_unp, reinterpret_cast<const unsigned char*>(&key_image)) != 0)
    return false;
  ge_dsmp image_pre;
  ge_dsm_precomp(image_pre, &image_unp);

  // Unpack pseudo_commitment once
  ge_p3 pseudo_unp;
  if (ge_frombytes_vartime(&pseudo_unp, reinterpret_cast<const unsigned char*>(&pseudo_commitment)) != 0)
    return false;

  mlsag_round buf;
  buf.prefix = prefix_hash;

  EllipticCurveScalar c_cur = sig_c0;

  for (size_t i = 0; i < ring_size; i++) {
    ge_p2 tmp2;
    ge_p3 tmp3;

    // Unpack K[i]
    if (ge_frombytes_vartime(&tmp3, reinterpret_cast<const unsigned char*>(&pubs[i])) != 0)
      return false;

    // L0 = s[i][0]*G + c*K[i]
    ge_double_scalarmult_base_vartime(&tmp2,
        reinterpret_cast<const unsigned char*>(&c_cur), &tmp3,
        reinterpret_cast<const unsigned char*>(&sig_s[i * 2]));
    ge_tobytes(reinterpret_cast<unsigned char*>(&buf.L0), &tmp2);

    // R0 = s[i][0]*H_p(K[i]) + c*I
    mlsag_hash_to_ec(pubs[i], tmp3);
    ge_double_scalarmult_precomp_vartime(&tmp2,
        reinterpret_cast<const unsigned char*>(&sig_s[i * 2]), &tmp3,
        reinterpret_cast<const unsigned char*>(&c_cur), image_pre);
    ge_tobytes(reinterpret_cast<unsigned char*>(&buf.R0), &tmp2);

    // D[i] = commitments[i] - pseudo_commitment
    ge_p3 D_i;
    if (!commitment_diff(D_i, commitments[i], pseudo_unp))
      return false;

    // L1 = s[i][1]*G + c*D[i]
    ge_double_scalarmult_base_vartime(&tmp2,
        reinterpret_cast<const unsigned char*>(&c_cur), &D_i,
        reinterpret_cast<const unsigned char*>(&sig_s[i * 2 + 1]));
    ge_tobytes(reinterpret_cast<unsigned char*>(&buf.L1), &tmp2);

    // c_next
    hash_to_scalar(&buf, sizeof(buf), c_cur);
  }

  // Ring closes when c_cur == sig_c0
  sc_sub(reinterpret_cast<unsigned char*>(&c_cur),
         reinterpret_cast<unsigned char*>(&c_cur),
         reinterpret_cast<const unsigned char*>(&sig_c0));
  return sc_isnonzero(reinterpret_cast<const unsigned char*>(&c_cur)) == 0;
}

} // namespace Crypto
