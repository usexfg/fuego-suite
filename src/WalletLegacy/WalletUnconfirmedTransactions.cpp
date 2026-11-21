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

#include "WalletUnconfirmedTransactions.h"
#include "WalletLegacy/WalletLegacySerialization.h"

#include "CryptoNoteCore/CryptoNoteTools.h"
#include "Serialization/ISerializer.h"
#include "Serialization/SerializationOverloads.h"
#include <algorithm>
#include <iterator>

using namespace Crypto;

namespace CryptoNote {

inline TransactionOutputId getOutputId(const TransactionOutputInformation& out) {
  return std::make_pair(out.transactionPublicKey, out.outputInTransaction);
}

WalletUnconfirmedTransactions::WalletUnconfirmedTransactions(uint64_t uncofirmedTransactionsLiveTime):
  m_uncofirmedTransactionsLiveTime(uncofirmedTransactionsLiveTime) {

}

bool WalletUnconfirmedTransactions::serialize(ISerializer& s) {
  s(m_unconfirmedTxs, "transactions");
  s(m_createdDeposits, "unconfirmedCreatedDeposits");
  s(m_spentDeposits, "unconfirmedSpentDeposits");

  if (s.type() == ISerializer::INPUT) {
    collectUsedOutputs();
  }

  return true;
}

bool WalletUnconfirmedTransactions::deserializeV1(ISerializer& s) {
  s(m_unconfirmedTxs, "transactions");

  if (s.type() == ISerializer::INPUT) {
    collectUsedOutputs();
  }

  return true;
}

bool WalletUnconfirmedTransactions::findTransactionId(const Hash& hash, TransactionId& id) {
  return findUnconfirmedTransactionId(hash, id) || findUnconfirmedDepositSpendingTransactionId(hash, id);
}

bool WalletUnconfirmedTransactions::findUnconfirmedTransactionId(const Crypto::Hash& hash, TransactionId& id) {
  auto it = m_unconfirmedTxs.find(hash);
  if (it == m_unconfirmedTxs.end()) {
    return false;
  }

  id = it->second.transactionId;
  return true;
}

bool WalletUnconfirmedTransactions::findUnconfirmedDepositSpendingTransactionId(const Crypto::Hash& hash, TransactionId& id) {
  auto it = m_spentDeposits.find(hash);
  if (it == m_spentDeposits.end()) {
    return false;
  }

  id = it->second.transactionId;
  return true;
}

// A commitment spend is recorded in both maps under one hash; both entries go.
void WalletUnconfirmedTransactions::erase(const Hash& hash) {
  eraseUnconfirmedTransaction(hash);
  eraseDepositSpendingTransaction(hash);
}

bool WalletUnconfirmedTransactions::eraseUnconfirmedTransaction(const Crypto::Hash& hash) {
  auto it = m_unconfirmedTxs.find(hash);
  if (it == m_unconfirmedTxs.end()) {
    return false;
  }

  deleteUsedOutputs(it->second.usedOutputs);
  m_unconfirmedTxs.erase(it);

  return true;
}

bool WalletUnconfirmedTransactions::eraseDepositSpendingTransaction(const Crypto::Hash& hash) {
  auto it = m_spentDeposits.find(hash);
  if (it == m_spentDeposits.end()) {
    return false;
  }

  m_spentDeposits.erase(it);
  m_spentDepositsRecordedAt.erase(hash);

  return true;
}

void WalletUnconfirmedTransactions::add(const Transaction& tx, TransactionId transactionId, 
  uint64_t amount, const std::vector<TransactionOutputInformation>& usedOutputs) {

  releaseReservedOutputs(transactionId);
  UnconfirmedTransferDetails& utd = m_unconfirmedTxs[getObjectHash(tx)];

  utd.amount = amount;
  utd.sentTime = time(nullptr);
  utd.tx = tx;
  utd.transactionId = transactionId;

  uint64_t outsAmount = 0;
  // process used outputs
  utd.usedOutputs.reserve(usedOutputs.size());
  for (const auto& out : usedOutputs) {
    auto id = getOutputId(out);
    utd.usedOutputs.push_back(id);
    m_usedOutputs.insert(id);
    outsAmount += out.amount;
  }

  utd.outsAmount = outsAmount;
}

void WalletUnconfirmedTransactions::updateTransactionId(const Hash& hash, TransactionId id) {
  auto it = m_unconfirmedTxs.find(hash);
  if (it != m_unconfirmedTxs.end()) {
    it->second.transactionId = id;
  }
}

void WalletUnconfirmedTransactions::addCreatedDeposit(DepositId id, uint64_t totalAmount) {
  m_createdDeposits[id] = totalAmount;
}

void WalletUnconfirmedTransactions::addDepositSpendingTransaction(const Hash& transactionHash, const UnconfirmedSpentDepositDetails& details) {
  assert(m_spentDeposits.count(transactionHash) == 0);
  m_spentDeposits.emplace(transactionHash, details);
  m_spentDepositsRecordedAt[transactionHash] = time(nullptr);
}

// The saved file does not carry when a deposit spend was recorded; on load the
// owner supplies each spending transaction's send time.
void WalletUnconfirmedTransactions::stampSpentDeposits(const std::function<time_t(TransactionId)>& sentTimeOf) {
  m_spentDepositsRecordedAt.clear();
  for (const auto& kv : m_spentDeposits) {
    m_spentDepositsRecordedAt[kv.first] = sentTimeOf(kv.second.transactionId);
  }
}

void WalletUnconfirmedTransactions::eraseCreatedDeposit(DepositId id) {
  m_createdDeposits.erase(id);
}

uint64_t WalletUnconfirmedTransactions::countCreatedDepositsSum() const {
  uint64_t sum = 0;

  for (const auto& kv: m_createdDeposits) {
    sum += kv.second;
  }

  return sum;
}

uint64_t WalletUnconfirmedTransactions::countSpentDepositsProfit() const {
  uint64_t sum = 0;

  for (const auto& kv: m_spentDeposits) {
    sum += kv.second.depositsSum - kv.second.fee;
  }

  return sum;
}

uint64_t WalletUnconfirmedTransactions::countSpentDepositsTotalAmount() const {
  uint64_t sum = 0;

  for (const auto& kv: m_spentDeposits) {
    sum += kv.second.depositsSum;
  }

  return sum;
}

uint64_t WalletUnconfirmedTransactions::countUnconfirmedOutsAmount() const {
  uint64_t amount = 0;

  for (auto& utx: m_unconfirmedTxs)
    amount+= utx.second.outsAmount;

  return amount;
}

uint64_t WalletUnconfirmedTransactions::countUnconfirmedTransactionsAmount() const {
  uint64_t amount = 0;

  for (auto& utx: m_unconfirmedTxs)
    amount+= utx.second.amount;

  return amount;
}

bool WalletUnconfirmedTransactions::isUsed(const TransactionOutputInformation& out) const {
  const TransactionOutputId id = getOutputId(out);
  return m_usedOutputs.count(id) != 0 || m_reservedOutputs.count(id) != 0;
}

bool WalletUnconfirmedTransactions::reserveOutputs(TransactionId transactionId,
                                                   const std::vector<TransactionOutputInformation>& outputs) {
  const bool anyHeld = std::any_of(outputs.begin(), outputs.end(),
                                   [this](const TransactionOutputInformation& out) { return isUsed(out); });
  if (anyHeld) {
    return false;
  }
  for (const auto& out : outputs) {
    m_reservedOutputs.emplace(getOutputId(out), transactionId);
  }
  return true;
}

void WalletUnconfirmedTransactions::releaseReservedOutputs(TransactionId transactionId) {
  for (auto it = m_reservedOutputs.begin(); it != m_reservedOutputs.end();) {
    it = it->second == transactionId ? m_reservedOutputs.erase(it) : std::next(it);
  }
}

void WalletUnconfirmedTransactions::collectUsedOutputs() {
  UsedOutputsContainer used;
  for (const auto& kv : m_unconfirmedTxs) {
    used.insert(kv.second.usedOutputs.begin(), kv.second.usedOutputs.end());
  }
  m_usedOutputs = std::move(used);
}

void WalletUnconfirmedTransactions::reset() {
  m_unconfirmedTxs.clear();
  m_usedOutputs.clear();
  m_reservedOutputs.clear();
  m_createdDeposits.clear();
  m_spentDeposits.clear();
  m_spentDepositsRecordedAt.clear();
}

void WalletUnconfirmedTransactions::deleteUsedOutputs(const std::vector<TransactionOutputId>& usedOutputs) {
  for (const auto& output: usedOutputs) {
    m_usedOutputs.erase(output);
  }
}

std::vector<TransactionId> WalletUnconfirmedTransactions::deleteOutdatedTransactions() {
  std::vector<TransactionId> deletedTransactions;

  uint64_t now = static_cast<uint64_t>(time(nullptr));
  assert(now >= m_uncofirmedTransactionsLiveTime);

  for (auto it = m_unconfirmedTxs.begin(); it != m_unconfirmedTxs.end();) {
    if (static_cast<uint64_t>(it->second.sentTime) <= now - m_uncofirmedTransactionsLiveTime) {
      deleteUsedOutputs(it->second.usedOutputs);
      deletedTransactions.push_back(it->second.transactionId);
      it = m_unconfirmedTxs.erase(it);
    } else {
      ++it;
    }
  }

  // A deposit spend the node dropped never confirms; past the live time its
  // deposits must return to the wallet like any other expired send.
  for (auto it = m_spentDeposits.begin(); it != m_spentDeposits.end();) {
    const auto recorded = m_spentDepositsRecordedAt.find(it->first);
    const bool spendingTransactionExpired =
        std::find(deletedTransactions.begin(), deletedTransactions.end(), it->second.transactionId) != deletedTransactions.end();
    const bool expired = spendingTransactionExpired || recorded == m_spentDepositsRecordedAt.end() ||
                         static_cast<uint64_t>(recorded->second) <= now - m_uncofirmedTransactionsLiveTime;
    if (expired) {
      if (!spendingTransactionExpired) {
        deletedTransactions.push_back(it->second.transactionId);
      }
      m_spentDepositsRecordedAt.erase(it->first);
      it = m_spentDeposits.erase(it);
    } else {
      ++it;
    }
  }

  return deletedTransactions;
}

} /* namespace CryptoNote */
