// Copyright (c) 2017-2026 Fuego Developers
// Copyright (c) 2020-2026 Elderfire Privacy Group
//
// 1-of-N OR proof (Cramer-Damgard-Schoenmakers) proving a Pedersen
// commitment C = v*H + mask*G hides one of N known values, without
// revealing which. Used for both amount and term privacy on commitment
// deposit outputs. Proof size: N * 64 bytes (256 bytes for N=4).
//
// See tier_proof.cpp for full protocol description.

#pragma once

#include <cstddef>
#include <cstdint>
#include "../../include/CryptoTypes.h"

namespace Crypto {

// Generate a 1-of-N membership proof.
//   proof:       output proof
//   commitment:  Pedersen commitment C = real_value*H + mask*G
//   real_value:  the value committed to (must appear in values[])
//   mask:        blinding factor used to create the commitment
//   values:      array of valid values (e.g. tier amounts or deposit terms)
//   n:           number of values; must equal FUEGO_MEMBERSHIP_N
// Returns false if real_value not in values[], or inputs are invalid.
bool generate_membership_proof(MembershipProof &proof,
                               const EllipticCurvePoint &commitment,
                               uint64_t real_value,
                               const EllipticCurveScalar &mask,
                               const uint64_t *values,
                               size_t n);

// Verify a membership proof.
//   proof:      the proof to verify
//   commitment: Pedersen commitment from the output
//   values:     array of valid values (same set used during generation)
//   n:          number of values; must equal FUEGO_MEMBERSHIP_N
// Returns true iff the proof is valid.
bool check_membership_proof(const MembershipProof &proof,
                            const EllipticCurvePoint &commitment,
                            const uint64_t *values,
                            size_t n);

} // namespace Crypto
