// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// WalletLegacy input reservation: a send holds the inputs it selected until it is
// built, sent or fails, so sends started back to back never pick the same input.

#include <deque>
#include <memory>
#include <sstream>
#include <vector>

#include "gtest/gtest.h"

#include "Common/StdInputStream.h"
#include "Common/StdOutputStream.h"
#include "CryptoNoteCore/Account.h"
#include "CryptoNoteCore/CryptoNoteTools.h"
#include "CryptoNoteCore/CryptoNoteFormatUtils.h"
#include "CryptoNoteCore/Currency.h"
#include "CryptoNoteCore/TransactionApi.h"
#include "Logging/ConsoleLogger.h"
#include "Serialization/BinaryInputStreamSerializer.h"
#include "Serialization/BinaryOutputStreamSerializer.h"
#include "NullNode.h"
#include "Transfers/TransfersContainer.h"
#include "WalletLegacy/WalletLegacyEvent.h"
#include "WalletLegacy/WalletSendTransactionContext.h"
#include "WalletLegacy/WalletTransactionSender.h"
#include "WalletLegacy/WalletUserTransactionsCache.h"
#include "Wallet/WalletErrors.h"

using namespace CryptoNote;

namespace {

const size_t kSpendableAge = 1;
const uint32_t kFundingHeight = 10;
const uint64_t kOutputAmount = 100000000;
const uint64_t kSendAmount = 1000000;

class WalletInputReservation : public ::testing::Test {
protected:
  WalletInputReservation()
      : logger(Logging::ERROR),
        currency(CurrencyBuilder(logger).currency()),
        container(currency, kSpendableAge),
        sender(currency, cache, walletKeys(), container, node) {
    fundWallet(2);
  }

  AccountKeys walletKeys() {
    account.generate();
    return account.getAccountKeys();
  }

  void fundWallet(size_t outputCount) {
    for (size_t i = 0; i < outputCount; ++i) {
      auto tx = createTransaction();
      KeyOutput out;
      Crypto::SecretKey unusedSecret;
      Crypto::generate_keys(out.key, unusedSecret);
      tx->addOutput(kOutputAmount, out);

      TransactionOutputInformationIn info{};
      info.type = TransactionTypes::OutputType::Key;
      info.amount = kOutputAmount;
      info.globalOutputIndex = static_cast<uint32_t>(i);
      info.outputInTransaction = 0;
      info.transactionPublicKey = tx->getTransactionPublicKey();
      info.outputKey = out.key;
      info.keyImage.data[0] = static_cast<uint8_t>(i + 1);

      TransactionBlockInfo block{kFundingHeight, 1000000};
      ASSERT_TRUE(container.addTransaction(block, *tx, {info}, {}, nullptr));
      funding.push_back(std::move(tx));
    }
    container.advanceHeight(kFundingHeight + 10);
  }

  struct StartedSend {
    TransactionId transactionId;
    std::vector<Crypto::Hash> inputs;
  };

  StartedSend startSend() {
    std::vector<WalletLegacyTransfer> transfers{{currency.accountAddressAsString(account), static_cast<int64_t>(kSendAmount)}};
    std::deque<std::unique_ptr<WalletLegacyEvent>> events;
    Crypto::SecretKey transactionSecret;
    StartedSend started{};
    auto request = sender.makeSendRequest(transactionSecret, false, started.transactionId, events, transfers,
                                          currency.minimumFee());
    EXPECT_NE(nullptr, request) << "the send waits for decoys, as it does on a live node";
    const auto& transaction = cache.getTransaction(started.transactionId);
    EXPECT_EQ(WalletLegacyTransactionState::Sending, transaction.state);
    for (const auto& out : spendableOutputs()) {
      if (cache.isUsed(out)) {
        started.inputs.push_back(out.transactionHash);
      }
    }
    return started;
  }

  std::vector<TransactionOutputInformation> spendableOutputs() {
    std::vector<TransactionOutputInformation> outputs;
    container.getOutputs(outputs, ITransfersContainer::IncludeKeyUnlocked);
    return outputs;
  }

  Logging::ConsoleLogger logger;
  Currency currency;
  AccountBase account;
  TransfersContainer container;
  WalletUserTransactionsCache cache;
  NullNode node;
  WalletTransactionSender sender;
  std::vector<std::unique_ptr<ITransaction>> funding;
};

} // namespace

