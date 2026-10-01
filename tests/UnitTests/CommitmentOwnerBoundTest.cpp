// Owner-bound commitment derivation coverage.
//
// The defect under test: deriveCommitmentKeys computes the spend scalar as
// Hs("fuego_commit_key" || depositSecret), and depositSecret is derived from
// D = rA = aR. The sender and any holder of the view secret a can therefore
// compute the spend key of a legacy commitment output. The owner-bound form
// P = B + tG requires the recipient spend secret b to produce x = b + t, so
// sender and view-only material is insufficient.
//
// Cases here are deterministic: all keys come from a fixed seed.

#include <array>
#include <set>
#include <vector>

#include "gtest/gtest.h"

#include "CryptoNoteCore/TransactionExtra.h"
#include "crypto/crypto.h"
#include "crypto/hash.h"

using namespace CryptoNote;

namespace {

struct TestKeys {
  Crypto::PublicKey   spendPublic;
  Crypto::SecretKey   spendSecret;
  Crypto::PublicKey   viewPublic;
  Crypto::SecretKey   viewSecret;
};

// Deterministic: `tag` fully determines the key material, so every vector is
// reproducible and the owner/stranger pairs are genuinely distinct keys.
TestKeys makeTestKeys(uint8_t tag) {
  Crypto::Hash seed;
  uint8_t preimage[5] = {'a', 'c', 'c', 't', tag};
  Crypto::cn_fast_hash(preimage, sizeof(preimage), seed);

  TestKeys keys;
  Crypto::SecretKey spendSeed;
  memcpy(spendSeed.data, seed.data, 32);
  spendSeed.data[31] &= 0x0F;
  spendSeed.data[0] |= 0x40;
  Crypto::generate_keys_from_seed(keys.spendPublic, keys.spendSecret, spendSeed);

  uint8_t viewPreimage[5] = {'v', 'i', 'e', 'w', tag};
  Crypto::Hash viewSeed;
  Crypto::cn_fast_hash(viewPreimage, sizeof(viewPreimage), viewSeed);
  memcpy(keys.viewSecret.data, viewSeed.data, 32);
  keys.viewSecret.data[31] &= 0x0F;
  keys.viewSecret.data[0] |= 0x40;
  Crypto::secret_key_to_public_key(keys.viewSecret, keys.viewPublic);
  return keys;
}

Crypto::KeyDerivation makeDerivation(const Crypto::PublicKey& txPublic, const Crypto::SecretKey& receiverViewSecret) {
  Crypto::KeyDerivation derivation;
  EXPECT_TRUE(Crypto::generate_key_derivation(txPublic, receiverViewSecret, derivation));
  return derivation;
}

// A deterministic transaction key, standing in for the sender's rG.
Crypto::PublicKey makeTxPublicKey(uint8_t tag) {
  Crypto::SecretKey txSecret;
  Crypto::Hash seed;
  uint8_t preimage[4] = {'t', 'x', 'k', tag};
  Crypto::cn_fast_hash(preimage, sizeof(preimage), seed);
  memcpy(txSecret.data, seed.data, 32);
  txSecret.data[31] &= 0x0F; // stay clear of the group order
  txSecret.data[0]  |= 0x40; // never the identity
  Crypto::PublicKey txPublic;
  Crypto::secret_key_to_public_key(txSecret, txPublic);
  return txPublic;
}

// Indices required by the guide: 0, 1, 127, 128 and a large index.
const std::vector<size_t>& guideIndices() {
  static const std::vector<size_t> indices = {0, 1, 127, 128, 1000000};
  return indices;
}

std::unordered_set<Crypto::PublicKey> keySet(const Crypto::PublicKey& key) {
  std::unordered_set<Crypto::PublicKey> set;
  set.insert(key);
  return set;
}

} // namespace

