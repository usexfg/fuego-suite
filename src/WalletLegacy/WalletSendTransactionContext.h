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

#include <list>
#include <vector>

#include "CryptoNoteCore/CryptoNoteBasic.h"
#include "IWalletLegacy.h"
#include "ITransfersContainer.h"
#include "Rpc/CoreRpcServerCommandsDefinitions.h"

namespace CryptoNote {

struct TxDustPolicy
{
  uint64_t dustThreshold;
  bool addToFee;
  CryptoNote::AccountPublicAddress addrForDust;

  TxDustPolicy(uint64_t a_dust_threshold = 0, bool an_add_to_fee = false, CryptoNote::AccountPublicAddress an_addr_for_dust = CryptoNote::AccountPublicAddress())
    : dustThreshold(a_dust_threshold), addToFee(an_add_to_fee), addrForDust(an_addr_for_dust) {}
};

struct SendTransactionContext
{
  TransactionId transactionId;
  std::vector<CryptoNote::COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::outs_for_amount> outs;
  uint64_t foundMoney;
  std::vector<TransactionOutputInformation> selectedTransfers;
  TxDustPolicy dustPolicy;
  uint64_t mixIn;
  std::vector<tx_message_entry> messages;
  uint64_t ttl;
  uint32_t depositTerm;
  std::string extra;
  bool dynamicRingSize = false; // true: select optimal ring size from actual daemon-returned outs
  std::vector<CryptoNote::COMMAND_RPC_GET_RANDOM_COMMITMENT_OUTPUTS::out_entry> commitmentOuts; // ring decoys for CommitmentSpend

  // v10 auth context
  bool isV10HeatMint = false;
  uint64_t v10XfgBurned = 0;
  uint64_t v10HeatMinted = 0;
  bool isV10LpAdd = false;  // LP add (reuses heat mint fields, separate flag for routing)
  bool isV10AmmSwap = false;
  uint8_t v10SwapDirection = 0;
  uint64_t v10SwapInput = 0;
  uint64_t v10SwapOutput = 0;
  uint64_t v10SwapMinOutput = 0;

  // v10 LP remove
  bool isV10LpRemove = false;
  uint64_t v10LpSharesBurned = 0;
  uint64_t v10LpMinXfg = 0;
  uint64_t v10LpMinHeat = 0;

  // v10 HEAT transfer
  bool isV10HeatTransfer = false;
  AccountPublicAddress v10HeatRecipient;
  uint64_t v10HeatTransferAmount = 0;

  // v11+ orderbook context
  bool isV11LimitDeposit = false;
  uint8_t  v11DepositSide = 0;
  uint64_t v11DepositAmount = 0;
  uint64_t v11DepositTargetPrice = 0;
  uint32_t v11DepositExpiration = 0;
  Crypto::Hash  v11DepositOrderId;
  Crypto::Hash  v11DepositAddressHash;
  bool isV11LimitWithdraw = false;
  Crypto::Hash v11WithdrawOrderId;
  bool isV11MarketBuy = false;
  uint64_t v11XfgWanted = 0;
  uint64_t v11MaxHeatCost = 0;
  bool isV11MarketSell = false;
  uint64_t v11XfgToSell = 0;
  uint64_t v11MinHeatReceive = 0;

  // OSPEAD async pipeline state (populated between WalletGetRandomOutsByAmountsRequest
  // and WalletGetOutputsHeightsRequest; consumed by sendTransactionAfterOspeadHeights).
  // Queries are kept in iteration order so heights[i] aligns 1:1 with the i-th
  // decoy across all amount groups in `outs`.
  std::vector<std::pair<uint64_t, uint32_t>> ospeadHeightQueries;
  std::vector<uint32_t> ospeadHeights;
  bool ospeadHeightsRequested = false; // guard: prevent re-entry into heights chain
};

} //namespace CryptoNote