// A send picks its inputs, then waits for decoys before it is built. A second
// send started in that window must not pick the same inputs.
TEST_F(WalletInputReservation, BackToBackSendsUseDisjointInputs) {
  ASSERT_EQ(2u, spendableOutputs().size());
  const StartedSend first = startSend();
  ASSERT_EQ(1u, first.inputs.size());

  const StartedSend second = startSend();
  ASSERT_EQ(2u, second.inputs.size()) << "the second send held the other output";
  EXPECT_NE(second.inputs[0], second.inputs[1]);
}

// A send that fails before it is built gives its inputs back.
TEST_F(WalletInputReservation, FailedSendReleasesItsInputs) {
  const StartedSend first = startSend();
  ASSERT_EQ(1u, first.inputs.size());

  cache.updateTransactionSendingState(first.transactionId, make_error_code(error::INTERNAL_WALLET_ERROR));
  for (const auto& out : spendableOutputs()) {
    EXPECT_FALSE(cache.isUsed(out));
  }

  const StartedSend retry = startSend();
  EXPECT_EQ(1u, retry.inputs.size());
}

// Once nothing is free, a further send is refused instead of reusing a held input.
TEST_F(WalletInputReservation, SendIsRefusedWhenEveryInputIsHeld) {
  startSend();
  startSend();
  std::vector<WalletLegacyTransfer> transfers{{currency.accountAddressAsString(account), static_cast<int64_t>(kSendAmount)}};
  std::deque<std::unique_ptr<WalletLegacyEvent>> events;
  Crypto::SecretKey transactionSecret;
  TransactionId transactionId;
  EXPECT_THROW(sender.makeSendRequest(transactionSecret, false, transactionId, events, transfers, currency.minimumFee()),
               std::system_error);
}

// A second claim on an output that a send already holds is refused, so a later
// selection that skipped the held-output check (fusion did) cannot take it over.
TEST_F(WalletInputReservation, HeldInputCannotBeTakenOver) {
  const StartedSend first = startSend();
  ASSERT_EQ(1u, first.inputs.size());
  const TransactionId otherSend = cache.addNewTransaction(0, currency.minimumFee(), std::string(), {}, 0, {});
  EXPECT_FALSE(cache.reserveOutputs(otherSend, spendableOutputs()));

  cache.updateTransactionSendingState(otherSend, make_error_code(error::INTERNAL_WALLET_ERROR));
  size_t held = 0;
  for (const auto& out : spendableOutputs()) {
    held += cache.isUsed(out) ? 1 : 0;
  }
  EXPECT_EQ(1u, held) << "the refused claim released nothing that belongs to the first send";
}

// A send that completes without being recorded as unconfirmed (the multisig deposit
// withdraw) gives its reservation back.
TEST_F(WalletInputReservation, CompletedSendReleasesItsReservation) {
  const StartedSend first = startSend();
  ASSERT_EQ(1u, first.inputs.size());
  cache.updateTransactionSendingState(first.transactionId, std::error_code());
  for (const auto& out : spendableOutputs()) {
    EXPECT_FALSE(cache.isUsed(out));
  }
}

// The node answers one decoy set per selected input, for that input's amount.
TEST(WalletDecoyResponse, MustMatchTheSelectedInputs) {
  SendTransactionContext context;
  TransactionOutputInformation first{};
  first.amount = 7;
  TransactionOutputInformation second{};
  second.amount = 9;
  context.selectedTransfers = {first, second};

  COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::outs_for_amount firstSet{};
  firstSet.amount = 7;
  COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::outs_for_amount secondSet{};
  secondSet.amount = 9;

  context.outs = {firstSet};
  EXPECT_FALSE(WalletTransactionSender::decoysMatchSelectedInputs(context)) << "one set short";
  context.outs = {secondSet, firstSet};
  EXPECT_FALSE(WalletTransactionSender::decoysMatchSelectedInputs(context)) << "sets for other amounts";
  context.outs = {firstSet, secondSet};
  EXPECT_TRUE(WalletTransactionSender::decoysMatchSelectedInputs(context));
}

