// Secure-zero primitive for secret material.
//
// The defect: sodium_memzero was declared in crypto/crypto-util.h but never
// defined or called anywhere, so every secret wipe in the tree was a plain
// memset -- which the optimiser may delete once the buffer is dead. A leaked
// core dump, a swapped page or a freed heap block can then still hold the key.
//
// This asserts the observable contract: after the call, the bytes are gone.

#include <cstring>

#include "gtest/gtest.h"

#include "crypto/crypto-util.h"

TEST(SecureZero, WipesEveryByte) {
  unsigned char buf[64];
  std::memset(buf, 0xAB, sizeof(buf));

  sodium_memzero(buf, sizeof(buf));

  for (size_t i = 0; i < sizeof(buf); ++i) {
    EXPECT_EQ(0, buf[i]) << "byte " << i << " survived the wipe";
  }
}

TEST(SecureZero, WipesPartialRange) {
  unsigned char buf[32];
  std::memset(buf, 0xFF, sizeof(buf));

  sodium_memzero(buf, 8);

  for (size_t i = 0; i < 8; ++i) {
    EXPECT_EQ(0, buf[i]) << "byte " << i << " should have been wiped";
  }
  for (size_t i = 8; i < sizeof(buf); ++i) {
    EXPECT_EQ(0xFF, buf[i]) << "byte " << i << " is outside the range and must be untouched";
  }
}

TEST(SecureZero, ZeroLengthIsANoOp) {
  unsigned char buf[8];
  std::memset(buf, 0x11, sizeof(buf));

  sodium_memzero(buf, 0);

  EXPECT_EQ(0x11, buf[0]);
}

TEST(SecureZero, OldBytesAreUnrecoverable) {
  unsigned char buf[128];
  std::memset(buf, 0x5A, sizeof(buf));
  unsigned char copy[128];
  std::memcpy(copy, buf, sizeof(buf));

  sodium_memzero(buf, sizeof(buf));

  EXPECT_NE(0, std::memcmp(buf, copy, sizeof(buf)));
}

TEST(SodiumCompare, EqualBuffersCompareEqual) {
  const unsigned char a[16] = {0};
  const unsigned char b[16] = {0};
  EXPECT_NE(0, sodium_compare(a, b, sizeof(a)));
}

TEST(SodiumCompare, DifferentBuffersCompareDifferent) {
  unsigned char a[16] = {0};
  unsigned char b[16] = {0};
  b[15] = 1;  // differs only in the LAST byte -- the case a timing leak would expose
  EXPECT_EQ(0, sodium_compare(a, b, sizeof(a)));
}

TEST(SodiumCompare, DetectsDifferenceInEveryPosition) {
  // A compare that only inspected part of the buffer would pass this for some
  // offsets. Sweep every position so a short read cannot hide.
  for (size_t i = 0; i < 16; ++i) {
    unsigned char a[16] = {0};
    unsigned char b[16] = {0};
    b[i] = 0x80;
    EXPECT_EQ(0, sodium_compare(a, b, sizeof(a))) << "missed a difference at offset " << i;
  }
}