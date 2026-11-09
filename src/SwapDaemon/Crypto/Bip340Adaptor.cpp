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

#include <secp256k1_extrakeys.h>
#include <secp256k1_schnorrsig.h>

#include <algorithm>
#include <string>
#include <vector>

#include "Secp256k1Ops.h"

namespace XfgSwap {
namespace Bip340 {
namespace {

using namespace Secp256k1Ops;

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
    if (!randomBytes(auxiliary.data(), auxiliary.size())) return false;
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
  const bool ok = randomBytes(auxiliary.data(), auxiliary.size()) &&
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
  if (!multiplyPoint(P, e, eTimesP)) return false;
  const bool signedNonceOk =
      hasEvenY(presig.R0) ? subtractPoints(R0, T, nonceTimesG) : subtractPoints(T, R0, nonceTimesG);
  return signedNonceOk && addPoints(nonceTimesG, eTimesP, expected) && samePoint(sPrimeTimesG, expected);
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
