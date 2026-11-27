// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// funsa V2 subaddresses through WalletGreen: published address form, own-address
// checks used for change, and survival of subaddress metadata across save/load.

#include <cstdio>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#include "gtest/gtest.h"

#include "CryptoNoteCore/Currency.h"
#include "INode.h"
#include "NullNode.h"
#include "Logging/ConsoleLogger.h"
#include "Common/FileMappedVector.h"
#include "System/Dispatcher.h"
#include "Wallet/WalletErrors.h"
#include "Wallet/WalletGreen.h"
#include "Wallet/WalletRecordCrypto.h"
#include "Wallet/WalletSerializationV2.h"
#include "crypto/hash.h"

using namespace CryptoNote;
using namespace Crypto;

namespace {

class SubaddressWallet : public ::testing::Test {
protected:
  SubaddressWallet()
      : logger(Logging::ERROR),
        currency(CurrencyBuilder(logger).currency()),
        path(::testing::TempDir() + "subaddress_wallet_test.wallet") {
    removeWalletFiles();
  }

  ~SubaddressWallet() override { removeWalletFiles(); }

  void removeWalletFiles() {
    for (const std::string& file : {path, path + ".v9", path + ".v9.v9.backup", path + ".v9.backup"}) {
      std::remove(file.c_str());
    }
  }

  struct CreatedWallet {
    std::string funsa;
    std::string standard;
  };

  // Primary address, one standard address and one funsa subaddress, saved in full.
  CreatedWallet createWalletWithSubaddress() {
    CreatedWallet created;
    WalletGreen green(dispatcher, currency, node, logger);
    IWallet& wallet = green;
    wallet.initialize(path, password);
    wallet.createAddress();
    created.funsa = green.createSubAddress(1, 7);
    created.standard = wallet.createAddress();
    wallet.save();
    wallet.shutdown();
    return created;
  }

  std::vector<std::string> reloadedAddresses(const std::string& walletPath) {
    WalletGreen green(dispatcher, currency, node, logger);
    IWallet& wallet = green;
    wallet.load(walletPath, password);
    std::vector<std::string> addresses;
    for (size_t i = 0; i < wallet.getAddressCount(); ++i) {
      addresses.push_back(wallet.getAddress(i));
    }
    wallet.shutdown();
    return addresses;
  }

  Crypto::chacha8_key fileKey() {
    Crypto::cn_context context;
    Crypto::chacha8_key key;
    Crypto::generate_chacha8_key(context, password, key);
    return key;
  }

  using AddressRecords = Common::FileMappedVector<EncryptedAddressRecord>;
  using KeyPairRecords = Common::FileMappedVector<EncryptedWalletRecord>;
  using Prefix = ContainerStoragePrefix;

  void dropCache(const std::string& walletPath) {
    AddressRecords records(walletPath, Common::FileMappedVectorOpenMode::OPEN, sizeof(Prefix));
    records.resizeSuffix(0);
  }

  void corruptCache(const std::string& walletPath) {
    AddressRecords records(walletPath, Common::FileMappedVectorOpenMode::OPEN, sizeof(Prefix));
    std::memset(records.suffix(), 0x5a, records.suffixSize());
    records.flush();
  }

