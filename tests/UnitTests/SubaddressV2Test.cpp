#include <cstring>
#include <string>
#include <vector>

#include "gtest/gtest.h"

#include "CryptoNoteConfig.h"
#include "CryptoNoteCore/CryptoNoteBasicImpl.h"
#include "CryptoNoteCore/SubaddressTx.h"
#include "CryptoNoteCore/TransactionExtra.h"
#include "crypto/crypto.h"
#include "crypto/subaddress.h"

using namespace Crypto;
using namespace CryptoNote;

namespace {

struct TestKeys {
  PublicKey spendPublic;
  SecretKey spendSecret;
  PublicKey viewPublic;
  SecretKey viewSecret;
};

TestKeys makeTestKeys(uint8_t tag) {
  Hash seed;
  uint8_t preimage[5] = {'s', 'u', 'b', 'a', tag};
  cn_fast_hash(preimage, sizeof(preimage), seed);

  TestKeys keys;
  SecretKey spendSeed;
  memcpy(spendSeed.data, seed.data, 32);
  spendSeed.data[31] &= 0x0F;
  spendSeed.data[0] |= 0x40;
  generate_keys_from_seed(keys.spendPublic, keys.spendSecret, spendSeed);

  uint8_t viewPreimage[5] = {'s', 'u', 'b', 'v', tag};
  Hash viewSeed;
  cn_fast_hash(viewPreimage, sizeof(viewPreimage), viewSeed);
  memcpy(keys.viewSecret.data, viewSeed.data, 32);
  keys.viewSecret.data[31] &= 0x0F;
  keys.viewSecret.data[0] |= 0x40;
  secret_key_to_public_key(keys.viewSecret, keys.viewPublic);
  return keys;
}

SecretKey makeScalar(uint8_t tag) {
  Hash seed;
  uint8_t preimage[4] = {'s', 'c', 'l', tag};
  cn_fast_hash(preimage, sizeof(preimage), seed);
  SecretKey s;
  memcpy(s.data, seed.data, 32);
  s.data[31] &= 0x0F;
  s.data[0] |= 0x40;
  return s;
}

} // namespace

TEST(SubaddressV2, V1ViewKeyEqualsMasterView) {
  TestKeys k = makeTestKeys(1);
  SubAddressKeys sub = deriveSubAddressKeys(k.viewSecret, k.spendPublic, &k.spendSecret, 0, 1);
  EXPECT_EQ(sub.version, SUBADDRESS_VERSION_V1);
  EXPECT_EQ(0, memcmp(&sub.viewPublicKey, &k.viewPublic, sizeof(PublicKey)));
  EXPECT_NE(0, memcmp(&sub.spendPublicKey, &k.spendPublic, sizeof(PublicKey)));
}

TEST(SubaddressV2, V2ViewKeyIsATimesD) {
  TestKeys k = makeTestKeys(2);
  SubAddressKeys sub = deriveSubAddressKeysV2(k.viewSecret, k.spendPublic, &k.spendSecret, 0, 1);
  EXPECT_EQ(sub.version, SUBADDRESS_VERSION_V2);
  EXPECT_NE(0, memcmp(&sub.viewPublicKey, &k.viewPublic, sizeof(PublicKey)));

  PublicKey expectedC;
  ASSERT_TRUE(scalarmultKeyNoCofactor(sub.spendPublicKey, k.viewSecret, expectedC));
  EXPECT_EQ(0, memcmp(&sub.viewPublicKey, &expectedC, sizeof(PublicKey)));
}

TEST(SubaddressV2, DomainTagSeparatesSpendPoints) {
  TestKeys k = makeTestKeys(3);
  SubAddressKeys v1 = deriveSubAddressKeys(k.viewSecret, k.spendPublic, &k.spendSecret, 1, 2);
  SubAddressKeys v2 = deriveSubAddressKeysV2(k.viewSecret, k.spendPublic, &k.spendSecret, 1, 2);
  EXPECT_NE(0, memcmp(&v1.spendPublicKey, &v2.spendPublicKey, sizeof(PublicKey)));
}

