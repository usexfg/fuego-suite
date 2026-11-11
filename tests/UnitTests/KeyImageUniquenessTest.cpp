// Intra-transaction key-image uniqueness (H-1).
//
// The defect under test: key-image uniqueness was enforced only against *chain*
// state. checkTransactionInputs walked the inputs and asked "has this key image
// already been spent on the chain?" for each one, but never asked whether two
// inputs of the *same* transaction carried the same key image.
//
// That made the crash reachable. A transaction shaped like
//
//     [ KeyInput(k), SwapEscrow(..), KeyInput(k) ]
//
// passed validation (neither instance of k is spent on chain yet, and the ring
// signatures are valid because they really are valid). It then failed at connect,
// where the second KeyInput(k) insert collided, and the rollback loop erased
// earlier inputs with an unconditional boost::get<KeyInput> -- which throws
// boost::bad_get on the SwapEscrow element. Nothing on the connect path caught
// it, so the exception escaped and the daemon terminated: a remote crash from
// any peer that can relay a block or get a transaction into a miner's mempool.
//
// checkKeyImagesUnique is the guard. It is a sibling of the existing
// checkMultisignatureInputsDiff / checkSwapEscrowInputsDiff and deliberately
// spans every key-image-carrying input type rather than just KeyInput -- a guard
// that only covers the type that happened to trigger the crash would leave the
// same bug one variant away.
//
// Cases are deterministic: fixed key images, no randomness, no clock, no chain.

#include <cstdint>
#include <vector>

#include "gtest/gtest.h"

#include "crypto/crypto.h"
#include "CryptoNote.h"
#include "CryptoNoteConfig.h"
#include "CryptoNoteCore/CryptoNoteFormatUtils.h"

using namespace CryptoNote;

namespace {

// A distinct, arbitrary 32-byte value per tag. Real key images are points; the
// guard compares bytes and does not interpret them, so any distinct value serves.
Crypto::KeyImage keyImageWithTag(uint8_t tag) {
  Crypto::KeyImage ki = {};
  ki.data[0] = tag;
  ki.data[31] = static_cast<uint8_t>(tag ^ 0xFFu);
  return ki;
}

KeyInput makeKeyInput(uint8_t tag) {
  KeyInput in;
  in.amount = 1000 + tag;
  in.outputIndexes = {0, 1, 2};
  in.keyImage = keyImageWithTag(tag);
  return in;
}

TransactionInputCommitmentSpend makeCommitmentSpend(uint8_t tag) {
  TransactionInputCommitmentSpend in;
  in.amount = 5000 + tag;
  in.outputIndexes = {0, 1};
  in.keyImage = keyImageWithTag(tag);
  in.claimedInterest = 0;
  return in;
}

TransactionInputCommitmentTransfer makeCommitmentTransfer(uint8_t tag) {
  TransactionInputCommitmentTransfer in;
  in.amount = 7000 + tag;
  in.outputIndexes = {0};
  in.keyImage = keyImageWithTag(tag);
  in.newTerm = 1;
  return in;
}

TransactionInputSwapEscrow makeSwapEscrow(uint8_t tag) {
  TransactionInputSwapEscrow in;
  in.amount = 9000 + tag;
  std::memset(&in.escrowTxId, 0, sizeof(in.escrowTxId));
  in.escrowTxId.data[0] = tag;
  in.escrowOutputIndex = 0;
  in.mode = 0;
  in.keyImage = keyImageWithTag(tag);
  return in;
}

MultisignatureInput makeMultisig(uint32_t outputIndex) {
  MultisignatureInput in;
  in.amount = 4242;
  in.signatureCount = 2;
  in.outputIndex = outputIndex;
  in.term = 0;
  return in;
}

TransactionPrefix prefixWith(std::vector<TransactionInput> inputs) {
  TransactionPrefix tx;
  tx.version = TRANSACTION_VERSION_1;
  tx.unlockTime = 0;
  tx.inputs = std::move(inputs);
  return tx;
}

} // namespace

