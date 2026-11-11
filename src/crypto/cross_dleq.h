// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free & open source software distributed in the hope
// it will be useful, but WITHOUT ANY WARRANTY; without even an
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You may redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.
//
// The scalar behind a cross-curve point lock (CROSS_CURVE_PLAN X1). The proof
// that its ed25519 and secp256k1 points share it is sigma_fun's, reached
// through SwapDaemon/Crypto/CrossCurveDleq.h; none of it lives here.

#pragma once

#include "../../include/CryptoTypes.h"

namespace Crypto {

// Draws a uniform nonzero scalar below 2^252, valid on ed25519 and secp256k1
// alike. generate_keys() can exceed 2^252, which the proof cannot decompose.
void cross_dleq_generate_scalar(SecretKey& x);

} // namespace Crypto
