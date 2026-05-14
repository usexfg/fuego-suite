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


// H generator: nothing-up-my-sleeve point derived from fixed seed.
// Commitment: C = amount*H + mask*G (standard Pedersen over Ed25519).

#pragma once

#include <cstdint>
#include "../../include/CryptoTypes.h"

extern "C" {
#include "crypto-ops.h"
}

namespace Crypto {

// Initialize the H generator point. Idempotent, thread-safe after first call.
void pedersen_init();

// Get the H generator as ge_p3. Auto-initializes on first call.
const ge_p3& pedersen_H();

// Compute Pedersen commitment: C = amount*H + mask*G
// result: 32-byte compressed Ed25519 point
void pedersen_commit(EllipticCurvePoint &result, uint64_t amount, const EllipticCurveScalar &mask);

// Verify that commitment C correctly opens to (amount, mask).
// Returns true if C == amount*H + mask*G.
bool pedersen_verify(const EllipticCurvePoint &C, uint64_t amount, const EllipticCurveScalar &mask);

} // namespace Crypto
