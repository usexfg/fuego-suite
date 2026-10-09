// Copyright (c) 2017-2026 Fuego Developers
//
// Tests RIPEMD-160 against the published reference vectors.
//
// This hash backs every BTC/LTC/KMD/BCH/DCR/DOGE/DASH/ZEC P2PKH address the
// swap daemon derives. It previously went through OpenSSL's EVP_ripemd160(),
// which returns a legacy-provider stub on OpenSSL 3.x: the digest init fails
// and the following Update/Final segfault. These vectors pin the replacement
// implementation so a silent address change cannot slip through again.
// 
#include <cstdio>
#include <string>
#include <vector>

#include "SwapDaemon/Crypto/ripemd160.h"

using namespace XfgSwap;

static std::string toHex(const std::vector<uint8_t>& v) {
  static const char* digits = "0123456789abcdef";
  std::string out;
  out.reserve(v.size() * 2);
  for (uint8_t c : v) {
    out += digits[c >> 4];
    out += digits[c & 0x0f];
  }
  return out;
}

static int g_failures = 0;

static void expectDigest(const std::string& input, const std::string& expected) {
  const std::vector<uint8_t> data(input.begin(), input.end());
  const std::string got = toHex(ripemd160(data));
  if (got == expected) {
    std::printf("  PASS  len=%-8zu %s\n", input.size(), got.c_str());
  } else {
    std::printf("  FAIL  len=%-8zu got %s want %s\n", input.size(),
                got.c_str(), expected.c_str());
    ++g_failures;
  }
}

int main() {
  std::printf("RIPEMD-160 reference vectors\n");

  expectDigest("", "9c1185a5c5e9fc54612808977ee8f548b2258d31");
  expectDigest("a", "0bdc9d2d256b3ee9daae347be6f4dc835a467ffe");
  expectDigest("abc", "8eb208f7e05d987a9b044a8e98c6b087f15a0bfc");
  expectDigest("message digest", "5d0689ef49d2fae572b881b123a85ffa21595f36");
  expectDigest("abcdefghijklmnopqrstuvwxyz",
               "f71c27109c692c1b56bbdceb5b9d2865b3708dbc");
  expectDigest("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
               "b0e20b6e3116640286ed3a87a5713079b21f5189");
  // 62 bytes: forces the two-block padding path.
  expectDigest("12345678901234567890123456789012345678901234567890123456789012345678901234567890",
               "9b752e45573d4b39f4dbd3323cab82bf63326bfb");
  expectDigest(std::string(1000000, 'a'),
               "52783243c1697bdbe16d37f97f68f08325dc1528");

  if (g_failures != 0) {
    std::printf("\n%d RIPEMD-160 vector(s) FAILED\n", g_failures);
    return 1;
  }
  std::printf("\nAll RIPEMD-160 vectors passed\n");
  return 0;
}