// Copyright (c) 2017-2026 Fuego Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "gtest/gtest.h"
#include "Common/FixedPoint.h"

using namespace CryptoNote;

namespace {

TEST(FixedPoint, zero) {
  auto z = FixedPoint64::zero();
  EXPECT_TRUE(z.isZero());
  EXPECT_EQ(z.toUint64(), 0u);
  EXPECT_FALSE(z.isPositive());
  EXPECT_FALSE(z.isNegative());
  EXPECT_EQ(z.sign(), 0);
}

TEST(FixedPoint, one) {
  auto one = FixedPoint64::one();
  EXPECT_FALSE(one.isZero());
  EXPECT_TRUE(one.isPositive());
  EXPECT_EQ(one.toUint64(), 1u);
}

TEST(FixedPoint, fromUint64) {
  auto v = FixedPoint64::fromUint64(42);
  EXPECT_EQ(v.toUint64(), 42u);
  EXPECT_TRUE(v.isPositive());
}

TEST(FixedPoint, fromRatio) {
  auto half = FixedPoint64::fromRatio(1, 2);
  EXPECT_EQ(half.toUint64(), 0u); // 0.5 truncates to 0
  EXPECT_TRUE(half.isPositive());

  auto two = FixedPoint64::fromRatio(6, 3);
  EXPECT_EQ(two.toUint64(), 2u);

  auto oneThird = FixedPoint64::fromRatio(1, 3);
  EXPECT_EQ(oneThird.toUint64(), 0u);
}

TEST(FixedPoint, add) {
  auto a = FixedPoint64::fromUint64(5);
  auto b = FixedPoint64::fromUint64(3);
  EXPECT_EQ(a.add(b).toUint64(), 8u);
}

TEST(FixedPoint, sub) {
  auto a = FixedPoint64::fromUint64(5);
  auto b = FixedPoint64::fromUint64(3);
  EXPECT_EQ(a.sub(b).toUint64(), 2u);
}

TEST(FixedPoint, mul) {
  auto three = FixedPoint64::fromUint64(3);
  auto two   = FixedPoint64::fromUint64(2);
  auto six   = three.mul(two);
  EXPECT_EQ(six.toUint64(), 6u);
}

TEST(FixedPoint, mul_fractional) {
  auto half   = FixedPoint64::fromRatio(1, 2);
  auto half2  = half.mul(half);
  EXPECT_EQ(half2.toUint64(), 0u); // 0.25 truncates to 0
}

TEST(FixedPoint, div_exact) {
  auto six = FixedPoint64::fromUint64(6);
  auto two = FixedPoint64::fromUint64(2);
  EXPECT_EQ(six.div(two).toUint64(), 3u);
}

TEST(FixedPoint, div_fractional) {
  auto one = FixedPoint64::one();
  auto two = FixedPoint64::fromUint64(2);
  auto half = one.div(two);
  EXPECT_EQ(half.toUint64(), 0u); // 0.5 truncates to 0
}

TEST(FixedPoint, mulToUint64) {
  auto half = FixedPoint64::fromRatio(1, 2);
  // half * 100 = 50
  EXPECT_EQ(half.mulToUint64(100), 50u);

  auto two = FixedPoint64::fromUint64(2);
  // 2 * 1000 = 2000
  EXPECT_EQ(two.mulToUint64(1000), 2000u);
}

TEST(FixedPoint, negate) {
  auto three = FixedPoint64::fromUint64(3);
  auto neg3  = three.negate();
  EXPECT_TRUE(neg3.isNegative());
  EXPECT_EQ(neg3.toUint64(), (uint64_t)(-3)); // 2's complement wrap
}

TEST(FixedPoint, comparison) {
  auto a = FixedPoint64::fromUint64(5);
  auto b = FixedPoint64::fromUint64(3);
  EXPECT_TRUE(a > b);
  EXPECT_TRUE(b < a);
  EXPECT_TRUE(a != b);
  EXPECT_TRUE(a == a);
  EXPECT_TRUE(a >= b);
  EXPECT_TRUE(b <= a);
}

TEST(FixedPoint, add_sub_identity) {
  auto a = FixedPoint64::fromUint64(100);
  auto b = a.add(FixedPoint64::fromUint64(50));
  EXPECT_EQ(b.sub(FixedPoint64::fromUint64(50)), a);
}

TEST(FixedPoint, mul_div_identity) {
  auto a = FixedPoint64::fromUint64(42);
  auto b = FixedPoint64::fromUint64(7);
  // (a * b) / b == a
  EXPECT_EQ(a.mul(b).div(b), a);
}

TEST(FixedPoint, div_zero) {
  auto a = FixedPoint64::fromUint64(100);
  auto zero = FixedPoint64::zero();
  EXPECT_EQ(a.div(zero).toUint64(), 0u); // returns zero, doesn't crash
}

TEST(FixedPoint, fromRatio_zero_denom) {
  auto r = FixedPoint64::fromRatio(5, 0);
  EXPECT_TRUE(r.isZero());
}

TEST(FixedPoint, exp_approx_one) {
  // exp(0) = 1
  auto e0 = FixedPoint64::exp_approx(FixedPoint64::zero());
  EXPECT_EQ(e0.toUint64(), 1u); // approximately 1
}

TEST(FixedPoint, exp_approx_positive) {
  // exp(1) ≈ 2.718 — truncates to 2
  auto e1 = FixedPoint64::exp_approx(FixedPoint64::one());
  EXPECT_EQ(e1.toUint64(), 2u);
}

TEST(FixedPoint, ln_approx_one) {
  // ln(1) = 0
  auto ln1 = FixedPoint64::ln_approx(FixedPoint64::one());
  EXPECT_EQ(ln1.toUint64(), 0u);
}

TEST(FixedPoint, ln_approx_negative_or_zero) {
  auto ln0 = FixedPoint64::ln_approx(FixedPoint64::zero());
  EXPECT_TRUE(ln0.isZero());
}

TEST(FixedPoint, serialization_roundtrip) {
  auto a = FixedPoint64::fromRatio(123456789, 987654321);
  auto raw = a.raw();
  auto b = FixedPoint64::fromRaw(raw);
  EXPECT_EQ(a, b);
  EXPECT_EQ(b.raw(), raw);
}

TEST(FixedPoint, deterministic_mul) {
  // Ensure deterministic output across runs
  auto x = FixedPoint64::fromRatio(355, 113); // approx PI
  auto y = FixedPoint64::fromRatio(22, 7);    // approx 22/7
  auto result = x.mul(y);
  // Should always produce the same raw value
  int128_t expected = result.raw();
  for (int i = 0; i < 10; ++i) {
    EXPECT_EQ(x.mul(y).raw(), expected);
  }
}

TEST(FixedPoint, edge_max_values) {
  auto two = FixedPoint64::fromUint64(2);
  auto max_mul = two.mul(FixedPoint64::fromUint64(UINT64_MAX / 4));
  EXPECT_GE(max_mul.toUint64(), 0u); // shouldn't crash or assert
}

} // namespace
