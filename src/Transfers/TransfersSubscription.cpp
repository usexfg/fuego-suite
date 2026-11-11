// Copyright (c) 2017-2022 Fuego Developers
// Copyright (c) 2018-2019 Conceal Network & Conceal Devs
// Copyright (c) 2016-2019 The Karbowanec developers
// Copyright (c) 2012-2018 The CryptoNote developers
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

#include "TransfersSubscription.h"
#include "IWalletLegacy.h"

using namespace Crypto;

namespace CryptoNote {

TransfersSubscription::TransfersSubscription(const CryptoNote::Currency& currency, const AccountSubscription& sub)
  : subscription(sub), transfers(currency, sub.transactionSpendableAge) {}


SynchronizationStart TransfersSubscription::getSyncStart() {
  return subscription.syncStart;
}

void TransfersSubscription::onBlockchainDetach(uint32_t height) {
  std::vector<Hash> deletedTransactions;
  std::vector<TransactionOutputInformation> lockedTransfers;
  transfers.detach(height, deletedTransactions, lockedTransfers);

  for (auto& hash : deletedTransactions) {
    m_observerManager.notify(&ITransfersObserver::onTransactionDeleted, this, hash);
  }

  if (!lockedTransfers.empty()) {
    m_observerManager.notify(&ITransfersObserver::onTransfersLocked, this, lockedTransfers);
  }
}

void TransfersSubscription::onError(const std::error_code& ec, uint32_t height) {
  if (height != WALLET_LEGACY_UNCONFIRMED_TRANSACTION_HEIGHT) {
    onBlockchainDetach(height);
  }
  m_observerManager.notify(&ITransfersObserver::onError, this, height, ec);
}

bool TransfersSubscription::advanceHeight(uint32_t height) {
  std::vector<TransactionOutputInformation> unlockedTransfers = transfers.advanceHeight(height);

  if (!unlockedTransfers.empty()) {
    m_observerManager.notify(&ITransfersObserver::onTransfersUnlocked, this, unlockedTransfers);
  }

  return true;
}

const AccountKeys& TransfersSubscription::getKeys() const {
  return subscription.keys;
}

bool TransfersSubscription::addTransaction(const TransactionBlockInfo& blockInfo, const ITransactionReader& tx,
                                           const std::vector<TransactionOutputInformationIn>& transfersList,
                                           std::vector<std::string>&& messages) {
  std::vector<TransactionOutputInformation> unlockedTransfers;

  bool added = transfers.addTransaction(blockInfo, tx, transfersList, std::move(messages), &unlockedTransfers);
  if (added) {
    m_observerManager.notify(&ITransfersObserver::onTransactionUpdated, this, tx.getTransactionHash());
  }

  if (!unlockedTransfers.empty()) {
    m_observerManager.notify(&ITransfersObserver::onTransfersUnlocked, this, unlockedTransfers);
  }

  return added;
}

AccountPublicAddress TransfersSubscription::getAddress() {
  return subscription.keys.address;
}

ITransfersContainer& TransfersSubscription::getContainer() {
  return transfers;
}

void TransfersSubscription::deleteUnconfirmedTransaction(const Hash& transactionHash) {
  if (transfers.deleteUnconfirmedTransaction(transactionHash)) {
    m_observerManager.notify(&ITransfersObserver::onTransactionDeleted, this, transactionHash);
  }
}

void TransfersSubscription::markTransactionConfirmed(const TransactionBlockInfo& block, const Hash& transactionHash,
                                                     const std::vector<uint32_t>& globalIndices) {
  transfers.markTransactionConfirmed(block, transactionHash, globalIndices);
  m_observerManager.notify(&ITransfersObserver::onTransactionUpdated, this, transactionHash);
}

}
