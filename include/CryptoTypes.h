// Copyright (c) 2012-2018 The CryptoNote developers
// Copyright (c) 2017-2025 Fuego Elder Council
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful- but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You are encouraged to redistribute it and/or modify it
// under the terms of the GNU General Public License v3 or later
// versions as published by the Free Software Foundation.
// You should receive a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>


#pragma once

#include <stdint.h>

// Max N for MembershipProof — covers 4 amount tiers AND 4 deposit terms.
#define FUEGO_MEMBERSHIP_N 4

#ifdef __cplusplus
namespace Crypto {
#endif

struct Hash {
  uint8_t data[32];
};

struct EllipticCurvePoint
{
  uint8_t data[32];
};

struct EllipticCurveScalar
{
  uint8_t data[32];
};

#ifdef __cplusplus
struct PublicKey : public EllipticCurvePoint {};
struct SecretKey : public EllipticCurveScalar {};
#else
typedef struct EllipticCurvePoint PublicKey;
typedef struct EllipticCurveScalar SecretKey;
#endif

struct KeyDerivation {
  uint8_t data[32];
};

struct KeyImage {
  uint8_t data[32];
};

struct Signature {
  uint8_t data[64];
};

// 1-of-N membership proof: proves amount ∈ {TIER_0..TIER_3}
struct MembershipProof {
  struct EllipticCurveScalar e[FUEGO_MEMBERSHIP_N];
  struct EllipticCurveScalar s[FUEGO_MEMBERSHIP_N];
};

const struct EllipticCurveScalar I = {{0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00} };

#ifdef __cplusplus
} // namespace Crypto
#endif
