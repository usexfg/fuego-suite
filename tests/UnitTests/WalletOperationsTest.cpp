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

#include <stdexcept>
#include <string>
#include <vector>

#include "gtest/gtest.h"

#include "CryptoNoteConfig.h"
#include "CryptoNoteCore/Currency.h"
#include "CryptoNoteCore/TransactionExtra.h"
#include "Logging/ConsoleLogger.h"
#include "WalletLegacy/WalletOperations.h"
#include "crypto/crypto.h"

using namespace CryptoNote;

namespace {

AccountPublicAddress randomAddress() {
  AccountPublicAddress address;
  Crypto::SecretKey unused;
  Crypto::generate_keys(address.spendPublicKey, unused);
  Crypto::generate_keys(address.viewPublicKey, unused);
  return address;
}

std::vector<TransactionExtraField> extraFieldsOf(const WalletSendPlan& plan) {
  std::vector<TransactionExtraField> fields;
  EXPECT_TRUE(parseTransactionExtra(std::vector<uint8_t>(plan.extra.begin(), plan.extra.end()), fields));
  return fields;
}

template <typename Field>
const Field* findField(const std::vector<TransactionExtraField>& fields) {
  for (const auto& field : fields) {
    if (field.type() == typeid(Field)) {
      return &boost::get<Field>(field);
    }
  }
  return nullptr;
}

class MainnetWalletOperations : public ::testing::Test {
protected:
  MainnetWalletOperations()
      : currency(CurrencyBuilder(logger).currency()),
        subaddress(currency.subAddressAsString(randomAddress())),
        masterAddress(currency.accountAddressAsString(randomAddress())) {}

