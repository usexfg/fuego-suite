// Copyright (c) 2011-2016 The Cryptonote developers
// Copyright (c) 2014-2016 SDN developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "Deposit.h"

#include <CryptoNoteConfig.h>
#include <CryptoNoteCore/TransactionApi.h>
#include <ITransaction.h>
#include "TransactionBuilder.h"
#include <cstring>

namespace DepositTests {

using namespace CryptoNote;

bool DepositTestsBase::check_emission(core& c, size_t ev_index, const std::vector<test_event_entry>& events) {
  emission = c.getTotalGeneratedAmount();
  return true;
}

bool DepositTestsBase::mark_invalid_tx(core& /*c*/, std::size_t ev_index, const std::vector<test_event_entry>& /*events*/) {
  blockId = ev_index + 1;
  return true;
}

TransactionBuilder::MultisignatureSource DepositTestsBase::createSource(uint32_t term, KeyPair key) const {
  TransactionBuilder::MultisignatureSource src;

  src.input.amount = m_currency.depositMinAmount();
  src.input.outputIndex = 0;
  src.input.signatureCount = 1;
  src.input.term = term;

  src.keys.push_back(from.getAccountKeys());
  src.srcTxPubKey = key.publicKey;
  src.srcOutputIndex = 0;

  return src;
}

bool DepositTestsBase::check_tx_verification_context(const tx_verification_context& tvc, bool tx_added, std::size_t event_idx, const Transaction& /*tx*/) {
  if (blockId == event_idx)
    return tvc.m_verification_failed;
  else
    return !tvc.m_verification_failed && tx_added;
}

void DepositTestsBase::addDepositOutput(Transaction& transaction) {
  auto lastOp = transaction.outputs.back();
  auto out = boost::get<KeyOutput>(lastOp.target);
  transaction.outputs.pop_back();
  transaction.outputs.push_back({m_currency.depositMinAmount(), MultisignatureOutput{{out.key}, 1, m_currency.depositMinTerm() + 1}});
}

Transaction DepositTestsBase::createDepositTransaction(std::vector<test_event_entry>& events) {
  TestGenerator generator(m_currency, events);
  auto builder = generator.createTxBuilder(generator.minerAccount, from, 100, m_currency.minimumFee() + 1, minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION));
  //builder.
  builder.m_destinations.clear();

  TransactionBuilder::KeysVector kv;
  kv.push_back(to.getAccountKeys());

  builder.addMultisignatureOut(1000, kv, 1);
  auto tx = builder.build();
  generator.addEvent(tx);
  return tx;
}

void DepositTestsBase::addDepositInput(Transaction& transaction) {
  auto lastOp = transaction.inputs.back();
  auto in = boost::get<KeyInput>(lastOp);
  transaction.inputs.pop_back();
  transaction.inputs.push_back(MultisignatureInput{m_currency.depositMinAmount(), 1, 1, m_currency.depositMinTerm() + 1});
/*  transaction.signatures.clear();

  auto trans = createTransaction(transaction);
  for (auto i = 0; i < transaction.vin.size(); ++i) {
    trans->signInputMultisignature(
        i, reinterpret_cast<const PublicKey&>(to.getAccountKeys().m_account_address.m_viewPublicKey), 1,
        reinterpret_cast<const AccountKeys&>(to.getAccountKeys()));
  }
 */
}

// Fuego admits version-2 txs (multisig / legacy deposit outputs) only in
// blocks >= v8; upstream gated them at v2. Upstream's version of this test
// pushed an empty v1 block that failed on its block version alone.
bool BlocksOfFirstTypeCantHaveTransactionsOfTypeTwo::generate(std::vector<test_event_entry>& events) {
  const uint8_t lastVersionWithoutTxV2 = BLOCK_MAJOR_VERSION_7;
  TestGenerator generator(m_currency, events);
  generator.generator.defaultMajorVersion = lastVersionWithoutTxV2;
  const size_t decoys = minRingDecoys(m_currency, lastVersionWithoutTxV2);
  generator.generateBlocks(m_currency.minedMoneyUnlockWindow() + decoys, lastVersionWithoutTxV2);

  auto builder = generator.createTxBuilder(generator.minerAccount, from, m_currency.depositMinAmount() + 100,
    m_currency.minimumFee(), decoys);
  builder.m_destinations.clear();

  TransactionBuilder::KeysVector kv;
  kv.push_back(to.getAccountKeys());

  builder.addMultisignatureOut(m_currency.depositMinAmount(), kv, 1, m_currency.depositMinTerm());
  builder.setVersion(TRANSACTION_VERSION_2);
  auto tx = builder.build();
  generator.addEvent(tx); // the pool admits it; the v7 block carrying it must fail
  generator.addCallback("mark_invalid_block");
  generator.makeNextBlock(tx);
  return true;
}

bool BlocksOfSecondTypeCanHaveTransactionsOfTypeOne::generate(std::vector<test_event_entry>& events) {
  TestGenerator generator(m_currency, events);
  generator.generator.defaultMajorVersion = TX_V2_MIN_BLOCK_VERSION;
  const size_t decoys = minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION);
  generator.generateBlocks(m_currency.minedMoneyUnlockWindow() + decoys, TX_V2_MIN_BLOCK_VERSION);

  auto builder = generator.createTxBuilder(generator.minerAccount, to, MK_COINS(1), m_currency.minimumFee(), decoys);
  builder.setVersion(TRANSACTION_VERSION_1);
  auto tx = builder.build();
  generator.addEvent(tx);
  generator.makeNextBlock(tx);
  return true;
}

