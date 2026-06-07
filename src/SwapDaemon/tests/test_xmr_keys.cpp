// Copyright (c) 2017-2026 Fuego Developers
//
// Unit test pinning the CORRECT XMR shared-key operations (spec 2026-06-06 §9):
//   - shared spend pub  = A + B            (computeSharedSpendPub)
//   - full spend key     = a + b (2-term)   (computeFullSpendKey)
// and contrasting them with the WRONG 3-term combineSpendKeys (a+b+adaptor),
// which the corrected model deprecates.

#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

#include "SwapDaemon/Monero/AdaptorSignature.h"
#include "SwapDaemon/Monero/MoneroAddress.h"
#include "crypto/crypto.h"

extern "C" {
#include "crypto/crypto-ops.h"
}

using namespace XfgSwap;

static std::vector<uint8_t> vec32(const void* p) {
  const uint8_t* b = reinterpret_cast<const uint8_t*>(p);
  return std::vector<uint8_t>(b, b + 32);
}

int main() {
  Crypto::PublicKey A_pk, B_pk;
  Crypto::SecretKey a_sk, b_sk;
  Crypto::generate_keys(A_pk, a_sk);
  Crypto::generate_keys(B_pk, b_sk);

  Crypto::EllipticCurvePoint A, B;
  std::memcpy(A.data, A_pk.data, 32);
  std::memcpy(B.data, B_pk.data, 32);
  Crypto::EllipticCurveScalar a, b;
  std::memcpy(a.data, a_sk.data, 32);
  std::memcpy(b.data, b_sk.data, 32);

  // 1. computeSharedSpendPub(A,B) == A+B == (a+b)*G.
  Crypto::EllipticCurvePoint shared = MoneroSwapProtocol::computeSharedSpendPub(A, B);
  std::vector<uint8_t> abPoint;
  assert(MoneroAddress::sharedSpendPub(vec32(A_pk.data), vec32(B_pk.data), abPoint));
  assert(std::memcmp(shared.data, abPoint.data(), 32) == 0 &&
         "computeSharedSpendPub must equal A+B");
  unsigned char abScalar[32];
  sc_add(abScalar, a_sk.data, b_sk.data);
  sc_reduce32(abScalar);
  ge_p3 R;
  ge_scalarmult_base(&R, abScalar);
  unsigned char expectPoint[32];
  ge_p3_tobytes(expectPoint, &R);
  assert(std::memcmp(shared.data, expectPoint, 32) == 0 &&
         "A+B must equal (a+b)*G");
  std::cout << "  [1] computeSharedSpendPub == A+B == (a+b)*G\n";

  // 2. computeFullSpendKey(b, a) == (b + a) mod l  (2-term, no adaptor).
  Crypto::EllipticCurveScalar full = MoneroSwapProtocol::computeFullSpendKey(b, a);
  unsigned char baScalar[32];
  sc_add(baScalar, b_sk.data, a_sk.data);
  sc_reduce32(baScalar);
  assert(std::memcmp(full.data, baScalar, 32) == 0 &&
         "computeFullSpendKey must be the 2-term a+b");
  std::cout << "  [2] computeFullSpendKey == a+b (2-term)\n";

  // 3. Contrast: the 3-term combineSpendKeys(a,b,adaptor) is a DIFFERENT key
  //    (wrong model) — proves they are not interchangeable.
  Crypto::PublicKey T_pk; Crypto::SecretKey t_sk;
  Crypto::generate_keys(T_pk, t_sk);
  std::array<uint8_t, 32> aa, bb, adaptor, combined{};
  std::memcpy(aa.data(), a_sk.data, 32);
  std::memcpy(bb.data(), b_sk.data, 32);
  std::memcpy(adaptor.data(), t_sk.data, 32);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  // Intentionally exercising the deprecated 3-term helper to prove it differs.
  assert(AdaptorSigScheme::combineSpendKeys(aa, bb, adaptor, combined));
#pragma GCC diagnostic pop
  assert(std::memcmp(combined.data(), baScalar, 32) != 0 &&
         "3-term combine must differ from the correct 2-term key");
  std::cout << "  [3] 3-term combineSpendKeys != 2-term (deprecated model)\n";

  std::cout << "=== test_xmr_keys: passed ===\n";
  return 0;
}
