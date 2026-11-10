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
#include "SwapDaemon/Crypto/Secp256k1Ops.h"
#include "SwapDaemon/Utxo/BchPointLock.h"
#include "SwapDaemon/Utxo/UtxoHtlc.h"

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

static BchSchnorr::Scalar randomKey() {
  BchSchnorr::Scalar key;
  do RAND_bytes(key.data(), static_cast<int>(key.size()));
  while (!Secp256k1Ops::validScalar(key));
  return key;
}

struct Party {
  BchSchnorr::Scalar secret;
  BchSchnorr::Point publicKey;
};

static Party newParty() {
  Party party{randomKey(), {}};
  BchSchnorr::publicKey(party.secret, party.publicKey);
  return party;
}

struct Fixture {
  Party alice = newParty();
  Party bob = newParty();
  BchSchnorr::Scalar t = randomKey();
  BchSchnorr::Point T{};
  BchPointLockTerms terms;
  PointLockSpend claim;
  Fixture() {
    BchSchnorr::publicKey(t, T);
    terms.aliceKey = alice.publicKey;
    terms.bobKey = bob.publicKey;
    terms.timeoutBlock = 850123;
    claim.lock = {std::string(64, 'a'), 1};
    claim.lockValue = 1000000;
    claim.fee = 2000;
    claim.destinationScript = p2pkhScriptForPubKey(std::vector<uint8_t>(bob.publicKey.begin(), bob.publicKey.end()));
  }
};

static void testScript(const Fixture& f) {
  const std::vector<uint8_t> script = bchPointLockScript(f.terms);
  check(script.size() == 114 && script[0] == 0x63 && script[1] == 0x21 && script[35] == 0xad && script[36] == 0x21 &&
            script[70] == 0xac && script[71] == 0x67,
        "the claim branch is OP_IF <A> OP_CHECKSIGVERIFY <B> OP_CHECKSIG");
  check(Common::toHex(script.data() + 72, 4) == "03cbf80c" && script[76] == 0xb1 && script[77] == 0x75 &&
            script[78] == 0x21 && script[112] == 0xac && script[113] == 0x68,
        "the refund branch is OP_ELSE <850123> CLTV DROP <A> OP_CHECKSIG OP_ENDIF");
  const std::vector<uint8_t> scriptPubKey = bchPointLockScriptPubKey(f.terms);
  check(scriptPubKey.size() == 23 && scriptPubKey[0] == 0xa9 && scriptPubKey[1] == 0x14 && scriptPubKey[22] == 0x87,
        "the output is P2SH");
  check(bchPointLockAddress(bitcoinCashNetworkParams(UtxoNetwork::REGTEST), f.terms).rfind('2', 0) == 0,
        "regtest address is a base58 P2SH address");
  BchPointLockTerms later = f.terms;
  later.timeoutBlock += 1;
  check(bchPointLockScriptPubKey(later) != scriptPubKey, "the output commits to the timeout");
  BchPointLockTerms otherBob = f.terms;
  otherBob.bobKey = newParty().publicKey;
  check(bchPointLockScriptPubKey(otherBob) != scriptPubKey, "the output commits to Bob's key");
}

static void testSwap(const Fixture& f) {
  BchSchnorr::Message sighash;
  check(bchPointLockClaimSighash(f.terms, f.claim, sighash), "claim sighash");
  BchSchnorr::AdaptorPresig presig;
  check(BchSchnorr::presign(f.alice.secret, sighash, f.T, presig), "Alice pre-signs Bob's claim to T");
  check(BchSchnorr::verifyPresig(f.alice.publicKey, sighash, f.T, presig), "Bob verifies Alice's pre-signature");

  BchSchnorr::Signature aliceSignature, bobSignature;
  check(BchSchnorr::complete(presig, f.t, aliceSignature) &&
            BchSchnorr::verify(f.alice.publicKey, sighash, aliceSignature),
        "Bob completes Alice's signature with t");
  check(BchSchnorr::sign(f.bob.secret, sighash, bobSignature) &&
            BchSchnorr::verify(f.bob.publicKey, sighash, bobSignature),
        "Bob signs the claim");
  std::vector<uint8_t> claimTx;
  check(bchPointLockClaimTransaction(f.terms, f.claim, bobSignature, aliceSignature, claimTx), "claim transaction");

  std::vector<std::vector<uint8_t>> items;
  check(spendScriptSig(claimTx, f.claim.lock, items) && items.size() == 4 && items[0].size() == 65 &&
            items[0].back() == 0x41 && items[1].size() == 65 && items[2] == std::vector<uint8_t>{0x01} &&
            items[3] == bchPointLockScript(f.terms),
        "the claim scriptSig is sigB, sigA (each with the FORKID type), OP_1, the redeem script");

  BchSchnorr::Signature published;
  BchSchnorr::Scalar recovered;
  check(bchPointLockClaimSignature(claimTx, f.claim.lock, f.terms, published) && published == aliceSignature &&
            BchSchnorr::extract(presig, published, f.T, recovered) && recovered == f.t,
        "Alice recovers t from the claim scriptSig");
  uint64_t value = 0;
  std::vector<uint8_t> paid;
  check(readTransactionOutput(claimTx, 0, value, paid) && value == f.claim.lockValue - f.claim.fee &&
            paid == f.claim.destinationScript,
        "the claim pays the agreed destination the lock value minus the fee");

  PointLockSpend otherFee = f.claim;
  otherFee.fee += 1;
  BchSchnorr::Message otherSighash;
  bchPointLockClaimSighash(f.terms, otherFee, otherSighash);
  check(!BchSchnorr::verifyPresig(f.alice.publicKey, otherSighash, f.T, presig), "the pre-signature covers the fee");
  PointLockSpend otherValue = f.claim;
  otherValue.lockValue += 1;
  bchPointLockClaimSighash(f.terms, otherValue, otherSighash);
  check(!BchSchnorr::verifyPresig(f.alice.publicKey, otherSighash, f.T, presig),
        "the FORKID sighash commits to the lock value");
}

