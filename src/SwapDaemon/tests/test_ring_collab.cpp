// Copyright (c) 2017-2026 Fuego Developers
//
// Collaborative ring signature unit test (production gate):
// Alice + Bob each hold only a Musig2 share of the escrow key. They run
// SwapTxBuilder ring Round1/Round2 and the resulting ring signature must
// verify under Crypto::check_ring_signature for the aggregate escrow pubkey.

#include <algorithm>
#include <cstring>
#include <iterator>
#include <iostream>
#include <vector>

#include "CryptoNoteCore/CryptoNoteTools.h"
#include "CryptoNoteCore/SubaddressTx.h"
#include "SwapDaemon/AdaptorSwap.h"
#include "SwapDaemon/FuegoRpcClient.h"
#include "SwapDaemon/SwapTxBuilder.h"
#include "SwapDaemon/SwapTypes.h"
#include "crypto/crypto.h"
#include "CryptoNote.h"

using namespace XfgSwap;

static bool setupPair(SwapParams& alice, SwapParams& bob) {
  alice = SwapParams{};
  bob   = SwapParams{};
  alice.role = SwapRole::ALICE;
  bob.role   = SwapRole::BOB;
  adaptor_generate_keys(alice);
  adaptor_generate_keys(bob);
  alice.peerSwapPubKey = bob.ourSwapPubKey;
  bob.peerSwapPubKey   = alice.ourSwapPubKey;
  if (!adaptor_key_aggregate(alice)) return false;
  if (!adaptor_key_aggregate(bob)) return false;
  if (std::memcmp(&alice.escrowPubKey, &bob.escrowPubKey, 32) != 0) return false;
  // Shared escrow tx hash seed (for decoy determinism if used)
  std::memset(alice.escrowTxHash.data, 0x42, 32);
  bob.escrowTxHash = alice.escrowTxHash;
  return true;
}

// Build a minimal unsigned tx shell with one KeyInput + empty signature slot.
static void makeTxShell(CryptoNote::Transaction& tx, size_t ringSize,
                        CollaborativeRingState& ring) {
  tx = CryptoNote::Transaction{};
  tx.version = 1;
  tx.unlockTime = 0;
  CryptoNote::KeyInput in;
  in.amount = 1000000;
  in.outputIndexes.resize(ringSize, 0);
  in.keyImage = Crypto::KeyImage{}; // filled later
  tx.inputs.push_back(in);
  CryptoNote::KeyOutput keyOut;
  Crypto::PublicKey dest;
  Crypto::SecretKey destSec;
  Crypto::generate_keys(dest, destSec);
  keyOut.key = dest;
  CryptoNote::TransactionOutput out;
  out.amount = 1000000;
  out.target = keyOut;
  tx.outputs.push_back(out);
  tx.signatures.resize(1);
  tx.signatures[0].resize(ringSize);
}

static bool test_collab_ring_verifies() {
  std::cout << "[1] collaborative ring (size 1) verifies under check_ring_signature\n";

  SwapParams alice, bob;
  if (!setupPair(alice, bob)) {
    std::cout << "    FAIL: setupPair\n";
    return false;
  }

  CollaborativeRingState stA, stB;
  // Ring of 1: only the escrow output (unit test; production uses MIN_RING_SIZE=9)
  stA.ringPubKeys = { alice.escrowPubKey };
  stA.realIndex = 0;
  stB.ringPubKeys = stA.ringPubKeys;
  stB.realIndex = 0;

  CryptoNote::Transaction txA, txB;
  makeTxShell(txA, 1, stA);
  makeTxShell(txB, 1, stB);

  // Round 1 generate on both sides
  SwapTxBuilder::ringRound1Generate(alice, stA);
  SwapTxBuilder::ringRound1Generate(bob, stB);

  // Exchange partial KI + nonces
  stA.peerPartialKeyImage = stB.ourPartialKeyImage;
  stA.peerRingNoncePub    = stB.ourRingNoncePub;
  stA.peerRingNonceHp     = stB.ourRingNonceHp;
  stB.peerPartialKeyImage = stA.ourPartialKeyImage;
  stB.peerRingNoncePub    = stA.ourRingNoncePub;
  stB.peerRingNonceHp     = stA.ourRingNonceHp;

  // Write aggregate KI before prefix hash
  if (!SwapTxBuilder::writeAggregateKeyImageToTx(txA, stA) ||
      !SwapTxBuilder::writeAggregateKeyImageToTx(txB, stB)) {
    std::cout << "    FAIL: writeAggregateKeyImageToTx\n";
    return false;
  }
  if (std::memcmp(&stA.aggregateKeyImage, &stB.aggregateKeyImage, 32) != 0) {
    std::cout << "    FAIL: aggregate key images diverged\n";
    return false;
  }

  // Fixed prefix hash for the unit test (both parties use the same)
  Crypto::Hash prefix{};
  std::memset(prefix.data, 0x7e, 32);

  if (!SwapTxBuilder::ringRound1Finalize(prefix, stA) ||
      !SwapTxBuilder::ringRound1Finalize(prefix, stB)) {
    std::cout << "    FAIL: ringRound1Finalize\n";
    return false;
  }
  if (std::memcmp(&stA.realChallenge, &stB.realChallenge, 32) != 0) {
    std::cout << "    FAIL: real challenges diverged\n";
    return false;
  }

  // Round 2
  SwapTxBuilder::ringRound2Sign(alice, stA);
  SwapTxBuilder::ringRound2Sign(bob, stB);
  stA.peerPartialResponse = stB.ourPartialResponse;
  stB.peerPartialResponse = stA.ourPartialResponse;

  if (!SwapTxBuilder::ringRound2Finalize(stA, txA)) {
    std::cout << "    FAIL: ringRound2Finalize\n";
    return false;
  }

  // Verify with check_ring_signature
  std::vector<const Crypto::PublicKey*> pubs = { &stA.ringPubKeys[0] };
  bool ok = Crypto::check_ring_signature(
      prefix, stA.aggregateKeyImage, pubs, txA.signatures[0].data());
  if (!ok) {
    std::cout << "    FAIL: check_ring_signature rejected collaborative spend\n";
    return false;
  }
  std::cout << "    PASS: ring sig verifies\n";
  return true;
}

