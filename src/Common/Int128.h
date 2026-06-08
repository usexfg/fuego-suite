// Copyright (c) 2024 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free & open source software distributed in the hope
// it will be useful, but WITHOUT ANY WARRANTY; without even an
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You may redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation.

#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>

#if defined(__SIZEOF_INT128__) || defined(__GNUC__) || defined(__clang__)

#ifndef FUEGO_INT128_T_DEFINED
#define FUEGO_INT128_T_DEFINED
typedef          __int128  int128_t;
typedef unsigned __int128 uint128_t;
#endif

#elif defined(_MSC_VER) && defined(_M_X64)

#include <intrin.h>

#ifndef FUEGO_UINT128_T_DEFINED
#define FUEGO_UINT128_T_DEFINED

struct uint128_t {
    uint64_t lo;
    uint64_t hi;

    uint128_t() : lo(0), hi(0) {}
    uint128_t(uint64_t v) : lo(v), hi(0) {}
    uint128_t(int64_t v) : lo((uint64_t)v), hi(v < 0 ? UINT64_MAX : 0) {}
    uint128_t(uint64_t _hi, uint64_t _lo) : lo(_lo), hi(_hi) {}

    uint128_t& operator=(uint64_t v) { lo = v; hi = 0; return *this; }

    uint128_t operator+(const uint128_t& o) const {
        uint128_t r;
        r.lo = lo + o.lo;
        r.hi = hi + o.hi + (r.lo < lo ? 1 : 0);
        return r;
    }

    uint128_t& operator+=(const uint128_t& o) {
        uint64_t old = lo;
        lo += o.lo;
        hi += o.hi + (lo < old ? 1 : 0);
        return *this;
    }

    uint128_t operator-(const uint128_t& o) const {
        uint128_t r;
        r.lo = lo - o.lo;
        r.hi = hi - o.hi - (o.lo > lo ? 1 : 0);
        return r;
    }

    uint128_t& operator-=(const uint128_t& o) {
        uint64_t old = lo;
        lo -= o.lo;
        hi -= o.hi + (o.lo > old ? 1 : 0);
        return *this;
    }

    uint128_t operator*(const uint128_t& o) const {
        uint128_t r;
        r.lo = _umul128(lo, o.lo, &(r.hi));
        r.hi += hi * o.lo + o.hi * lo;
        return r;
    }

    uint128_t operator*(uint64_t v) const {
        uint128_t r;
        r.lo = _umul128(lo, v, &(r.hi));
        r.hi += hi * v;
        return r;
    }

    uint128_t operator/(const uint128_t& o) const {
        if (o.hi == 0) {
            if (o.lo == 0) throw std::runtime_error("division by zero");
            uint128_t r;
            uint64_t rem;
            r.hi = hi / o.lo;
            rem  = hi % o.lo;
            r.lo = _udiv128(rem, lo, o.lo, &rem);
            return r;
        }
        uint128_t q(0);
        uint128_t r(*this);
        int bits = 128;
        for (int i = 127; i >= 0 && r >= o; --i) {
            uint128_t div = o << i;
            if (r >= div) {
                r -= div;
                if (i >= 64) q.hi |= (uint64_t)1 << (i - 64);
                else         q.lo |= (uint64_t)1 << i;
            }
        }
        return q;
    }

    uint128_t operator/(uint64_t v) const { return *this / uint128_t(v); }
    uint128_t operator%(const uint128_t& o) const { return *this - (*this / o) * o; }
    uint128_t operator%(uint64_t v) const { return *this % uint128_t(v); }

    uint128_t operator<<(int shift) const {
        if (shift == 0) return *this;
        if (shift >= 128) return uint128_t(0, 0);
        if (shift >= 64)  return uint128_t(lo << (shift - 64), 0);
        if (shift == 0)   return *this;
        return uint128_t((hi << shift) | (lo >> (64 - shift)), lo << shift);
    }

