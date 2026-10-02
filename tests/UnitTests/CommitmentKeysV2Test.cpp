// Spend-key-bound (v2) commitment keys. Legacy (v1) keys come from the view-key
// derivation alone, so the sender and any view-key holder could spend them.
#include <gtest/gtest.h>

#include "Common/StringTools.h"
#include "CryptoNoteCore/TransactionExtra.h"
#include "crypto/crypto.h"

using namespace CryptoNote;
using Common::podToHex;

namespace {

Crypto::SecretKey scalarFrom(const char* label) {
  Crypto::SecretKey s;
  Crypto::hash_to_scalar(label, strlen(label), reinterpret_cast<Crypto::EllipticCurveScalar&>(s));
  return s;
}

struct Keys {
  Crypto::SecretKey sec;
  Crypto::PublicKey pub;
  explicit Keys(const char* label) : sec(scalarFrom(label)) { Crypto::secret_key_to_public_key(sec, pub); }
};

const Keys view("fuego-test-view");
const Keys spend("fuego-test-spend");
const Keys tx("fuego-test-tx");
const uint32_t outputIndex = 3;

Crypto::KeyDerivation senderDerivation() {
  Crypto::KeyDerivation d;
  EXPECT_TRUE(Crypto::generate_key_derivation(view.pub, tx.sec, d));
  return d;
}

Crypto::KeyDerivation receiverDerivation() {
  Crypto::KeyDerivation d;
  EXPECT_TRUE(Crypto::generate_key_derivation(tx.pub, view.sec, d));
  return d;
}

}  // namespace

TEST(CommitmentKeysV2, SenderAndReceiverAgreeAndOnlyTheSpendKeyOpensIt) {
  Crypto::PublicKey sent, found;
  ASSERT_TRUE(deriveCommitmentPublicKeyV2(senderDerivation(), outputIndex, spend.pub, sent));
  ASSERT_TRUE(deriveCommitmentPublicKeyV2(receiverDerivation(), outputIndex, spend.pub, found));
  EXPECT_EQ(podToHex(sent), podToHex(found));

  Crypto::SecretKey keyScalar;
  deriveCommitmentSecretKeyV2(receiverDerivation(), outputIndex, spend.sec, keyScalar);
  Crypto::PublicKey fromScalar;
  ASSERT_TRUE(Crypto::secret_key_to_public_key(keyScalar, fromScalar));
  EXPECT_EQ(podToHex(fromScalar), podToHex(sent));

  // What the sender or a view-key holder can compute does not open it.
  EXPECT_NE(podToHex(deriveCommitmentKeysV1(senderDerivation(), outputIndex).commitKey), podToHex(sent));
  Crypto::SecretKey wrongScalar;
  deriveCommitmentSecretKeyV2(receiverDerivation(), outputIndex, view.sec, wrongScalar);
  Crypto::PublicKey wrongKey;
  ASSERT_TRUE(Crypto::secret_key_to_public_key(wrongScalar, wrongKey));
  EXPECT_NE(podToHex(wrongKey), podToHex(sent));
}

TEST(CommitmentKeysV2, OwnedKeysFollowTheRecordedKeyImage) {
  const Crypto::KeyDerivation d = receiverDerivation();
  Crypto::PublicKey v2Key;
  ASSERT_TRUE(deriveCommitmentPublicKeyV2(d, outputIndex, spend.pub, v2Key));
  Crypto::SecretKey v2Scalar;
  deriveCommitmentSecretKeyV2(d, outputIndex, spend.sec, v2Scalar);
  Crypto::KeyImage v2Image;
  Crypto::generate_key_image(v2Key, v2Scalar, v2Image);
  const DepositCommitmentKeys v1 = deriveCommitmentKeysV1(d, outputIndex);

  Crypto::PublicKey key;
  Crypto::SecretKey scalar;
  Crypto::KeyImage image;
  ASSERT_TRUE(deriveOwnedCommitmentKeys(d, outputIndex, spend.pub, spend.sec, v2Image, key, scalar, image));
  EXPECT_EQ(podToHex(key), podToHex(v2Key));
  EXPECT_EQ(podToHex(image), podToHex(v2Image));

  ASSERT_TRUE(deriveOwnedCommitmentKeys(d, outputIndex, spend.pub, spend.sec, v1.keyImage, key, scalar, image));
  EXPECT_EQ(podToHex(key), podToHex(v1.commitKey));
  EXPECT_EQ(podToHex(image), podToHex(v1.keyImage));

  Crypto::KeyImage unknown{};
  EXPECT_FALSE(deriveOwnedCommitmentKeys(d, outputIndex, spend.pub, spend.sec, unknown, key, scalar, image));
}

// Fixed vector shared with the Rust SDK (fuego-crypto ring.rs):
//   view.sec  7a7bf6318c2307d1294e1958ec156e40105c329009b655f39a44557bf0d97709
//   spend.sec 1b7fb8744e107908891ad502d07f58cb3a28c4fb3afeee312a4323c91e066401
//   tx.sec    5e579bb6fdd693c6af76e12b413e6cc5553fa74a8734d33fc29f1b1ab61fc408
//   output index 3
TEST(CommitmentKeysV2, Vector) {
  EXPECT_EQ(podToHex(view.sec), "7a7bf6318c2307d1294e1958ec156e40105c329009b655f39a44557bf0d97709");
  EXPECT_EQ(podToHex(spend.sec), "1b7fb8744e107908891ad502d07f58cb3a28c4fb3afeee312a4323c91e066401");
  EXPECT_EQ(podToHex(tx.sec), "5e579bb6fdd693c6af76e12b413e6cc5553fa74a8734d33fc29f1b1ab61fc408");
  Crypto::PublicKey key;
  ASSERT_TRUE(deriveCommitmentPublicKeyV2(senderDerivation(), outputIndex, spend.pub, key));
  EXPECT_EQ(podToHex(key), "52eedf4b1b4f157305bd31335fb64fd911acbe33e5eeab378b5010aba07df70d");
}
