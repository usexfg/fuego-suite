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

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "SwapDaemon/Crypto/CrossCurveDleq.h"
#include "crypto/crypto.h"
#include "crypto/cross_dleq.h"
#include "crypto/secp_adaptor.h"

using namespace Crypto;
using namespace XfgSwap;

static int g_checks = 0;
static int g_failures = 0;

static void check(bool condition, const std::string& label) {
  ++g_checks;
  if (!condition) ++g_failures;
  std::printf("%s  %s\n", condition ? "ok   " : "FAIL ", label.c_str());
}

struct Statement {
  SecretKey t;
  PublicKey edPoint;
  SecpPubKey secpPoint;
  std::vector<uint8_t> proof;
};

static Statement proven() {
  Statement statement;
  cross_dleq_generate_scalar(statement.t);
  CrossCurveDleq::prove(statement.t, statement.edPoint, statement.secpPoint, statement.proof);
  return statement;
}

static bool verifies(const Statement& statement) {
  return CrossCurveDleq::verify(statement.edPoint, statement.secpPoint, statement.proof);
}

static void testCompleteness() {
  bool all = true;
  for (int round = 0; round < 4 && all; ++round) all = verifies(proven());
  check(all, "proofs for fresh scalars verify");
}

// The library derives both points from t; they must be the points the swap
// derives itself, or Bob's own exchange would be refused.
static void testPointsMatchTheSwapsDerivation() {
  const Statement statement = proven();
  PublicKey edPoint;
  SecpPubKey secpPoint;
  check(secret_key_to_public_key(statement.t, edPoint) && edPoint == statement.edPoint,
        "the ed25519 point is the XFG adaptor point t*G");
  check(secp_point_from_ed_secret(statement.t, secpPoint) && secpPoint == statement.secpPoint,
        "the secp256k1 point is T_secp for the same integer t");
}

static void testStatementBinding() {
  const Statement statement = proven();
  const Statement other = proven();
  check(!CrossCurveDleq::verify(other.edPoint, statement.secpPoint, statement.proof),
        "another ed25519 point is refused");
  check(!CrossCurveDleq::verify(statement.edPoint, other.secpPoint, statement.proof),
        "another secp256k1 point is refused");
  check(!CrossCurveDleq::verify(statement.edPoint, other.secpPoint, other.proof),
        "points with different logarithms are refused under either proof");
  SecpPubKey negated = statement.secpPoint;
  negated.data[0] ^= 0x01;
  check(!CrossCurveDleq::verify(statement.edPoint, negated, statement.proof), "the negated secp256k1 point is refused");
}

static void testTampering() {
  const Statement statement = proven();
  bool everyChangeRefused = true;
  for (size_t position = 0; position < statement.proof.size() && everyChangeRefused; position += 61) {
    Statement changed = statement;
    changed.proof[position] ^= 0x01;
    everyChangeRefused = !verifies(changed);
  }
  check(everyChangeRefused, "a proof changed at any sampled byte is refused");

  Statement truncated = statement;
  truncated.proof.pop_back();
  check(!verifies(truncated), "a truncated proof is refused");
  Statement extended = statement;
  extended.proof.push_back(0);
  check(!verifies(extended), "a proof with a trailing byte is refused");
  Statement empty = statement;
  empty.proof.clear();
  check(!verifies(empty), "an empty proof is refused");
  Statement oversized = statement;
  oversized.proof.assign(CrossCurveDleq::MAX_PROOF_BYTES + 1, 0);
  check(!verifies(oversized), "a proof above the size bound is refused");
  Statement garbage = statement;
  garbage.proof.assign(statement.proof.size(), 0xff);
  check(!verifies(garbage), "bytes that are no proof are refused without a crash");
}

static void testMalformedPoints() {
  const Statement statement = proven();
  static const uint8_t ORDER_TWO_POINT[32] = {0xec, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f};
  PublicKey smallOrder;
  std::memcpy(smallOrder.data, ORDER_TWO_POINT, 32);
  check(!CrossCurveDleq::verify(smallOrder, statement.secpPoint, statement.proof),
        "a small-order ed25519 point is refused");
  PublicKey offCurve;
  std::memset(offCurve.data, 0x02, 32);
  check(!CrossCurveDleq::verify(offCurve, statement.secpPoint, statement.proof), "bytes that are no ed25519 point are refused");
  SecpPubKey badPrefix = statement.secpPoint;
  badPrefix.data[0] = 0x05;
  check(!CrossCurveDleq::verify(statement.edPoint, badPrefix, statement.proof), "a malformed secp256k1 point is refused");
}

static void testScalarBounds() {
  PublicKey edPoint;
  SecpPubKey secpPoint;
  std::vector<uint8_t> proof;
  SecretKey zero{};
  check(!CrossCurveDleq::prove(zero, edPoint, secpPoint, proof), "t = 0 is refused");
  SecretKey tooLarge{};
  reinterpret_cast<uint8_t*>(&tooLarge)[31] = 0x10;
  check(!CrossCurveDleq::prove(tooLarge, edPoint, secpPoint, proof), "t = 2^252 is refused");
  bool allInRange = true;
  for (int i = 0; i < 2000 && allInRange; ++i) {
    SecretKey t;
    cross_dleq_generate_scalar(t);
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&t);
    bool nonzero = false;
    for (size_t b = 0; b < 32; ++b) nonzero |= bytes[b] != 0;
    allInRange = nonzero && (bytes[31] & 0xf0) == 0;
  }
  check(allInRange, "generated scalars are nonzero and below 2^252");
}

static void testSizeAndTiming() {
  const auto start = std::chrono::steady_clock::now();
  const Statement statement = proven();
  const auto proved = std::chrono::steady_clock::now();
  const bool valid = verifies(statement);
  const auto verified = std::chrono::steady_clock::now();
  const Statement other = proven();
  check(valid && statement.proof.size() == other.proof.size() &&
            statement.proof.size() <= CrossCurveDleq::MAX_PROOF_BYTES &&
            CrossCurveDleq::maxProofSize() == CrossCurveDleq::MAX_PROOF_BYTES,
        "the proof is " + std::to_string(statement.proof.size()) + " bytes, within the wire bound");
  std::printf("       prove %lld ms, verify %lld ms\n",
              static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(proved - start).count()),
              static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(verified - proved).count()));
}

int main() {
  testCompleteness();
  testPointsMatchTheSwapsDerivation();
  testStatementBinding();
  testTampering();
  testMalformedPoints();
  testScalarBounds();
  testSizeAndTiming();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
