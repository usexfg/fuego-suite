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
// secp256k1 scalar and point operations on libsecp256k1 (constant time in
// secret scalars), shared by the Schnorr adaptor signature schemes.

#pragma once

#include <secp256k1.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace XfgSwap {
namespace Secp256k1Ops {

using Scalar = std::array<uint8_t, 32>;  // big-endian
using Point = std::array<uint8_t, 33>;   // compressed

const secp256k1_context* context();
void wipe(void* data, size_t size);

Scalar sha256(const std::vector<uint8_t>& data);
Scalar taggedHash(const std::string& tag, const std::vector<uint8_t>& data);
Scalar reducedModOrder(Scalar value);
bool randomBytes(uint8_t* out, size_t size);

bool validScalar(const Scalar& scalar);
bool scalarAdd(const Scalar& a, const Scalar& b, Scalar& out);
bool scalarSub(const Scalar& a, const Scalar& b, Scalar& out);
bool scalarMul(const Scalar& a, const Scalar& b, Scalar& out);
bool scalarNegate(const Scalar& a, Scalar& out);

bool parsePoint(const Point& bytes, secp256k1_pubkey& point);
bool serializePoint(const secp256k1_pubkey& point, Point& bytes);
bool pointOf(const Scalar& scalar, secp256k1_pubkey& point);
bool addPoints(const secp256k1_pubkey& a, const secp256k1_pubkey& b, secp256k1_pubkey& out);
bool subtractPoints(const secp256k1_pubkey& a, const secp256k1_pubkey& b, secp256k1_pubkey& out);
bool multiplyPoint(const secp256k1_pubkey& point, const Scalar& scalar, secp256k1_pubkey& out);
bool samePoint(const secp256k1_pubkey& a, const secp256k1_pubkey& b);
// Whether y(point) is a quadratic residue mod p.
bool hasResidueY(const secp256k1_pubkey& point);

}  // namespace Secp256k1Ops
}  // namespace XfgSwap
