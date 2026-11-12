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

#include "cross_dleq_inhouse.h"

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>
#include <openssl/sha.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "hash.h"

namespace Crypto {

extern "C" {
#include "crypto-ops.h"
#include "random.h"
}

namespace InHouseCrossDleq {

namespace {

using Bytes32 = std::array<uint8_t, 32>;
using Bytes33 = std::array<uint8_t, 33>;
using Challenge = std::array<uint8_t, CROSS_DLEQ_CHALLENGE_BYTES>;

constexpr size_t BIT_WIRE_SIZE = 32 + 33 + CROSS_DLEQ_CHALLENGE_BYTES + 2 * 32 + 2 * 32;
constexpr size_t PROOF_WIRE_SIZE = CROSS_DLEQ_BITS * BIT_WIRE_SIZE + 32 + 32 + 33 + 32;

constexpr Bytes32 ED_GROUP_ORDER = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7,
                                    0xa2, 0xde, 0xf9, 0xde, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10};
constexpr Bytes32 ED_IDENTITY = {1};
constexpr Bytes32 ED_SCALAR_ONE = {1};
constexpr Bytes32 ED_SCALAR_ZERO = {};

const char ED_GENERATOR_TAG[] = "Fuego/cross-dleq/v1/H_ed";
const char SECP_GENERATOR_TAG[] = "Fuego/cross-dleq/v1/H_secp";
const char BIT_TRANSCRIPT_TAG[] = "Fuego/cross-dleq/v1/bit";
const char ED_SUM_TAG[] = "Fuego/cross-dleq/v1/sum-ed";
const char SECP_SUM_TAG[] = "Fuego/cross-dleq/v1/sum-secp";

// ── constant-time helpers ───────────────────────────────────────────────

template <size_t N>
std::array<uint8_t, N> selectBytes(uint8_t chooseSecond, const std::array<uint8_t, N>& first,
                                   const std::array<uint8_t, N>& second) {
  const uint8_t mask = static_cast<uint8_t>(0 - (chooseSecond & 1));
  std::array<uint8_t, N> out{};
  for (size_t i = 0; i < N; ++i) out[i] = static_cast<uint8_t>(first[i] ^ ((first[i] ^ second[i]) & mask));
  return out;
}

void wipe(void* data, size_t size) {
  volatile uint8_t* bytes = static_cast<volatile uint8_t*>(data);
  while (size--) *bytes++ = 0;
}

void appendBytes(std::vector<uint8_t>& out, const void* data, size_t size) {
  const uint8_t* bytes = static_cast<const uint8_t*>(data);
  out.insert(out.end(), bytes, bytes + size);
}

void appendTag(std::vector<uint8_t>& out, const char* tag) { appendBytes(out, tag, std::strlen(tag)); }

Bytes32 keccak(const std::vector<uint8_t>& data) {
  Hash digest;
  cn_fast_hash(data.data(), data.size(), digest);
  Bytes32 out;
  std::memcpy(out.data(), digest.data, 32);
  return out;
}

// ── ed25519 ─────────────────────────────────────────────────────────────

Bytes32 edEncode(const ge_p3& point) {
  Bytes32 out;
  ge_p3_tobytes(out.data(), &point);
  return out;
}

Bytes32 edEncodeP2(const ge_p2& point) {
  Bytes32 out;
  ge_tobytes(out.data(), &point);
  return out;
}

bool edDecode(const uint8_t* bytes, ge_p3& point) { return ge_frombytes_vartime(&point, bytes) == 0; }

bool edInPrimeOrderSubgroup(const ge_p3& point) {
  ge_p2 product;
  ge_scalarmult(&product, ED_GROUP_ORDER.data(), &point);
  return edEncodeP2(product) == ED_IDENTITY;
}

bool edDecodeSubgroupPoint(const uint8_t* bytes, ge_p3& point) {
  return edDecode(bytes, point) && edInPrimeOrderSubgroup(point) && edEncode(point) != ED_IDENTITY;
}

ge_p3 deriveEdPedersenGenerator() {
  std::vector<uint8_t> seed;
  appendTag(seed, ED_GENERATOR_TAG);
  const Bytes32 digest = keccak(seed);
  ge_p2 candidate;
  ge_fromfe_frombytes_vartime(&candidate, digest.data());
  ge_p1p1 cleared;
  ge_mul8(&cleared, &candidate);
  ge_p3 generator;
  ge_p1p1_to_p3(&generator, &cleared);
  return generator;
}

const ge_p3& edPedersenGenerator() {
  static const ge_p3 generator = deriveEdPedersenGenerator();
  return generator;
}

struct EdGeneratorTable {
  ge_dsmp table;
  EdGeneratorTable() { ge_dsm_precomp(table, &edPedersenGenerator()); }
};

const ge_cached* edPedersenTable() {
  static const EdGeneratorTable precomputed;
  return precomputed.table;
}

const ge_p3& edBasePoint() {
  static const ge_p3 base = [] {
    ge_p3 point;
    ge_scalarmult_base(&point, ED_SCALAR_ONE.data());
    return point;
  }();
  return base;
}

Bytes32 edRandomScalar() {
  uint8_t wide[64];
  generate_random_bytes(sizeof(wide), wide);
  sc_reduce(wide);
  Bytes32 scalar;
  std::memcpy(scalar.data(), wide, 32);
  wipe(wide, sizeof(wide));
  return scalar;
}

bool edCanonicalScalar(const uint8_t* scalar) { return sc_check(scalar) == 0; }

ge_p3 edAdd(const ge_p3& a, const ge_p3& b) {
  ge_cached cached;
  ge_p3_to_cached(&cached, &b);
  ge_p1p1 sum;
  ge_add(&sum, &a, &cached);
  ge_p3 out;
  ge_p1p1_to_p3(&out, &sum);
  return out;
}

ge_p3 edSub(const ge_p3& a, const ge_p3& b) {
  ge_cached cached;
  ge_p3_to_cached(&cached, &b);
  ge_p1p1 difference;
  ge_sub(&difference, &a, &cached);
  ge_p3 out;
  ge_p1p1_to_p3(&out, &difference);
  return out;
}

ge_p3 edDouble(const ge_p3& point) {
  ge_p2 asP2;
  ge_p3_to_p2(&asP2, &point);
  ge_p1p1 doubled;
  ge_p2_dbl(&doubled, &asP2);
  ge_p3 out;
  ge_p1p1_to_p3(&out, &doubled);
  return out;
}

// scalar * H_ed, constant time in the scalar.
bool edMulPedersen(const Bytes32& scalar, ge_p3& out) {
  ge_p2 product;
  ge_scalarmult(&product, scalar.data(), &edPedersenGenerator());
  return edDecode(edEncodeP2(product).data(), out);
}

// a*A + b*H_ed (variable time: public inputs only).
Bytes32 edLinearCombination(const Bytes32& a, const ge_p3& A, const Bytes32& b) {
  ge_p2 combination;
  ge_double_scalarmult_precomp_vartime(&combination, a.data(), &A, b.data(), edPedersenTable());
  return edEncodeP2(combination);
}

// ── secp256k1 (OpenSSL) ─────────────────────────────────────────────────

struct BnDeleter { void operator()(BIGNUM* bn) const { BN_clear_free(bn); } };
struct PointDeleter { void operator()(EC_POINT* point) const { EC_POINT_clear_free(point); } };
struct CtxDeleter { void operator()(BN_CTX* ctx) const { BN_CTX_free(ctx); } };
using Bn = std::unique_ptr<BIGNUM, BnDeleter>;
using Point = std::unique_ptr<EC_POINT, PointDeleter>;
using Ctx = std::unique_ptr<BN_CTX, CtxDeleter>;

const EC_GROUP* secpGroup() {
  static EC_GROUP* group = EC_GROUP_new_by_curve_name(NID_secp256k1);
  return group;
}

const BIGNUM* secpOrder() { return EC_GROUP_get0_order(secpGroup()); }

Ctx newCtx() { return Ctx(BN_CTX_new()); }
Point newPoint() { return Point(EC_POINT_new(secpGroup())); }

Bn secretBn(const uint8_t* bigEndian, size_t size) {
  Bn bn(BN_bin2bn(bigEndian, static_cast<int>(size), nullptr));
  if (bn) BN_set_flags(bn.get(), BN_FLG_CONSTTIME);
  return bn;
}

Bytes32 bnToBytes(const BIGNUM* bn) {
  Bytes32 out{};
  BN_bn2binpad(bn, out.data(), 32);
  return out;
}

bool secpScalarInRange(const Bytes32& bigEndian) {
  Bn value(BN_bin2bn(bigEndian.data(), 32, nullptr));
  return value && !BN_is_zero(value.get()) && BN_cmp(value.get(), secpOrder()) < 0;
}

Bytes32 secpRandomScalar() {
  for (;;) {
    Bytes32 candidate;
    generate_random_bytes(candidate.size(), candidate.data());
    if (secpScalarInRange(candidate)) return candidate;
  }
}

bool secpEncode(const EC_POINT* point, Bytes33& out, BN_CTX* ctx) {
  if (EC_POINT_is_at_infinity(secpGroup(), point)) return false;
  return EC_POINT_point2oct(secpGroup(), point, POINT_CONVERSION_COMPRESSED, out.data(), out.size(), ctx) ==
         out.size();
}

bool secpDecode(const uint8_t* bytes, Point& point, BN_CTX* ctx) {
  point = newPoint();
  return point && EC_POINT_oct2point(secpGroup(), point.get(), bytes, 33, ctx) == 1 &&
         !EC_POINT_is_at_infinity(secpGroup(), point.get()) &&
         EC_POINT_is_on_curve(secpGroup(), point.get(), ctx) == 1;
}

Bytes33 deriveSecpPedersenGenerator() {
  Ctx ctx = newCtx();
  for (uint32_t counter = 0;; ++counter) {
    std::vector<uint8_t> seed;
    appendTag(seed, SECP_GENERATOR_TAG);
    const uint8_t counterBytes[4] = {static_cast<uint8_t>(counter >> 24), static_cast<uint8_t>(counter >> 16),
                                     static_cast<uint8_t>(counter >> 8), static_cast<uint8_t>(counter)};
    appendBytes(seed, counterBytes, sizeof(counterBytes));
    Bytes33 candidate;
    candidate[0] = 0x02;
    SHA256(seed.data(), seed.size(), candidate.data() + 1);
    Point point;
    if (secpDecode(candidate.data(), point, ctx.get())) return candidate;
  }
}

const Bytes33& secpPedersenGeneratorBytes() {
  static const Bytes33 generator = deriveSecpPedersenGenerator();
  return generator;
}

Point secpPedersenGenerator(BN_CTX* ctx) {
  Point generator;
  secpDecode(secpPedersenGeneratorBytes().data(), generator, ctx);
  return generator;
}

// scalar * P (single-point multiplication: OpenSSL uses its constant-time ladder).
bool secpMul(const EC_POINT* P, const BIGNUM* scalar, Point& out, BN_CTX* ctx) {
  out = newPoint();
  return out && EC_POINT_mul(secpGroup(), out.get(), nullptr, P, scalar, ctx) == 1;
}

bool secpMulBase(const BIGNUM* scalar, Point& out, BN_CTX* ctx) {
  out = newPoint();
  return out && EC_POINT_mul(secpGroup(), out.get(), scalar, nullptr, nullptr, ctx) == 1;
}

bool secpAdd(const EC_POINT* a, const EC_POINT* b, Point& out, BN_CTX* ctx) {
  out = newPoint();
  return out && EC_POINT_add(secpGroup(), out.get(), a, b, ctx) == 1;
}

bool secpSub(const EC_POINT* a, const EC_POINT* b, Point& out, BN_CTX* ctx) {
  Point negated(EC_POINT_dup(b, secpGroup()));
  return negated && EC_POINT_invert(secpGroup(), negated.get(), ctx) == 1 && secpAdd(a, negated.get(), out, ctx);
}

// k - e*x mod n
bool secpMulSub(const BIGNUM* k, const BIGNUM* e, const BIGNUM* x, Bytes32& out, BN_CTX* ctx) {
  Bn product(BN_new());
  Bn difference(BN_new());
  if (!product || !difference) return false;
  BN_set_flags(product.get(), BN_FLG_CONSTTIME);
  BN_set_flags(difference.get(), BN_FLG_CONSTTIME);
  if (BN_mod_mul(product.get(), e, x, secpOrder(), ctx) != 1 ||
      BN_mod_sub(difference.get(), k, product.get(), secpOrder(), ctx) != 1)
    return false;
  out = bnToBytes(difference.get());
  return true;
}

bool secpModAdd(const BIGNUM* a, const BIGNUM* b, Bn& out, BN_CTX* ctx) {
  out.reset(BN_new());
  if (!out) return false;
  BN_set_flags(out.get(), BN_FLG_CONSTTIME);
  return BN_mod_add(out.get(), a, b, secpOrder(), ctx) == 1;
}

// ── challenges ──────────────────────────────────────────────────────────

Challenge truncatedChallenge(const std::vector<uint8_t>& transcript) {
  const Bytes32 digest = keccak(transcript);
  Challenge challenge;
  std::copy(digest.begin(), digest.begin() + challenge.size(), challenge.begin());
  return challenge;
}

// The same 128-bit integer in both scalar encodings.
Bytes32 edScalarOf(const Challenge& challenge) {
  Bytes32 scalar{};
  for (size_t i = 0; i < challenge.size(); ++i) scalar[i] = challenge[challenge.size() - 1 - i];
  return scalar;
}

Bn secpScalarOf(const Challenge& challenge) { return Bn(BN_bin2bn(challenge.data(), challenge.size(), nullptr)); }

std::vector<uint8_t> statementPrefix(const char* tag, const Hash& context, const PublicKey& A, const SecpPubKey& B) {
  std::vector<uint8_t> prefix;
  appendTag(prefix, tag);
  appendBytes(prefix, context.data, 32);
  appendBytes(prefix, A.data, 32);
  appendBytes(prefix, B.data.data(), 33);
  return prefix;
}

std::vector<uint8_t> bitTranscript(const std::vector<uint8_t>& statement, size_t index, const CrossDleqBit& bit) {
  std::vector<uint8_t> transcript = statement;
  const uint8_t indexBytes[2] = {static_cast<uint8_t>(index >> 8), static_cast<uint8_t>(index)};
  appendBytes(transcript, indexBytes, sizeof(indexBytes));
  appendBytes(transcript, bit.C_ed, 32);
  appendBytes(transcript, bit.D_secp, 33);
  return transcript;
}

Challenge ringChallenge(const std::vector<uint8_t>& bitPrefix, uint8_t branch, const Bytes32& Red,
                        const Bytes33& Rsecp) {
  std::vector<uint8_t> transcript = bitPrefix;
  transcript.push_back(branch);
  appendBytes(transcript, Red.data(), Red.size());
  appendBytes(transcript, Rsecp.data(), Rsecp.size());
  return truncatedChallenge(transcript);
}

// ── ring branch: e*(C - branch*G) + z*H on both curves ──────────────────

struct BitPoints {
  ge_p3 C;
  Point D;
};

bool decodeBitPoints(const CrossDleqBit& bit, BitPoints& points, BN_CTX* ctx) {
  return edDecodeSubgroupPoint(bit.C_ed, points.C) && secpDecode(bit.D_secp, points.D, ctx);
}

bool ringBranchCommitments(const Challenge& challenge, uint8_t branch, const BitPoints& points, const Bytes32& zEd,
                           const Bytes32& zSecp, Bytes32& Red, Bytes33& Rsecp, BN_CTX* ctx) {
  const ge_p3 shiftedC = branch ? edSub(points.C, edBasePoint()) : points.C;
  Red = edLinearCombination(edScalarOf(challenge), shiftedC, zEd);

  Point shiftedD(EC_POINT_dup(points.D.get(), secpGroup()));
  if (!shiftedD) return false;
  if (branch && !secpSub(points.D.get(), EC_GROUP_get0_generator(secpGroup()), shiftedD, ctx)) return false;
  Point H = secpPedersenGenerator(ctx);
  Bn e = secpScalarOf(challenge);
  Bn z(BN_bin2bn(zSecp.data(), 32, nullptr));
  Point eD, zH, sum;
  return H && e && z && secpMul(shiftedD.get(), e.get(), eD, ctx) && secpMul(H.get(), z.get(), zH, ctx) &&
         secpAdd(eD.get(), zH.get(), sum, ctx) && secpEncode(sum.get(), Rsecp, ctx);
}

// ── proving ─────────────────────────────────────────────────────────────

struct BitOpening {
  Bytes32 rEd;
  Bytes32 sSecp;
};

bool commitBit(uint8_t bitValue, CrossDleqBit& bit, BitOpening& opening, BN_CTX* ctx) {
  opening.rEd = edRandomScalar();
  ge_p3 rH;
  if (!edMulPedersen(opening.rEd, rH)) return false;
  const Bytes32 withoutG = edEncode(rH);
  const Bytes32 withG = edEncode(edAdd(rH, edBasePoint()));
  const Bytes32 C = selectBytes(bitValue, withoutG, withG);
  std::memcpy(bit.C_ed, C.data(), 32);

  opening.sSecp = secpRandomScalar();
  Bn s = secretBn(opening.sSecp.data(), 32);
  Point H = secpPedersenGenerator(ctx);
  Point sH, sHplusG;
  Bytes33 secpWithoutG, secpWithG;
  if (!s || !H || !secpMul(H.get(), s.get(), sH, ctx) ||
      !secpAdd(sH.get(), EC_GROUP_get0_generator(secpGroup()), sHplusG, ctx) ||
      !secpEncode(sH.get(), secpWithoutG, ctx) || !secpEncode(sHplusG.get(), secpWithG, ctx))
    return false;
  const Bytes33 D = selectBytes(bitValue, secpWithoutG, secpWithG);
  std::memcpy(bit.D_secp, D.data(), 33);
  return true;
}

bool proveBit(const std::vector<uint8_t>& statement, size_t index, uint8_t bitValue, CrossDleqBit& bit,
              BitOpening& opening, BN_CTX* ctx) {
  if (!commitBit(bitValue, bit, opening, ctx)) return false;
  const std::vector<uint8_t> prefix = bitTranscript(statement, index, bit);
  BitPoints points;
  if (!decodeBitPoints(bit, points, ctx)) return false;

  const uint8_t real = bitValue;
  const uint8_t simulated = static_cast<uint8_t>(1 - bitValue);

  const Bytes32 kEd = edRandomScalar();
  const Bytes32 kSecp = secpRandomScalar();
  ge_p3 kH;
  Bn k = secretBn(kSecp.data(), 32);
  Point H = secpPedersenGenerator(ctx);
  Point kHsecp;
  Bytes33 realRsecp;
  if (!edMulPedersen(kEd, kH) || !k || !H || !secpMul(H.get(), k.get(), kHsecp, ctx) ||
      !secpEncode(kHsecp.get(), realRsecp, ctx))
    return false;
  const Challenge simulatedChallenge = ringChallenge(prefix, real, edEncode(kH), realRsecp);

  const Bytes32 zEdSimulated = edRandomScalar();
  const Bytes32 zSecpSimulated = secpRandomScalar();
  Bytes32 candidateRed[2];
  Bytes33 candidateRsecp[2];
  for (uint8_t branch = 0; branch < 2; ++branch)
    if (!ringBranchCommitments(simulatedChallenge, branch, points, zEdSimulated, zSecpSimulated,
                               candidateRed[branch], candidateRsecp[branch], ctx))
      return false;
  const Bytes32 simulatedRed = selectBytes(simulated, candidateRed[0], candidateRed[1]);
  const Bytes33 simulatedRsecp = selectBytes(simulated, candidateRsecp[0], candidateRsecp[1]);
  const Challenge realChallenge = ringChallenge(prefix, simulated, simulatedRed, simulatedRsecp);

  Bytes32 zEdReal;
  sc_mulsub(zEdReal.data(), edScalarOf(realChallenge).data(), opening.rEd.data(), kEd.data());
  Bytes32 zSecpReal;
  Bn e = secpScalarOf(realChallenge);
  Bn s = secretBn(opening.sSecp.data(), 32);
  if (!e || !s || !secpMulSub(k.get(), e.get(), s.get(), zSecpReal, ctx) || !secpScalarInRange(zSecpReal))
    return false;

  const Bytes32 zEdBranch0 = selectBytes(real, zEdReal, zEdSimulated);
  const Bytes32 zEdBranch1 = selectBytes(real, zEdSimulated, zEdReal);
  const Bytes32 zSecpBranch0 = selectBytes(real, zSecpReal, zSecpSimulated);
  const Bytes32 zSecpBranch1 = selectBytes(real, zSecpSimulated, zSecpReal);
  const Challenge firstChallenge = selectBytes(real, realChallenge, simulatedChallenge);
  std::memcpy(bit.z_ed[0], zEdBranch0.data(), 32);
  std::memcpy(bit.z_ed[1], zEdBranch1.data(), 32);
  std::memcpy(bit.z_secp[0], zSecpBranch0.data(), 32);
  std::memcpy(bit.z_secp[1], zSecpBranch1.data(), 32);
  std::memcpy(bit.e0, firstChallenge.data(), firstChallenge.size());
  return true;
}

uint8_t bitOf(const SecretKey& x, size_t index) {
  return static_cast<uint8_t>((reinterpret_cast<const uint8_t*>(&x)[index / 8] >> (index % 8)) & 1);
}

bool belowCrossCurveBound(const SecretKey& x) {
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&x);
  bool nonzero = false;
  for (size_t i = 0; i < 32; ++i) nonzero |= bytes[i] != 0;
  return nonzero && (bytes[31] & 0xF0) == 0;
}

Bytes32 bigEndianOf(const SecretKey& x) {
  Bytes32 out;
  std::memcpy(out.data(), &x, 32);
  std::reverse(out.begin(), out.end());
  return out;
}

Bytes32 sumTranscriptDigest(const char* tag, const Hash& context, const PublicKey& A, const SecpPubKey& B,
                            const uint8_t* P, size_t pSize, const uint8_t* K, size_t kSize) {
  std::vector<uint8_t> transcript = statementPrefix(tag, context, A, B);
  appendBytes(transcript, P, pSize);
  appendBytes(transcript, K, kSize);
  return keccak(transcript);
}

Bytes32 edSumChallenge(const Hash& context, const PublicKey& A, const SecpPubKey& B, const Bytes32& P,
                       const Bytes32& K) {
  Bytes32 digest = sumTranscriptDigest(ED_SUM_TAG, context, A, B, P.data(), P.size(), K.data(), K.size());
  sc_reduce32(digest.data());
  return digest;
}

Bn secpSumChallenge(const Hash& context, const PublicKey& A, const SecpPubKey& B, const Bytes33& P,
                    const Bytes33& K, BN_CTX* ctx) {
  const Bytes32 digest = sumTranscriptDigest(SECP_SUM_TAG, context, A, B, P.data(), P.size(), K.data(), K.size());
  Bn raw(BN_bin2bn(digest.data(), 32, nullptr));
  Bn reduced(BN_new());
  if (!raw || !reduced || BN_nnmod(reduced.get(), raw.get(), secpOrder(), ctx) != 1) return Bn();
  return reduced;
}

// ── verification of the weighted sums ───────────────────────────────────

bool edWeightedSumMinus(const CrossDleqProof& proof, const ge_p3& A, ge_p3& out) {
  ge_p3 accumulator;
  if (!edDecode(proof.bits[CROSS_DLEQ_BITS - 1].C_ed, accumulator)) return false;
  for (size_t i = CROSS_DLEQ_BITS - 1; i-- > 0;) {
    ge_p3 C;
    if (!edDecode(proof.bits[i].C_ed, C)) return false;
    accumulator = edAdd(edDouble(accumulator), C);
  }
  out = edSub(accumulator, A);
  return true;
}

bool secpWeightedSumMinus(const CrossDleqProof& proof, const EC_POINT* B, Point& out, BN_CTX* ctx) {
  Point accumulator;
  if (!secpDecode(proof.bits[CROSS_DLEQ_BITS - 1].D_secp, accumulator, ctx)) return false;
  for (size_t i = CROSS_DLEQ_BITS - 1; i-- > 0;) {
    Point doubled = newPoint();
    Point D;
    if (!doubled || EC_POINT_dbl(secpGroup(), doubled.get(), accumulator.get(), ctx) != 1 ||
        !secpDecode(proof.bits[i].D_secp, D, ctx) || !secpAdd(doubled.get(), D.get(), accumulator, ctx))
      return false;
  }
  return secpSub(accumulator.get(), B, out, ctx);
}

bool verifyEdSum(const CrossDleqProof& proof, const Hash& context, const PublicKey& A, const SecpPubKey& B,
                 const ge_p3& Aed) {
  ge_p3 P;
  ge_p3 K;
  if (!edWeightedSumMinus(proof, Aed, P) || !edDecode(proof.R_ed, K) || !edCanonicalScalar(proof.z_R_ed))
    return false;
  Bytes32 Kbytes;
  std::memcpy(Kbytes.data(), proof.R_ed, 32);
  const Bytes32 c = edSumChallenge(context, A, B, edEncode(P), Kbytes);
  Bytes32 z;
  std::memcpy(z.data(), proof.z_R_ed, 32);
  return edLinearCombination(c, P, z) == Kbytes;
}

bool verifySecpSum(const CrossDleqProof& proof, const Hash& context, const PublicKey& A, const SecpPubKey& B,
                   const EC_POINT* Bsecp, BN_CTX* ctx) {
  Point P;
  Point K;
  Bytes33 Pbytes;
  Bytes33 Kbytes;
  Bytes32 zBytes;
  std::memcpy(zBytes.data(), proof.z_S_secp, 32);
  std::memcpy(Kbytes.data(), proof.R_secp, 33);
  if (!secpWeightedSumMinus(proof, Bsecp, P, ctx) || !secpEncode(P.get(), Pbytes, ctx) ||
      !secpDecode(proof.R_secp, K, ctx) || !secpScalarInRange(zBytes))
    return false;
  Bn c = secpSumChallenge(context, A, B, Pbytes, Kbytes, ctx);
  Bn z(BN_bin2bn(zBytes.data(), 32, nullptr));
  Point H = secpPedersenGenerator(ctx);
  Point cP, zH, sum;
  Bytes33 recomputed;
  return c && z && H && secpMul(P.get(), c.get(), cP, ctx) && secpMul(H.get(), z.get(), zH, ctx) &&
         secpAdd(cP.get(), zH.get(), sum, ctx) && secpEncode(sum.get(), recomputed, ctx) && recomputed == Kbytes;
}

bool verifyBit(const std::vector<uint8_t>& statement, size_t index, const CrossDleqBit& bit, BN_CTX* ctx) {
  BitPoints points;
  if (!decodeBitPoints(bit, points, ctx)) return false;
  for (int branch = 0; branch < 2; ++branch) {
    Bytes32 zSecp;
    std::memcpy(zSecp.data(), bit.z_secp[branch], 32);
    if (!edCanonicalScalar(bit.z_ed[branch]) || !secpScalarInRange(zSecp)) return false;
  }
  const std::vector<uint8_t> prefix = bitTranscript(statement, index, bit);
  Challenge challenge;
  std::memcpy(challenge.data(), bit.e0, challenge.size());
  for (uint8_t branch = 0; branch < 2; ++branch) {
    Bytes32 zEd, zSecp;
    std::memcpy(zEd.data(), bit.z_ed[branch], 32);
    std::memcpy(zSecp.data(), bit.z_secp[branch], 32);
    Bytes32 Red;
    Bytes33 Rsecp;
    if (!ringBranchCommitments(challenge, branch, points, zEd, zSecp, Red, Rsecp, ctx)) return false;
    challenge = ringChallenge(prefix, branch, Red, Rsecp);
  }
  return std::memcmp(challenge.data(), bit.e0, challenge.size()) == 0;
}

}  // namespace

