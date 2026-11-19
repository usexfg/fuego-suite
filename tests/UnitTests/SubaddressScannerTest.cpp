// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// funsa V2 subaddress receive, Janus and fail-closed paths, driven through the
// production scanner (TransfersConsumer), TransactionImpl, Currency and the
// consensus extra validator.

#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"

#include "CryptoNoteConfig.h"
#include "CryptoNoteCore/Account.h"
#include "CryptoNoteCore/CryptoNoteBasicImpl.h"
#include "CryptoNoteCore/CryptoNoteFormatUtils.h"
#include "CryptoNoteCore/CryptoNoteTools.h"
#include "CryptoNoteCore/Currency.h"
#include "CryptoNoteCore/SubaddressTx.h"
#include "CryptoNoteCore/SubaddressTxProof.h"
#include "Common/Base58.h"
#include "CryptoNoteCore/TransactionApi.h"
#include "CryptoNoteCore/TransactionExtra.h"
#include "INode.h"
#include "IWallet.h"
#include "ITransfersContainer.h"
#include "Logging/ConsoleLogger.h"
#include "Transfers/TransfersConsumer.h"
#include "Transfers/TransfersContainer.h"
#include "crypto/crypto.h"
#include "crypto/subaddress.h"

using namespace CryptoNote;
using namespace Crypto;

namespace {

class NullNode : public INode {
public:
  bool addObserver(INodeObserver*) override { return true; }
  bool removeObserver(INodeObserver*) override { return true; }
  void init(const Callback& cb) override { cb(std::error_code()); }
  bool shutdown() override { return true; }
  size_t getPeerCount() const override { return 0; }
  uint32_t getLastLocalBlockHeight() const override { return 0; }
  uint32_t getLastKnownBlockHeight() const override { return 0; }
  uint32_t getLocalBlockCount() const override { return 0; }
  uint32_t getKnownBlockCount() const override { return 0; }
  uint64_t getLastLocalBlockTimestamp() const override { return 0; }
  void relayTransaction(const Transaction&, const Callback& cb) override { cb(std::error_code()); }
  void getRandomOutsByAmounts(std::vector<uint64_t>&&, uint64_t, std::vector<COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::outs_for_amount>&, const Callback& cb) override { cb(std::error_code()); }
  void getRandomCommitmentOutsForAmount(uint64_t, uint64_t, uint32_t, uint32_t, std::vector<COMMAND_RPC_GET_RANDOM_COMMITMENT_OUTPUTS::out_entry>&, const Callback& cb) override { cb(std::error_code()); }
  void getNewBlocks(std::vector<Hash>&&, std::vector<block_complete_entry>&, uint32_t&, const Callback& cb) override { cb(std::error_code()); }
  void getTransactionOutsGlobalIndices(const Hash&, std::vector<uint32_t>&, const Callback& cb) override { cb(std::error_code()); }
  void queryBlocks(std::vector<Hash>&&, uint64_t, std::vector<BlockShortEntry>&, uint32_t&, const Callback& cb) override { cb(std::error_code()); }
  void getPoolSymmetricDifference(std::vector<Hash>&&, Hash, bool& actual, std::vector<std::unique_ptr<ITransactionReader>>&, std::vector<Hash>&, const Callback& cb) override { actual = true; cb(std::error_code()); }
  void getMultisignatureOutputByGlobalIndex(uint64_t, uint32_t, MultisignatureOutput&, const Callback& cb) override { cb(std::error_code()); }
  void getTransaction(const Hash&, Transaction&, const Callback& cb) override { cb(std::error_code()); }
  void getBlocks(const std::vector<uint32_t>&, std::vector<std::vector<BlockDetails>>&, const Callback& cb) override { cb(std::error_code()); }
  void getBlocks(const std::vector<Hash>&, std::vector<BlockDetails>&, const Callback& cb) override { cb(std::error_code()); }
  void getBlocks(uint64_t, uint64_t, uint32_t, std::vector<BlockDetails>&, uint32_t&, const Callback& cb) override { cb(std::error_code()); }
  void getTransactions(const std::vector<Hash>&, std::vector<TransactionDetails>&, const Callback& cb) override { cb(std::error_code()); }
  void getTransactionsByPaymentId(const Hash&, std::vector<TransactionDetails>&, const Callback& cb) override { cb(std::error_code()); }
  void getPoolTransactions(uint64_t, uint64_t, uint32_t, std::vector<TransactionDetails>&, uint64_t&, const Callback& cb) override { cb(std::error_code()); }
  void isSynchronized(bool& s, const Callback& cb) override { s = true; cb(std::error_code()); }
};

struct Wallet {
  AccountKeys master;
  SubAddressKeys sub1;
  SubAddressKeys sub2;

