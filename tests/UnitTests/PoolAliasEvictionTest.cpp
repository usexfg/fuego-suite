// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You can redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation. Fuego includes elements written
// by third parties. See file labeled LICENSE for more details.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

#include <boost/utility/value_init.hpp>

#include "gtest/gtest.h"

#include "CryptoNoteCore/Account.h"
#include "CryptoNoteCore/CryptoNoteFormatUtils.h"
#include "CryptoNoteCore/CryptoNoteTools.h"
#include "CryptoNoteCore/Currency.h"
#include "CryptoNoteCore/ITimeProvider.h"
#include "CryptoNoteCore/ITransactionValidator.h"
#include "CryptoNoteCore/TransactionExtra.h"
#include "CryptoNoteCore/TransactionPool.h"
#include "Logging/ConsoleLogger.h"

using namespace CryptoNote;

namespace {

bool carriesAliasRegistration(const Transaction& tx) {
  std::vector<TransactionExtraField> fields;
  parseTransactionExtra(tx.extra, fields);
  for (const auto& field : fields) {
    if (field.type() == typeid(TransactionExtraAliasRegistration)) {
      return true;
    }
  }
  return false;
}

// Settles every transaction; an alias registration's tag applies only while its alias is free.
class AliasSettlementValidator : public ITransactionValidator {
public:
  bool aliasFree = true;

  bool checkTransactionInputs(const Transaction&, BlockInfo&) override { return true; }
  bool checkTransactionInputs(const Transaction&, BlockInfo&, BlockInfo&) override { return true; }
  bool haveSpentKeyImages(const Transaction&) override { return false; }
  bool checkTransactionSize(size_t) override { return true; }
  bool checkTransactionSettlement(const Transaction& tx, uint64_t& fee) override {
    fee = 0;
    for (const auto& in : tx.inputs) {
      fee += boost::get<KeyInput>(in).amount;
    }
    for (const auto& out : tx.outputs) {
      fee -= out.amount;
    }
    return true;
  }
  bool aliasTagsCannotApply(const Transaction& tx) override { return !aliasFree && carriesAliasRegistration(tx); }
};

class SteppedTimeProvider : public ITimeProvider {
public:
  time_t timeNow = time(nullptr);
  time_t now() override { return timeNow; }
};

class PoolAliasEviction : public ::testing::Test {
protected:
  PoolAliasEviction()
      : logger(Logging::ERROR),
        currency(CurrencyBuilder(logger).currency()),
        pool(currency, validator, clock, logger) {}

  Transaction spendOfFreshCoinbase(const std::vector<uint8_t>& extra) {
    AccountBase miner;
    miner.generate();
    Transaction coinbase;
    EXPECT_TRUE(currency.constructMinerTx(BLOCK_MAJOR_VERSION_1, 0, 0, 0, 2, 0, miner.getAccountKeys().address, coinbase));
    TransactionSourceEntry source;
    source.amount = coinbase.outputs[0].amount;
    source.realTransactionPublicKey = getTransactionPublicKeyFromExtra(coinbase.extra);
    source.realOutputIndexInTransaction = 0;
    source.outputs.push_back({0, boost::get<KeyOutput>(coinbase.outputs[0].target).key});
    source.realOutput = 0;
    AccountBase recipient;
    recipient.generate();
    std::vector<TransactionDestinationEntry> destinations{
        TransactionDestinationEntry(source.amount - currency.minimumFee(), recipient.getAccountKeys().address)};
    Transaction tx;
    Logging::ConsoleLogger quiet(Logging::ERROR);
    Crypto::SecretKey transactionSecret;
    EXPECT_TRUE(constructTransaction(miner.getAccountKeys(), {source}, destinations, extra, tx, 0, quiet, transactionSecret));
    return tx;
  }

  void addToPool(const Transaction& tx) {
    tx_verification_context tvc = boost::value_initialized<tx_verification_context>();
    ASSERT_TRUE(pool.add_tx(tx, tvc, false, 0));
    ASSERT_TRUE(tvc.m_added_to_pool);
  }

  void runPoolCleanup() {
    clock.timeNow += 61;
    pool.on_idle();
  }

  Logging::ConsoleLogger logger;
  Currency currency;
  AliasSettlementValidator validator;
  SteppedTimeProvider clock;
  tx_memory_pool pool;
};

TEST_F(PoolAliasEviction, RegistrationThatLostItsAliasLeavesThePool) {
  TransactionExtraAliasRegistration registration;
  registration.alias = "firenode";
  registration.ownerAddress = "owner";
  registration.aliasType = 1;
  std::vector<uint8_t> aliasExtra;
  ASSERT_TRUE(addAliasToExtra(aliasExtra, registration));
  const Transaction registering = spendOfFreshCoinbase(aliasExtra);
  const Transaction payment = spendOfFreshCoinbase({});
  addToPool(registering);
  addToPool(payment);

  runPoolCleanup();
  EXPECT_EQ(2u, pool.get_transactions_count()) << "an alias tag that still applies stays";

  validator.aliasFree = false;
  runPoolCleanup();
  EXPECT_FALSE(pool.have_tx(getObjectHash(registering)));
  EXPECT_TRUE(pool.have_tx(getObjectHash(payment))) << "only alias tags are re-checked";

  validator.aliasFree = true;
  addToPool(registering);
  EXPECT_TRUE(pool.have_tx(getObjectHash(registering))) << "after a reorg frees the alias it may be relayed again";
}

}