static uint32_t lockTimeOf(const std::vector<uint8_t>& rawTx) {
  const size_t offset = rawTx.size() - 4;
  return rawTx[offset] | rawTx[offset + 1] << 8 | rawTx[offset + 2] << 16 |
         static_cast<uint32_t>(rawTx[offset + 3]) << 24;
}

static void testRefund(const Fixture& f) {
  BchSchnorr::Message sighash;
  BchSchnorr::Signature aliceSignature;
  std::vector<uint8_t> refundTx;
  check(bchPointLockRefundSighash(f.terms, f.claim, sighash) && BchSchnorr::sign(f.alice.secret, sighash, aliceSignature) &&
            bchPointLockRefundTransaction(f.terms, f.claim, aliceSignature, refundTx),
        "refund transaction");
  check(lockTimeOf(refundTx) == f.terms.timeoutBlock, "the refund's nLockTime is the timeout");
  BchSchnorr::Signature ignored;
  check(!bchPointLockClaimSignature(refundTx, f.claim.lock, f.terms, ignored), "a refund is not read as a claim");
  BchSchnorr::Message claimSighash;
  bchPointLockClaimSighash(f.terms, f.claim, claimSighash);
  check(claimSighash != sighash, "claim and refund sign different messages");
}

static void testSpendSizes(Fixture f) {
  f.terms.timeoutBlock = 0xffffffff;
  BchSchnorr::Signature signature{};
  std::vector<uint8_t> claimTx, refundTx;
  bchPointLockClaimTransaction(f.terms, f.claim, signature, signature, claimTx);
  bchPointLockRefundTransaction(f.terms, f.claim, signature, refundTx);
  check(bchPointLockScript(f.terms).size() == 116, "the largest timeout gives a 116-byte redeem script");
  check(claimTx.size() == bchPointLockSpendVirtualSize(true) && refundTx.size() == bchPointLockSpendVirtualSize(false),
        "fee sizes equal the largest spends (" + std::to_string(claimTx.size()) + ", " +
            std::to_string(refundTx.size()) + ")");
}

static void testState(const Fixture& f) {
  BchPointLockState state;
  state.terms = f.terms;
  for (const uint64_t fee : claimFeeLadder(2000, 100000)) {
    PointLockSpend spend = f.claim;
    spend.fee = fee;
    BchPointLockClaimOffer offer;
    offer.fee = fee;
    BchSchnorr::Message sighash;
    bchPointLockClaimSighash(f.terms, spend, sighash);
    BchSchnorr::presign(f.alice.secret, sighash, f.T, offer.presig);
    state.claimOffers.push_back(offer);
  }
  const std::string encoded = encodeBchPointLockState(state);
  BchPointLockState decoded;
  bool same = isBchPointLockState(encoded) && decodeBchPointLockState(encoded, decoded) &&
              decoded.terms.aliceKey == state.terms.aliceKey && decoded.terms.bobKey == state.terms.bobKey &&
              decoded.terms.timeoutBlock == state.terms.timeoutBlock &&
              decoded.claimOffers.size() == state.claimOffers.size();
  for (size_t i = 0; same && i < state.claimOffers.size(); ++i)
    same = decoded.claimOffers[i].fee == state.claimOffers[i].fee &&
           decoded.claimOffers[i].presig.R0 == state.claimOffers[i].presig.R0 &&
           decoded.claimOffers[i].presig.sPrime == state.claimOffers[i].presig.sPrime;
  check(same, "chain state round-trips with its fee ladder");
  check(encoded.size() < 1024, "the state fits a lock notice (" + std::to_string(encoded.size()) + " bytes)");
  check(!decodeBchPointLockState(encoded + ":1", decoded), "an extra field is refused");
  BchPointLockState descending = state;
  std::swap(descending.claimOffers[0], descending.claimOffers[1]);
  check(!decodeBchPointLockState(encodeBchPointLockState(descending), decoded), "a ladder out of order is refused");
  BchPointLockState none = state;
  none.claimOffers.clear();
  check(!decodeBchPointLockState(encodeBchPointLockState(none), decoded), "a lock with no pre-signed claim is refused");
  check(!isBchPointLockState("tr1:" + encoded.substr(4)) && !isPointLockState(encoded),
        "BCH and Taproot point-lock states are told apart");
}

int main() {
  const Fixture fixture;
  testScript(fixture);
  testSwap(fixture);
  testRefund(fixture);
  testSpendSizes(fixture);
  testState(fixture);
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
