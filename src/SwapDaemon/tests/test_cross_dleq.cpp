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
#include <memory>
#include <string>
#include <vector>

#include "crypto/crypto.h"
#include "crypto/cross_dleq.h"
#include "crypto/secp_adaptor.h"

using namespace Crypto;

static int g_checks = 0;
static int g_failures = 0;

static void check(bool condition, const std::string& label) {
  ++g_checks;
  if (!condition) ++g_failures;
  std::printf("%s  %s\n", condition ? "ok   " : "FAIL ", label.c_str());
}

struct Statement {
  SecretKey x;
  Hash context;
  PublicKey A;
  SecpPubKey B;
  std::unique_ptr<CrossDleqProof> proof = std::make_unique<CrossDleqProof>();
};

static Hash contextOf(uint8_t fill) {
  Hash context;
  std::memset(context.data, fill, sizeof(context.data));
  return context;
}

static bool proveFresh(Statement& statement, uint8_t contextFill = 0x5a) {
  cross_dleq_generate_scalar(statement.x);
  statement.context = contextOf(contextFill);
  return cross_dleq_prove(statement.x, statement.context, statement.A, statement.B, *statement.proof);
}

static bool verifies(const Statement& statement, const CrossDleqProof& proof) {
  return cross_dleq_verify(statement.A, statement.B, statement.context, proof);
}

static std::unique_ptr<CrossDleqProof> copyOf(const CrossDleqProof& proof) {
  return std::make_unique<CrossDleqProof>(proof);
}

static void testCompleteness(const Statement& statement) {
  PublicKey expectedA;
  check(secret_key_to_public_key(statement.x, expectedA) && expectedA == statement.A,
        "A is x*G on ed25519, the XFG adaptor point");
  SecpPubKey expectedB;
  check(secp_point_from_ed_secret(statement.x, expectedB) && expectedB == statement.B,
        "B is x*G on secp256k1, as secp_point_from_ed_secret derives it");
  check(verifies(statement, *statement.proof), "an honest proof verifies");
}

static void testStatementBinding(const Statement& statement) {
  Statement other;
  check(proveFresh(other), "a second statement proves");
  check(!cross_dleq_verify(other.A, statement.B, statement.context, *statement.proof),
        "the proof does not verify for another ed25519 point");
  check(!cross_dleq_verify(statement.A, other.B, statement.context, *statement.proof),
        "the proof does not verify for another secp256k1 point");
  check(!cross_dleq_verify(statement.A, other.B, statement.context, *other.proof),
        "mixing one statement's ed25519 point with another's secp256k1 point and proof fails");
  check(!cross_dleq_verify(statement.A, statement.B, contextOf(0x5b), *statement.proof),
        "the proof is bound to its swap context");

  SecretKey flipped = statement.x;
  reinterpret_cast<uint8_t*>(&flipped)[0] ^= 1;
  SecpPubKey flippedB;
  check(secp_point_from_ed_secret(flipped, flippedB) &&
            !cross_dleq_verify(statement.A, flippedB, statement.context, *statement.proof),
        "a secp256k1 point one bit away from x is refused");
}

static void testTampering(const Statement& statement) {
  auto tamperedRing = copyOf(*statement.proof);
  tamperedRing->bits[17].e0[0] ^= 1;
  check(!verifies(statement, *tamperedRing), "a changed ring challenge is refused");

  auto tamperedResponse = copyOf(*statement.proof);
  tamperedResponse->bits[100].z_secp[1][31] ^= 1;
  check(!verifies(statement, *tamperedResponse), "a changed secp256k1 response is refused");

  auto tamperedEdResponse = copyOf(*statement.proof);
  tamperedEdResponse->bits[200].z_ed[0][0] ^= 1;
  check(!verifies(statement, *tamperedEdResponse), "a changed ed25519 response is refused");

  auto swappedBits = copyOf(*statement.proof);
  std::swap(swappedBits->bits[3], swappedBits->bits[4]);
  check(!verifies(statement, *swappedBits), "reordered bit proofs are refused");

  Statement other;
  check(proveFresh(other), "a donor statement proves");
  auto crossedCommitment = copyOf(*statement.proof);
  std::memcpy(crossedCommitment->bits[9].D_secp, other.proof->bits[9].D_secp, 33);
  check(!verifies(statement, *crossedCommitment),
        "a secp256k1 bit commitment taken from another proof is refused");

  auto tamperedSum = copyOf(*statement.proof);
  tamperedSum->z_R_ed[5] ^= 1;
  check(!verifies(statement, *tamperedSum), "a changed ed25519 sum response is refused");
  auto tamperedSecpSum = copyOf(*statement.proof);
  tamperedSecpSum->z_S_secp[31] ^= 1;
  check(!verifies(statement, *tamperedSecpSum), "a changed secp256k1 sum response is refused");
}

