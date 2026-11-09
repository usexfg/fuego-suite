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

#include "BchSchnorrAdaptor.h"

#include <algorithm>
#include <string>
#include <vector>

#include "Secp256k1Ops.h"

namespace XfgSwap {
namespace BchSchnorr {
namespace {

using namespace Secp256k1Ops;
using XCoordinate = std::array<uint8_t, 32>;

XCoordinate xOf(const Point& point) {
  XCoordinate x;
  std::copy(point.begin() + 1, point.end(), x.begin());
  return x;
}

Scalar challenge(const XCoordinate& r, const Point& publicKey, const Message& message) {
  std::vector<uint8_t> data(r.begin(), r.end());
  data.insert(data.end(), publicKey.begin(), publicKey.end());
  data.insert(data.end(), message.begin(), message.end());
  return reducedModOrder(sha256(data));
}

bool parsedResidue(const Point& bytes, secp256k1_pubkey& point, bool& residue) {
  if (!parsePoint(bytes, point)) return false;
  residue = hasResidueY(point);
  return true;
}

bool signingNonce(const std::string& tag, const Scalar& secretKey, const Point& publicKey, const Point& adaptorPoint,
                  const Message& message, Scalar& nonce) {
  for (int attempt = 0; attempt < 8; ++attempt) {
    Scalar auxiliary;
    if (!randomBytes(auxiliary.data(), auxiliary.size())) return false;
    std::vector<uint8_t> data(secretKey.begin(), secretKey.end());
    data.insert(data.end(), publicKey.begin(), publicKey.end());
    data.insert(data.end(), adaptorPoint.begin(), adaptorPoint.end());
    data.insert(data.end(), message.begin(), message.end());
    data.insert(data.end(), auxiliary.begin(), auxiliary.end());
    nonce = reducedModOrder(taggedHash(tag, data));
    wipe(data.data(), data.size());
    if (validScalar(nonce)) return true;
  }
  return false;
}

// s = signedNonce + e*x for the challenge over `r`.
bool respond(const Scalar& secretKey, const Scalar& signedNonce, const XCoordinate& r, const Point& publicKey,
             const Message& message, Scalar& s) {
  Scalar eTimesKey;
  const bool ok = scalarMul(secretKey, challenge(r, publicKey, message), eTimesKey) &&
                  scalarAdd(eTimesKey, signedNonce, s);
  wipe(eTimesKey.data(), eTimesKey.size());
  return ok;
}

Signature joined(const XCoordinate& r, const Scalar& s) {
  Signature signature;
  std::copy(r.begin(), r.end(), signature.begin());
  std::copy(s.begin(), s.end(), signature.begin() + 32);
  return signature;
}

}  // namespace

bool publicKey(const Scalar& secretKey, Point& key) {
  secp256k1_pubkey point;
  return pointOf(secretKey, point) && serializePoint(point, key);
}

bool sign(const Scalar& secretKey, const Message& message, Signature& signature) {
  Point key, nonceBytes;
  Scalar nonce, s;
  secp256k1_pubkey R;
  bool ok = publicKey(secretKey, key) && signingNonce("Fuego/bch-schnorr/nonce", secretKey, key, Point{}, message, nonce) &&
            pointOf(nonce, R) && serializePoint(R, nonceBytes);
  if (ok && !hasResidueY(R)) ok = scalarNegate(nonce, nonce);
  ok = ok && respond(secretKey, nonce, xOf(nonceBytes), key, message, s);
  if (ok) signature = joined(xOf(nonceBytes), s);
  wipe(nonce.data(), nonce.size());
  return ok;
}

bool verify(const Point& key, const Message& message, const Signature& signature) {
  XCoordinate r;
  Scalar s;
  std::copy(signature.begin(), signature.begin() + 32, r.begin());
  std::copy(signature.begin() + 32, signature.end(), s.begin());
  secp256k1_pubkey P, sTimesG, eTimesP, R;
  Point recomputed;
  return validScalar(s) && parsePoint(key, P) && pointOf(s, sTimesG) &&
         multiplyPoint(P, challenge(r, key, message), eTimesP) && subtractPoints(sTimesG, eTimesP, R) &&
         serializePoint(R, recomputed) && xOf(recomputed) == r && hasResidueY(R);
}

bool presign(const Scalar& secretKey, const Message& message, const Point& adaptorPoint, AdaptorPresig& presig) {
  Point key;
  Scalar nonce, signedNonce;
  secp256k1_pubkey T, nonceTimesG, R0;
  bool residue = false;
  bool ok = publicKey(secretKey, key) && parsePoint(adaptorPoint, T) &&
            signingNonce("Fuego/bch-schnorr-adaptor/nonce", secretKey, key, adaptorPoint, message, nonce) &&
            pointOf(nonce, nonceTimesG) && addPoints(nonceTimesG, T, R0) && serializePoint(R0, presig.R0);
  if (ok) {
    residue = hasResidueY(R0);
    signedNonce = nonce;
    ok = (residue || scalarNegate(nonce, signedNonce)) &&
         respond(secretKey, signedNonce, xOf(presig.R0), key, message, presig.sPrime);
  }
  wipe(nonce.data(), nonce.size());
  wipe(signedNonce.data(), signedNonce.size());
  return ok;
}

bool verifyPresig(const Point& key, const Message& message, const Point& adaptorPoint, const AdaptorPresig& presig) {
  secp256k1_pubkey R0, T, P, sPrimeTimesG, eTimesP, signedNonceTimesG, expected;
  bool residue = false;
  if (!validScalar(presig.sPrime) || !parsedResidue(presig.R0, R0, residue) || !parsePoint(adaptorPoint, T) ||
      !parsePoint(key, P) || !pointOf(presig.sPrime, sPrimeTimesG) ||
      !multiplyPoint(P, challenge(xOf(presig.R0), key, message), eTimesP))
    return false;
  const bool signedNonceOk =
      residue ? subtractPoints(R0, T, signedNonceTimesG) : subtractPoints(T, R0, signedNonceTimesG);
  return signedNonceOk && addPoints(signedNonceTimesG, eTimesP, expected) && samePoint(sPrimeTimesG, expected);
}

bool complete(const AdaptorPresig& presig, const Scalar& adaptorSecret, Signature& signature) {
  secp256k1_pubkey R0;
  bool residue = false;
  Scalar s;
  if (!parsedResidue(presig.R0, R0, residue)) return false;
  const bool ok = residue ? scalarAdd(presig.sPrime, adaptorSecret, s) : scalarSub(presig.sPrime, adaptorSecret, s);
  if (ok) signature = joined(xOf(presig.R0), s);
  return ok;
}

bool extract(const AdaptorPresig& presig, const Signature& signature, const Point& adaptorPoint,
             Scalar& adaptorSecret) {
  secp256k1_pubkey R0, T, recovered;
  bool residue = false;
  const XCoordinate r = xOf(presig.R0);
  if (!std::equal(r.begin(), r.end(), signature.begin()) || !parsedResidue(presig.R0, R0, residue) ||
      !parsePoint(adaptorPoint, T))
    return false;
  Scalar s;
  std::copy(signature.begin() + 32, signature.end(), s.begin());
  const bool ok = residue ? scalarSub(s, presig.sPrime, adaptorSecret) : scalarSub(presig.sPrime, s, adaptorSecret);
  return ok && pointOf(adaptorSecret, recovered) && samePoint(recovered, T);
}

}  // namespace BchSchnorr
}  // namespace XfgSwap
