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
#include <secp256k1.h>

#include <cstdio>
#include <string>

#include "Common/StringTools.h"
#include "SwapDaemon/Crypto/LegacySighash.h"
#include "SwapDaemon/Utxo/UtxoHtlc.h"
#include "SwapDaemon/Utxo/UtxoPointLock.h"
#include "SwapDaemon/Utxo/UtxoPointLockSwap.h"

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

static UtxoHtlcChain chainWith(UtxoFeeBump feeBump) {
  UtxoHtlcChain chain{"LTC", SwapPair::LTC, UtxoHtlcEncoding::P2WSH, litecoinNetworkParams(UtxoNetwork::REGTEST)};
  chain.network.feeBump = feeBump;
  return chain;
}

static void testClaimOfferChoice(const Fixture& f) {
  PointLockState state;
  state.terms = f.terms;
  for (const uint64_t fee : claimFeeLadder(1000, 100000)) state.claimOffers.push_back({fee, {}});
  check(claimOfferFor(chainWith(UtxoFeeBump::CHILD_PAYS_FOR_PARENT), state, 2000).fee == 3000,
        "a chain that can raise the fee later claims at the lowest adequate rung");
  check(claimOfferFor(chainWith(UtxoFeeBump::NONE), state, 2000).fee == 27000,
        "a chain with no later fee raise claims at the top rung");
}

// The scriptSig signature of a one-input P2PKH spend, checked against the legacy sighash.
static bool selfSpendSignatureValid(const std::vector<uint8_t>& rawChild, const UtxoOutpoint& input,
                                    const std::vector<uint8_t>& publicKey, uint64_t outputValue) {
  const size_t scriptSigOffset = 4 + 1 + 32 + 4 + 1;
  if (rawChild.size() < scriptSigOffset + 1) return false;
  const size_t signatureSize = rawChild[scriptSigOffset];
  const std::vector<uint8_t> der(rawChild.begin() + scriptSigOffset + 1,
                                 rawChild.begin() + scriptSigOffset + signatureSize);  // without the hashtype
  std::vector<uint8_t> txid;
  Common::fromHex(input.txid, txid);
  std::array<uint8_t, 32> littleEndianTxid{};
  std::copy(txid.rbegin(), txid.rend(), littleEndianTxid.begin());
  const std::vector<uint8_t> script = p2pkhScriptForPubKey(publicKey);
  const std::array<uint8_t, 32> digest = CryptoNote::SwapDaemon::Crypto::LegacySighash().computeForP2sh(
      2, 0, 0xfffffffd, littleEndianTxid, input.vout, script, script, outputValue, 0x01);
  secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_VERIFY);
  secp256k1_ecdsa_signature signature;
  secp256k1_pubkey key;
  const bool valid = rawChild[scriptSigOffset + signatureSize] == 0x01 &&
                     secp256k1_ecdsa_signature_parse_der(ctx, &signature, der.data(), der.size()) == 1 &&
                     secp256k1_ec_pubkey_parse(ctx, &key, publicKey.data(), publicKey.size()) == 1 &&
                     secp256k1_ecdsa_verify(ctx, &signature, digest.data(), &key) == 1;
  secp256k1_context_destroy(ctx);
  return valid;
}

