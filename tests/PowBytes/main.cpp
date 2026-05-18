// Standalone PoW reproducibility test for block 1,000,001 of the v1.9.3 mainnet,
// PLUS canonical CryptoNight test vectors to bisect which code path is broken.
//
// On M1 we see wrong CN-Lite-v2 hashes. To localize the bug we run:
//   - CN v0 (variant=0, light=0) : shared core only (AES, scratchpad, Keccak)
//   - CN v2 (variant=2, light=0) : shared core + VARIANT2 macros at 2MB
//   - CN v2-Lite (variant=2, light=1) : above + 128KB light path (the daemon's)
//
// If v0 FAILS  -> bug is in the shared core (AES/NEON/Keccak/__mul).
// If v0 OK, v2 FAILS  -> bug is in VARIANT2 macros (VARIANT2_2, SHUFFLE_ADD_NEON).
// If v0+v2 OK, v2-Lite FAILS -> bug is light=1 specific (the light?0x30:0x10 swap
//   in VARIANT2_SHUFFLE_ADD_NEON, or the ITER/MEMORY scaling for light scratchpad).

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "crypto/hash.h"

#if defined(__aarch64__)
#include <arm_neon.h>
#endif

// Forward decls for primitives we want to bisect.
extern "C" {
  // Provided by libCrypto.a (src/crypto/keccak.c)
  void keccak1600(const uint8_t *in, int inlen, uint8_t *md);
}

// ---------------------------------------------------------------------------
// Block 1,000,001 captured blobs (kept from the prior diagnostic for context)
// ---------------------------------------------------------------------------
static const char* kParentBlobHex =
  "0100c8affccf06"
  "881344ac64f0fb7b550f143ea209b5e6d1f233bff29ec45d2aba5dbf87074e24"
  "0b2c2f00"
  "e84b387448a02d4b431dfd418c79621fa93c2fc689aa4afdb210b7346b69140b"
  "01";

static const char* kBlockBlobHex =
  "0900"
  "881344ac64f0fb7b550f143ea209b5e6d1f233bff29ec45d2aba5dbf87074e24"
  "1d2e1a385596cdde6a4718d4833d9bd8c8d0a677d29018df0fbf88e1dab151b1"
  "01";

static const char* kExpectedM1BlockHash =
  "eded3840999f9a17c0547fead1384d36d698ff05bec73a3e041bd4562dba144d";

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
    s[2 * i]     = d[p[i] >> 4];
    s[2 * i + 1] = d[p[i] & 0xf];
  }
  return s;
}

static bool check_hash(const Crypto::Hash& hash, uint64_t difficulty) {
  auto words = reinterpret_cast<const uint64_t*>(&hash);
  __uint128_t product = static_cast<__uint128_t>(words[3]) * difficulty;
  return (product >> 64) == 0;
}

// ---------------------------------------------------------------------------
// Original difficulty-based reproducibility check (block 1,000,001).
// ---------------------------------------------------------------------------
static void run_diff(const char* label, const std::vector<uint8_t>& blob,
                     int light, int variant) {
  Crypto::cn_context ctx;
  Crypto::Hash h{};
  Crypto::cn_slow_hash(ctx, blob.data(), blob.size(), h, light, variant);
  uint64_t top = reinterpret_cast<const uint64_t*>(&h)[3];
  bool pass = check_hash(h, kExpectedDifficulty);
  std::printf("[%s]\n", label);
  std::printf("  input  len=%zu (variant=%d, light=%d)\n", blob.size(), variant, light);
  std::printf("  hash   = %s\n", to_hex(&h, sizeof(h)).c_str());
  std::printf("  top64  = 0x%016llx\n", static_cast<unsigned long long>(top));
  std::printf("  check_hash(diff=%llu) = %s\n\n",
              static_cast<unsigned long long>(kExpectedDifficulty),
              pass ? "PASS" : "FAIL");
}

// ---------------------------------------------------------------------------
// Canonical vector runner: compares computed hash to expected.
// ---------------------------------------------------------------------------
struct VectorResult {
  bool ok;
  std::string got;
  std::string expected;
  const char* label;
};

static VectorResult run_vector(const char* label,
                               const std::vector<uint8_t>& input,
                               const char* expected_hex,
                               int light, int variant) {
  Crypto::cn_context ctx;
  Crypto::Hash h{};
  Crypto::cn_slow_hash(ctx, input.data(), input.size(), h, light, variant);
  std::string got = to_hex(&h, sizeof(h));
  std::string exp = expected_hex;
  bool ok = (got == exp);
  std::printf("[%s] variant=%d light=%d input_len=%zu\n",
              label, variant, light, input.size());
  std::printf("  expected = %s\n", exp.c_str());
  std::printf("  got      = %s\n", got.c_str());
  std::printf("  result   = %s\n\n", ok ? "PASS" : "FAIL");
  return {ok, got, exp, label};
}

// "This is a test" as raw ASCII (14 bytes)
static std::vector<uint8_t> ascii(const char* s) {
  return std::vector<uint8_t>(s, s + std::strlen(s));
}