// The recipient's spend secret reproduces the output key; the sender and a
// view-only holder cannot produce a valid key image for it.
TEST(OwnerBoundCommitment, SpendSecretMatchesOutputKey) {
  TestKeys keys = makeTestKeys(1);
  Crypto::PublicKey txPublic = makeTxPublicKey(1);
  Crypto::KeyDerivation derivation = makeDerivation(txPublic, keys.viewSecret);

  for (size_t index : guideIndices()) {
    Crypto::PublicKey commitKey;
    ASSERT_TRUE(deriveOwnerBoundCommitKey(derivation, index, keys.spendPublic, commitKey))
      << "index " << index;

    Crypto::SecretKey spendSecret;
    Crypto::KeyImage keyImage;
    ASSERT_TRUE(deriveOwnerBoundKeyImage(
      derivation, index, commitKey, keys.spendSecret, spendSecret, keyImage))
      << "index " << index;

    // x must be a genuine scalar and must reproduce P.
    Crypto::PublicKey check;
    ASSERT_TRUE(secret_key_to_public_key(spendSecret, check));
    EXPECT_EQ(check, commitKey) << "index " << index;
  }
}

// A sender holding only r, the recipient's A, B and public chain data must not
// be able to sign. The sender can regenerate D = rA but has no b.
TEST(OwnerBoundCommitment, SenderCannotDeriveKeyImage) {
  TestKeys keys = makeTestKeys(2);
  Crypto::PublicKey txPublic = makeTxPublicKey(2);
  Crypto::KeyDerivation recipientDerivation = makeDerivation(txPublic, keys.viewSecret);

  for (size_t index : guideIndices()) {
    Crypto::PublicKey commitKey;
    ASSERT_TRUE(deriveOwnerBoundCommitKey(recipientDerivation, index, keys.spendPublic, commitKey));

    // Sender-side derivation from public data only.
    Crypto::Hash senderSeed;
    uint8_t preimage[32];
    memcpy(preimage, txPublic.data, 32);
    Crypto::cn_fast_hash(preimage, sizeof(preimage), senderSeed);
    Crypto::SecretKey senderGuess;
    memcpy(senderGuess.data, senderSeed.data, 32);

    Crypto::SecretKey spendSecret;
    Crypto::KeyImage keyImage;
    EXPECT_FALSE(deriveOwnerBoundKeyImage(
      recipientDerivation, index, commitKey, senderGuess, spendSecret, keyImage))
      << "index " << index;
  }
}

// A wrong spend key must not produce a key image for someone else's output.
TEST(OwnerBoundCommitment, WrongSpendKeyRejected) {
  TestKeys owner = makeTestKeys(3);
  TestKeys stranger = makeTestKeys(30);

  Crypto::PublicKey txPublic = makeTxPublicKey(3);
  Crypto::KeyDerivation derivation = makeDerivation(txPublic, owner.viewSecret);

  for (size_t index : guideIndices()) {
    Crypto::PublicKey commitKey;
    ASSERT_TRUE(deriveOwnerBoundCommitKey(derivation, index, owner.spendPublic, commitKey));

    Crypto::SecretKey spendSecret;
    Crypto::KeyImage keyImage;
    EXPECT_FALSE(deriveOwnerBoundKeyImage(
      derivation, index, commitKey, stranger.spendSecret, spendSecret, keyImage))
      << "index " << index;
  }
}

// The view secret alone must never yield a spend scalar for an owner-bound
// output. This is the exact property the legacy derivation violated.
TEST(OwnerBoundCommitment, ViewSecretAloneCannotSpend) {
  TestKeys keys = makeTestKeys(4);
  Crypto::PublicKey txPublic = makeTxPublicKey(4);
  Crypto::KeyDerivation derivation = makeDerivation(txPublic, keys.viewSecret);

  for (size_t index : guideIndices()) {
    Crypto::PublicKey commitKey;
    ASSERT_TRUE(deriveOwnerBoundCommitKey(derivation, index, keys.spendPublic, commitKey));

    Crypto::SecretKey spendSecret;
    Crypto::KeyImage keyImage;
    EXPECT_FALSE(deriveOwnerBoundKeyImage(
      derivation, index, commitKey, keys.viewSecret, spendSecret, keyImage))
      << "index " << index;
  }
}