bool BlocksOfSecondTypeCanHaveTransactionsOfTypeTwo::generate(std::vector<test_event_entry>& events) {
  TestGenerator generator(m_currency, events);
  generator.generator.defaultMajorVersion = TX_V2_MIN_BLOCK_VERSION;
  generator.generateBlocks(m_currency.minedMoneyUnlockWindow() + minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION), TX_V2_MIN_BLOCK_VERSION);
  auto builder = generator.createTxBuilder(generator.minerAccount, from, 100, m_currency.minimumFee() + 1, minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION));
  builder.m_destinations.clear();

  TransactionBuilder::KeysVector kv;
  kv.push_back(to.getAccountKeys());

  builder.addMultisignatureOut(m_currency.depositMinAmount() + 1, kv, 1, m_currency.depositMinTerm() + 1);
  auto tx = builder.build();
  generator.addEvent(tx);
  generator.makeNextBlock(tx);
  return true;
}

bool TransactionOfTypeOneWithDepositInputIsRejected::generate(std::vector<test_event_entry>& events) {
  TestGenerator generator(m_currency, events);
  generator.generator.defaultMajorVersion = TX_V2_MIN_BLOCK_VERSION;
  generator.generateBlocks(m_currency.minedMoneyUnlockWindow() + minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION), TX_V2_MIN_BLOCK_VERSION);
  KeyPair key;
  {
    auto builder = generator.createTxBuilder(generator.minerAccount, from, 100, m_currency.minimumFee() + 1, minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION));
    builder.m_destinations.clear();

    TransactionBuilder::KeysVector kv;
    kv.push_back(to.getAccountKeys());

    builder.addMultisignatureOut(m_currency.depositMinAmount(), kv, 1, m_currency.depositMinTerm() + 1);
    key = builder.getTxKeys();
    auto tx = builder.build();
    generator.addEvent(tx);
    generator.makeNextBlock(tx);
  }
  {
    TransactionBuilder builder(m_currency);
    builder.addMultisignatureInput(createSource(m_currency.depositMinTerm(), key));
    builder.setVersion(TRANSACTION_VERSION_1);
    auto tx = builder.build();
    generator.addCallback("mark_invalid_tx"); // should be rejected by the core
    generator.addEvent(tx);
  }
  return true;
}

bool TransactionOfTypeOneWithDepositOutputIsRejected::generate(std::vector<test_event_entry>& events) {
  TestGenerator generator(m_currency, events);
  generator.generator.defaultMajorVersion = TX_V2_MIN_BLOCK_VERSION;
  generator.generateBlocks(m_currency.minedMoneyUnlockWindow() + minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION), TX_V2_MIN_BLOCK_VERSION);
  auto builder = generator.createTxBuilder(generator.minerAccount, from, 100, m_currency.minimumFee() + 1, minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION));
  builder.m_destinations.clear();

  TransactionBuilder::KeysVector kv;
  kv.push_back(to.getAccountKeys());

  builder.addMultisignatureOut(1000, kv, 1, m_currency.depositMinTerm() + 1);
  builder.setVersion(TRANSACTION_VERSION_1);
  auto tx = builder.build();
  generator.addCallback("mark_invalid_tx"); // should be rejected by the core
  generator.addEvent(tx);
  return true;
}

bool TransactionWithAmountLowerThenMinIsRejected::generate(std::vector<test_event_entry>& events) {
  TestGenerator generator(m_currency, events);
  generator.generator.defaultMajorVersion = TX_V2_MIN_BLOCK_VERSION;
  generator.generateBlocks(m_currency.minedMoneyUnlockWindow() + minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION), TX_V2_MIN_BLOCK_VERSION);
  auto builder = generator.createTxBuilder(generator.minerAccount, from, 100, m_currency.minimumFee() + 1, minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION));
  builder.m_destinations.clear();

  TransactionBuilder::KeysVector kv;
  kv.push_back(to.getAccountKeys());

  builder.addMultisignatureOut(m_currency.depositMinAmount() - 1, kv, 1, m_currency.depositMinTerm() + 1);
  auto tx = builder.build();
  generator.addCallback("mark_invalid_tx"); // should be rejected by the core
  generator.addEvent(tx);
  return true;
}

bool TransactionWithMinAmountIsAccepted::generate(std::vector<test_event_entry>& events) {
  TestGenerator generator(m_currency, events);
  generator.generator.defaultMajorVersion = TX_V2_MIN_BLOCK_VERSION;
  generator.generateBlocks(m_currency.minedMoneyUnlockWindow() + minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION), TX_V2_MIN_BLOCK_VERSION);
  auto builder = generator.createTxBuilder(generator.minerAccount, from, 100, m_currency.minimumFee() + 1, minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION));
  builder.m_destinations.clear();

  TransactionBuilder::KeysVector kv;
  kv.push_back(to.getAccountKeys());

  builder.addMultisignatureOut(m_currency.depositMinAmount(), kv, 1, m_currency.depositMinTerm() + 1);
  auto tx = builder.build();
  generator.addEvent(tx);
  return true;
}

bool TransactionWithTermLowerThenMinIsRejected::generate(std::vector<test_event_entry>& events) {
  TestGenerator generator(m_currency, events);
  generator.generator.defaultMajorVersion = TX_V2_MIN_BLOCK_VERSION;
  generator.generateBlocks(m_currency.minedMoneyUnlockWindow() + minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION), TX_V2_MIN_BLOCK_VERSION);
  auto builder = generator.createTxBuilder(generator.minerAccount, from, 100, m_currency.minimumFee() + 1, minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION));
  builder.m_destinations.clear();

  TransactionBuilder::KeysVector kv;
  kv.push_back(to.getAccountKeys());

  builder.addMultisignatureOut(m_currency.depositMinAmount(), kv, 1, m_currency.depositMinTerm() - 1);
  auto tx = builder.build();
  generator.addCallback("mark_invalid_tx"); // should be rejected by the core
  generator.addEvent(tx);
  return true;
}

