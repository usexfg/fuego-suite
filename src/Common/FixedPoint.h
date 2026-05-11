// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You can redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation. Fuego includes elements written
// by third parties. See file labeled LICENSE for more details.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <cstdint>

namespace CryptoNote {

// FixedPoint64: deterministic Q64.64 fixed-point arithmetic.
// 64-bit signed integer part + 64-bit fractional part = 128 bits total.
// Internal storage is __int128. All ops use integer arithmetic only.
// No float/double/cmath — safe for cross-architecture consensus.
//
// Range:  approx ±9.22e18
// Precision: 1 / 2^64 ≈ 5.42e-20

typedef          __int128  int128_t;
typedef unsigned __int128 uint128_t;

class FixedPoint64 {
public:
  FixedPoint64() : m_value(0) {}

  static FixedPoint64 zero()   { return FixedPoint64(0, Raw{}); }
  static FixedPoint64 one()    { return FixedPoint64(ONE, Raw{}); }
  static FixedPoint64 fromUint64(uint64_t v);
  static FixedPoint64 fromRatio(uint64_t num, uint64_t denom);
  static FixedPoint64 fromRaw(int128_t raw) { return FixedPoint64(raw, Raw{}); }

  uint64_t toUint64() const;          // truncates toward zero
  int128_t raw() const { return m_value; }

  FixedPoint64 add(FixedPoint64 o) const;
  FixedPoint64 sub(FixedPoint64 o) const;
  FixedPoint64 mul(FixedPoint64 o) const;
  FixedPoint64 div(FixedPoint64 o) const;

  // multiply by uint64 → uint64: (this * v) truncated to integer
  uint64_t mulToUint64(uint64_t v) const;

  // truncated Taylor series, fixed iteration count (deterministic)
  // exp_approx: |x| ≤ 2.0, 8 iterations
  // ln_approx:  x > 0,   8 iterations
  static FixedPoint64 exp_approx(FixedPoint64 x);
  static FixedPoint64 ln_approx(FixedPoint64 x);

  // sign and zero checks
  bool isZero()    const { return m_value == 0; }
  bool isPositive() const { return m_value > 0; }
  bool isNegative() const { return m_value < 0; }
  int  sign()      const { return (m_value > 0) ? 1 : (m_value < 0) ? -1 : 0; }

  FixedPoint64 negate() const;

  bool operator==(FixedPoint64 o) const { return m_value == o.m_value; }
  bool operator!=(FixedPoint64 o) const { return m_value != o.m_value; }
  bool operator< (FixedPoint64 o) const { return m_value <  o.m_value; }
  bool operator> (FixedPoint64 o) const { return m_value >  o.m_value; }
  bool operator<=(FixedPoint64 o) const { return m_value <= o.m_value; }
  bool operator>=(FixedPoint64 o) const { return m_value >= o.m_value; }

private:
  int128_t m_value;
  struct Raw {};
  FixedPoint64(int128_t raw, Raw) : m_value(raw) {}

  // Q64.64: ONE = 1.0 = (1 << 64)
  static const int128_t ONE = (int128_t)1 << 64;
};

} // namespace CryptoNote