// ---------------------------------------------------------------------------
// Micro-tests for shared-core primitives.
// ---------------------------------------------------------------------------

// (1) cn_fast_hash("") -> Keccak-256("") = c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470
static bool micro_test_cn_fast_hash_empty() {
  Crypto::Hash h{};
  Crypto::cn_fast_hash(nullptr, 0, h);
  std::string got = to_hex(&h, sizeof(h));
  const char* exp = "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470";
  bool ok = (got == exp);
  std::printf("[micro] cn_fast_hash(empty) = %s\n  expect Keccak-256(empty) %s\n  %s\n\n",
              got.c_str(), exp, ok ? "PASS" : "FAIL");
  return ok;
}

// (2) keccak1600(empty) — what cn_slow_hash uses to init state. First 32 bytes
// of Keccak-permutation(initial-padded-empty) should equal Keccak-256(empty)
// because for empty input both share the same single padded block. Then we
// also check bytes 32..63 which are the next 256 bits of the 1600-bit state.
static bool micro_test_keccak1600_empty() {
  uint8_t md[200] = {0};
  keccak1600(nullptr, 0, md);
  std::string head = to_hex(md, 32);
  const char* exp = "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470";
  bool ok = (head == exp);
  std::printf("[micro] keccak1600(empty)[0..32]  = %s\n  expect (Keccak-256 empty) %s\n  %s\n",
              head.c_str(), exp, ok ? "PASS" : "FAIL");
  std::printf("        keccak1600(empty)[32..64] = %s\n\n", to_hex(md + 32, 32).c_str());
  return ok;
}

// (3) NEON AES smoke test: one round of vaeseq_u8(in, zero) + vaesmcq_u8 on
// all-zero input should equal MixColumns(SubBytes(ShiftRows(0))) (zero AddRoundKey).
// SubBytes(0) = 0x63 in every byte. ShiftRows(constant) = constant.
// MixColumns of all-0x63 = ((0x02^0x03^0x01^0x01)*0x63) = 0x01*0x63 in each
// column position = 0x63 in every byte. So expected = 16 bytes of 0x63.
// NOTE: ARM aese does AddRoundKey THEN SubBytes/ShiftRows, so vaeseq_u8(0,0)
// gives ShiftRows(SubBytes(0 ^ 0)) = 16 bytes of 0x63, then vaesmcq_u8 gives
// MixColumns of that = 16 bytes of 0x63.
static bool micro_test_neon_aes_smoke() {
#if defined(__aarch64__) && defined(__ARM_FEATURE_CRYPTO)
  uint8x16_t zero = vdupq_n_u8(0);
  uint8x16_t r = vaeseq_u8(zero, zero);   // ShiftRows(SubBytes(0 ^ 0)) = {0x63}*16
  uint8_t after_aese[16];
  vst1q_u8(after_aese, r);

  uint8x16_t m = vaesmcq_u8(r);           // MixColumns({0x63}*16) = {0x63}*16
  uint8_t after_mc[16];
  vst1q_u8(after_mc, m);

  bool aese_ok = true;
  bool mc_ok = true;
  for (int i = 0; i < 16; ++i) {
    if (after_aese[i] != 0x63) aese_ok = false;
    if (after_mc[i] != 0x63)   mc_ok = false;
  }
  std::printf("[micro] vaeseq_u8(0, 0)   = %s (expect 16x0x63)  %s\n",
              to_hex(after_aese, 16).c_str(), aese_ok ? "PASS" : "FAIL");
  std::printf("[micro] vaesmcq_u8(above) = %s (expect 16x0x63)  %s\n\n",
              to_hex(after_mc, 16).c_str(), mc_ok ? "PASS" : "FAIL");
  return aese_ok && mc_ok;
#else
  std::printf("[micro] NEON AES smoke test SKIPPED (no __ARM_FEATURE_CRYPTO)\n\n");
  return true;
#endif
}

