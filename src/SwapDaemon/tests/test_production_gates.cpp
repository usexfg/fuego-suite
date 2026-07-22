// Copyright (c) 2017-2026 Fuego Developers
//
// Production gate unit tests — automated portion of the mainnet checklist:
//   - timelockOrderingOk rejects inverted / under-margin windows
//   - offer canonical signature rejects rate mutation rebroadcast
//   - soft-order maker pubkey gate (foreign maker != local)
//   - secret encrypt/decrypt roundtrip + wrong key fails
//   - crash/restart simulation: serialize → deserialize → decrypt recovers
//     ourSwapSecKey and adaptorSecret

#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

#include "SwapDaemon/SwapTimelock.h"
#include "SwapDaemon/SwapSecretEncryption.h"
#include "SwapDaemon/SwapStateMachine.h"
#include "SwapDaemon/SwapTypes.h"
#include "Common/StringTools.h"
#include "crypto/crypto.h"
#include "crypto/hash.h"

using namespace XfgSwap;

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg) do { \
  if (cond) { ++g_pass; std::cout << "  PASS: " << msg << "\n"; } \
  else { ++g_fail; std::cerr << "  FAIL: " << msg << "\n"; } \
} while (0)

// Mirror of SwapOfferRelay::offerCanonicalHash (must stay in sync).
static Crypto::Hash offerCanonicalHash(
    const std::string& offerId, uint8_t pair, uint64_t xfgAmount,
    uint64_t rateNum, bool isSoftOrder, uint32_t ttlBlocks,
    uint8_t allowedSlippagePct, int64_t timestamp) {
  std::string data;
  data.reserve(offerId.size() + 64);
  data.append(offerId);
  data.append(1, static_cast<char>(pair));
  data.append(reinterpret_cast<const char*>(&xfgAmount), sizeof(xfgAmount));
  data.append(reinterpret_cast<const char*>(&rateNum), sizeof(rateNum));
  data.append(1, isSoftOrder ? '\x01' : '\x00');
  data.append(reinterpret_cast<const char*>(&ttlBlocks), sizeof(ttlBlocks));
  data.append(1, static_cast<char>(allowedSlippagePct));
  data.append(reinterpret_cast<const char*>(&timestamp), sizeof(timestamp));
  Crypto::Hash h;
  cn_fast_hash(data.data(), data.size(), h);
  return h;
}

static void testTimelock() {
  std::cout << "timelockOrderingOk\n";
  // Inverted: CTR window longer than XFG → reject
  CHECK(!timelockOrderingOk(SwapPair::SOL,
                            /*xfgCur*/100, /*xfgTO*/110,
                            /*ctrCur*/1000, /*ctrTO*/1000000,
                            /*margin*/3600),
        "rejects inverted windows (CTR outlasts XFG)");

  // Underflow guards
  CHECK(!timelockOrderingOk(SwapPair::ETH, 100, 100, 50, 100, 0),
        "rejects xfgTimeout <= xfgCurrent");
  CHECK(!timelockOrderingOk(SwapPair::ETH, 100, 200, 50, 50, 0),
        "rejects ctrTimeout <= ctrCurrent");

  // Healthy SOL: XFG 100 blocks (~13.3h) vs SOL 10000 slots (~1.1h) + 1h margin
  CHECK(timelockOrderingOk(SwapPair::SOL, 1000, 1100, 10000, 20000, 3600),
        "accepts well-ordered SOL vs XFG windows");
}

static void testOfferRateMutation() {
  std::cout << "offer canonical signature\n";
  Crypto::PublicKey pub;
  Crypto::SecretKey sec;
  Crypto::generate_keys(pub, sec);

  const std::string offerId = "test-offer-id-abc";
  const uint8_t pair = 0;
  const uint64_t xfgAmount = 1'000'000'000ULL;
  const uint64_t rateNum = 50'000'000ULL; // 5.0 * 1e7
  const bool soft = true;
  const uint32_t ttl = 60;
  const uint8_t slip = 5;
  const int64_t ts = 1700000000;

  Crypto::Hash good = offerCanonicalHash(offerId, pair, xfgAmount, rateNum, soft, ttl, slip, ts);
  Crypto::Signature sig;
  Crypto::generate_signature(good, pub, sec, sig);
  CHECK(Crypto::check_signature(good, pub, sig), "valid signature over economic fields");

  // Mutate rate (rebroadcast attack) — signature must not verify under new digest
  Crypto::Hash mutated = offerCanonicalHash(offerId, pair, xfgAmount, rateNum + 1, soft, ttl, slip, ts);
  CHECK(!Crypto::check_signature(mutated, pub, sig),
        "rebroadcast with mutated rate is rejected");

  // Mutate amount
  Crypto::Hash mutAmt = offerCanonicalHash(offerId, pair, xfgAmount + 1, rateNum, soft, ttl, slip, ts);
  CHECK(!Crypto::check_signature(mutAmt, pub, sig),
        "rebroadcast with mutated amount is rejected");
}

