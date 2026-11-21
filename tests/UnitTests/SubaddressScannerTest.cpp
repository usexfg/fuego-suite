// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// funsa V2 subaddress receive, Janus and fail-closed paths, driven through the
// production scanner (TransfersConsumer), TransactionImpl, Currency and the
// consensus extra validator.

#include <cstring>
#include <limits>
#include <unordered_set>
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
#include "CryptoNoteCore/TransactionPool.h"
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
  EXPECT_TRUE(validateSubaddressTxExtra(raw, BLOCK_MAJOR_VERSION_11, false, &error)) << error;

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
  EXPECT_FALSE(validateSubaddressTxExtra(tx, BLOCK_MAJOR_VERSION_11, false, &error));

  addJanusBasesToExtra(tx.extra, {randomPoint()});
  EXPECT_FALSE(validateSubaddressTxExtra(tx, BLOCK_MAJOR_VERSION_11, false, &error));

  tx.extra.clear();
  addAdditionalPubKeysToExtra(tx.extra, {randomPoint(), randomPoint()});
  addJanusBasesToExtra(tx.extra, {randomPoint(), randomPoint()});
  EXPECT_TRUE(validateSubaddressTxExtra(tx, BLOCK_MAJOR_VERSION_11, false, &error)) << error;
  EXPECT_FALSE(validateSubaddressTxExtra(tx, BLOCK_MAJOR_VERSION_10, false, &error));
}

