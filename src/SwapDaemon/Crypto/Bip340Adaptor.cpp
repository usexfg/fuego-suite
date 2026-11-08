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

#include "Bip340Adaptor.h"

#include <openssl/rand.h>
#include <openssl/sha.h>
#include <secp256k1.h>
#include <secp256k1_extrakeys.h>
#include <secp256k1_schnorrsig.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace XfgSwap {
namespace Bip340 {
namespace {

constexpr Scalar GROUP_ORDER = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xBA, 0xAE, 0xDC, 0xE6, 0xAF, 0x48,
                                0xA0, 0x3B, 0xBF, 0xD2, 0x5E, 0x8C, 0xD0, 0x36, 0x41, 0x41};

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

Scalar taggedHash(const std::string& tag, const std::vector<uint8_t>& data) {
  unsigned char tagHash[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char*>(tag.data()), tag.size(), tagHash);
  std::vector<uint8_t> preimage(tagHash, tagHash + sizeof(tagHash));
  preimage.insert(preimage.end(), tagHash, tagHash + sizeof(tagHash));
  preimage.insert(preimage.end(), data.begin(), data.end());
  Scalar digest;
  SHA256(preimage.data(), preimage.size(), digest.data());
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

bool liftX(const XOnlyKey& x, secp256k1_pubkey& point) {
  Point compressed;
  compressed[0] = 0x02;
  std::copy(x.begin(), x.end(), compressed.begin() + 1);
  return parsePoint(compressed, point);
}

bool hasEvenY(const Point& point) { return point[0] == 0x02; }

XOnlyKey xOf(const Point& point) {
  XOnlyKey x;
  std::copy(point.begin() + 1, point.end(), x.begin());
  return x;
}

Scalar challenge(const XOnlyKey& Rx, const XOnlyKey& Px, const Message& message) {
  std::vector<uint8_t> data(Rx.begin(), Rx.end());
  data.insert(data.end(), Px.begin(), Px.end());
  data.insert(data.end(), message.begin(), message.end());
  return reducedModOrder(taggedHash("BIP0340/challenge", data));
}

// x with P = x*G of even y, and P.x.
bool evenYSigningKey(const Scalar& secretKey, Scalar& signingKey, XOnlyKey& publicKey) {
  secp256k1_keypair keypair;
  secp256k1_xonly_pubkey xonly;
  int parity = 0;
  if (secp256k1_keypair_create(context(), &keypair, secretKey.data()) != 1 ||
      secp256k1_keypair_xonly_pub(context(), &xonly, &parity, &keypair) != 1 ||
      secp256k1_xonly_pubkey_serialize(context(), publicKey.data(), &xonly) != 1) {
    wipe(&keypair, sizeof(keypair));
    return false;
  }
  wipe(&keypair, sizeof(keypair));
  return parity == 0 ? (signingKey = secretKey, true) : scalarNegate(secretKey, signingKey);
}

bool deterministicNonce(const Scalar& secretKey, const XOnlyKey& publicKey, const Point& adaptorPoint,
                        const Message& message, Scalar& nonce) {
  for (int attempt = 0; attempt < 8; ++attempt) {
    Scalar auxiliary;
    if (RAND_bytes(auxiliary.data(), static_cast<int>(auxiliary.size())) != 1) return false;
    std::vector<uint8_t> data(secretKey.begin(), secretKey.end());
    data.insert(data.end(), publicKey.begin(), publicKey.end());
    data.insert(data.end(), adaptorPoint.begin(), adaptorPoint.end());
    data.insert(data.end(), message.begin(), message.end());
    data.insert(data.end(), auxiliary.begin(), auxiliary.end());
    nonce = reducedModOrder(taggedHash("Fuego/bip340-adaptor/nonce", data));
    wipe(data.data(), data.size());
    if (validScalar(nonce)) return true;
  }
  return false;
}

}  // namespace

bool xOnlyPublicKey(const Scalar& secretKey, XOnlyKey& publicKey) {
  Scalar signingKey;
  const bool ok = evenYSigningKey(secretKey, signingKey, publicKey);
  wipe(signingKey.data(), signingKey.size());
  return ok;
}

bool sign(const Scalar& secretKey, const Message& message, Signature& signature) {
  secp256k1_keypair keypair;
  Scalar auxiliary;
  const bool ok = RAND_bytes(auxiliary.data(), static_cast<int>(auxiliary.size())) == 1 &&
                  secp256k1_keypair_create(context(), &keypair, secretKey.data()) == 1 &&
                  secp256k1_schnorrsig_sign32(context(), signature.data(), message.data(), &keypair,
                                              auxiliary.data()) == 1;
  wipe(&keypair, sizeof(keypair));
  return ok;
}

bool verify(const XOnlyKey& publicKey, const Message& message, const Signature& signature) {
  secp256k1_xonly_pubkey xonly;
  return secp256k1_xonly_pubkey_parse(context(), &xonly, publicKey.data()) == 1 &&
         secp256k1_schnorrsig_verify(context(), signature.data(), message.data(), message.size(), &xonly) == 1;
}

bool presign(const Scalar& secretKey, const Message& message, const Point& adaptorPoint, AdaptorPresig& presig) {
  Scalar signingKey;
  XOnlyKey publicKey;
  secp256k1_pubkey T;
  if (!evenYSigningKey(secretKey, signingKey, publicKey) || !parsePoint(adaptorPoint, T)) return false;

  Scalar nonce, eTimesKey, signedNonce;
  secp256k1_pubkey nonceTimesG, R0;
  bool ok = deterministicNonce(secretKey, publicKey, adaptorPoint, message, nonce) && pointOf(nonce, nonceTimesG) &&
            addPoints(nonceTimesG, T, R0) && serializePoint(R0, presig.R0);
  if (ok) {
    const Scalar e = challenge(xOf(presig.R0), publicKey, message);
    signedNonce = nonce;
    ok = scalarMul(signingKey, e, eTimesKey) && (hasEvenY(presig.R0) || scalarNegate(nonce, signedNonce)) &&
         scalarAdd(eTimesKey, signedNonce, presig.sPrime);
  }
  wipe(signingKey.data(), signingKey.size());
  wipe(nonce.data(), nonce.size());
  wipe(signedNonce.data(), signedNonce.size());
  wipe(eTimesKey.data(), eTimesKey.size());
  return ok;
}

bool verifyPresig(const XOnlyKey& publicKey, const Message& message, const Point& adaptorPoint,
                  const AdaptorPresig& presig) {
  secp256k1_pubkey R0, T, P, sPrimeTimesG, eTimesP, nonceTimesG, expected;
  if (!validScalar(presig.sPrime) || !parsePoint(presig.R0, R0) || !parsePoint(adaptorPoint, T) ||
      !liftX(publicKey, P) || !pointOf(presig.sPrime, sPrimeTimesG))
    return false;
  const Scalar e = challenge(xOf(presig.R0), publicKey, message);
  eTimesP = P;
  if (secp256k1_ec_pubkey_tweak_mul(context(), &eTimesP, e.data()) != 1) return false;
  const bool signedNonceOk =
      hasEvenY(presig.R0) ? subtractPoints(R0, T, nonceTimesG) : subtractPoints(T, R0, nonceTimesG);
  Point lhs, rhs;
  return signedNonceOk && addPoints(nonceTimesG, eTimesP, expected) && serializePoint(sPrimeTimesG, lhs) &&
         serializePoint(expected, rhs) && lhs == rhs;
}

bool complete(const AdaptorPresig& presig, const Scalar& adaptorSecret, Signature& signature) {
  Scalar s;
  const bool ok = hasEvenY(presig.R0) ? scalarAdd(presig.sPrime, adaptorSecret, s)
                                      : scalarSub(presig.sPrime, adaptorSecret, s);
  if (!ok) return false;
  const XOnlyKey Rx = xOf(presig.R0);
  std::copy(Rx.begin(), Rx.end(), signature.begin());
  std::copy(s.begin(), s.end(), signature.begin() + 32);
  return true;
}

bool extract(const AdaptorPresig& presig, const Signature& signature, const Point& adaptorPoint,
             Scalar& adaptorSecret) {
  const XOnlyKey Rx = xOf(presig.R0);
  if (!std::equal(Rx.begin(), Rx.end(), signature.begin())) return false;
  Scalar s;
  std::copy(signature.begin() + 32, signature.end(), s.begin());
  const bool ok = hasEvenY(presig.R0) ? scalarSub(s, presig.sPrime, adaptorSecret)
                                      : scalarSub(presig.sPrime, s, adaptorSecret);
  secp256k1_pubkey recovered;
  Point recoveredBytes;
  return ok && pointOf(adaptorSecret, recovered) && serializePoint(recovered, recoveredBytes) &&
         recoveredBytes == adaptorPoint;
}

}  // namespace Bip340
}  // namespace XfgSwap