// View-only recognition: the scanner identifies the matching spend key using
// underive_public_key, and matches the correct key when several are registered.
TEST(OwnerBoundCommitment, ViewOnlyScanningIdentifiesMatchingKey) {
  TestKeys owner = makeTestKeys(5);
  TestKeys other = makeTestKeys(50);

  Crypto::PublicKey txPublic = makeTxPublicKey(5);
  Crypto::KeyDerivation derivation = makeDerivation(txPublic, owner.viewSecret);

  std::unordered_set<Crypto::PublicKey> registered;
  registered.insert(other.spendPublic);
  registered.insert(owner.spendPublic);

  for (size_t index : guideIndices()) {
    Crypto::PublicKey commitKey;
    ASSERT_TRUE(deriveOwnerBoundCommitKey(derivation, index, owner.spendPublic, commitKey));

    Crypto::PublicKey matched;
    bool ambiguous = false;
    ASSERT_TRUE(matchOwnerBoundCommitKey(
      derivation, index, commitKey, registered, matched, ambiguous))
      << "index " << index;
    EXPECT_FALSE(ambiguous) << "index " << index;
    EXPECT_EQ(matched, owner.spendPublic)
      << "index " << index << ": scanner attributed the output to the wrong key";
  }
}

// An output belonging to an unregistered key must not be claimed.
TEST(OwnerBoundCommitment, UnrelatedOutputNotClaimed) {
  TestKeys owner = makeTestKeys(6);
  TestKeys stranger = makeTestKeys(60);

  Crypto::PublicKey txPublic = makeTxPublicKey(6);
  Crypto::KeyDerivation derivation = makeDerivation(txPublic, owner.viewSecret);

  Crypto::PublicKey commitKey;
  ASSERT_TRUE(deriveOwnerBoundCommitKey(derivation, 3, owner.spendPublic, commitKey));

  Crypto::PublicKey matched;
  bool ambiguous = false;
  EXPECT_FALSE(matchOwnerBoundCommitKey(
    derivation, 3, commitKey, keySet(stranger.spendPublic), matched, ambiguous));
  EXPECT_FALSE(ambiguous);
}

// An invalid public point must be rejected at the boundary.
TEST(OwnerBoundCommitment, InvalidPublicKeyRejected) {
  TestKeys keys = makeTestKeys(7);
  Crypto::PublicKey txPublic = makeTxPublicKey(7);
  Crypto::KeyDerivation derivation = makeDerivation(txPublic, keys.viewSecret);

  Crypto::PublicKey invalid;
  memset(invalid.data, 0xFF, sizeof(invalid.data));

  Crypto::PublicKey commitKey;
  EXPECT_FALSE(deriveOwnerBoundCommitKey(derivation, 0, invalid, commitKey));

  Crypto::SecretKey spendSecret;
  Crypto::KeyImage keyImage;
  EXPECT_FALSE(deriveOwnerBoundKeyImage(
    derivation, 0, invalid, keys.spendSecret, spendSecret, keyImage));
}

// Two recipients in one transaction must each be attributed to their own key.
TEST(OwnerBoundCommitment, TwoRecipientsInOneTransaction) {
  TestKeys first = makeTestKeys(8);
  TestKeys second = makeTestKeys(80);

  Crypto::PublicKey txPublic = makeTxPublicKey(8);
  Crypto::KeyDerivation derivation = makeDerivation(txPublic, first.viewSecret);

  std::unordered_set<Crypto::PublicKey> registered;
  registered.insert(first.spendPublic);
  registered.insert(second.spendPublic);

  Crypto::PublicKey firstCommit;
  Crypto::PublicKey secondCommit;
  ASSERT_TRUE(deriveOwnerBoundCommitKey(derivation, 0, first.spendPublic, firstCommit));
  ASSERT_TRUE(deriveOwnerBoundCommitKey(derivation, 1, second.spendPublic, secondCommit));

  Crypto::PublicKey matched;
  bool ambiguous = false;
  ASSERT_TRUE(matchOwnerBoundCommitKey(derivation, 0, firstCommit, registered, matched, ambiguous));
  EXPECT_EQ(matched, first.spendPublic);
  ASSERT_TRUE(matchOwnerBoundCommitKey(derivation, 1, secondCommit, registered, matched, ambiguous));
  EXPECT_EQ(matched, second.spendPublic);
}

