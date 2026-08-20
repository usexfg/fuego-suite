// Copyright (c) 2012-2018 The CryptoNote developers
// Copyright (c) 2017-2022 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful- but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You are encouraged to redistribute it and/or modify it
// under the terms of the GNU General Public License v3 or later
// versions as published by the Free Software Foundation.
// You should receive a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>


#pragma once

#include <cstdint>
#include <functional>
#include <system_error>
#include <vector>

#include "crypto/crypto.h"
#include "CryptoNoteCore/CryptoNoteBasic.h"
#include "CryptoNoteProtocol/CryptoNoteProtocolDefinitions.h"
#include "Rpc/CoreRpcServerCommandsDefinitions.h"

#include "BlockchainExplorerData.h"
#include "ITransaction.h"
#include <memory>

namespace CryptoNote {

class INodeObserver {
public:
  virtual ~INodeObserver() {}
  virtual void peerCountUpdated(size_t count) {}
  virtual void localBlockchainUpdated(uint32_t height) {}
  virtual void lastKnownBlockHeightUpdated(uint32_t height) {}
  virtual void poolChanged() {}
  virtual void blockchainSynchronized(uint32_t topHeight) {}
};

struct OutEntry {
  uint32_t outGlobalIndex;
  Crypto::PublicKey outKey;
};

struct OutsForAmount {
  uint64_t amount;
  std::vector<OutEntry> outs;
};

struct TransactionShortInfo {
  Crypto::Hash txId;
  TransactionPrefix txPrefix;
};

struct BlockShortEntry {
  Crypto::Hash blockHash;
  bool hasBlock;
  Block block;
  std::vector<TransactionShortInfo> txsShortInfo;
};

class INode {
public:
  typedef std::function<void(std::error_code)> Callback;

  virtual ~INode() {}
  virtual bool addObserver(INodeObserver* observer) = 0;
  virtual bool removeObserver(INodeObserver* observer) = 0;
  virtual void init(const Callback& callback) = 0;
  virtual bool shutdown() = 0;

  virtual size_t getPeerCount() const = 0;
  virtual uint32_t getLastLocalBlockHeight() const = 0;
  virtual uint32_t getLastKnownBlockHeight() const = 0;
  virtual uint32_t getLocalBlockCount() const = 0;
  virtual uint32_t getKnownBlockCount() const = 0;
  virtual uint64_t getLastLocalBlockTimestamp() const = 0;

  virtual void relayTransaction(const Transaction& transaction, const Callback& callback) = 0;
  virtual void getRandomOutsByAmounts(std::vector<uint64_t>&& amounts, uint64_t outsCount, std::vector<COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::outs_for_amount>& result, const Callback& callback) = 0;
  virtual void getRandomCommitmentOutsForAmount(uint64_t amount, uint64_t outsCount, uint32_t maxHeight, std::vector<COMMAND_RPC_GET_RANDOM_COMMITMENT_OUTPUTS::out_entry>& result, const Callback& callback) = 0;
  // Bulk lookup of creation heights for (amount, global_index) pairs. Used by wallets for OSPEAD decoy filtering.
  // Default impl: leaves heights vector empty so wallet falls back to no-filter behavior when daemon lacks the endpoint.
  virtual void getOutputsHeights(const std::vector<std::pair<uint64_t, uint32_t>>& queries, std::vector<uint32_t>& heights, const Callback& callback) {
    heights.clear();
    callback(std::error_code());
  }
  // Explicit capability flag for the sidecar above. Real RPC nodes override to true.
  // Stubs and old-daemon clients leave the default false so wallets can skip the call entirely.
  virtual bool supportsOutputsHeights() const { return false; }
  virtual void getNewBlocks(std::vector<Crypto::Hash>&& knownBlockIds, std::vector<block_complete_entry>& newBlocks, uint32_t& startHeight, const Callback& callback) = 0;
  virtual void getTransactionOutsGlobalIndices(const Crypto::Hash& transactionHash, std::vector<uint32_t>& outsGlobalIndices, const Callback& callback) = 0;
  virtual void queryBlocks(std::vector<Crypto::Hash>&& knownBlockIds, uint64_t timestamp, std::vector<BlockShortEntry>& newBlocks, uint32_t& startHeight, const Callback& callback) = 0;
  virtual void getPoolSymmetricDifference(std::vector<Crypto::Hash>&& knownPoolTxIds, Crypto::Hash knownBlockId, bool& isBcActual, std::vector<std::unique_ptr<ITransactionReader>>& newTxs, std::vector<Crypto::Hash>& deletedTxIds, const Callback& callback) = 0;
  virtual void getMultisignatureOutputByGlobalIndex(uint64_t amount, uint32_t gindex, MultisignatureOutput& out, const Callback& callback) = 0;
  virtual void getTransaction(const Crypto::Hash &transactionHash, Transaction &transaction, const Callback &callback) = 0;
  virtual void getBlocks(const std::vector<uint32_t>& blockHeights, std::vector<std::vector<BlockDetails>>& blocks, const Callback& callback) = 0;
  virtual void getBlocks(const std::vector<Crypto::Hash>& blockHashes, std::vector<BlockDetails>& blocks, const Callback& callback) = 0;
  virtual void getBlocks(uint64_t timestampBegin, uint64_t timestampEnd, uint32_t blocksNumberLimit, std::vector<BlockDetails>& blocks, uint32_t& blocksNumberWithinTimestamps, const Callback& callback) = 0;
  virtual void getTransactions(const std::vector<Crypto::Hash>& transactionHashes, std::vector<TransactionDetails>& transactions, const Callback& callback) = 0;
  virtual void getTransactionsByPaymentId(const Crypto::Hash& paymentId, std::vector<TransactionDetails>& transactions, const Callback& callback) = 0;
  virtual void getPoolTransactions(uint64_t timestampBegin, uint64_t timestampEnd, uint32_t transactionsNumberLimit, std::vector<TransactionDetails>& transactions, uint64_t& transactionsNumberWithinTimestamps, const Callback& callback) = 0;
  virtual void isSynchronized(bool& syncStatus, const Callback& callback) = 0;