static bool test_collab_ring_with_decoys() {
  std::cout << "[2] collaborative ring (size 9, decoys) verifies\n";

  SwapParams alice, bob;
  if (!setupPair(alice, bob)) {
    std::cout << "    FAIL: setupPair\n";
    return false;
  }

  CollaborativeRingState stA, stB;
  stA.ringPubKeys.resize(9);
  stA.realIndex = 3;
  for (size_t i = 0; i < 9; ++i) {
    if (i == stA.realIndex) {
      stA.ringPubKeys[i] = alice.escrowPubKey;
    } else {
      Crypto::SecretKey junk;
      Crypto::generate_keys(stA.ringPubKeys[i], junk);
    }
  }
  stB.ringPubKeys = stA.ringPubKeys;
  stB.realIndex = stA.realIndex;

  CryptoNote::Transaction tx;
  makeTxShell(tx, 9, stA);

  SwapTxBuilder::ringRound1Generate(alice, stA);
  SwapTxBuilder::ringRound1Generate(bob, stB);
  stA.peerPartialKeyImage = stB.ourPartialKeyImage;
  stA.peerRingNoncePub = stB.ourRingNoncePub;
  stA.peerRingNonceHp = stB.ourRingNonceHp;
  stB.peerPartialKeyImage = stA.ourPartialKeyImage;
  stB.peerRingNoncePub = stA.ourRingNoncePub;
  stB.peerRingNonceHp = stA.ourRingNonceHp;

  if (!SwapTxBuilder::writeAggregateKeyImageToTx(tx, stA)) {
    std::cout << "    FAIL: writeAggregateKeyImageToTx\n";
    return false;
  }
  // Bob mirrors KI (same aggregate)
  stB.aggregateKeyImage = stA.aggregateKeyImage;

  Crypto::Hash prefix{};
  std::memset(prefix.data, 0x91, 32);

  if (!SwapTxBuilder::ringRound1Finalize(prefix, stA) ||
      !SwapTxBuilder::ringRound1Finalize(prefix, stB)) {
    std::cout << "    FAIL: ringRound1Finalize\n";
    return false;
  }
  if (std::memcmp(&stA.realChallenge, &stB.realChallenge, 32) != 0) {
    std::cout << "    FAIL: challenges diverged\n";
    return false;
  }

  SwapTxBuilder::ringRound2Sign(alice, stA);
  SwapTxBuilder::ringRound2Sign(bob, stB);
  stA.peerPartialResponse = stB.ourPartialResponse;

  if (!SwapTxBuilder::ringRound2Finalize(stA, tx)) {
    std::cout << "    FAIL: ringRound2Finalize\n";
    return false;
  }

  std::vector<const Crypto::PublicKey*> pubs;
  pubs.reserve(9);
  for (auto& pk : stA.ringPubKeys) pubs.push_back(&pk);

  bool ok = Crypto::check_ring_signature(
      prefix, stA.aggregateKeyImage, pubs, tx.signatures[0].data());
  if (!ok) {
    std::cout << "    FAIL: check_ring_signature rejected size-9 collab ring\n";
    return false;
  }
  std::cout << "    PASS: size-9 ring sig verifies\n";
  return true;
}

// ── Cooperative spend as the daemon runs it: each party rebuilds the tx
// from its own persisted state, never from a shared prefix.

