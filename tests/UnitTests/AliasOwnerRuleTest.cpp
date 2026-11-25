// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// F9: from v11 an alias belongs to a funsa subaddress, and its addressHash is
// that owner's own keys, so no alias points at, or reveals, a primary address.

#include <string>

#include "gtest/gtest.h"

#include "CryptoNoteConfig.h"
#include "CryptoNoteCore/AliasOwnerRule.h"
#include "CryptoNoteCore/Currency.h"
#include "CryptoNoteCore/TransactionExtra.h"
#include "Logging/ConsoleLogger.h"
#include "crypto/crypto.h"
#include "crypto/subaddress.h"

using namespace CryptoNote;

namespace {

struct WalletKeys {
  AccountPublicAddress primary;
  AccountPublicAddress funsa;
};

WalletKeys makeWalletKeys() {
  Crypto::PublicKey spendPublic, viewPublic;
  Crypto::SecretKey spendSecret, viewSecret;
  Crypto::generate_keys(spendPublic, spendSecret);
  Crypto::generate_keys(viewPublic, viewSecret);
  const Crypto::SubAddressKeys sub = Crypto::deriveSubAddressKeysV2(viewSecret, spendPublic, &spendSecret, 0, 1);
  return {{spendPublic, viewPublic}, {sub.spendPublicKey, sub.viewPublicKey}};
}

class AliasOwnerRule : public ::testing::Test {
protected:
  AliasOwnerRule()
      : logger(Logging::ERROR),
        mainnet(CurrencyBuilder(logger).currency()),
        testnet(CurrencyBuilder(logger).testnet(true).currency()),
        keys(makeWalletKeys()) {}

  Transaction registering(const std::string& owner, const Crypto::Hash& addressHash) const {
    TransactionExtraAliasRegistration registration;
    registration.alias = "firenode";
    registration.aliasHash = Crypto::cn_fast_hash(registration.alias.data(), registration.alias.size());
    registration.addressHash = addressHash;
    registration.ownerAddress = owner;
    registration.aliasType = 1;
    Transaction tx;
    EXPECT_TRUE(addAliasToExtra(tx.extra, registration));
    return tx;
  }

  Transaction transferringTo(const std::string& newOwner) const {
    TransactionExtraAliasTransfer transfer;
    transfer.alias = "firenode";
    transfer.aliasHash = Crypto::cn_fast_hash(transfer.alias.data(), transfer.alias.size());
    transfer.oldOwnerAddress = mainnet.accountAddressAsString(keys.primary);
    transfer.newOwnerAddress = newOwner;
    Transaction tx;
    EXPECT_TRUE(addAliasTransferToExtra(tx.extra, transfer));
    return tx;
  }

  bool accepted(const Transaction& tx, uint8_t version = BLOCK_MAJOR_VERSION_11) const {
    return validateAliasOwners(tx, version, mainnet, nullptr);
  }

  Logging::ConsoleLogger logger;
  Currency mainnet;
  Currency testnet;
  WalletKeys keys;
};

TEST_F(AliasOwnerRule, FunsaOwnerWithItsOwnKeyHashIsAccepted) {
  EXPECT_TRUE(accepted(registering(mainnet.subAddressAsString(keys.funsa), aliasOwnerHash(keys.funsa))));
}

TEST_F(AliasOwnerRule, StandardOwnerIsRefusedFromV11) {
  const Transaction tx = registering(mainnet.accountAddressAsString(keys.primary), aliasOwnerHash(keys.primary));
  std::string error;
  EXPECT_FALSE(validateAliasOwners(tx, BLOCK_MAJOR_VERSION_11, mainnet, &error));
  EXPECT_NE(error.find("not a funsa subaddress"), std::string::npos);
}

TEST_F(AliasOwnerRule, HashOfThePrimaryKeysIsRefused) {
  std::string error;
  const Transaction tx = registering(mainnet.subAddressAsString(keys.funsa), aliasOwnerHash(keys.primary));
  EXPECT_FALSE(validateAliasOwners(tx, BLOCK_MAJOR_VERSION_11, mainnet, &error));
  EXPECT_NE(error.find("addressHash"), std::string::npos);
}

TEST_F(AliasOwnerRule, OwnerOfAnotherNetworkIsRefused) {
  EXPECT_FALSE(accepted(registering(testnet.subAddressAsString(keys.funsa), aliasOwnerHash(keys.funsa))));
}

TEST_F(AliasOwnerRule, BlocksBeforeV11KeepTheirRules) {
  const Transaction standardOwner = registering(mainnet.accountAddressAsString(keys.primary), Crypto::Hash{});
  EXPECT_TRUE(accepted(standardOwner, BLOCK_MAJOR_VERSION_10));
  EXPECT_FALSE(accepted(standardOwner, BLOCK_MAJOR_VERSION_11));
}

TEST_F(AliasOwnerRule, TransferMustMoveTheAliasToAFunsaOwner) {
  EXPECT_TRUE(accepted(transferringTo(mainnet.subAddressAsString(keys.funsa))));
  EXPECT_FALSE(accepted(transferringTo(mainnet.accountAddressAsString(keys.primary))));
  EXPECT_TRUE(accepted(transferringTo(mainnet.accountAddressAsString(keys.primary)), BLOCK_MAJOR_VERSION_10));
}

TEST_F(AliasOwnerRule, TransactionsWithoutAliasesAreUntouched) {
  EXPECT_TRUE(accepted(Transaction{}));
}

}