  AccountPublicAddress publishedSub1() const { return {sub1.spendPublicKey, sub1.viewPublicKey}; }
  AccountPublicAddress publishedSub2() const { return {sub2.spendPublicKey, sub2.viewPublicKey}; }
};

Wallet makeWallet() {
  AccountBase account;
  account.generate();
  Wallet w;
  w.master = account.getAccountKeys();
  w.sub1 = deriveSubAddressKeysV2(w.master.viewSecretKey, w.master.address.spendPublicKey, &w.master.spendSecretKey, 1, 0);
  w.sub2 = deriveSubAddressKeysV2(w.master.viewSecretKey, w.master.address.spendPublicKey, &w.master.spendSecretKey, 1, 1);
  return w;
}

AccountSubscription masterSubscription(const Wallet& w) {
  AccountSubscription s{};
  s.keys = w.master;
  s.syncStart = {0, 0};
  s.transactionSpendableAge = 1;
  return s;
}

AccountSubscription v2Subscription(const Wallet& w, const SubAddressKeys& sub, bool viewOnly = false) {
  AccountSubscription s = masterSubscription(w);
  s.keys.address.spendPublicKey = sub.spendPublicKey;
  s.keys.spendSecretKey = viewOnly ? NULL_SECRET_KEY : sub.spendSecretKey;
  s.janusCheckRequired = true;
  return s;
}

PublicKey randomPoint() {
  PublicKey p;
  SecretKey s;
  generate_keys(p, s);
  return p;
}

Transaction txWithKeyOutputs(const std::vector<PublicKey>& outputKeys, const std::vector<uint8_t>& extra) {
  Transaction tx;
  tx.version = TRANSACTION_VERSION_1;
  tx.unlockTime = 0;
  KeyInput in;
  in.amount = 1000 * outputKeys.size();
  in.outputIndexes = {0};
  const PublicKey imageSource = randomPoint();
  memcpy(&in.keyImage, &imageSource, sizeof(KeyImage));
  tx.inputs.push_back(in);
  for (const auto& key : outputKeys) {
    KeyOutput out;
    out.key = key;
    tx.outputs.push_back(TransactionOutput{1000, out});
  }
  tx.extra = extra;
  return tx;
}

class SubaddressScanner : public ::testing::Test {
protected:
  SubaddressScanner() : logger(Logging::ERROR), currency(CurrencyBuilder(logger).currency()) {}

  std::vector<TransactionOutputInformation> credited(const Wallet& w, const std::vector<AccountSubscription>& subs,
                                                     const Transaction& tx, size_t subscriptionIndex) {
    TransfersConsumer consumer(currency, node, logger, w.master.viewSecretKey);
    std::vector<ITransfersSubscription*> handles;
    for (const auto& s : subs) {
      handles.push_back(&consumer.addSubscription(s));
    }
    auto reader = createTransactionPrefix(tx);
    EXPECT_FALSE(consumer.addUnconfirmedTransaction(*reader));
    std::vector<TransactionOutputInformation> outs;
    handles[subscriptionIndex]->getContainer().getOutputs(outs, ITransfersContainer::IncludeAll);
    return outs;
  }