bool cross_dleq_prove(const SecretKey& x, const Hash& context, PublicKey& A_ed, SecpPubKey& B_secp,
                      CrossDleqProof& proof) {
  if (!belowCrossCurveBound(x)) return false;
  Ctx ctx = newCtx();
  if (!ctx) return false;

  ge_p3 Aed;
  ge_scalarmult_base(&Aed, reinterpret_cast<const uint8_t*>(&x));
  ge_p3_tobytes(reinterpret_cast<uint8_t*>(A_ed.data), &Aed);
  Bytes32 xBigEndian = bigEndianOf(x);
  Bn xSecp = secretBn(xBigEndian.data(), 32);
  wipe(xBigEndian.data(), xBigEndian.size());
  Point Bsecp;
  if (!xSecp || !secpMulBase(xSecp.get(), Bsecp, ctx.get()) || !secpEncode(Bsecp.get(), B_secp.data, ctx.get()))
    return false;

  const std::vector<uint8_t> statement = statementPrefix(BIT_TRANSCRIPT_TAG, context, A_ed, B_secp);
  Bytes32 negatedEdBlindSum = ED_SCALAR_ZERO;
  Bytes32 edPower = ED_SCALAR_ONE;
  Bn secpBlindSum(BN_new());
  Bn secpPower(BN_new());
  if (!secpBlindSum || !secpPower) return false;
  BN_set_flags(secpBlindSum.get(), BN_FLG_CONSTTIME);
  BN_zero(secpBlindSum.get());
  BN_one(secpPower.get());

  for (size_t i = 0; i < CROSS_DLEQ_BITS; ++i) {
    BitOpening opening;
    if (!proveBit(statement, i, bitOf(x, i), proof.bits[i], opening, ctx.get())) return false;
    sc_mulsub(negatedEdBlindSum.data(), edPower.data(), opening.rEd.data(), negatedEdBlindSum.data());
    sc_add(edPower.data(), edPower.data(), edPower.data());
    Bn s = secretBn(opening.sSecp.data(), 32);
    Bn weighted(BN_new());
    Bn accumulated;
    if (!s || !weighted || BN_mod_mul(weighted.get(), secpPower.get(), s.get(), secpOrder(), ctx.get()) != 1 ||
        !secpModAdd(secpBlindSum.get(), weighted.get(), accumulated, ctx.get()))
      return false;
    secpBlindSum = std::move(accumulated);
    Bn doubledPower;
    if (!secpModAdd(secpPower.get(), secpPower.get(), doubledPower, ctx.get())) return false;
    secpPower = std::move(doubledPower);
    wipe(&opening, sizeof(opening));
  }

  Bytes32 edBlindSum;
  sc_sub(edBlindSum.data(), ED_SCALAR_ZERO.data(), negatedEdBlindSum.data());
  ge_p3 Ped;
  const Bytes32 kEd = edRandomScalar();
  ge_p3 Ked;
  if (!edMulPedersen(edBlindSum, Ped) || !edMulPedersen(kEd, Ked)) return false;
  const Bytes32 KedBytes = edEncode(Ked);
  const Bytes32 cEd = edSumChallenge(context, A_ed, B_secp, edEncode(Ped), KedBytes);
  std::memcpy(proof.R_ed, KedBytes.data(), 32);
  sc_mulsub(proof.z_R_ed, cEd.data(), edBlindSum.data(), kEd.data());

  Point H = secpPedersenGenerator(ctx.get());
  const Bytes32 kSecp = secpRandomScalar();
  Bn k = secretBn(kSecp.data(), 32);
  Point Psecp, Ksecp;
  Bytes33 PsecpBytes, KsecpBytes;
  if (!H || !k || !secpMul(H.get(), secpBlindSum.get(), Psecp, ctx.get()) ||
      !secpMul(H.get(), k.get(), Ksecp, ctx.get()) || !secpEncode(Psecp.get(), PsecpBytes, ctx.get()) ||
      !secpEncode(Ksecp.get(), KsecpBytes, ctx.get()))
    return false;
  Bn cSecp = secpSumChallenge(context, A_ed, B_secp, PsecpBytes, KsecpBytes, ctx.get());
  Bytes32 zSecp;
  if (!cSecp || !secpMulSub(k.get(), cSecp.get(), secpBlindSum.get(), zSecp, ctx.get()) ||
      !secpScalarInRange(zSecp))
    return false;
  std::memcpy(proof.R_secp, KsecpBytes.data(), 33);
  std::memcpy(proof.z_S_secp, zSecp.data(), 32);
  wipe(edBlindSum.data(), edBlindSum.size());
  wipe(negatedEdBlindSum.data(), negatedEdBlindSum.size());
  return true;
}