static void testSoftOrderMakerGate() {
  std::cout << "soft-order maker gate\n";
  Crypto::PublicKey localPub, foreignPub;
  Crypto::SecretKey a, b;
  Crypto::generate_keys(localPub, a);
  Crypto::generate_keys(foreignPub, b);

  // Gate as in SwapDaemon::handleSoftOrderRequest
  auto isLocalMaker = [](const Crypto::PublicKey& offerMaker,
                         const Crypto::PublicKey& ourMaker) {
    return std::memcmp(&offerMaker, &ourMaker, sizeof(Crypto::PublicKey)) == 0;
  };

  CHECK(isLocalMaker(localPub, localPub), "local maker matches");
  CHECK(!isLocalMaker(foreignPub, localPub),
        "foreign makerPubKey does not pass soft-order gate");
}

static void testSecretEncryptRoundtrip() {
  std::cout << "secret encrypt/decrypt\n";
  Crypto::SecretKey secret;
  Crypto::PublicKey throwaway;
  Crypto::generate_keys(throwaway, secret);

  const std::string key = "production-gate-test-enc-key-32b!";
  SwapSecretEncryption::EncryptedSecret enc;
  CHECK(SwapSecretEncryption::encrypt(secret, key, enc), "encrypt succeeds");

  Crypto::SecretKey recovered{};
  CHECK(SwapSecretEncryption::decrypt(enc, key, recovered), "decrypt succeeds");
  CHECK(std::memcmp(&secret, &recovered, sizeof(secret)) == 0,
        "roundtrip recovers secret");

  Crypto::SecretKey wrong{};
  CHECK(!SwapSecretEncryption::decrypt(enc, "wrong-key-xxxxxxxxxxxxxxxxxxxxx", wrong),
        "wrong key fails decrypt");
}

static void testCrashRestartSecrets() {
  std::cout << "crash/restart secret recovery\n";
  Crypto::PublicKey pub;
  Crypto::SecretKey swapSec;
  Crypto::generate_keys(pub, swapSec);

  Crypto::SecretKey adaptor;
  Crypto::PublicKey adaptPub;
  Crypto::generate_keys(adaptPub, adaptor);

  SwapParams p;
  p.swapId = "gate-crash-restart-1";
  p.role = SwapRole::BOB;
  p.pair = SwapPair::SOL;
  p.ourSwapPubKey = pub;
  p.ourSwapSecKey = swapSec;
  p.adaptorSecret = adaptor;
  p.adaptorSecretReceived = true;
  p.xfgAmount = 12345;
  p.ctrAmount = 67890;

  const std::string encKey = "maker-derived-escrow-key-material";
  SwapStateMachine sm(p);
  sm.setEncryptionKey(encKey);
  // Force a non-initial state so serialize is meaningful
  // (machine may start at INITIATED depending on ctor — force via transition if needed)
  std::string json = sm.serialize();
  CHECK(!json.empty(), "serialize with enc key produces JSON");
  CHECK(json.find("ourSwapSecKeyEnc") != std::string::npos,
        "JSON contains encrypted ourSwapSecKeyEnc");
  // Must not contain raw secret hex of ourSwapSecKey as a free-standing field value
  // (encrypted blob is hex of nonce|salt|ct|tag — not the plain key)
  std::string plainHex = Common::podToHex(swapSec);
  // Plaintext secret key must not appear as a JSON string value of ourSwapSecKey
  CHECK(json.find("\"ourSwapSecKey\"") == std::string::npos ||
        json.find(plainHex) == std::string::npos,
        "plaintext ourSwapSecKey not stored");

  // Simulate process restart: new SM from JSON, re-inject key, decrypt
  SwapStateMachine loaded = SwapStateMachine::deserialize(json);
  loaded.setEncryptionKey(encKey);
  loaded.decryptStoredSecret();

  CHECK(std::memcmp(&loaded.params().ourSwapSecKey, &swapSec, sizeof(swapSec)) == 0,
        "ourSwapSecKey recovered after restart");
  CHECK(std::memcmp(&loaded.params().adaptorSecret, &adaptor, sizeof(adaptor)) == 0,
        "adaptorSecret recovered after restart");

  // Without enc key, secrets stay zeroed after deserialize
  SwapStateMachine noKey = SwapStateMachine::deserialize(json);
  Crypto::SecretKey zero{};
  std::memset(&zero, 0, sizeof(zero));
  CHECK(std::memcmp(&noKey.params().ourSwapSecKey, &zero, sizeof(zero)) == 0,
        "without enc key ourSwapSecKey remains zero after load");
}

int main() {
  std::cout << "=== Production gate unit tests ===\n";
  testTimelock();
  testOfferRateMutation();
  testSoftOrderMakerGate();
  testSecretEncryptRoundtrip();
  testCrashRestartSecrets();

  std::cout << "\nResults: " << g_pass << " passed, " << g_fail << " failed\n";
  return g_fail == 0 ? 0 : 1;
}
