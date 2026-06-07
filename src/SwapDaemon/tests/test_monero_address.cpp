// Copyright (c) 2017-2026 Fuego Developers
//
// Unit test for MoneroAddress (XMR swap leg address encoding + shared spend pub).
//
// Anchors:
//   - encode() -> Tools::Base58::decode_addr() round-trip. decode_addr validates
//     the keccak checksum and the prefix, so a successful round-trip proves the
//     encoder produces a genuinely valid CryptoNote/Monero address (the same
//     functions the codebase uses for real Fuego addresses).
//   - sharedSpendPub(A,A) == (a+a)*G : cross-checks ed25519 point-add against
//     scalar-add via the base generator.

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "SwapDaemon/Monero/MoneroAddress.h"
#include "Common/Base58.h"
#include "crypto/crypto.h"

extern "C" {
#include "crypto/crypto-ops.h"
}

using namespace XfgSwap;

static std::vector<uint8_t> vec32(const void* p) {
  const uint8_t* b = reinterpret_cast<const uint8_t*>(p);
  return std::vector<uint8_t>(b, b + 32);
}

int main() {
  Crypto::PublicKey spend, view;
  Crypto::SecretKey ss, vs;
  Crypto::generate_keys(spend, ss);
  Crypto::generate_keys(view, vs);
  const std::vector<uint8_t> spendV = vec32(&spend);
  const std::vector<uint8_t> viewV  = vec32(&view);

  // 1. encode -> decode_addr round-trip (validates checksum + prefix + format).
  const uint64_t PREFIX = MoneroAddress::MAINNET;
  const std::string addr = MoneroAddress::encode(spendV, viewV, PREFIX);
  assert(!addr.empty() && "encode must succeed for valid keys");

  uint64_t tag = 0;
  std::string data;
  bool ok = Tools::Base58::decode_addr(addr, tag, data);
  assert(ok && "encoded address must decode (checksum valid)");
  assert(tag == PREFIX && "prefix must round-trip");
  assert(data.size() == 64 && "payload must be spend(32)||view(32)");
  assert(std::equal(spendV.begin(), spendV.end(),
                    reinterpret_cast<const uint8_t*>(data.data())) &&
         "spend pub must round-trip");
  assert(std::equal(viewV.begin(), viewV.end(),
                    reinterpret_cast<const uint8_t*>(data.data()) + 32) &&
         "view pub must round-trip");
  assert(addr[0] == '4' && "Monero mainnet addresses start with '4'");
  std::cout << "  [1] encode/decode_addr round-trip OK (" << addr.substr(0, 6) << "...)\n";

  // 2. invalid sizes -> empty.
  assert(MoneroAddress::encode({}, viewV, PREFIX).empty());
  assert(MoneroAddress::encode(spendV, std::vector<uint8_t>(31, 0), PREFIX).empty());
  std::cout << "  [2] rejects malformed key sizes\n";

  // 3. sharedSpendPub(A, A) == (ss + ss) * G   (A = spend = ss*G).
  std::vector<uint8_t> shared;
  ok = MoneroAddress::sharedSpendPub(spendV, spendV, shared);
  assert(ok && shared.size() == 32 && "A+B point add must succeed");
  unsigned char twoSs[32];
  sc_add(twoSs, reinterpret_cast<const unsigned char*>(&ss),
         reinterpret_cast<const unsigned char*>(&ss));
  ge_p3 R;
  ge_scalarmult_base(&R, twoSs);
  unsigned char expected[32];
  ge_p3_tobytes(expected, &R);
  assert(std::equal(shared.begin(), shared.end(), expected) &&
         "A+A must equal (a+a)*G");
  std::cout << "  [3] sharedSpendPub(A,A) == (a+a)*G OK\n";

  std::cout << "=== test_monero_address: passed ===\n";
  return 0;
}
