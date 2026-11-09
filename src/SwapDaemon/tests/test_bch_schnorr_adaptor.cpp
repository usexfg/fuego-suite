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

#include <openssl/rand.h>

#include <cstdio>
#include <string>
#include <vector>

#include "Common/StringTools.h"
#include "SwapDaemon/Crypto/BchSchnorrAdaptor.h"
#include "SwapDaemon/Crypto/Secp256k1Ops.h"
#include "crypto/cross_dleq.h"
#include "crypto/secp_adaptor.h"

using namespace XfgSwap::BchSchnorr;
namespace BchSchnorr = XfgSwap::BchSchnorr;

static int g_checks = 0;
static int g_failures = 0;

static void check(bool condition, const std::string& label) {
  ++g_checks;
  if (!condition) {
    ++g_failures;
    std::printf("FAIL  %s\n", label.c_str());
  }
}

template <typename Bytes>
static bool fromHex(const std::string& hex, Bytes& out) {
  std::vector<uint8_t> bytes;
  if (!Common::fromHex(hex, bytes) || bytes.size() != out.size()) return false;
  std::copy(bytes.begin(), bytes.end(), out.begin());
  return true;
}

struct PublishedVector {
  const char* name;
  const char* publicKey;
  const char* message;
  const char* signature;
  bool valid;
};

// The Bitcoin ABC secp256k1 Schnorr module's compact test vectors, which the
// 2019-05-15 upgrade specification names as its conformance suite.
static const PublishedVector PUBLISHED_VECTORS[] = {
    {"1", "0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798",
     "0000000000000000000000000000000000000000000000000000000000000000",
     "787a848e71043d280c50470e8e1532b2dd5d20ee912a45dbdd2bd1dfbf187ef67031a98831859dc34dffeedda86831842ccd0079e1f92af177f7f22cc1dced05",
     true},
    {"2", "02dff1d77f2a671c5f36183726db2341be58feae1da2deced843240f7b502ba659",
     "243f6a8885a308d313198a2e03707344a4093822299f31d0082efa98ec4e6c89",
     "2a298dacae57395a15d0795ddbfd1dcb564da82b0f269bc70a74f8220429ba1d1e51a22ccec35599b8f266912281f8365ffc2d035a230434a1a64dc59f7013fd",
     true},
    {"3", "03fac2114c2fbb091527eb7c64ecb11f8021cb45e8e7809d3c0938e4b8c0e5f84b",
     "5e2d58d8b3bcdf1abadec7829054f90dda9805aab56c77333024b9d0a508b75c",
     "00da9b08172a9b6f0466a2defd817f2d7ab437e0d253cb5395a963866b3574be00880371d01766935b92d2ab4cd5c8a2a5837ec57fed7660773a05f0de142380",
     true},
    {"4", "03defdea4cdb677750a420fee807eacf21eb9898ae79b9768766e4faa04a2d4a34",
     "4df3c3f68fcc83b27e9d42c90431a72499f17875c81a599b566c9889b9696703",
     "00000000000000000000003b78ce563f89a0ed9414f5aa28ad0d96d6795f9c6302a8dc32e64e86a333f20ef56eac9ba30b7246d6d25e22adb8c6be1aeb08d49d",
     true},
    {"4b", "031b84c5567b126440995d3ed5aaba0565d71e1834604819ff9c17f5e9d5dd078f",
     "0000000000000000000000000000000000000000000000000000000000000000",
     "52818579aca59767e3291d91b76b637bef062083284992f2d95f564ca6cb4e3530b1da849c8e8304adc0cfe870660334b3cfc18e825ef1db34cfae3dfc5d8187",
     true},
    {"6: R.y is not a quadratic residue", "02dff1d77f2a671c5f36183726db2341be58feae1da2deced843240f7b502ba659",
     "243f6a8885a308d313198a2e03707344a4093822299f31d0082efa98ec4e6c89",
     "2a298dacae57395a15d0795ddbfd1dcb564da82b0f269bc70a74f8220429ba1dfa16aee06609280a19b67a24e1977e4697712b5fd2943914ecd5f730901b4ab7",
     false},
    {"7: negated message hash, R.x mismatch", "03fac2114c2fbb091527eb7c64ecb11f8021cb45e8e7809d3c0938e4b8c0e5f84b",
     "5e2d58d8b3bcdf1abadec7829054f90dda9805aab56c77333024b9d0a508b75c",
     "00da9b08172a9b6f0466a2defd817f2d7ab437e0d253cb5395a963866b3574bed092f9d860f1776a1f7412ad8a1eb50daccc222bc8c0e26b2056df2f273efdec",
     false},
    {"8: negated s, R.x mismatch", "0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798",
     "0000000000000000000000000000000000000000000000000000000000000000",
     "787a848e71043d280c50470e8e1532b2dd5d20ee912a45dbdd2bd1dfbf187ef68fce5677ce7a623cb20011225797ce7a8de1dc6ccd4f754a47da6c600e59543c",
     false},
    {"9: negated P, R.x mismatch", "03dff1d77f2a671c5f36183726db2341be58feae1da2deced843240f7b502ba659",
     "243f6a8885a308d313198a2e03707344a4093822299f31d0082efa98ec4e6c89",
     "2a298dacae57395a15d0795ddbfd1dcb564da82b0f269bc70a74f8220429ba1d1e51a22ccec35599b8f266912281f8365ffc2d035a230434a1a64dc59f7013fd",
     false},
    {"10: s*G = e*P, R is the point at infinity", "02dff1d77f2a671c5f36183726db2341be58feae1da2deced843240f7b502ba659",
     "243f6a8885a308d313198a2e03707344a4093822299f31d0082efa98ec4e6c89",
     "2a298dacae57395a15d0795ddbfd1dcb564da82b0f269bc70a74f8220429ba1d8c3428869a663ed1e954705b020cbb3e7bb6ac31965b9ea4c73e227b17c5af5a",
     false},
    {"11: R.x not on the curve", "02dff1d77f2a671c5f36183726db2341be58feae1da2deced843240f7b502ba659",
     "243f6a8885a308d313198a2e03707344a4093822299f31d0082efa98ec4e6c89",
     "4a298dacae57395a15d0795ddbfd1dcb564da82b0f269bc70a74f8220429ba1d1e51a22ccec35599b8f266912281f8365ffc2d035a230434a1a64dc59f7013fd",
     false},
    {"12: r = p", "02dff1d77f2a671c5f36183726db2341be58feae1da2deced843240f7b502ba659",
     "243f6a8885a308d313198a2e03707344a4093822299f31d0082efa98ec4e6c89",
     "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffc2f1e51a22ccec35599b8f266912281f8365ffc2d035a230434a1a64dc59f7013fd",
     false},
    {"13: s = n", "02dff1d77f2a671c5f36183726db2341be58feae1da2deced843240f7b502ba659",
     "243f6a8885a308d313198a2e03707344a4093822299f31d0082efa98ec4e6c89",
     "2a298dacae57395a15d0795ddbfd1dcb564da82b0f269bc70a74f8220429ba1dfffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141",
     false},
};