static const uint64_t ESCROW_AMOUNT = 1000000000;
static const uint32_t ESCROW_GLOBAL_INDEX = 500;

static void agreeOnRing(SwapParams& alice, SwapParams& bob) {
  std::vector<uint32_t> indices;
  std::vector<Crypto::PublicKey> keys;
  for (uint32_t i = 0; i < 9; ++i) {
    const uint32_t globalIndex = 100 + i * 100;
    indices.push_back(globalIndex);
    if (globalIndex == ESCROW_GLOBAL_INDEX) {
      keys.push_back(alice.escrowPubKey);
    } else {
      Crypto::PublicKey decoy;
      Crypto::SecretKey unused;
      Crypto::generate_keys(decoy, unused);
      keys.push_back(decoy);
    }
  }
  for (SwapParams* party : {&alice, &bob}) {
    party->xfgAmount = ESCROW_AMOUNT;
    party->escrowOutputIndex = ESCROW_GLOBAL_INDEX;
    party->ringGlobalIndices = indices;
    party->ringPubKeys = keys;
    party->ringRealIndex = 4;
    party->ringDescriptorValid = true;
  }
}

static void storeRoundOne(SwapParams& party, const CollaborativeRingState& ours) {
  party.ringOurPartialKeyImage = ours.ourPartialKeyImage;
  party.ringOurRingNoncePub = ours.ourRingNoncePub;
  party.ringOurRingNonceHp = ours.ourRingNonceHp;
  party.ringOurRingNonceSec = ours.ourRingNonceSec;
  party.ringOurRound1MaterialValid = true;
  party.ringOurRound1Sent = true;
}

static void receiveRoundOne(SwapParams& party, const SwapParams& peer) {
  party.ringPeerPartialKeyImage = peer.ringOurPartialKeyImage;
  party.ringPeerRingNoncePub = peer.ringOurRingNoncePub;
  party.ringPeerRingNonceHp = peer.ringOurRingNonceHp;
  party.ringPeerRound1Received = true;
}

static void exchangeRoundOne(SwapParams& alice, SwapParams& bob) {
  CollaborativeRingState aliceRound, bobRound;
  SwapTxBuilder::ringRound1Generate(alice, aliceRound);
  SwapTxBuilder::ringRound1Generate(bob, bobRound);
  storeRoundOne(alice, aliceRound);
  storeRoundOne(bob, bobRound);
  receiveRoundOne(alice, bob);
  receiveRoundOne(bob, alice);
}

static SwapTxBuilder::CooperativeSpendTerms claimTerms(const SwapParams& alice) {
  SwapTxBuilder::CooperativeSpendTerms terms;
  terms.destinationKey = alice.ourSwapPubKey;
  terms.treasuryFee = ESCROW_AMOUNT / 50;
  Crypto::SecretKey unused;
  Crypto::generate_keys(terms.treasuryKey, unused);
  return terms;
}

static bool signRoundTwo(FuegoRpcClient& rpc, SwapParams& party,
                         const SwapTxBuilder::CooperativeSpendTerms& terms, Crypto::Hash& prefixHash) {
  CryptoNote::Transaction tx;
  CollaborativeRingState ring;
  if (!SwapTxBuilder::prepareCooperativeSpend(rpc, party, terms, tx, prefixHash, ring)) return false;
  SwapTxBuilder::ringRound2Sign(party, ring);
  party.ringOurPartialResponse = ring.ourPartialResponse;
  party.ringOurRound2Sent = true;
  return true;
}

static void receiveRoundTwo(SwapParams& party, const SwapParams& peer) {
  party.ringPeerPartialResponse = peer.ringOurPartialResponse;
  party.ringPeerRound2Received = true;
}

static bool runCooperativeClaim(bool aliceFiller, bool bobFiller, CryptoNote::Transaction& aliceTx,
                                CryptoNote::Transaction& bobTx, bool& prefixesMatch,
                                bool& aliceComplete, bool& bobComplete) {
  SwapParams alice, bob;
  if (!setupPair(alice, bob)) return false;
  agreeOnRing(alice, bob);
  alice.ringJanusFiller = aliceFiller;
  bob.ringJanusFiller = bobFiller;
  exchangeRoundOne(alice, bob);

  FuegoRpcClient unusedRpc("127.0.0.1", 1);
  const auto terms = claimTerms(alice);
  Crypto::Hash alicePrefix, bobPrefix;
  if (!signRoundTwo(unusedRpc, alice, terms, alicePrefix) || !signRoundTwo(unusedRpc, bob, terms, bobPrefix)) {
    return false;
  }
  prefixesMatch = std::memcmp(&alicePrefix, &bobPrefix, sizeof(alicePrefix)) == 0;
  receiveRoundTwo(alice, bob);
  receiveRoundTwo(bob, alice);
  aliceComplete = SwapTxBuilder::completeCooperativeSpend(unusedRpc, alice, terms, aliceTx);
  bobComplete = SwapTxBuilder::completeCooperativeSpend(unusedRpc, bob, terms, bobTx);
  return true;
}

