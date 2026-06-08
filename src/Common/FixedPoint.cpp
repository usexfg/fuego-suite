// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See file labeled LICENSE for more details.

#include "FixedPoint.h"

namespace CryptoNote {

const int128_t FixedPoint64::ONE = int128_t(uint64_t(1) << 64);

static const uint64_t MASK64 = (uint64_t)-1;

FixedPoint64 FixedPoint64::fromUint64(uint64_t v) {
  return FixedPoint64((int128_t)v << 64, Raw{});
}

FixedPoint64 FixedPoint64::fromRatio(uint64_t num, uint64_t denom) {
  if (denom == 0)
    return FixedPoint64::zero();
  int128_t val = ((int128_t)num << 64) / (int128_t)denom;
  return FixedPoint64(val, Raw{});
}

uint64_t FixedPoint64::toUint64() const {
  return (uint64_t)(m_value >> 64);
}

FixedPoint64 FixedPoint64::add(FixedPoint64 o) const {
  return FixedPoint64(m_value + o.m_value, Raw{});
}

FixedPoint64 FixedPoint64::sub(FixedPoint64 o) const {
  return FixedPoint64(m_value - o.m_value, Raw{});
}

FixedPoint64 FixedPoint64::mul(FixedPoint64 o) const {
  int64_t  a_hi = (int64_t)(m_value >> 64);
  uint64_t a_lo = (uint64_t)(m_value - ((int128_t)a_hi << 64));
  int64_t  b_hi = (int64_t)(o.m_value >> 64);
  uint64_t b_lo = (uint64_t)(o.m_value - ((int128_t)b_hi << 64));

  int128_t result = ((int128_t)a_hi * b_hi) << 64;
  result += (int128_t)a_hi * b_lo;
  result += (int128_t)a_lo * b_hi;
  result += (int128_t)(((uint128_t)a_lo * b_lo) >> 64);

  return FixedPoint64(result, Raw{});
}

uint64_t FixedPoint64::mulToUint64(uint64_t v) const {
  return (uint64_t)((int128_t)(m_value * (int128_t)v) >> 64);
}

FixedPoint64 FixedPoint64::div(FixedPoint64 o) const {
  if (o.m_value == 0)
    return zero();

  int128_t q = m_value / o.m_value;
  int128_t r = m_value % o.m_value;

  int128_t result = (q << 64) + ((r << 64) / o.m_value);
  return FixedPoint64(result, Raw{});
}

FixedPoint64 FixedPoint64::negate() const {
  return FixedPoint64(-m_value, Raw{});
}

FixedPoint64 FixedPoint64::exp_approx(FixedPoint64 x) {
  int128_t two = int128_t(uint64_t(2)) << 64;
  if (x.m_value > two)  x.m_value = two;
  if (x.m_value < -two) x.m_value = -two;

  FixedPoint64 sum   = one();
  FixedPoint64 term  = one();
  FixedPoint64 xpow  = one();

  for (int k = 1; k <= 8; ++k) {
    xpow = x.mul(xpow);
    static const uint64_t fact[9] = {1, 1, 2, 6, 24, 120, 720, 5040, 40320};
    term = xpow.div(fromUint64(fact[k]));
    sum  = sum.add(term);
  }
  return sum;
}

FixedPoint64 FixedPoint64::ln_approx(FixedPoint64 x) {
  if (x.m_value <= 0)
    return zero();

  FixedPoint64 y   = x.sub(one());
  FixedPoint64 sum = y;
  FixedPoint64 ypow = y;
  int128_t     sign = -ONE;

  for (int k = 2; k <= 8; ++k) {
    ypow = ypow.mul(y);
    FixedPoint64 k_fp = fromUint64((uint64_t)k);
    FixedPoint64 term = ypow.div(k_fp);
    if (sign > 0)
      sum = sum.add(term);
    else
      sum = sum.sub(term);
    sign = -sign;
  }
  return sum;
}

} // namespace CryptoNote