// A commitment spend is recorded under one hash in both the unconfirmed
// transactions and the spent deposits. Both go when it confirms or fails.
TEST(WalletUnconfirmedSpends, EraseRemovesTheSpentDepositRecordToo) {
  WalletUnconfirmedTransactions unconfirmed(60 * 60);
  Transaction tx{};
  tx.version = TRANSACTION_VERSION_2;
  const Crypto::Hash hash = getObjectHash(tx);
  unconfirmed.add(tx, 0, 0, {});
  unconfirmed.addDepositSpendingTransaction(hash, UnconfirmedSpentDepositDetails{0, 500, 10});
  ASSERT_EQ(490u, unconfirmed.countSpentDepositsProfit() + unconfirmed.countSpentDepositsTotalAmount() - 500);

  unconfirmed.erase(hash);
  EXPECT_EQ(0u, unconfirmed.countSpentDepositsTotalAmount());
  TransactionId id = 0;
  EXPECT_FALSE(unconfirmed.findTransactionId(hash, id));
}

// A send saved while it waited for decoys was never built. On load it is failed
// and the deposit it held is spendable again.
TEST(WalletCacheLoad, UnbuiltSendReleasesItsDeposit) {
  WalletUserTransactionsCache saved;
  Deposit deposit{};
  deposit.amount = 1000;
  deposit.term = 10;
  deposit.locked = false;
  deposit.spendingTransactionId = WALLET_LEGACY_INVALID_TRANSACTION_ID;
  const DepositId depositId = saved.insertDeposit(deposit, 0, Crypto::Hash{});
  const TransactionId unbuilt = saved.addNewTransaction(0, 10, std::string(), {}, 0, {});
  saved.getDeposit(depositId).spendingTransactionId = unbuilt;

  std::stringstream stream;
  {
    Common::StdOutputStream output(stream);
    BinaryOutputStreamSerializer serializer(output);
    ASSERT_TRUE(saved.serialize(serializer));
  }
  WalletUserTransactionsCache loaded;
  {
    Common::StdInputStream input(stream);
    BinaryInputStreamSerializer serializer(input);
    ASSERT_TRUE(loaded.serialize(serializer));
  }
  EXPECT_EQ(WalletLegacyTransactionState::Failed, loaded.getTransaction(unbuilt).state);
  EXPECT_EQ(WALLET_LEGACY_INVALID_TRANSACTION_ID, loaded.getDeposit(depositId).spendingTransactionId);
}

// A refused send leaves no transaction record behind.
TEST_F(WalletInputReservation, RefusedSendLeavesNoRecord) {
  const StartedSend first = startSend();
  ASSERT_EQ(1u, first.inputs.size());
  SendTransactionContext context;
  context.selectedTransfers = spendableOutputs();
  const size_t recordsBefore = cache.getTransactionCount();
  EXPECT_THROW(sender.recordSendHoldingInputs(context, [&] {
    return cache.addNewTransaction(0, currency.minimumFee(), std::string(), {}, 0, {});
  }), std::system_error);
  EXPECT_EQ(recordsBefore, cache.getTransactionCount());
}

// A deposit spend the node dropped expires like any other send, and its deposit
// becomes spendable again instead of staying held for good.
TEST(WalletCacheExpiry, DroppedDepositSpendReleasesItsDeposit) {
  WalletUserTransactionsCache cache(0);
  Deposit deposit{};
  deposit.amount = 1000;
  deposit.spendingTransactionId = WALLET_LEGACY_INVALID_TRANSACTION_ID;
  const DepositId depositId = cache.insertDeposit(deposit, 0, Crypto::Hash{});
  const TransactionId withdraw = cache.addNewTransaction(0, 10, std::string(), {}, 0, {});
  cache.getDeposit(depositId).spendingTransactionId = withdraw;
  Crypto::Hash withdrawHash;
  withdrawHash.data[0] = 1;
  cache.addDepositSpendingTransaction(withdrawHash, UnconfirmedSpentDepositDetails{withdraw, 1000, 10});

  cache.deleteOutdatedTransactions();
  EXPECT_EQ(WalletLegacyTransactionState::Deleted, cache.getTransaction(withdraw).state);
  EXPECT_EQ(WALLET_LEGACY_INVALID_TRANSACTION_ID, cache.getDeposit(depositId).spendingTransactionId);
  EXPECT_EQ(0u, cache.countUnconfirmedSpentDepositsTotalAmount());
}
