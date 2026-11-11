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

#include "SwapDaemon/Crypto/Bip340Adaptor.h"
#include "crypto/cross_dleq.h"
#include "crypto/secp_adaptor.h"

using namespace XfgSwap::Bip340;

static int g_checks = 0;
static int g_failures = 0;

static void check(bool condition, const std::string& label) {
  ++g_checks;
  if (!condition) {
    ++g_failures;
    std::printf("FAIL  %s\n", label.c_str());
  }
}

static Scalar randomKey() {
  Scalar key;
  XOnlyKey ignored;
  do RAND_bytes(key.data(), static_cast<int>(key.size()));
  while (!xOnlyPublicKey(key, ignored));
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

static void testPlainSignature() {
  const Scalar key = randomKey();
  XOnlyKey publicKey;
  Signature signature;
  const Message message = randomMessage();
  check(xOnlyPublicKey(key, publicKey) && sign(key, message, signature) && verify(publicKey, message, signature),
        "a BIP340 signature verifies");
  Message other = message;
  other[0] ^= 1;
  check(!verify(publicKey, other, signature), "a BIP340 signature does not verify another message");
}

static void testAdaptorRoundTrips() {
  bool sawEvenNonce = false;
  bool sawOddNonce = false;
  for (int round = 0; round < 64; ++round) {
    const Scalar key = randomKey();
    XOnlyKey publicKey;
    xOnlyPublicKey(key, publicKey);
    const Adaptor adaptor = swapAdaptor();
    const Message message = randomMessage();
    AdaptorPresig presig;
    const std::string label = "round " + std::to_string(round);
    if (!presign(key, message, adaptor.T, presig)) {
      check(false, label + ": pre-signs");
      continue;
    }
    (presig.R0[0] == 0x02 ? sawEvenNonce : sawOddNonce) = true;
    check(verifyPresig(publicKey, message, adaptor.T, presig), label + ": the pre-signature verifies");
    Signature signature;
    check(complete(presig, adaptor.t, signature) && verify(publicKey, message, signature),
          label + ": the completed signature is a valid BIP340 signature");
    Scalar recovered;
    check(extract(presig, signature, adaptor.T, recovered) && recovered == adaptor.t,
          label + ": t is extracted from the completed signature");
  }
  check(sawEvenNonce && sawOddNonce, "both nonce parities were exercised");
}

static void testRefusals() {
  const Scalar key = randomKey();
  XOnlyKey publicKey;
  xOnlyPublicKey(key, publicKey);
  const Adaptor adaptor = swapAdaptor();
  const Adaptor otherAdaptor = swapAdaptor();
  const Message message = randomMessage();
  AdaptorPresig presig;
  check(presign(key, message, adaptor.T, presig), "pre-signs");

  check(!verifyPresig(publicKey, message, otherAdaptor.T, presig), "a pre-signature for another point is refused");
  Message otherMessage = message;
  otherMessage[31] ^= 1;
  check(!verifyPresig(publicKey, otherMessage, adaptor.T, presig), "a pre-signature over another message is refused");
  XOnlyKey otherKey;
  xOnlyPublicKey(randomKey(), otherKey);
  check(!verifyPresig(otherKey, message, adaptor.T, presig), "a pre-signature by another key is refused");
  AdaptorPresig tampered = presig;
  tampered.sPrime[31] ^= 1;
  check(!verifyPresig(publicKey, message, adaptor.T, tampered), "a changed pre-signature is refused");

  Signature wrongCompletion;
  check(complete(presig, otherAdaptor.t, wrongCompletion) && !verify(publicKey, message, wrongCompletion),
        "completing with another secret yields no valid signature");
  Signature signature;
  complete(presig, adaptor.t, signature);
  Scalar recovered;
  check(!extract(presig, signature, otherAdaptor.T, recovered), "extraction checks t against the adaptor point");
  Signature foreign;
  sign(key, message, foreign);
  check(!extract(presig, foreign, adaptor.T, recovered), "a signature from another nonce yields no secret");
}

int main() {
  testPlainSignature();
  testAdaptorRoundTrips();
  testRefusals();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