int main() {
  std::printf("=== M1 CryptoNight bisection: shared core vs VARIANT2 vs light ===\n\n");

  std::printf("--- Sub-primitive micro tests ---\n");
  bool keccak_fast_ok = micro_test_cn_fast_hash_empty();
  bool keccak_1600_ok = micro_test_keccak1600_empty();
  bool aes_ok = micro_test_neon_aes_smoke();
  std::printf("--- end micro tests ---\n\n");
  (void)keccak_fast_ok; (void)keccak_1600_ok; (void)aes_ok;

  // -------------------------------------------------------------------------
  // (A) Canonical CN v0 vectors (variant=0, light=0) — shared core only.
  // Source: well-known CryptoNight whitepaper / Bytecoin / Monero reference.
  // -------------------------------------------------------------------------
  std::vector<VectorResult> results;

  // v0 #1: empty input
  results.push_back(run_vector(
      "CN v0 / empty",
      std::vector<uint8_t>{},
      "eb14e8a833fac6fe9a43b57b336789c46ffe93f2868452240720607b14387e11",
      /*light=*/0, /*variant=*/0));

  // v0 #2: "This is a test" (ASCII, 14 bytes)
  results.push_back(run_vector(
      "CN v0 / \"This is a test\"",
      ascii("This is a test"),
      "a084f01d1437a09c6985401b60d43554ae105802c5f5d8a9b3253649c0be6605",
      /*light=*/0, /*variant=*/0));

  // v0 #3: from Monero tests-slow.txt first line — "de omnibus dubitandum"
  // (cross-check; this rules out any concern about the well-known vectors above)
  results.push_back(run_vector(
      "CN v0 / Monero ref #1 \"de omnibus dubitandum\"",
      from_hex("6465206f6d6e69627573206475626974616e64756d"),
      "2f8e3df40bd11f9ac90c743ca8e32bb391da4fb98612aa3b6cdc639ee00b31f5",
      /*light=*/0, /*variant=*/0));

  // -------------------------------------------------------------------------
  // (B) Canonical CN v2 vectors (variant=2, light=0) — shared core + VARIANT2.
  // Source: /tmp/monero/tests/hash/tests-slow-2.txt (Monero reference).
  // Each line is "expected_hash<space>input_hex".
  // -------------------------------------------------------------------------

  // v2 #1: input = "This is a test This is a test This is a test"
  results.push_back(run_vector(
      "CN v2 / 3x \"This is a test\"",
      from_hex("5468697320697320612074657374205468697320697320612074657374205468697320697320612074657374"),
      "353fdc068fd47b03c04b9431e005e00b68c2168a3cc7335c8b9b308156591a4f",
      /*light=*/0, /*variant=*/2));

  // v2 #2: input = "Lorem ipsum dolor sit amet, consectetur adipiscing"
  results.push_back(run_vector(
      "CN v2 / \"Lorem ipsum dolor sit amet...\"",
      from_hex("4c6f72656d20697073756d20646f6c6f722073697420616d65742c20636f6e73656374657475722061646970697363696e67"),
      "72f134fc50880c330fe65a2cb7896d59b2e708a0221c6a9da3f69b3a702d8682",
      /*light=*/0, /*variant=*/2));

  // -------------------------------------------------------------------------
  // (C) Daemon's actual code path: CN v2-Lite (variant=2, light=1).
  // We don't have a canonical vector for "lite v2" at this exact input, so we
  // keep the existing difficulty-based reproducibility check from block
  // 1,000,001 below to characterize the light=1 behaviour.
  // -------------------------------------------------------------------------

  // -------------------------------------------------------------------------
  // Verdict
  // -------------------------------------------------------------------------
  std::printf("=== Summary ===\n");
  int passed = 0, failed = 0;
  for (auto& r : results) {
    std::printf("  %-50s : %s\n", r.label, r.ok ? "PASS" : "FAIL");
    (r.ok ? passed : failed)++;
  }
  std::printf("Vector tests: %d passed, %d failed\n\n", passed, failed);

  bool v0_pass = results[0].ok && results[1].ok && results[2].ok;
  bool v2_pass = results[3].ok && results[4].ok;
  std::printf("Diagnostic verdict:\n");
  if (!v0_pass) {
    std::printf("  *** CN v0 FAILS -> bug is in SHARED CORE.\n");
    std::printf("      Suspects: AES rounds (NEON path in slow-hash.c),\n");
    std::printf("      scratchpad init/finalizer, Keccak, or __mul.\n");
    std::printf("      Next step: isolate sub-primitives (single AES round,\n");
    std::printf("      Keccak of empty, scratchpad fill checksum).\n");
  } else if (!v2_pass) {
    std::printf("  *** CN v0 PASS, CN v2 FAIL -> bug is in VARIANT2 macros.\n");
    std::printf("      Suspects: VARIANT2_2 (line ~211 of slow-hash.c),\n");
    std::printf("      VARIANT2_SHUFFLE_ADD_NEON non-light branch, or\n");
    std::printf("      VARIANT2_INTEGER_MATH division.\n");
  } else {
    std::printf("  *** CN v0 + CN v2 PASS -> shared core and VARIANT2 are fine.\n");
    std::printf("      Bug is light=1-specific. Check the (light ? 0x30 : 0x10)\n");
    std::printf("      chunk swap in VARIANT2_SHUFFLE_ADD_NEON (line ~134) and\n");
    std::printf("      the ITER / MEMORY scaling for the 128KB scratchpad.\n");
  }
  std::printf("\n");

  // -------------------------------------------------------------------------
  // Existing block-1,000,001 reproducibility checks (kept for continuity).
  // -------------------------------------------------------------------------
  std::printf("--- Block 1,000,001 reproducibility (daemon's actual path) ---\n");
  auto parent = from_hex(kParentBlobHex);
  auto block  = from_hex(kBlockBlobHex);
  run_diff("parentBlob + CNv2-Lite (variant=2, light=1)", parent, 1, 2);
  run_diff("blockBlob  + CNv2-Lite (variant=2, light=1)", block,  1, 2);
  std::printf("Daemon-observed hash for blockBlob path: %s\n\n", kExpectedM1BlockHash);

  return (failed == 0) ? 0 : 1;
}
