// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free & open source software distributed in the hope
// it will be useful, but WITHOUT ANY WARRANTY; without even an
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You may redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

#include <openssl/rand.h>

#include <cstdio>
#include <string>

#include "Common/StringTools.h"
#include "SwapDaemon/Utxo/UtxoHtlc.h"
#include "SwapDaemon/Utxo/UtxoPointLock.h"

using namespace XfgSwap;

static int g_checks = 0;
static int g_failures = 0;

static void check(bool condition, const std::string& label) {
  ++g_checks;
  if (!condition) {
    ++g_failures;
    std::printf("FAIL  %s\n", label.c_str());
  }
}

static Bip340::Scalar randomKey() {
  Bip340::Scalar key;
  Bip340::XOnlyKey ignored;
  do RAND_bytes(key.data(), static_cast<int>(key.size()));
  while (!Bip340::xOnlyPublicKey(key, ignored));
  return key;
}

struct Party {
  Bip340::Scalar secret;
  Bip340::XOnlyKey publicKey;
};

static Party newParty() {
  Party party{randomKey(), {}};
  Bip340::xOnlyPublicKey(party.secret, party.publicKey);
  return party;
}

struct Fixture {
  Party alice = newParty();
  Party bob = newParty();
  Bip340::Scalar t = randomKey();
  Bip340::Point T{};
  PointLockTerms terms;
  PointLockSpend claim;
  Fixture() {
    std::vector<uint8_t> tPub;
    compressedPublicKey(t, tPub);
    std::copy(tPub.begin(), tPub.end(), T.begin());
    terms.aliceKey = alice.publicKey;
    terms.bobKey = bob.publicKey;
    terms.timeoutBlock = 850123;
    claim.lock = {std::string(64, 'a'), 1};
    claim.lockValue = 1000000;
    claim.fee = 2000;
    claim.destinationScript = std::vector<uint8_t>(25, 0x76);
  }
};

static void testScripts(const Fixture& f) {
  const std::vector<uint8_t> claimScript = pointLockClaimScript(f.terms);
  check(claimScript.size() == 68 && claimScript[0] == 0x20 && claimScript[33] == 0xad && claimScript[34] == 0x20 &&
            claimScript[67] == 0xac,
        "claim leaf is <A> CHECKSIGVERIFY <B> CHECKSIG");
  const std::vector<uint8_t> refundScript = pointLockRefundScript(f.terms);
  check(Common::toHex(refundScript.data(), 5) == "03cbf80c" + std::string("b1") &&
            refundScript[5] == 0x75 && refundScript[6] == 0x20 && refundScript.back() == 0xac,
        "refund leaf is <850123> CLTV DROP <A> CHECKSIG with a minimal number push");
  std::vector<uint8_t> scriptPubKey;
  check(pointLockScriptPubKey(f.terms, scriptPubKey) && scriptPubKey.size() == 34 && scriptPubKey[0] == 0x51 &&
            scriptPubKey[1] == 0x20,
        "the output is a segwit v1 program");
  const std::string address = pointLockAddress(f.terms, "bcrt");
  check(address.rfind("bcrt1p", 0) == 0 && address.size() == 64, "regtest address is bech32m " + address);
  PointLockTerms later = f.terms;
  later.timeoutBlock += 1;
  std::vector<uint8_t> laterScriptPubKey;
  pointLockScriptPubKey(later, laterScriptPubKey);
  check(laterScriptPubKey != scriptPubKey, "the output commits to the timeout");
  PointLockTerms otherBob = f.terms;
  otherBob.bobKey = newParty().publicKey;
  std::vector<uint8_t> otherScriptPubKey;
  pointLockScriptPubKey(otherBob, otherScriptPubKey);
  check(otherScriptPubKey != scriptPubKey, "the output commits to Bob's key");
}

static void testSwap(const Fixture& f) {
  Bip340::Message sighash;
  check(pointLockClaimSighash(f.terms, f.claim, sighash), "claim sighash");
  Bip340::AdaptorPresig presig;
  check(Bip340::presign(f.alice.secret, sighash, f.T, presig), "Alice pre-signs Bob's claim to T");
  check(Bip340::verifyPresig(f.alice.publicKey, sighash, f.T, presig), "Bob verifies Alice's pre-signature");

  Bip340::Signature aliceSignature, bobSignature;
  check(Bip340::complete(presig, f.t, aliceSignature) && Bip340::verify(f.alice.publicKey, sighash, aliceSignature),
        "Bob completes Alice's signature with t");
  check(Bip340::sign(f.bob.secret, sighash, bobSignature) && Bip340::verify(f.bob.publicKey, sighash, bobSignature),
        "Bob signs the claim");
  std::vector<uint8_t> claimTx;
  check(pointLockClaimTransaction(f.terms, f.claim, bobSignature, aliceSignature, claimTx), "claim transaction");

  Bip340::Signature published;
  Bip340::Scalar recovered;
  check(pointLockClaimSignature(claimTx, f.claim.lock, f.terms, published) && published == aliceSignature &&
            Bip340::extract(presig, published, f.T, recovered) && recovered == f.t,
        "Alice recovers t from the claim witness");
  uint64_t value = 0;
  std::vector<uint8_t> paid;
  check(readTransactionOutput(claimTx, 0, value, paid) && value == f.claim.lockValue - f.claim.fee &&
            paid == f.claim.destinationScript,
        "the claim pays the agreed destination the lock value minus the fee");

  PointLockSpend otherFee = f.claim;
  otherFee.fee += 1;
  Bip340::Message otherSighash;
  pointLockClaimSighash(f.terms, otherFee, otherSighash);
  check(!Bip340::verifyPresig(f.alice.publicKey, otherSighash, f.T, presig),
        "the pre-signature covers the claim fee");
  PointLockSpend otherDestination = f.claim;
  otherDestination.destinationScript[3] ^= 1;
  pointLockClaimSighash(f.terms, otherDestination, otherSighash);
  check(!Bip340::verifyPresig(f.alice.publicKey, otherSighash, f.T, presig),
        "the pre-signature covers Bob's destination");
}

