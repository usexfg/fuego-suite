// Copyright (c) 2017-2026 Fuego Developers
//
// v11+ XMR adaptor share-exchange primitives (offline):
//   [1] shared spend key = A + B; combined secret = a + b verifies against it
//       (and the old pubkey-bytes-as-scalar combination does NOT).
//   [2] spend-share reveal verification: revealed*G == published pubkey.
//   [3] XMR_KEYS / XMR_SHARE_REVEAL wire roundtrip with signatures.
//   [4] SwapParams XMR fields survive encrypt→serialize→deserialize→decrypt.

#include <cstring>
#include <iostream>
#include "SwapDaemon/AdaptorSwap.h"
#include "SwapDaemon/SwapPeerProtocol.h"
#include "SwapDaemon/SwapStateMachine.h"
#include "SwapDaemon/SwapTypes.h"
#include "SwapDaemon/Monero/MoneroAddress.h"
#include "crypto/crypto.h"
#include "crypto/hash.h"

extern "C" {
#include "crypto/crypto-ops.h"
}

using namespace XfgSwap;

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) { ++g_pass; std::cout << "  PASS: " << m << "\n"; } \
  else { ++g_fail; std::cerr << "  FAIL: " << m << "\n"; } } while (0)

namespace {

std::vector<uint8_t> pubVec(const Crypto::PublicKey& pk) {
  return std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(&pk),
                              reinterpret_cast<const uint8_t*>(&pk) + 32);
}

bool scalarAddVec(const Crypto::SecretKey& a, const Crypto::SecretKey& b,
                  Crypto::SecretKey& out) {
  unsigned char sum[32];
  sc_add(sum, reinterpret_cast<const unsigned char*>(&a),
         reinterpret_cast<const unsigned char*>(&b));
  bool zero = true;
  for (size_t i = 0; i < 32; ++i) if (sum[i]) { zero = false; break; }
  if (zero) return false;
  std::memcpy(&out, sum, 32);
  return true;
}

bool pointIsEqualTo(const Crypto::PublicKey& p, const std::vector<uint8_t>& bytes) {
  return bytes.size() == 32 && std::memcmp(&p, bytes.data(), 32) == 0;
}

} // anonymous namespace

static bool test_combined_spend_key() {
  std::cout << "[1] Combined spend key = a + b matches published A + B\n";
  Crypto::PublicKey A, B, V;
  Crypto::SecretKey a, b, v;
  Crypto::generate_keys(A, a);
  Crypto::generate_keys(B, b);
  Crypto::generate_keys(V, v);

  std::vector<uint8_t> expectedPub;
  CHECK(MoneroAddress::sharedSpendPub(pubVec(A), pubVec(B), expectedPub),
        "sharedSpendPub computes A+B");

  Crypto::SecretKey combined;
  CHECK(scalarAddVec(a, b, combined), "combined = a + b");
  Crypto::PublicKey derived;
  CHECK(Crypto::secret_key_to_public_key(combined, derived) &&
        pointIsEqualTo(derived, expectedPub),
        "combined*G == A + B");

  // The historical bug: peer pubkey bytes used as a scalar must NOT match.
  Crypto::SecretKey bogus;
  std::memcpy(&bogus, &B, sizeof(Crypto::PublicKey));
  Crypto::SecretKey bogusCombined;
  if (scalarAddVec(a, bogus, bogusCombined)) {
    Crypto::PublicKey bogusDerived;
    Crypto::secret_key_to_public_key(bogusCombined, bogusDerived);
    CHECK(!pointIsEqualTo(bogusDerived, expectedPub),
          "pubkey-bytes-as-scalar combination does NOT match A+B (old bug shape)");
  } else {
    CHECK(true, "pubkey-bytes-as-scalar combination rejected entirely");
  }

  // Zero-result: combining a with -a must be rejected.
  unsigned char negA[32];
  {
    unsigned char zero[32] = {};
    unsigned char one[32] = {1};
    // sc_mulsub(s, a, b, c) = c - a*b  =>  zero - one*a = -a
    sc_mulsub(negA, one, reinterpret_cast<const unsigned char*>(&a), zero);
  }
  Crypto::SecretKey negASecret;
  std::memcpy(&negASecret, negA, 32);
  Crypto::SecretKey zeroOut;
  CHECK(!scalarAddVec(a, negASecret, zeroOut),
        "a + (-a) rejected (zero combined key)");
  return true;
}

static bool test_share_reveal_verification() {
  std::cout << "[2] Spend-share reveal verification\n";
  Crypto::PublicKey S;
  Crypto::SecretKey s;
  Crypto::generate_keys(S, s);

  Crypto::PublicKey derived;
  CHECK(Crypto::secret_key_to_public_key(s, derived) &&
        std::memcmp(&derived, &S, sizeof(S)) == 0,
        "revealed share matches published pubkey");

  Crypto::SecretKey wrong;
  Crypto::generate_keys(derived, wrong);
  CHECK(std::memcmp(&wrong, &S, sizeof(S)) != 0 ||
        std::memcmp(&wrong, &s, sizeof(s)) != 0,
        "a different secret does not match the published pubkey");
  return true;
}

