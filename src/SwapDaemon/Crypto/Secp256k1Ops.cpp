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

#include "Secp256k1Ops.h"

#include <openssl/bn.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <algorithm>
#include <memory>

namespace XfgSwap {
namespace Secp256k1Ops {
namespace {

constexpr Scalar GROUP_ORDER = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xBA, 0xAE, 0xDC, 0xE6, 0xAF, 0x48,
                                0xA0, 0x3B, 0xBF, 0xD2, 0x5E, 0x8C, 0xD0, 0x36, 0x41, 0x41};
constexpr Scalar FIELD_PRIME = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFC, 0x2F};

}  // namespace

const secp256k1_context* context() {
  static secp256k1_context* ctx = [] {
    secp256k1_context* created = secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
    unsigned char seed[32];
    if (created && RAND_bytes(seed, sizeof(seed)) == 1) secp256k1_context_randomize(created, seed);
    return created;
  }();
  return ctx;
}

void wipe(void* data, size_t size) {
  volatile uint8_t* bytes = static_cast<volatile uint8_t*>(data);
  while (size--) *bytes++ = 0;
}

Scalar sha256(const std::vector<uint8_t>& data) {
  Scalar digest;
  SHA256(data.data(), data.size(), digest.data());
  return digest;
}

Scalar taggedHash(const std::string& tag, const std::vector<uint8_t>& data) {
  const Scalar tagHash = sha256(std::vector<uint8_t>(tag.begin(), tag.end()));
  std::vector<uint8_t> preimage(tagHash.begin(), tagHash.end());
  preimage.insert(preimage.end(), tagHash.begin(), tagHash.end());
  preimage.insert(preimage.end(), data.begin(), data.end());
  const Scalar digest = sha256(preimage);
  wipe(preimage.data(), preimage.size());
  return digest;
}

Scalar reducedModOrder(Scalar value) {
  if (!std::lexicographical_compare(value.begin(), value.end(), GROUP_ORDER.begin(), GROUP_ORDER.end())) {
    int borrow = 0;
    for (int i = 31; i >= 0; --i) {
      const int difference = value[i] - GROUP_ORDER[i] - borrow;
      borrow = difference < 0;
      value[i] = static_cast<uint8_t>(difference + (borrow ? 256 : 0));
    }
  }
  return value;
}

bool randomBytes(uint8_t* out, size_t size) { return RAND_bytes(out, static_cast<int>(size)) == 1; }

bool validScalar(const Scalar& scalar) { return secp256k1_ec_seckey_verify(context(), scalar.data()) == 1; }

bool scalarAdd(const Scalar& a, const Scalar& b, Scalar& out) {
  out = a;
  return secp256k1_ec_seckey_tweak_add(context(), out.data(), b.data()) == 1;
}

bool scalarMul(const Scalar& a, const Scalar& b, Scalar& out) {
  out = a;
  return secp256k1_ec_seckey_tweak_mul(context(), out.data(), b.data()) == 1;
}

bool scalarNegate(const Scalar& a, Scalar& out) {
  out = a;
  return secp256k1_ec_seckey_negate(context(), out.data()) == 1;
}

bool scalarSub(const Scalar& a, const Scalar& b, Scalar& out) {
  Scalar negated;
  return scalarNegate(b, negated) && scalarAdd(a, negated, out);
}

bool parsePoint(const Point& bytes, secp256k1_pubkey& point) {
  return secp256k1_ec_pubkey_parse(context(), &point, bytes.data(), bytes.size()) == 1;
}

bool serializePoint(const secp256k1_pubkey& point, Point& bytes) {
  size_t size = bytes.size();
  return secp256k1_ec_pubkey_serialize(context(), bytes.data(), &size, &point, SECP256K1_EC_COMPRESSED) == 1 &&
         size == bytes.size();
}

bool pointOf(const Scalar& scalar, secp256k1_pubkey& point) {
  return secp256k1_ec_pubkey_create(context(), &point, scalar.data()) == 1;
}

bool addPoints(const secp256k1_pubkey& a, const secp256k1_pubkey& b, secp256k1_pubkey& out) {
  const secp256k1_pubkey* points[2] = {&a, &b};
  return secp256k1_ec_pubkey_combine(context(), &out, points, 2) == 1;
}

bool subtractPoints(const secp256k1_pubkey& a, const secp256k1_pubkey& b, secp256k1_pubkey& out) {
  secp256k1_pubkey negated = b;
  return secp256k1_ec_pubkey_negate(context(), &negated) == 1 && addPoints(a, negated, out);
}

bool multiplyPoint(const secp256k1_pubkey& point, const Scalar& scalar, secp256k1_pubkey& out) {
  out = point;
  return secp256k1_ec_pubkey_tweak_mul(context(), &out, scalar.data()) == 1;
}

bool samePoint(const secp256k1_pubkey& a, const secp256k1_pubkey& b) {
  Point first, second;
  return serializePoint(a, first) && serializePoint(b, second) && first == second;
}

bool hasResidueY(const secp256k1_pubkey& point) {
  std::array<uint8_t, 65> uncompressed;
  size_t size = uncompressed.size();
  if (secp256k1_ec_pubkey_serialize(context(), uncompressed.data(), &size, &point, SECP256K1_EC_UNCOMPRESSED) != 1)
    return false;
  std::unique_ptr<BN_CTX, decltype(&BN_CTX_free)> ctx(BN_CTX_new(), &BN_CTX_free);
  std::unique_ptr<BIGNUM, decltype(&BN_free)> y(BN_bin2bn(uncompressed.data() + 33, 32, nullptr), &BN_free);
  std::unique_ptr<BIGNUM, decltype(&BN_free)> p(BN_bin2bn(FIELD_PRIME.data(), 32, nullptr), &BN_free);
  return ctx && y && p && BN_kronecker(y.get(), p.get(), ctx.get()) == 1;
}

}  // namespace Secp256k1Ops
}  // namespace XfgSwap