bool TransactionWithMinTermIsAccepted::generate(std::vector<test_event_entry>& events) {
  TestGenerator generator(m_currency, events);
  generator.generator.defaultMajorVersion = TX_V2_MIN_BLOCK_VERSION;
  generator.generateBlocks(m_currency.minedMoneyUnlockWindow() + minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION), TX_V2_MIN_BLOCK_VERSION);
  auto builder = generator.createTxBuilder(generator.minerAccount, from, 100, m_currency.minimumFee() + 1, minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION));
  builder.m_destinations.clear();

  TransactionBuilder::KeysVector kv;
  kv.push_back(to.getAccountKeys());

  builder.addMultisignatureOut(m_currency.depositMinAmount(), kv, 1, m_currency.depositMinTerm());
  auto tx = builder.build();
  generator.addEvent(tx);
  return true;
}

bool TransactionWithTermGreaterThenMaxIsRejected::generate(std::vector<test_event_entry>& events) {
  TestGenerator generator(m_currency, events);
  generator.generator.defaultMajorVersion = TX_V2_MIN_BLOCK_VERSION;
  generator.generateBlocks(m_currency.minedMoneyUnlockWindow() + minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION), TX_V2_MIN_BLOCK_VERSION);
  auto builder = generator.createTxBuilder(generator.minerAccount, from, 100, m_currency.minimumFee() + 1, minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION));
  builder.m_destinations.clear();

  TransactionBuilder::KeysVector kv;
  kv.push_back(to.getAccountKeys());

  builder.addMultisignatureOut(m_currency.depositMinAmount(), kv, 1, m_currency.depositMaxTerm() + 1);
  auto tx = builder.build();
  generator.addCallback("mark_invalid_tx"); // should be rejected by the core
  generator.addEvent(tx);
  return true;
}

bool TransactionWithMaxTermIsAccepted::generate(std::vector<test_event_entry>& events) {
  TestGenerator generator(m_currency, events);
  generator.generator.defaultMajorVersion = TX_V2_MIN_BLOCK_VERSION;
  generator.generateBlocks(m_currency.minedMoneyUnlockWindow() + minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION), TX_V2_MIN_BLOCK_VERSION);
  auto builder = generator.createTxBuilder(generator.minerAccount, from, 100, m_currency.minimumFee() + 1, minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION));
  builder.m_destinations.clear();

  TransactionBuilder::KeysVector kv;
  kv.push_back(to.getAccountKeys());

  builder.addMultisignatureOut(m_currency.depositMinAmount(), kv, 1, m_currency.depositMaxTerm());
  auto tx = builder.build();
  generator.addEvent(tx);
  return true;
}

bool TransactionWithoutSignaturesIsRejected::generate(std::vector<test_event_entry>& events) {
  TestGenerator generator(m_currency, events);
  generator.generator.defaultMajorVersion = TX_V2_MIN_BLOCK_VERSION;
  generator.generateBlocks(m_currency.minedMoneyUnlockWindow() + minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION), TX_V2_MIN_BLOCK_VERSION);
  auto builder = generator.createTxBuilder(generator.minerAccount, from, 100, m_currency.minimumFee() + 1, minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION));
  builder.m_destinations.clear();

  TransactionBuilder::KeysVector kv;
  kv.push_back(to.getAccountKeys());

  builder.addMultisignatureOut(m_currency.depositMinAmount(), kv, 1, m_currency.depositMaxTerm());
  auto tx = builder.build();
  tx.signatures.clear();
  generator.addCallback("mark_invalid_tx"); // should be rejected by the core
  generator.addEvent(tx);
  return true;
}

bool TransactionWithZeroRequiredSignaturesIsRejected::generate(std::vector<test_event_entry>& events) {
  TestGenerator generator(m_currency, events);
  generator.generator.defaultMajorVersion = TX_V2_MIN_BLOCK_VERSION;
  generator.generateBlocks(m_currency.minedMoneyUnlockWindow() + minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION), TX_V2_MIN_BLOCK_VERSION);
  auto builder = generator.createTxBuilder(generator.minerAccount, from, 100, m_currency.minimumFee() + 1, minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION));
  builder.m_destinations.clear();

  TransactionBuilder::KeysVector kv;
  kv.push_back(to.getAccountKeys());

  builder.addMultisignatureOut(m_currency.depositMinAmount(), kv, 0, m_currency.depositMaxTerm());
  auto tx = builder.build();
  generator.addEvent(tx);
  return true;
}

bool TransactionWithNumberOfRequiredSignaturesGreaterThanKeysIsRejected::generate(std::vector<test_event_entry>& events) {
  TestGenerator generator(m_currency, events);
  generator.generator.defaultMajorVersion = TX_V2_MIN_BLOCK_VERSION;
  generator.generateBlocks(m_currency.minedMoneyUnlockWindow() + minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION), TX_V2_MIN_BLOCK_VERSION);
  auto builder = generator.createTxBuilder(generator.minerAccount, from, 100, m_currency.minimumFee() + 1, minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION));
  builder.m_destinations.clear();

  TransactionBuilder::KeysVector kv;
  kv.push_back(to.getAccountKeys());

  builder.addMultisignatureOut(m_currency.depositMinAmount(), kv, 2, m_currency.depositMaxTerm());
  auto tx = builder.build();
  generator.addCallback("mark_invalid_tx"); // should be rejected by the core
  generator.addEvent(tx);
  return true;
}

Crypto::PublicKey generate_invalid_pub_key() {
  for (int i = 0; i <= 0xFF; ++i) {
    Crypto::PublicKey key;
    memset(&key, i, sizeof(Crypto::PublicKey));
    if (!Crypto::check_key(key)) {
      return key;
    }
  }

  throw std::runtime_error("invalid public key wasn't found");
  return Crypto::PublicKey();
}

