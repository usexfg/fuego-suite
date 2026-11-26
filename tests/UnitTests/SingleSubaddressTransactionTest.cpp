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

#include <memory>
#include <stdexcept>

#include "gtest/gtest.h"

#include "CryptoNoteConfig.h"
#include "CryptoNoteCore/Account.h"
#include "CryptoNoteCore/SubaddressTx.h"
#include "CryptoNoteCore/TransactionApi.h"
#include "CryptoNoteCore/TransactionExtra.h"
#include "ITransaction.h"
#include "crypto/subaddress.h"

using namespace CryptoNote;
using Crypto::PublicKey;
using Crypto::SecretKey;

namespace {

AccountKeys newAccount() {
  AccountBase account;
  account.generate();
  return account.getAccountKeys();
}

class SingleSubaddressTransaction : public ::testing::Test {
protected:
  SingleSubaddressTransaction()
      : sender(newAccount()),
        recipient(newAccount()),
        subaddressKeys(Crypto::deriveSubAddressKeysV2(recipient.viewSecretKey, recipient.address.spendPublicKey,
                                                      &recipient.spendSecretKey, 0, 1)),
        subaddress{subaddressKeys.spendPublicKey, subaddressKeys.viewPublicKey},
        transaction(createTransaction()) {}

  PublicKey outputKey(size_t index) const {
    KeyOutput out;
    uint64_t amount = 0;
    transaction->getOutput(index, out, amount);
    return out.key;
  }

  // What a wallet scanning with this view secret derives for `index` under `spendKey`.
  PublicKey scannedKey(const SecretKey& viewSecret, size_t index, const PublicKey& spendKey) const {
    Crypto::KeyDerivation derivation;
    PublicKey key;
    EXPECT_TRUE(Crypto::generate_key_derivation(transaction->getTransactionPublicKey(), viewSecret, derivation));
    EXPECT_TRUE(Crypto::derive_public_key(derivation, index, spendKey, key));
    return key;
  }

  AccountKeys sender;
  AccountKeys recipient;
  Crypto::SubAddressKeys subaddressKeys;
  AccountPublicAddress subaddress;
  std::unique_ptr<ITransaction> transaction;
};

TEST_F(SingleSubaddressTransaction, PublishesOneTxKeyAndOneJanusBase) {
  const PublicKey originalTxKey = transaction->getTransactionPublicKey();
  transaction->sendToOneSubaddress(subaddress, sender);
  transaction->addOutput(5 * parameters::COIN, subaddress, true);
  transaction->addOutput(2 * parameters::COIN, sender.address);

  const auto extra = transaction->getExtra();
  const SubaddressTxKeys keys = readSubaddressTxKeys(std::vector<uint8_t>(extra.begin(), extra.end()));
  EXPECT_TRUE(keys.additionalPubKeys.empty());
  ASSERT_EQ(1u, keys.janusBases.size());
  EXPECT_EQ(originalTxKey, keys.janusBases.front());
  EXPECT_NE(originalTxKey, transaction->getTransactionPublicKey());
  EXPECT_TRUE(Crypto::janusCheck(transaction->getTransactionPublicKey(), keys.janusBases.front(),
                                 subaddressKeys.spendSecretKey));
}

TEST_F(SingleSubaddressTransaction, RecipientAndSenderEachFindTheirOutput) {
  transaction->sendToOneSubaddress(subaddress, sender);
  transaction->addOutput(5 * parameters::COIN, subaddress, true);
  transaction->addOutput(2 * parameters::COIN, sender.address);

  EXPECT_EQ(outputKey(0), scannedKey(recipient.viewSecretKey, 0, subaddress.spendPublicKey));
  EXPECT_EQ(outputKey(1), scannedKey(sender.viewSecretKey, 1, sender.address.spendPublicKey));
  EXPECT_NE(outputKey(0), scannedKey(sender.viewSecretKey, 0, subaddress.spendPublicKey));
}

TEST_F(SingleSubaddressTransaction, CommitmentOutputsTakeTheSameDerivations) {
  transaction->sendToOneSubaddress(subaddress, sender);
  Crypto::KeyDerivation expected;
  ASSERT_TRUE(Crypto::generate_key_derivation(transaction->getTransactionPublicKey(), sender.viewSecretKey, expected));
  const Crypto::KeyDerivation change = transaction->outputDerivation(sender.address, false);
  EXPECT_EQ(0, memcmp(&expected, &change, sizeof(expected)));
  ASSERT_TRUE(Crypto::generate_key_derivation(transaction->getTransactionPublicKey(), recipient.viewSecretKey, expected));
  const Crypto::KeyDerivation payment = transaction->outputDerivation(subaddress, true);
  EXPECT_EQ(0, memcmp(&expected, &payment, sizeof(expected)));
}

TEST_F(SingleSubaddressTransaction, RefusesAnyOtherDestination) {
  transaction->sendToOneSubaddress(subaddress, sender);
  const AccountKeys thirdParty = newAccount();
  EXPECT_THROW(transaction->addOutput(parameters::COIN, thirdParty.address), std::logic_error);
  const Crypto::SubAddressKeys other = Crypto::deriveSubAddressKeysV2(
      recipient.viewSecretKey, recipient.address.spendPublicKey, &recipient.spendSecretKey, 0, 2);
  EXPECT_THROW(transaction->addOutput(parameters::COIN, AccountPublicAddress{other.spendPublicKey, other.viewPublicKey}, true),
               std::logic_error);
  EXPECT_THROW(transaction->outputDerivation(thirdParty.address, false), std::logic_error);
}

TEST_F(SingleSubaddressTransaction, IsChosenBeforeTheFirstOutput) {
  transaction->addOutput(parameters::COIN, sender.address);
  EXPECT_THROW(transaction->sendToOneSubaddress(subaddress, sender), std::logic_error);
}

TEST(MainKeyTransaction, RefusesASubaddressDerivation) {
  const AccountKeys account = newAccount();
  std::unique_ptr<ITransaction> transaction = createTransaction();
  EXPECT_THROW(transaction->outputDerivation(account.address, true), std::logic_error);
  Crypto::KeyDerivation expected;
  SecretKey txSecret;
  ASSERT_TRUE(transaction->getTransactionSecretKey(txSecret));
  ASSERT_TRUE(Crypto::generate_key_derivation(account.address.viewPublicKey, txSecret, expected));
  const Crypto::KeyDerivation derivation = transaction->outputDerivation(account.address, false);
  EXPECT_EQ(0, memcmp(&expected, &derivation, sizeof(expected)));
}

}
