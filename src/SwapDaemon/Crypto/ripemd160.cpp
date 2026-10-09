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

#include "SwapDaemon/Crypto/ripemd160.h"

#include <array>
#include <cstring>

namespace XfgSwap {
namespace {

constexpr uint32_t kDigestLength = 20;
constexpr uint32_t kBlockLength = 64;

inline uint32_t rotl(uint32_t x, uint32_t n) {
  return (x << n) | (x >> (32 - n));
}

inline uint32_t rotr(uint32_t x, uint32_t n) {
  return (x >> n) | (x << (32 - n));
}

// Per-round nonlinear functions, indexed by round 0..4.
inline uint32_t f(int round, uint32_t x, uint32_t y, uint32_t z) {
  switch (round) {
    case 0: return x ^ y ^ z;
    case 1: return (x & y) | (~x & z);
    case 2: return (x | ~y) ^ z;
    case 3: return (x & z) | (y & ~z);
    default: return x ^ (y | ~z);
  }
}

// Round constants for the left and right lines.
constexpr uint32_t kK[5] = {0x00000000u, 0x5a827999u, 0x6ed9eba1u,
                            0x8f1bbcdcu, 0xa953fd4eu};
constexpr uint32_t kKPrime[5] = {0x50a28be6u, 0x5c4dd124u, 0x6d703ef3u,
                                 0x7a6d76e9u, 0x00000000u};

// Message word order per round.
constexpr uint8_t kR[5][16] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {7, 4, 13, 1, 10, 6, 15, 3, 12, 0, 9, 5, 2, 14, 11, 8},
    {3, 10, 14, 4, 9, 15, 8, 1, 2, 7, 0, 6, 13, 11, 5, 12},
    {1, 9, 11, 10, 0, 8, 12, 4, 13, 3, 7, 15, 14, 5, 6, 2},
    {4, 0, 5, 9, 7, 12, 2, 10, 14, 1, 3, 8, 11, 6, 15, 13},
};
constexpr uint8_t kRPrime[5][16] = {
    {5, 14, 7, 0, 9, 2, 11, 4, 13, 6, 15, 8, 1, 10, 3, 12},
    {6, 11, 3, 7, 0, 13, 5, 10, 14, 15, 8, 12, 4, 9, 1, 2},
    {15, 5, 1, 3, 7, 14, 6, 9, 11, 8, 12, 2, 10, 0, 4, 13},
    {8, 6, 4, 1, 3, 11, 15, 0, 5, 12, 2, 13, 9, 7, 10, 14},
    {12, 15, 10, 4, 1, 5, 8, 7, 6, 2, 13, 14, 0, 3, 9, 11},
};

// Rotation amounts. Left line rotates left, right line rotates right.
constexpr uint8_t kS[5][16] = {
    {11, 14, 15, 12, 5, 8, 7, 9, 11, 13, 14, 15, 6, 7, 9, 8},
    {7, 6, 8, 13, 11, 9, 7, 15, 7, 12, 15, 9, 11, 7, 13, 12},
    {11, 13, 6, 7, 14, 9, 13, 15, 14, 8, 13, 6, 5, 12, 7, 5},
    {11, 12, 14, 15, 14, 15, 9, 8, 9, 14, 5, 6, 8, 6, 5, 12},
    {9, 15, 5, 11, 6, 8, 13, 12, 5, 12, 13, 14, 11, 8, 5, 6},
};
constexpr uint8_t kSPrime[5][16] = {
    {8, 9, 9, 11, 13, 15, 15, 5, 7, 7, 8, 11, 14, 14, 12, 6},
    {9, 13, 15, 7, 12, 8, 9, 11, 7, 7, 12, 7, 6, 15, 13, 11},
    {9, 7, 15, 11, 8, 6, 6, 14, 12, 13, 5, 14, 13, 13, 7, 5},
    {15, 5, 8, 11, 14, 14, 6, 14, 6, 9, 12, 9, 12, 5, 15, 8},
    {8, 5, 12, 9, 12, 5, 14, 6, 8, 13, 6, 5, 15, 13, 11, 11},
};

inline uint32_t loadLE32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

inline void storeLE32(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
  p[2] = static_cast<uint8_t>(v >> 16);
  p[3] = static_cast<uint8_t>(v >> 24);
}

void compress(uint32_t h[5], const uint8_t block[kBlockLength]) {
  uint32_t x[16];
  for (int i = 0; i < 16; ++i) x[i] = loadLE32(block + i * 4);

  uint32_t a1 = h[0], b1 = h[1], c1 = h[2], d1 = h[3], e1 = h[4];
  uint32_t a2 = h[0], b2 = h[1], c2 = h[2], d2 = h[3], e2 = h[4];

  for (int j = 0; j < 80; ++j) {
    const int round = j / 16;
    // Left line.
    uint32_t t = rotl(a1 + f(round, b1, c1, d1) + x[kR[round][j % 16]] +
                          kK[round],
                      kS[round][j % 16]) + e1;
    a1 = e1; e1 = d1; d1 = rotl(c1, 10); c1 = b1; b1 = t;

    // Right line: reversed round index. Both lines rotate left — kSPrime
    // holds different amounts, not a different direction.
    t = rotl(a2 + f(4 - round, b2, c2, d2) + x[kRPrime[round][j % 16]] +
                 kKPrime[round],
             kSPrime[round][j % 16]) + e2;
    a2 = e2; e2 = d2; d2 = rotl(c2, 10); c2 = b2; b2 = t;
  }

  const uint32_t t = h[1] + c1 + d2;
  h[1] = h[2] + d1 + e2;
  h[2] = h[3] + e1 + a2;
  h[3] = h[4] + a1 + b2;
  h[4] = h[0] + b1 + c2;
  h[0] = t;
}

} // namespace

std::vector<uint8_t> ripemd160(const std::vector<uint8_t>& data) {
  uint32_t h[5] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u,
                   0xc3d2e1f0u};

  const uint8_t* p = data.data();
  size_t remaining = data.size();

  // Full blocks, then a final padded tail.
  while (remaining >= kBlockLength) {
    compress(h, p);
    p += kBlockLength;
    remaining -= kBlockLength;
  }

  // Two block slots: 56..63 remaining bytes need a second block for the
  // length field.
  std::array<uint8_t, 2 * kBlockLength> tail{};
  if (remaining > 0) std::memcpy(tail.data(), p, remaining);
  tail[remaining] = 0x80;

  // Length in bits as a 64-bit little-endian value.
  const uint64_t bitLength = static_cast<uint64_t>(data.size()) * 8;
  const size_t tailLen =
      (remaining < 56) ? kBlockLength : 2 * kBlockLength;
  for (int i = 0; i < 8; ++i) {
    tail[tailLen - 8 + i] = static_cast<uint8_t>(bitLength >> (8 * i));
  }

  for (size_t off = 0; off < tailLen; off += kBlockLength) {
    compress(h, tail.data() + off);
  }

  std::vector<uint8_t> out(kDigestLength);
  for (int i = 0; i < 5; ++i) {
    storeLE32(out.data() + i * 4, h[i]);
  }
  return out;
}

} // namespace XfgSwap