bool TransactionWithInvalidKeyIsRejected::generate(std::vector<test_event_entry>& events) {
  TestGenerator generator(m_currency, events);
  generator.generator.defaultMajorVersion = TX_V2_MIN_BLOCK_VERSION;
  generator.generateBlocks(m_currency.minedMoneyUnlockWindow() + minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION), TX_V2_MIN_BLOCK_VERSION);
  auto builder = generator.createTxBuilder(generator.minerAccount, from, 100, m_currency.minimumFee() + 1, minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION));
  builder.m_destinations.clear();

  auto pub = generate_invalid_pub_key();
  KeyPair k;
  k.publicKey = pub;
  auto src = createSource(m_currency.depositMinTerm(), k);

  builder.addMultisignatureInput(src);
  auto tx = builder.build();
  generator.addCallback("mark_invalid_tx"); // should be rejected by the core
  generator.addEvent(tx);
  return true;
}

bool TransactionWithDepositExtendsEmission::generate(std::vector<test_event_entry>& events) {
  TestGenerator generator(m_currency, events);
  generator.generator.defaultMajorVersion = TX_V2_MIN_BLOCK_VERSION;
  generator.generateBlocks(m_currency.minedMoneyUnlockWindow() + minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION), TX_V2_MIN_BLOCK_VERSION);
  KeyPair key;
  {
    auto builder = generator.createTxBuilder(generator.minerAccount, from, m_currency.depositMinAmount() + 100, m_currency.minimumFee(), minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION));
    builder.m_destinations.clear();

    TransactionBuilder::KeysVector kv;
    kv.push_back(from.getAccountKeys());

    builder.addMultisignatureOut(m_currency.depositMinAmount(), kv, 1, m_currency.depositMinTerm());
    key = builder.getTxKeys();
    auto tx = builder.build();
    assert(tx.outputs.size() == 1);
    generator.addEvent(tx);
    generator.makeNextBlock(tx);
  }

  generator.generateBlocks(m_currency.depositMinTerm() - 1, TX_V2_MIN_BLOCK_VERSION);

  {
    TransactionBuilder builder(m_currency);
    auto src = createSource(m_currency.depositMinTerm(), key);
    src.input.amount = m_currency.depositMinAmount();
    builder.addMultisignatureInput(src);
    auto tx = builder.build();
    generator.addEvent(tx);
    generator.addCallback("save_emission_before");
    generator.makeNextBlock(tx);
    generator.addCallback("save_emission_after");
    generator.generateBlocks(1, TX_V2_MIN_BLOCK_VERSION);
  }

  return true;
}

bool TransactionWithDepositRestorsEmissionOnAlternativeChain::generate(std::vector<test_event_entry>& events) {
  TestGenerator generator(m_currency, events);
  generator.generator.defaultMajorVersion = TX_V2_MIN_BLOCK_VERSION;
  generator.generateBlocks(m_currency.minedMoneyUnlockWindow() + minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION), TX_V2_MIN_BLOCK_VERSION);
  KeyPair key;
  {
    auto builder = generator.createTxBuilder(generator.minerAccount, from, m_currency.depositMinAmount() + 100, m_currency.minimumFee(), minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION));
    builder.m_destinations.clear();

    TransactionBuilder::KeysVector kv;
    kv.push_back(from.getAccountKeys());

    builder.addMultisignatureOut(m_currency.depositMinAmount(), kv, 1, m_currency.depositMinTerm());
    key = builder.getTxKeys();
    auto tx = builder.build();
    assert(tx.outputs.size() == 1);
    generator.addEvent(tx);
    generator.makeNextBlock(tx);
  }

  generator.generateBlocks(m_currency.depositMinTerm() - 1, TX_V2_MIN_BLOCK_VERSION);
  auto lastBlock = generator.lastBlock;

  {
    TransactionBuilder builder(m_currency);
    auto src = createSource(m_currency.depositMinTerm(), key);
    src.input.amount = m_currency.depositMinAmount();
    builder.addMultisignatureInput(src);
    auto tx = builder.build();
    generator.addEvent(tx);
    generator.makeNextBlock(tx);
    generator.addCallback("save_emission_before");
    generator.generateBlocks(1, TX_V2_MIN_BLOCK_VERSION);
  }

  // move to alternative chain
  generator.lastBlock = lastBlock;
  generator.generateBlocks(4, TX_V2_MIN_BLOCK_VERSION);
  generator.addCallback("save_emission_after");
  generator.generateBlocks(1, TX_V2_MIN_BLOCK_VERSION);

  return true;
}

bool TransactionWithOutputToSpentInputWillBeRejected::generate(std::vector<test_event_entry>& events) {
  TestGenerator generator(m_currency, events);
  generator.generator.defaultMajorVersion = TX_V2_MIN_BLOCK_VERSION;
  generator.generateBlocks(m_currency.minedMoneyUnlockWindow() + minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION), TX_V2_MIN_BLOCK_VERSION);
  KeyPair key;
  {
    auto builder = generator.createTxBuilder(generator.minerAccount, from, m_currency.depositMinAmount() + 100, m_currency.minimumFee(), minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION));
    builder.m_destinations.clear();

    TransactionBuilder::KeysVector kv;
    kv.push_back(from.getAccountKeys());

    builder.addMultisignatureOut(m_currency.depositMinAmount(), kv, 1, m_currency.depositMinTerm());
    key = builder.getTxKeys();
    auto tx = builder.build();
    assert(tx.outputs.size() == 1);
    generator.addEvent(tx);
    generator.makeNextBlock(tx);
  }

  generator.generateBlocks(m_currency.depositMinTerm() - 1, TX_V2_MIN_BLOCK_VERSION);

  {
    TransactionBuilder builder(m_currency);
    auto src = createSource(m_currency.depositMinTerm(), key);
    src.input.amount = m_currency.depositMinAmount();
    builder.addMultisignatureInput(src);
    auto tx = builder.build();
    generator.addEvent(tx);
    generator.makeNextBlock(tx);
  }

  {
    TransactionBuilder builder(m_currency);
    auto src = createSource(m_currency.depositMinTerm(), key);
    src.input.amount = m_currency.depositMinAmount();
    builder.addMultisignatureInput(src);
    auto tx = builder.build();
    generator.addCallback("mark_invalid_tx");
    generator.addEvent(tx);
  }

  return true;
}

