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

// Tests for MoneroRpcClient::claimAdaptor / refundAdaptor combined-key
// behaviour and the underlying AdaptorSigScheme::combineSpendKeys helper.
//
// Written in the standalone main() + pass/fail-counter style used by the
// sibling test_adaptor_roundtrip.cpp, because this project does not enable
// the gtest harness by default (BUILD_TESTS=OFF). Each test returns true
// on success and prints a diagnostic on failure.

#include <array>
#include <cstdio>
#include <cstring>
#include <string>

#include "SwapDaemon/Monero/AdaptorSignature.h"
#include "SwapDaemon/Monero/MoneroRpcClient.h"

using namespace XfgSwap;

namespace {

// Ed25519 group order ℓ = 2^252 + 27742317777372353535851937790883648493,
// little-endian 32-byte encoding.
constexpr std::array<uint8_t, 32> kL_LE = {{
    0xED, 0xD3, 0xF5, 0x5C, 0x1A, 0x63, 0x12, 0x58,
    0xD6, 0x9C, 0xF7, 0xA2, 0xDE, 0xF9, 0xDE, 0x14,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10,
}};

// Set `out` = ℓ - n (for small n in [1, 0xFF]). Used to craft near-ℓ scalars.
std::array<uint8_t, 32> l_minus(uint8_t n) {
  std::array<uint8_t, 32> out = kL_LE;
  // Low byte of ℓ is 0xED; subtract n (assumes n < 0xED — plenty of room).
  out[0] = static_cast<uint8_t>(out[0] - n);
  return out;
}

std::array<uint8_t, 32> small_scalar(uint8_t v) {
  std::array<uint8_t, 32> out{};
  out[0] = v;
  return out;
}

bool array_eq(const std::array<uint8_t, 32>& a, const std::array<uint8_t, 32>& b) {
  return std::memcmp(a.data(), b.data(), 32) == 0;
}

std::string toHex(const std::array<uint8_t, 32>& a) {
  static const char* hex = "0123456789abcdef";
  std::string s(64, '\0');
  for (size_t i = 0; i < 32; ++i) {
    s[2 * i]     = hex[(a[i] >> 4) & 0xF];
    s[2 * i + 1] = hex[a[i]        & 0xF];
  }
  return s;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────
// Task X1: AdaptorSigScheme::combineSpendKeys
// ─────────────────────────────────────────────────────────────────────────

// Simplest vector: 1 + 2 + 3 = 6.
static bool test_combine_small_scalars() {
  std::printf("[X1.1] combineSpendKeys: 1 + 2 + 3 = 6 ... ");
  auto a = small_scalar(1);
  auto b = small_scalar(2);
  auto t = small_scalar(3);
  std::array<uint8_t, 32> out{};
  if (!AdaptorSigScheme::combineSpendKeys(a, b, t, out)) {
    std::printf("FAIL (returned false)\n");
    return false;
  }
  auto expect = small_scalar(6);
  if (!array_eq(out, expect)) {
    std::printf("FAIL (result != 6)\n  got: %s\n", toHex(out).c_str());
    return false;
  }
  std::printf("PASS\n");
  return true;
}

// Modular reduction: (ℓ - 1) + 1 + 5 ≡ 5 (mod ℓ).
static bool test_combine_near_order_reduces() {
  std::printf("[X1.2] combineSpendKeys: (ℓ-1) + 1 + 5 ≡ 5 (mod ℓ) ... ");
  auto a = l_minus(1);
  auto b = small_scalar(1);
  auto t = small_scalar(5);
  std::array<uint8_t, 32> out{};
  if (!AdaptorSigScheme::combineSpendKeys(a, b, t, out)) {
    std::printf("FAIL (returned false)\n");
    return false;
  }
  auto expect = small_scalar(5);
  if (!array_eq(out, expect)) {
    std::printf("FAIL (not reduced mod ℓ)\n  got:    %s\n  expect: %s\n",
                toHex(out).c_str(), toHex(expect).c_str());
    return false;
  }
  std::printf("PASS\n");
  return true;
}

// Zero-result rejection: (ℓ - 3) + 2 + 1 ≡ 0 (mod ℓ). Must return false and
// NOT write `out` (caller sentinel preserved).
static bool test_combine_rejects_zero_result() {
  std::printf("[X1.3] combineSpendKeys: rejects 0-scalar combined key ... ");
  auto a = l_minus(3);
  auto b = small_scalar(2);
  auto t = small_scalar(1);

  // Pre-fill `out` with a sentinel; it must remain untouched on failure.
  std::array<uint8_t, 32> out;
  out.fill(0xAB);
  std::array<uint8_t, 32> sentinel;
  sentinel.fill(0xAB);

  if (AdaptorSigScheme::combineSpendKeys(a, b, t, out)) {
    std::printf("FAIL (accepted zero result)\n");
    return false;
  }
  if (!array_eq(out, sentinel)) {
    std::printf("FAIL (out was written on failure path)\n");
    return false;
  }
  std::printf("PASS\n");
  return true;
}

// ─────────────────────────────────────────────────────────────────────────
// Task X2: MoneroRpcClient::claimAdaptor feeds combined key into sweep
// Task X3: MoneroRpcClient::refundAdaptor feeds combined key into sweep
//
// We can't talk to a real monero-wallet-rpc, and sweepSharedAddress is not
// virtual. To exercise the combine-and-forward logic, we subclass
// MoneroRpcClient and override sweepSharedAddress (now virtual) to capture
// the spend key it was handed.
// ─────────────────────────────────────────────────────────────────────────

class CapturingClient : public MoneroRpcClient {
public:
  CapturingClient() : MoneroRpcClient("", 0, "", 0) {}
  bool sweepSharedAddress(const std::string& spendKeyHex,
                          const std::string& viewKeyHex,
                          const std::string& destAddress,
                          MoneroTransferResult& result) override {
    (void)viewKeyHex; (void)destAddress;
    capturedSpendKey = spendKeyHex;
    sweepCallCount++;
    result.success   = true;
    result.txHash    = "captured";
    result.fee       = 0;
    result.error.clear();
    return sweepReturn;
  }
  std::string capturedSpendKey;
  int         sweepCallCount = 0;
  bool        sweepReturn    = true;
};

static bool test_claim_combines_before_sweep() {
  std::printf("[X2.1] claimAdaptor combines Alice+Bob+adaptor before sweep ... ");

  auto a = small_scalar(7);
  auto b = small_scalar(11);
  auto t = small_scalar(13);
  std::array<uint8_t, 32> expect{};
  if (!AdaptorSigScheme::combineSpendKeys(a, b, t, expect)) {
    std::printf("FAIL (precondition: combineSpendKeys itself failed)\n");
    return false;
  }

  CapturingClient c;
  MoneroTransferResult result;
  bool ok = c.claimAdaptor(
      toHex(a),
      toHex(b),
      toHex(t),
      /*viewKeyHex=*/"view",
      /*destAddress=*/"dest",
      result);

  if (!ok) {
    std::printf("FAIL (claim returned false)\n");
    return false;
  }
  if (c.sweepCallCount != 1) {
    std::printf("FAIL (sweep called %d times, want 1)\n", c.sweepCallCount);
    return false;
  }
  if (c.capturedSpendKey != toHex(expect)) {
    std::printf("FAIL (sweep got wrong key)\n  got:    %s\n  expect: %s\n",
                c.capturedSpendKey.c_str(), toHex(expect).c_str());
    return false;
  }
  std::printf("PASS\n");
  return true;
}

static bool test_claim_rejects_zero_combine() {
  std::printf("[X2.2] claimAdaptor refuses to sweep when combine = 0 ... ");

  // Scalars that sum to ℓ (reduces to zero).
  auto a = l_minus(3);
  auto b = small_scalar(2);
  auto t = small_scalar(1);

  CapturingClient c;
  MoneroTransferResult result;
  bool ok = c.claimAdaptor(toHex(a), toHex(b), toHex(t),
                           /*viewKeyHex=*/"v", /*destAddress=*/"d", result);
  if (ok) {
    std::printf("FAIL (claim returned true on zero-combine)\n");
    return false;
  }
  if (c.sweepCallCount != 0) {
    std::printf("FAIL (sweep called %d times, want 0)\n", c.sweepCallCount);
    return false;
  }
  std::printf("PASS\n");
  return true;
}

static bool test_claim_rejects_bad_hex() {
  std::printf("[X2.3] claimAdaptor rejects malformed hex inputs ... ");

  CapturingClient c;
  MoneroTransferResult result;
  // One short-length input — should fail hex decode, return false, no sweep.
  bool ok = c.claimAdaptor("aa", "bb", "cc", "v", "d", result);
  if (ok) {
    std::printf("FAIL (accepted short hex)\n");
    return false;
  }
  if (c.sweepCallCount != 0) {
    std::printf("FAIL (sweep was called despite bad input)\n");
    return false;
  }
  std::printf("PASS\n");
  return true;
}

// ─────────────────────────────────────────────────────────────────────────
// Task X3 tests
// ─────────────────────────────────────────────────────────────────────────

static bool test_refund_combines_both_shares() {
  std::printf("[X3.1] refundAdaptor combines both shares before sweep ... ");

  auto a = small_scalar(4);
  auto b = small_scalar(9);
  // Cooperative refund aggregates both parties' shares; the adaptor secret
  // is irrelevant on the refund branch (no on-chain reveal has occurred),
  // so we pass 0 for it and expect the combined key == a + b.
  std::array<uint8_t, 32> zero{};
  std::array<uint8_t, 32> expect{};
  if (!AdaptorSigScheme::combineSpendKeys(a, b, zero, expect)) {
    std::printf("FAIL (combineSpendKeys precondition)\n");
    return false;
  }

  CapturingClient c;
  MoneroTransferResult result;
  bool ok = c.refundAdaptor(toHex(a), toHex(b), /*viewKeyHex=*/"v",
                            /*destAddress=*/"d", result);
  if (!ok) {
    std::printf("FAIL (refund returned false)\n");
    return false;
  }
  if (c.sweepCallCount != 1) {
    std::printf("FAIL (sweep called %d times, want 1)\n", c.sweepCallCount);
    return false;
  }
  if (c.capturedSpendKey != toHex(expect)) {
    std::printf("FAIL (wrong combined key)\n  got:    %s\n  expect: %s\n",
                c.capturedSpendKey.c_str(), toHex(expect).c_str());
    return false;
  }
  std::printf("PASS\n");
  return true;
}

static bool test_refund_requires_both_shares() {
  std::printf("[X3.2] refundAdaptor fails fast if a share is missing ... ");

  CapturingClient c;
  MoneroTransferResult result;

  // Empty Alice share.
  bool okEmpty = c.refundAdaptor(
      "", std::string(64, '0'), "v", "d", result);
  if (okEmpty || c.sweepCallCount != 0) {
    std::printf("FAIL (accepted empty Alice share)\n");
    return false;
  }

  // All-zero Bob share (would bypass combine safety).
  CapturingClient c2;
  MoneroTransferResult result2;
  bool okZero = c2.refundAdaptor(
      std::string(64, '0'), std::string(64, '0'), "v", "d", result2);
  if (okZero || c2.sweepCallCount != 0) {
    std::printf("FAIL (accepted all-zero shares)\n");
    return false;
  }

  std::printf("PASS\n");
  return true;
}

// ─────────────────────────────────────────────────────────────────────────

int main() {
  std::printf("=== XMR claim/refund adaptor combined-key tests ===\n\n");
  int pass = 0, total = 0;
  struct T { const char* name; bool (*fn)(); } tests[] = {
      {"X1.1", test_combine_small_scalars},
      {"X1.2", test_combine_near_order_reduces},
      {"X1.3", test_combine_rejects_zero_result},
      {"X2.1", test_claim_combines_before_sweep},
      {"X2.2", test_claim_rejects_zero_combine},
      {"X2.3", test_claim_rejects_bad_hex},
      {"X3.1", test_refund_combines_both_shares},
      {"X3.2", test_refund_requires_both_shares},
  };
  for (auto& t : tests) {
    ++total;
    if (t.fn()) ++pass;
  }
  std::printf("\n=== %d/%d passed ===\n", pass, total);
  return (pass == total) ? 0 : 1;
}