static void testClaimFeeChild(const Fixture& f) {
  const UtxoHtlcChain chain = chainWith(UtxoFeeBump::CHILD_PAYS_FOR_PARENT);
  std::vector<uint8_t> bobKey;
  compressedPublicKey(f.bob.secret, bobKey);
  PointLockSpend claim = f.claim;
  claim.destinationScript = p2pkhScriptForPubKey(bobKey);
  Bip340::Signature someSignature{};
  std::vector<uint8_t> rawClaim;
  pointLockClaimTransaction(f.terms, claim, someSignature, someSignature, rawClaim);
  const std::string claimTxid(64, 'c');
  const SpendingTransactionLookup unspent = [](const UtxoOutpoint&, std::string&, std::vector<uint8_t>&) {
    return false;
  };
  ClaimFeePackage package;
  std::string error;
  check(traceClaimFeePackage(claimTxid, rawClaim, unspent, package, error) && package.children == 0 &&
            package.tip.txid == claimTxid && package.tip.vout == 0 &&
            package.tipValue == claim.lockValue - claim.fee && package.virtualSize == pointLockSpendVirtualSize(true),
        "an unspent claim is a package of one");

  const uint64_t raisedFee = raisedClaimPackageFee(package, claim.lockValue, 0);
  const uint64_t paidRate = claim.fee / package.virtualSize + 1;
  check(raisedFee == paidRate * CLAIM_FEE_STEP * (package.virtualSize + P2PKH_SELF_SPEND_SIZE),
        "a child triples the package fee rate");
  check(raisedClaimPackageFee(package, claim.lockValue, raisedFee + 5) == raisedFee + 5,
        "a higher estimate wins");
  check(raisedClaimPackageFee(package, claim.lockValue, claim.lockValue) == spendFeeCeiling(claim.lockValue),
        "the package never pays above the swap's fee ceiling");

  std::vector<uint8_t> rawChild;
  check(buildClaimFeeChild(chain, package, claim.lockValue, raisedFee, f.bob.secret, bobKey, rawChild).empty(),
        "Bob builds the child");
  uint64_t childValue = 0;
  std::vector<uint8_t> childScript;
  const uint64_t childFee = raisedFee - claim.fee;
  check(spendsOutpoint(rawChild, package.tip) && readTransactionOutput(rawChild, 0, childValue, childScript) &&
            childValue == package.tipValue - childFee && childScript == claim.destinationScript,
        "the child spends the claim output back to Bob's key, paying the package's shortfall");
  check(rawChild.size() <= P2PKH_SELF_SPEND_SIZE && rawChild.size() >= P2PKH_SELF_SPEND_SIZE - 2,
        "the child's size is the one the fee was computed for (" + std::to_string(rawChild.size()) + ")");
  check(selfSpendSignatureValid(rawChild, package.tip, bobKey, childValue), "the child carries Bob's valid signature");

  const std::string childTxid(64, 'd');
  const SpendingTransactionLookup oneChild = [&](const UtxoOutpoint& outpoint, std::string& txid,
                                                 std::vector<uint8_t>& rawTx) {
    if (outpoint.txid != claimTxid) return false;
    txid = childTxid;
    rawTx = rawChild;
    return true;
  };
  ClaimFeePackage extended;
  check(traceClaimFeePackage(claimTxid, rawClaim, oneChild, extended, error) && extended.children == 1 &&
            extended.tip.txid == childTxid && extended.tipValue == childValue &&
            extended.virtualSize == package.virtualSize + rawChild.size(),
        "the walk follows the claim output to the end of the chain");
  check(raisedClaimPackageFee(extended, claim.lockValue, 0) > raisedFee, "each child raises the fee again");

  const SpendingTransactionLookup foreignSpend = [&](const UtxoOutpoint& outpoint, std::string& txid,
                                                     std::vector<uint8_t>& rawTx) {
    if (outpoint.txid != claimTxid) return false;
    txid = childTxid;
    rawTx = rawClaim;  // pays as much as the claim output holds
    return true;
  };
  check(!traceClaimFeePackage(claimTxid, rawClaim, foreignSpend, extended, error),
        "a spend that is not a fee child stops the walk");
  const SpendingTransactionLookup endless = [&](const UtxoOutpoint&, std::string& txid, std::vector<uint8_t>& rawTx) {
    txid = childTxid;
    rawTx = rawChild;
    return true;
  };
  check(!traceClaimFeePackage(claimTxid, rawClaim, endless, extended, error), "a chain without end is refused");

  check(!buildClaimFeeChild(chain, package, claim.lockValue, claim.fee, f.bob.secret, bobKey, rawChild).empty(),
        "no child when the package already pays the target");
  ClaimFeePackage paysElsewhere = package;
  paysElsewhere.tipScript = f.claim.destinationScript;
  check(!buildClaimFeeChild(chain, paysElsewhere, claim.lockValue, raisedFee, f.bob.secret, bobKey, rawChild).empty(),
        "no child when the claim pays an address Bob's key cannot spend");
}

int main() {
  const Fixture fixture;
  testScripts(fixture);
  testSwap(fixture);
  testRefund(fixture);
  testFeeLadder();
  testState(fixture);
  testClaimOfferChoice(fixture);
  testClaimFeeChild(fixture);
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