bool TransactionWithMultipleInputsThatSpendOneOutputWillBeRejected::generate(std::vector<test_event_entry>& events) {
  TestGenerator generator(m_currency, events);
  generator.generator.defaultMajorVersion = TX_V2_MIN_BLOCK_VERSION;
  generator.generateBlocks(m_currency.minedMoneyUnlockWindow() + minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION), TX_V2_MIN_BLOCK_VERSION);
  KeyPair key;
  {
    auto builder = generator.createTxBuilder(generator.minerAccount, from, m_currency.depositMinAmount() + 100, m_currency.minimumFee(), minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION));
    builder.m_destinations.clear();

    TransactionBuilder::KeysVector kv;
    kv.push_back(from.getAccountKeys());

    builder.addMultisignatureOut(m_currency.depositMinAmount(), kv, 1, m_currency.depositMinTerm());
    key = builder.getTxKeys();
    auto tx = builder.build();
    assert(tx.outputs.size() == 1);
    generator.addEvent(tx);
    generator.makeNextBlock(tx);
  }

  generator.generateBlocks(m_currency.depositMinTerm() - 1, TX_V2_MIN_BLOCK_VERSION);

  {
    TransactionBuilder builder(m_currency);
    auto src = createSource(m_currency.depositMinTerm(), key);
    src.input.amount = m_currency.depositMinAmount();
    builder.addMultisignatureInput(src);
    builder.addMultisignatureInput(src);
    auto tx = builder.build();
    generator.addCallback("mark_invalid_tx");
    generator.addEvent(tx);
  }

  return true;
}

bool TransactionWithInputWithAmountThatIsDoesntHaveOutputWithSameAmountWillBeRejected::generate(std::vector<test_event_entry>& events) {
  TestGenerator generator(m_currency, events);
  generator.generator.defaultMajorVersion = TX_V2_MIN_BLOCK_VERSION;
  generator.generateBlocks(m_currency.minedMoneyUnlockWindow() + minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION), TX_V2_MIN_BLOCK_VERSION);
  KeyPair key;
  {
    auto builder = generator.createTxBuilder(generator.minerAccount, from, m_currency.depositMinAmount() + 100, m_currency.minimumFee(), minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION));
    builder.m_destinations.clear();

    TransactionBuilder::KeysVector kv;
    kv.push_back(from.getAccountKeys());

    builder.addMultisignatureOut(m_currency.depositMinAmount() + 42, kv, 1, m_currency.depositMinTerm());
    key = builder.getTxKeys();
    auto tx = builder.build();
    assert(tx.outputs.size() == 1);
    generator.addEvent(tx);
    generator.makeNextBlock(tx);
  }

  generator.generateBlocks(m_currency.depositMinTerm() - 1, TX_V2_MIN_BLOCK_VERSION);

  {
    TransactionBuilder builder(m_currency);
    auto src = createSource(m_currency.depositMinTerm(), key);
    src.input.amount = m_currency.depositMinAmount();
    builder.addMultisignatureInput(src);
    auto tx = builder.build();
    generator.addCallback("mark_invalid_tx");
    generator.addEvent(tx);
  }

  return true;
}

//TODO: check
bool TransactionWithInputWithIndexLargerThanNumberOfOutputsWithThisSumWillBeRejected::generate(std::vector<test_event_entry>& events) {
  TestGenerator generator(m_currency, events);
  generator.generator.defaultMajorVersion = TX_V2_MIN_BLOCK_VERSION;
  generator.generateBlocks(m_currency.minedMoneyUnlockWindow() + minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION), TX_V2_MIN_BLOCK_VERSION);
  KeyPair key;
  {
    auto builder = generator.createTxBuilder(generator.minerAccount, from, 2 * m_currency.depositMinAmount() + 100, m_currency.minimumFee(), minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION));
    builder.m_destinations.clear();

    TransactionBuilder::KeysVector kv;
    kv.push_back(from.getAccountKeys());

    builder.addMultisignatureOut(m_currency.depositMinAmount(), kv, 1, m_currency.depositMinTerm());
    builder.addMultisignatureOut(m_currency.depositMinAmount(), kv, 1, m_currency.depositMinTerm());
    key = builder.getTxKeys();
    auto tx = builder.build();
    generator.addEvent(tx);
    generator.makeNextBlock(tx);
  }

  generator.generateBlocks(m_currency.depositMinTerm() - 1, TX_V2_MIN_BLOCK_VERSION);

  {
    TransactionBuilder builder(m_currency);
    auto src = createSource(m_currency.depositMinTerm(), key);
    src.input.amount = m_currency.depositMinAmount() * 2;
    src.input.outputIndex = 2;
    builder.addMultisignatureInput(src);
    auto tx = builder.build();
    generator.addCallback("mark_invalid_tx");
    generator.addEvent(tx);
  }

  return true;
}

bool TransactionWithInputThatPointsToTheOutputButHasAnotherTermWillBeRejected::generate(std::vector<test_event_entry>& events) {
  TestGenerator generator(m_currency, events);
  generator.generator.defaultMajorVersion = TX_V2_MIN_BLOCK_VERSION;
  generator.generateBlocks(m_currency.minedMoneyUnlockWindow() + minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION), TX_V2_MIN_BLOCK_VERSION);
  KeyPair key;
  {
    auto builder = generator.createTxBuilder(generator.minerAccount, from, m_currency.depositMinAmount() + 100, m_currency.minimumFee(), minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION));
    builder.m_destinations.clear();

    TransactionBuilder::KeysVector kv;
    kv.push_back(from.getAccountKeys());

    builder.addMultisignatureOut(m_currency.depositMinAmount(), kv, 1, m_currency.depositMinTerm());
    key = builder.getTxKeys();
    auto tx = builder.build();
    generator.addEvent(tx);
    generator.makeNextBlock(tx);
  }

  generator.generateBlocks(m_currency.depositMinTerm() - 1, TX_V2_MIN_BLOCK_VERSION);

  {
    TransactionBuilder builder(m_currency);
    auto src = createSource(m_currency.depositMinTerm(), key);
    src.input.term = m_currency.depositMinTerm() + 1;
    builder.addMultisignatureInput(src);
    auto tx = builder.build();
    generator.addCallback("mark_invalid_tx");
    generator.addEvent(tx);
  }

  return true;
}