bool cross_dleq_verify(const PublicKey& A_ed, const SecpPubKey& B_secp, const Hash& context,
                       const CrossDleqProof& proof) {
  Ctx ctx = newCtx();
  ge_p3 Aed;
  Point Bsecp;
  if (!ctx || !edDecodeSubgroupPoint(reinterpret_cast<const uint8_t*>(A_ed.data), Aed) ||
      !secpDecode(B_secp.data.data(), Bsecp, ctx.get()))
    return false;
  const std::vector<uint8_t> statement = statementPrefix(BIT_TRANSCRIPT_TAG, context, A_ed, B_secp);
  for (size_t i = 0; i < CROSS_DLEQ_BITS; ++i)
    if (!verifyBit(statement, i, proof.bits[i], ctx.get())) return false;
  return verifyEdSum(proof, context, A_ed, B_secp, Aed) &&
         verifySecpSum(proof, context, A_ed, B_secp, Bsecp.get(), ctx.get());
}

void cross_dleq_serialize(const CrossDleqProof& proof, std::vector<uint8_t>& out) {
  out.clear();
  out.reserve(PROOF_WIRE_SIZE);
  for (const CrossDleqBit& bit : proof.bits) {
    appendBytes(out, bit.C_ed, sizeof(bit.C_ed));
    appendBytes(out, bit.D_secp, sizeof(bit.D_secp));
    appendBytes(out, bit.e0, sizeof(bit.e0));
    appendBytes(out, bit.z_ed, sizeof(bit.z_ed));
    appendBytes(out, bit.z_secp, sizeof(bit.z_secp));
  }
  appendBytes(out, proof.R_ed, sizeof(proof.R_ed));
  appendBytes(out, proof.z_R_ed, sizeof(proof.z_R_ed));
  appendBytes(out, proof.R_secp, sizeof(proof.R_secp));
  appendBytes(out, proof.z_S_secp, sizeof(proof.z_S_secp));
}

bool cross_dleq_deserialize(const std::vector<uint8_t>& in, CrossDleqProof& proof) {
  if (in.size() != PROOF_WIRE_SIZE) return false;
  const uint8_t* cursor = in.data();
  auto take = [&cursor](void* field, size_t size) {
    std::memcpy(field, cursor, size);
    cursor += size;
  };
  for (CrossDleqBit& bit : proof.bits) {
    take(bit.C_ed, sizeof(bit.C_ed));
    take(bit.D_secp, sizeof(bit.D_secp));
    take(bit.e0, sizeof(bit.e0));
    take(bit.z_ed, sizeof(bit.z_ed));
    take(bit.z_secp, sizeof(bit.z_secp));
  }
  take(proof.R_ed, sizeof(proof.R_ed));
  take(proof.z_R_ed, sizeof(proof.z_R_ed));
  take(proof.R_secp, sizeof(proof.R_secp));
  take(proof.z_S_secp, sizeof(proof.z_S_secp));
  return true;
}

size_t cross_dleq_serialized_size() { return PROOF_WIRE_SIZE; }

}  // namespace InHouseCrossDleq
}  // namespace Crypto
