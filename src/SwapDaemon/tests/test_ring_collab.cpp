// Copyright (c) 2017-2026 Fuego Developers
//
// Collaborative ring signature unit test (production gate):
// Alice + Bob each hold only a Musig2 share of the escrow key. They run
// SwapTxBuilder ring Round1/Round2 and the resulting ring signature must
// verify under Crypto::check_ring_signature for the aggregate escrow pubkey.

#include <cstring>
#include <iostream>
#include <vector>

#include "SwapDaemon/AdaptorSwap.h"
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

int main() {
  std::cout << "=== Collaborative ring signature tests ===\n\n";
  int pass = 0;
  if (test_collab_ring_verifies()) ++pass;
  if (test_collab_ring_with_decoys()) ++pass;
  std::cout << "\n=== " << pass << "/2 tests passed ===\n";
  return pass == 2 ? 0 : 1;
}
