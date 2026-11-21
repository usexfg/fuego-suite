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

#pragma once

#include <memory>
#include "ITransaction.h"

namespace CryptoNote {
  class Currency;

  std::unique_ptr<ITransaction> createTransaction();
  // An outgoing tx for the chain whose top block is tipHeight: once that block is
  // v11, the tx carries a 0x09 janus base (F8).
  std::unique_ptr<ITransaction> createOutgoingTransaction(const Currency& currency, uint32_t tipHeight);
  bool janusBaseExpectedAtTip(const Currency& currency, uint32_t tipHeight);
  std::unique_ptr<ITransaction> createTransaction(const BinaryArray& transactionBlob);
  std::unique_ptr<ITransaction> createTransaction(const Transaction& tx);

  std::unique_ptr<ITransactionReader> createTransactionPrefix(const TransactionPrefix& prefix, const Crypto::Hash& transactionHash);
  std::unique_ptr<ITransactionReader> createTransactionPrefix(const Transaction& fullTransaction);
}