    uint128_t operator>>(int shift) const {
        if (shift == 0) return *this;
        if (shift >= 128) return uint128_t(0, 0);
        if (shift >= 64)  return uint128_t(0, hi >> (shift - 64));
        return uint128_t(hi >> shift, (hi << (64 - shift)) | (lo >> shift));
    }

    uint128_t operator~() const { return uint128_t(~hi, ~lo); }
    uint128_t operator-() const { return ~(*this) + uint128_t(1); }
    uint128_t operator&(const uint128_t& o) const { return uint128_t(hi & o.hi, lo & o.lo); }
    uint128_t operator|(const uint128_t& o) const { return uint128_t(hi | o.hi, lo | o.lo); }
    uint128_t operator^(const uint128_t& o) const { return uint128_t(hi ^ o.hi, lo ^ o.lo); }

    bool operator==(const uint128_t& o) const { return hi == o.hi && lo == o.lo; }
    bool operator!=(const uint128_t& o) const { return !(*this == o); }
    bool operator<(const uint128_t& o) const  { return hi < o.hi || (hi == o.hi && lo < o.lo); }
    bool operator>(const uint128_t& o) const  { return o < *this; }
    bool operator<=(const uint128_t& o) const { return !(o < *this); }
    bool operator>=(const uint128_t& o) const { return !(*this < o); }

    explicit operator bool() const { return lo != 0 || hi != 0; }
    operator uint64_t() const { return lo; }
};

inline uint128_t operator*(uint64_t a, const uint128_t& b) { return b * a; }

struct int128_t {
    uint128_t val;
    bool neg;

    int128_t() : val(0), neg(false) {}
    int128_t(uint64_t v) : val(v), neg(false) {}
    int128_t(int64_t v) : val(v >= 0 ? (uint64_t)v : (uint64_t)-v), neg(v < 0) {}
    int128_t(uint128_t v, bool n) : val(v), neg(n && (bool)v) {}

    int128_t operator+(const int128_t& o) const {
        if (neg == o.neg)
            return int128_t(val + o.val, neg);
        if (val >= o.val)
            return int128_t(val - o.val, neg);
        return int128_t(o.val - val, o.neg);
    }

    int128_t operator-(const int128_t& o) const { return *this + int128_t(o.val, !o.neg); }

    int128_t operator*(const int128_t& o) const {
        return int128_t(val * o.val, neg != o.neg);
    }

    int128_t operator/(const int128_t& o) const {
        return int128_t(val / o.val, neg != o.neg);
    }

    int128_t operator%(const int128_t& o) const {
        return int128_t(val % o.val, neg);
    }

    int128_t operator<<(int shift) const { return int128_t(val << shift, neg); }
    int128_t operator>>(int shift) const { return int128_t(val >> shift, neg); }

    int128_t operator-() const { return int128_t(val, !neg); }
    int128_t operator~() const {
        int128_t r;
        if (neg) { r.val = val - uint128_t(1); r.val = ~r.val; r.neg = false; }
        else     { r.val = ~val; r.neg = true; r.val = r.val + uint128_t(1); }
        return r;
    }

    bool operator==(const int128_t& o) const { return neg == o.neg && val == o.val; }
    bool operator!=(const int128_t& o) const { return !(*this == o); }
    bool operator<(const int128_t& o) const {
        if (neg != o.neg) return neg;
        return neg ? val > o.val : val < o.val;
    }
    bool operator>(const int128_t& o) const  { return o < *this; }
    bool operator<=(const int128_t& o) const { return !(o < *this); }
    bool operator>=(const int128_t& o) const { return !(*this < o); }

    explicit operator bool() const { return (bool)val; }
    operator int64_t() const {
        uint64_t r = (uint64_t)val;
        if (neg) return -(int64_t)r;
        return (int64_t)r;
    }
};

#endif // FUEGO_UINT128_T_DEFINED

#else
#error "128-bit integer type not available on this platform"
#endif
