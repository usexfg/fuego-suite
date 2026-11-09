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
// Bitcoin Cash Schnorr signatures (2019-05-15 upgrade) and their adaptor
// form (CROSS_CURVE_PLAN X7).
//
// BCH Schnorr: e = SHA256(R.x || compressed(P) || m) mod n, s = k + e*x, and
// R must have a y that is a quadratic residue mod p. A pre-signature commits
// to R0 = k*G + T; the signature uses R0 when y(R0) is a residue and -R0
// otherwise (-1 is a non-residue because p = 3 mod 4), so
//   residue R0:     s' = k + e*x,  s = s' + t,  t = s - s'
//   non-residue R0: s' = -k + e*x, s = s' - t,  t = s' - s

#pragma once

#include "Bip340Adaptor.h"

namespace XfgSwap {
namespace BchSchnorr {

using Scalar = Bip340::Scalar;
using Point = Bip340::Point;      // compressed public key
using Message = Bip340::Message;  // sighash digest
using Signature = Bip340::Signature;
using AdaptorPresig = Bip340::AdaptorPresig;

bool publicKey(const Scalar& secretKey, Point& key);

bool sign(const Scalar& secretKey, const Message& message, Signature& signature);
bool verify(const Point& publicKey, const Message& message, const Signature& signature);

bool presign(const Scalar& secretKey, const Message& message, const Point& adaptorPoint, AdaptorPresig& presig);
bool verifyPresig(const Point& publicKey, const Message& message, const Point& adaptorPoint,
                  const AdaptorPresig& presig);
bool complete(const AdaptorPresig& presig, const Scalar& adaptorSecret, Signature& signature);
bool extract(const AdaptorPresig& presig, const Signature& signature, const Point& adaptorPoint,
             Scalar& adaptorSecret);

}  // namespace BchSchnorr
}  // namespace XfgSwap