TEST(SubaddressV2, DistinctIndicesUnlinkable) {
  TestKeys k = makeTestKeys(4);
  SubAddressKeys a = deriveSubAddressKeysV2(k.viewSecret, k.spendPublic, &k.spendSecret, 0, 1);
  SubAddressKeys b = deriveSubAddressKeysV2(k.viewSecret, k.spendPublic, &k.spendSecret, 0, 2);
  EXPECT_NE(0, memcmp(&a.spendPublicKey, &b.spendPublicKey, sizeof(PublicKey)));
  EXPECT_NE(0, memcmp(&a.viewPublicKey, &b.viewPublicKey, sizeof(PublicKey)));
}

TEST(SubaddressV2, FunsaPrefixAndParseFlag) {
  TestKeys k = makeTestKeys(5);
  SubAddressKeys sub = deriveSubAddressKeysV2(k.viewSecret, k.spendPublic, &k.spendSecret, 0, 1);
  AccountPublicAddress addr{sub.spendPublicKey, sub.viewPublicKey};
  std::string encoded = getAccountAddressAsStr(parameters::CRYPTONOTE_SUBADDRESS_BASE58_PREFIX, addr);
  EXPECT_EQ(encoded.compare(0, 5, "funsa"), 0);
  EXPECT_EQ(encoded.size(), 101u);

  uint64_t prefix = 0;
  AccountPublicAddress parsed;
  ASSERT_TRUE(parseAccountAddressString(prefix, parsed, encoded));
  EXPECT_EQ(prefix, parameters::CRYPTONOTE_SUBADDRESS_BASE58_PREFIX);
  EXPECT_EQ(0, memcmp(&parsed.spendPublicKey, &addr.spendPublicKey, sizeof(PublicKey)));
  EXPECT_EQ(0, memcmp(&parsed.viewPublicKey, &addr.viewPublicKey, sizeof(PublicKey)));
}

TEST(SubaddressV2, TestnetTsubPrefix) {
  TestKeys k = makeTestKeys(6);
  SubAddressKeys sub = deriveSubAddressKeysV2(k.viewSecret, k.spendPublic, &k.spendSecret, 0, 1);
  AccountPublicAddress addr{sub.spendPublicKey, sub.viewPublicKey};
  std::string encoded = getAccountAddressAsStr(CRYPTONOTE_SUBADDRESS_BASE58_PREFIX_TESTNET, addr);
  EXPECT_EQ(encoded.compare(0, 4, "Tsub"), 0);
}

TEST(SubaddressV2, AdditionalPubKeysRoundTrip) {
  TestKeys k = makeTestKeys(7);
  std::vector<PublicKey> keys{k.spendPublic, k.viewPublic};
  std::vector<uint8_t> extra;
  ASSERT_TRUE(addAdditionalPubKeysToExtra(extra, keys));
  std::vector<PublicKey> parsed;
  ASSERT_TRUE(getAdditionalPubKeysFromExtra(extra, parsed));
  ASSERT_EQ(parsed.size(), 2u);
  EXPECT_EQ(0, memcmp(&parsed[0], &keys[0], sizeof(PublicKey)));
  EXPECT_EQ(0, memcmp(&parsed[1], &keys[1], sizeof(PublicKey)));
}

TEST(SubaddressV2, JanusCheckPositiveAndNegative) {
  TestKeys k = makeTestKeys(8);
  SubAddressKeys sub1 = deriveSubAddressKeysV2(k.viewSecret, k.spendPublic, &k.spendSecret, 0, 1);
  SubAddressKeys sub2 = deriveSubAddressKeysV2(k.viewSecret, k.spendPublic, &k.spendSecret, 0, 2);
  ASSERT_TRUE(sub1.hasSpendSecretKey);
  ASSERT_TRUE(sub2.hasSpendSecretKey);

  SecretKey r = makeScalar(9);
  PublicKey rG;
  ASSERT_TRUE(secret_key_to_public_key(r, rG));
  PublicKey R1;
  ASSERT_TRUE(subaddressTxPubKey(r, sub1.spendPublicKey, R1));

  EXPECT_TRUE(janusCheck(R1, rG, sub1.spendSecretKey));
  EXPECT_FALSE(janusCheck(R1, rG, sub2.spendSecretKey));
}

