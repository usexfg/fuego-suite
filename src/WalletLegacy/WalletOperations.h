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

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "CryptoNoteCore/TransactionExtra.h"
#include "IWalletLegacy.h"

namespace CryptoNote {

class Currency;

// What a wallet command sends: the transfers, the fee and the extra, built once
// so the console, the wallet RPC and the tests send the same transaction.
struct WalletSendPlan {
  std::vector<WalletLegacyTransfer> transfers;
  uint64_t fee = 0;
  std::string extra;
};

// An alias is exactly 8 characters of [a-z0-9&].
// Alias owners are the subaddresses (ALIAS_SUBADDRESS_MAJOR, 1..), apart from the
// payment subaddresses under major 0. A restored wallet does not recreate them
// by itself; they can be found by recreating (ALIAS_SUBADDRESS_MAJOR, 1..) and
// matching the node's alias list. The console refuses to hand them out as
// payment subaddresses.
const uint32_t ALIAS_SUBADDRESS_MAJOR = 1;
const uint32_t MAX_ALIAS_SUBADDRESS_MINOR = 1024;

bool isValidAliasName(const std::string& alias);

// Registers `alias` to `ownerSubaddress`, a subaddress derived for it, so the
// master address never appears on chain. From v11 on mainnet the registration
// fee is a re-mint burn carried in the fee; on testnet it is a self-transfer.
// Throws std::invalid_argument for an invalid alias or owner address.
WalletSendPlan planAliasRegistration(const Currency& currency, const std::string& alias,
                                     const std::string& ownerSubaddress, uint8_t nextBlockMajorVersion);

// A dev-fund donation (v11+): `amount` is burned as a re-mint burn carried in the
// fee, which consensus withholds from the miner and re-mints to the dev fund at the
// next FCI quarter boundary. The transaction pays the minimum fee to the donor.
WalletSendPlan planDevDonation(const Currency& currency, const std::string& ownAddress, uint64_t amount);

// The wallet's own transactions as limitOrdersDueForWithdraw reads them: which
// orders were placed, stopped or withdrawn, and whether those withdraws settled.
std::vector<WalletOrderTransaction> walletOrderHistory(IWalletLegacy& wallet);

struct SubaddressIndex {
  uint32_t major = 0;
  uint32_t minor = 0;
};

// The payment subaddress `gen_new_sub [major minor]` creates: the given index, or
// with no arguments the next unused minor under major 0. Returns why the index is
// refused (wrong arguments, the primary address, the alias major), else empty.
std::string choosePaymentSubaddressIndex(const std::vector<std::string>& args,
                                         const std::vector<std::pair<uint32_t, uint32_t>>& existing,
                                         SubaddressIndex& index);

// An alias registration this wallet sent (not one it received) that has not
// confirmed or failed yet.
bool hasPendingAliasRegistration(IWalletLegacy& wallet);

}