static void testRefund(const Fixture& f) {
  PointLockSpend refund = f.claim;
  Bip340::Message sighash;
  Bip340::Signature aliceSignature;
  std::vector<uint8_t> refundTx;
  check(pointLockRefundSighash(f.terms, refund, sighash) && Bip340::sign(f.alice.secret, sighash, aliceSignature) &&
            pointLockRefundTransaction(f.terms, refund, aliceSignature, refundTx),
        "refund transaction");
  const size_t lockTimeOffset = refundTx.size() - 4;
  const uint32_t lockTime = refundTx[lockTimeOffset] | refundTx[lockTimeOffset + 1] << 8 |
                            refundTx[lockTimeOffset + 2] << 16 | static_cast<uint32_t>(refundTx[lockTimeOffset + 3]) << 24;
  check(lockTime == f.terms.timeoutBlock, "the refund's nLockTime is the timeout");
  Bip340::Signature ignored;
  check(!pointLockClaimSignature(refundTx, f.claim.lock, f.terms, ignored), "a refund is not read as a claim");
  Bip340::Message claimSighash;
  pointLockClaimSighash(f.terms, f.claim, claimSighash);
  check(claimSighash != sighash, "claim and refund sign different messages");
  check(pointLockSpendVirtualSize(true) == 152 && pointLockSpendVirtualSize(false) == 129,
        "spend sizes for fee estimates");
}

static void testFeeLadder() {
  check(claimFeeLadder(1000, 100000) == std::vector<uint64_t>{1000, 3000, 9000, 27000}, "four rungs at x3");
  check(claimFeeLadder(5000, 20000) == std::vector<uint64_t>{5000, 15000, 20000}, "the ceiling caps the ladder");
  check(claimFeeLadder(50000, 20000) == std::vector<uint64_t>{20000}, "an estimate above the ceiling pays the ceiling");
  check(claimFeeLadder(0, 1000).front() == 1, "a zero estimate still pays something");
}

static void testState(const Fixture& f) {
  PointLockState state;
  state.terms = f.terms;
  for (const uint64_t fee : claimFeeLadder(2000, 100000)) {
    PointLockSpend spend = f.claim;
    spend.fee = fee;
    PointLockClaimOffer offer;
    offer.fee = fee;
    Bip340::Message sighash;
    pointLockClaimSighash(f.terms, spend, sighash);
    Bip340::presign(f.alice.secret, sighash, f.T, offer.presig);
    state.claimOffers.push_back(offer);
  }
  const std::string encoded = encodePointLockState(state);
  PointLockState decoded;
  bool same = isPointLockState(encoded) && decodePointLockState(encoded, decoded) &&
              decoded.terms.aliceKey == state.terms.aliceKey && decoded.terms.bobKey == state.terms.bobKey &&
              decoded.terms.timeoutBlock == state.terms.timeoutBlock &&
              decoded.claimOffers.size() == state.claimOffers.size();
  for (size_t i = 0; same && i < state.claimOffers.size(); ++i)
    same = decoded.claimOffers[i].fee == state.claimOffers[i].fee &&
           decoded.claimOffers[i].presig.R0 == state.claimOffers[i].presig.R0 &&
           decoded.claimOffers[i].presig.sPrime == state.claimOffers[i].presig.sPrime;
  check(same, "chain state round-trips with its fee ladder");
  check(encoded.size() < 1024, "the state fits a lock notice (" + std::to_string(encoded.size()) + " bytes)");
  check(!decodePointLockState(encoded + ":1", decoded), "an extra field is refused");
  check(!decodePointLockState(encoded.substr(0, encoded.size() - 1) + "x", decoded), "a non-hex scalar is refused");
  PointLockState descending = state;
  std::swap(descending.claimOffers[0], descending.claimOffers[1]);
  check(!decodePointLockState(encodePointLockState(descending), decoded), "a ladder out of order is refused");
  PointLockState tooMany = state;
  tooMany.claimOffers.push_back(state.claimOffers.back());
  tooMany.claimOffers.back().fee += 1;
  check(!decodePointLockState(encodePointLockState(tooMany), decoded), "more than four rungs are refused");
  PointLockState none = state;
  none.claimOffers.clear();
  check(!decodePointLockState(encodePointLockState(none), decoded), "a lock with no pre-signed claim is refused");
  check(!isPointLockState("p2tr:" + encoded.substr(4)), "other lock states are not point-lock states");
}

int main() {
  const Fixture fixture;
  testScripts(fixture);
  testSwap(fixture);
  testRefund(fixture);
  testFeeLadder();
  testState(fixture);
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