TEST(SubaddressV2, ExtraRejectedBeforeV11) {
  Transaction tx;
  tx.outputs.resize(1);
  PublicKey pk{};
  pk.data[0] = 1;
  ASSERT_TRUE(secret_key_to_public_key(makeScalar(10), pk));
  ASSERT_TRUE(addAdditionalPubKeysToExtra(tx.extra, {pk}));

  std::string err;
  EXPECT_FALSE(validateSubaddressTxExtra(tx, BLOCK_MAJOR_VERSION_10, &err));
  EXPECT_TRUE(validateSubaddressTxExtra(tx, BLOCK_MAJOR_VERSION_11, &err));
}

TEST(SubaddressV2, AdditionalCountMustMatchOutputs) {
  Transaction tx;
  tx.outputs.resize(2);
  PublicKey pk{};
  ASSERT_TRUE(secret_key_to_public_key(makeScalar(11), pk));
  ASSERT_TRUE(addAdditionalPubKeysToExtra(tx.extra, {pk}));

  std::string err;
  EXPECT_FALSE(validateSubaddressTxExtra(tx, BLOCK_MAJOR_VERSION_11, &err));
}

TEST(SubaddressV2, FunsaActivationHelper) {
  EXPECT_FALSE(funsaSubaddressActivated(BLOCK_MAJOR_VERSION_10));
  EXPECT_TRUE(funsaSubaddressActivated(BLOCK_MAJOR_VERSION_11));
  EXPECT_TRUE(funsaSubaddressActivated(BLOCK_MAJOR_VERSION_12));
}

TEST(SubaddressV2, SendScanSingleSubaddress) {
  TestKeys sender = makeTestKeys(12);
  TestKeys recv = makeTestKeys(13);
  SubAddressKeys sub = deriveSubAddressKeysV2(recv.viewSecret, recv.spendPublic, &recv.spendSecret, 0, 1);

  SecretKey r = makeScalar(14);
  PublicKey rG;
  ASSERT_TRUE(secret_key_to_public_key(r, rG));

  TransactionDestinationEntry dest(1000, AccountPublicAddress{sub.spendPublicKey, sub.viewPublicKey}, true);
  SubaddressOutputPlan plan;
  ASSERT_TRUE(planSubaddressOutputs(r, rG, sender.viewSecret, sender.spendPublic, {dest}, plan));
  EXPECT_TRUE(plan.singleSubaddressTx);
  EXPECT_TRUE(plan.hasJanusBase);

  PublicKey ephemeral;
  ASSERT_TRUE(derivePlannedOutputKey(dest, plan, sender.viewSecret, 0, ephemeral));

  KeyDerivation recvDer;
  ASSERT_TRUE(generate_key_derivation(plan.mainTxPubKey, recv.viewSecret, recvDer));
  PublicKey recovered;
  ASSERT_TRUE(underive_public_key(recvDer, 0, ephemeral, recovered));
  EXPECT_EQ(0, memcmp(&recovered, &sub.spendPublicKey, sizeof(PublicKey)));
  EXPECT_TRUE(janusCheck(plan.mainTxPubKey, plan.janusBase, sub.spendSecretKey));
}