bool TransactionThatTriesToSpendOutputWhosTermHasntFinishedWillBeRejected::generate(std::vector<test_event_entry>& events) {
  TestGenerator generator(m_currency, events);
  generator.generator.defaultMajorVersion = TX_V2_MIN_BLOCK_VERSION;
  generator.generateBlocks(m_currency.minedMoneyUnlockWindow() + minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION), TX_V2_MIN_BLOCK_VERSION);
  KeyPair key;
  {
    auto builder = generator.createTxBuilder(generator.minerAccount, from, m_currency.depositMinAmount() + 100, m_currency.minimumFee(), minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION));
    builder.m_destinations.clear();

    TransactionBuilder::KeysVector kv;
    kv.push_back(from.getAccountKeys());

    builder.addMultisignatureOut(m_currency.depositMinAmount(), kv, 1, m_currency.depositMinTerm());
    key = builder.getTxKeys();
    auto tx = builder.build();
    generator.addEvent(tx);
    generator.makeNextBlock(tx);
  }

  generator.generateBlocks(m_currency.depositMinTerm() - 2, TX_V2_MIN_BLOCK_VERSION);

  {
    TransactionBuilder builder(m_currency);
    auto src = createSource(m_currency.depositMinTerm(), key);
    builder.addMultisignatureInput(src);
    auto tx = builder.build();
    generator.addCallback("mark_invalid_tx");
    generator.addEvent(tx);
  }

  return true;
}

bool TransactionWithAmountThatHasAlreadyFinishedWillBeAccepted::generate(std::vector<test_event_entry>& events) {
  TestGenerator generator(m_currency, events);
  generator.generator.defaultMajorVersion = TX_V2_MIN_BLOCK_VERSION;
  generator.generateBlocks(m_currency.minedMoneyUnlockWindow() + minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION), TX_V2_MIN_BLOCK_VERSION);
  KeyPair key;
  {
    auto builder = generator.createTxBuilder(generator.minerAccount, from, m_currency.depositMinAmount() + 100, m_currency.minimumFee(), minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION));
    builder.m_destinations.clear();

    TransactionBuilder::KeysVector kv;
    kv.push_back(from.getAccountKeys());

    builder.addMultisignatureOut(m_currency.depositMinAmount(), kv, 1, m_currency.depositMinTerm());
    key = builder.getTxKeys();
    auto tx = builder.build();
    generator.addEvent(tx);
    generator.makeNextBlock(tx);
  }

  generator.generateBlocks(m_currency.depositMinTerm() - 1, TX_V2_MIN_BLOCK_VERSION);

  {
    TransactionBuilder builder(m_currency);
    auto src = createSource(m_currency.depositMinTerm(), key);
    builder.addMultisignatureInput(src);
    auto tx = builder.build();
    generator.addEvent(tx);
    generator.makeNextBlock(tx);
  }

  return true;
}

bool TransactionWithDepositExtendsTotalDeposit::generate(std::vector<test_event_entry>& events) {
  TestGenerator generator(m_currency, events);
  generator.generator.defaultMajorVersion = TX_V2_MIN_BLOCK_VERSION;
  generator.generateBlocks(m_currency.minedMoneyUnlockWindow() + minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION), TX_V2_MIN_BLOCK_VERSION);
  generator.addCallback("amountZero");
  KeyPair key;
  {
    auto builder = generator.createTxBuilder(generator.minerAccount, from, m_currency.depositMinAmount() + 100, m_currency.minimumFee(), minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION));
    builder.m_destinations.clear();

    TransactionBuilder::KeysVector kv;
    kv.push_back(from.getAccountKeys());

    builder.addMultisignatureOut(m_currency.depositMinAmount(), kv, 1, m_currency.depositMinTerm());
    key = builder.getTxKeys();
    auto tx = builder.build();
    generator.addEvent(tx);
    generator.makeNextBlock(tx);
  }
  generator.addCallback("amountOneMinimal");
  generator.generateBlocks(1, TX_V2_MIN_BLOCK_VERSION);
  return true;
}

bool TransactionWithMultipleDepositOutsExtendsTotalDeposit::generate(std::vector<test_event_entry>& events) {
  TestGenerator generator(m_currency, events);
  generator.generator.defaultMajorVersion = TX_V2_MIN_BLOCK_VERSION;
  // Four deposit outputs need four matured coinbases, each ring with a matured decoy.
  generator.generateBlocks(m_currency.minedMoneyUnlockWindow() + minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION) + 4, TX_V2_MIN_BLOCK_VERSION);
  generator.addCallback("amountZero");
  KeyPair key;
  {
    auto builder = generator.createTxBuilder(generator.minerAccount, from, 4 * m_currency.depositMinAmount() + 100, m_currency.minimumFee(), minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION));
    builder.m_destinations.clear();

    TransactionBuilder::KeysVector kv;
    kv.push_back(from.getAccountKeys());

    builder.addMultisignatureOut(m_currency.depositMinAmount(), kv, 1, m_currency.depositMinTerm());
    builder.addMultisignatureOut(m_currency.depositMinAmount(), kv, 1, m_currency.depositMinTerm());
    builder.addMultisignatureOut(m_currency.depositMinAmount(), kv, 1, m_currency.depositMinTerm());
    builder.addMultisignatureOut(m_currency.depositMinAmount(), kv, 1, 0);
    key = builder.getTxKeys();
    auto tx = builder.build();
    generator.addEvent(tx);
    generator.makeNextBlock(tx);
  }
  generator.addCallback("amountThreeMinimal");
  generator.generateBlocks(2, TX_V2_MIN_BLOCK_VERSION);
  return true;
}

