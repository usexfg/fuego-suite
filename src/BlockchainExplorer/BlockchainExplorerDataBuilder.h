// Copyright (c) 2017-2022, Fuego Developers
// Copyright (c) 2012-2016, CryptoNote Developers
//
// This file is part of Fuego.
//
// Fuego is free & open source software distributed in the hope that
// it will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You may redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation. Fuego includes elements written
// by third parties. See file labeled LICENSE for more details.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.


#pragma once

#include <vector>
#include <array>

#include "CryptoNoteProtocol/ICryptoNoteProtocolQuery.h"
#include "CryptoNoteCore/ICore.h"
#include "BlockchainExplorerData.h"

namespace CryptoNote {

class BlockchainExplorerDataBuilder
{
public:
  BlockchainExplorerDataBuilder(CryptoNote::ICore& core, CryptoNote::ICryptoNoteProtocolQuery& protocol);

  BlockchainExplorerDataBuilder(const BlockchainExplorerDataBuilder&) = delete;
  BlockchainExplorerDataBuilder(BlockchainExplorerDataBuilder&&) = delete;

  BlockchainExplorerDataBuilder& operator=(const BlockchainExplorerDataBuilder&) = delete;
  BlockchainExplorerDataBuilder& operator=(BlockchainExplorerDataBuilder&&) = delete;

  bool fillBlockDetails(const Block& block, BlockDetails& blockDetails);
  bool fillTransactionDetails(const Transaction &tx, TransactionDetails& txRpcInfo, uint64_t timestamp = 0);

  static bool getPaymentId(const Transaction& transaction, Crypto::Hash& paymentId);

private:
  bool getMixin(const Transaction& transaction, uint64_t& mixin);
  bool fillTxExtra(const std::vector<uint8_t>& rawExtra, TransactionExtraDetails& extraDetails);
  size_t median(std::vector<size_t>& v);

  CryptoNote::ICore& core;
  CryptoNote::ICryptoNoteProtocolQuery& protocol;
};
}