TEST(SubaddressV2, SingleSubaddressWithChange) {
  TestKeys sender = makeTestKeys(15);
  TestKeys recv = makeTestKeys(16);
  SubAddressKeys sub = deriveSubAddressKeysV2(recv.viewSecret, recv.spendPublic, &recv.spendSecret, 0, 3);

  SecretKey r = makeScalar(17);
  PublicKey rG;
  ASSERT_TRUE(secret_key_to_public_key(r, rG));

  TransactionDestinationEntry subDest(1000, AccountPublicAddress{sub.spendPublicKey, sub.viewPublicKey}, true);
  TransactionDestinationEntry changeDest(500, AccountPublicAddress{sender.spendPublic, sender.viewPublic}, false);

  SubaddressOutputPlan plan;
  ASSERT_TRUE(planSubaddressOutputs(r, rG, sender.viewSecret, sender.spendPublic, {subDest, changeDest}, plan));
  EXPECT_TRUE(plan.singleSubaddressTx);
  EXPECT_TRUE(plan.hasJanusBase);
  EXPECT_NE(0, memcmp(&plan.mainTxPubKey, &rG, sizeof(PublicKey)));

  PublicKey subEph;
  ASSERT_TRUE(derivePlannedOutputKey(subDest, plan, sender.viewSecret, 0, subEph));
  PublicKey changeEph;
  ASSERT_TRUE(derivePlannedOutputKey(changeDest, plan, sender.viewSecret, 1, changeEph));

  KeyDerivation recvDer;
  ASSERT_TRUE(generate_key_derivation(plan.mainTxPubKey, recv.viewSecret, recvDer));
  PublicKey recoveredSub;
  ASSERT_TRUE(underive_public_key(recvDer, 0, subEph, recoveredSub));
  EXPECT_EQ(0, memcmp(&recoveredSub, &sub.spendPublicKey, sizeof(PublicKey)));

  KeyDerivation changeDer;
  ASSERT_TRUE(generate_key_derivation(plan.mainTxPubKey, sender.viewSecret, changeDer));
  PublicKey recoveredChange;
  ASSERT_TRUE(underive_public_key(changeDer, 1, changeEph, recoveredChange));
  EXPECT_EQ(0, memcmp(&recoveredChange, &sender.spendPublic, sizeof(PublicKey)));

  std::vector<uint8_t> extra;
  ASSERT_TRUE(applySubaddressTxExtra(extra, plan));
  PublicKey janus;
  EXPECT_TRUE(getJanusBaseFromExtra(extra, janus));
  EXPECT_EQ(0, memcmp(&janus, &rG, sizeof(PublicKey)));
  std::vector<PublicKey> additional;
  EXPECT_FALSE(getAdditionalPubKeysFromExtra(extra, additional));
}

TEST(SubaddressV2, MixedStandardAndSubaddress) {
  TestKeys sender = makeTestKeys(18);
  TestKeys recv = makeTestKeys(19);
  SubAddressKeys sub = deriveSubAddressKeysV2(recv.viewSecret, recv.spendPublic, &recv.spendSecret, 1, 4);

  SecretKey r = makeScalar(20);
  PublicKey rG;
  ASSERT_TRUE(secret_key_to_public_key(r, rG));

  TransactionDestinationEntry stdDest(700, AccountPublicAddress{recv.spendPublic, recv.viewPublic}, false);
  TransactionDestinationEntry subDest(1200, AccountPublicAddress{sub.spendPublicKey, sub.viewPublicKey}, true);

  SubaddressOutputPlan plan;
  ASSERT_TRUE(planSubaddressOutputs(r, rG, sender.viewSecret, sender.spendPublic, {stdDest, subDest}, plan));
  EXPECT_FALSE(plan.singleSubaddressTx);
  EXPECT_FALSE(plan.hasJanusBase);
  ASSERT_EQ(plan.additionalPubKeys.size(), 2u);

  PublicKey stdEph;
  ASSERT_TRUE(derivePlannedOutputKey(stdDest, plan, sender.viewSecret, 0, stdEph));
  PublicKey subEph;
  ASSERT_TRUE(derivePlannedOutputKey(subDest, plan, sender.viewSecret, 1, subEph));

  KeyDerivation stdDer;
  ASSERT_TRUE(generate_key_derivation(plan.additionalPubKeys[0], recv.viewSecret, stdDer));
  PublicKey recoveredStd;
  ASSERT_TRUE(underive_public_key(stdDer, 0, stdEph, recoveredStd));
  EXPECT_EQ(0, memcmp(&recoveredStd, &recv.spendPublic, sizeof(PublicKey)));

  KeyDerivation subDer;
  ASSERT_TRUE(generate_key_derivation(plan.additionalPubKeys[1], recv.viewSecret, subDer));
  PublicKey recoveredSub;
  ASSERT_TRUE(underive_public_key(subDer, 1, subEph, recoveredSub));
  EXPECT_EQ(0, memcmp(&recoveredSub, &sub.spendPublicKey, sizeof(PublicKey)));

  std::vector<uint8_t> extra;
  ASSERT_TRUE(applySubaddressTxExtra(extra, plan));
  std::vector<PublicKey> additional;
  ASSERT_TRUE(getAdditionalPubKeysFromExtra(extra, additional));
  ASSERT_EQ(additional.size(), 2u);
  PublicKey janus;
  EXPECT_FALSE(getJanusBaseFromExtra(extra, janus));
}
