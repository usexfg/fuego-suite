// Copyright (c) 2017-2026 Fuego Developers
// Copyright (c) 2020-2022 The Monero Project
//
// Bulletproofs+ range proof (Bünz et al. 2020).
// Proves an amount is in [0, 2^64) without revealing it.
//
// Protocol:
//   1. Bit-decompose amount into 64 bits
//   2. Commit to left/right vectors (A, B points)
//   3. Fiat-Shamir challenges (y, z)
//   4. Weighted inner-product argument (WIPA)
//   5. Output: (A, B, r1, s1, d1, L[], R[]) serialized

#pragma once

#include <cstdint>
#include <vector>
#include "../../include/CryptoTypes.h"

namespace Crypto {

void bulletproofs_plus_init();

std::vector<uint8_t> bulletproofs_plus_generate(
    const EllipticCurvePoint& commitment,
    uint64_t amount,
    const EllipticCurveScalar& mask);

bool bulletproofs_plus_verify(
    const EllipticCurvePoint& commitment,
    const std::vector<uint8_t>& proof);

} // namespace Crypto
