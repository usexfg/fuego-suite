// Copyright (c) 2017-2026 Fuego Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "AliasOwnerRule.h"

#include <cstring>
#include <vector>

#include "CryptoNoteConfig.h"
#include "Currency.h"
#include "TransactionExtra.h"

namespace CryptoNote {

namespace {

bool refuse(std::string* error, const std::string& reason) {
  if (error) *error = reason;
  return false;
}

bool parseFunsaOwner(const Currency& currency, const std::string& owner, AccountPublicAddress& address) {
  bool isSubaddress = false;
  return currency.parseAccountAddressString(owner, address, isSubaddress) && isSubaddress;
}

bool registrationNamesFunsaOwner(const TransactionExtraAliasRegistration& registration,
                                 const Currency& currency, std::string* error) {
  AccountPublicAddress owner;
  if (!parseFunsaOwner(currency, registration.ownerAddress, owner)) {
    return refuse(error, "alias @" + registration.alias + " owner is not a funsa subaddress of this network");
  }
  if (registration.addressHash != aliasOwnerHash(owner)) {
    return refuse(error, "alias @" + registration.alias + " addressHash does not match its owner's keys");
  }
  return true;
}

bool transferNamesFunsaOwner(const TransactionExtraAliasTransfer& transfer,
                             const Currency& currency, std::string* error) {
  AccountPublicAddress newOwner;
  if (!parseFunsaOwner(currency, transfer.newOwnerAddress, newOwner)) {
    return refuse(error, "alias @" + transfer.alias + " new owner is not a funsa subaddress of this network");
  }
  return true;
}

}

Crypto::Hash aliasOwnerHash(const AccountPublicAddress& owner) {
  uint8_t preimage[sizeof(owner.spendPublicKey) + sizeof(owner.viewPublicKey)];
  std::memcpy(preimage, &owner.spendPublicKey, sizeof(owner.spendPublicKey));
  std::memcpy(preimage + sizeof(owner.spendPublicKey), &owner.viewPublicKey, sizeof(owner.viewPublicKey));
  return Crypto::cn_fast_hash(preimage, sizeof(preimage));
}

bool validateAliasOwners(const Transaction& tx, uint8_t blockMajorVersion,
                         const Currency& currency, std::string* error) {
  if (!aliasTagsRefused(blockMajorVersion) && !aliasOwnerRulesApply(blockMajorVersion)) {
    return true;
  }
  std::vector<TransactionExtraField> fields;
  if (!parseTransactionExtra(tx.extra, fields)) {
    return true;
  }
  for (const auto& field : fields) {
    const bool isRegistration = field.type() == typeid(TransactionExtraAliasRegistration);
    const bool isTransfer = field.type() == typeid(TransactionExtraAliasTransfer);
    if (aliasTagsRefused(blockMajorVersion) &&
        (isRegistration || isTransfer || field.type() == typeid(TransactionExtraAliasRelease))) {
      return refuse(error, "alias tags are not accepted before v12");
    }
    if (isRegistration) {
      if (!registrationNamesFunsaOwner(boost::get<TransactionExtraAliasRegistration>(field), currency, error)) {
        return false;
      }
    } else if (isTransfer) {
      if (!transferNamesFunsaOwner(boost::get<TransactionExtraAliasTransfer>(field), currency, error)) {
        return false;
      }
    }
  }
  return true;
}

}
