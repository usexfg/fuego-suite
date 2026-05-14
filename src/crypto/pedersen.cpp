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


// H = hash_to_point("Fuego_pedersen_H_generator_v1"), cofactor-cleared.

#include "pedersen.h"
#include <cstring>

extern "C" {
#include "hash-ops.h"
}

namespace Crypto {

static ge_p3 s_H_p3;
static bool s_H_initialized = false;

// Encode uint64_t amount as 32-byte little-endian scalar.
// Valid without reduction since amount < 2^64 < group order l (~2^252).
static void amount_to_scalar(unsigned char out[32], uint64_t amount) {
  memset(out, 0, 32);
  out[0] = (unsigned char)(amount);
  out[1] = (unsigned char)(amount >> 8);
  out[2] = (unsigned char)(amount >> 16);
  out[3] = (unsigned char)(amount >> 24);
  out[4] = (unsigned char)(amount >> 32);
  out[5] = (unsigned char)(amount >> 40);
  out[6] = (unsigned char)(amount >> 48);
  out[7] = (unsigned char)(amount >> 56);
}

void pedersen_init() {
  if (s_H_initialized) return;

  // hash_to_ec pattern from crypto.cpp:
  // 1. Hash fixed seed
  // 2. Map hash to curve point via Elligator
  // 3. Multiply by cofactor 8 to land in prime-order subgroup
  static const char seed[] = "Fuego_pedersen_H_generator_v1";
  unsigned char h[32];
  cn_fast_hash(seed, sizeof(seed) - 1, reinterpret_cast<char*>(h));

  ge_p2 point_p2;
  ge_p1p1 point_p1p1;
  ge_fromfe_frombytes_vartime(&point_p2, h);
  ge_mul8(&point_p1p1, &point_p2);
  ge_p1p1_to_p3(&s_H_p3, &point_p1p1);

  s_H_initialized = true;
}

const ge_p3& pedersen_H() {
  if (!s_H_initialized) pedersen_init();
  return s_H_p3;
}

void pedersen_commit(EllipticCurvePoint &result, uint64_t amount, const EllipticCurveScalar &mask) {
  if (!s_H_initialized) pedersen_init();

  unsigned char amount_scalar[32];
  amount_to_scalar(amount_scalar, amount);

  // C = amount*H + mask*G
  // ge_double_scalarmult_base_vartime(result, a, A, b) computes a*A + b*G
  ge_p2 C_p2;
  ge_double_scalarmult_base_vartime(&C_p2,
    amount_scalar, &s_H_p3,
    reinterpret_cast<const unsigned char*>(&mask));
  ge_tobytes(reinterpret_cast<unsigned char*>(&result), &C_p2);
}

bool pedersen_verify(const EllipticCurvePoint &C, uint64_t amount, const EllipticCurveScalar &mask) {
  EllipticCurvePoint expected;
  pedersen_commit(expected, amount, mask);
  return memcmp(&C, &expected, 32) == 0;
}

} // namespace Crypto