static void testMalformedEncodings(const Statement& statement) {
  static const uint8_t ED_ORDER[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7,
                                       0xa2, 0xde, 0xf9, 0xde, 0x14, 0, 0, 0, 0, 0, 0,
                                       0,    0,    0,    0,    0,    0,    0,    0,    0,    0x10};
  static const uint8_t SECP_ORDER[32] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                         0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xBA, 0xAE, 0xDC, 0xE6, 0xAF, 0x48,
                                         0xA0, 0x3B, 0xBF, 0xD2, 0x5E, 0x8C, 0xD0, 0x36, 0x41, 0x41};
  // (0, -1): the ed25519 point of order 2.
  static const uint8_t ORDER_TWO_POINT[32] = {0xec, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f};

  auto nonCanonicalEd = copyOf(*statement.proof);
  std::memcpy(nonCanonicalEd->bits[2].z_ed[1], ED_ORDER, 32);
  check(!verifies(statement, *nonCanonicalEd), "an ed25519 response equal to the group order is refused");

  auto outOfRangeSecp = copyOf(*statement.proof);
  std::memcpy(outOfRangeSecp->bits[2].z_secp[0], SECP_ORDER, 32);
  check(!verifies(statement, *outOfRangeSecp), "a secp256k1 response equal to the group order is refused");

  auto smallOrderCommitment = copyOf(*statement.proof);
  std::memcpy(smallOrderCommitment->bits[0].C_ed, ORDER_TWO_POINT, 32);
  check(!verifies(statement, *smallOrderCommitment), "a small-order ed25519 commitment is refused");

  auto offCurve = copyOf(*statement.proof);
  offCurve->bits[1].D_secp[0] = 0x05;
  check(!verifies(statement, *offCurve), "a malformed secp256k1 commitment is refused");

  PublicKey smallOrderA;
  std::memcpy(smallOrderA.data, ORDER_TWO_POINT, 32);
  check(!cross_dleq_verify(smallOrderA, statement.B, statement.context, *statement.proof),
        "a small-order ed25519 statement point is refused");
}

static void testScalarBounds() {
  auto proof = std::make_unique<CrossDleqProof>();
  PublicKey A;
  SecpPubKey B;
  SecretKey zero{};
  check(!cross_dleq_prove(zero, contextOf(1), A, B, *proof), "x = 0 is refused");
  SecretKey tooLarge{};
  reinterpret_cast<uint8_t*>(&tooLarge)[31] = 0x10;
  check(!cross_dleq_prove(tooLarge, contextOf(1), A, B, *proof), "x = 2^252 is refused");

  bool allInRange = true;
  for (int i = 0; i < 2000; ++i) {
    SecretKey x;
    cross_dleq_generate_scalar(x);
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&x);
    bool nonzero = false;
    for (int j = 0; j < 32; ++j) nonzero |= bytes[j] != 0;
    allInRange &= nonzero && (bytes[31] & 0xF0) == 0;
  }
  check(allInRange, "generated scalars are nonzero and below 2^252");
}

static void testSerialization(const Statement& statement) {
  std::vector<uint8_t> wire;
  cross_dleq_serialize(*statement.proof, wire);
  check(wire.size() == cross_dleq_serialized_size() && wire.size() == 52797,
        "the wire form is " + std::to_string(cross_dleq_serialized_size()) + " bytes");
  auto decoded = std::make_unique<CrossDleqProof>();
  check(cross_dleq_deserialize(wire, *decoded) && verifies(statement, *decoded), "the wire form round-trips");
  wire.pop_back();
  check(!cross_dleq_deserialize(wire, *decoded), "a truncated wire form is refused");
}

int main() {
  Statement statement;
  const auto proveStart = std::chrono::steady_clock::now();
  check(proveFresh(statement), "prove");
  const auto proveEnd = std::chrono::steady_clock::now();
  check(verifies(statement, *statement.proof), "verify");
  const auto verifyEnd = std::chrono::steady_clock::now();
  std::printf("prove %lld ms, verify %lld ms\n",
              static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(proveEnd - proveStart).count()),
              static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(verifyEnd - proveEnd).count()));

  testCompleteness(statement);
  testStatementBinding(statement);
  testTampering(statement);
  testMalformedEncodings(statement);
  testScalarBounds();
  testSerialization(statement);
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
