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
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

#include "SwapDaemon/SwapPeerProtocol.h"
#include "Common/JsonValue.h"
#include "crypto/crypto.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace XfgSwap;

static int g_failures = 0;
static int g_checks = 0;

static void check(bool condition, const char* label) {
  ++g_checks;
  if (!condition) {
    ++g_failures;
    std::printf("FAIL  %s\n", label);
  }
}

static const char* const CLAIM_KEY = "02466d7fcae563e5cb09a0d1870bb580344804617879a14949cf22285f1bae3f27";
static const char* const LOCK_TXID = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b";

static PeerMessage adaptorExchange() {
  PeerMessage msg;
  msg.type = PeerMessageType::ADAPTOR_EXCHANGE;
  msg.swapId = "swap-ctr-lock";
  std::memset(&msg.adaptorExchange.adaptorPoint, 0x11, sizeof(msg.adaptorExchange.adaptorPoint));
  std::memset(&msg.adaptorExchange.adaptorDleqQ, 0x22, sizeof(msg.adaptorExchange.adaptorDleqQ));
  std::memset(&msg.adaptorExchange.htlcHashLock, 0x33, sizeof(msg.adaptorExchange.htlcHashLock));
  return msg;
}

static PeerMessage lockNotice() {
  PeerMessage msg;
  msg.type = PeerMessageType::CTR_LOCKED;
  msg.swapId = "swap-ctr-lock";
  msg.ctrLocked.lockRef = LOCK_TXID;
  msg.ctrLocked.lockOutputIndex = 1;
  msg.ctrLocked.lockState = "63a820deb0e3";
  return msg;
}

static bool sameDigest(const PeerMessage& a, const PeerMessage& b) {
  const Crypto::Hash da = peerMessageDigest(a);
  const Crypto::Hash db = peerMessageDigest(b);
  return std::memcmp(&da, &db, sizeof(da)) == 0;
}

static void testClaimKeyIsSigned() {
  Crypto::PublicKey pub;
  Crypto::SecretKey sec;
  Crypto::generate_keys(pub, sec);

  PeerMessage legacy = adaptorExchange();
  PeerMessage withKey = adaptorExchange();
  withKey.adaptorExchange.ctrClaimPubKeyHex = CLAIM_KEY;
  check(!sameDigest(legacy, withKey), "claim key is covered by the digest");

  signPeerMessage(withKey, pub, sec);
  PeerMessage received;
  check(deserializePeerMessage(serializePeerMessage(withKey), received) &&
            received.adaptorExchange.ctrClaimPubKeyHex == CLAIM_KEY &&
            verifyPeerMessage(received, pub),
        "claim key survives the wire and verifies");

  received.adaptorExchange.ctrClaimPubKeyHex[65] = '8';
  check(!verifyPeerMessage(received, pub), "substituted claim key breaks the signature");

  PeerMessage legacyReceived;
  signPeerMessage(legacy, pub, sec);
  check(deserializePeerMessage(serializePeerMessage(legacy), legacyReceived) &&
            legacyReceived.adaptorExchange.ctrClaimPubKeyHex.empty() &&
            verifyPeerMessage(legacyReceived, pub),
        "legacy ADAPTOR_EXCHANGE without a claim key still verifies");

  std::string truncated = serializePeerMessage(withKey);
  truncated.replace(truncated.find(CLAIM_KEY), 66, std::string(CLAIM_KEY).substr(0, 64));
  check(!deserializePeerMessage(truncated, received), "claim key of the wrong length rejected");
}

static void testLockNotice() {
  Crypto::PublicKey pub;
  Crypto::SecretKey sec;
  Crypto::generate_keys(pub, sec);

  PeerMessage notice = lockNotice();
  signPeerMessage(notice, pub, sec);
  PeerMessage received;
  check(deserializePeerMessage(serializePeerMessage(notice), received) &&
            received.type == PeerMessageType::CTR_LOCKED &&
            received.ctrLocked.lockRef == LOCK_TXID && received.ctrLocked.lockOutputIndex == 1 &&
            received.ctrLocked.lockState == "63a820deb0e3" && verifyPeerMessage(received, pub),
        "CTR_LOCKED round-trips and verifies");

  PeerMessage otherIndex = lockNotice();
  otherIndex.ctrLocked.lockOutputIndex = 0;
  check(!sameDigest(lockNotice(), otherIndex), "output index is covered by the digest");
  PeerMessage otherState = lockNotice();
  otherState.ctrLocked.lockState = "63a820deb0e4";
  check(!sameDigest(lockNotice(), otherState), "lock script is covered by the digest");
  PeerMessage shifted = lockNotice();
  shifted.ctrLocked.lockRef = std::string(LOCK_TXID) + "6";
  shifted.ctrLocked.lockState = "3a820deb0e3";
  check(!sameDigest(lockNotice(), shifted), "field boundaries are length-prefixed");

  const std::string wire = serializePeerMessage(notice);
  std::string negative = wire;
  negative.replace(negative.find("\"lockOutputIndex\":1"), 19, "\"lockOutputIndex\":-1");
  check(!deserializePeerMessage(negative, received), "negative output index rejected");
  std::string oversized = wire;
  oversized.replace(oversized.find(LOCK_TXID), 64, std::string(200, 'a'));
  check(!deserializePeerMessage(oversized, received), "oversized lock reference rejected");
  std::string empty = wire;
  empty.replace(empty.find(LOCK_TXID), 64, "");
  check(!deserializePeerMessage(empty, received), "empty lock reference rejected");
}

int main() {
  testClaimKeyIsSigned();
  testLockNotice();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
