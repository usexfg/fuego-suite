// Standalone PoW reproducibility test for block 1,000,001 of the v1.9.3 mainnet.
//
// Takes the parentBlob bytes captured by the POW_DIAG instrumentation, feeds
// them through cn_slow_hash with the same (variant, light) parameters the
// daemon used, and prints the resulting hash. Lets us verify whether the
// algorithm on this platform produces the value miners produced for this
// block (top word should fit < 2^64 / 31300056, i.e. start near zero).

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "crypto/hash.h"

// Captured from POW_DIAG on M1 (2026-05-17 14:52), block 1,000,001:
// parentBlob_len=76
static const char* kParentBlobHex =
  "0100c8affccf06"
  "881344ac64f0fb7b550f143ea209b5e6d1f233bff29ec45d2aba5dbf87074e24"
  "0b2c2f00"
  "e84b387448a02d4b431dfd418c79621fa93c2fc689aa4afdb210b7346b69140b"
  "01";

// blockBlob (#54 format, currently used by hearth's V9 path)
static const char* kBlockBlobHex =
  "0900"
  "881344ac64f0fb7b550f143ea209b5e6d1f233bff29ec45d2aba5dbf87074e24"
  "1d2e1a385596cdde6a4718d4833d9bd8c8d0a677d29018df0fbf88e1dab151b1"
  "01";

// The PoW hash the M1 daemon computed for block_blob (failed):
static const char* kExpectedM1BlockHash =
  "eded3840999f9a17c0547fead1384d36d698ff05bec73a3e041bd4562dba144d";

// Expected difficulty for block 1,000,001 per explorer:
static const uint64_t kExpectedDifficulty = 31300056ULL;

static std::vector<uint8_t> from_hex(const char* hex) {
  std::vector<uint8_t> out;
  size_t len = std::strlen(hex);
  out.reserve(len / 2);
  for (size_t i = 0; i + 1 < len; i += 2) {
    auto nibble = [](char c) {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return 0;
    };
    out.push_back(static_cast<uint8_t>((nibble(hex[i]) << 4) | nibble(hex[i + 1])));
  }
  return out;
}

static std::string to_hex(const void* data, size_t len) {
  static const char* d = "0123456789abcdef";
  std::string s;
  s.resize(len * 2);
  auto* p = reinterpret_cast<const uint8_t*>(data);
  for (size_t i = 0; i < len; ++i) {
    s[2 * i] = d[p[i] >> 4];
    s[2 * i + 1] = d[p[i] & 0xf];
  }
  return s;
}

// CryptoNote's check_hash: hash * difficulty must fit in 256 bits (no overflow
// into the upper 64-bit word when multiplied by difficulty).
static bool check_hash(const Crypto::Hash& hash, uint64_t difficulty) {
  auto words = reinterpret_cast<const uint64_t*>(&hash);
  // Top word: words[3] (little-endian, last 8 bytes).
  __uint128_t product = static_cast<__uint128_t>(words[3]) * difficulty;
  return (product >> 64) == 0;
}

static void run(const char* label, const std::vector<uint8_t>& blob,
                int light, int variant) {
  Crypto::cn_context ctx;
  Crypto::Hash h{};
  Crypto::cn_slow_hash(ctx, blob.data(), blob.size(), h, light, variant);
  uint64_t top = reinterpret_cast<const uint64_t*>(&h)[3];
  bool pass = check_hash(h, kExpectedDifficulty);
  std::printf("[%s]\n", label);
  std::printf("  input  len=%zu (variant=%d, light=%d)\n", blob.size(), variant, light);
  std::printf("  hash   = %s\n", to_hex(&h, sizeof(h)).c_str());
  std::printf("  top64  = 0x%016llx  (need < 2^64/%llu = ~0x%016llx)\n",
              static_cast<unsigned long long>(top),
              static_cast<unsigned long long>(kExpectedDifficulty),
              static_cast<unsigned long long>(~0ULL / kExpectedDifficulty));
  std::printf("  check_hash(diff=%llu) = %s\n\n",
              static_cast<unsigned long long>(kExpectedDifficulty),
              pass ? "PASS" : "FAIL");
}

int main() {
  std::printf("PoW reproducibility test for block 1,000,001\n");
  std::printf("Expected miner-produced hash satisfies difficulty %llu\n",
              static_cast<unsigned long long>(kExpectedDifficulty));
  std::printf("Target top64 must be < 0x%016llx\n\n",
              static_cast<unsigned long long>(~0ULL / kExpectedDifficulty));

  auto parent = from_hex(kParentBlobHex);
  auto block  = from_hex(kBlockBlobHex);

  run("parentBlob + CNv2-Lite (variant=2, light=1)", parent, 1, 2);
  run("blockBlob  + CNv2-Lite (variant=2, light=1)", block,  1, 2);

  std::printf("Daemon-observed hash for blockBlob path: %s\n", kExpectedM1BlockHash);
  std::printf("(If the [blockBlob...] line above matches this exactly, the algorithm\n");
  std::printf(" is reproducible on this M1; the bug is purely the blob format choice.\n");
  std::printf(" If parentBlob's check_hash = PASS, that's the format miners use and\n");
  std::printf(" reverting PR #54 will fix the sync.)\n");
  return 0;
}