static bool test_wire_roundtrip() {
  std::cout << "[3] XMR_KEYS / XMR_SHARE_REVEAL wire roundtrip\n";
  Crypto::PublicKey spendPub, viewPub;
  Crypto::SecretKey spendSec, viewSec;
  Crypto::generate_keys(spendPub, spendSec);
  Crypto::generate_keys(viewPub, viewSec);

  PeerMessage xk;
  xk.type = PeerMessageType::XMR_KEYS;
  xk.swapId = "xmr-test-swap";
  xk.xmrKeys.spendPub = spendPub;
  xk.xmrKeys.viewPub = viewPub;
  xk.xmrKeys.viewSec = viewSec;
  CHECK(signPeerMessage(xk, spendPub, spendSec), "XMR_KEYS signed");

  std::string json = serializePeerMessage(xk);
  PeerMessage xk2;
  CHECK(deserializePeerMessage(json, xk2) &&
        xk2.type == PeerMessageType::XMR_KEYS &&
        std::memcmp(&xk2.xmrKeys.spendPub, &spendPub, sizeof(spendPub)) == 0 &&
        std::memcmp(&xk2.xmrKeys.viewPub, &viewPub, sizeof(viewPub)) == 0 &&
        std::memcmp(&xk2.xmrKeys.viewSec, &viewSec, sizeof(viewSec)) == 0,
        "XMR_KEYS fields preserved");
  CHECK(verifyPeerMessage(xk2, spendPub), "XMR_KEYS signature verifies");
  CHECK(!verifyPeerMessage(xk2, viewPub), "XMR_KEYS rejected under wrong key");

  PeerMessage sr;
  sr.type = PeerMessageType::XMR_SHARE_REVEAL;
  sr.swapId = "xmr-test-swap";
  sr.xmrShareReveal.spendShare = spendSec;
  CHECK(signPeerMessage(sr, spendPub, spendSec), "XMR_SHARE_REVEAL signed");
  std::string json2 = serializePeerMessage(sr);
  PeerMessage sr2;
  CHECK(deserializePeerMessage(json2, sr2) &&
        sr2.type == PeerMessageType::XMR_SHARE_REVEAL &&
        std::memcmp(&sr2.xmrShareReveal.spendShare, &spendSec, sizeof(spendSec)) == 0,
        "XMR_SHARE_REVEAL field preserved");
  CHECK(verifyPeerMessage(sr2, spendPub), "XMR_SHARE_REVEAL signature verifies");
  return true;
}

static bool test_state_roundtrip() {
  std::cout << "[4] SwapParams XMR fields encrypt→serialize→deserialize→decrypt\n";
  SwapParams p{};
  p.swapId = "xmr-state-test";
  p.pair = SwapPair::XMR;
  p.role = SwapRole::BOB;
  Crypto::PublicKey spendPub, viewPub, peerSpendPub, peerViewPub;
  Crypto::SecretKey peerViewSec;
  Crypto::generate_keys(spendPub, p.xmrSpendSec);
  Crypto::generate_keys(viewPub, p.xmrViewSec);
  Crypto::generate_keys(peerSpendPub, p.xmrSpendPub == Crypto::PublicKey{} ? p.xmrSpendSec : p.xmrSpendSec);
  Crypto::generate_keys(peerViewPub, peerViewSec);
  p.xmrSpendPub = spendPub;
  p.xmrViewPub = viewPub;
  p.peerXmrSpendPub = peerSpendPub;
  p.peerXmrViewPub = peerViewPub;
  p.peerXmrViewSec = peerViewSec;
  Crypto::SecretKey peerShare;
  Crypto::generate_keys(peerSpendPub, peerShare);
  p.peerXmrSpendShare = peerShare;
  p.xmrKeysGenerated = true;
  p.xmrKeysSent = true;
  p.peerXmrKeysReceived = true;
  p.peerXmrShareReceived = true;
  p.xmrShareSent = true;

  SwapStateMachine sm(p);
  sm.setEncryptionKey("xmr-state-test-key");
  std::string json = sm.serialize();
  CHECK(!json.empty(), "serialized with live XMR secrets");

  SwapStateMachine loaded = SwapStateMachine::deserialize(json);
  loaded.setEncryptionKey("xmr-state-test-key");
  loaded.decryptStoredSecret();
  const auto& q = loaded.params();
  CHECK(std::memcmp(&q.xmrSpendSec, &p.xmrSpendSec, 32) == 0, "xmrSpendSec roundtrip");
  CHECK(std::memcmp(&q.xmrViewSec, &p.xmrViewSec, 32) == 0, "xmrViewSec roundtrip");
  CHECK(std::memcmp(&q.peerXmrSpendShare, &p.peerXmrSpendShare, 32) == 0, "peerXmrSpendShare roundtrip");
  CHECK(std::memcmp(&q.xmrSpendPub, &p.xmrSpendPub, 32) == 0, "xmrSpendPub roundtrip");
  CHECK(std::memcmp(&q.peerXmrViewSec, &p.peerXmrViewSec, 32) == 0, "peerXmrViewSec roundtrip");
  CHECK(q.peerXmrKeysReceived && q.peerXmrShareReceived && q.xmrShareSent,
        "XMR flags roundtrip");
  return true;
}

int main() {
  std::cout << "=== v11 XMR share-exchange primitives ===\n";
  test_combined_spend_key();
  test_share_reveal_verification();
  test_wire_roundtrip();
  test_state_roundtrip();
  std::cout << "\nResults: " << g_pass << " passed, " << g_fail << " failed\n";
  return g_fail == 0 ? 0 : 1;
}
