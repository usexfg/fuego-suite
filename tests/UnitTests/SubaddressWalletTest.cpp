// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// funsa V2 subaddresses through WalletGreen: published address form, own-address
// checks used for change, and survival of subaddress metadata across save/load.

#include <cstdio>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include "gtest/gtest.h"

#include "CryptoNoteCore/Currency.h"
#include "INode.h"
#include "Logging/ConsoleLogger.h"
#include "System/Dispatcher.h"
#include "Wallet/WalletErrors.h"
#include "Wallet/WalletGreen.h"

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
  void getNewBlocks(std::vector<Hash>&&, std::vector<block_complete_entry>& blocks, uint32_t& startHeight, const Callback& cb) override { blocks.clear(); startHeight = 0; cb(std::error_code()); }
  void getTransactionOutsGlobalIndices(const Hash&, std::vector<uint32_t>&, const Callback& cb) override { cb(std::error_code()); }
  void queryBlocks(std::vector<Hash>&&, uint64_t, std::vector<BlockShortEntry>& blocks, uint32_t& startHeight, const Callback& cb) override { blocks.clear(); startHeight = 0; cb(std::error_code()); }
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

class SubaddressWallet : public ::testing::Test {
protected:
  SubaddressWallet()
      : logger(Logging::ERROR),
        currency(CurrencyBuilder(logger).currency()),
        path(::testing::TempDir() + "subaddress_wallet_test.wallet") {
    std::remove(path.c_str());
  }

  ~SubaddressWallet() override { std::remove(path.c_str()); }

  std::error_code changeDestinationError(IWallet& wallet, const std::string& changeDestination) {
    TransactionParameters params;
    params.destinations.push_back(WalletOrder{wallet.getAddress(0), currency.minimumFee()});
    params.changeDestination = changeDestination;
    try {
      Crypto::SecretKey transactionSecret;
      wallet.transfer(params, transactionSecret);
    } catch (const std::system_error& e) {
      return e.code();
    }
    return std::error_code();
  }

  Logging::ConsoleLogger logger;
  Currency currency;
  System::Dispatcher dispatcher;
  NullNode node;
  std::string path;
};

TEST_F(SubaddressWallet, SecondStandardAddressSurvivesReload) {
  std::string second;
  {
    WalletGreen green(dispatcher, currency, node, logger);
    IWallet& wallet = green;
    wallet.initialize(path, "password");
    wallet.createAddress();
    second = wallet.createAddress();
    wallet.save();
    wallet.shutdown();
  }
  WalletGreen green(dispatcher, currency, node, logger);
  IWallet& reloaded = green;
  reloaded.load(path, "password");
  ASSERT_EQ(reloaded.getAddressCount(), 2u);
  EXPECT_EQ(reloaded.getAddress(1), second);
  reloaded.shutdown();
}

TEST_F(SubaddressWallet, V2RecordPublishesFunsaAndSurvivesReload) {
  std::string funsa;
  {
    WalletGreen green(dispatcher, currency, node, logger);
    IWallet& wallet = green;
    wallet.initialize(path, "password");
    wallet.createAddress();
    funsa = green.createSubAddress(1, 0, 0);
    EXPECT_EQ(funsa.compare(0, 5, "funsa"), 0);
    ASSERT_EQ(wallet.getAddressCount(), 2u);
    EXPECT_EQ(wallet.getAddress(1), funsa);
    auto listed = green.listSubAddresses();
    ASSERT_EQ(listed.size(), 1u);
    EXPECT_EQ(std::get<2>(listed[0]), funsa);
    wallet.save();
    wallet.shutdown();
  }
  {
    WalletGreen green(dispatcher, currency, node, logger);
    IWallet& reloaded = green;
    reloaded.load(path, "password");
    ASSERT_EQ(reloaded.getAddressCount(), 2u);
    EXPECT_EQ(reloaded.getAddress(1), funsa);
    reloaded.shutdown();
  }
}

TEST_F(SubaddressWallet, ChangeDestinationMustUseTheSubaddressForm) {
  WalletGreen green(dispatcher, currency, node, logger);
  IWallet& wallet = green;
  wallet.initialize(path, "password");
  wallet.createAddress();
  const std::string funsa = green.createSubAddress(1, 0, 0);

  AccountPublicAddress published;
  bool isSubaddress = false;
  ASSERT_TRUE(currency.parseAccountAddressString(funsa, published, isSubaddress));
  AccountPublicAddress masterView;
  ASSERT_TRUE(currency.parseAccountAddressString(wallet.getAddress(0), masterView));
  const std::string linkableForm = currency.accountAddressAsString({published.spendPublicKey, masterView.viewPublicKey});

  EXPECT_EQ(changeDestinationError(wallet, linkableForm), make_error_code(error::CHANGE_ADDRESS_NOT_FOUND));
  EXPECT_NE(changeDestinationError(wallet, funsa), make_error_code(error::CHANGE_ADDRESS_NOT_FOUND));
  wallet.shutdown();
}

} // namespace
