// Copyright (c) 2017-2026 Fuego Developers
//
// Regression test for the atomic-swap HTLC hashlock bug.
//
// The counterparty HTLC programs verify the revealed preimage against a
// committed hashlock:
//   - Solana xfg_htlc:  require!(keccak256(preimage) == hash_lock)
//   - BCH P2SH HTLC:     OP_SHA256 <hash_lock> OP_EQUALVERIFY
//
// claim() reveals params.adaptorSecret (t) as the preimage. Therefore the
// hashlock committed at lock() time MUST be H(t):
//   - SOL: keccak256(t)
//   - BCH: sha256(t)
//
// The bug: SolChainClient::lock / BchChainClient::lock committed
// podToHex(params.adaptorPoint) — i.e. T = t*G, the elliptic-curve point —
// as the hashlock. Since keccak256(t) != t*G (and sha256(t) != t*G), the
// claim can NEVER succeed: locked funds can only be refunded after timeout.
//
// This test pins:
//   1. The daemon's keccak == Ethereum/Solana keccak256 (known vectors),
//      so our derived hashlock will match what the SOL program computes.
//   2. solHashLockHex(t) == keccak256(t)  AND  != podToHex(T).
//   3. bchHashLockHex(t) == sha256(t)     AND  != podToHex(T).

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "SwapDaemon/SwapHashLock.h"
#include "SwapDaemon/BitcoinCash/HtlcScript.h"
#include "Common/StringTools.h"
#include "crypto/crypto.h"

extern "C" {
#include "crypto/keccak.h"
}

using namespace XfgSwap;

static std::string toHex(const uint8_t* p, size_t n) {
  static const char* h = "0123456789abcdef";
  std::string s; s.reserve(n * 2);
  for (size_t i = 0; i < n; ++i) { s += h[p[i] >> 4]; s += h[p[i] & 0xf]; }
  return s;
}

int main() {
  int passed = 0;

  // ── 1. Daemon keccak must equal Ethereum/Solana keccak256 ────────────────
  {
    uint8_t md[32];
    keccak(reinterpret_cast<const uint8_t*>(""), 0, md, 32);
    assert(toHex(md, 32) == "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470"
           && "keccak('') must match Ethereum/Solana keccak256");
    const char* abc = "abc";
    keccak(reinterpret_cast<const uint8_t*>(abc), 3, md, 32);
    assert(toHex(md, 32) == "4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45"
           && "keccak('abc') must match Ethereum/Solana keccak256");
    ++passed;
    std::cout << "  [1] daemon keccak == keccak256 (empty + abc vectors)\n";
  }

  // ── 1b. BchHtlcScript::sha256 must match the NIST SHA-256 vector ─────────
  {
    std::vector<uint8_t> abc = {'a', 'b', 'c'};
    std::string got = BchHtlcScript::bytesToHex(BchHtlcScript::sha256(abc));
    assert(got == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
           && "sha256('abc') must match NIST vector");
    ++passed;
    std::cout << "  [1b] BchHtlcScript::sha256 == NIST SHA-256\n";
  }

  // ── 2. SOL hashlock = keccak256(secret), NOT the adaptor point T ─────────
  {
    Crypto::PublicKey T;       // T = secret*G
    Crypto::SecretKey secret;
    Crypto::generate_keys(T, secret);

    uint8_t md[32];
    keccak(reinterpret_cast<const uint8_t*>(&secret), 32, md, 32);
    const std::string expected = toHex(md, 32);
    const std::string tHex = Common::podToHex(T);

    const std::string got = solHashLockHex(secret);
    assert(got.size() == 64);
    assert(got == expected && "solHashLockHex must equal keccak256(secret)");
    assert(got != tHex && "SOL hashlock must NOT be the adaptor point T (the bug)");
    ++passed;
    std::cout << "  [2] solHashLockHex == keccak256(secret) != T\n";
  }

  // ── 3. BCH hashlock = sha256(secret), NOT the adaptor point T ────────────
  {
    Crypto::PublicKey T;
    Crypto::SecretKey secret;
    Crypto::generate_keys(T, secret);

    std::vector<uint8_t> sbytes(reinterpret_cast<const uint8_t*>(&secret),
                                reinterpret_cast<const uint8_t*>(&secret) + 32);
    const std::string expected = BchHtlcScript::bytesToHex(BchHtlcScript::sha256(sbytes));
    const std::string tHex = Common::podToHex(T);

    const std::string got = bchHashLockHex(secret);
    assert(got.size() == 64);
    assert(got == expected && "bchHashLockHex must equal sha256(secret)");
    assert(got != tHex && "BCH hashlock must NOT be the adaptor point T (the bug)");
    ++passed;
    std::cout << "  [3] bchHashLockHex == sha256(secret) != T\n";
  }

  std::cout << "=== test_swap_hashlock: " << passed << "/4 groups passed ===\n";
  return 0;
}