bool TransactionWithDepositIsClearedAfterInputSpend::generate(std::vector<test_event_entry>& events) {
  TestGenerator generator(m_currency, events);
  generator.generator.defaultMajorVersion = TX_V2_MIN_BLOCK_VERSION;
  generator.generateBlocks(m_currency.minedMoneyUnlockWindow() + minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION), TX_V2_MIN_BLOCK_VERSION);
  generator.addCallback("amountZero");
  KeyPair key;
  {
    auto builder = generator.createTxBuilder(generator.minerAccount, from, m_currency.depositMinAmount() + 100, m_currency.minimumFee(), minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION));
    builder.m_destinations.clear();

    TransactionBuilder::KeysVector kv;
    kv.push_back(from.getAccountKeys());

    builder.addMultisignatureOut(m_currency.depositMinAmount(), kv, 1, m_currency.depositMinTerm());
    key = builder.getTxKeys();
    auto tx = builder.build();
    generator.addEvent(tx);
    generator.makeNextBlock(tx);
  }

  generator.generateBlocks(m_currency.depositMinTerm() - 1, TX_V2_MIN_BLOCK_VERSION);

  generator.addCallback("amountOneMinimal");
  {
    TransactionBuilder builder(m_currency);
    auto src = createSource(m_currency.depositMinTerm(), key);
    src.input.term = m_currency.depositMinTerm();
    builder.addMultisignatureInput(src);
    auto tx = builder.build();
    generator.addEvent(tx);
    generator.makeNextBlock(tx);
  }
  generator.addCallback("amountZero");
  generator.generateBlocks(1, TX_V2_MIN_BLOCK_VERSION);

  return true;
}

bool TransactionWithDepositUpdatesInterestAfterDepositUnlock::generate(std::vector<test_event_entry>& events) {
  TestGenerator generator(m_currency, events);
  generator.generator.defaultMajorVersion = TX_V2_MIN_BLOCK_VERSION;
  generator.generateBlocks(m_currency.minedMoneyUnlockWindow() + minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION), TX_V2_MIN_BLOCK_VERSION);
  KeyPair key;
  {
    auto builder = generator.createTxBuilder(generator.minerAccount, from, m_currency.depositMinAmount() + 100, m_currency.minimumFee(), minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION));
    builder.m_destinations.clear();

    TransactionBuilder::KeysVector kv;
    kv.push_back(from.getAccountKeys());

    builder.addMultisignatureOut(m_currency.depositMinAmount(), kv, 1, m_currency.depositMinTerm());
    key = builder.getTxKeys();
    auto tx = builder.build();
    generator.addEvent(tx);
    generator.makeNextBlock(tx);
  }

  generator.generateBlocks(m_currency.depositMinTerm() - 1, TX_V2_MIN_BLOCK_VERSION);

  generator.addCallback("interestZero");
  {
    TransactionBuilder builder(m_currency);
    auto src = createSource(m_currency.depositMinTerm(), key);
    src.input.term = m_currency.depositMinTerm();
    builder.addMultisignatureInput(src);
    auto tx = builder.build();
    generator.addEvent(tx);
    generator.makeNextBlock(tx);
    generator.addCallback("interestOneMinimal");
    generator.generateBlocks(1, TX_V2_MIN_BLOCK_VERSION);
  }

  return true;
}

bool TransactionWithDepositUpdatesInterestAfterDepositUnlockMultiple::generate(std::vector<test_event_entry>& events) {
  TestGenerator generator(m_currency, events);
  generator.generator.defaultMajorVersion = TX_V2_MIN_BLOCK_VERSION;
  generator.generateBlocks(m_currency.minedMoneyUnlockWindow() + minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION), TX_V2_MIN_BLOCK_VERSION);
  KeyPair key;
  {
    auto builder = generator.createTxBuilder(generator.minerAccount, from, 2 * m_currency.depositMinAmount() + 100, m_currency.minimumFee(), minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION));
    builder.m_destinations.clear();

    TransactionBuilder::KeysVector kv;
    kv.push_back(from.getAccountKeys());

    builder.addMultisignatureOut(m_currency.depositMinAmount(), kv, 1, m_currency.depositMinTerm());
    builder.addMultisignatureOut(m_currency.depositMinAmount(), kv, 1, m_currency.depositMinTerm());
    key = builder.getTxKeys();
    auto tx = builder.build();
    generator.addEvent(tx);
    generator.makeNextBlock(tx);
  }

  generator.generateBlocks(m_currency.depositMinTerm() - 1, TX_V2_MIN_BLOCK_VERSION);

  generator.addCallback("interestZero");
  {
    TransactionBuilder builder(m_currency);
    auto src1 = createSource(m_currency.depositMinTerm(), key);
    auto src2 = createSource(m_currency.depositMinTerm(), key);
    src1.input.term = m_currency.depositMinTerm();
    src2.input.term = m_currency.depositMinTerm();
    src2.input.outputIndex = 1;
    src2.srcOutputIndex = 1;
    builder.addMultisignatureInput(src1);
    builder.addMultisignatureInput(src2);
    auto tx = builder.build();
    generator.addEvent(tx);
    generator.makeNextBlock(tx);
    generator.addCallback("interestTwoMininmal");
    generator.generateBlocks(1, TX_V2_MIN_BLOCK_VERSION);
  }

  return true;
}