  // CD interest and epoch fee rate queries — delegated to CommitmentIndex in the daemon.
  // Default implementations return success with zero so existing INode implementations
  // (e.g. NodeRpcProxy) compile without changes until they override these.
  virtual std::error_code getCdInterest(uint64_t amount, uint32_t creationHeight,
                                        uint32_t currentHeight, uint64_t& outInterest,
                                        bool isLegacyBond = false) {
    outInterest = 0;
    return {};
  }
  // Pool-aware CD claim info. formulaInterest is the accrued epoch-rate sum;
  // claimableInterest is what consensus accepts today (capped by the fee pool
  // and CD_APY_POOL vault backing). poolInfoPresent=false when the daemon
  // predates pool-aware estimates — callers fall back to formula-only behavior.
  // v11+: baseInterest/bonusInterest are the consensus split (pool-backed base
  // vs BV-backed tier bonus); claimableBonus is bonus capped by the vault.
  struct CdClaimInfo {
    uint64_t formulaInterest = 0;
    uint64_t claimableInterest = 0;
    uint64_t feePoolBalance = 0;
    uint64_t vaultBalance = 0; // CD_APY_POOL partition, HEAT
    bool poolInfoPresent = false;
    uint64_t baseInterest = 0;
    uint64_t bonusInterest = 0;
    uint64_t bonusVaultBalance = 0;
    uint64_t claimableBonus = 0;
  };
  virtual std::error_code getCdClaimInfo(uint64_t amount, uint32_t creationHeight,
                                         uint32_t currentHeight, CdClaimInfo& out,
                                         uint32_t term = 0) {
    out = CdClaimInfo{};
    return getCdInterest(amount, creationHeight, currentHeight, out.formulaInterest);
  }
  virtual std::error_code getEpochFeeRate(uint32_t epoch, uint64_t& outFeeRate) {
    outFeeRate = 0;
    return {};
  }

  // AMM pool info for HEAT mint calculation and liquidity operations.
  struct AmmPoolInfo {
    uint64_t reserveXfg = 0;
    uint64_t reserveHeat = 0;
    uint64_t spotPrice = 0; // FixedPoint64: reserveXfg / reserveHeat
  };
  virtual std::error_code getAmmPoolInfo(AmmPoolInfo& info) {
    return std::make_error_code(std::errc::not_supported);
  }

  // Limit order deposit info from the daemon (all orders, filtered by wallet).
  // Default returns empty so existing INode implementations compile.
  struct LimitDepositRpcEntry {
    std::string order_id;
    std::string address_hash; // cn_fast_hash(spendKey||viewKey) — wallet filters by this
    uint8_t side;
    uint64_t amount;
    uint64_t proceeds_xfg;
    uint64_t proceeds_heat;
    uint64_t target_price;
    uint32_t expiration;
    bool withdrawn;
    bool expired;
  };
  virtual std::error_code getLimitDeposits(std::vector<LimitDepositRpcEntry>& deposits) {
    deposits.clear();
    return {};
  }

  // AMM pool redemption price: returns (reserveXfg, reserveHeat) from the Hearth pool.
  // Wallet uses this to compute correct heatMinted = xfgBurned * reserveHeat / reserveXfg.
  // Default returns (1,1) so existing INode implementations compile.
  struct AmmPoolReserves {
    uint64_t reserveXfg = 1;
    uint64_t reserveHeat = 1;
  };
  virtual std::error_code getAmmPoolReserves(AmmPoolReserves& out) {
    out = {1, 1};
    return {};
  }
  // Rolling 8-block TWAP of Hearth spot price for HEAT mint validation.
  // Returns 0 if unavailable (caller should fall back to spot pool rate).
  virtual uint64_t getHearthTwap() { return 0; }
};

}