static void testPublishedVectors() {
  for (const PublishedVector& vector : PUBLISHED_VECTORS) {
    Point key;
    Message message;
    Signature signature;
    const bool parsed = fromHex(vector.publicKey, key) && fromHex(vector.message, message) &&
                        fromHex(vector.signature, signature);
    check(parsed && verify(key, message, signature) == vector.valid,
          std::string("published vector ") + vector.name + (vector.valid ? " verifies" : " is rejected"));
  }
}

static Scalar randomKey() {
  Scalar key;
  do RAND_bytes(key.data(), static_cast<int>(key.size()));
  while (!XfgSwap::Secp256k1Ops::validScalar(key));
  return key;
}

struct Adaptor {
  Scalar t;
  Point T;
};

// t is drawn as the swap draws it (below 2^252) and T is derived the way
// secp_point_from_ed_secret derives T_secp for the cross-curve proof.
static Adaptor swapAdaptor() {
  Crypto::SecretKey edSecret;
  Crypto::cross_dleq_generate_scalar(edSecret);
  Crypto::SecpPubKey secpPoint;
  Crypto::secp_point_from_ed_secret(edSecret, secpPoint);
  Adaptor adaptor;
  std::copy(secpPoint.data.begin(), secpPoint.data.end(), adaptor.T.begin());
  const uint8_t* little = reinterpret_cast<const uint8_t*>(&edSecret);
  for (size_t i = 0; i < 32; ++i) adaptor.t[i] = little[31 - i];
  return adaptor;
}

static Message randomMessage() {
  Message message;
  RAND_bytes(message.data(), static_cast<int>(message.size()));
  return message;
}

static bool nonceHasResidueY(const AdaptorPresig& presig) {
  secp256k1_pubkey R0;
  return XfgSwap::Secp256k1Ops::parsePoint(presig.R0, R0) && XfgSwap::Secp256k1Ops::hasResidueY(R0);
}

