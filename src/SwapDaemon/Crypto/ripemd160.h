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

// Self-contained RIPEMD-160 (ISO/IEC 10118-3).
//
// Replaces the OpenSSL EVP call that previously stood here. RIPEMD-160 was
// moved to OpenSSL's *legacy provider* in 3.0, so `EVP_ripemd160()` yields a
// stub whose `EVP_DigestInit_ex` fails and leaves the context without a
// digest — the following Update/Final then dereference NULL and segfault.
// Carrying the algorithm here keeps address derivation identical on every
// OpenSSL version (1.1.1 through 4.x) and every platform, which matters
// because these digests decide user-visible chain addresses.
//
// Verified against the published RIPEMD-160 test vectors in
// SwapDaemon/tests/test_ripemd160.cpp.

#pragma once

#include <cstdint>
#include <vector>

namespace XfgSwap {

/// 20-byte RIPEMD-160 digest of `data`.
std::vector<uint8_t> ripemd160(const std::vector<uint8_t>& data);

} // namespace XfgSwap