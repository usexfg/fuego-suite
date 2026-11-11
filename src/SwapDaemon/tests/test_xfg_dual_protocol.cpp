// Copyright (c) 2017-2026 Fuego Developers
//
// Dual-party XFG protocol drive (offline, no wallet/node):
// Alice + Bob exchange keys, run adaptor pre-sig path, then collaborative
// ring spend. Proves the fund-safety critical path that on-chain e2e would
// hit after escrow is funded.
//
// Stages:
//   1. keygen + key aggregate (shared escrow pubkey)
//   2. Bob adaptor secret t + Alice DLEQ verify
//   3. Musig2 nonces + partials + adapted aggregate + extract t
//   4. Collaborative ring size-9 spend verifies under check_ring_signature

#include <cstring>
#include <iostream>
#include <vector>

#include "SwapDaemon/AdaptorSwap.h"
#include "SwapDaemon/SwapTxBuilder.h"
#include "SwapDaemon/SwapTypes.h"
#include "SwapDaemon/SwapStateMachine.h"
#include "crypto/crypto.h"
#include "CryptoNote.h"

using namespace XfgSwap;

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) { ++g_pass; std::cout << "  PASS: " << m << "\n"; } \
  else { ++g_fail; std::cerr << "  FAIL: " << m << "\n"; } } while (0)

static bool setupRoles(SwapParams& alice, SwapParams& bob) {
  alice = SwapParams{};
  bob = SwapParams{};
  alice.role = SwapRole::ALICE;
  bob.role = SwapRole::BOB;
  alice.pair = bob.pair = SwapPair::ETH;
  alice.xfgAmount = bob.xfgAmount = 10'000'000; // 1 XFG
  alice.ctrAmount = bob.ctrAmount = 1'000'000'000'000'000ULL;
  alice.swapId = bob.swapId = "dual-proto-e2e-1";

  adaptor_generate_keys(alice);
  adaptor_generate_keys(bob);
  alice.peerSwapPubKey = bob.ourSwapPubKey;
  bob.peerSwapPubKey = alice.ourSwapPubKey;
  if (!adaptor_key_aggregate(alice) || !adaptor_key_aggregate(bob)) return false;
  if (std::memcmp(&alice.escrowPubKey, &bob.escrowPubKey, 32) != 0) return false;

  // Simulate Bob funded escrow (on-chain tx hash known to both)
  std::memset(bob.escrowTxHash.data, 0xab, 32);
  alice.escrowTxHash = bob.escrowTxHash;
  bob.escrowOutputIndex = alice.escrowOutputIndex = 42;
  return true;
}

static bool stage_adaptor_and_extract(SwapParams& alice, SwapParams& bob) {
  if (!adaptor_generate_adaptor(bob, bob.escrowPubKey)) return false;
  alice.adaptorPoint = bob.adaptorPoint;
  alice.adaptorDleqQ = bob.adaptorDleqQ;
  alice.adaptorDleqProof = bob.adaptorDleqProof;
  if (!adaptor_verify_adaptor(alice, alice.escrowPubKey, alice.adaptorDleqQ)) return false;

  adaptor_nonce_generate(alice);
  adaptor_nonce_generate(bob);
  alice.musig2.peerPubNonce = bob.musig2.ourPubNonce;
  bob.musig2.peerPubNonce = alice.musig2.ourPubNonce;

  Crypto::Hash msg{};
  std::memset(msg.data, 0x5A, 32);
  if (!adaptor_session_init(alice, msg, true)) return false;
  if (!adaptor_session_init(bob, msg, true)) return false;
  adaptor_partial_sign(alice);
  adaptor_partial_sign(bob);
  alice.musig2.peerPartialSig = bob.musig2.ourPartialSig;
  bob.musig2.peerPartialSig = alice.musig2.ourPartialSig;
  if (!adaptor_partial_verify(alice) || !adaptor_partial_verify(bob)) return false;

  Crypto::SecretKey t_orig = bob.adaptorSecret;
  Crypto::Signature onChain = adaptor_aggregate(bob, true);
  std::memset(&alice.adaptorSecret, 0, 32);
  if (!adaptor_extract_secret(alice, onChain)) return false;
  return std::memcmp(&alice.adaptorSecret, &t_orig, 32) == 0;
}