TEST(KeyImageUniqueness, EmptyTransactionIsAccepted) {
  EXPECT_TRUE(checkKeyImagesUnique(prefixWith({})));
}

TEST(KeyImageUniqueness, SingleKeyInputIsAccepted) {
  EXPECT_TRUE(checkKeyImagesUnique(prefixWith({makeKeyInput(1)})));
}

TEST(KeyImageUniqueness, DistinctKeyInputsAreAccepted) {
  EXPECT_TRUE(checkKeyImagesUnique(prefixWith({makeKeyInput(1), makeKeyInput(2)})));
}

// The direct defect: two inputs of one transaction spending the same key image.
TEST(KeyImageUniqueness, DuplicateKeyInputIsRejected) {
  EXPECT_FALSE(checkKeyImagesUnique(prefixWith({makeKeyInput(7), makeKeyInput(7)})));
}

// The exact crash shape. The mixed-type variant is what made the old rollback
// throw: erasing input 0 or 1 as KeyInput would have hit the SwapEscrow element.
TEST(KeyImageUniqueness, KeyInputDuplicatedAroundAnotherTypeIsRejected) {
  EXPECT_FALSE(checkKeyImagesUnique(prefixWith(
      {makeKeyInput(3), makeSwapEscrow(9), makeKeyInput(3)})));
}

TEST(KeyImageUniqueness, KeyInputAndCommitmentSpendSharingAnImageIsRejected) {
  EXPECT_FALSE(checkKeyImagesUnique(
      prefixWith({makeKeyInput(4), makeCommitmentSpend(4)})));
}

TEST(KeyImageUniqueness, KeyInputAndSwapEscrowSharingAnImageIsRejected) {
  EXPECT_FALSE(checkKeyImagesUnique(
      prefixWith({makeKeyInput(5), makeSwapEscrow(5)})));
}

TEST(KeyImageUniqueness, CommitmentSpendAndTransferSharingAnImageIsRejected) {
  EXPECT_FALSE(checkKeyImagesUnique(
      prefixWith({makeCommitmentSpend(6), makeCommitmentTransfer(6)})));
}

// A duplicate three inputs in, so the guard cannot pass by only comparing
// consecutive pairs.
TEST(KeyImageUniqueness, NonAdjacentDuplicateIsRejected) {
  EXPECT_FALSE(checkKeyImagesUnique(prefixWith(
      {makeKeyInput(8), makeCommitmentSpend(1), makeSwapEscrow(2), makeKeyInput(8)})));
}

// The control for the mixed-type case: same types, distinct images. Guards
// against a fix that simply rejects every mixed-type transaction.
TEST(KeyImageUniqueness, MixedTypesWithDistinctImagesAreAccepted) {
  EXPECT_TRUE(checkKeyImagesUnique(prefixWith(
      {makeKeyInput(1), makeSwapEscrow(2), makeCommitmentSpend(3),
       makeCommitmentTransfer(4)})));
}

// MultisignatureInput carries no key image, so it is out of scope here and is
// covered by checkMultisignatureInputsDiff instead. Two of them must not make
// this guard fire -- otherwise the two checks would contradict each other.
TEST(KeyImageUniqueness, RepeatedMultisignatureInputsAreNotThisGuardsBusiness) {
  EXPECT_TRUE(checkKeyImagesUnique(
      prefixWith({makeMultisig(0), makeMultisig(1), makeMultisig(1)})));
}

// MultisignatureInput mixed with a duplicated key image still has to fail.
TEST(KeyImageUniqueness, DuplicateKeyImageIsRejectedAlongsideMultisig) {
  EXPECT_FALSE(checkKeyImagesUnique(
      prefixWith({makeMultisig(0), makeKeyInput(2), makeKeyInput(2)})));
}