TEST(SubaddressConsensus, SingleKeyTransactionCarriesExactlyOneJanusBase) {
  Transaction tx;
  tx.outputs.resize(2);
  std::string error;

  addJanusBasesToExtra(tx.extra, {randomPoint()});
  EXPECT_TRUE(validateSubaddressTxExtra(tx, BLOCK_MAJOR_VERSION_11, false, &error)) << error;

  tx.extra.clear();
  addJanusBasesToExtra(tx.extra, {randomPoint(), randomPoint()});
  EXPECT_FALSE(validateSubaddressTxExtra(tx, BLOCK_MAJOR_VERSION_11, false, &error));
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

Transaction keyInputTransaction(const std::vector<uint8_t>& extra) {
  Transaction tx;
  tx.version = TRANSACTION_VERSION_1;
  tx.unlockTime = 0;
  KeyInput input;
  input.amount = 1000;
  input.keyImage = reinterpret_cast<const KeyImage&>(static_cast<const PublicKey&>(randomPoint()));
  input.outputIndexes.push_back(0);
  tx.inputs.push_back(input);
  TransactionOutput output;
  output.amount = 900;
  output.target = KeyOutput{randomPoint()};
  tx.outputs.push_back(output);
  tx.extra = extra;
  tx.signatures.push_back(std::vector<Signature>(input.outputIndexes.size()));
  return tx;
}

std::vector<uint8_t> mainKeyOnlyExtra(const KeyPair& txKey) {
  std::vector<uint8_t> extra;
  addTransactionPublicKeyToExtra(extra, txKey.publicKey);
  return extra;
}

KeyPair freshKeyPair() {
  KeyPair keys;
  generate_keys(keys.publicKey, keys.secretKey);
  return keys;
}

Currency currencyWithV11At(Logging::ILogger& logger, uint32_t height) {
  return CurrencyBuilder(logger).upgradeHeightV11(height).upgradeHeightV12(height + 1000000).currency();
}

TEST(JanusBaseEverywhere, KeyInputTransactionNeedsJanusBaseOnceRequired) {
  const KeyPair txKey = freshKeyPair();
  Transaction bare = keyInputTransaction(mainKeyOnlyExtra(txKey));
  std::string error;
  EXPECT_TRUE(validateSubaddressTxExtra(bare, BLOCK_MAJOR_VERSION_11, false, &error)) << error;
  EXPECT_FALSE(validateSubaddressTxExtra(bare, BLOCK_MAJOR_VERSION_11, true, &error));

  Transaction sealed = bare;
  ASSERT_TRUE(addJanusFillerWhenMissing(sealed.extra, txKey.secretKey));
  EXPECT_TRUE(validateSubaddressTxExtra(sealed, BLOCK_MAJOR_VERSION_11, true, &error)) << error;
  EXPECT_FALSE(validateSubaddressTxExtra(sealed, BLOCK_MAJOR_VERSION_10, false, &error));
}

TEST(JanusBaseEverywhere, TransactionWithoutKeyInputsIsExempt) {
  Transaction coinbase;
  coinbase.inputs.push_back(BaseInput{1});
  addTransactionPublicKeyToExtra(coinbase.extra, randomPoint());
  std::string error;
  EXPECT_TRUE(validateSubaddressTxExtra(coinbase, BLOCK_MAJOR_VERSION_11, true, &error)) << error;

  Transaction multisigSpend;
  multisigSpend.inputs.push_back(MultisignatureInput{});
  multisigSpend.extra = {0xFF, 0x00};
  EXPECT_TRUE(validateSubaddressTxExtra(multisigSpend, BLOCK_MAJOR_VERSION_11, true, &error)) << error;
}

TEST(JanusBaseEverywhere, UnparseableExtraOnKeyInputTransactionIsRejectedOnceRequired) {
  Transaction tx = keyInputTransaction({0xFF, 0x00});
  std::string error;
  EXPECT_TRUE(validateSubaddressTxExtra(tx, BLOCK_MAJOR_VERSION_11, false, &error)) << error;
  EXPECT_FALSE(validateSubaddressTxExtra(tx, BLOCK_MAJOR_VERSION_11, true, &error));
  EXPECT_FALSE(addJanusFillerWhenMissing(tx.extra, freshKeyPair().secretKey));
}

TEST(JanusBaseEverywhere, RuleStartsAfterTheGraceWindow) {
  Logging::ConsoleLogger logger(Logging::ERROR);
  const Currency mainnet = CurrencyBuilder(logger).currency();
  const uint32_t lastOptional = parameters::UPGRADE_HEIGHT_V11 + parameters::JANUS_BASE_GRACE_BLOCKS;
  EXPECT_FALSE(mainnet.janusBaseRequiredAtHeight(parameters::UPGRADE_HEIGHT_V11 + 1));
  EXPECT_FALSE(mainnet.janusBaseRequiredAtHeight(lastOptional));
  EXPECT_TRUE(mainnet.janusBaseRequiredAtHeight(lastOptional + 1));

  const Currency unscheduled = currencyWithV11At(logger, std::numeric_limits<uint32_t>::max());
  EXPECT_FALSE(unscheduled.janusBaseRequiredAtHeight(std::numeric_limits<uint32_t>::max()));
}

TEST(JanusBaseEverywhere, FillerIsDeterministicAndDiffersFromTheMainKey) {
  const KeyPair txKey = freshKeyPair();
  PublicKey first;
  PublicKey second;
  ASSERT_TRUE(janusFillerForTxSecret(txKey.secretKey, first));
  ASSERT_TRUE(janusFillerForTxSecret(txKey.secretKey, second));
  EXPECT_EQ(first, second);
  EXPECT_NE(first, txKey.publicKey);
  EXPECT_TRUE(check_key(first));

  std::vector<uint8_t> extra = mainKeyOnlyExtra(txKey);
  ASSERT_TRUE(addJanusFillerWhenMissing(extra, txKey.secretKey));
  const std::vector<uint8_t> once = extra;
  ASSERT_TRUE(addJanusFillerWhenMissing(extra, freshKeyPair().secretKey));
  EXPECT_EQ(extra, once);
  const SubaddressTxKeys keys = readSubaddressTxKeys(extra);
  ASSERT_EQ(keys.janusBases.size(), 1u);
  EXPECT_EQ(keys.janusBases[0], first);
}

TEST(JanusBaseEverywhere, TransactionImplSealsFillerBeforeFirstSignature) {
  auto signedCopy = [](bool required) {
    auto tx = createTransaction();
    if (required) {
      tx->requireJanusBase();
    }
    tx->addInput(MultisignatureInput{});
    tx->addOutput(10, KeyOutput{randomPoint()});
    tx->signInputMultisignature(0, freshKeyPair());
    SecretKey txSecret;
    EXPECT_TRUE(tx->getTransactionSecretKey(txSecret));
    PublicKey expected;
    EXPECT_TRUE(janusFillerForTxSecret(txSecret, expected));
    return std::make_pair(readSubaddressTxKeys(tx->getExtra()), expected);
  };

  const auto sealed = signedCopy(true);
  ASSERT_EQ(sealed.first.janusBases.size(), 1u);
  EXPECT_EQ(sealed.first.janusBases[0], sealed.second);
  EXPECT_TRUE(sealed.first.additionalPubKeys.empty());
  EXPECT_TRUE(signedCopy(false).first.janusBases.empty());
}

TEST(JanusBaseEverywhere, PerOutputTransactionKeepsOneJanusBasePerOutputWhenSealed) {
  AccountBase standard;
  standard.generate();
  AccountBase subaddressOwner;
  subaddressOwner.generate();
  const SubAddressKeys sub = deriveSubAddressKeysV2(subaddressOwner.getAccountKeys().viewSecretKey,
                                                    subaddressOwner.getAccountKeys().address.spendPublicKey, nullptr, 1, 0);

  auto tx = createTransaction();
  tx->requireJanusBase();
  tx->addInput(MultisignatureInput{});
  tx->addOutput(10, standard.getAccountKeys().address, false);
  tx->addOutput(20, AccountPublicAddress{sub.spendPublicKey, sub.viewPublicKey}, true);
  tx->signInputMultisignature(0, freshKeyPair());

  Transaction raw;
  ASSERT_TRUE(fromBinaryArray(raw, tx->getTransactionData()));
  std::string error;
  EXPECT_TRUE(validateSubaddressTxExtra(raw, BLOCK_MAJOR_VERSION_11, true, &error)) << error;
  EXPECT_EQ(readSubaddressTxKeys(raw.extra).janusBases.size(), 2u);
}

TEST(JanusBaseEverywhere, OutgoingTransactionCarriesFillerOnlyOnceTheTipIsV11) {
  Logging::ConsoleLogger logger(Logging::ERROR);
  const Currency currency = currencyWithV11At(logger, 100);
  auto sealedJanusBases = [&currency](uint32_t tipHeight) {
    auto tx = createOutgoingTransaction(currency, tipHeight);
    tx->addInput(MultisignatureInput{});
    tx->signInputMultisignature(0, freshKeyPair());
    return readSubaddressTxKeys(tx->getExtra()).janusBases.size();
  };
  EXPECT_EQ(sealedJanusBases(100), 0u);
  EXPECT_EQ(sealedJanusBases(101), 1u);
}

TEST(JanusBaseEverywhere, StandardRecipientDecryptsMessageBesideTheFiller) {
  AccountBase recipient;
  recipient.generate();
  const KeyPair txKey = freshKeyPair();
  std::vector<uint8_t> extra = mainKeyOnlyExtra(txKey);
  ASSERT_TRUE(addJanusFillerWhenMissing(extra, txKey.secretKey));
  tx_extra_message message;
  ASSERT_TRUE(message.encrypt(0, "beside the filler", &recipient.getAccountKeys().address, txKey));
  ASSERT_TRUE(append_message_to_extra(extra, message));

  const SecretKey spendSecret = recipient.getAccountKeys().spendSecretKey;
  const auto asStandard = get_messages_from_extra(extra, messageTransactionKey(extra, txKey.publicKey, false), &spendSecret);
  ASSERT_EQ(asStandard.size(), 1u);
  EXPECT_EQ(asStandard[0], "beside the filler");
  EXPECT_TRUE(get_messages_from_extra(extra, messageTransactionKey(extra, txKey.publicKey, true), &spendSecret).empty());
}

TransactionSourceEntry ownedSource(const AccountKeys& owner, uint64_t amount) {
  const KeyPair previousTxKey = freshKeyPair();
  KeyDerivation derivation;
  PublicKey outputKey;
  EXPECT_TRUE(generate_key_derivation(previousTxKey.publicKey, owner.viewSecretKey, derivation));
  EXPECT_TRUE(derive_public_key(derivation, 0, owner.address.spendPublicKey, outputKey));
  TransactionSourceEntry source;
  source.outputs.push_back({0, outputKey});
  source.realOutput = 0;
  source.realTransactionPublicKey = previousTxKey.publicKey;
  source.realOutputIndexInTransaction = 0;
  source.amount = amount;
  return source;
}

TEST(JanusBaseEverywhere, ConstructTransactionAddsFillerOnlyWhenRequired) {
  Logging::ConsoleLogger logger(Logging::ERROR);
  AccountBase sender;
  sender.generate();
  AccountBase recipient;
  recipient.generate();
  const std::vector<TransactionDestinationEntry> destinations{{900, recipient.getAccountKeys().address}};

  auto build = [&](bool janusBaseRequired, Transaction& tx, SecretKey& txSecret) {
    return constructTransaction(sender.getAccountKeys(), {ownedSource(sender.getAccountKeys(), 1000)}, destinations,
                                {}, 0, {}, tx, 0, logger, txSecret, janusBaseRequired);
  };

  Transaction sealed;
  SecretKey sealedSecret;
  ASSERT_TRUE(build(true, sealed, sealedSecret));
  PublicKey expected;
  ASSERT_TRUE(janusFillerForTxSecret(sealedSecret, expected));
  const SubaddressTxKeys keys = readSubaddressTxKeys(sealed.extra);
  ASSERT_EQ(keys.janusBases.size(), 1u);
  EXPECT_EQ(keys.janusBases[0], expected);
  std::string error;
  EXPECT_TRUE(validateSubaddressTxExtra(sealed, BLOCK_MAJOR_VERSION_11, true, &error)) << error;

  Transaction bare;
  SecretKey bareSecret;
  ASSERT_TRUE(build(false, bare, bareSecret));
  EXPECT_TRUE(readSubaddressTxKeys(bare.extra).janusBases.empty());
  EXPECT_FALSE(validateSubaddressTxExtra(bare, BLOCK_MAJOR_VERSION_11, true, &error));
}

class AcceptingValidator : public ITransactionValidator {
public:
  bool checkTransactionInputs(const Transaction&, BlockInfo&) override { return true; }
  bool checkTransactionInputs(const Transaction&, BlockInfo&, BlockInfo&) override { return true; }
  bool haveSpentKeyImages(const Transaction&) override { return false; }
  bool checkTransactionSize(size_t) override { return true; }
  bool checkTransactionSettlement(const Transaction&, uint64_t& fee) override { fee = settledFee; return true; }
  uint64_t settledFee = 0;
};

class FixedTime : public ITimeProvider {
public:
  time_t now() override { return 1700000000; }
};

TEST(JanusBaseEverywhere, BlockTemplateDropsTransactionWithoutJanusBaseOnceRequired) {
  Logging::ConsoleLogger logger(Logging::ERROR);
  const uint32_t v11Height = 100;
  const Currency currency = currencyWithV11At(logger, v11Height);
  AcceptingValidator validator;
  validator.settledFee = currency.minimumFee();
  FixedTime clock;
  tx_memory_pool pool(currency, validator, clock, logger);
  ASSERT_TRUE(pool.init(::testing::TempDir() + "janus_pool_missing_dir"));

  const KeyPair bareKey = freshKeyPair();
  const Transaction bare = keyInputTransaction(mainKeyOnlyExtra(bareKey));
  const KeyPair sealedKey = freshKeyPair();
  Transaction sealed = keyInputTransaction(mainKeyOnlyExtra(sealedKey));
  ASSERT_TRUE(addJanusFillerWhenMissing(sealed.extra, sealedKey.secretKey));
  for (const Transaction& tx : {bare, sealed}) {
    tx_verification_context tvc = boost::value_initialized<tx_verification_context>();
    ASSERT_TRUE(pool.add_tx(tx, tvc, false, v11Height + 1));
  }

  auto templateAt = [&pool](uint32_t height) {
    Block block;
    block.majorVersion = BLOCK_MAJOR_VERSION_11;
    size_t totalSize = 0;
    uint64_t fee = 0;
    EXPECT_TRUE(pool.fill_block_template(block, 1000000, 1000000, 0, totalSize, fee, height));
    return std::unordered_set<Hash>(block.transactionHashes.begin(), block.transactionHashes.end());
  };
  const uint32_t lastOptional = v11Height + parameters::JANUS_BASE_GRACE_BLOCKS;
  EXPECT_EQ(templateAt(lastOptional), (std::unordered_set<Hash>{getObjectHash(bare), getObjectHash(sealed)}));
  EXPECT_EQ(templateAt(lastOptional + 1), (std::unordered_set<Hash>{getObjectHash(sealed)}));
}

} // namespace
