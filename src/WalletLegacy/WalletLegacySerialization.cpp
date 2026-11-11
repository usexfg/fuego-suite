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

#include "WalletLegacySerialization.h"

#include "WalletLegacy/WalletDepositInfo.h"
#include "WalletLegacy/WalletUnconfirmedTransactions.h"
#include "IWalletLegacy.h"

#include "CryptoNoteCore/CryptoNoteSerialization.h"
#include "Serialization/ISerializer.h"
#include "Serialization/SerializationOverloads.h"

namespace CryptoNote {

void serialize(UnconfirmedTransferDetails& utd, CryptoNote::ISerializer& serializer) {
  serializer(utd.tx, "transaction");
  serializer(utd.amount, "amount");
  serializer(utd.outsAmount, "outs_amount");
  uint64_t time = static_cast<uint64_t>(utd.sentTime);
  serializer(time, "sent_time");
  utd.sentTime = static_cast<time_t>(time);
  uint64_t txId = static_cast<uint64_t>(utd.transactionId);
  serializer(txId, "transaction_id");
  utd.transactionId = static_cast<size_t>(txId);
}

void serialize(UnconfirmedSpentDepositDetails& details, ISerializer& serializer) {
  uint64_t txId = details.transactionId;
  serializer(txId, "spendingTransactionId");
  details.transactionId = txId;

  serializer(details.depositsSum, "depositsSum");
  serializer(details.fee, "fee");
}

void serialize(WalletLegacyTransaction& txi, CryptoNote::ISerializer& serializer) {
  uint64_t trId = static_cast<uint64_t>(txi.firstTransferId);
  serializer(trId, "first_transfer_id");
  txi.firstTransferId = static_cast<size_t>(trId);

  uint64_t trCount = static_cast<uint64_t>(txi.transferCount);
  serializer(trCount, "transfer_count");
  txi.transferCount = static_cast<size_t>(trCount);

  uint64_t dtId = static_cast<uint64_t>(txi.firstDepositId);
  serializer(dtId, "first_deposit_id");
  txi.firstDepositId = static_cast<size_t>(dtId);

  uint64_t dtCount = static_cast<uint64_t>(txi.depositCount);
  serializer(dtCount, "deposit_count");
  txi.depositCount = static_cast<size_t>(dtCount);

  serializer(txi.totalAmount, "total_amount");

  serializer(txi.fee, "fee");
  serializer(txi.hash, "hash");
  serializer(txi.isCoinbase, "is_coinbase");

  CryptoNote::serializeBlockHeight(serializer, txi.blockHeight, "block_height");

  serializer(txi.timestamp, "timestamp");
  serializer(txi.unlockTime, "unlock_time");
  serializer(txi.extra, "extra");

  uint8_t state = static_cast<uint8_t>(txi.state);
  serializer(state, "state");
  txi.state = static_cast<WalletLegacyTransactionState>(state);

  serializer(txi.messages, "messages");

  //this field has been added later in the structure.
  //in order to not break backward binary compatibility
  // we just set it to zero
  txi.sentTime = 0;
}

void serialize(WalletLegacyTransfer& tr, CryptoNote::ISerializer& serializer) {
  serializer(tr.address, "address");
  serializer(tr.amount, "amount");
}

void serialize(Deposit& deposit, CryptoNote::ISerializer& serializer) {
  uint64_t creatingTxId = static_cast<uint64_t>(deposit.creatingTransactionId);
  serializer(creatingTxId, "creating_transaction_id");
  deposit.creatingTransactionId = static_cast<size_t>(creatingTxId);

  uint64_t spendingTxIx = static_cast<uint64_t>(deposit.spendingTransactionId);
  serializer(spendingTxIx, "spending_transaction_id");
  deposit.spendingTransactionId = static_cast<size_t>(spendingTxIx);

  serializer(deposit.term, "term");
  serializer(deposit.amount, "amount");
  serializer(deposit.interest, "interest");
  serializer(deposit.locked, "locked");
}

void serialize(DepositInfo& depositInfo, CryptoNote::ISerializer& serializer) {
  serializer(depositInfo.deposit, "deposit");
  serializer(depositInfo.outputInTransaction, "output_in_transaction");
}

} //namespace CryptoNote