static void testPlainSignature() {
  const Scalar secretKey = randomKey();
  Point key;
  Signature signature;
  const Message message = randomMessage();
  check(publicKey(secretKey, key) && sign(secretKey, message, signature) && verify(key, message, signature),
        "a BCH Schnorr signature verifies");
  Message other = message;
  other[0] ^= 1;
  check(!verify(key, other, signature), "a BCH Schnorr signature does not verify another message");
  Point otherKey;
  publicKey(randomKey(), otherKey);
  check(!verify(otherKey, message, signature), "a BCH Schnorr signature does not verify under another key");
  Signature flipped = signature;
  flipped[40] ^= 1;
  check(!verify(key, message, flipped), "a changed s is rejected");
  for (int round = 0; round < 32; ++round) {
    Signature each;
    const Message m = randomMessage();
    if (!sign(secretKey, m, each) || !verify(key, m, each)) {
      check(false, "signatures over 32 random messages verify");
      return;
    }
  }
  check(true, "signatures over 32 random messages verify");
}

static void testAdaptorRounds() {
  int residueRounds = 0;
  int nonResidueRounds = 0;
  bool allRoundsHold = true;
  for (int round = 0; round < 64 && allRoundsHold; ++round) {
    const Scalar secretKey = randomKey();
    Point key;
    publicKey(secretKey, key);
    const Adaptor adaptor = swapAdaptor();
    const Message message = randomMessage();
    AdaptorPresig presig;
    Signature signature;
    Scalar recovered;
    allRoundsHold = BchSchnorr::presign(secretKey, message, adaptor.T, presig) && BchSchnorr::verifyPresig(key, message, adaptor.T, presig) &&
                    BchSchnorr::complete(presig, adaptor.t, signature) && verify(key, message, signature) &&
                    BchSchnorr::extract(presig, signature, adaptor.T, recovered) && recovered == adaptor.t;
    (nonceHasResidueY(presig) ? residueRounds : nonResidueRounds) += 1;
  }
  check(allRoundsHold, "64 rounds: pre-sign, verify, complete into a BCH signature, extract t");
  check(residueRounds > 0 && nonResidueRounds > 0,
        "the rounds cover both y-residue cases of R0 (" + std::to_string(residueRounds) + " residue, " +
            std::to_string(nonResidueRounds) + " non-residue)");
}

struct Presigned {
  Scalar secretKey;
  Point key;
  Adaptor adaptor;
  Message message;
  AdaptorPresig presig;
};

static Presigned newPresigned() {
  Presigned fixture;
  fixture.secretKey = randomKey();
  publicKey(fixture.secretKey, fixture.key);
  fixture.adaptor = swapAdaptor();
  fixture.message = randomMessage();
  BchSchnorr::presign(fixture.secretKey, fixture.message, fixture.adaptor.T, fixture.presig);
  return fixture;
}

static void testPresignatureRejections() {
  const Presigned f = newPresigned();
  check(BchSchnorr::verifyPresig(f.key, f.message, f.adaptor.T, f.presig), "the fixture pre-signature verifies");
  check(!BchSchnorr::verifyPresig(f.key, f.message, swapAdaptor().T, f.presig), "a pre-signature does not open to another point");
  Message other = f.message;
  other[5] ^= 1;
  check(!BchSchnorr::verifyPresig(f.key, other, f.adaptor.T, f.presig), "a pre-signature covers its message");
  Point otherKey;
  publicKey(randomKey(), otherKey);
  check(!BchSchnorr::verifyPresig(otherKey, f.message, f.adaptor.T, f.presig), "a pre-signature belongs to its signer");
  AdaptorPresig tampered = f.presig;
  tampered.sPrime[31] ^= 1;
  check(!BchSchnorr::verifyPresig(f.key, f.message, f.adaptor.T, tampered), "a changed s' is rejected");
  tampered = f.presig;
  tampered.R0[20] ^= 1;
  check(!BchSchnorr::verifyPresig(f.key, f.message, f.adaptor.T, tampered), "a changed R0 is rejected");

  Signature signature;
  Scalar recovered;
  BchSchnorr::complete(f.presig, f.adaptor.t, signature);
  check(!BchSchnorr::extract(f.presig, signature, swapAdaptor().T, recovered), "t is not extracted against another point");
  Scalar wrongSecret = f.adaptor.t;
  wrongSecret[31] ^= 1;
  Signature wrong;
  check(BchSchnorr::complete(f.presig, wrongSecret, wrong) && !verify(f.key, f.message, wrong),
        "completing with another scalar gives no valid signature");
  Signature foreign;
  sign(f.secretKey, f.message, foreign);
  check(!BchSchnorr::extract(f.presig, foreign, f.adaptor.T, recovered), "a signature from another nonce yields no t");
}

int main() {
  testPublishedVectors();
  testPlainSignature();
  testAdaptorRounds();
  testPresignatureRejections();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