  Logging::ConsoleLogger logger;
  Currency currency;
  const std::string subaddress;
  const std::string masterAddress;
};

TEST_F(MainnetWalletOperations, AliasFromV12BurnsTheRegistrationFee) {
  const WalletSendPlan plan = planAliasRegistration(currency, "firenode", subaddress, BLOCK_MAJOR_VERSION_12);
  EXPECT_EQ(currency.minimumFee() + parameters::ALIAS_REGISTRATION_FEE, plan.fee);
  ASSERT_EQ(1u, plan.transfers.size());
  EXPECT_EQ(subaddress, plan.transfers[0].address);
  EXPECT_EQ(static_cast<int64_t>(currency.minimumFee()), plan.transfers[0].amount);

  const auto fields = extraFieldsOf(plan);
  const auto* burn = findField<TransactionExtraRemintBurn>(fields);
  ASSERT_NE(nullptr, burn);
  EXPECT_EQ(parameters::ALIAS_REGISTRATION_FEE, burn->amountXfg);
  const auto* registration = findField<TransactionExtraAliasRegistration>(fields);
  ASSERT_NE(nullptr, registration);
  EXPECT_EQ("firenode", registration->alias);
  EXPECT_EQ(subaddress, registration->ownerAddress);
  AccountPublicAddress owner;
  bool ownerIsSubaddress = false;
  ASSERT_TRUE(currency.parseAccountAddressString(subaddress, owner, ownerIsSubaddress));
  uint8_t preimage[64];
  memcpy(preimage, &owner.spendPublicKey, 32);
  memcpy(preimage + 32, &owner.viewPublicKey, 32);
  EXPECT_EQ(Crypto::cn_fast_hash(preimage, sizeof(preimage)), registration->addressHash)
      << "the owner hash is the published subaddress's key hash";
}

TEST_F(MainnetWalletOperations, AliasBeforeV12IsRefused) {
  for (uint8_t version : {BLOCK_MAJOR_VERSION_10, BLOCK_MAJOR_VERSION_11}) {
    EXPECT_THROW(planAliasRegistration(currency, "firenode", subaddress, version), std::invalid_argument);
    EXPECT_THROW(planAliasRegistration(currency, "firenode", masterAddress, version), std::invalid_argument);
  }
}

TEST_F(MainnetWalletOperations, AliasNamesFollowTheConsensusRule) {
  EXPECT_TRUE(isValidAliasName("fire&n0d"));
  EXPECT_FALSE(isValidAliasName("Firenode"));
  EXPECT_FALSE(isValidAliasName("firenod"));
  EXPECT_FALSE(isValidAliasName("firenodes"));
  EXPECT_THROW(planAliasRegistration(currency, "fire-nod", subaddress, BLOCK_MAJOR_VERSION_12), std::invalid_argument);
}

TEST_F(MainnetWalletOperations, DevDonationBurnsTheAmountOnTopOfTheFee) {
  const uint64_t amount = 25 * parameters::COIN;
  const WalletSendPlan plan = planDevDonation(currency, masterAddress, amount);
  EXPECT_EQ(currency.minimumFee() + amount, plan.fee);
  ASSERT_EQ(1u, plan.transfers.size());
  EXPECT_EQ(masterAddress, plan.transfers[0].address);
  const auto fields = extraFieldsOf(plan);
  const auto* burn = findField<TransactionExtraRemintBurn>(fields);
  ASSERT_NE(nullptr, burn);
  EXPECT_EQ(amount, burn->amountXfg);
}

TEST_F(MainnetWalletOperations, DevDonationRefusesAmountsOutsideTheMoneySupply) {
  EXPECT_THROW(planDevDonation(currency, masterAddress, 0), std::invalid_argument);
  EXPECT_THROW(planDevDonation(currency, masterAddress, parameters::MONEY_SUPPLY + 1), std::invalid_argument);
  EXPECT_THROW(planDevDonation(currency, masterAddress, UINT64_MAX), std::invalid_argument);
}

TEST(PaymentSubaddressIndex, ExplicitIndexIsKept) {
  SubaddressIndex index;
  EXPECT_EQ("", choosePaymentSubaddressIndex({"2", "5"}, {{0, 1}, {0, 2}}, index));
  EXPECT_EQ(2u, index.major);
  EXPECT_EQ(5u, index.minor);
}

TEST(PaymentSubaddressIndex, NoArgumentsTakesTheNextMinorUnderMajorZero) {
  SubaddressIndex index;
  EXPECT_EQ("", choosePaymentSubaddressIndex({}, {{0, 1}, {0, 4}, {2, 9}, {1, 7}}, index));
  EXPECT_EQ(0u, index.major);
  EXPECT_EQ(5u, index.minor);
}

TEST(PaymentSubaddressIndex, RefusesThePrimaryAndBadArguments) {
  SubaddressIndex index;
  EXPECT_NE("", choosePaymentSubaddressIndex({"0", "0"}, {}, index));
  EXPECT_NE("", choosePaymentSubaddressIndex({"2"}, {}, index));
  EXPECT_NE("", choosePaymentSubaddressIndex({"-1", "2"}, {}, index));
  EXPECT_NE("", choosePaymentSubaddressIndex({"4294967296", "2"}, {}, index));
  EXPECT_NE("", choosePaymentSubaddressIndex({}, {{0, ALIAS_MINOR_BASE - 1}}, index));
}

TEST(PaymentSubaddressIndex, NeverReachesTheAliasOwnerRange) {
  SubaddressIndex index;
  EXPECT_NE("", choosePaymentSubaddressIndex({"0", "2147483648"}, {}, index));
  EXPECT_NE("", choosePaymentSubaddressIndex({"0", "4294967295"}, {}, index));
  EXPECT_EQ("", choosePaymentSubaddressIndex({"0", "2147483647"}, {}, index));
  EXPECT_EQ("", choosePaymentSubaddressIndex({"1", "2147483648"}, {}, index));
  EXPECT_EQ("", choosePaymentSubaddressIndex({}, {{0, 1}, {0, ALIAS_MINOR_BASE}, {0, ALIAS_MINOR_BASE + 3}}, index));
  EXPECT_EQ(0u, index.major);
  EXPECT_EQ(2u, index.minor);
}

}