// The legacy and owner-bound forms must be distinguishable by the output key,
// so an upgraded scanner can tell them apart with no new tx_extra tag.
TEST(OwnerBoundCommitment, LegacyAndOwnerBoundOutputsAreDistinguishable) {
  TestKeys keys = makeTestKeys(9);
  Crypto::PublicKey txPublic = makeTxPublicKey(9);
  Crypto::KeyDerivation derivation = makeDerivation(txPublic, keys.viewSecret);

  for (size_t index : guideIndices()) {
    Crypto::PublicKey ownerBound;
    ASSERT_TRUE(deriveOwnerBoundCommitKey(derivation, index, keys.spendPublic, ownerBound));

    const std::array<uint8_t, 32> depositSecret = deriveDepositSecret(derivation, index);
    const DepositCommitmentKeys legacy = deriveLegacyCommitmentKeys(depositSecret);

    EXPECT_NE(ownerBound, legacy.commitKey) << "index " << index;
  }
}

// The amount mask is unchanged: it still derives from depositSecret, so amounts
// stay masked exactly as before.
TEST(OwnerBoundCommitment, AmountMaskDerivedFromDepositSecret) {
  Crypto::KeyDerivation derivation;
  memset(derivation.data, 0xA5, sizeof(derivation.data));

  for (size_t index : guideIndices()) {
    const std::array<uint8_t, 32> depositSecret = deriveDepositSecret(derivation, index);
    const DepositCommitmentKeys legacy = deriveLegacyCommitmentKeys(depositSecret);
    const DepositCommitmentKeys again = deriveLegacyCommitmentKeys(depositSecret);
    EXPECT_EQ(0, memcmp(legacy.amountMask.data, again.amountMask.data, 32)) << "index " << index;
  }
}

// depositSecret must keep its exact pre-v11 byte layout so historical rescans
// and amount masks stay valid.
TEST(OwnerBoundCommitment, DepositSecretLayoutUnchanged) {
  Crypto::KeyDerivation derivation;
  memset(derivation.data, 0x5A, sizeof(derivation.data));

  for (size_t index : guideIndices()) {
    uint8_t preimage[36];
    memcpy(preimage, &derivation, 32);
    uint32_t outIdx = static_cast<uint32_t>(index);
    preimage[32] = outIdx & 0xFF;
    preimage[33] = (outIdx >> 8) & 0xFF;
    preimage[34] = (outIdx >> 16) & 0xFF;
    preimage[35] = (outIdx >> 24) & 0xFF;
    Crypto::Hash expected = Crypto::cn_fast_hash(preimage, sizeof(preimage));

    const std::array<uint8_t, 32> depositSecret = deriveDepositSecret(derivation, index);
    EXPECT_EQ(0, memcmp(expected.data, depositSecret.data(), 32)) << "index " << index;
  }
}

// A legacy output must still be recognised after the change, so old funds
// remain discoverable. This is the property the guide calls "old outputs
// remain discoverable".
TEST(OwnerBoundCommitment, LegacyOutputStillRecognisable) {
  TestKeys keys = makeTestKeys(10);
  Crypto::PublicKey txPublic = makeTxPublicKey(10);
  Crypto::KeyDerivation derivation = makeDerivation(txPublic, keys.viewSecret);

  for (size_t index : guideIndices()) {
    const std::array<uint8_t, 32> depositSecret = deriveDepositSecret(derivation, index);
    const DepositCommitmentKeys legacy = deriveLegacyCommitmentKeys(depositSecret);

    Crypto::PublicKey matched;
    bool ambiguous = false;
    // No owner-bound key in this wallet derives the legacy output key.
    EXPECT_FALSE(matchOwnerBoundCommitKey(
      derivation, index, legacy.commitKey, keySet(keys.spendPublic), matched, ambiguous))
      << "index " << index;
    EXPECT_FALSE(ambiguous) << "index " << index;
  }
}