static bool stage_collab_ring_spend(SwapParams& alice, SwapParams& bob) {
  CollaborativeRingState stA, stB;
  stA.ringPubKeys.resize(9);
  stA.realIndex = 4;
  for (size_t i = 0; i < 9; ++i) {
    if (i == stA.realIndex) stA.ringPubKeys[i] = alice.escrowPubKey;
    else {
      Crypto::SecretKey junk;
      Crypto::generate_keys(stA.ringPubKeys[i], junk);
    }
  }
  stB.ringPubKeys = stA.ringPubKeys;
  stB.realIndex = stA.realIndex;

  CryptoNote::Transaction tx{};
  tx.version = 1;
  CryptoNote::KeyInput in;
  in.amount = alice.xfgAmount;
  in.outputIndexes.assign(9, 0);
  tx.inputs.push_back(in);
  CryptoNote::KeyOutput ko;
  Crypto::PublicKey dest; Crypto::SecretKey ds;
  Crypto::generate_keys(dest, ds);
  ko.key = dest;
  CryptoNote::TransactionOutput out;
  out.amount = alice.xfgAmount - 10000;
  out.target = ko;
  tx.outputs.push_back(out);
  tx.signatures.resize(1);
  tx.signatures[0].resize(9);

  SwapTxBuilder::ringRound1Generate(alice, stA);
  SwapTxBuilder::ringRound1Generate(bob, stB);
  stA.peerPartialKeyImage = stB.ourPartialKeyImage;
  stA.peerRingNoncePub = stB.ourRingNoncePub;
  stA.peerRingNonceHp = stB.ourRingNonceHp;
  stB.peerPartialKeyImage = stA.ourPartialKeyImage;
  stB.peerRingNoncePub = stA.ourRingNoncePub;
  stB.peerRingNonceHp = stA.ourRingNonceHp;

  if (!SwapTxBuilder::writeAggregateKeyImageToTx(tx, stA)) return false;
  stB.aggregateKeyImage = stA.aggregateKeyImage;

  Crypto::Hash prefix{};
  std::memset(prefix.data, 0xcd, 32);
  if (!SwapTxBuilder::ringRound1Finalize(prefix, stA) ||
      !SwapTxBuilder::ringRound1Finalize(prefix, stB)) return false;
  if (std::memcmp(&stA.realChallenge, &stB.realChallenge, 32) != 0) return false;

  SwapTxBuilder::ringRound2Sign(alice, stA);
  SwapTxBuilder::ringRound2Sign(bob, stB);
  stA.peerPartialResponse = stB.ourPartialResponse;
  if (!SwapTxBuilder::ringRound2Finalize(stA, tx)) return false;

  std::vector<const Crypto::PublicKey*> pubs;
  for (auto& p : stA.ringPubKeys) pubs.push_back(&p);
  return Crypto::check_ring_signature(prefix, stA.aggregateKeyImage, pubs,
                                      tx.signatures[0].data());
}

static bool stage_state_machine_path() {
  SwapParams p{};
  p.swapId = "sm-dual";
  p.role = SwapRole::BOB;
  p.pair = SwapPair::ETH;
  SwapStateMachine sm(p);
  sm.setEncryptionKey("dual-proto-enc");
  if (!sm.transition(SwapState::ADAPTOR_KEYS_EXCHANGED)) return false;
  if (!sm.transition(SwapState::ADAPTOR_ESCROW_FUNDED)) return false;
  if (!sm.transition(SwapState::ADAPTOR_PRESIGS_READY)) return false;
  if (!sm.transition(SwapState::ADAPTOR_CTR_LOCKED)) return false;
  if (!sm.transition(SwapState::ADAPTOR_SECRET_REVEALED)) return false;
  if (!sm.transition(SwapState::ADAPTOR_XFG_SPENT)) return false;
  return sm.isTerminal();
}

int main() {
  std::cout << "=== Dual-party XFG protocol drive ===\n";
  SwapParams alice, bob;
  CHECK(setupRoles(alice, bob), "key exchange + shared escrow pubkey");
  CHECK(stage_adaptor_and_extract(alice, bob),
        "adaptor pre-sig + on-chain extract of t");
  CHECK(stage_collab_ring_spend(alice, bob),
        "collab ring size-9 spend verifies (XFG spend path)");
  CHECK(stage_state_machine_path(), "SM path keys→escrow→secret→XFG_SPENT");

  std::cout << "\nResults: " << g_pass << " passed, " << g_fail << " failed\n";
  if (g_fail == 0) {
    std::cout << "NOTE: On-chain fundEscrow still requires working miner/decoys;\n"
              << "this test covers the dual-party crypto after escrow exists.\n";
  }
  return g_fail == 0 ? 0 : 1;
}