static bool test_independent_builds_verify(bool janusFiller) {
  std::cout << "[3] independent builds by both parties verify (0x09 " << (janusFiller ? "on" : "off") << ")\n";
  CryptoNote::Transaction aliceTx, bobTx;
  bool prefixesMatch = false, aliceComplete = false, bobComplete = false;
  if (!runCooperativeClaim(janusFiller, janusFiller, aliceTx, bobTx, prefixesMatch, aliceComplete, bobComplete)) {
    std::cout << "    FAIL: rounds could not run\n";
    return false;
  }
  if (!prefixesMatch) {
    std::cout << "    FAIL: the parties built different prefixes\n";
    return false;
  }
  if (!aliceComplete || !bobComplete) {
    std::cout << "    FAIL: assembled ring signature rejected (alice " << aliceComplete << ", bob " << bobComplete << ")\n";
    return false;
  }
  if (CryptoNote::toBinaryArray(aliceTx) != CryptoNote::toBinaryArray(bobTx)) {
    std::cout << "    FAIL: the parties assembled different transactions\n";
    return false;
  }
  const size_t janusBases = CryptoNote::readSubaddressTxKeys(aliceTx.extra).janusBases.size();
  if (janusBases != (janusFiller ? 1u : 0u)) {
    std::cout << "    FAIL: 0x09 keys " << janusBases << "\n";
    return false;
  }
  std::cout << "    PASS: identical, verifying transaction from both sides\n";
  return true;
}

static bool test_disagreed_filler_never_verifies() {
  std::cout << "[4] a party with a different 0x09 decision cannot complete the spend\n";
  CryptoNote::Transaction aliceTx, bobTx;
  bool prefixesMatch = true, aliceComplete = true, bobComplete = true;
  if (!runCooperativeClaim(true, false, aliceTx, bobTx, prefixesMatch, aliceComplete, bobComplete)) {
    std::cout << "    FAIL: rounds could not run\n";
    return false;
  }
  if (prefixesMatch || aliceComplete || bobComplete) {
    std::cout << "    FAIL: a mismatched spend was accepted\n";
    return false;
  }
  std::cout << "    PASS: both sides refuse the mismatched signature\n";
  return true;
}

static bool test_tx_key_is_shared_and_private() {
  std::cout << "[5] the spend tx key is the same for both parties and unknown to others\n";
  SwapParams alice, bob;
  if (!setupPair(alice, bob)) return false;
  CryptoNote::KeyPair aliceKey, bobKey, outsiderKey;
  if (!SwapTxBuilder::cooperativeEscrowTxKey(alice, alice.ourSwapPubKey, aliceKey) ||
      !SwapTxBuilder::cooperativeEscrowTxKey(bob, alice.ourSwapPubKey, bobKey)) {
    std::cout << "    FAIL: derivation\n";
    return false;
  }
  SwapParams outsider = alice;
  Crypto::generate_keys(outsider.ourSwapPubKey, outsider.ourSwapSecKey);
  if (!SwapTxBuilder::cooperativeEscrowTxKey(outsider, alice.ourSwapPubKey, outsiderKey)) return false;
  CryptoNote::KeyPair refundKey;
  if (!SwapTxBuilder::cooperativeEscrowTxKey(alice, bob.ourSwapPubKey, refundKey)) return false;
  const bool shared = std::memcmp(&aliceKey.publicKey, &bobKey.publicKey, 32) == 0;
  const bool outsiderDiffers = std::memcmp(&aliceKey.publicKey, &outsiderKey.publicKey, 32) != 0;
  const bool perDestination = std::memcmp(&aliceKey.publicKey, &refundKey.publicKey, 32) != 0;
  if (!shared || !outsiderDiffers || !perDestination) {
    std::cout << "    FAIL: shared " << shared << ", outsider differs " << outsiderDiffers
              << ", claim and refund differ " << perDestination << "\n";
    return false;
  }
  std::cout << "    PASS\n";
  return true;
}

int main() {
  std::cout << "=== Collaborative ring signature tests ===\n\n";
  const bool results[] = {
    test_collab_ring_verifies(),
    test_collab_ring_with_decoys(),
    test_independent_builds_verify(false),
    test_independent_builds_verify(true),
    test_disagreed_filler_never_verifies(),
    test_tx_key_is_shared_and_private(),
  };
  const size_t total = sizeof(results) / sizeof(results[0]);
  const size_t passed = static_cast<size_t>(std::count(std::begin(results), std::end(results), true));
  std::cout << "\n=== " << passed << "/" << total << " tests passed ===\n";
  return passed == total ? 0 : 1;
}