// The legacy derivation is retained and is byte-identical to the pre-change
// behaviour, so historical key images and amounts still validate.
TEST(OwnerBoundCommitment, LegacyDerivationUnchanged) {
  std::array<uint8_t, 32> depositSecret;
  for (size_t i = 0; i < depositSecret.size(); ++i) {
    depositSecret[i] = static_cast<uint8_t>(i * 7 + 3);
  }

  const DepositCommitmentKeys legacyNamed = deriveLegacyCommitmentKeys(depositSecret);
  const DepositCommitmentKeys legacyOriginal = deriveCommitmentKeys(depositSecret);

  EXPECT_EQ(legacyNamed.commitKey, legacyOriginal.commitKey);
  EXPECT_EQ(0, memcmp(legacyNamed.amountMask.data, legacyOriginal.amountMask.data, 32));
  EXPECT_EQ(legacyNamed.keyImage, legacyOriginal.keyImage);
}


// deriveCommitmentOutputKey is the single entry point wallet creation sites
// use. With a recipient spend key it must produce the owner-bound form, so a
// caller cannot emit an exposed output by omitting the key.
TEST(OwnerBoundCommitment, CreationHelperProducesOwnerBoundKey) {
  const TestKeys keys = makeTestKeys(11);
  const TestKeys sender = makeTestKeys(12);
  const Crypto::KeyDerivation derivation =
    makeDerivation(sender.viewPublic, keys.viewSecret);

  for (size_t index : guideIndices()) {
    Crypto::PublicKey commitKey{};
    ASSERT_TRUE(deriveCommitmentOutputKey(derivation, index, keys.spendPublic, commitKey));

    Crypto::PublicKey direct{};
    ASSERT_TRUE(deriveOwnerBoundCommitKey(derivation, index, keys.spendPublic, direct));
    EXPECT_EQ(direct, commitKey) << "index " << index;

    Crypto::SecretKey senderScalar;
    Crypto::KeyImage senderImage;
    EXPECT_FALSE(deriveOwnerBoundKeyImage(
      derivation, index, commitKey, sender.spendSecret, senderScalar, senderImage));
  }
}

// A null recipient key is the protocol-owned escape hatch (pool escrow markers,
// excluded from rings by term). It must reproduce the legacy key exactly and
// never fail, or those creation paths would start throwing.
TEST(OwnerBoundCommitment, CreationHelperProtocolOwnedUsesLegacyForm) {
  const TestKeys keys = makeTestKeys(13);
  const TestKeys sender = makeTestKeys(14);
  const Crypto::KeyDerivation derivation =
    makeDerivation(sender.viewPublic, keys.viewSecret);

  for (size_t index : guideIndices()) {
    Crypto::PublicKey commitKey{};
    ASSERT_TRUE(deriveCommitmentOutputKey(derivation, index, Crypto::PublicKey{}, commitKey));

    const std::array<uint8_t, 32> depositSecret = deriveDepositSecret(derivation, index);
    const DepositCommitmentKeys legacy = deriveLegacyCommitmentKeys(depositSecret);
    EXPECT_EQ(legacy.commitKey, commitKey) << "index " << index;
  }
}

// A creation site must never silently degrade to the legacy form: an invalid
// recipient key is a rejection, not a fallback.
TEST(OwnerBoundCommitment, CreationHelperRejectsInvalidRecipientKey) {
  const TestKeys keys = makeTestKeys(15);
  const TestKeys sender = makeTestKeys(16);
  const Crypto::KeyDerivation derivation =
    makeDerivation(sender.viewPublic, keys.viewSecret);

  Crypto::PublicKey invalid{};
  memset(invalid.data, 0xFF, sizeof(invalid.data));
  for (size_t index : guideIndices()) {
    Crypto::PublicKey commitKey{};
    EXPECT_FALSE(deriveCommitmentOutputKey(derivation, index, invalid, commitKey))
      << "index " << index;
  }
}