  Transaction janusProbe(const Wallet& victim, bool withJanusList, bool viaAdditionalKey) {
    SecretKey r;
    PublicKey rG;
    generate_keys(rG, r);
    PublicKey keyedToSub2;
    EXPECT_TRUE(subaddressTxPubKey(r, victim.sub2.spendPublicKey, keyedToSub2));
    KeyDerivation shared;
    EXPECT_TRUE(generate_key_derivation(victim.sub2.viewPublicKey, r, shared));
    PublicKey payingSub1;
    EXPECT_TRUE(derive_public_key(shared, 0, victim.sub1.spendPublicKey, payingSub1));

    std::vector<uint8_t> extra;
    if (viaAdditionalKey) {
      addTransactionPublicKeyToExtra(extra, randomPoint());
      addAdditionalPubKeysToExtra(extra, {keyedToSub2});
    } else {
      addTransactionPublicKeyToExtra(extra, keyedToSub2);
    }
    if (withJanusList) {
      addJanusBasesToExtra(extra, {rG});
    }
    return txWithKeyOutputs({payingSub1}, extra);
  }

  Logging::ConsoleLogger logger;
  Currency currency;
  NullNode node;
};

TEST_F(SubaddressScanner, HonestSingleSubaddressPaymentIsCreditedAndVerified) {
  Wallet sender = makeWallet();
  Wallet recipient = makeWallet();
  SecretKey r;
  PublicKey rG;
  generate_keys(rG, r);
  TransactionDestinationEntry dest(1000, recipient.publishedSub1(), true);

  SubaddressOutputPlan plan;
  ASSERT_TRUE(planSubaddressOutputs(r, rG, sender.master.viewSecretKey, sender.master.address.spendPublicKey, {dest}, plan));
  PublicKey outputKey;
  ASSERT_TRUE(derivePlannedOutputKey(dest, plan, 0, outputKey));
  std::vector<uint8_t> extra;
  addTransactionPublicKeyToExtra(extra, rG);
  ASSERT_TRUE(applySubaddressTxExtra(extra, plan));

  auto outs = credited(recipient, {v2Subscription(recipient, recipient.sub1)}, txWithKeyOutputs({outputKey}, extra), 0);
  ASSERT_EQ(outs.size(), 1u);
  EXPECT_FALSE(outs[0].janusUnverified);
  EXPECT_EQ(outs[0].transactionPublicKey, plan.mainTxPubKey);
}

TEST_F(SubaddressScanner, JanusProbeWithoutJanusBaseIsNotCredited) {
  Wallet victim = makeWallet();
  auto outs = credited(victim, {v2Subscription(victim, victim.sub1), v2Subscription(victim, victim.sub2)},
                       janusProbe(victim, false, false), 0);
  EXPECT_TRUE(outs.empty());
}

TEST_F(SubaddressScanner, JanusProbeWithForgedJanusBaseIsNotCredited) {
  Wallet victim = makeWallet();
  auto outs = credited(victim, {v2Subscription(victim, victim.sub1), v2Subscription(victim, victim.sub2)},
                       janusProbe(victim, true, false), 0);
  EXPECT_TRUE(outs.empty());
}

TEST_F(SubaddressScanner, JanusProbeThroughAdditionalKeyIsNotCredited) {
  Wallet victim = makeWallet();
  auto outs = credited(victim, {v2Subscription(victim, victim.sub1), v2Subscription(victim, victim.sub2)},
                       janusProbe(victim, true, true), 0);
  EXPECT_TRUE(outs.empty());
}

TEST_F(SubaddressScanner, ViewOnlyWalletCreditsUnverifiableOutputFlagged) {
  Wallet victim = makeWallet();
  auto outs = credited(victim, {v2Subscription(victim, victim.sub1, true)}, janusProbe(victim, false, false), 0);
  ASSERT_EQ(outs.size(), 1u);
  EXPECT_TRUE(outs[0].janusUnverified);
}

TEST_F(SubaddressScanner, PerOutputKeysFromTransactionImplCreditBothRecipients) {
  Wallet recipient = makeWallet();
  auto tx = createTransaction();
  tx->addOutput(700, recipient.master.address, false);
  tx->addOutput(1200, recipient.publishedSub1(), true);
  Transaction raw;
  ASSERT_TRUE(fromBinaryArray(raw, tx->getTransactionData()));

  std::string error;
  EXPECT_TRUE(validateSubaddressTxExtra(raw, BLOCK_MAJOR_VERSION_11, &error)) << error;

  const SubaddressTxKeys keys = readSubaddressTxKeys(raw.extra);
  ASSERT_EQ(keys.additionalPubKeys.size(), 2u);
  ASSERT_EQ(keys.janusBases.size(), 2u);
  const PublicKey mainTxPublicKey = getTransactionPublicKeyFromExtra(raw.extra);
  EXPECT_NE(keys.additionalPubKeys[0], mainTxPublicKey);
  EXPECT_NE(keys.additionalPubKeys[0], keys.janusBases[0]);

  const std::vector<AccountSubscription> subs{masterSubscription(recipient), v2Subscription(recipient, recipient.sub1)};
  auto standardOuts = credited(recipient, subs, raw, 0);
  auto subaddressOuts = credited(recipient, subs, raw, 1);
  ASSERT_EQ(standardOuts.size(), 1u);
  ASSERT_EQ(subaddressOuts.size(), 1u);
  EXPECT_FALSE(subaddressOuts[0].janusUnverified);
  EXPECT_EQ(subaddressOuts[0].transactionPublicKey, keys.additionalPubKeys[1]);
}

TEST_F(SubaddressScanner, PerOutputSecretsAreDeterministicFromTheTxSecret) {
  Wallet recipient = makeWallet();
  auto tx = createTransaction();
  tx->addOutput(1200, recipient.publishedSub1(), true);
  SecretKey txSecret;
  ASSERT_TRUE(tx->getTransactionSecretKey(txSecret));
  Transaction raw;
  ASSERT_TRUE(fromBinaryArray(raw, tx->getTransactionData()));

  PublicKey expected;
  ASSERT_TRUE(subaddressTxPubKey(additionalTxSecret(txSecret, 0), recipient.sub1.spendPublicKey, expected));
  EXPECT_EQ(readSubaddressTxKeys(raw.extra).additionalPubKeys.at(0), expected);
  EXPECT_NE(additionalTxSecret(txSecret, 0), additionalTxSecret(txSecret, 1));
}

TEST_F(SubaddressScanner, StandardOutputCannotFollowSubaddressOutputWithoutItsOwnKey) {
  Wallet recipient = makeWallet();
  auto tx = createTransaction();
  tx->addOutput(1200, recipient.publishedSub1(), true);
  KeyOutput raw;
  raw.key = randomPoint();
  EXPECT_THROW(tx->addOutput(100, raw), std::logic_error);
}

TEST_F(SubaddressScanner, DecoyAdditionalKeysCannotRedirectTheRecordedTxKey) {
  Wallet victim = makeWallet();
  SecretKey r;
  PublicKey mainR;
  generate_keys(mainR, r);
  KeyDerivation shared;
  ASSERT_TRUE(generate_key_derivation(victim.master.address.viewPublicKey, r, shared));
  PublicKey outputKey;
  ASSERT_TRUE(derive_public_key(shared, 0, victim.master.address.spendPublicKey, outputKey));

  std::vector<uint8_t> extra;
  addTransactionPublicKeyToExtra(extra, mainR);
  addAdditionalPubKeysToExtra(extra, {randomPoint()});
  addJanusBasesToExtra(extra, {randomPoint()});

  auto outs = credited(victim, {masterSubscription(victim)}, txWithKeyOutputs({outputKey}, extra), 0);
  ASSERT_EQ(outs.size(), 1u);
  EXPECT_EQ(outs[0].transactionPublicKey, mainR);

  KeyPair ephemeral;
  KeyImage image;
  ASSERT_TRUE(generate_key_image_helper(victim.master, outs[0].transactionPublicKey, 0, ephemeral, image));
  EXPECT_EQ(ephemeral.publicKey, outs[0].outputKey);
}

TEST_F(SubaddressScanner, OwnSubaddressOutputWithoutSubaddressDerivationIsNotCredited) {
  Wallet owner = makeWallet();
  auto tx = createTransaction();
  tx->addOutput(1000, owner.publishedSub1(), false);
  Transaction raw;
  ASSERT_TRUE(fromBinaryArray(raw, tx->getTransactionData()));
  EXPECT_TRUE(credited(owner, {v2Subscription(owner, owner.sub1)}, raw, 0).empty());
}

TEST_F(SubaddressScanner, MiningAndOtherStandardOnlyPathsRefuseFunsa) {
  Wallet miner = makeWallet();
  const std::string funsa = currency.subAddressAsString(miner.publishedSub1());
  ASSERT_EQ(funsa.compare(0, 5, "funsa"), 0);

  AccountPublicAddress parsed;
  EXPECT_FALSE(currency.parseAccountAddressString(funsa, parsed));
  bool isSubaddress = false;
  EXPECT_TRUE(currency.parseAccountAddressString(funsa, parsed, isSubaddress));
  EXPECT_TRUE(isSubaddress);
  EXPECT_TRUE(currency.parseAccountAddressString(currency.accountAddressAsString(miner.master.address), parsed));
}

TEST(SubaddressConsensus, AdditionalKeysNeedOneJanusBasePerOutput) {
  Transaction tx;
  tx.outputs.resize(2);
  std::string error;

  addAdditionalPubKeysToExtra(tx.extra, {randomPoint(), randomPoint()});
  EXPECT_FALSE(validateSubaddressTxExtra(tx, BLOCK_MAJOR_VERSION_11, &error));

  addJanusBasesToExtra(tx.extra, {randomPoint()});
  EXPECT_FALSE(validateSubaddressTxExtra(tx, BLOCK_MAJOR_VERSION_11, &error));

  tx.extra.clear();
  addAdditionalPubKeysToExtra(tx.extra, {randomPoint(), randomPoint()});
  addJanusBasesToExtra(tx.extra, {randomPoint(), randomPoint()});
  EXPECT_TRUE(validateSubaddressTxExtra(tx, BLOCK_MAJOR_VERSION_11, &error)) << error;
  EXPECT_FALSE(validateSubaddressTxExtra(tx, BLOCK_MAJOR_VERSION_10, &error));
}

TEST(SubaddressConsensus, SingleKeyTransactionCarriesExactlyOneJanusBase) {
  Transaction tx;
  tx.outputs.resize(2);
  std::string error;

  addJanusBasesToExtra(tx.extra, {randomPoint()});
  EXPECT_TRUE(validateSubaddressTxExtra(tx, BLOCK_MAJOR_VERSION_11, &error)) << error;

  tx.extra.clear();
  addJanusBasesToExtra(tx.extra, {randomPoint(), randomPoint()});
  EXPECT_FALSE(validateSubaddressTxExtra(tx, BLOCK_MAJOR_VERSION_11, &error));
}

TEST(SubaddressContainer, JanusFlagSurvivesSaveAndLoad) {
  Logging::ConsoleLogger logger(Logging::ERROR);
  Currency currency = CurrencyBuilder(logger).currency();
  Wallet w = makeWallet();

  auto tx = createTransaction();
  tx->addOutput(1000, w.publishedSub1(), true);
  Transaction raw;
  ASSERT_TRUE(fromBinaryArray(raw, tx->getTransactionData()));
  auto reader = createTransactionPrefix(raw);

  TransactionOutputInformationIn output{};
  output.type = TransactionTypes::OutputType::Key;
  output.amount = 1000;
  output.outputInTransaction = 0;
  output.globalOutputIndex = UNCONFIRMED_TRANSACTION_GLOBAL_OUTPUT_INDEX;
  output.transactionPublicKey = readSubaddressTxKeys(raw.extra).additionalPubKeys.at(0);
  output.outputKey = boost::get<KeyOutput>(raw.outputs[0].target).key;
  output.janusUnverified = true;

  TransfersContainer container(currency, 1);
  TransactionBlockInfo block{WALLET_UNCONFIRMED_TRANSACTION_HEIGHT, 0, 0};
  ASSERT_TRUE(container.addTransaction(block, *reader, {output}, {}));

  std::stringstream stored;
  container.save(stored);
  TransfersContainer restored(currency, 1);
  restored.load(stored);

  std::vector<TransactionOutputInformation> outs;
  restored.getOutputs(outs, ITransfersContainer::IncludeAll);
  ASSERT_EQ(outs.size(), 1u);
  EXPECT_TRUE(outs[0].janusUnverified);
}

TEST(SubaddressProof, PerOutputPaymentProvesForSubaddressAndStandardRecipient) {
  Wallet recipient = makeWallet();
  Wallet other = makeWallet();
  auto tx = createTransaction();
  tx->addOutput(700, recipient.master.address, false);
  tx->addOutput(1200, recipient.publishedSub1(), true);
  SecretKey txSecret;
  ASSERT_TRUE(tx->getTransactionSecretKey(txSecret));
  Transaction raw;
  ASSERT_TRUE(fromBinaryArray(raw, tx->getTransactionData()));
  const Hash txid = getObjectHash(raw);

  std::string subProof;
  ASSERT_TRUE(generatePaymentProof(raw, txid, recipient.publishedSub1(), true, txSecret, subProof));
  PaymentProofResult proven;
  ASSERT_TRUE(checkPaymentProof(raw, txid, recipient.publishedSub1(), true, subProof, proven));
  EXPECT_EQ(proven.receivedAmount, 1200u);

  std::string standardProof;
  ASSERT_TRUE(generatePaymentProof(raw, txid, recipient.master.address, false, txSecret, standardProof));
  ASSERT_TRUE(checkPaymentProof(raw, txid, recipient.master.address, false, standardProof, proven));
  EXPECT_EQ(proven.receivedAmount, 700u);

  EXPECT_FALSE(checkPaymentProof(raw, txid, other.publishedSub1(), true, subProof, proven));
  std::string unrelated;
  EXPECT_FALSE(generatePaymentProof(raw, txid, other.publishedSub1(), true, txSecret, unrelated));
}

TEST(SubaddressProof, SingleKeySubaddressPaymentProvesAndTamperingFails) {
  Wallet sender = makeWallet();
  Wallet recipient = makeWallet();
  SecretKey r;
  PublicKey rG;
  generate_keys(rG, r);
  TransactionDestinationEntry dest(1000, recipient.publishedSub1(), true);
  SubaddressOutputPlan plan;
  ASSERT_TRUE(planSubaddressOutputs(r, rG, sender.master.viewSecretKey, sender.master.address.spendPublicKey, {dest}, plan));
  PublicKey outputKey;
  ASSERT_TRUE(derivePlannedOutputKey(dest, plan, 0, outputKey));
  std::vector<uint8_t> extra;
  addTransactionPublicKeyToExtra(extra, rG);
  ASSERT_TRUE(applySubaddressTxExtra(extra, plan));
  Transaction raw = txWithKeyOutputs({outputKey}, extra);
  const Hash txid = getObjectHash(raw);

  std::string proof;
  ASSERT_TRUE(generatePaymentProof(raw, txid, recipient.publishedSub1(), true, r, proof));
  PaymentProofResult proven;
  ASSERT_TRUE(checkPaymentProof(raw, txid, recipient.publishedSub1(), true, proof, proven));
  EXPECT_EQ(proven.receivedAmount, 1000u);

  std::string tampered = proof;
  tampered[tampered.size() - 3] = tampered[tampered.size() - 3] == '2' ? '3' : '2';
  EXPECT_FALSE(checkPaymentProof(raw, txid, recipient.publishedSub1(), true, tampered, proven));
  EXPECT_FALSE(checkPaymentProof(raw, txid, recipient.publishedSub1(), false, proof, proven));
}

TEST(SubaddressProof, OversizedProofIsRejectedBeforeVerification) {
  Wallet recipient = makeWallet();
  Transaction raw = txWithKeyOutputs({randomPoint()}, {});
  const size_t itemSize = 1 + sizeof(PublicKey) + sizeof(Signature);
  const std::string oversized = "ProofV2" + Tools::Base58::encode(std::string(257 * itemSize, '\x01'));
  PaymentProofResult proven;
  EXPECT_FALSE(checkPaymentProof(raw, getObjectHash(raw), recipient.publishedSub1(), true, oversized, proven));
}

} // namespace
