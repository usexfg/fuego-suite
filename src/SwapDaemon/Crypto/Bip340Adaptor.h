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
// BIP340 adaptor signatures (CROSS_CURVE_PLAN X5).
//
// A pre-signature commits to R0 = k*G + T. The completed BIP340 signature
// uses R = R0 when R0 has even y and R = -R0 otherwise, so
//   even R0: s' = k + e*x,  s = s' + t,  t = s - s'
//   odd  R0: s' = -k + e*x, s = s' - t,  t = s' - s
// with e = TaggedHash("BIP0340/challenge", R.x || P.x || m) and x the signing
// key negated when needed so P has even y. Completed signatures verify with
// any BIP340 verifier, including Bitcoin and Litecoin consensus.

#pragma once

#include <array>
#include <cstdint>

namespace XfgSwap {
namespace Bip340 {

using Scalar = std::array<uint8_t, 32>;      // big-endian, below the group order
using XOnlyKey = std::array<uint8_t, 32>;    // BIP340 public key
using Point = std::array<uint8_t, 33>;       // compressed
using Message = std::array<uint8_t, 32>;
using Signature = std::array<uint8_t, 64>;   // R.x || s

struct AdaptorPresig {
  Point R0;
  Scalar sPrime;
};

bool xOnlyPublicKey(const Scalar& secretKey, XOnlyKey& publicKey);

bool sign(const Scalar& secretKey, const Message& message, Signature& signature);
bool verify(const XOnlyKey& publicKey, const Message& message, const Signature& signature);

bool presign(const Scalar& secretKey, const Message& message, const Point& adaptorPoint, AdaptorPresig& presig);
bool verifyPresig(const XOnlyKey& publicKey, const Message& message, const Point& adaptorPoint,
                  const AdaptorPresig& presig);
bool complete(const AdaptorPresig& presig, const Scalar& adaptorSecret, Signature& signature);
// Recovers t from a completed signature and checks t*G == adaptorPoint.
bool extract(const AdaptorPresig& presig, const Signature& signature, const Point& adaptorPoint,
             Scalar& adaptorSecret);

}  // namespace Bip340
}  // namespace XfgSwap
