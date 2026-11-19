// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// WalletLegacy input reservation: a send holds the inputs it selected until it is
// built, sent or fails, so sends started back to back never pick the same input.

#include <deque>
#include <memory>
#include <vector>

#include "gtest/gtest.h"

#include "CryptoNoteCore/Account.h"
#include "CryptoNoteCore/CryptoNoteFormatUtils.h"
#include "CryptoNoteCore/Currency.h"
#include "CryptoNoteCore/TransactionApi.h"
#include "Logging/ConsoleLogger.h"
#include "NullNode.h"
#include "Transfers/TransfersContainer.h"
#include "WalletLegacy/WalletLegacyEvent.h"
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
