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
#include "NullNode.h"
#include "Logging/ConsoleLogger.h"
#include "System/Dispatcher.h"
#include "Wallet/WalletErrors.h"
#include "Wallet/WalletGreen.h"

using namespace CryptoNote;
using namespace Crypto;

namespace {

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