  // Rewrites a version 10 file as the version 9 layout: key pairs only, same cache.
  void downgradeToVersion9(const std::string& from, const std::string& to) {
    const Crypto::chacha8_key key = fileKey();
    AddressRecords current(from, Common::FileMappedVectorOpenMode::OPEN, sizeof(Prefix));
    KeyPairRecords legacy(to, Common::FileMappedVectorOpenMode::CREATE, sizeof(Prefix));
    Prefix prefix = *reinterpret_cast<const Prefix*>(current.prefix());
    prefix.version = 9;
    for (size_t i = 0; i < current.size(); ++i) {
      const StoredAddress address = openAddress(current[i], key);
      legacy.push_back(sealKeyPair(address.keys, key, prefix.nextIv));
      advanceIv(prefix.nextIv);
    }
    *reinterpret_cast<Prefix*>(legacy.prefix()) = prefix;
    legacy.resizeSuffix(current.suffixSize());
    std::copy(current.suffix(), current.suffix() + current.suffixSize(), legacy.suffix());
    legacy.flush();
  }

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
  const std::string password = "password";
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

// Deleting an address removes its own stored keys, not another address's.
TEST_F(SubaddressWallet, DeletingAMiddleAddressKeepsTheOthersKeys) {
  std::string first;
  std::string last;
  {
    WalletGreen green(dispatcher, currency, node, logger);
    IWallet& wallet = green;
    wallet.initialize(path, "password");
    first = wallet.createAddress();
    const std::string middle = wallet.createAddress();
    last = wallet.createAddress();
    wallet.deleteAddress(middle);
    ASSERT_EQ(wallet.getAddressCount(), 2u);
    wallet.save();
    wallet.shutdown();
  }
  WalletGreen green(dispatcher, currency, node, logger);
  IWallet& reloaded = green;
  reloaded.load(path, "password");
  ASSERT_EQ(reloaded.getAddressCount(), 2u);
  EXPECT_EQ(reloaded.getAddress(0), first);
  EXPECT_EQ(reloaded.getAddress(1), last);
  reloaded.shutdown();
}

TEST_F(SubaddressWallet, V2RecordPublishesFunsaAndSurvivesReload) {
  std::string funsa;
  {
    WalletGreen green(dispatcher, currency, node, logger);
    IWallet& wallet = green;
    wallet.initialize(path, "password");
    wallet.createAddress();
    funsa = green.createSubAddress(1, 0);
    EXPECT_EQ(funsa.compare(0, 5, "funsa"), 0);
    ASSERT_EQ(wallet.getAddressCount(), 2u);
    EXPECT_EQ(wallet.getAddress(1), funsa);
    const auto listed = wallet.getSubAddresses();
    ASSERT_EQ(listed.size(), 1u);
    EXPECT_EQ(listed[0].major, 1u);
    EXPECT_EQ(listed[0].minor, 0u);
    EXPECT_EQ(listed[0].address, funsa);
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
  const std::string funsa = green.createSubAddress(1, 0);

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

TEST_F(SubaddressWallet, SubaddressKindSurvivesAKeysOnlySave) {
  const CreatedWallet created = createWalletWithSubaddress();
  {
    WalletGreen green(dispatcher, currency, node, logger);
    IWallet& wallet = green;
    wallet.load(path, password);
    wallet.save(WalletSaveLevel::SAVE_KEYS_ONLY);
    wallet.shutdown();
  }
  const auto addresses = reloadedAddresses(path);
  ASSERT_EQ(addresses.size(), 3u);
  EXPECT_EQ(addresses[1], created.funsa);
  EXPECT_EQ(addresses[2], created.standard);
}

TEST_F(SubaddressWallet, SubaddressKindSurvivesALostCache) {
  const CreatedWallet created = createWalletWithSubaddress();
  dropCache(path);
  const auto addresses = reloadedAddresses(path);
  ASSERT_EQ(addresses.size(), 3u);
  EXPECT_EQ(addresses[1], created.funsa);
}

TEST_F(SubaddressWallet, SubaddressKindSurvivesACorruptedCache) {
  const CreatedWallet created = createWalletWithSubaddress();
  corruptCache(path);
  const auto addresses = reloadedAddresses(path);
  ASSERT_EQ(addresses.size(), 3u);
  EXPECT_EQ(addresses[1], created.funsa);
}

TEST_F(SubaddressWallet, Version9FileIsUpgradedAndRecordsTheKindItsCacheHolds) {
  const CreatedWallet created = createWalletWithSubaddress();
  const std::string version9 = path + ".v9";
  downgradeToVersion9(path, version9);

  const auto upgraded = reloadedAddresses(version9);
  ASSERT_EQ(upgraded.size(), 3u);
  EXPECT_EQ(upgraded[1], created.funsa);
  EXPECT_EQ(upgraded[2], created.standard);
  std::FILE* backup = std::fopen((version9 + ".v9.backup").c_str(), "rb");
  EXPECT_NE(backup, nullptr);
  if (backup != nullptr) std::fclose(backup);

  dropCache(version9);
  const auto recorded = reloadedAddresses(version9);
  ASSERT_EQ(recorded.size(), 3u);
  EXPECT_EQ(recorded[1], created.funsa);
}

TEST_F(SubaddressWallet, Version9FileWithoutACacheLoadsItsAddressesAsStandard) {
  const CreatedWallet created = createWalletWithSubaddress();
  const std::string version9 = path + ".v9";
  downgradeToVersion9(path, version9);
  {
    KeyPairRecords legacy(version9, Common::FileMappedVectorOpenMode::OPEN, sizeof(Prefix));
    legacy.resizeSuffix(0);
  }
  const auto addresses = reloadedAddresses(version9);
  ASSERT_EQ(addresses.size(), 3u);
  EXPECT_NE(addresses[1], created.funsa) << "the kind is unknown without the cache, as before version 10";
  EXPECT_EQ(addresses[1].compare(0, 4, "fire"), 0);
}

TEST_F(SubaddressWallet, SubaddressRecordWithAnotherIndexIsRefused) {
  createWalletWithSubaddress();
  {
    const Crypto::chacha8_key key = fileKey();
    AddressRecords records(path, Common::FileMappedVectorOpenMode::OPEN, sizeof(Prefix));
    StoredAddress subaddress = openAddress(records[1], key);
    ASSERT_EQ(subaddress.kind, StoredAddressKind::SubaddressV2);
    subaddress.subaddrMinor += 1;
    auto* prefix = reinterpret_cast<Prefix*>(records.prefix());
    records[1] = sealAddress(subaddress, key, prefix->nextIv);
    advanceIv(prefix->nextIv);
    records.flush();
  }
  WalletGreen green(dispatcher, currency, node, logger);
  IWallet& wallet = green;
  EXPECT_THROW(wallet.load(path, password), std::system_error);
}

TEST_F(SubaddressWallet, ResetReloadsAPasswordProtectedWallet) {
  const CreatedWallet created = createWalletWithSubaddress();
  {
    WalletGreen green(dispatcher, currency, node, logger);
    IWallet& wallet = green;
    wallet.load(path, password);
    ASSERT_NO_THROW(wallet.reset(0));
    ASSERT_EQ(wallet.getAddressCount(), 3u);
    EXPECT_EQ(wallet.getAddress(1), created.funsa);
    wallet.shutdown();
  }
}

TEST_F(SubaddressWallet, EveryRecordAndTheCacheUseTheirOwnIv) {
  createWalletWithSubaddress();
  {
    WalletGreen green(dispatcher, currency, node, logger);
    IWallet& wallet = green;
    wallet.load(path, password);
    wallet.reset(0);
    wallet.save();
    wallet.shutdown();
  }
  AddressRecords records(path, Common::FileMappedVectorOpenMode::OPEN, sizeof(Prefix));
  const auto* prefix = reinterpret_cast<const Prefix*>(records.prefix());
  std::set<uint64_t> ivs;
  auto distinct = [&ivs](const Crypto::chacha8_iv& iv) {
    uint64_t counter;
    std::memcpy(&counter, &iv, sizeof(counter));
    return ivs.insert(counter).second;
  };
  EXPECT_TRUE(distinct(prefix->encryptedViewKeys.iv));
  for (size_t i = 0; i < records.size(); ++i) {
    EXPECT_TRUE(distinct(records[i].iv)) << "record " << i;
  }
  ASSERT_GE(records.suffixSize(), sizeof(Crypto::chacha8_iv));
  Crypto::chacha8_iv cacheIv;
  std::memcpy(&cacheIv, records.suffix(), sizeof(cacheIv));
  EXPECT_TRUE(distinct(cacheIv)) << "cache";
}

} // namespace

namespace {

TEST_F(SubaddressWallet, AliasRegistrationIsRefusedBeforeV11) {
  WalletGreen green(dispatcher, currency, node, logger);
  IWallet& wallet = green;
  wallet.initialize(path, "password");
  wallet.createAddress();
  Crypto::Hash txHash{};
  EXPECT_EQ(green.registerAlias("firenode", wallet.getAddress(0), txHash), make_error_code(std::errc::invalid_argument));
  EXPECT_TRUE(green.getSubAddresses().empty());
  wallet.shutdown();
}

}
