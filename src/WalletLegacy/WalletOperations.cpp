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

#include "WalletLegacy/WalletOperations.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdint>
#include <cstring>
#include <stdexcept>

#include "CryptoNoteConfig.h"
#include "CryptoNoteCore/Currency.h"
#include "CryptoNoteCore/TransactionExtra.h"
#include "crypto/hash.h"

namespace CryptoNote {

namespace {

const uint8_t REGULAR_ALIAS_TYPE = 1;
const size_t ALIAS_LENGTH = 8;

bool isAliasCharacter(char c) {
  return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '&';
}

// cn_fast_hash(spendKey || viewKey): the raw 64-byte key preimage, not the base58
// string, so the on-chain hash resists precomputed base58 tables.
Crypto::Hash aliasAddressHash(const AccountPublicAddress& owner) {
  uint8_t preimage[64];
  memcpy(preimage, &owner.spendPublicKey, 32);
  memcpy(preimage + 32, &owner.viewPublicKey, 32);
  Crypto::Hash hash;
  Crypto::cn_fast_hash(preimage, sizeof(preimage), hash);
  return hash;
}

WalletLegacyTransfer transferTo(const std::string& address, uint64_t amount) {
  WalletLegacyTransfer transfer;
  transfer.address = address;
  transfer.amount = static_cast<int64_t>(amount);
  return transfer;
}

}

bool isValidAliasName(const std::string& alias) {
  return alias.size() == ALIAS_LENGTH && std::all_of(alias.begin(), alias.end(), isAliasCharacter);
}

WalletSendPlan planAliasRegistration(const Currency& currency, const std::string& alias,
                                     const std::string& ownerSubaddress, uint8_t nextBlockMajorVersion) {
  if (!aliasOwnerRulesApply(nextBlockMajorVersion)) {
    throw std::invalid_argument("aliases open at block major version 12");
  }
  if (!isValidAliasName(alias)) {
    throw std::invalid_argument("alias must be exactly 8 characters of [a-z0-9&]");
  }
  AccountPublicAddress owner;
  bool ownerIsSubaddress = false;
  if (!currency.parseAccountAddressString(ownerSubaddress, owner, ownerIsSubaddress)) {
    throw std::invalid_argument("alias owner is not a valid address");
  }

  TransactionExtraAliasRegistration registration;
  registration.version = 1;
  registration.alias = alias;
  registration.aliasHash = Crypto::cn_fast_hash(alias.data(), alias.size());
  registration.addressHash = aliasAddressHash(owner);
  registration.ownerAddress = ownerSubaddress;
  registration.aliasType = REGULAR_ALIAS_TYPE;
  registration.networkId = currency.getFuegoNetworkId();

  std::vector<uint8_t> extra;
  if (!registration.isValid() || !addAliasToExtra(extra, registration)) {
    throw std::invalid_argument("alias registration data is invalid");
  }

  WalletSendPlan plan;
  plan.fee = currency.minimumFee();
  if (!currency.isTestnet()) {
    addRemintBurnToExtra(extra, parameters::ALIAS_REGISTRATION_FEE);
    plan.fee += parameters::ALIAS_REGISTRATION_FEE;
  }
  plan.transfers.push_back(transferTo(ownerSubaddress, currency.minimumFee()));
  plan.extra.assign(extra.begin(), extra.end());
  return plan;
}

std::vector<WalletOrderTransaction> walletOrderHistory(IWalletLegacy& wallet) {
  std::vector<WalletOrderTransaction> history;
  for (TransactionId id = 0; id < wallet.getTransactionCount(); ++id) {
    WalletLegacyTransaction transaction;
    if (!wallet.getTransaction(id, transaction)) {
      continue;
    }
    const bool settled = transaction.blockHeight != WALLET_LEGACY_UNCONFIRMED_TRANSACTION_HEIGHT;
    const bool failed = transaction.state == WalletLegacyTransactionState::Cancelled ||
                        transaction.state == WalletLegacyTransactionState::Failed ||
                        transaction.state == WalletLegacyTransactionState::Deleted;
    history.push_back({transaction.extra, settled, failed});
  }
  return history;
}

std::vector<WalletOrderTransaction> walletOrderHistory(IWallet& wallet) {
  std::vector<WalletOrderTransaction> history;
  for (size_t id = 0; id < wallet.getTransactionCount(); ++id) {
    const WalletTransaction transaction = wallet.getTransaction(id);
    const bool settled = transaction.blockHeight != WALLET_UNCONFIRMED_TRANSACTION_HEIGHT;
    const bool failed = transaction.state == WalletTransactionState::FAILED ||
                        transaction.state == WalletTransactionState::CANCELLED ||
                        transaction.state == WalletTransactionState::DELETED;
    history.push_back({transaction.extra, settled, failed});
  }
  return history;
}

WalletSendPlan planDevDonation(const Currency& currency, const std::string& ownAddress, uint64_t amount) {
  if (amount == 0 || amount > parameters::MONEY_SUPPLY) {
    throw std::invalid_argument("dev donation amount must be positive and within the money supply");
  }
  std::vector<uint8_t> extra;
  addRemintBurnToExtra(extra, amount);
  WalletSendPlan plan;
  plan.fee = currency.minimumFee() + amount;
  plan.transfers.push_back(transferTo(ownAddress, currency.minimumFee()));
  plan.extra.assign(extra.begin(), extra.end());
  return plan;
}

bool hasPendingAliasRegistration(IWalletLegacy& wallet) {
  for (TransactionId id = 0; id < wallet.getTransactionCount(); ++id) {
    WalletLegacyTransaction transaction;
    if (!wallet.getTransaction(id, transaction) ||
        transaction.blockHeight != WALLET_LEGACY_UNCONFIRMED_TRANSACTION_HEIGHT || transaction.totalAmount >= 0 ||
        (transaction.state != WalletLegacyTransactionState::Active &&
         transaction.state != WalletLegacyTransactionState::Sending)) {
      continue;
    }
    std::vector<TransactionExtraField> fields;
    parseTransactionExtra(std::vector<uint8_t>(transaction.extra.begin(), transaction.extra.end()), fields);
    for (const auto& field : fields) {
      if (field.type() == typeid(TransactionExtraAliasRegistration)) {
        return true;
      }
    }
  }
  return false;
}

namespace {

bool parseIndex(const std::string& text, uint32_t& value) {
  if (text.empty() || !std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
    return false;
  }
  try {
    const unsigned long long parsed = std::stoull(text);
    if (parsed > UINT32_MAX) {
      return false;
    }
    value = static_cast<uint32_t>(parsed);
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

}

std::string choosePaymentSubaddressIndex(const std::vector<std::string>& args,
                                         const std::vector<std::pair<uint32_t, uint32_t>>& existing,
                                         SubaddressIndex& index) {
  index = SubaddressIndex{};
  if (args.size() == 1) {
    return "Usage: gen_new_sub [major] [minor]  (provide both or neither)";
  }
  if (args.size() > 2) {
    return "Usage: gen_new_sub [major] [minor]  (at most two indexes)";
  }
  if (args.size() == 2) {
    if (!parseIndex(args[0], index.major) || !parseIndex(args[1], index.minor)) {
      return "Usage: gen_new_sub [major] [minor]";
    }
  } else {
    index.minor = 1;
    for (const auto& used : existing) {
      if (used.first == 0 && used.second < ALIAS_MINOR_BASE && used.second >= index.minor) {
        index.minor = used.second + 1;
      }
    }
  }
  if (index.major == 0 && index.minor == 0) {
    return "Index (0,0) is the primary address. Use minor >= 1.";
  }
  if (index.major == ALIAS_SUBADDRESS_MAJOR && index.minor >= ALIAS_MINOR_BASE) {
    return "Minor indexes from 0x80000000 under major 0 are reserved for alias owners.";
  }
  return {};
}

}
