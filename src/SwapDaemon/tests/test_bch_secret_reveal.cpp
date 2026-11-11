// Copyright (c) 2017-2026 Fuego Developers
//
// BCH Alice-locks secret-reveal offline path:
//   t  →  H = SHA256(t)  (= bchHashLockHex)
//   redeem script with H
//   claim scriptSig reveals t
//   parseClaimPreimage recovers t
//   verify SHA256(extracted) == H

#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

#include "SwapDaemon/BitcoinCash/HtlcScript.h"
#include "SwapDaemon/SwapHashLock.h"
#include "Common/StringTools.h"
#include "crypto/crypto.h"

using namespace XfgSwap;

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) { ++g_pass; std::cout << "  PASS: " << m << "\n"; } \
  else { ++g_fail; std::cerr << "  FAIL: " << m << "\n"; } } while (0)

int main() {
  std::cout << "=== BCH secret-reveal offline ===\n";

  Crypto::PublicKey T;
  Crypto::SecretKey t;
  Crypto::generate_keys(T, t);

  // Alice-locks hashlock
  std::string hashHex = bchHashLockHex(t);
  auto hashBytes = BchHtlcScript::hexToBytes(hashHex);
  auto secretBytes = BchHtlcScript::hexToBytes(Common::podToHex(t));
  CHECK(hashBytes.size() == 32 && secretBytes.size() == 32, "sizes");
  auto recomputed = BchHtlcScript::sha256(secretBytes);
  CHECK(recomputed == hashBytes, "bchHashLockHex == SHA256(t)");
  CHECK(hashHex != Common::podToHex(T), "hashlock is not adaptor point T");

  // Keys for HTLC parties (compressed pubkeys — use random valid-looking 33-byte)
  std::vector<uint8_t> recipientPub(33, 0x02);
  recipientPub[32] = 0x11;
  std::vector<uint8_t> senderPub(33, 0x03);
  senderPub[32] = 0x22;
  uint32_t timeout = 500000;

  auto redeem = BchHtlcScript::createRedeemScript(hashBytes, recipientPub, senderPub, timeout);
  auto p2sh = BchHtlcScript::redeemScriptToP2shScriptPubKey(redeem);
  CHECK(!redeem.empty() && p2sh.size() == 23, "redeem + p2sh built");

  // Fake DER sig (parseClaimPreimage only needs script structure)
  std::vector<uint8_t> sig(71, 0xab);
  sig[0] = 0x30; sig[1] = 0x44; sig[2] = 0x02; sig[3] = 0x20; sig[70] = 0x41;
  auto scriptSig = BchHtlcScript::createClaimScriptSig(sig, secretBytes, redeem);

  // Prefer library builder for claim path (nSequence RBF)
  std::string fakeTxid(64, 'a');
  std::string dest = "1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa";
  auto rawTx = BchHtlcScript::buildRawTransaction(
      fakeTxid, 0, 100000, scriptSig, dest, 99000, /*nLockTime=*/0);
  CHECK(!rawTx.empty(), "buildRawTransaction claim tx");

  auto extracted = BchHtlcScript::parseClaimPreimage(rawTx, p2sh);
  CHECK(extracted.size() == 32, "extracted 32-byte preimage");
  CHECK(extracted == secretBytes, "extracted == adaptor secret t");
  auto H2 = BchHtlcScript::sha256(extracted);
  CHECK(H2 == hashBytes, "SHA256(extracted) == committed hashlock");

  // Wrong P2SH must not extract
  std::vector<uint8_t> wrongP2sh = p2sh;
  wrongP2sh[2] ^= 0xff;
  auto none = BchHtlcScript::parseClaimPreimage(rawTx, wrongP2sh);
  CHECK(none.empty(), "wrong p2sh yields empty extract");

  std::cout << "\nResults: " << g_pass << " passed, " << g_fail << " failed\n";
  return g_fail == 0 ? 0 : 1;
}