bool TransactionWithDepositUnrolesInterestAfterSwitchToAlternativeChain::generate(std::vector<test_event_entry>& events) {
  TestGenerator generator(m_currency, events);
  generator.generator.defaultMajorVersion = TX_V2_MIN_BLOCK_VERSION;
  generator.generateBlocks(m_currency.minedMoneyUnlockWindow() + minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION), TX_V2_MIN_BLOCK_VERSION);
  KeyPair key;
  {
    auto builder = generator.createTxBuilder(generator.minerAccount, from, m_currency.depositMinAmount() + 100, m_currency.minimumFee(), minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION));
    builder.m_destinations.clear();

    TransactionBuilder::KeysVector kv;
    kv.push_back(from.getAccountKeys());

    builder.addMultisignatureOut(m_currency.depositMinAmount(), kv, 1, m_currency.depositMinTerm());
    key = builder.getTxKeys();
    auto tx = builder.build();
    generator.addEvent(tx);
    generator.makeNextBlock(tx);
  }

  generator.generateBlocks(m_currency.depositMinTerm() - 1, TX_V2_MIN_BLOCK_VERSION);
  auto lastBlock = generator.lastBlock;

  generator.addCallback("interestZero");
  {
    TransactionBuilder builder(m_currency);
    auto src = createSource(m_currency.depositMinTerm(), key);
    src.input.term = m_currency.depositMinTerm();
    builder.addMultisignatureInput(src);
    auto tx = builder.build();
    generator.addEvent(tx);
    generator.makeNextBlock(tx);
    generator.addCallback("interestOneMinimal");
    generator.generateBlocks(1, TX_V2_MIN_BLOCK_VERSION);
  }

  generator.lastBlock = lastBlock;
  generator.generateBlocks(4, TX_V2_MIN_BLOCK_VERSION);
  generator.addCallback("interestZero");
  generator.generateBlocks(1, TX_V2_MIN_BLOCK_VERSION);

  return true;
}

bool TransactionWithDepositUnrolesAmountAfterSwitchToAlternativeChain::generate(std::vector<test_event_entry>& events) {
  TestGenerator generator(m_currency, events);
  generator.generator.defaultMajorVersion = TX_V2_MIN_BLOCK_VERSION;
  generator.generateBlocks(m_currency.minedMoneyUnlockWindow() + minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION), TX_V2_MIN_BLOCK_VERSION);
  auto lastBlock = generator.lastBlock;
  KeyPair key;
  {
    auto builder = generator.createTxBuilder(generator.minerAccount, from, m_currency.depositMinAmount() + 100, m_currency.minimumFee(), minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION));
    builder.m_destinations.clear();

    TransactionBuilder::KeysVector kv;
    kv.push_back(from.getAccountKeys());

    builder.addMultisignatureOut(m_currency.depositMinAmount(), kv, 1, m_currency.depositMinTerm());
    key = builder.getTxKeys();
    auto tx = builder.build();
    generator.addEvent(tx);
    generator.makeNextBlock(tx);
  }

  generator.addCallback("amountOneMinimal");
  generator.generateBlocks(m_currency.depositMinTerm(), TX_V2_MIN_BLOCK_VERSION);

  generator.addCallback("amountOneMinimal");
  generator.lastBlock = lastBlock;
  generator.generateBlocks(m_currency.depositMinTerm() + 4, TX_V2_MIN_BLOCK_VERSION);
  generator.addCallback("amountZero");
  generator.generateBlocks(1, TX_V2_MIN_BLOCK_VERSION);

  return true;
}

bool TransactionWithDepositUnrolesPartOfAmountAfterSwitchToAlternativeChain::generate(std::vector<test_event_entry>& events) {
  TestGenerator generator(m_currency, events);
  generator.generator.defaultMajorVersion = TX_V2_MIN_BLOCK_VERSION;
  generator.generateBlocks(m_currency.minedMoneyUnlockWindow() + 3, TX_V2_MIN_BLOCK_VERSION);
  KeyPair key;
  {
    auto builder = generator.createTxBuilder(generator.minerAccount, from, m_currency.depositMinAmount(), m_currency.minimumFee(), minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION));
    builder.m_destinations.clear();

    TransactionBuilder::KeysVector kv;
    kv.push_back(from.getAccountKeys());

    builder.addMultisignatureOut(m_currency.depositMinAmount(), kv, 1, m_currency.depositMinTerm());
    key = builder.getTxKeys();
    auto tx = builder.build();
    generator.addEvent(tx);
    generator.makeNextBlock(tx);
  }

  auto lastBlock = generator.lastBlock;
  generator.addCallback("amountOneMinimal");
  generator.generateBlocks(m_currency.depositMinTerm(), TX_V2_MIN_BLOCK_VERSION);

  {
    auto builder = generator.createTxBuilder(generator.minerAccount, from, m_currency.depositMinAmount(), m_currency.minimumFee(), minRingDecoys(m_currency, TX_V2_MIN_BLOCK_VERSION));
    builder.m_sources.clear();
    builder.m_destinations.clear();
    TransactionBuilder::KeysVector kv;
    kv.push_back(from.getAccountKeys());
    auto src1 = createSource(m_currency.depositMinTerm(), key);
    src1.input.term = m_currency.depositMinTerm();
    builder.addMultisignatureInput(src1);
    builder.addMultisignatureOut(m_currency.depositMinAmount(), kv, 1, m_currency.depositMinTerm());
    auto tx = builder.build();
    generator.addEvent(tx);
    generator.makeNextBlock(tx);
    generator.generateBlocks(1, TX_V2_MIN_BLOCK_VERSION);
  }

  generator.lastBlock = lastBlock;
  generator.generateBlocks(m_currency.depositMinTerm() + 4, TX_V2_MIN_BLOCK_VERSION);
  generator.addCallback("amountOneMinimal");
  generator.generateBlocks(1, TX_V2_MIN_BLOCK_VERSION);

  return true;
}

}
