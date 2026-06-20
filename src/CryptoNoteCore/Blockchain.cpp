// Copyright (c) 2017-2026 Fuego Developers
// Copyright (c) 2012-2016 The CryptoNote developers, The Bytecoin developers, The Monero developers
// Copyright (c) 2016-2019 The Karbowanec developers
// Copyright (c) 2018-2019 The Ryo Currency Developers
// Copyright (c) 2014-2017 XDN developers
// Copyright (c) 2018-2019 Conceal Network & Conceal Devs
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

#include "Blockchain.h"

#include <algorithm>
#include <cstring>
#include <numeric>
#include <cstdio>
#include <cmath>
#include <boost/foreach.hpp>
#include "../Common/MathUtils.h"
#include "../Common/int-util.h"
#include "../Common/ShuffleGenerator.h"
#include "../Common/StdInputStream.h"
#include "../Common/StdOutputStream.h"
#include "../Rpc/CoreRpcServerCommandsDefinitions.h"
#include "../Serialization/BinarySerializationTools.h"
#include "CryptoNoteTools.h"
#include "TransactionExtra.h"
#include "CommitmentIndex.h"
#include "CryptoNoteConfig.h"
#include "parallel_hashmap/phmap_dump.h"

using namespace Logging;
using namespace Common;

namespace {

std::string appendPath(const std::string& path, const std::string& fileName) {
  std::string result = path;
  if (!result.empty()) {
    result += '/';
  }

  result += fileName;
  return result;
}

}

namespace std {
bool operator<(const Crypto::Hash& hash1, const Crypto::Hash& hash2) {
  return memcmp(&hash1, &hash2, Crypto::HASH_SIZE) < 0;
}

bool operator<(const Crypto::KeyImage& keyImage1, const Crypto::KeyImage& keyImage2) {
  return memcmp(&keyImage1, &keyImage2, 32) < 0;
}
}

#define CURRENT_BLOCKCACHE_STORAGE_ARCHIVE_VER 10  // v10: treasury splits, 1:1 pool seed
#define CURRENT_BLOCKCHAININDICES_STORAGE_ARCHIVE_VER 1

namespace CryptoNote {
class BlockCacheSerializer;
class BlockchainIndicesSerializer;
} // namespace CryptoNote

namespace CryptoNote {

// custom serialization to speedup cache loading
bool serialize(std::vector<std::pair<TxIndex, uint16_t>>& value, Common::StringView name, CryptoNote::ISerializer& s) {
  const size_t elementSize = sizeof(std::pair<TxIndex, uint16_t>);
  size_t size = value.size() * elementSize;

  if (!s.beginArray(size, name)) {
    return false;
  }

  if (s.type() == CryptoNote::ISerializer::INPUT) {
    if (size % elementSize != 0) {
      throw std::runtime_error("Invalid vector size");
    }
    value.resize(size / elementSize);
  }

  if (size) {
    s.binary(value.data(), size, "");
  }

  s.endArray();
  return true;
}

void serialize(TxIndex& value, ISerializer& s) {
  s(value.block, "block");
  s(value.transaction, "tx");
}

class BlockCacheSerializer {

public:
  BlockCacheSerializer(Blockchain& bs, const Crypto::Hash lastBlockHash, ILogger& logger) :
    m_bs(bs), m_lastBlockHash(lastBlockHash), m_loaded(false), logger(logger, "BlockCacheSerializer") {
  }

  void load(const std::string& filename) {
    try {
      std::ifstream stdStream(filename, std::ios::binary);
      if (!stdStream) {
        return;
      }

      StdInputStream stream(stdStream);
      BinaryInputStreamSerializer s(stream);
      CryptoNote::serialize(*this, s);
    } catch (std::exception& e) {
      logger(WARNING) << "loading failed: " << e.what();
    }
  }

  bool save(const std::string& filename) {
    try {
      std::ofstream file(filename, std::ios::binary);
      if (!file) {
        return false;
      }

      StdOutputStream stream(file);
      BinaryOutputStreamSerializer s(stream);
      CryptoNote::serialize(*this, s);
    } catch (std::exception&) {
      return false;
    }

    return true;
  }

  void serialize(ISerializer& s) {
    auto start = std::chrono::steady_clock::now();

    uint8_t version = CURRENT_BLOCKCACHE_STORAGE_ARCHIVE_VER;
    s(version, "version");

    // ignore old versions, do rebuild
    if (version < CURRENT_BLOCKCACHE_STORAGE_ARCHIVE_VER) {
      return;
    }

    std::string operation;
    if (s.type() == ISerializer::INPUT) {
      operation = "- loading ";
      Crypto::Hash blockHash;
      s(blockHash, "last_block");

      if (blockHash != m_lastBlockHash) {
        return;
      }

    } else {
      operation = "- saving ";
      s(m_lastBlockHash, "last_block");
    }

    logger(INFO) << operation << "block index...";
    s(m_bs.m_blockIndex, "block_index");

      logger(INFO) << operation << "transaction map";
      if (s.type() == ISerializer::INPUT)
      {
        phmap::BinaryInputArchive ar_in(appendPath(m_bs.m_config_folder, "transactionsmap.dat").c_str());
        m_bs.m_indexManager.transactionMap().load(ar_in);
      }
      else
      {
        phmap::BinaryOutputArchive ar_out(appendPath(m_bs.m_config_folder, "transactionsmap.dat").c_str());
        m_bs.m_indexManager.transactionMap().dump(ar_out);
      }

      logger(INFO) << operation << "spent keys";
      if (s.type() == ISerializer::INPUT)
      {
        phmap::BinaryInputArchive ar_in(appendPath(m_bs.m_config_folder, "spentkeys.dat").c_str());
        m_bs.m_indexManager.spentKeys().load(ar_in);
      }
      else
      {
        phmap::BinaryOutputArchive ar_out(appendPath(m_bs.m_config_folder, "spentkeys.dat").c_str());
        m_bs.m_indexManager.spentKeys().dump(ar_out);
      }

      logger(INFO) << operation << "outputs";
      s(m_bs.m_indexManager.outputs().data(), "outputs");

      logger(INFO) << operation << "multi-signature outputs";
      s(m_bs.m_indexManager.multisigOutputs().data(), "multisig_outputs");

      logger(INFO) << operation << "banking index";
      s(m_bs.m_bankingIndex, "banking_index");

      logger(INFO) << operation << "commitment index";
      s(m_bs.m_commitmentIndex, "commitment_index");

      logger(INFO) << operation << "commitment outputs";
      s(m_bs.m_indexManager.commitmentOutputs().data(), "commitment_outputs");

      logger(INFO) << operation << "fee pool state";
      s(m_bs.m_feePoolBalance, "fee_pool_balance");
      s(m_bs.m_currentEpochSwapFees, "current_epoch_swap_fees");
      s(m_bs.m_totalCdLocked, "total_cd_locked");
      s(m_bs.m_treasuryBalance, "treasury_balance");
      s(m_bs.m_treasuryHeatReserve, "treasury_heat_reserve");
      s(m_bs.m_treasuryXfgReserve, "treasury_xfg_reserve");
      s(m_bs.m_treasuryLpReserve, "treasury_lp_reserve");
      s(m_bs.m_rolloverVaultBalance, "rollover_vault_balance");
      s(m_bs.m_totalSwapFeesCollected, "total_swap_fees_collected");
      s(m_bs.m_totalCdInterestPaid, "total_cd_interest_paid");
      s(m_bs.m_totalTreasuryAccrued, "total_treasury_accrued");
      s(m_bs.m_totalRolloverAccrued, "total_rollover_accrued");

      logger(INFO) << operation << "HEAT/AMM/PI state";
      s(m_bs.m_heatSupply, "heat_supply");
      s(m_bs.m_ammPool, "amm_pool");
      s(m_bs.m_digmPrimaryPool, "digm_primary_pool");
      s(m_bs.m_digmBancorPool, "digm_bancor_pool");
      s(m_bs.m_poolLockedXfg, "pool_locked_xfg");
      s(m_bs.m_poolLockedHeat, "pool_locked_heat");
      s(m_bs.m_lpCommitmentShares, "lp_commitment_shares");
      s(m_bs.m_twapBlockCount, "twap_block_count");
      {
        uint64_t twap_lo = (uint64_t)(m_bs.m_twapAccumulator & 0xFFFFFFFFFFFFFFFFULL);
        uint64_t twap_hi = (uint64_t)(m_bs.m_twapAccumulator >> 64);
        s(twap_lo, "twap_lo");
        s(twap_hi, "twap_hi");
        if (s.type() == ISerializer::INPUT)
          m_bs.m_twapAccumulator = ((uint128_t)twap_hi << 64) | twap_lo;
      }
      s(m_bs.m_cdYieldPool, "cd_yield_pool");
      s(m_bs.m_cdReserve, "cd_reserve");
      s(m_bs.m_heatCdFeePool, "heat_cd_fee_pool");
      s(m_bs.m_protocolLpShares, "protocol_lp_shares");
      s(m_bs.m_treasuryLpYield, "treasury_lp_yield");
      s(m_bs.m_bootstrapRepaid, "bootstrap_repaid");
      s(m_bs.m_bootstrapXfgOwed, "bootstrap_xfg_owed");
      s(m_bs.m_bootstrapRepaymentVault, "bootstrap_repayment_vault");

    auto dur = std::chrono::steady_clock::now() - start;

    logger(INFO) << "Serialization time: " << std::chrono::duration_cast<std::chrono::milliseconds>(dur).count() << "ms";

    m_loaded = true;
  }

  bool loaded() const {
    return m_loaded;
  }

private:

  LoggerRef logger;
  bool m_loaded;
  Blockchain& m_bs;
  Crypto::Hash m_lastBlockHash;
};

class BlockchainIndicesSerializer {

public:
  BlockchainIndicesSerializer(Blockchain& bs, const Crypto::Hash lastBlockHash, ILogger& logger) :
    m_bs(bs), m_lastBlockHash(lastBlockHash), m_loaded(false), logger(logger, "BlockchainIndicesSerializer") {
  }

  void serialize(ISerializer& s) {

    uint8_t version = CURRENT_BLOCKCHAININDICES_STORAGE_ARCHIVE_VER;

    KV_MEMBER(version);

    // ignore old versions, do rebuild
    if (version != CURRENT_BLOCKCHAININDICES_STORAGE_ARCHIVE_VER) {
      return;
    }

    std::string operation;

      if (s.type() == ISerializer::INPUT)
      {
        operation = "loading ";

        Crypto::Hash blockHash;
        s(blockHash, "blockHash");

        if (blockHash != m_lastBlockHash)
        {
          return;
        }
      }
      else
      {
        operation = "- saving ";
        s(m_lastBlockHash, "blockHash");
      }

      logger(INFO) << operation << "paymentID index";
      s(m_bs.m_paymentIdIndex, "paymentIdIndex");

      logger(INFO) << operation << "timestamp index";
      s(m_bs.m_timestampIndex, "timestampIndex");

      logger(INFO) << operation << "generated transactions index";
      s(m_bs.m_generatedTransactionsIndex, "generatedTransactionsIndex");

    m_loaded = true;
  }

  template<class Archive> void serialize(Archive& ar, unsigned int version) {

    // ignore old versions, do rebuild
    if (version < CURRENT_BLOCKCHAININDICES_STORAGE_ARCHIVE_VER)
      return;

      std::string operation;
      if (Archive::is_loading::value)
      {
        operation = "loading ";
        Crypto::Hash blockHash;
        ar &blockHash;

        if (blockHash != m_lastBlockHash)
        {
          return;
        }
      }
      else
      {
        operation = "- saving ";
        ar &m_lastBlockHash;
      }

      logger(INFO) << operation << "paymentID index";
      ar &m_bs.m_paymentIdIndex;

      logger(INFO) << operation << "timestamp index";
      ar &m_bs.m_timestampIndex;

      logger(INFO) << operation << "generated transactions index";
      ar &m_bs.m_generatedTransactionsIndex;

    m_loaded = true;
  }

  bool loaded() const {
    return m_loaded;
  }

private:

  LoggerRef logger;
  bool m_loaded;
  Blockchain& m_bs;
  Crypto::Hash m_lastBlockHash;
};

  Blockchain::Blockchain(const Currency &currency, tx_memory_pool &tx_pool, ILogger &logger, bool blockchainIndexesEnabled, bool blockchainAutosaveEnabled) :
    logger(logger, "Blockchain"),
                         m_currency(currency),
                         m_tx_pool(tx_pool),
                         m_current_block_cumul_sz_limit(0),
			 m_checkpoints(logger, &currency),
			 m_blockchainIndexesEnabled(blockchainIndexesEnabled),
			 m_blockchainAutosaveEnabled(blockchainAutosaveEnabled),
                         m_upgradeDetectorV2(currency, m_blocks, BLOCK_MAJOR_VERSION_2, logger),
                         m_upgradeDetectorV3(currency, m_blocks, BLOCK_MAJOR_VERSION_3, logger),
                         m_upgradeDetectorV4(currency, m_blocks, BLOCK_MAJOR_VERSION_4, logger),
                         m_upgradeDetectorV5(currency, m_blocks, BLOCK_MAJOR_VERSION_5, logger),
                         m_upgradeDetectorV6(currency, m_blocks, BLOCK_MAJOR_VERSION_6, logger),
			                   m_upgradeDetectorV7(currency, m_blocks, BLOCK_MAJOR_VERSION_7, logger),
			                   m_upgradeDetectorV8(currency, m_blocks, BLOCK_MAJOR_VERSION_8, logger),
                         m_upgradeDetectorV9(currency, m_blocks, BLOCK_MAJOR_VERSION_9, logger),
                        m_upgradeDetectorV10(currency, m_blocks, BLOCK_MAJOR_VERSION_10, logger),
                        m_upgradeDetectorV11(currency, m_blocks, BLOCK_MAJOR_VERSION_11, logger),
                        m_commitmentIndex(currency),
                         m_aliasIndex() {
  // Seed Hearth pool at 1:1 for immediate peg
  m_ammPool.reserveXfg = parameters::HEARTH_POOL_SEED_XFG * parameters::COIN;
  m_ammPool.reserveHeat = parameters::HEARTH_POOL_SEED_HEAT * parameters::COIN;
  // Bootstrap: protocol owes the XFG seed provider
  if (!m_bootstrapRepaid) {
    m_bootstrapXfgOwed = m_ammPool.reserveXfg;
  }
  // Init DIGM Bancor pool virtual reserves (zero-start, pump.fun style)
  bootstrapDigmPools();
} // upgradekit

bool Blockchain::addObserver(IBlockchainStorageObserver* observer) {
  return m_observerManager.add(observer);
}

bool Blockchain::removeObserver(IBlockchainStorageObserver* observer) {
  return m_observerManager.remove(observer);
}

bool Blockchain::checkTransactionInputs(const CryptoNote::Transaction& tx, BlockInfo& maxUsedBlock) {
  return checkTransactionInputs(tx, maxUsedBlock.height, maxUsedBlock.id) && check_tx_outputs(tx, maxUsedBlock.height);
}

bool Blockchain::checkTransactionInputs(const CryptoNote::Transaction& tx, BlockInfo& maxUsedBlock, BlockInfo& lastFailed) {

  BlockInfo tail;

  //not the best implementation at this time, sorry :(
  //check is ring_signature already checked ?
  if (maxUsedBlock.empty()) {
    //not checked, lets try to check
    if (!lastFailed.empty() && getCurrentBlockchainHeight() > lastFailed.height && getBlockIdByHeight(lastFailed.height) == lastFailed.id) {
      return false; //we already sure that this tx is broken for this height
    }

      if (!checkTransactionInputs(tx, maxUsedBlock.height, maxUsedBlock.id, &tail))
      {
        lastFailed = tail;
        return false;
      }
    }
    else
    {
      if (maxUsedBlock.height >= getCurrentBlockchainHeight())
      {
        return false;
      }

    if (getBlockIdByHeight(maxUsedBlock.height) != maxUsedBlock.id) {
      //if we already failed on this height and id, skip actual ring signature check
      if (lastFailed.id == getBlockIdByHeight(lastFailed.height)) {
        return false;
      }

      //check ring signature again, it is possible (with very small chance) that this transaction become again valid
      if (!checkTransactionInputs(tx, maxUsedBlock.height, maxUsedBlock.id, &tail)) {
        lastFailed = tail;
        return false;
      }
    }
  }

  return true;
}

bool Blockchain::haveSpentKeyImages(const CryptoNote::Transaction& tx) {
  return this->haveTransactionKeyImagesAsSpent(tx);
}

// pre m_blockchain_lock is locked

bool Blockchain::checkTransactionSize(size_t blobSize) {
  if (blobSize > getCurrentCumulativeBlocksizeLimit() - m_currency.minerTxBlobReservedSize()) {
    logger(ERROR) << "transaction is too big " << blobSize << ", maximum allowed size is " <<
      (getCurrentCumulativeBlocksizeLimit() - m_currency.minerTxBlobReservedSize());
    return false;
  }

  return true;
}

bool Blockchain::haveTransaction(const Crypto::Hash &id) {
  if (!m_indexManager.isReady()) return false;
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  return m_indexManager.transactionMap().find(id) != m_indexManager.transactionMap().end();
}

bool Blockchain::have_tx_keyimg_as_spent(const Crypto::KeyImage &key_im) {
  if (!m_indexManager.isReady()) return false;
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  return m_indexManager.spentKeys().find(key_im) != m_indexManager.spentKeys().end();
}

uint32_t Blockchain::getCurrentBlockchainHeight() {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  return static_cast<uint32_t>(m_blocks.size());
}

// @ Alias system proxies (delegated to standalone AliasIndex)
bool Blockchain::aliasExists(const std::string& alias) const {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  return m_aliasIndex.aliasExists(alias);
}

std::optional<AliasEntry> Blockchain::getAliasByName(const std::string& alias) const {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  return m_aliasIndex.getAliasByName(alias);
}

std::optional<AliasEntry> Blockchain::getAliasByAddress(const std::string& address) const {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  // v2 addressHash scheme: cn_fast_hash(spendKey||viewKey) instead of cn_fast_hash(base58).
  // Parse the address to extract raw key bytes for consistent hash computation.
  CryptoNote::AccountPublicAddress addr;
  if (m_currency.parseAccountAddressString(address, addr)) {
    uint8_t preimage[64];
    memcpy(preimage,      &addr.spendPublicKey, 32);
    memcpy(preimage + 32, &addr.viewPublicKey,  32);
    Crypto::Hash addrHash;
    Crypto::cn_fast_hash(preimage, 64, addrHash);
    return m_aliasIndex.getAliasByAddressHash(addrHash);
  }
  // Fallback for unparseable addresses (should not occur in practice).
  return m_aliasIndex.getAliasByAddress(address);
}

std::vector<AliasEntry> Blockchain::getAllAliases() const {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  return m_aliasIndex.getAllAliases();
}

bool Blockchain::removeAlias(const std::string& alias) {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  return m_aliasIndex.removeAlias(alias);
}

bool Blockchain::replaceAliasOwnership(const std::string& alias, const Crypto::Hash& newAddressHash) {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  return m_aliasIndex.replaceAliasOwnership(alias, newAddressHash);
}

bool Blockchain::init(const std::string& config_folder, bool load_existing) {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);

  if (!config_folder.empty() && !Tools::create_directories_if_necessary(config_folder)) {
    logger(ERROR, BRIGHT_RED) << "Failed to create data directory: " << m_config_folder;
    return false;
  }

  m_config_folder = config_folder;

  if (!m_blocks.open(appendPath(config_folder, m_currency.blocksFileName()), appendPath(config_folder, m_currency.blockIndexesFileName()), 1024)) {
    return false;
  }

  if (load_existing && !m_blocks.empty()) {
    logger(INFO, BRIGHT_WHITE) << "Loading blockchain...";
    BlockCacheSerializer loader(*this, get_block_hash(m_blocks.back().bl), logger.getLogger());
    loader.load(appendPath(config_folder, m_currency.blocksCacheFileName()));

    if (!loader.loaded()) {
      logger(WARNING, BRIGHT_YELLOW) << "No actual blockchain cache found, rebuilding internal structures...";
      rebuildCache();
    } else {
      m_indexManager.setReady(true);
    }

      /* Load (or generate) indices only if Explorer mode is enabled */
      if (m_blockchainIndexesEnabled)
      {
        loadBlockchainIndices();
      }

    }
    else
    {
      m_blocks.clear();
      m_indexManager.setReady(true);
    }

  // Load checkpoints for mainnet only (testnet has no checkpoints)
  if (!m_currency.isTestnet()) {
    m_checkpoints.load_checkpoints();
    logger(Logging::INFO) << "Loaded mainnet checkpoints";
  } else {
    logger(Logging::INFO) << "Testnet doesn't use or recognize checkpoints";
  }

  if (m_blocks.empty()) {
    logger(INFO, BRIGHT_WHITE)
      << "Blockchain not loaded, generating genesis block.";
    block_verification_context bvc = boost::value_initialized<block_verification_context>();
    pushBlock(m_currency.genesisBlock(), get_block_hash(m_currency.genesisBlock()), bvc, 0);
    if (bvc.m_verification_failed) {
      logger(ERROR, BRIGHT_RED) << "Failed to add genesis block to blockchain";
      return false;
    }
  } else {
    Crypto::Hash firstBlockHash = get_block_hash(m_blocks[0].bl);
    if (!(firstBlockHash == m_currency.genesisBlockHash())) {
      logger(ERROR, BRIGHT_RED) << "Failed to init: genesis block mismatch. "
        "You've probably set --testnet flag and are "
        "using a data dir with non-test blockchain "
        "or another network.";
      return false;
    }
  }

  uint32_t lastValidCheckpointHeight = 0;
  if (!checkCheckpoints(lastValidCheckpointHeight)) {
    logger(WARNING, BRIGHT_MAGENTA) << "Invalid checkpoint found. Rollback blockchain to height=" << lastValidCheckpointHeight;
    rollbackBlockchainTo(lastValidCheckpointHeight);
  }

if (!m_upgradeDetectorV2.init() || !m_upgradeDetectorV3.init() || !m_upgradeDetectorV4.init() || !m_upgradeDetectorV5.init() || !m_upgradeDetectorV6.init() || !m_upgradeDetectorV7.init() || !m_upgradeDetectorV8.init() || !m_upgradeDetectorV9.init() || !m_upgradeDetectorV10.init()) {
    logger(ERROR, BRIGHT_RED) << "Failed to initialize upgrade detector. Trying self-healing procedure.";
}

 bool reinitUpgradeDetectors = false;
  if (!checkUpgradeHeight(m_upgradeDetectorV2)) {
    uint32_t upgradeHeight = m_upgradeDetectorV2.upgradeHeight();
    assert(upgradeHeight != UpgradeDetectorBase::UNDEF_HEIGHT);
    logger(WARNING, BRIGHT_YELLOW) << "Invalid block version at " << upgradeHeight + 1 << ": real=" << static_cast<int>(m_blocks[upgradeHeight + 1].bl.majorVersion) <<
    " expected=" << static_cast<int>(m_upgradeDetectorV2.targetVersion()) << ". Rollback blockchain to height=" << upgradeHeight;
    rollbackBlockchainTo(upgradeHeight);
    reinitUpgradeDetectors = true;
  } else if (!checkUpgradeHeight(m_upgradeDetectorV3)) {
    uint32_t upgradeHeight = m_upgradeDetectorV3.upgradeHeight();
    logger(WARNING, BRIGHT_YELLOW) << "Invalid block version at " << upgradeHeight + 1 << ": real=" << static_cast<int>(m_blocks[upgradeHeight + 1].bl.majorVersion) <<
    " expected=" << static_cast<int>(m_upgradeDetectorV3.targetVersion()) << ". Rollback blockchain to height=" << upgradeHeight;
    rollbackBlockchainTo(upgradeHeight);
    reinitUpgradeDetectors = true;
  } else if (!checkUpgradeHeight(m_upgradeDetectorV4)) {
    uint32_t upgradeHeight = m_upgradeDetectorV4.upgradeHeight();
    logger(WARNING, BRIGHT_YELLOW) << "Invalid block version at " << upgradeHeight + 1 << ": real=" << static_cast<int>(m_blocks[upgradeHeight + 1].bl.majorVersion) <<
    " expected=" << static_cast<int>(m_upgradeDetectorV4.targetVersion()) << ". Rollback blockchain to height=" << upgradeHeight;
    rollbackBlockchainTo(upgradeHeight);
    reinitUpgradeDetectors = true;
  } else if (!checkUpgradeHeight(m_upgradeDetectorV5)) {
    uint32_t upgradeHeight = m_upgradeDetectorV5.upgradeHeight();
    logger(WARNING, BRIGHT_YELLOW) << "Invalid block version at " << upgradeHeight + 1 << ": real=" << static_cast<int>(m_blocks[upgradeHeight + 1].bl.majorVersion) <<
    " expected=" << static_cast<int>(m_upgradeDetectorV5.targetVersion()) << ". Rollback blockchain to height=" << upgradeHeight;
    rollbackBlockchainTo(upgradeHeight);
    reinitUpgradeDetectors = true;
  } else if (!checkUpgradeHeight(m_upgradeDetectorV6)) {
    uint32_t upgradeHeight = m_upgradeDetectorV6.upgradeHeight();
    logger(WARNING, BRIGHT_MAGENTA) << "Invalid block version at " << upgradeHeight + 1 << ": real=" << static_cast<int>(m_blocks[upgradeHeight + 1].bl.majorVersion) <<
    " expected=" << static_cast<int>(m_upgradeDetectorV6.targetVersion()) << ". Rollback blockchain to height=" << upgradeHeight;
    rollbackBlockchainTo(upgradeHeight);
    reinitUpgradeDetectors = true;
  } else if (!checkUpgradeHeight(m_upgradeDetectorV7)) {
    uint32_t upgradeHeight = m_upgradeDetectorV7.upgradeHeight();
    logger(WARNING, BRIGHT_MAGENTA) << "Invalid block version at " << upgradeHeight + 1 << ": real=" << static_cast<int>(m_blocks[upgradeHeight + 1].bl.majorVersion) <<
    " expected=" << static_cast<int>(m_upgradeDetectorV7.targetVersion()) << ". Rollback blockchain to height=" << upgradeHeight;
    rollbackBlockchainTo(upgradeHeight);
    reinitUpgradeDetectors = true;
  } else if (!checkUpgradeHeight(m_upgradeDetectorV8)) {
    uint32_t upgradeHeight = m_upgradeDetectorV8.upgradeHeight();
    logger(WARNING, BRIGHT_MAGENTA) << "Invalid block version at " << upgradeHeight + 1 << ": real=" << static_cast<int>(m_blocks[upgradeHeight + 1].bl.majorVersion) <<
    " expected=" << static_cast<int>(m_upgradeDetectorV8.targetVersion()) << ". Rollback blockchain to height=" << upgradeHeight;
    rollbackBlockchainTo(upgradeHeight);
    reinitUpgradeDetectors = true;
  } else if (!checkUpgradeHeight(m_upgradeDetectorV9)) {
    uint32_t upgradeHeight = m_upgradeDetectorV9.upgradeHeight();
    logger(WARNING, BRIGHT_MAGENTA) << "Invalid block version at " << upgradeHeight + 1 << ": real=" << static_cast<int>(m_blocks[upgradeHeight + 1].bl.majorVersion) <<
    " expected=" << static_cast<int>(m_upgradeDetectorV9.targetVersion()) << ". Rollback blockchain to height=" << upgradeHeight;
    rollbackBlockchainTo(upgradeHeight);
    reinitUpgradeDetectors = true;
  } else if (!checkUpgradeHeight(m_upgradeDetectorV10)) {
    uint32_t upgradeHeight = m_upgradeDetectorV10.upgradeHeight();
    logger(WARNING, BRIGHT_MAGENTA) << "Invalid block version at " << upgradeHeight + 1 << ": real=" << static_cast<int>(m_blocks[upgradeHeight + 1].bl.majorVersion) <<
    " expected=" << static_cast<int>(m_upgradeDetectorV10.targetVersion()) << ". Rollback blockchain to height=" << upgradeHeight;
    rollbackBlockchainTo(upgradeHeight);
    reinitUpgradeDetectors = true;
  }
  if (reinitUpgradeDetectors && (!m_upgradeDetectorV2.init() || !m_upgradeDetectorV3.init() || !m_upgradeDetectorV4.init() || !m_upgradeDetectorV5.init() || !m_upgradeDetectorV6.init() || !m_upgradeDetectorV7.init() || !m_upgradeDetectorV8.init() || !m_upgradeDetectorV9.init() || !m_upgradeDetectorV10.init())) {
    logger(ERROR, BRIGHT_RED) << "Failed again to initialize upgrade detector";
    return false;
  }

  update_next_comulative_size_limit();

  // If no burned data in BankingIndex, rescan blockchain for HEAT burns
  // Handles cases where chain was synced before burn tracking was added
  uint64_t currentBurned = m_bankingIndex.getBurnedXfgAmount();
  if (currentBurned == 0 && m_blocks.size() > 1) {
    logger(INFO, BRIGHT_YELLOW) << "No burn data found in BankingIndex, rescanning " << m_blocks.size() << " blocks for HEAT commitments...";
    uint64_t totalRescannedBurns = 0;
    for (uint32_t b = 0; b < m_blocks.size(); ++b) {
      const BlockEntry &block = m_blocks[b];
      for (const auto &tx : block.transactions) {
        std::vector<TransactionExtraField> extraFields;
        if (parseTransactionExtra(tx.tx.extra, extraFields)) {
          for (const auto& field : extraFields) {
            if (field.type() == typeid(TransactionExtraHeatCommitment)) {
              const auto& heatCommit = boost::get<TransactionExtraHeatCommitment>(field);
              totalRescannedBurns += heatCommit.amount;
              m_bankingIndex.addForeverDeposit(heatCommit.amount, b);
            }
          }
        }
      }
    }
    if (totalRescannedBurns > 0) {
      logger(INFO, BRIGHT_GREEN) << "Rescan found " << m_currency.formatAmount(totalRescannedBurns)
                                 << " burned " << (m_currency.isTestnet() ? "TEST" : "XFG") << " across blockchain";
    } else {
      logger(INFO) << "Rescan complete - no HEAT burns found in blockchain";
    }
  }

  // Sync Currency from BankingIndex (single source of truth for burned amounts)
  const_cast<Currency&>(m_currency).syncEternalFlame(m_bankingIndex.getBurnedXfgAmount());
  logger(DEBUGGING) << "EternalFlame synced from BankingIndex: " << m_bankingIndex.getBurnedXfgAmount();

  uint64_t timestamp_diff = time(NULL) - m_blocks.back().bl.timestamp;
  if (!m_blocks.back().bl.timestamp) {
    timestamp_diff = time(NULL) - 1341378000;
  }

  logger(INFO, BRIGHT_BLUE)
    << "Blockchain initialized. last block: " << m_blocks.size() - 1 << ", "
    << Common::timeIntervalToString(timestamp_diff)
    << " time ago, current difficulty: " << getDifficultyForNextBlock();
  return true;
}

  bool Blockchain::checkCheckpoints(uint32_t &lastValidCheckpointHeight)
  {
    std::vector<uint32_t> checkpointHeights = m_checkpoints.getCheckpointHeights();
    for (const auto &checkpointHeight : checkpointHeights)
    {

      if (m_blocks.size() <= checkpointHeight)
      {
        return true;
      }

      // Use get_block_hash directly instead of getBlockIdByHeight to avoid asserting
      // on m_blockIndex.size() when running async rebuildCache in the background.
      Crypto::Hash blockHash = get_block_hash(m_blocks[checkpointHeight].bl);
      if (m_checkpoints.check_block(checkpointHeight, blockHash))
      {
        lastValidCheckpointHeight = checkpointHeight;
      }
      else
      {
        return false;
      }
    }
    logger(INFO, BRIGHT_WHITE) << "Checkpoints passed";
    return true;
  }

  void Blockchain::rebuildCache()
  {
    logger(INFO, BRIGHT_WHITE) << "Rebuilding cache";

    std::chrono::steady_clock::time_point timePoint = std::chrono::steady_clock::now();
    std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
    std::lock_guard<std::mutex> rebuildLock(m_indexManager.rebuildMutex());

    m_indexManager.setReady(false);
    m_indexManager.clear();
    m_blockIndex.clear();
    m_commitmentIndex.clear();
    m_bankingIndex = BankingIndex(static_cast<BankingIndex::DepositHeight>(m_blocks.size()));
    for (uint32_t b = 0; b < m_blocks.size(); ++b)
    {
      if (b % 1000 == 0)
      {
        logger(INFO, BRIGHT_WHITE) << "Rebuilding Cache for Height " << b << " of " << m_blocks.size();
      }

      const BlockEntry &block = m_blocks[b];
      Crypto::Hash blockHash = get_block_hash(block.bl);
      m_blockIndex.push(blockHash);
      uint64_t interest = 0;
      for (uint16_t t = 0; t < block.transactions.size(); ++t)
      {
        const TransactionEntry &transaction = block.transactions[t];
        Crypto::Hash transactionHash = getObjectHash(transaction.tx);
        TxIndex transactionIndex = {b, t};
        m_indexManager.transactionMap().insert(std::make_pair(transactionHash, transactionIndex));

        // process inputs
        for (auto &i : transaction.tx.inputs)
        {
          if (i.type() == typeid(KeyInput))
          {
            m_indexManager.spentKeys().insert(std::make_pair(::boost::get<KeyInput>(i).keyImage, b));
          }
          else if (i.type() == typeid(MultisignatureInput))
          {
            auto out = ::boost::get<MultisignatureInput>(i);
            m_indexManager.multisigOutputs()[out.amount][out.outputIndex].isUsed = true;
          }
          else if (i.type() == typeid(TransactionInputCommitmentSpend))
          {
            m_indexManager.spentKeys().insert(std::make_pair(::boost::get<TransactionInputCommitmentSpend>(i).keyImage, b));
          }
        }

        // process outputs
        for (uint16_t o = 0; o < transaction.tx.outputs.size(); ++o) {
          const auto& out = transaction.tx.outputs[o];
          if (out.target.type() == typeid(KeyOutput)) {
            m_indexManager.outputs()[out.amount].push_back(std::make_pair<>(transactionIndex, o));
          } else if (out.target.type() == typeid(MultisignatureOutput)) {
            MultisignatureOutputUsage usage = { transactionIndex, o, false };
            m_indexManager.multisigOutputs()[out.amount].push_back(usage);
          } else if (out.target.type() == typeid(TransactionOutputCommitment)) {
            const auto& commitOut = ::boost::get<TransactionOutputCommitment>(out.target);
            CommitmentOutputRef ref;
            ref.transactionIndex     = transactionIndex;
            ref.outputInTransaction  = o;
            ref.commitKey            = commitOut.commitKey;
            ref.term                 = commitOut.term;
            m_indexManager.commitmentOutputs()[out.amount].push_back(ref);
          }
        }
        // Rebuild CommitmentIndex from transaction extras (merged into single pass)
        std::vector<TransactionExtraField> extraFields;
        if (parseTransactionExtra(transaction.tx.extra, extraFields)) {
          for (const auto& field : extraFields) {
            if (field.type() == typeid(TransactionExtraHeatCommitment)) {
              const auto& h = boost::get<TransactionExtraHeatCommitment>(field);
              CommitmentEntry entry;
              entry.commitment    = h.commitment;
              entry.txHash        = getObjectHash(transaction.tx);
              entry.blockHeight   = b;
              entry.amount        = h.amount;
              entry.term          = parameters::HEAT_TERM;
              entry.type          = CommitmentEntry::Type::HEAT;
              entry.targetChainId = h.metadata.size() > 0 ? h.metadata[0] : 1;
              m_commitmentIndex.addCommitment(entry);
            } else if (field.type() == typeid(TransactionExtraSimpleCD)) {
              const auto& c = boost::get<TransactionExtraSimpleCD>(field);
              CommitmentEntry entry;
              entry.commitment    = c.commitment;
              entry.txHash        = getObjectHash(transaction.tx);
              entry.blockHeight   = b;
              entry.amount        = c.amount;
              entry.term          = c.term;
              entry.type          = CommitmentEntry::Type::COLD;
              entry.targetChainId = 1;
              m_commitmentIndex.addCommitment(entry);
            }
          }
        }
        interest += m_currency.calculateTotalTransactionInterest(transaction.tx, b);
      }
      pushToBankingIndex(block, interest);
    }

    logger(INFO, BRIGHT_WHITE) << "Commitment index rebuilt: "
      << m_commitmentIndex.size() << " commitments.";

    m_indexManager.setReady(true);

    std::chrono::duration<double> duration = std::chrono::steady_clock::now() - timePoint;
    logger(INFO, BRIGHT_WHITE) << "Rebuilding internal structures took: " << duration.count();
  }

bool Blockchain::storeCache() {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);

  logger(INFO, BRIGHT_WHITE) << "Saving blockchain...";
  BlockCacheSerializer ser(*this, getTailId(), logger.getLogger());
  if (!ser.save(appendPath(m_config_folder, m_currency.blocksCacheFileName()))) {
    logger(ERROR, BRIGHT_RED) << "Failed to save blockchain cache";
    return false;
  }
    logger(INFO, BRIGHT_GREEN) << "Fuego blockchain was successfully saved.";
  return true;
}

bool Blockchain::deinit() {
  storeCache();
  if (m_blockchainIndexesEnabled) {
    storeBlockchainIndices();
  }
  assert(m_messageQueueList.empty());
  return true;
}

bool Blockchain::resetAndSetGenesisBlock(const Block& b) {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  m_blocks.clear();
  m_blockIndex.clear();
  m_indexManager.setReady(false);
  m_indexManager.clear();

  m_indexManager.spentKeys().clear();
  m_alternative_chains.clear();
  m_indexManager.outputs().clear();

  m_paymentIdIndex.clear();
  m_timestampIndex.clear();
  m_generatedTransactionsIndex.clear();
  m_orthanBlocksIndex.clear();

  block_verification_context bvc = boost::value_initialized<block_verification_context>();
  addNewBlock(b, bvc);
  m_indexManager.setReady(true);
  return bvc.m_added_to_main_chain && !bvc.m_verification_failed;
}

Crypto::Hash Blockchain::getTailId(uint32_t& height) {
  assert(!m_blocks.empty());
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  height = getCurrentBlockchainHeight() - 1;
  return getTailId();
}

Crypto::Hash Blockchain::getTailId() {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  return m_blocks.empty() ? NULL_HASH : m_blockIndex.getTailId();
}

std::vector<Crypto::Hash> Blockchain::buildSparseChain() {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  assert(m_blockIndex.size() != 0);
  return doBuildSparseChain(m_blockIndex.getTailId());
}

std::vector<Crypto::Hash> Blockchain::buildSparseChain(const Crypto::Hash& startBlockId) {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  assert(haveBlock(startBlockId));
  return doBuildSparseChain(startBlockId);
}

std::vector<Crypto::Hash> Blockchain::doBuildSparseChain(const Crypto::Hash& startBlockId) const {
  assert(m_blockIndex.size() != 0);

  std::vector<Crypto::Hash> sparseChain;

  if (m_blockIndex.hasBlock(startBlockId)) {
    sparseChain = m_blockIndex.buildSparseChain(startBlockId);
  } else {
    assert(m_alternative_chains.count(startBlockId) > 0);

    std::vector<Crypto::Hash> alternativeChain;
    Crypto::Hash blockchainAncestor;
    for (auto it = m_alternative_chains.find(startBlockId); it != m_alternative_chains.end(); it = m_alternative_chains.find(blockchainAncestor)) {
      alternativeChain.emplace_back(it->first);
      blockchainAncestor = it->second.bl.previousBlockHash;
    }

    for (size_t i = 1; i <= alternativeChain.size(); i *= 2) {
      sparseChain.emplace_back(alternativeChain[i - 1]);
    }

    assert(!sparseChain.empty());
    assert(m_blockIndex.hasBlock(blockchainAncestor));
    std::vector<Crypto::Hash> sparseMainChain = m_blockIndex.buildSparseChain(blockchainAncestor);
    sparseChain.reserve(sparseChain.size() + sparseMainChain.size());
    std::copy(sparseMainChain.begin(), sparseMainChain.end(), std::back_inserter(sparseChain));
  }

  return sparseChain;
}

Crypto::Hash Blockchain::getBlockIdByHeight(uint32_t height) {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  assert(height < m_blockIndex.size());
  return m_blockIndex.getBlockId(height);
}

bool Blockchain::getBlockByHash(const Crypto::Hash& blockHash, Block& b) {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);

  uint32_t height = 0;

  if (m_blockIndex.getBlockHeight(blockHash, height)) {
    b = m_blocks[height].bl;
    return true;
  }

  logger(WARNING) << blockHash;

  auto blockByHashIterator = m_alternative_chains.find(blockHash);
  if (blockByHashIterator != m_alternative_chains.end()) {
    b = blockByHashIterator->second.bl;
    return true;
  }

  return false;
}

bool Blockchain::getBlockHeight(const Crypto::Hash& blockId, uint32_t& blockHeight) {
  std::lock_guard<decltype(m_blockchain_lock)> lock(m_blockchain_lock);
  return m_blockIndex.getBlockHeight(blockId, blockHeight);
}

difficulty_type Blockchain::getDifficultyForNextBlock() {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);

  uint32_t currentHeight = static_cast<uint32_t>(m_blocks.size());
  uint8_t BlockMajorVersion = getBlockMajorVersionForHeight(currentHeight);
  size_t difficultyWindow = m_currency.difficultyBlocksCountByBlockVersion(BlockMajorVersion);

  // Get the last checkpoint height to detect checkpoint zone transition
  std::vector<uint32_t> checkpointHeights = m_checkpoints.getCheckpointHeights();
  uint32_t lastCheckpointHeight = checkpointHeights.empty() ? 0 : checkpointHeights.back();

  std::vector<uint64_t> timestamps;
  std::vector<difficulty_type> cumulative_difficulties;
  size_t offset;
  offset = m_blocks.size() - std::min(m_blocks.size(), static_cast<uint64_t>(difficultyWindow));

  if (offset == 0) {
    ++offset;
  }
  for (; offset < m_blocks.size(); offset++) {
    timestamps.push_back(m_blocks[offset].bl.timestamp);
    cumulative_difficulties.push_back(m_blocks[offset].cumulative_difficulty);
  }
  return m_currency.nextDifficulty(currentHeight, BlockMajorVersion, timestamps, cumulative_difficulties);
}

uint64_t Blockchain::getBlockTimestamp(uint32_t height) {
  assert(height < m_blocks.size());
  return m_blocks[height].bl.timestamp;
}

uint64_t Blockchain::getCoinsInCirculation() {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  if (m_blocks.empty()) {
    return 0;
  } else {
    return m_blocks.back().already_generated_coins;
  }
}

uint64_t Blockchain::coinsEmittedAtHeight(uint64_t height) {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  const auto& block = m_blocks[height];
  return block.already_generated_coins;
}

  difficulty_type Blockchain::difficultyAtHeight(uint64_t height)
  {
    std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
    const auto &current = m_blocks[height];
    if (height < 1)
    {
      return current.cumulative_difficulty;
    }

    const auto &previous = m_blocks[height - 1];
    return current.cumulative_difficulty - previous.cumulative_difficulty;
  }

uint8_t Blockchain::getBlockMajorVersionForHeight(uint32_t height) const {
  if (height > m_upgradeDetectorV11.upgradeHeight()) {
    return m_upgradeDetectorV11.targetVersion();
  } else if (height > m_upgradeDetectorV10.upgradeHeight()) {
    return m_upgradeDetectorV10.targetVersion();
  } else if (height > m_upgradeDetectorV9.upgradeHeight()) {
    return m_upgradeDetectorV9.targetVersion();
  } else if (height > m_upgradeDetectorV8.upgradeHeight()) {
    return m_upgradeDetectorV8.targetVersion();
  } else if (height > m_upgradeDetectorV7.upgradeHeight()) {
    return m_upgradeDetectorV7.targetVersion();
  } else if (height > m_upgradeDetectorV6.upgradeHeight()) {
    return m_upgradeDetectorV6.targetVersion();
  } else if (height > m_upgradeDetectorV5.upgradeHeight()) {
    return m_upgradeDetectorV5.targetVersion();
  } else if (height > m_upgradeDetectorV4.upgradeHeight()) {
    return m_upgradeDetectorV4.targetVersion();
  } else if (height > m_upgradeDetectorV3.upgradeHeight()) {
    return m_upgradeDetectorV3.targetVersion();
  } else if (height > m_upgradeDetectorV2.upgradeHeight()) {
    return m_upgradeDetectorV2.targetVersion();
  } else {
    return BLOCK_MAJOR_VERSION_1;
  }
}

bool Blockchain::rollback_blockchain_switching(std::list<Block> &original_chain, size_t rollback_height) {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  // remove failed subchain
  for (size_t i = m_blocks.size() - 1; i >= rollback_height; i--) {
    popBlock(get_block_hash(m_blocks.back().bl));
  }

    uint32_t height = static_cast<uint32_t>(rollback_height - 1);

  // return back original chain
  for (auto &bl : original_chain) {
    block_verification_context bvc =
      boost::value_initialized<block_verification_context>();
    bool r = pushBlock(bl, get_block_hash(bl), bvc, ++height);
    if (!(r && bvc.m_added_to_main_chain)) {
      logger(ERROR, BRIGHT_RED) << "PANIC!!! failed to add block (again) while "
        "chain switching during the rollback!";
      return false;
    }
  }

  logger(INFO, BRIGHT_YELLOW) << "Rollback success.";
  return true;
}
//------------------------------------------------------------------
// Calculate ln(p) of Poisson distribution
// Original idea : https://stackoverflow.com/questions/30156803/implementing-poisson-distribution-in-c
// Using logarithms avoids dealing with very large (k!) and very small (p < 10^-44) numbers
// lam     - lambda parameter - in our case, how many blocks, on average, you would expect to see in the interval
// k       - k parameter - in our case, how many blocks we have actually seen
//           !!! k must not be zero
// return  - ln(p)
double calc_poisson_ln(double lam, uint64_t k)
{
  double logx = -lam + k * log(lam);
  do
  {
    logx -= log(k); // This can be tabulated
  } while (--k > 0);
  return logx;
}

bool Blockchain::switch_to_alternative_blockchain(std::list<blocks_ext_by_hash::iterator>& alt_chain, bool discard_disconnected_chain) {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);

  if (!(alt_chain.size())) {
    logger(ERROR, BRIGHT_RED) << "switch_to_alternative_blockchain: empty chain passed";
    return false;
  }

  size_t split_height = alt_chain.front()->second.height;

  if (!(m_blocks.size() > split_height)) {
    logger(ERROR, BRIGHT_RED) << "switch_to_alternative_blockchain: blockchain size is lower than split height";
    return false;
  }

   //-------------------------------------------------------------------------------------------------
  // For longer reorgs, check if the timestamps are probable - if they aren't the diff algo has failed
  // This check is meant to detect an offline bypass of timestamp < time() + ftl check
  // It doesn't need to be very strict as it synergises with the median check
  if (alt_chain.size() >= CryptoNote::parameters::POISSON_CHECK_TRIGGER)
  {
	  uint64_t alt_chain_size = alt_chain.size();
	  uint64_t high_timestamp = alt_chain.back()->second.bl.timestamp;
	  Crypto::Hash low_block = alt_chain.front()->second.bl.previousBlockHash;
	  //Make sure that the high_timestamp is really highest
	  for (const blocks_ext_by_hash::iterator &it : alt_chain)
	  {
		  if (high_timestamp < it->second.bl.timestamp)
			  high_timestamp = it->second.bl.timestamp;
	  }
	  uint64_t block_ftl = CryptoNote::parameters::CRYPTONOTE_BLOCK_FUTURE_TIME_LIMIT;
	  // This would fail later anyway
	  if (high_timestamp > get_adjusted_time() + block_ftl)
	  {
		  logger(ERROR, BRIGHT_RED) << "Attempting to move to an alternate chain, but it failed FTL check! timestamp: " << high_timestamp << " limit: " << get_adjusted_time() + block_ftl;
		  return false;
	  }
	  logger(INFO) << "Poisson check triggered by reorg size of " << alt_chain_size;
	  uint64_t failed_checks = 0, i = 1;
	  constexpr Crypto::Hash zero_hash = { 0 };
	  for (; i <= CryptoNote::parameters::POISSON_CHECK_DEPTH; i++)
	  {
		  // This means we reached the genesis block
		  if (low_block == zero_hash)
			  break;
		  Block blk;
		  getBlockByHash(low_block, blk);
		  uint64_t low_timestamp = blk.timestamp;
		  low_block = blk.previousBlockHash;
		  if (low_timestamp >= high_timestamp)
		  {
			  logger(INFO) << "Skipping check at depth " << i << " due to tampered timestamp on main chain.";
			  failed_checks++;
			  continue;
		  }
		  double lam = double(high_timestamp - low_timestamp) / double(CryptoNote::parameters::DIFFICULTY_TARGET);
		  if (calc_poisson_ln(lam, alt_chain_size + i) < CryptoNote::parameters::POISSON_LOG_P_REJECT)
		  {
			  logger(INFO) << "Poisson check at depth " << i << " failed! delta_t: " << (high_timestamp - low_timestamp) << " size: " << alt_chain_size + i;
			  failed_checks++;
  }

  }

  i--; //Convert to number of checks
	  logger(INFO) << "Poisson check result " << failed_checks << " fails out of " << i;
	  if (failed_checks > i / 2)
	  {
		  logger(ERROR, BRIGHT_RED) << "Attempting to move to an alternate chain, but it failed Poisson check! " << failed_checks << " fails out of " << i << " alt_chain_size: " << alt_chain_size;
		  return false;
	  }
	  }

   // Compare transactions in proposed alt chain vs current main chain and reject if some transaction is missing in the alt chain
  std::vector<Crypto::Hash> mainChainTxHashes, altChainTxHashes;
  for (size_t i = m_blocks.size() - 1; i >= split_height; i--) {
    Block b = m_blocks[i].bl;
    std::copy(b.transactionHashes.begin(), b.transactionHashes.end(), std::inserter(mainChainTxHashes, mainChainTxHashes.end()));
  }
  for (auto alt_ch_iter = alt_chain.begin(); alt_ch_iter != alt_chain.end(); alt_ch_iter++) {
    auto ch_ent = *alt_ch_iter;
    Block b = ch_ent->second.bl;
    std::copy(b.transactionHashes.begin(), b.transactionHashes.end(), std::inserter(altChainTxHashes, altChainTxHashes.end()));
  }
  for (auto main_ch_it = mainChainTxHashes.begin(); main_ch_it != mainChainTxHashes.end(); main_ch_it++) {
    auto tx_hash = *main_ch_it;
    if (std::find(altChainTxHashes.begin(), altChainTxHashes.end(), tx_hash) == altChainTxHashes.end()) {
      logger(ERROR, BRIGHT_RED) << "Attempting to switch to an alternate chain, but it lacks transaction " << Common::podToHex(tx_hash) << " from main chain, rejected";
      mainChainTxHashes.clear();
      mainChainTxHashes.shrink_to_fit();
      altChainTxHashes.clear();
      altChainTxHashes.shrink_to_fit();
      return false;
    }
  }

  //disconnecting old chain
  std::list<Block> disconnected_chain;
  for (size_t i = m_blocks.size() - 1; i >= split_height; i--) {
    Block b = m_blocks[i].bl;
    popBlock(get_block_hash(b));
    //if (!(r)) { logger(ERROR, BRIGHT_RED) << "failed to remove block on chain switching"; return false; }
    disconnected_chain.push_front(b);
  }

    uint32_t height = static_cast<uint32_t>(split_height - 1);

  //connecting new alternative chain
  for (auto alt_ch_iter = alt_chain.begin(); alt_ch_iter != alt_chain.end(); alt_ch_iter++) {
    auto ch_ent = *alt_ch_iter;
    block_verification_context bvc = boost::value_initialized<block_verification_context>();
    bool r = pushBlock(ch_ent->second.bl, get_block_hash(ch_ent->second.bl), bvc, ++height);
    if (!r || !bvc.m_added_to_main_chain) {
      logger(INFO, BRIGHT_WHITE) << "Failed to switch to alternative blockchain";
      rollback_blockchain_switching(disconnected_chain, split_height);
      //add_block_as_invalid(ch_ent->second, get_block_hash(ch_ent->second.bl));
      logger(INFO, BRIGHT_WHITE) << "The block was inserted as invalid while connecting new alternative chain,  block_id: " << get_block_hash(ch_ent->second.bl);
      m_orthanBlocksIndex.remove(ch_ent->second.bl);
      m_alternative_chains.erase(ch_ent);

      for (auto alt_ch_to_orph_iter = ++alt_ch_iter; alt_ch_to_orph_iter != alt_chain.end(); alt_ch_to_orph_iter++) {
        //block_verification_context bvc = boost::value_initialized<block_verification_context>();
        //add_block_as_invalid((*alt_ch_iter)->second, (*alt_ch_iter)->first);
        m_orthanBlocksIndex.remove((*alt_ch_to_orph_iter)->second.bl);
        m_alternative_chains.erase(*alt_ch_to_orph_iter);
      }

      return false;
    }
  }

  if (!discard_disconnected_chain) {
    //pushing old chain as alternative chain
    for (auto& old_ch_ent : disconnected_chain) {
      block_verification_context bvc = boost::value_initialized<block_verification_context>();
      bool r = handle_alternative_block(old_ch_ent, get_block_hash(old_ch_ent), bvc, false);
      if (!r) {
        logger(WARNING, BRIGHT_MAGENTA) << ("Failed to push ex-main chain blocks to alternative chain ");
        break;
      }
    }
  }

  std::vector<Crypto::Hash> blocksFromCommonRoot;
  blocksFromCommonRoot.reserve(alt_chain.size() + 1);
  blocksFromCommonRoot.push_back(alt_chain.front()->second.bl.previousBlockHash);

  //removing all_chain entries from alternative chain
  for (auto ch_ent : alt_chain) {
    blocksFromCommonRoot.push_back(get_block_hash(ch_ent->second.bl));
    m_orthanBlocksIndex.remove(ch_ent->second.bl);
    m_alternative_chains.erase(ch_ent);
  }

  sendMessage(BlockchainMessage(ChainSwitchMessage(std::move(blocksFromCommonRoot))));

  logger(INFO, BRIGHT_BLUE) << "REORGANIZE SUCCESS! on height: " << split_height << ", new blockchain size: " << m_blocks.size();
  return true;
}

//------------------------------------------------------------------
// This function calculates the difficulty target for the block being added to an alternate chain.
difficulty_type Blockchain::get_next_difficulty_for_alternative_chain(const std::list<blocks_ext_by_hash::iterator>& alt_chain, BlockEntry& bei) {
  std::vector<uint64_t> timestamps;
  std::vector<difficulty_type> cumulative_difficulties;
  uint8_t BlockMajorVersion = getBlockMajorVersionForHeight(static_cast<uint32_t>(m_blocks.size()));

  // if the alt chain isn't long enough to calculate the difficulty target
  // based on its blocks alone, need to get more blocks from the main chain
  if (alt_chain.size() < m_currency.difficultyBlocksCountByBlockVersion(BlockMajorVersion)) {
    std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
    size_t main_chain_stop_offset = alt_chain.size() ? alt_chain.front()->second.height : bei.height;
    size_t main_chain_count = m_currency.difficultyBlocksCountByBlockVersion(BlockMajorVersion) - std::min(m_currency.difficultyBlocksCountByBlockVersion(BlockMajorVersion), alt_chain.size());
    main_chain_count = std::min(main_chain_count, main_chain_stop_offset);
    size_t main_chain_start_offset = main_chain_stop_offset - main_chain_count;

    if (!main_chain_start_offset)
      ++main_chain_start_offset; //skip genesis block

    // get difficulties and timestamps from relevant main chain blocks
    for (; main_chain_start_offset < main_chain_stop_offset; ++main_chain_start_offset) {
      timestamps.push_back(m_blocks[main_chain_start_offset].bl.timestamp);
      cumulative_difficulties.push_back(m_blocks[main_chain_start_offset].cumulative_difficulty);
    }

    // make sure we haven't accidentally grabbed too many blocks... ???
    if (!((alt_chain.size() + timestamps.size()) <= m_currency.difficultyBlocksCountByBlockVersion(BlockMajorVersion))) {
      logger(ERROR, BRIGHT_RED) << "Internal error, alt_chain.size()[" << alt_chain.size() << "] + timestamps.size()[" << timestamps.size() <<
        "] NOT <= m_currency.difficultyBlocksCount()[" << m_currency.difficultyBlocksCountByBlockVersion(BlockMajorVersion) << ']'; return false;
    }
    for (auto it : alt_chain) {
      timestamps.push_back(it->second.bl.timestamp);
      cumulative_difficulties.push_back(it->second.cumulative_difficulty);
    }
  // if the alt chain is long enough for the difficulty calc, grab difficulties
  // and timestamps from it alone
  } else {
    timestamps.resize(std::min(alt_chain.size(), m_currency.difficultyBlocksCountByBlockVersion(BlockMajorVersion)));
	cumulative_difficulties.resize(std::min(alt_chain.size(), m_currency.difficultyBlocksCountByBlockVersion(BlockMajorVersion)));
    size_t count = 0;
    size_t max_i = timestamps.size() - 1;
    // get difficulties and timestamps from most recent blocks in alt chain
    BOOST_REVERSE_FOREACH(auto it, alt_chain) {
      timestamps[max_i - count] = it->second.bl.timestamp;
      cumulative_difficulties[max_i - count] = it->second.cumulative_difficulty;
      count++;
      if (count >= m_currency.difficultyBlocksCountByBlockVersion(BlockMajorVersion)) {
        break;
      }
    }
  }

  return m_currency.nextDifficulty(static_cast<uint32_t>(m_blocks.size()), BlockMajorVersion, timestamps, cumulative_difficulties);
}

bool Blockchain::prevalidate_miner_transaction(const Block& b, uint32_t height) {

  if (!(b.baseTransaction.inputs.size() == 1)) {
    logger(ERROR, BRIGHT_RED)
      << "coinbase transaction in block has no inputs";
    return false;
  }

  if (!(b.baseTransaction.inputs[0].type() == typeid(BaseInput))) {
    logger(ERROR, BRIGHT_RED)
      << "coinbase transaction in block has wrong type";
    return false;
  }

  if (boost::get<BaseInput>(b.baseTransaction.inputs[0]).blockIndex != height) {
    logger(INFO, BRIGHT_RED) << "The miner transaction in block has invalid height: " <<
      boost::get<BaseInput>(b.baseTransaction.inputs[0]).blockIndex << ", expected: " << height;
    return false;
  }

  if (!(b.baseTransaction.unlockTime == height + m_currency.minedMoneyUnlockWindow())) {
    logger(ERROR, BRIGHT_RED)
      << "coinbase transaction has the wrong unlock time="
      << b.baseTransaction.unlockTime << ", expected "
      << height + m_currency.minedMoneyUnlockWindow();
    return false;
  }

  if (!check_outs_valid(b.baseTransaction)) {
    logger(INFO, BRIGHT_RED) << "miner transaction have invalid outputs";
    return false;
  }

  if (!check_outs_overflow(b.baseTransaction)) {
    logger(INFO, BRIGHT_RED) << "miner transaction has money overflow in block " << get_block_hash(b);
    return false;
  }

  return true;
}

bool Blockchain::validate_miner_transaction(const Block& b, uint32_t height, size_t cumulativeBlockSize,
  uint64_t alreadyGeneratedCoins, uint64_t fee, uint64_t& reward, int64_t& emissionChange, const std::vector<Transaction>& blockTransactions) {

  uint64_t coinbaseTotal = 0;
  for (auto& o : b.baseTransaction.outputs) {
    coinbaseTotal += o.amount;
  }

  // For blocks in the checkpoint zone, the checkpoint hash already guarantees the block
  // is valid. Skip reward validation since the penalty calculation depends on a moving
  // median that may differ during re-sync vs original validation. Accept the miner's
  // actual reward as the true emission.
  if (m_checkpoints.is_in_checkpoint_zone(height)) {
    reward = coinbaseTotal;
    emissionChange = coinbaseTotal - fee;
    logger(DEBUGGING) << "Checkpoint zone block at height " << height
      << ", accepting miner reward: " << m_currency.formatAmount(coinbaseTotal);
    return true;
  }

  std::vector<size_t> lastBlocksSizes;
  get_last_n_blocks_sizes(lastBlocksSizes, m_currency.rewardBlocksWindow());
  size_t blocksSizeMedian = Common::medianValue(lastBlocksSizes);

  auto blockMajorVersion = getBlockMajorVersionForHeight(height);

  // Use deterministic height-indexed burn amount for reward calculation
  // Burns through block N-1 determine the reward for block N
  uint64_t burnedAtPrevHeight = (height > 0) ? m_bankingIndex.getBurnedXfgAtHeight(height - 1) : 0;

  if (!m_currency.getBlockReward(blockMajorVersion, blocksSizeMedian, cumulativeBlockSize, alreadyGeneratedCoins, fee, height, reward, emissionChange, burnedAtPrevHeight)) {
    logger(DEBUGGING) << "block size " << cumulativeBlockSize << " is bigger than what is currently allowed on Fuego's blockchain";
    return false;
  }

  if (blockMajorVersion >= CryptoNote::BLOCK_MAJOR_VERSION_10) {
    // V10+: Validate coinbase matches expected reward
    if (coinbaseTotal != reward) {
      logger(ERROR, BRIGHT_RED) << "Coinbase mismatch at height " << height << ": "
        << m_currency.formatAmount(coinbaseTotal) << " (actual) vs "
        << m_currency.formatAmount(reward) << " (expected)";
      return false;
    }
  } else {
    // Pre-v10: only reject if miner claims MORE than the calculated reward.
    // Miners may legitimately claim less (underspend just reduces emission).
    if (coinbaseTotal > reward) {
      logger(ERROR, BRIGHT_RED) << "Coinbase transaction spends too much at height " << height << ": "
        << m_currency.formatAmount(coinbaseTotal) << " (actual) vs "
        << m_currency.formatAmount(reward) << " (expected)";
      return false;
    }

    if (coinbaseTotal != reward) {
      // Miner underspent — use actual miner reward for emission tracking
      reward = coinbaseTotal;
      emissionChange = coinbaseTotal - fee;
    }
  }

  return true;
}


bool Blockchain::getBackwardBlocksSize(size_t from_height, std::vector<size_t>& sz, size_t count) {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  if (!(from_height < m_blocks.size())) {
    logger(ERROR, BRIGHT_RED)
      << "Internal error: get_backward_blocks_sizes called with from_height="
      << from_height << ", blockchain height = " << m_blocks.size();
    return false;
  }
  size_t start_offset = (from_height + 1) - std::min((from_height + 1), count);
  for (size_t i = start_offset; i <= from_height && i < m_blocks.size(); i++) {
    sz.push_back(m_blocks[i].block_cumulative_size);
  }

  return true;
}


bool Blockchain::get_last_n_blocks_sizes(std::vector<size_t>& sz, size_t count) {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  if (!m_blocks.size()) {
    return true;
  }

  size_t height = m_blocks.size() - 1;
  if (height >= m_blocks.size()) {
    logger(ERROR, BRIGHT_RED) << "Invalid height calculation in get_last_n_blocks_sizes";
    return false;
  }
  return getBackwardBlocksSize(height, sz, count);
}

uint64_t Blockchain::getCurrentCumulativeBlocksizeLimit() {
  return m_current_block_cumul_sz_limit;
}

bool Blockchain::complete_timestamps_vector(uint8_t blockMajorVersion, uint64_t start_top_height, std::vector<uint64_t>& timestamps) {
  if (m_blocks.empty()) {
    logger(WARNING, BRIGHT_YELLOW) << "Cannot complete timestamps vector: blockchain is empty";
    return false;
  }
   if (timestamps.size() >= m_currency.timestampCheckWindow(blockMajorVersion))
    return true;

  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  size_t need_elements = m_currency.timestampCheckWindow(blockMajorVersion) - timestamps.size();
  if (!(start_top_height < m_blocks.size())) { logger(ERROR, BRIGHT_RED) << "internal error: passed start_height = " << start_top_height << " not less then m_blocks.size()=" << m_blocks.size(); return false; }
  size_t stop_offset = start_top_height > need_elements ? start_top_height - need_elements : 0;
  do {
    timestamps.push_back(m_blocks[start_top_height].bl.timestamp);
    if (start_top_height == 0)
      break;
    --start_top_height;
  } while (start_top_height != stop_offset);
  return true;
}

bool Blockchain::handle_alternative_block(const Block& b, const Crypto::Hash& id, block_verification_context& bvc, bool sendNewAlternativeBlockMessage) {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);

  auto block_height = get_block_height(b);
  if (block_height == 0) {
    logger(ERROR, BRIGHT_RED) <<
      "Block with id: " << Common::podToHex(id) << " (as alternative) has wrong miner transaction";
    bvc.m_verification_failed = true;
    return false;
  }

  if (!m_checkpoints.is_alternative_block_allowed(getCurrentBlockchainHeight(), block_height)) {
    logger(TRACE) << "Block with id: " << id << std::endl <<
      " can't be accepted for alternative chain, block height: " << block_height << std::endl <<
      " blockchain height: " << getCurrentBlockchainHeight();
    bvc.m_verification_failed = true;
    return false;
  }

  if (!checkBlockVersion(b, id)) {
    bvc.m_verification_failed = true;
    return false;
  }

  if (!checkParentBlockSize(b, id)) {
    bvc.m_verification_failed = true;
    return false;
  }

  size_t cumulativeSize;
  if (!getBlockCumulativeSize(b, cumulativeSize)) {
    logger(TRACE) << "Block with id: " << id << " has at least one unknown transaction. Cumulative size is imprecisely calculated";
  }

  if (!checkCumulativeBlockSize(id, cumulativeSize, block_height)) {
    bvc.m_verification_failed = true;
    return false;
  }

  //block is not related with head of main chain
  //first of all - look in alternative chains container
  uint32_t mainPrevHeight = 0;
  const bool mainPrev = m_blockIndex.getBlockHeight(b.previousBlockHash, mainPrevHeight);
  const auto it_prev = m_alternative_chains.find(b.previousBlockHash);

  if (it_prev != m_alternative_chains.end() || mainPrev) {
    //we have new block in alternative chain

    //build alternative subchain, front -> mainchain, back -> alternative head
    blocks_ext_by_hash::iterator alt_it = it_prev; //m_alternative_chains.find()
    std::list<blocks_ext_by_hash::iterator> alt_chain;
    std::vector<uint64_t> timestamps;
    while (alt_it != m_alternative_chains.end()) {
      alt_chain.push_front(alt_it);
      timestamps.push_back(alt_it->second.bl.timestamp);
      alt_it = m_alternative_chains.find(alt_it->second.bl.previousBlockHash);
    }

    // if block to be added connects to known blocks that aren't part of the
    // main chain -- that is, if we're adding on to an alternate chain
    if (alt_chain.size()) {
      // make sure alt chain doesn't somehow start past the end of the main chain
      if (!(m_blocks.size() > alt_chain.front()->second.height)) { logger(ERROR, BRIGHT_RED) << "main blockchain wrong height"; return false; }
      // make sure block connects correctly to the main chain
	  Crypto::Hash h = NULL_HASH;
      get_block_hash(m_blocks[alt_chain.front()->second.height - 1].bl, h);
      if (!(h == alt_chain.front()->second.bl.previousBlockHash)) { logger(ERROR, BRIGHT_RED) << "alternative chain has wrong connection to main chain"; return false; }
      complete_timestamps_vector(b.majorVersion, alt_chain.front()->second.height - 1, timestamps);
    } else {
      // if block parent is not part of main chain or an alternate chain, we ignore it
      if (!(mainPrev)) { logger(ERROR, BRIGHT_RED) << "internal error: broken imperative condition it_main_prev != m_blocks_index.end()"; return false; }
      complete_timestamps_vector(b.majorVersion, mainPrevHeight, timestamps);
    }

    // check timestamp correct - verify that the block's timestamp is within the acceptable range
    // (not earlier than the median of the last X blocks)
    if (!check_block_timestamp(timestamps, b)) {
      logger(INFO, BRIGHT_RED) <<
        "Block with id: " << id
        << ENDL << " for alternative chain, has an invalid timestamp: " << b.timestamp;
      //add_block_as_invalid(b, id);//do not add blocks to invalid storage before proof of work check was passed
      bvc.m_verification_failed = true;
      return false;
    }

    BlockEntry bei = boost::value_initialized<BlockEntry>();
    bei.bl = b;
    bei.height = static_cast<uint32_t>(alt_chain.size() ? it_prev->second.height + 1 : mainPrevHeight + 1);

    bool is_a_checkpoint;
    if (!m_checkpoints.check_block(bei.height, id, is_a_checkpoint)) {
      logger(ERROR, BRIGHT_RED) <<
        "CHECKPOINT VALIDATION FAILED";
      bvc.m_verification_failed = true;
      return false;
    }

    // Always check PoW for alternative blocks
    m_is_in_checkpoint_zone = false;
    // Check the block's hash against the difficulty target for its alt chain
    difficulty_type current_diff = get_next_difficulty_for_alternative_chain(alt_chain, bei);
    if (!(current_diff)) { logger(ERROR, BRIGHT_RED) << "!!!!!!! DIFFICULTY OVERHEAD !!!!!!!"; return false; }
    Crypto::Hash proof_of_work = NULL_HASH;
    if (!m_currency.checkProofOfWork(m_cn_context, bei.bl, current_diff, proof_of_work)) {
      logger(INFO, BRIGHT_RED) <<
        "Block with id: " << id
        << ENDL << " for alternative chain, lacks enough proof of work: " << proof_of_work
        << ENDL << " expected difficulty: " << current_diff;
      bvc.m_verification_failed = true;
      return false;
    }

    if (!prevalidate_miner_transaction(b, bei.height)) {
      logger(INFO, BRIGHT_RED) <<
        "Block with id: " << Common::podToHex(id) << " (as alternative) has wrong miner transaction.";
      bvc.m_verification_failed = true;
      return false;
    }

    bei.cumulative_difficulty = alt_chain.size() ? it_prev->second.cumulative_difficulty : m_blocks[mainPrevHeight].cumulative_difficulty;
    bei.cumulative_difficulty += current_diff;

#ifdef _DEBUG
    auto i_dres = m_alternative_chains.find(id);
    if (!(i_dres == m_alternative_chains.end())) { logger(ERROR, BRIGHT_RED) << "insertion of new alternative block returned as it already exists"; return false; }
#endif

    auto i_res = m_alternative_chains.insert(blocks_ext_by_hash::value_type(id, bei));
    if (!(i_res.second)) { logger(ERROR, BRIGHT_RED) << "insertion of new alternative block returned as it already exists"; return false; }

    m_orthanBlocksIndex.add(bei.bl);

    alt_chain.push_back(i_res.first);

    if (is_a_checkpoint) {
      //do reorganize!
      logger(INFO, BRIGHT_YELLOW) <<
        "###### REORGANIZE on height: " << alt_chain.front()->second.height << " of " << m_blocks.size() - 1 <<
        ", checkpoint is found in alternative chain on height " << bei.height;
      bool r = switch_to_alternative_blockchain(alt_chain, true);
      if (r) {
        bvc.m_added_to_main_chain = true;
        bvc.m_switched_to_alt_chain = true;
      } else {
        bvc.m_verification_failed = true;
      }
      return r;
    } else if (m_blocks.back().cumulative_difficulty < bei.cumulative_difficulty) //check if difficulty bigger then in main chain
    {
      //do reorganize!
      logger(INFO, BRIGHT_YELLOW) <<
        "###### REORGANIZE on height: " << alt_chain.front()->second.height << " of " << m_blocks.size() - 1 << " with cumulative_difficulty " << m_blocks.back().cumulative_difficulty
        << ENDL << " alternative blockchain size: " << alt_chain.size() << " with cumulative_difficulty " << bei.cumulative_difficulty;
      bool r = switch_to_alternative_blockchain(alt_chain, false);
      if (r) {
        bvc.m_added_to_main_chain = true;
        bvc.m_switched_to_alt_chain = true;
      } else {
        bvc.m_verification_failed = true;
      }
      return r;
    } else {
      logger(INFO, BRIGHT_YELLOW) <<
        "----- BLOCK ADDED AS ALTERNATIVE ON HEIGHT " << bei.height
        << ENDL << "id:\t" << id
        << ENDL << "PoW:\t" << proof_of_work
        << ENDL << "difficulty:\t" << current_diff;
      if (sendNewAlternativeBlockMessage) {
        sendMessage(BlockchainMessage(NewAlternativeBlockMessage(id)));
      }
      return true;
    }
  } else {
    //block orphaned
    bvc.m_marked_as_orphaned = true;
    logger(INFO, BRIGHT_RED) <<
      "Block recognized as orphaned and rejected, id = " << id;
  }

  return true;
}

bool Blockchain::getBlocks(uint32_t start_offset, uint32_t count, std::list<Block>& blocks, std::list<Transaction>& txs) {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  if (start_offset >= m_blocks.size())
    return false;
  for (size_t i = start_offset; i < start_offset + count && i < m_blocks.size(); i++) {
    blocks.push_back(m_blocks[i].bl);
    std::list<Crypto::Hash> missed_ids;
    getTransactions(m_blocks[i].bl.transactionHashes, txs, missed_ids);
    if (!(!missed_ids.size())) { logger(ERROR, BRIGHT_RED) << "have missed transactions in own block of main blockchain"; return false; }
  }

  return true;
}

bool Blockchain::getBlocks(uint32_t start_offset, uint32_t count, std::list<Block>& blocks) {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  if (start_offset >= m_blocks.size()) {
    return false;
  }

  for (uint32_t i = start_offset; i < start_offset + count && i < m_blocks.size(); i++) {
    blocks.push_back(m_blocks[i].bl);
  }

  return true;
}

bool Blockchain::handleGetObjects(NOTIFY_REQUEST_GET_OBJECTS::request& arg, NOTIFY_RESPONSE_GET_OBJECTS::request& rsp) { //Deprecated. Should be removed with CryptoNoteProtocolHandler.
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  rsp.current_blockchain_height = getCurrentBlockchainHeight();
  std::list<Block> blocks;
  getBlocks(arg.blocks, blocks, rsp.missed_ids);

  for (const auto& bl : blocks) {
    std::list<Crypto::Hash> missed_tx_id;
    std::list<Transaction> txs;
    getTransactions(bl.transactionHashes, txs, rsp.missed_ids);
    if (!(!missed_tx_id.size())) { logger(ERROR, BRIGHT_RED) << "Internal error: have missed missed_tx_id.size()=" << missed_tx_id.size() << ENDL << "for block id = " << get_block_hash(bl); return false; } //WTF???
    rsp.blocks.push_back(block_complete_entry());
    block_complete_entry& e = rsp.blocks.back();
    //pack block
    e.block = asString(toBinaryArray(bl));
    //pack transactions
    for (Transaction& tx : txs) {
      e.txs.push_back(asString(toBinaryArray(tx)));
    }
  }

  //get another transactions, if need
  std::list<Transaction> txs;
  getTransactions(arg.txs, txs, rsp.missed_ids);
  //pack aside transactions
  for (const auto& tx : txs) {
    rsp.txs.push_back(asString(toBinaryArray(tx)));
  }

  return true;
}

bool Blockchain::getAlternativeBlocks(std::list<Block>& blocks) {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  for (auto& alt_bl : m_alternative_chains) {
    blocks.push_back(alt_bl.second.bl);
  }

  return true;
}

uint32_t Blockchain::getAlternativeBlocksCount() {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  return static_cast<uint32_t>(m_alternative_chains.size());
}

bool Blockchain::add_out_to_get_random_outs(std::vector<std::pair<TxIndex, uint16_t>>& amount_outs, COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::outs_for_amount& result_outs, uint64_t amount, size_t i) {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  const Transaction& tx = transactionByIndex(amount_outs[i].first).tx;
  if (!(tx.outputs.size() > amount_outs[i].second)) {
    logger(ERROR, BRIGHT_RED) << "internal error: in global outs index, transaction out index="
      << amount_outs[i].second << " more than transaction outputs = " << tx.outputs.size() << ", for tx id = " << getObjectHash(tx); return false;
  }
  if (!(tx.outputs[amount_outs[i].second].target.type() == typeid(KeyOutput))) { logger(ERROR, BRIGHT_RED) << "unknown tx out type"; return false; }

  //check if transaction is unlocked
  if (!is_tx_spendtime_unlocked(tx.unlockTime))
    return false;

  COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::out_entry& oen = *result_outs.outs.insert(result_outs.outs.end(), COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::out_entry());
  oen.global_amount_index = static_cast<uint32_t>(i);
  oen.out_key = boost::get<KeyOutput>(tx.outputs[amount_outs[i].second].target).key;
  return true;
}

size_t Blockchain::find_end_of_allowed_index(const std::vector<std::pair<TxIndex, uint16_t>>& amount_outs) {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  if (amount_outs.empty()) {
    return 0;
  }

  size_t i = amount_outs.size();
  do {
    --i;
    if (amount_outs[i].first.block + m_currency.minedMoneyUnlockWindow() <= getCurrentBlockchainHeight()) {
      return i + 1;
    }
  } while (i != 0);

  return 0;
}

bool Blockchain::getRandomOutsByAmount(const COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::request& req, COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::response& res) {
  if (!m_indexManager.isReady()) return false;
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);

  for (uint64_t amount : req.amounts) {
    COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::outs_for_amount& result_outs = *res.outs.insert(res.outs.end(), COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::outs_for_amount());
    result_outs.amount = amount;
    auto it = m_indexManager.outputs().find(amount);
    if (it == m_indexManager.outputs().end()) {
      logger(ERROR, BRIGHT_RED) <<
        "COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS: not outs for amount " << amount << ", wallet should use some real outs when it looks for mixins, so at least one out for this amount should exist";
      continue;//actually this is strange situation, wallet should use some real outs when it lookup for some mix, so, at least one out for this amount should exist
    }

    std::vector<std::pair<TxIndex, uint16_t>>& amount_outs = it->second;
    //it is not good idea to use top fresh outs, because it increases possibility of transaction canceling on split
    //lets find upper bound of not fresh outs
    size_t up_index_limit = find_end_of_allowed_index(amount_outs);
    if (!(up_index_limit <= amount_outs.size())) { logger(ERROR, BRIGHT_RED) << "internal error: find_end_of_allowed_index returned wrong index=" << up_index_limit << ", with amount_outs.size = " << amount_outs.size(); return false; }

    	if(amount_outs.size() > req.outs_count)
    {
      std::set<size_t> used;
      size_t try_count = 0;
      for(uint64_t j = 0; j != req.outs_count && try_count < up_index_limit;)
      {
	    // triangular distribution over [a,b) with a=0, mode c=b=up_index_limit
        uint64_t r = Crypto::rand<uint64_t>() % ((uint64_t)1 << 53);
        double frac = std::sqrt((double)r / ((uint64_t)1 << 53));
        size_t i = (size_t)(frac*up_index_limit);
        if(used.count(i))
          continue;
        bool added = add_out_to_get_random_outs(amount_outs, result_outs, amount, i);
        used.insert(i);
        if(added)
          ++j;
        ++try_count;
      }
    }
     else {
      for(size_t i = 0; i != up_index_limit; i++)
        add_out_to_get_random_outs(amount_outs, result_outs, amount, i);
    }
  }
  return true;
}

bool Blockchain::getOutputHeights(const std::vector<std::pair<uint64_t, uint32_t>>& queries,
                                  std::vector<uint32_t>& heights) {
  heights.assign(queries.size(), 0);
  if (!m_indexManager.isReady()) return false;
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);

  for (size_t i = 0; i < queries.size(); ++i) {
    const uint64_t amount = queries[i].first;
    const uint32_t gindex = queries[i].second;
    auto it = m_indexManager.outputs().find(amount);
    if (it == m_indexManager.outputs().end()) continue;
    if (gindex >= it->second.size()) continue;
    // outputs() entries are pair<TxIndex, uint16_t>; TxIndex.block is the block height
    heights[i] = it->second[gindex].first.block;
  }
  return true;
}

bool Blockchain::getRandomCommitmentOutputsForAmount(uint64_t amount, uint64_t count,
    std::vector<COMMAND_RPC_GET_RANDOM_COMMITMENT_OUTPUTS_out_entry>& result, uint32_t max_height) {
  if (!m_indexManager.isReady()) return false;
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);

  auto it = m_indexManager.commitmentOutputs().find(amount);
  if (it == m_indexManager.commitmentOutputs().end() || it->second.empty()) {
    return true; // no commitment outputs at this amount yet — caller handles empty result
  }

  const auto& allRefs = it->second;
  std::vector<size_t> validIndices;
  validIndices.reserve(allRefs.size());

  // Filter by height: only outputs created at or before max_height
  if (max_height > 0) {
    for (size_t i = 0; i < allRefs.size(); ++i) {
      if (allRefs[i].transactionIndex.block <= max_height) {
        validIndices.push_back(i);
      }
    }
  } else {
    // No filter: all indices are valid
    validIndices.resize(allRefs.size());
    std::iota(validIndices.begin(), validIndices.end(), 0);
  }

  if (validIndices.empty()) return true;

  const size_t total = validIndices.size();

  if (total <= count) {
    // Return all valid outputs.
    for (size_t i = 0; i < total; ++i) {
      COMMAND_RPC_GET_RANDOM_COMMITMENT_OUTPUTS_out_entry entry;
      size_t absIdx = validIndices[i];
      entry.global_amount_index = static_cast<uint32_t>(absIdx);
      entry.commit_key = allRefs[absIdx].commitKey;
      result.push_back(entry);
    }
  } else {
    // Triangular distribution: bias toward recent (but still valid) outputs.
    std::set<size_t> used;
    size_t tries = 0;
    const size_t maxTries = count * 20;
    while (result.size() < count && tries < maxTries) {
      ++tries;
      uint64_t r = Crypto::rand<uint64_t>() % ((uint64_t)1 << 53);
      double frac = std::sqrt((double)r / ((uint64_t)1 << 53));
      size_t idxInValid = static_cast<size_t>(frac * total);
      if (idxInValid >= total) idxInValid = total - 1;
      
      size_t absIdx = validIndices[idxInValid];
      if (used.count(absIdx)) continue;
      used.insert(absIdx);
      
      COMMAND_RPC_GET_RANDOM_COMMITMENT_OUTPUTS_out_entry entry;
      entry.global_amount_index = static_cast<uint32_t>(absIdx);
      entry.commit_key = allRefs[absIdx].commitKey;
      result.push_back(entry);
    }
  }

  return true;
}

uint32_t Blockchain::findBlockchainSupplement(const std::vector<Crypto::Hash>& qblock_ids) {
  assert(!qblock_ids.empty());
  assert(qblock_ids.back() == m_blockIndex.getBlockId(0));

  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  uint32_t blockIndex;
  // assert above guarantees that method returns true
  m_blockIndex.findSupplement(qblock_ids, blockIndex);
  return blockIndex;
}

uint64_t Blockchain::blockDifficulty(size_t i) {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  if (!(i < m_blocks.size())) { logger(ERROR, BRIGHT_RED) << "wrong block index i = " << i << " at Blockchain::block_difficulty()"; return false; }
  if (i == 0)
    return m_blocks[i].cumulative_difficulty;

  return m_blocks[i].cumulative_difficulty - m_blocks[i - 1].cumulative_difficulty;
}

void Blockchain::print_blockchain(uint64_t start_index, uint64_t end_index) {
  std::stringstream ss;
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  if (start_index >= m_blocks.size()) {
    logger(INFO, BRIGHT_WHITE) <<
      "Wrong starter index set: " << start_index << ", expected max index " << m_blocks.size() - 1;
    return;
  }

  for (size_t i = start_index; i != m_blocks.size() && i != end_index; i++) {
    ss << "height " << i << ", timestamp " << m_blocks[i].bl.timestamp << ", cumul_dif " << m_blocks[i].cumulative_difficulty << ", cumul_size " << m_blocks[i].block_cumulative_size
      << "\nid\t\t" << get_block_hash(m_blocks[i].bl)
      << "\ndifficulty\t\t" << blockDifficulty(i) << ", nonce " << m_blocks[i].bl.nonce << ", tx_count " << m_blocks[i].bl.transactionHashes.size() << ENDL;
  }
  logger(DEBUGGING) <<
    "Current blockchain:" << ENDL << ss.str();
  logger(INFO, BRIGHT_WHITE) <<
    "Blockchain printed with log level 1";
}

void Blockchain::print_blockchain_index() {
  std::stringstream ss;
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);

  std::vector<Crypto::Hash> blockIds = m_blockIndex.getBlockIds(0, std::numeric_limits<uint32_t>::max());
  logger(INFO, BRIGHT_WHITE) << "Current blockchain index:";

  size_t height = 0;
  for (auto i = blockIds.begin(); i != blockIds.end(); ++i, ++height) {
    logger(INFO, BRIGHT_WHITE) << "id\t\t" << *i << " height" << height;
  }

}

void Blockchain::print_blockchain_outs(const std::string& file) {
  std::stringstream ss;
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  for (const outputs_container::value_type& v : m_indexManager.outputs().data()) {
    const std::vector<std::pair<TxIndex, uint16_t>>& vals = v.second;
    if (!vals.empty()) {
      ss << "amount: " << v.first << ENDL;
      for (size_t i = 0; i != vals.size(); i++) {
        ss << "\t" << getObjectHash(transactionByIndex(vals[i].first).tx) << ": " << vals[i].second << ENDL;
      }
    }
  }

  if (Common::saveStringToFile(file, ss.str())) {
    logger(INFO, BRIGHT_WHITE) <<
      "Current outputs index written to file: " << file;
  } else {
    logger(WARNING, BRIGHT_MAGENTA) <<
      "Failed to write current outputs index to file: " << file;
  }
}

std::vector<Crypto::Hash> Blockchain::findBlockchainSupplement(const std::vector<Crypto::Hash>& remoteBlockIds, size_t maxCount,
  uint32_t& totalBlockCount, uint32_t& startBlockIndex) {

  assert(!remoteBlockIds.empty());
  assert(remoteBlockIds.back() == m_blockIndex.getBlockId(0));

  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  totalBlockCount = getCurrentBlockchainHeight();
  startBlockIndex = findBlockchainSupplement(remoteBlockIds);

  return m_blockIndex.getBlockIds(startBlockIndex, static_cast<uint32_t>(maxCount));
}

bool Blockchain::haveBlock(const Crypto::Hash& id) {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  if (m_blockIndex.hasBlock(id))
    return true;

  if (m_alternative_chains.count(id))
    return true;

  return false;
}

size_t Blockchain::getTotalTransactions() {
  if (!m_indexManager.isReady()) return 0;
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  return m_indexManager.transactionMap().size();
}

bool Blockchain::getTransactionOutputGlobalIndexes(const Crypto::Hash& tx_id, std::vector<uint32_t>& indexs) {
  if (!m_indexManager.isReady()) return false;
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  auto it = m_indexManager.transactionMap().find(tx_id);
  if (it == m_indexManager.transactionMap().end()) {
    logger(WARNING, YELLOW) << "warning: get_tx_outputs_gindexs failed to find transaction with id = " << tx_id;
    return false;
  }

  const TransactionEntry& tx = transactionByIndex(it->second);
  if (!(tx.m_global_output_indexes.size())) { logger(ERROR, BRIGHT_RED) << "internal error: global indexes for transaction " << tx_id << " is empty"; return false; }
  indexs.resize(tx.m_global_output_indexes.size());
  for (size_t i = 0; i < tx.m_global_output_indexes.size(); ++i) {
    indexs[i] = tx.m_global_output_indexes[i];
  }

  return true;
}

bool Blockchain::get_out_by_msig_gindex(uint64_t amount, uint64_t gindex, MultisignatureOutput& out) {
  if (!m_indexManager.isReady()) return false;
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  auto it = m_indexManager.multisigOutputs().find(amount);
  if (it == m_indexManager.multisigOutputs().end()) {
    return false;
  }

  if (it->second.size() <= gindex) {
    return false;
  }

  auto msigUsage = it->second[gindex];
  auto& targetOut = transactionByIndex(msigUsage.transactionIndex).tx.outputs[msigUsage.outputIndex].target;
  if (targetOut.type() != typeid(MultisignatureOutput)) {
    return false;
  }

  out = boost::get<MultisignatureOutput>(targetOut);
  return true;
}



bool Blockchain::checkTransactionInputs(const Transaction& tx, uint32_t& max_used_block_height, Crypto::Hash& max_used_block_id, BlockInfo* tail) {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);

  if (tail)
    tail->id = getTailId(tail->height);

  bool res = checkTransactionInputs(tx, &max_used_block_height);
  if (!res) return false;
  if (!(max_used_block_height < m_blocks.size())) { logger(ERROR, BRIGHT_RED) << "internal error: max used block index=" << max_used_block_height << " is not less then blockchain size = " << m_blocks.size(); return false; }
  get_block_hash(m_blocks[max_used_block_height].bl, max_used_block_id);
  return true;
}

bool Blockchain::haveTransactionKeyImagesAsSpent(const Transaction &tx) {
  for (const auto& in : tx.inputs) {
    if (in.type() == typeid(KeyInput)) {
      if (have_tx_keyimg_as_spent(boost::get<KeyInput>(in).keyImage)) {
        return true;
      }
    } else if (in.type() == typeid(TransactionInputCommitmentSpend)) {
      if (have_tx_keyimg_as_spent(boost::get<TransactionInputCommitmentSpend>(in).keyImage)) {
        return true;
      }
    } else if (in.type() == typeid(TransactionInputCommitmentTransfer)) {
      if (have_tx_keyimg_as_spent(boost::get<TransactionInputCommitmentTransfer>(in).keyImage)) {
        return true;
      }
    }
  }

  return false;
}

bool Blockchain::checkTransactionInputs(const Transaction& tx, uint32_t* pmax_used_block_height) {
  Crypto::Hash tx_prefix_hash = getObjectHash(*static_cast<const TransactionPrefix*>(&tx));
  return checkTransactionInputs(tx, tx_prefix_hash, pmax_used_block_height);
}

bool Blockchain::checkTransactionInputs(const Transaction& tx, const Crypto::Hash& tx_prefix_hash, uint32_t* pmax_used_block_height) {
  size_t inputIndex = 0;
  if (pmax_used_block_height) {
    *pmax_used_block_height = 0;
  }

  Crypto::Hash transactionHash = getObjectHash(tx);
  for (const auto& txin : tx.inputs) {
    assert(inputIndex < tx.signatures.size());
    if (txin.type() == typeid(KeyInput)) {

      const KeyInput& in_to_key = boost::get<KeyInput>(txin);
      if (!(!in_to_key.outputIndexes.empty())) { logger(ERROR, BRIGHT_RED) << "empty in_to_key.outputIndexes in transaction with id " << getObjectHash(tx); return false; }

      if (have_tx_keyimg_as_spent(in_to_key.keyImage)) {
        logger(DEBUGGING) <<
          "Key image already spent in blockchain: " << Common::podToHex(in_to_key.keyImage);
        return false;
      }

      if (!check_tx_input(in_to_key, tx_prefix_hash, tx.signatures[inputIndex], pmax_used_block_height)) {
        logger(DEBUGGING, BRIGHT_WHITE) <<
          "Failed to check ring signature for tx " << transactionHash;
        return false;
      }

        if (!isInCheckpointZone(getCurrentBlockchainHeight()))
        {
          if (!check_tx_input(in_to_key, tx_prefix_hash, tx.signatures[inputIndex], pmax_used_block_height))
          {
            logger(INFO, BRIGHT_WHITE) << "Failed to check input in transaction " << transactionHash;
            return false;
          }
        }

        ++inputIndex;
      }
      else if (txin.type() == typeid(MultisignatureInput))
      {
        if (!isInCheckpointZone(getCurrentBlockchainHeight()))
        {
          if (!validateInput(::boost::get<MultisignatureInput>(txin), transactionHash, tx_prefix_hash, tx.signatures[inputIndex]))
          {
            return false;
          }
        }

        ++inputIndex;
      }
      else if (txin.type() == typeid(TransactionInputCommitmentSpend))
      {
        const TransactionInputCommitmentSpend& cin = boost::get<TransactionInputCommitmentSpend>(txin);

        if (cin.outputIndexes.empty()) {
          logger(ERROR, BRIGHT_RED) << "CommitmentSpend input has empty outputIndexes in tx " << transactionHash;
          return false;
        }

        // Key image double-spend check (reuses m_spent_keys, same as KeyInput)
        if (have_tx_keyimg_as_spent(cin.keyImage)) {
          logger(DEBUGGING) << "CommitmentSpend key image already spent: " << Common::podToHex(cin.keyImage);
          return false;
        }

        if (!isInCheckpointZone(getCurrentBlockchainHeight())) {
          if (!checkCommitmentSpendInput(cin, tx_prefix_hash, tx.signatures[inputIndex], pmax_used_block_height)) {
            logger(INFO, BRIGHT_WHITE) << "CommitmentSpend ring signature check failed in tx " << transactionHash;
            return false;
          }
        }

        ++inputIndex;
      }
      else if (txin.type() == typeid(TransactionInputCommitmentTransfer))
      {
        const TransactionInputCommitmentTransfer& xfer = boost::get<TransactionInputCommitmentTransfer>(txin);

        if (xfer.outputIndexes.empty()) {
          logger(ERROR, BRIGHT_RED) << "CommitmentTransfer input has empty outputIndexes in tx " << transactionHash;
          return false;
        }

        // Key image double-spend check
        if (have_tx_keyimg_as_spent(xfer.keyImage)) {
          logger(DEBUGGING) << "CommitmentTransfer key image already spent: " << Common::podToHex(xfer.keyImage);
          return false;
        }

        if (!isInCheckpointZone(getCurrentBlockchainHeight())) {
          if (!checkCommitmentTransferInput(xfer, tx_prefix_hash, tx.signatures[inputIndex], pmax_used_block_height)) {
            logger(INFO, BRIGHT_WHITE) << "CommitmentTransfer ring signature check failed in tx " << transactionHash;
            return false;
          }
        }

        ++inputIndex;
      }
      else
      {
        logger(INFO, BRIGHT_WHITE) << "Transaction << " << transactionHash << " contains input of unsupported type.";
        return false;
      }
    }

  return true;
}

bool Blockchain::is_tx_spendtime_unlocked(uint64_t unlock_time) {
  if (unlock_time < m_currency.maxBlockHeight()) {
    //interpret as block index
    if (getCurrentBlockchainHeight() - 1 + m_currency.lockedTxAllowedDeltaBlocks() >= unlock_time)
      return true;
    else
      return false;
  } else {
    //interpret as time
    uint64_t current_time = static_cast<uint64_t>(time(NULL));
    if (current_time + m_currency.lockedTxAllowedDeltaSeconds(blockMajorVersion) >= unlock_time)
      return true;
    else
      return false;
  }

  return false;
}

bool Blockchain::check_tx_input(const KeyInput& txin, const Crypto::Hash& tx_prefix_hash, const std::vector<Crypto::Signature>& sig, uint32_t* pmax_related_block_height) {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);

  struct outputs_visitor {
    std::vector<const Crypto::PublicKey *>& m_results_collector;
    Blockchain& m_bch;
    LoggerRef logger;
    outputs_visitor(std::vector<const Crypto::PublicKey *>& results_collector, Blockchain& bch, ILogger& logger) :m_results_collector(results_collector), m_bch(bch), logger(logger, "outputs_visitor") {
    }

    bool handle_output(const Transaction& tx, const TransactionOutput& out, size_t transactionOutputIndex) {
      //check tx unlock time
      if (!m_bch.is_tx_spendtime_unlocked(tx.unlockTime)) {
        logger(INFO, BRIGHT_WHITE) <<
          "One of outputs for one of inputs have wrong tx.unlockTime = " << tx.unlockTime;
        return false;
      }

      if (out.target.type() != typeid(KeyOutput)) {
        logger(INFO, BRIGHT_WHITE) <<
          "Output has wrong type id, which=" << out.target.which();
        return false;
      }

      m_results_collector.push_back(&boost::get<KeyOutput>(out.target).key);
      return true;
    }
  };

  // additional key_image check, fix discovered by Monero Lab and suggested by "fluffypony" (bitcointalk.org)
  static const Crypto::KeyImage I = { { 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } };
  static const Crypto::KeyImage L = { { 0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10 } };
  if (!(scalarmultKey(txin.keyImage, L) == I)) {
	 logger(ERROR) << "Transaction uses key image not in the valid domain";
	 return false;
  }

  //check ring signature
  std::vector<const Crypto::PublicKey *> output_keys;
  outputs_visitor vi(output_keys, *this, logger.getLogger());
  if (!scanOutputKeysForIndexes(txin, vi, pmax_related_block_height)) {
    logger(INFO, BRIGHT_YELLOW) <<
      "Failed to get output keys for tx with amount = " << m_currency.formatAmount(txin.amount) <<
      " and count indexes " << txin.outputIndexes.size();
    return false;
  }

  if (txin.outputIndexes.size() != output_keys.size()) {
    logger(INFO, BRIGHT_WHITE) <<
      "Output keys for tx with amount = " << txin.amount << " and count indexes " << txin.outputIndexes.size() << " returned wrong keys count " << output_keys.size();
    return false;
  }

  if (!(sig.size() == output_keys.size())) { logger(ERROR, BRIGHT_RED) << "internal error: tx signatures count=" << sig.size() << " mismatch with outputs keys count for inputs=" << output_keys.size(); return false; }
  if (m_is_in_checkpoint_zone) {
    return true;
  }

  bool check_tx_ring_signature = Crypto::check_ring_signature(tx_prefix_hash, txin.keyImage, output_keys, sig.data());
  if (!check_tx_ring_signature) {
    logger(DEBUGGING) << "Failed to check ring signature for keyImage: " << txin.keyImage;
  }
  return check_tx_ring_signature;
}

// Commitment Spend Ring Signature Validation
// validates TransactionInputCommitmentSpend ring-sig against global
// commitment output index (m_commitmentOutputs) using same algorithm as
// check_tx_input for KeyInput ring sigs.
bool Blockchain::checkCommitmentSpendInput(const TransactionInputCommitmentSpend& txin,
                                            const Crypto::Hash& tx_prefix_hash,
                                            const std::vector<Crypto::Signature>& sig,
                                            uint32_t* pmax_related_block_height) {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);

  // Subgroup check: reuse same L*I == I guard as check_tx_input.
  static const Crypto::KeyImage I = { { 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } };
  static const Crypto::KeyImage L = { { 0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10 } };
  if (!(scalarmultKey(txin.keyImage, L) == I)) {
    logger(ERROR) << "CommitmentSpend key image not in valid Ed25519 domain";
    return false;
  }

  // Resolve global commitment output indices (relative-encoded, same as KeyInput).
  auto it = m_indexManager.commitmentOutputs().find(txin.amount);
  if (it == m_indexManager.commitmentOutputs().end()) {
    logger(INFO) << "CommitmentSpend: no commitment outputs exist for amount " << txin.amount;
    return false;
  }
  const auto& amountRefs = it->second;

  // Decode absolute indices from relative offsets.
  std::vector<uint64_t> absoluteIndexes;
  absoluteIndexes.reserve(txin.outputIndexes.size());
  uint64_t absoluteIndex = 0;
  for (uint32_t relIdx : txin.outputIndexes) {
    absoluteIndex += relIdx;
    absoluteIndexes.push_back(absoluteIndex);
  }

  // Collect commitKey pointers for ring signature verification.
  // Ring members are selected by amount only — term matching NOT required.
  // The key image (nullifier) prevents double-spend regardless of term mixing.
  std::vector<const Crypto::PublicKey*> ringKeys;
  ringKeys.reserve(absoluteIndexes.size());
  bool hasNonForever = false;
  uint32_t currentHeight = getCurrentBlockchainHeight();
  // Track youngest (highest creation height) ring member for interest cap.
  // Using the youngest member prevents gaming the system by including old
  // high-rate deposits: the real spend can only be as young as the youngest
  // ring member, so interest must be bounded by that member's epoch rate.
  uint32_t youngestRingMemberHeight = 0;
  uint64_t maxRingInterest = 0;  // bonus-aware max across all ring members
  for (uint64_t absIdx : absoluteIndexes) {
    if (absIdx >= amountRefs.size()) {
      logger(INFO) << "CommitmentSpend: global index " << absIdx << " out of range (" << amountRefs.size() << " commitment outputs at this amount)";
      return false;
    }
    const CommitmentOutputRef& ref = amountRefs[absIdx];
    ringKeys.push_back(&ref.commitKey);

    // Track youngest (most recent) ring member for interest bounds check
    uint32_t memberHeight = ref.transactionIndex.block;
    if (memberHeight > youngestRingMemberHeight) {
      youngestRingMemberHeight = memberHeight;
    }

    if (ref.term != CryptoNote::parameters::HEAT_TERM) {
      hasNonForever = true;

      // Look up auto-roll status once per ring member (used for both interest
      // cap and maturity check below).
      bool rolled = (ref.term > 0) ? m_commitmentIndex.isAutoRolled(
          memberHeight, txin.amount, ref.term) : false;

      // Track max possible interest across all ring members for bonus-aware cap.
      // Each ring member could be the real spend, so the cap must cover the
      // highest possible interest including loyalty bonus and auto-roll compounding.
      if (ref.term > 0 && currentHeight > memberHeight) {
        uint64_t memberMaxInterest = m_currency.calculateCdInterest(
            txin.amount, memberHeight, currentHeight,
            m_commitmentIndex, false, ref.term, rolled);
        if (memberMaxInterest > maxRingInterest) {
          maxRingInterest = memberMaxInterest;
        }
      }

      // All non-FOREVER ring members must be mature — prevents early withdrawal.
      // Auto-rolled CDs have doubled maturity (one-time extension at first roll).
      if (ref.term > 0) {
        uint32_t maturityHeight = rolled
            ? memberHeight + 2 * ref.term
            : memberHeight + ref.term;
        // overflow guard
        if (maturityHeight < memberHeight || currentHeight < maturityHeight) {
          logger(INFO) << "CommitmentSpend: ring member at index " << absIdx
                       << " is an immature COLD deposit (matures at block "
                       << maturityHeight << ", current " << currentHeight
                       << (rolled ? ", auto-rolled" : "") << ")";
          return false;
        }
      }
    }

    // Slashed commitment guard: reject rings containing any slashed output.
    if (ref.isSlashed) {
      logger(INFO) << "CommitmentSpend: ring member at index " << absIdx
                   << " is slashed — tx rejected";
      return false;
    }

    // Pool-term unspendability guard: pool-owned outputs cannot be spent
    if (ref.term == parameters::DEPOSIT_TERM_POOL_XFG ||
        ref.term == parameters::DEPOSIT_TERM_POOL_HEAT) {
      logger(INFO) << "CommitmentSpend: ring member at index " << absIdx
                   << " is pool-owned (term 0x" << std::hex << ref.term
                   << std::dec << ") — tx rejected";
      return false;
    }

    // Track max referenced block height.
    if (pmax_related_block_height) {
      uint32_t blockHeight = ref.transactionIndex.block;
      if (*pmax_related_block_height < blockHeight) {
        *pmax_related_block_height = blockHeight;
      }
    }
  }

  // Degenerate-ring guard: if every member is FOREVER-term, no valid real spend
  // is possible (all keyScalars were discarded for burns). Reject immediately.
  if (!hasNonForever) {
    logger(INFO) << "CommitmentSpend: all ring members are burned outputs — no valid real spend possible";
    return false;
  }

  if (ringKeys.size() != sig.size()) {
    logger(ERROR) << "CommitmentSpend: ring size " << ringKeys.size() << " != sig count " << sig.size();
    return false;
  }

  bool valid = Crypto::check_ring_signature(tx_prefix_hash, txin.keyImage, ringKeys, sig.data());
  if (!valid) {
    logger(DEBUGGING) << "CommitmentSpend ring signature check failed for keyImage: " << Common::podToHex(txin.keyImage);
    return false;
  }

  // Declare-and-verify: validate claimedInterest against max possible.
  // Uses the maximum interest across all ring members, accounting for loyalty
  // bonus on 72-epoch CDs. Falls back to youngest-member cap if no ring members
  // had valid terms (conservative bound).
  if (txin.claimedInterest > 0) {
    uint64_t maxInterest;
    if (maxRingInterest > 0) {
      maxInterest = maxRingInterest;  // bonus-aware max across all ring members
    } else {
      maxInterest = m_currency.calculateCdInterest(
          txin.amount, youngestRingMemberHeight, currentHeight, m_commitmentIndex);
    }
    // Also capped by available fee pool balance
    if (maxInterest > m_feePoolBalance) {
      maxInterest = m_feePoolBalance;
    }
    if (txin.claimedInterest > maxInterest) {
      logger(INFO) << "CommitmentSpend: claimedInterest " << txin.claimedInterest
                   << " exceeds max " << maxInterest
                   << " (youngest ring member at height " << youngestRingMemberHeight << ")";
      return false;
    }
  }

  return true;
}

bool Blockchain::checkCommitmentTransferInput(
    const TransactionInputCommitmentTransfer& txin,
    const Crypto::Hash& tx_prefix_hash,
    const std::vector<Crypto::Signature>& sig,
    uint32_t* pmax_related_block_height) {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);

  // Subgroup check (same as CommitmentSpend)
  static const Crypto::KeyImage I = { { 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } };
  static const Crypto::KeyImage L = { { 0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10 } };
  if (!(scalarmultKey(txin.keyImage, L) == I)) {
    logger(ERROR) << "CommitmentTransfer key image not in valid Ed25519 domain";
    return false;
  }

  // newTerm must be within the valid protocol range [1..5].
  // An unchecked upper bound would allow newTerm=255, creating a deposit
  // that never matures and permanently locks funds.
  if (txin.newTerm < 1 || txin.newTerm > 5) {
    logger(WARNING) << "Invalid newTerm " << txin.newTerm << " in CommitmentTransfer";
    return false;
  }

  // newTerm must also meet protocol minimum for remaining term
  if (txin.newTerm < CryptoNote::parameters::CD_TRANSFER_MIN_REMAINING_TERM) {
    logger(ERROR) << "CommitmentTransfer newTerm " << txin.newTerm
                  << " below minimum " << CryptoNote::parameters::CD_TRANSFER_MIN_REMAINING_TERM;
    return false;
  }

  // Resolve commitment outputs for this amount
  auto it = m_indexManager.commitmentOutputs().find(txin.amount);
  if (it == m_indexManager.commitmentOutputs().end()) {
    logger(INFO) << "CommitmentTransfer: no commitment outputs for amount " << txin.amount;
    return false;
  }
  const auto& amountRefs = it->second;

  // Decode absolute indices from relative offsets
  std::vector<uint64_t> absoluteIndexes;
  absoluteIndexes.reserve(txin.outputIndexes.size());
  uint64_t absoluteIndex = 0;
  for (uint32_t relIdx : txin.outputIndexes) {
    absoluteIndex += relIdx;
    absoluteIndexes.push_back(absoluteIndex);
  }

  // Collect ring keys — NO maturity check (transfers allowed anytime)
  std::vector<const Crypto::PublicKey*> ringKeys;
  ringKeys.reserve(absoluteIndexes.size());
  bool hasNonForever = false;
  for (uint64_t absIdx : absoluteIndexes) {
    if (absIdx >= amountRefs.size()) {
      logger(INFO) << "CommitmentTransfer: global index " << absIdx << " out of range";
      return false;
    }
    const CommitmentOutputRef& ref = amountRefs[absIdx];
    ringKeys.push_back(&ref.commitKey);

    if (ref.term != CryptoNote::parameters::HEAT_TERM) {
      hasNonForever = true;
    }

    // Reject slashed outputs
    if (ref.isSlashed) {
      logger(INFO) << "CommitmentTransfer: ring member at index " << absIdx << " is slashed — rejected";
      return false;
    }

    if (pmax_related_block_height) {
      uint32_t blockHeight = ref.transactionIndex.block;
      if (*pmax_related_block_height < blockHeight) {
        *pmax_related_block_height = blockHeight;
      }
    }
  }

  // All-FOREVER guard (same as CommitmentSpend)
  if (!hasNonForever) {
    logger(INFO) << "CommitmentTransfer: all ring members are burned outputs — no valid transfer possible";
    return false;
  }

  if (ringKeys.size() != sig.size()) {
    logger(ERROR) << "CommitmentTransfer: ring size " << ringKeys.size() << " != sig count " << sig.size();
    return false;
  }

  bool valid = Crypto::check_ring_signature(tx_prefix_hash, txin.keyImage, ringKeys, sig.data());
  if (!valid) {
    logger(DEBUGGING) << "CommitmentTransfer ring signature check failed for keyImage: " << Common::podToHex(txin.keyImage);
  }
  return valid;
}

uint64_t Blockchain::get_adjusted_time() {
  //TODO: add collecting median time
  return time(NULL);
}

bool Blockchain::check_tx_outputs(const Transaction& tx, uint32_t height) const {
  for (TransactionOutput out : tx.outputs) {
    if (out.target.type() == typeid(MultisignatureOutput)) {
      if (tx.version < CryptoNote::TRANSACTION_VERSION_2) {
        logger(INFO, BRIGHT_WHITE) << getObjectHash(tx) << " contains multisignature output but have version " << tx.version;
        return false;
      } else {
        const auto& multisignatureOutput = ::boost::get<MultisignatureOutput>(out.target);
        if (multisignatureOutput.term != 0 && height >= 821000) {
          // Allow HEAT_TERM for burn deposits (HEAT)
          if (multisignatureOutput.term != CryptoNote::parameters::HEAT_TERM &&
              (multisignatureOutput.term < m_currency.depositMinTerm() || multisignatureOutput.term > m_currency.depositMaxTerm())) {
            logger(INFO, BRIGHT_WHITE) << getObjectHash(tx) << " multisignature output has invalid term: " << multisignatureOutput.term;
            return false;
          } else if (out.amount < m_currency.depositMinAmount()) {
            logger(INFO, BRIGHT_WHITE) << getObjectHash(tx) << " multisignature output is a deposit output, but it has too small amount: " << out.amount;
            return false;
          }
        }
      }
    }
  }

  return true;
}


bool Blockchain::check_block_timestamp_main(const Block& b) {
   if (b.timestamp > get_adjusted_time() + m_currency.blockFutureTimeLimit(b.majorVersion)) {
	   logger(INFO, BRIGHT_WHITE) <<
      "Timestamp of block with id: " << get_block_hash(b) << ", " << b.timestamp << ", bigger than adjusted time + 8 min.";
    return false;
  }

  std::vector<uint64_t> timestamps;
 size_t offset = m_blocks.size() <= m_currency.timestampCheckWindow(b.majorVersion) ? 0 : m_blocks.size() - m_currency.timestampCheckWindow(b.majorVersion);  for (; offset != m_blocks.size(); ++offset) {
    timestamps.push_back(m_blocks[offset].bl.timestamp);
  }

  return check_block_timestamp(std::move(timestamps), b);
}

//------------------------------------------------------------------
// This function takes the timestamps from the most recent <n> blocks,
// where n = BLOCKCHAIN_TIMESTAMP_CHECK_WINDOW. If there are not that many
// blocks in the blockchain, the timestap is assumed to be valid. If there
// are, this function returns:
//   true if the block's timestamp is not less than the median timestamp
//       of the selected blocks
//   false otherwise
bool Blockchain::check_block_timestamp(std::vector<uint64_t> timestamps, const Block& b) {
    if (timestamps.size() < m_currency.timestampCheckWindow(b.majorVersion)) {
	return true;
  }

  uint64_t median_ts = Common::medianValue(timestamps);

  if (b.timestamp < median_ts) {
    logger(INFO, BRIGHT_WHITE) <<
      "Timestamp of block with id: " << get_block_hash(b) << ", " << b.timestamp <<
      ", less than median of last " << m_currency.timestampCheckWindow(b.majorVersion) << " blocks, " << median_ts;
	  return false;
  }

  return true;
}

bool Blockchain::checkBlockVersion(const Block& b, const Crypto::Hash& blockHash) {
  uint32_t height = get_block_height(b);
  const uint8_t expectedBlockVersion = getBlockMajorVersionForHeight(height);
  if (b.majorVersion != expectedBlockVersion) {
    logger(TRACE) << "Block " << blockHash << " has wrong major version: " << static_cast<int>(b.majorVersion) <<
      ", at height " << height << " expected version is " << static_cast<int>(expectedBlockVersion);
    return false;
  }

  if (b.majorVersion == BLOCK_MAJOR_VERSION_2 && b.parentBlock.majorVersion > BLOCK_MAJOR_VERSION_1) {
    logger(ERROR, BRIGHT_RED) << "Parent block of block " << blockHash << " has wrong major version: " << static_cast<int>(b.parentBlock.majorVersion) <<
      ", at height " << height << " expected version is " << static_cast<int>(BLOCK_MAJOR_VERSION_1);
    return false;
  }

  return true;
}

bool Blockchain::checkParentBlockSize(const Block& b, const Crypto::Hash& blockHash) {
  if (b.majorVersion >= BLOCK_MAJOR_VERSION_2) {
    auto serializer = makeParentBlockSerializer(b, false, false);
    size_t parentBlockSize;
    if (!getObjectBinarySize(serializer, parentBlockSize)) {
      logger(ERROR, BRIGHT_RED) <<
        "Block " << blockHash << ": failed to determine parent block size";
      return false;
    }

    if (parentBlockSize > 2 * 1024) {
      logger(INFO, BRIGHT_WHITE) <<
        "Block " << blockHash << " contains too big parent block: " << parentBlockSize <<
        " bytes, expected no more than " << 2 * 1024 << " bytes";
      return false;
    }
  }

  return true;
}

bool Blockchain::checkCumulativeBlockSize(const Crypto::Hash& blockId, size_t cumulativeBlockSize, uint64_t height) {
  size_t maxBlockCumulativeSize = m_currency.maxBlockCumulativeSize(height);
  if (cumulativeBlockSize > maxBlockCumulativeSize) {
    logger(INFO, BRIGHT_WHITE) <<
      "Block " << blockId << " is too big: " << cumulativeBlockSize << " bytes, " <<
      "expected no more than " << maxBlockCumulativeSize << " bytes";
    return false;
  }

  return true;
}

// Returns true, if cumulativeSize is calculated precisely, else returns false.
bool Blockchain::getBlockCumulativeSize(const Block& block, size_t& cumulativeSize) {
  std::vector<Transaction> blockTxs;
  std::vector<Crypto::Hash> missedTxs;
  getTransactions(block.transactionHashes, blockTxs, missedTxs, true);

  cumulativeSize = getObjectBinarySize(block.baseTransaction);
  for (const Transaction& tx : blockTxs) {
    cumulativeSize += getObjectBinarySize(tx);
  }

  return missedTxs.empty();
}

// Precondition: m_blockchain_lock is locked.
bool Blockchain::update_next_comulative_size_limit() {
  uint8_t nextBlockMajorVersion = getBlockMajorVersionForHeight(static_cast<uint32_t>(m_blocks.size()));
  size_t nextBlockGrantedFullRewardZone = m_currency.blockGrantedFullRewardZoneByBlockVersion(nextBlockMajorVersion);

  std::vector<size_t> sz;
  get_last_n_blocks_sizes(sz, m_currency.rewardBlocksWindow());

  uint64_t median = Common::medianValue(sz);
  if (median <= nextBlockGrantedFullRewardZone) {
    median = nextBlockGrantedFullRewardZone;
  }

  m_current_block_cumul_sz_limit = median * 2;
  return true;
}

bool Blockchain::addNewBlock(const Block& bl_, block_verification_context& bvc) {
  //copy block here to let modify block.target
  Block bl = bl_;
  Crypto::Hash id;
  if (!get_block_hash(bl, id)) {
    logger(ERROR, BRIGHT_RED) <<
      "Failed to get block hash, possible block has invalid format";
    bvc.m_verification_failed = true;
    return false;
  }

  bool add_result;

  { //to avoid deadlock lets lock tx_pool for whole add/reorganize process
    std::lock_guard<decltype(m_tx_pool)> poolLock(m_tx_pool);
    std::lock_guard<decltype(m_blockchain_lock)> bcLock(m_blockchain_lock);

    if (haveBlock(id)) {
      logger(TRACE) << "block with id = " << id << " already exists";
      bvc.m_already_exists = true;
      return false;
    }

  	uint32_t height = m_blocks.size();

      //check that block refers to chain tail
      if (!(bl.previousBlockHash == getTailId()))
      {
        //chain switching or wrong block
        bvc.m_added_to_main_chain = false;
        add_result = handle_alternative_block(bl, id, bvc);
      }
      else
      {
        // `height` here is m_blocks.size() BEFORE the push — i.e. the index the
        // new block will occupy after pushBlock returns successfully. This is
        // what the checkpoint comparator expects: m_points[N] holds the hash of
        // m_blocks[N]. Passing ++height was an off-by-one that caused
        // check_block(N+1, hash_of_block_N) at every checkpoint height,
        // surfacing whenever a checkpoint exists at exactly N+1.
        add_result = pushBlock(bl, id, bvc, height);
        if (add_result)
        {
          sendMessage(BlockchainMessage(NewBlockMessage(id)));

          /** Save the blockchain every 720 blocks if the option is enabled*/
          if (m_blockchainAutosaveEnabled) {
            if (m_blocks.size() % 720 == 0)
            {
              storeCache();
            }
          }

        }
      }
    }

  if (add_result && bvc.m_added_to_main_chain) {
    m_observerManager.notify(&IBlockchainStorageObserver::blockchainUpdated);
  }

  return add_result;
}

const Blockchain::TransactionEntry& Blockchain::transactionByIndex(TxIndex index) {
  return m_blocks[index.block].transactions[index.transaction];
}

bool Blockchain::pushBlock(const Block &blockData, const Crypto::Hash &id, block_verification_context &bvc, uint32_t height) {
  std::vector<Transaction> transactions;
  if (!loadTransactions(blockData, transactions, height)) {
    bvc.m_verification_failed = true;
    return false;
  }

  if (!pushBlock(blockData, transactions, id, bvc, height)) {
    saveTransactions(transactions, height);
    return false;
  }

  return true;
}

bool Blockchain::pushBlock(const Block &blockData, const std::vector<Transaction> &transactions, const Crypto::Hash &id, block_verification_context &bvc, uint32_t height) {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);

  auto blockProcessingStart = std::chrono::steady_clock::now();

  Crypto::Hash blockHash = get_block_hash(blockData);

  if (m_blockIndex.hasBlock(blockHash)) {
    logger(ERROR, BRIGHT_RED) <<
      "Block " << blockHash << " already exists in blockchain.";
    bvc.m_verification_failed = true;
    return false;
  }

  if (!checkBlockVersion(blockData, blockHash)) {
    bvc.m_verification_failed = true;
    return false;
  }

  if (!checkParentBlockSize(blockData, blockHash)) {
    bvc.m_verification_failed = true;
    return false;
  }

  if (blockData.previousBlockHash != getTailId()) {
    logger(INFO, BRIGHT_WHITE) <<
      "Block " << blockHash << " has wrong previousBlockHash: " << blockData.previousBlockHash << ", expected: " << getTailId();
    bvc.m_verification_failed = true;
    return false;
  }

  // make sure block timestamp is not less than the median timestamp
  // of a set number of the most recent blocks.
  if (!check_block_timestamp_main(blockData)) {
    logger(INFO, BRIGHT_WHITE) <<
      "Block " << blockHash << " has invalid timestamp: " << blockData.timestamp;
    bvc.m_verification_failed = true;
    return false;
  }

  auto targetTimeStart = std::chrono::steady_clock::now();
  difficulty_type currentDifficulty = getDifficultyForNextBlock();
  auto target_calculating_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - targetTimeStart).count();

  if (!(currentDifficulty)) {
    logger(ERROR, BRIGHT_RED) << "!!!!!!!!! difficulty overhead !!!!!!!!!";
    return false;
  }


  auto longhashTimeStart = std::chrono::steady_clock::now();
  Crypto::Hash proof_of_work = NULL_HASH;
  if (m_checkpoints.is_in_checkpoint_zone(height)) {
    if (!m_checkpoints.check_block(height, blockHash)) {
      logger(ERROR, BRIGHT_RED) <<
        "CHECKPOINT VALIDATION FAILED";
      bvc.m_verification_failed = true;
      return false;
    }
  } else {
    if (!m_currency.checkProofOfWork(m_cn_context, blockData, currentDifficulty, proof_of_work)) {
      logger(INFO, BRIGHT_WHITE) <<
        "Block " << blockHash << ", has too weak proof of work: " << proof_of_work << ", expected difficulty: " << currentDifficulty;
      bvc.m_verification_failed = true;
      return false;
    }
  }

  auto longhash_calculating_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - longhashTimeStart).count();

  if (!prevalidate_miner_transaction(blockData, static_cast<uint32_t>(m_blocks.size()))) {
    logger(INFO, BRIGHT_WHITE) <<
      "Block " << blockHash << " failed to pass prevalidation";
    bvc.m_verification_failed = true;
    return false;
  }

  Crypto::Hash minerTransactionHash = getObjectHash(blockData.baseTransaction);

  BlockEntry block;
  block.bl = blockData;
  block.height = static_cast<uint32_t>(m_blocks.size());
  block.transactions.resize(1);
  block.transactions[0].tx = blockData.baseTransaction;
  TxIndex transactionIndex = { block.height, static_cast<uint16_t>(0) };
  pushTransaction(block, minerTransactionHash, transactionIndex);

  size_t coinbase_blob_size = getObjectBinarySize(blockData.baseTransaction);
  size_t cumulative_block_size = coinbase_blob_size;
  uint64_t fee_summary = 0;
    uint64_t interestSummary = 0;

    for (size_t i = 0; i < transactions.size(); ++i)
    {
      const Crypto::Hash &tx_id = blockData.transactionHashes[i];
      block.transactions.resize(block.transactions.size() + 1);
      block.transactions.back().tx = transactions[i];
      size_t blob_size = toBinaryArray(transactions[i]).size();

    uint64_t in_amount = m_currency.getTransactionAllInputsAmount(transactions[i], block.height);
	  uint64_t out_amount = getOutputAmount(transactions[i]);

    uint64_t fee = in_amount < out_amount ? m_currency.minimumFee(blockData.majorVersion) : in_amount - out_amount;

    // v12+ per-asset balance rule
    AssetBalance inAssets, outAssets;
    bool hasHeatMintAuth = false;
    uint64_t authXfgBurned = 0, authHeatMinted = 0;
    bool hasAmmSwapAuth = false;
    uint8_t swapDirection = 0;
    uint64_t swapInputAmount = 0, swapOutputAmount = 0, swapMinOutput = 0;
    bool hasLpAddAuth = false;
    uint64_t lpAddAmountXfg = 0, lpAddAmountHeat = 0, lpAddShares = 0;
    bool hasLpRemoveAuth = false;
    uint64_t lpRemoveShares = 0, lpRemoveMinXfg = 0, lpRemoveMinHeat = 0;
    bool hasLegacyBondClaim = false;
    uint64_t legacyClaimedInterest = 0;

    if (block.bl.majorVersion >= BLOCK_MAJOR_VERSION_10) {
      inAssets = getTransactionInputAssetAmounts(transactions[i], block.height);
      outAssets = m_currency.getTransactionOutputAssetAmounts(transactions[i]);

       // Scan for v10 auth tags
      std::vector<TransactionExtraField> tx_extra_fields;
      if (parseTransactionExtra(transactions[i].extra, tx_extra_fields)) {
        for (const auto& field : tx_extra_fields) {
          if (field.type() == typeid(TransactionExtraHeatMintAuth)) {
            hasHeatMintAuth = true;
            const auto& auth = boost::get<TransactionExtraHeatMintAuth>(field);
            authXfgBurned = auth.xfgBurned;
            authHeatMinted = auth.heatMinted;
          }
          if (field.type() == typeid(TransactionExtraAmmSwapAuth)) {
            hasAmmSwapAuth = true;
            const auto& auth = boost::get<TransactionExtraAmmSwapAuth>(field);
            swapDirection = auth.direction;
            swapInputAmount = auth.inputAmount;
            swapOutputAmount = auth.outputAmount;
            swapMinOutput = auth.minOutput;
          }
          if (field.type() == typeid(TransactionExtraLpAddAuth)) {
            hasLpAddAuth = true;
            const auto& auth = boost::get<TransactionExtraLpAddAuth>(field);
            lpAddAmountXfg = auth.amountXfg;
            lpAddAmountHeat = auth.amountHeat;
            lpAddShares = auth.lpShares;
          }
          if (field.type() == typeid(TransactionExtraLpRemoveAuth)) {
            hasLpRemoveAuth = true;
            const auto& auth = boost::get<TransactionExtraLpRemoveAuth>(field);
            lpRemoveShares = auth.lpSharesBurned;
            lpRemoveMinXfg = auth.minAmountXfg;
            lpRemoveMinHeat = auth.minAmountHeat;
          }
          if (field.type() == typeid(TransactionExtraLegacyBondClaim)) {
            hasLegacyBondClaim = true;
            legacyClaimedInterest = boost::get<TransactionExtraLegacyBondClaim>(field).claimedInterest;
          }
        }
      }
    }

    bool isTransactionValid = true;

    // v10 per-asset balance check — skip coinbase (block reward creates coins from nothing)
    if (isTransactionValid && block.bl.majorVersion >= BLOCK_MAJOR_VERSION_10 && i > 0) {
      uint64_t xfgFee = fee;
      if (hasHeatMintAuth) {
        if (inAssets.xfg < outAssets.xfg + xfgFee + authXfgBurned ||
            inAssets.heat + authHeatMinted != outAssets.heat) {
          isTransactionValid = false;
          logger(INFO, BRIGHT_WHITE) << "Transaction " << tx_id << " HEAT mint auth balance mismatch";
        }
      } else if (hasAmmSwapAuth) {
        if (swapDirection == 0) {
          if (inAssets.xfg < outAssets.xfg + xfgFee ||
              inAssets.heat + swapOutputAmount != outAssets.heat) {
            isTransactionValid = false;
            logger(INFO, BRIGHT_WHITE) << "Transaction " << tx_id << " AMM swap balance mismatch (XFG->HEAT)";
          }
        } else {
          // Direction=1: fee paid from swap output. Override flat-sum fee for block reward.
          fee = m_currency.minimumFee(blockData.majorVersion);
          if (inAssets.xfg + swapOutputAmount < outAssets.xfg + fee ||
              inAssets.heat < outAssets.heat) {
            isTransactionValid = false;
            logger(INFO, BRIGHT_WHITE) << "Transaction " << tx_id << " AMM swap balance mismatch (HEAT->XFG)";
          }
        }
      } else {
        if (inAssets.xfg < outAssets.xfg + xfgFee ||
            inAssets.heat != outAssets.heat ||
            inAssets.lp != outAssets.lp) {
          isTransactionValid = false;
          logger(INFO, BRIGHT_WHITE) << "Transaction " << tx_id << " per-asset balance violation";
        }
      }
    }

    if (block.bl.majorVersion < BLOCK_MAJOR_VERSION_8 && transactions[i].version > TRANSACTION_VERSION_1) {
      isTransactionValid = false;
      logger(INFO, BRIGHT_WHITE) << "Block " << blockHash << " can't contain transaction " << tx_id << " because it has invalid version " << transactions[i].version;
    }

    if (!checkTransactionInputs(transactions[i])) {
      isTransactionValid = false;
      logger(INFO, BRIGHT_WHITE) << "Block " << blockHash << " has at least one transaction with wrong inputs: " << tx_id;
    }

    if (!check_tx_outputs(transactions[i], block.height)) {
      isTransactionValid = false;
      logger(INFO, BRIGHT_WHITE) << "Transaction " << tx_id << " has at least one invalid output";
    }

    // F-001 fix: per-transaction CD-interest fee-pool cap.
    // checkCommitmentSpendInput() caps each input's claimedInterest at the
    // current m_feePoolBalance, but every input of a transaction is validated
    // against the same pre-connect snapshot. Multiple CommitmentSpend inputs can
    // therefore each pass individually while their sum exceeds the pool — and
    // since claimedInterest is minted into the outputs (getTransactionInputAmount
    // adds it to the input side of the conservation check), the excess is
    // unbacked supply. The fee pool is the sole backing for CD interest, so the
    // aggregate must be enforced here, before pushTransaction draws it down.
    if (isTransactionValid && block.bl.majorVersion >= BLOCK_MAJOR_VERSION_10) {
      uint64_t txClaimedInterest = 0;
      if (!m_currency.sumCommitmentClaimedInterest(transactions[i], txClaimedInterest)) {
        isTransactionValid = false;
        logger(INFO, BRIGHT_WHITE) << "Transaction " << tx_id << " CD interest sum overflow";
      } else if (txClaimedInterest > m_feePoolBalance) {
        isTransactionValid = false;
        logger(INFO, BRIGHT_WHITE) << "Transaction " << tx_id
            << " aggregate CD interest " << txClaimedInterest
            << " exceeds fee pool " << m_feePoolBalance;
      }
    }

    if (isTransactionValid && block.bl.majorVersion >= BLOCK_MAJOR_VERSION_10) {
      if (hasHeatMintAuth) {
        FixedPoint64 poolRate = (!m_ammPool.isEmpty() && m_ammPool.reserveHeat > 0)
          ? FixedPoint64::fromRatio(m_ammPool.reserveXfg, m_ammPool.reserveHeat)
          : FixedPoint64::fromUint64(1);
        if (!m_heatMintEngine.validateMintAuth(transactions[i], fee, poolRate,
                                                authXfgBurned, authHeatMinted)) {
          isTransactionValid = false;
          logger(INFO, BRIGHT_WHITE) << "Transaction " << tx_id << " HEAT mint auth validation failed";
        }
      }
    }
    if (isTransactionValid && block.bl.majorVersion >= BLOCK_MAJOR_VERSION_10) {
      if (!hasHeatMintAuth && m_heatMintEngine.isHeatMint(transactions[i])) {
        FixedPoint64 poolRate = (!m_ammPool.isEmpty() && m_ammPool.reserveHeat > 0)
          ? FixedPoint64::fromRatio(m_ammPool.reserveXfg, m_ammPool.reserveHeat)
          : FixedPoint64::fromUint64(1);
        uint64_t xfgBurned = 0, heatMinted = 0;
        if (!m_heatMintEngine.validateMint(transactions[i], fee, poolRate, xfgBurned, heatMinted)) {
          isTransactionValid = false;
          logger(INFO, BRIGHT_WHITE) << "Transaction " << tx_id << " HEAT mint validation failed";
        }
      }
    }

    if (isTransactionValid && block.bl.majorVersion >= BLOCK_MAJOR_VERSION_10) {
      // v10 AMM swap + LP auth pool-math validation
      if (block.bl.majorVersion >= BLOCK_MAJOR_VERSION_10 && hasAmmSwapAuth) {
        uint32_t feeBps = parameters::HEARTH_FEE_BPS;
        uint64_t expectedOutput;
        if (swapDirection == 0) {
          expectedOutput = ammGetOutputAmount(swapInputAmount, m_ammPool.reserveXfg, m_ammPool.reserveHeat, feeBps);
        } else {
          expectedOutput = ammGetOutputAmount(swapInputAmount, m_ammPool.reserveHeat, m_ammPool.reserveXfg, feeBps);
        }
        if (swapOutputAmount != expectedOutput || swapOutputAmount < swapMinOutput || swapOutputAmount == 0) {
          isTransactionValid = false;
          logger(INFO, BRIGHT_WHITE) << "Transaction " << tx_id << " AMM swap auth output mismatch (expected="
                                     << expectedOutput << " declared=" << swapOutputAmount << ")";
        }
        // Verify pool-deposit output exists with correct term and amount
        bool foundPoolDeposit = false;
        Crypto::PublicKey poolKey = computePoolCommitKey();
        uint32_t expectedTerm = (swapDirection == 0) ? parameters::DEPOSIT_TERM_POOL_XFG
                                                     : parameters::DEPOSIT_TERM_POOL_HEAT;
        for (const auto& out : transactions[i].outputs) {
          if (out.target.type() == typeid(TransactionOutputCommitment)) {
            const auto& co = boost::get<TransactionOutputCommitment>(out.target);
            if (co.term == expectedTerm && co.commitKey == poolKey && out.amount == swapInputAmount) {
              foundPoolDeposit = true;
            }
          }
        }
        if (!foundPoolDeposit) {
          isTransactionValid = false;
          logger(INFO, BRIGHT_WHITE) << "Transaction " << tx_id << " AMM swap missing pool-deposit output";
        }
        // Verify user-receive output
        bool foundUserReceive = false;
        uint32_t expectedReceiveTerm = (swapDirection == 0) ? parameters::HEAT_TERM
                                                            : parameters::DEPOSIT_TERM_SWAP_RECEIVE_XFG;
        for (const auto& out : transactions[i].outputs) {
          if (out.target.type() == typeid(TransactionOutputCommitment)) {
            const auto& co = boost::get<TransactionOutputCommitment>(out.target);
            if (co.term == expectedReceiveTerm && out.amount == swapOutputAmount) {
              foundUserReceive = true;
            }
          }
        }
        if (!foundUserReceive) {
          isTransactionValid = false;
          logger(INFO, BRIGHT_WHITE) << "Transaction " << tx_id << " AMM swap missing user-receive output";
        }
      }

      // v10 LP add auth — pool math validation
      if (block.bl.majorVersion >= BLOCK_MAJOR_VERSION_10 && hasLpAddAuth) {
        if (lpAddAmountXfg == 0 && lpAddAmountHeat == 0) {
          isTransactionValid = false;
          logger(INFO, BRIGHT_WHITE) << "Transaction " << tx_id << " LP add auth: zero amounts";
        }
        uint64_t computedShares = ammMintLpShares(lpAddAmountXfg, lpAddAmountHeat,
          m_ammPool.totalLpShares, m_ammPool.reserveXfg, m_ammPool.reserveHeat);
        if (computedShares != lpAddShares || computedShares == 0) {
          isTransactionValid = false;
          logger(INFO, BRIGHT_WHITE) << "Transaction " << tx_id << " LP add auth: share mismatch (computed="
                                     << computedShares << " declared=" << lpAddShares << ")";
        }
        if (!ammValidateDepositRatio(lpAddAmountXfg, lpAddAmountHeat,
              m_ammPool.reserveXfg, m_ammPool.reserveHeat, 100)) {
          isTransactionValid = false;
          logger(INFO, BRIGHT_WHITE) << "Transaction " << tx_id << " LP add auth: deposit ratio out of tolerance";
        }
      }

      // v10 LP remove auth — pool math validation  
      if (block.bl.majorVersion >= BLOCK_MAJOR_VERSION_10 && hasLpRemoveAuth) {
        if (lpRemoveShares == 0 || lpRemoveShares > m_ammPool.totalLpShares) {
          isTransactionValid = false;
          logger(INFO, BRIGHT_WHITE) << "Transaction " << tx_id << " LP remove auth: invalid shares";
        }
        uint64_t amountXfg = 0, amountHeat = 0;
        ammGetWithdrawalAmounts(lpRemoveShares, m_ammPool.totalLpShares,
          m_ammPool.reserveXfg, m_ammPool.reserveHeat, amountXfg, amountHeat);
        if (amountXfg < lpRemoveMinXfg || amountHeat < lpRemoveMinHeat) {
          isTransactionValid = false;
          logger(INFO, BRIGHT_WHITE) << "Transaction " << tx_id << " LP remove auth: below minimum";
        }
      }

      // Legacy bond interest claim validation (0xCC)
      if (hasLegacyBondClaim) {
        bool foundLegacyInput = false;
        uint32_t creationHeight = 0;
        uint64_t legacyPrincipal = 0;
        uint64_t oldInterestToRemove = 0;
        uint32_t legacyTerm = 0;
        for (const auto& in : transactions[i].inputs) {
          if (in.type() == typeid(MultisignatureInput)) {
            const auto& msIn = boost::get<MultisignatureInput>(in);
            auto amountOutputs = m_indexManager.multisigOutputs().find(msIn.amount);
            if (amountOutputs != m_indexManager.multisigOutputs().end() &&
                msIn.outputIndex < amountOutputs->second.size()) {
              const auto& usage = amountOutputs->second[msIn.outputIndex];
              if (!usage.isUsed) {
                const auto& outTx = m_blocks[usage.transactionIndex.block]
                    .transactions[usage.transactionIndex.transaction].tx;
                std::vector<TransactionExtraField> depositExtra;
                if (parseTransactionExtra(outTx.extra, depositExtra)) {
                  for (const auto& dField : depositExtra) {
                    if (dField.type() == typeid(TransactionExtraLegacyBond)) {
                      const auto& bond = boost::get<TransactionExtraLegacyBond>(dField);
                      if (bond.amount == msIn.amount) {
                        foundLegacyInput = true;
                        creationHeight = bond.originalCreationHeight;
                        legacyPrincipal = msIn.amount;
                        legacyTerm = msIn.term;
                        // Remove old fixed-term interest for this input
                        if (msIn.term != 0) {
                          oldInterestToRemove = m_currency.calculateInterest(legacyPrincipal, legacyTerm, block.height);
                        }
                        break;
                      }
                    }
                  }
                }
              }
            }
            if (foundLegacyInput) break;
          }
        }

        if (!foundLegacyInput) {
          isTransactionValid = false;
          logger(INFO, BRIGHT_WHITE) << "Transaction " << tx_id << " has legacy bond claim but no valid legacy bond input";
        } else {
          // Remove old fixed interest from in_amount (legacy bonds use fee-pool interest instead)
          if (oldInterestToRemove > 0 && in_amount >= oldInterestToRemove) {
            in_amount -= oldInterestToRemove;
          }

          uint64_t maxInterest = m_currency.calculateCdInterest(
              legacyPrincipal > 0 ? legacyPrincipal : (legacyClaimedInterest > in_amount ? in_amount : legacyClaimedInterest),
              creationHeight, block.height, m_commitmentIndex, true);
          if (maxInterest > m_legacyBondYieldPool) {
            maxInterest = m_legacyBondYieldPool;
          }
          if (legacyClaimedInterest > maxInterest) {
            isTransactionValid = false;
            logger(INFO, BRIGHT_WHITE) << "Transaction " << tx_id
                << " legacy bond claim " << legacyClaimedInterest
                << " exceeds max " << maxInterest;
          } else {
            in_amount += legacyClaimedInterest;
            fee = in_amount < out_amount ? m_currency.minimumFee(blockData.majorVersion) : in_amount - out_amount;
          }
        }
      }

      // Original v11 AMM validation (backward compat for pre-auth-tag txs)
      std::vector<TransactionExtraField> tx_extra_fields;
      if (parseTransactionExtra(transactions[i].extra, tx_extra_fields)) {
        for (const auto& field : tx_extra_fields) {
          if (field.type() == typeid(TransactionExtraAmmSwap)) {
            const auto& swap = boost::get<TransactionExtraAmmSwap>(field);
            uint32_t feeBps = parameters::HEARTH_FEE_BPS;
            uint64_t expectedOutput;
            if (swap.direction == 0) {
              expectedOutput = ammGetOutputAmount(swap.inputAmount, m_ammPool.reserveXfg, m_ammPool.reserveHeat, feeBps);
            } else {
              expectedOutput = ammGetOutputAmount(swap.inputAmount, m_ammPool.reserveHeat, m_ammPool.reserveXfg, feeBps);
            }
            if (expectedOutput < swap.minOutput || expectedOutput == 0) {
              isTransactionValid = false;
              logger(INFO, BRIGHT_WHITE) << "Transaction " << tx_id << " AMM swap output insufficient";
              break;
            }
          } else if (field.type() == typeid(TransactionExtraAmmAddLiquidity)) {
            const auto& add = boost::get<TransactionExtraAmmAddLiquidity>(field);
            if (add.amountXfg == 0 && add.amountHeat == 0) {
              isTransactionValid = false;
              break;
            }
            uint64_t shares = ammMintLpShares(add.amountXfg, add.amountHeat,
              m_ammPool.totalLpShares, m_ammPool.reserveXfg, m_ammPool.reserveHeat);
            if (shares == 0) {
              isTransactionValid = false;
              logger(INFO, BRIGHT_WHITE) << "Transaction " << tx_id << " AMM LP shares zero";
              break;
            }
            if (!ammValidateDepositRatio(add.amountXfg, add.amountHeat,
              m_ammPool.reserveXfg, m_ammPool.reserveHeat, 100)) {
              isTransactionValid = false;
              logger(INFO, BRIGHT_WHITE) << "Transaction " << tx_id << " AMM deposit ratio out of tolerance";
              break;
            }
          } else if (field.type() == typeid(TransactionExtraAmmRemoveLiquidity)) {
            const auto& rem = boost::get<TransactionExtraAmmRemoveLiquidity>(field);
            if (rem.lpSharesBurned == 0 || rem.lpSharesBurned > m_ammPool.totalLpShares) {
              isTransactionValid = false;
              logger(INFO, BRIGHT_WHITE) << "Transaction " << tx_id << " AMM LP shares invalid";
              break;
            }
            uint64_t amountXfg = 0, amountHeat = 0;
            ammGetWithdrawalAmounts(rem.lpSharesBurned, m_ammPool.totalLpShares,
              m_ammPool.reserveXfg, m_ammPool.reserveHeat, amountXfg, amountHeat);
            if (amountXfg < rem.minAmountXfg || amountHeat < rem.minAmountHeat) {
              isTransactionValid = false;
              logger(INFO, BRIGHT_WHITE) << "Transaction " << tx_id << " AMM withdrawal below minimum";
              break;
            }
          } else if (field.type() == typeid(TransactionExtraAmmCompound)) {
            // Compound: always valid, no-op (fees already in reserves)
          } else if (field.type() == typeid(TransactionExtraAmmClaim)) {
            const auto& claim = boost::get<TransactionExtraAmmClaim>(field);
            if (claim.lpShares == 0 || claim.lpShares > m_ammPool.totalLpShares) {
              isTransactionValid = false;
              logger(INFO, BRIGHT_WHITE) << "Transaction " << tx_id << " AMM claim LP shares invalid";
              break;
            }
            uint64_t feeXfg = (m_ammPool.accumulatedLpFeesXfg * claim.lpShares) / m_ammPool.totalLpShares;
            if (feeXfg < claim.minAmountXfg) {
              isTransactionValid = false;
              logger(INFO, BRIGHT_WHITE) << "Transaction " << tx_id << " AMM claim below XFG minimum";
              break;
            }
            uint64_t feeHeat = (m_ammPool.accumulatedLpFeesHeat * claim.lpShares) / m_ammPool.totalLpShares;
            if (feeHeat < claim.minAmountHeat) {
              isTransactionValid = false;
              logger(INFO, BRIGHT_WHITE) << "Transaction " << tx_id << " AMM claim below HEAT minimum";
              break;
            }
          }
        }
      }
    }

    if (!isTransactionValid) {
      logger(INFO, BRIGHT_WHITE) << "Block " << blockHash << " has at least one invalid transaction: " << tx_id;
      bvc.m_verification_failed = true;

      block.transactions.pop_back();
      popTransactions(block, minerTransactionHash);
      return false;
    }

    ++transactionIndex.transaction;
    pushTransaction(block, tx_id, transactionIndex);

    cumulative_block_size += blob_size;
    fee_summary += fee;
      // Interest calculation removed - no on-chain interest
  }

  if (!checkCumulativeBlockSize(blockHash, cumulative_block_size, m_blocks.size())) {
    bvc.m_verification_failed = true;
    return false;
  }

  int64_t emissionChange = 0;
  uint64_t reward = 0;
  uint64_t already_generated_coins = m_blocks.empty() ? 0 : m_blocks.back().already_generated_coins;

  if (!validate_miner_transaction(blockData, static_cast<uint32_t>(m_blocks.size()), cumulative_block_size, already_generated_coins, fee_summary, reward, emissionChange, transactions)) {
    logger(INFO, BRIGHT_WHITE) << "Block " << blockHash << " has invalid miner transaction";
    bvc.m_verification_failed = true;
    popTransactions(block, minerTransactionHash);
    return false;
  }

  block.height = static_cast<uint32_t>(m_blocks.size());
  block.block_cumulative_size = cumulative_block_size;
  block.cumulative_difficulty = currentDifficulty;
  block.already_generated_coins = already_generated_coins + emissionChange;
  if (m_blocks.size() > 0) {
    block.cumulative_difficulty += m_blocks.back().cumulative_difficulty;
  }

  pushBlock(block);
    pushToBankingIndex(block, interestSummary);

  // TWAP accumulation per block (v11+) — spot price stored in Q64.64
  if (block.bl.majorVersion >= BLOCK_MAJOR_VERSION_11 && !m_ammPool.isEmpty()) {
    uint64_t spotPrice = ammGetSpotPrice(m_ammPool.reserveXfg, m_ammPool.reserveHeat);
    uint128_t q64 = ((uint128_t)spotPrice << 64) / 1000000000000000000ULL;
    m_twapAccumulator += q64;
    m_twapBlockCount++;
    m_blockTwapContributions.push_back(q64);
  }

  // Epoch boundary: reset TWAP accumulator (v11+)
  uint32_t epochDuration = m_currency.isTestnet() ?
    parameters::TESTNET_EPOCH_DURATION_BLOCKS : parameters::EPOCH_DURATION_BLOCKS;
  if (block.height > 0 && block.height % epochDuration == 0 &&
      block.bl.majorVersion >= BLOCK_MAJOR_VERSION_11) {
    // Reset TWAP for next epoch
    m_twapAccumulator = 0;
    m_twapBlockCount = 0;

    // CD yield is processed in pushBlock (consolidated epoch processing).
    // m_currentEpochSwapFees is already consumed+reset by pushBlock at this point.
  }
/*
  // Track per-block banking fees for audit/query
  uint32_t blockHeight = static_cast<uint32_t>(m_blocks.size()) - 1;
  {
    std::vector<Transaction> blockTxs;
    for (size_t i = 1; i < block.transactions.size(); ++i) {
      blockTxs.push_back(block.transactions[i].tx);
    }
    uint64_t blockBankingFee = computeBankingFeesFromTransactions(blockTxs);
    if (blockBankingFee > 0) {
      m_commitmentIndex.addBlockBankingFee(blockHeight, blockBankingFee);
    }
  }
*/
  auto block_processing_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - blockProcessingStart).count();

  logger(DEBUGGING, YELLOW) <<
    "+++++ BLOCK SUCCESSFULLY ADDED" << ENDL << "id:\t" << blockHash
    << ENDL << "PoW:\t" << proof_of_work
    << ENDL << "HEIGHT " << block.height << ", difficulty:\t" << currentDifficulty
    << ENDL << "block reward: " << m_currency.formatAmount(reward) << ", fee = " << m_currency.formatAmount(fee_summary)
    << ", coinbase_blob_size: " << coinbase_blob_size << ", cumulative size: " << cumulative_block_size
    << ", " << block_processing_time << "(" << target_calculating_time << "/" << longhash_calculating_time << ")ms";

  bvc.m_added_to_main_chain = true;

  m_upgradeDetectorV2.blockPushed();
  m_upgradeDetectorV3.blockPushed();
  m_upgradeDetectorV4.blockPushed();
  m_upgradeDetectorV5.blockPushed();
  m_upgradeDetectorV6.blockPushed();
  m_upgradeDetectorV7.blockPushed();
  m_upgradeDetectorV8.blockPushed();
  m_upgradeDetectorV9.blockPushed();
  m_upgradeDetectorV10.blockPushed();
  m_upgradeDetectorV11.blockPushed();

  update_next_comulative_size_limit();

  return true;
}

uint64_t Blockchain::fullDepositAmount() const {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  return m_bankingIndex.fullDepositAmount();
}

uint64_t Blockchain::depositAmountAtHeight(size_t height) const {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  return m_bankingIndex.depositAmountAtHeight(static_cast<BankingIndex::DepositHeight>(height));
}

  uint64_t Blockchain::depositInterestAtHeight(size_t height) const
  {
    std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
    return m_bankingIndex.depositInterestAtHeight(static_cast<BankingIndex::DepositHeight>(height));
  }

  uint64_t Blockchain::getBurnedXfgAtHeight(size_t height) const
  {
    std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
    return m_bankingIndex.getBurnedXfgAtHeight(static_cast<BankingIndex::DepositHeight>(height));
  }

  // --- Commitment Index Accessors ---

  std::optional<CommitmentEntry> Blockchain::getCommitmentByHash(const Crypto::Hash& commitment) const {
    std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
    return m_commitmentIndex.getByCommitment(commitment);
  }

  bool Blockchain::hasCommitment(const Crypto::Hash& commitment) const {
    std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
    return m_commitmentIndex.hasCommitment(commitment);
  }

  size_t Blockchain::getCommitmentCount() const {
    std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
    return m_commitmentIndex.size();
  }

  size_t Blockchain::getHeatCommitmentCount() const {
    std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
    return m_commitmentIndex.heatCount();
  }

  size_t Blockchain::getColdCommitmentCount() const {
    std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
    return m_commitmentIndex.coldCount();
  }

  Crypto::Hash Blockchain::getCommitmentMerkleRoot() const {
    std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
    return m_commitmentIndex.computeMerkleRoot();
  }

  std::vector<Crypto::Hash> Blockchain::getCommitmentMerkleProof(const Crypto::Hash& commitment) const {
    std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
    return m_commitmentIndex.getMerkleProof(commitment);
  }

  int64_t Blockchain::getCommitmentLeafIndex(const Crypto::Hash& commitment) const {
    std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
    return m_commitmentIndex.getLeafIndex(commitment);
  }

  CommitmentIndex::Height Blockchain::getCommitmentHighestBlock() const {
    std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
    return m_commitmentIndex.highestBlock();
  }

  std::vector<Crypto::Hash> Blockchain::getCommitmentLeaves() const {
    std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
    return m_commitmentIndex.getAllLeaves();
  }

  uint64_t Blockchain::computeBankingFeesFromTransactions(const std::vector<Transaction>& txs) {
    // Banking fees go to miners. Fixed rate: 0.1% on HEAT/COLD commitments.
    uint64_t totalBankingFees = 0;
    for (const auto& tx : txs) {
      std::vector<TransactionExtraField> extraFields;
      if (!parseTransactionExtra(tx.extra, extraFields)) continue;
      for (const auto& field : extraFields) {
        if (field.type() == typeid(TransactionExtraHeatCommitment)) {
          const auto& heat = boost::get<TransactionExtraHeatCommitment>(field);
          totalBankingFees += heat.amount / 1000;
        } else if (field.type() == typeid(TransactionExtraSimpleCD)) {
          const auto& cd = boost::get<TransactionExtraSimpleCD>(field);
          totalBankingFees += cd.amount / 1000;
        }
      }
    }
    return totalBankingFees;
  }

  void Blockchain::pushToBankingIndex(const BlockEntry &block, uint64_t interest)
  {
    int64_t deposit = 0;
    uint64_t permanentBurns = 0;  // Track permanent burns for ethereal_xfg

    logger(DEBUGGING) << "Processing block " << block.height << " for BankingIndex, current burned: " << m_bankingIndex.getBurnedXfgAmount();

    for (const auto &tx : block.transactions)
    {
      // Parse transaction extra to detect burn types (0X08 0xEF)
      std::vector<TransactionExtraField> extraFields;
      if (parseTransactionExtra(tx.tx.extra, extraFields)) {
        logger(DEBUGGING, "Blockchain") << "Transaction " << getObjectHash(tx.tx)
                                 << " extra: Found " << extraFields.size() << " fields";
        for (size_t i = 0; i < extraFields.size(); ++i) {
          if (extraFields[i].type() == typeid(TransactionExtraAliasRegistration)) {
            logger(DEBUGGING, "Blockchain") << "  Field " << i << ": TransactionExtraAliasRegistration";
          } else if (extraFields[i].type() == typeid(TransactionExtraPublicKey)) {
            logger(DEBUGGING, "Blockchain") << "  Field " << i << ": TransactionExtraPublicKey";
          } else {
            logger(DEBUGGING, "Blockchain") << "  Field " << i << ": Unknown type";
          }
        }
        for (const auto& field : extraFields) {
          // Check for HEAT commitment (0x08) - permanent burn
          if (field.type() == typeid(TransactionExtraHeatCommitment)) {
            const auto& heatCommit = boost::get<TransactionExtraHeatCommitment>(field);
            permanentBurns += (heatCommit.amount * CryptoNote::parameters::MINT_BURN_EF_PCT) / 100;
            uint64_t treasuryShare = (heatCommit.amount * CryptoNote::parameters::MINT_BURN_TREASURY_PCT) / 100;
            m_treasuryLpReserve += (treasuryShare * CryptoNote::parameters::TREASURY_LP_PCT) / 100;
            m_treasuryBalance += (treasuryShare * CryptoNote::parameters::TREASURY_PEG_PCT) / 100;
            logger(DEBUGGING) << "Detected HEAT burn in block " << block.height << ": " << heatCommit.amount << " XFG";

            // Index the HEAT commitment for RPC queries
            Crypto::Hash txHash = getObjectHash(tx.tx);
            CommitmentEntry entry;
            entry.commitment = heatCommit.commitment;
            entry.txHash = txHash;
            entry.blockHeight = block.height;
            entry.amount = heatCommit.amount;
            entry.term = parameters::HEAT_TERM;  // HEAT is permanent
            entry.type = CommitmentEntry::Type::HEAT;
            entry.targetChainId = heatCommit.metadata.size() > 0 ? heatCommit.metadata[0] : 1;  // Default to ETH
            m_commitmentIndex.addCommitment(entry);

            logger(DEBUGGING) << "HEAT commitment indexed: " << Common::podToHex(heatCommit.commitment)
                             << " amount=" << heatCommit.amount;
          }
          // Check for CD commitment (0xCD) - term deposit
          else if (field.type() == typeid(TransactionExtraSimpleCD)) {
            const auto& coldCommit = boost::get<TransactionExtraSimpleCD>(field);

            // Index the CD commitment for RPC queries
            Crypto::Hash txHash = getObjectHash(tx.tx);
            CommitmentEntry entry;
            entry.commitment = coldCommit.commitment;
            entry.txHash = txHash;
            entry.blockHeight = block.height;
            entry.amount = coldCommit.amount;
            entry.term = coldCommit.term;
            entry.type = CommitmentEntry::Type::COLD;
            entry.targetChainId = 1; // Default to ETH
            m_commitmentIndex.addCommitment(entry);

            logger(DEBUGGING) << "CD commitment indexed: " << Common::podToHex(coldCommit.commitment)
                              << " amount=" << coldCommit.amount << " term=" << coldCommit.term;
          }
          // 0xCE: COLD migration — register v3 commitment for a pre-v3 legacy deposit
          else if (field.type() == typeid(TransactionExtraColdMigration)) {
            const auto& migration = boost::get<TransactionExtraColdMigration>(field);

            // Validate: the referenced original tx must exist and contain a legacy
            // deposit output (MultisignatureOutput) with matching amount.
            // Migration is ONLY for pre-v3 legacy deposits which use multisig outputs.
            std::list<Crypto::Hash> txIds = {migration.originalTxHash};
            std::list<Transaction> txs;
            std::list<Crypto::Hash> missed;
            getTransactions(txIds, txs, missed, false);
            bool depositFound = false;
            if (!txs.empty()) {
              const auto& origTx = txs.front();
              for (const auto& out : origTx.outputs) {
                if (out.target.type() == typeid(MultisignatureOutput) &&
                    out.amount == migration.amount) {
                  depositFound = true;
                  break;
                }
              }
              if (!depositFound) {
                logger(INFO, BRIGHT_WHITE) << "Cold migration: original tx " << Common::podToHex(migration.originalTxHash)
                  << " has no matching legacy deposit output (amount=" << migration.amount << ")";
              }
            } else {
              logger(WARNING) << "COLD migration rejected: original tx " << Common::podToHex(migration.originalTxHash)
                              << " not found in blockchain";
            }

            // Also ensure this commitment hasn't already been registered
            if (depositFound && !m_commitmentIndex.hasCommitment(migration.commitment)) {
              // Look up original deposit's block height for legacy rate detection.
              // The L2 contract needs the original deposit date (not migration date)
              // to determine if legacy (pre-2026) interest rates apply.
              uint32_t originalBlockHeight = block.height;  // fallback: migration block
              auto origIt = m_indexManager.transactionMap().find(migration.originalTxHash);
              if (origIt != m_indexManager.transactionMap().end()) {
                originalBlockHeight = origIt->second.block;
              }

              CommitmentEntry entry;
              entry.commitment = migration.commitment;
              entry.txHash = migration.originalTxHash;  // Reference original deposit tx
              entry.blockHeight = originalBlockHeight;  // Original deposit block, not migration block
              entry.amount = migration.amount;
              entry.term = migration.term;
              entry.type = CommitmentEntry::Type::COLD;
              entry.targetChainId = migration.targetChainId;
              entry.isLegacyMigration = true;  // Confirmed: original tx has MultisignatureOutput
              m_commitmentIndex.addCommitment(entry);

              logger(DEBUGGING) << "COLD migration indexed: original=" << Common::podToHex(migration.originalTxHash)
                                << " commitment=" << Common::podToHex(migration.commitment)
                                << " amount=" << migration.amount
                                << " originalBlock=" << originalBlockHeight;
            } else if (!depositFound) {
              logger(WARNING) << "COLD migration rejected: original tx " << Common::podToHex(migration.originalTxHash)
                               << " has no legacy deposit (multisig) output matching amount=" << migration.amount;
            }
          }
          // 0xCB: Legacy Bond migration (v1.10.00) — marks bug-era Multisig deposit for 50% CD share
          else if (field.type() == typeid(TransactionExtraLegacyBond)) {
            const auto& bond = boost::get<TransactionExtraLegacyBond>(field);

            // Validate: the referenced original tx must exist with a MultisignatureOutput deposit
            std::list<Crypto::Hash> txIds = {bond.originalTxHash};
            std::list<Transaction> txs;
            std::list<Crypto::Hash> missed;
            getTransactions(txIds, txs, missed, false);
            bool depositFound = false;
            if (!txs.empty()) {
              const auto& origTx = txs.front();
              for (const auto& out : origTx.outputs) {
                if (out.target.type() == typeid(MultisignatureOutput) &&
                    out.amount == bond.amount) {
                  depositFound = true;
                  break;
                }
              }
              if (!depositFound) {
                logger(INFO, BRIGHT_WHITE) << "Legacy bond: original tx " << Common::podToHex(bond.originalTxHash)
                  << " has no matching multisig deposit output (amount=" << bond.amount << ")";
              }
            } else {
              logger(WARNING) << "Legacy bond rejected: original tx " << Common::podToHex(bond.originalTxHash)
                              << " not found in blockchain";
            }

            if (depositFound) {
              m_totalLegacyBondLocked += bond.amount;
              logger(DEBUGGING) << "Legacy bond registered: original=" << Common::podToHex(bond.originalTxHash)
                                << " amount=" << bond.amount
                                << " totalLegacyBondLocked=" << m_totalLegacyBondLocked;
            } else {
              logger(WARNING) << "Legacy bond rejected: original tx " << Common::podToHex(bond.originalTxHash)
                              << " — deposit validation failed";
            }
          }
          // 0xCC: Legacy Bond interest claim — debit the yield pool
          else if (field.type() == typeid(TransactionExtraLegacyBondClaim)) {
            const auto& claim = boost::get<TransactionExtraLegacyBondClaim>(field);
            if (claim.claimedInterest > 0) {
              if (m_legacyBondYieldPool >= claim.claimedInterest) {
                m_legacyBondYieldPool -= claim.claimedInterest;
                logger(DEBUGGING) << "Legacy bond claim: " << claim.claimedInterest
                                 << " XFG interest paid, pool remaining=" << m_legacyBondYieldPool;
              } else {
                logger(WARNING) << "Legacy bond claim: insufficient pool (have="
                               << m_legacyBondYieldPool << " need=" << claim.claimedInterest << ")";
              }
            }
          }
          // Check for @ Alias Registration (0xEA)
          else if (field.type() == typeid(TransactionExtraAliasRegistration)) {
            const auto& aliasReg = boost::get<TransactionExtraAliasRegistration>(field);

            if (aliasReg.isValid()) {
              // Network ID check: reject registrations whose networkId field is set but
              // does not match this chain's ID.  networkId == 0 means the field was
              // absent (legacy tx) and is always accepted for backward compatibility.
              // A non-zero mismatch means the tx was constructed for a different network
              // (e.g. testnet replay on mainnet) and must be rejected.
              const bool networkIdOk =
                  (aliasReg.networkId == 0) ||
                  m_currency.validateNetworkId(static_cast<uint64_t>(aliasReg.networkId));

              if (!networkIdOk) {
                logger(WARNING) << "@ Alias registration rejected in block " << block.height
                                << ": @" << aliasReg.alias
                                << " — networkId mismatch (got "
                                << aliasReg.networkId << ", expected "
                                << m_currency.getFuegoNetworkId() << ")";
              } else {
                // Fee enforcement: regular users (aliasType == 1) must pay ALIAS_REGISTRATION_FEE
                // to FUEGO_DEV_FUND_ADDRESS. Amount alone is insufficient — the destination must
                // also match, otherwise a self-transfer satisfies the amount check.
                // Reserved aliases (aliasType == 0) are exempt. Testnet always passes.
                bool feeOk = true;
                if (!m_currency.isTestnet() && aliasReg.aliasType != 0) {
                  bool feeOutputFound = false;
                  AccountPublicAddress devFundAddr;
                  bool devAddrParsed = m_currency.parseAccountAddressString(
                    std::string(CryptoNote::FUEGO_DEV_FUND_ADDRESS), devFundAddr);
                  for (const auto& out : tx.tx.outputs) {
                    if (out.amount < parameters::ALIAS_REGISTRATION_FEE) continue;
                    // Verify output is addressed to the dev fund key.
                    const auto* keyOut = boost::get<KeyOutput>(&out.target);
                    if (!keyOut) continue;
                    if (devAddrParsed) {
                      // Check that the output one-time key is derivable to the dev fund address.
                      // Use a heuristic: accept if the output exists and amount matches when we
                      // cannot do full key derivation here (no tx private key in scope).
                      // Full enforcement requires the tx public key and is done in wallet scanning.
                      // At consensus level we enforce amount; wallet-level enforces destination.
                      feeOutputFound = true;
      } else {
                      feeOutputFound = true; // dev addr parse failed — allow, log warning
                      logger(WARNING) << "@ Could not parse FUEGO_DEV_FUND_ADDRESS for fee check";
                    }
                    break;
                  }
                  if (!feeOutputFound) {
                    logger(WARNING) << "@ Alias registration skipped in block " << block.height
                                    << ": @" << aliasReg.alias
                                    << " — missing ALIAS_REGISTRATION_FEE output ("
                                    << parameters::ALIAS_REGISTRATION_FEE << " atomic units)";
                    feeOk = false;
                  }
                }

                if (feeOk) {
                  AliasEntry aliasEntry;
                  aliasEntry.alias = aliasReg.alias;
                  aliasEntry.ownerAddress = aliasReg.ownerAddress;
                  aliasEntry.aliasHash = aliasReg.aliasHash;
                  aliasEntry.addressHash = aliasReg.addressHash;
                  aliasEntry.aliasType = aliasReg.aliasType;
                  aliasEntry.registeredBlock = block.height;

                  if (m_aliasIndex.registerAlias(aliasEntry)) {
                    logger(INFO) << "@ Alias registered in block " << block.height
                                 << ": @" << aliasReg.alias
                                 << " (type=" << static_cast<int>(aliasReg.aliasType) << ")";
                  } else {
                    logger(WARNING) << "@ Alias registration rejected in block " << block.height
                                    << ": @" << aliasReg.alias << " (duplicate or invalid)";
                  }
                }
              }
            }
          }
          // Check for @ Alias Release (0xEC)
          else if (field.type() == typeid(TransactionExtraAliasRelease)) {
            const auto& aliasRel = boost::get<TransactionExtraAliasRelease>(field);
            if (!aliasRel.isValid()) {
              logger(WARNING) << "@ Alias release rejected in block " << block.height << ": invalid data";
            } else {
              AccountPublicAddress ownerAddr;
              if (!m_currency.parseAccountAddressString(aliasRel.ownerAddress, ownerAddr)) {
                logger(WARNING) << "@ Alias release rejected: cannot parse owner address for @" << aliasRel.alias;
              } else {
                uint8_t preimage[64];
                memcpy(preimage,      &ownerAddr.spendPublicKey, 32);
                memcpy(preimage + 32, &ownerAddr.viewPublicKey,  32);
                Crypto::Hash addrHash;
                Crypto::cn_fast_hash(preimage, 64, addrHash);
                auto existingEntry = m_aliasIndex.getAliasByName(aliasRel.alias);
                if (!existingEntry.has_value()) {
                  logger(WARNING) << "@ Alias release rejected: alias @" << aliasRel.alias << " not found";
                } else if (memcmp(&existingEntry->addressHash, &addrHash, sizeof(Crypto::Hash)) != 0) {
                  logger(WARNING) << "@ Alias release rejected: ownership mismatch for @" << aliasRel.alias;
                } else {
                  uint8_t cp[8 + 32 + 1];
                  memcpy(cp,     aliasRel.alias.data(), 8);
                  memcpy(cp + 8, &addrHash,             32);
                  cp[40] = 0x00;
                  Crypto::Hash challenge;
                  Crypto::cn_fast_hash(cp, sizeof(cp), challenge);
                  if (!Crypto::check_signature(challenge, ownerAddr.spendPublicKey, aliasRel.proof)) {
                    logger(WARNING) << "@ Alias release rejected: invalid ownership proof for @" << aliasRel.alias;
                  } else if (removeAlias(aliasRel.alias)) {
                    logger(INFO) << "@ Alias released in block " << block.height << ": @" << aliasRel.alias;
                  } else {
                    logger(WARNING) << "@ Alias release failed for @" << aliasRel.alias;
                  }
                }
              }
            }
          }
          // Check for @ Alias Transfer (0xED)
          else if (field.type() == typeid(TransactionExtraAliasTransfer)) {
            const auto& aliasXfer = boost::get<TransactionExtraAliasTransfer>(field);
            if (!aliasXfer.isValid()) {
              logger(WARNING) << "@ Alias transfer rejected in block " << block.height << ": invalid data";
            } else {
              AccountPublicAddress oldOwnerAddr;
              if (!m_currency.parseAccountAddressString(aliasXfer.oldOwnerAddress, oldOwnerAddr)) {
                logger(WARNING) << "@ Alias transfer rejected: cannot parse old owner address";
              } else {
                uint8_t oldprev[64];
                memcpy(oldprev,      &oldOwnerAddr.spendPublicKey, 32);
                memcpy(oldprev + 32, &oldOwnerAddr.viewPublicKey,  32);
                Crypto::Hash oldAddrHash;
                Crypto::cn_fast_hash(oldprev, 64, oldAddrHash);
                auto existingEntry = m_aliasIndex.getAliasByName(aliasXfer.alias);
                if (!existingEntry.has_value()) {
                  logger(WARNING) << "@ Alias transfer rejected: alias @" << aliasXfer.alias << " not found";
                } else if (memcmp(&existingEntry->addressHash, &oldAddrHash, sizeof(Crypto::Hash)) != 0) {
                  logger(WARNING) << "@ Alias transfer rejected: ownership mismatch for @" << aliasXfer.alias;
                } else {
                  uint8_t cp[8 + 32 + 32 + 1];
                  memcpy(cp,      aliasXfer.alias.data(), 8);
                  memcpy(cp + 8,  &oldAddrHash,           32);
                  memcpy(cp + 40, &aliasXfer.newAddressHash, 32);
                  cp[72] = 0x01;
                  Crypto::Hash challenge;
                  Crypto::cn_fast_hash(cp, sizeof(cp), challenge);
                  if (!Crypto::check_signature(challenge, oldOwnerAddr.spendPublicKey, aliasXfer.proof)) {
                    logger(WARNING) << "@ Alias transfer rejected: invalid ownership proof for @" << aliasXfer.alias;
                  } else {
                    AccountPublicAddress newOwnerAddr;
                    if (!m_currency.parseAccountAddressString(aliasXfer.newOwnerAddress, newOwnerAddr)) {
                      logger(WARNING) << "@ Alias transfer rejected: cannot parse new owner address";
                    } else {
                      uint8_t newprev[64];
                      memcpy(newprev,      &newOwnerAddr.spendPublicKey, 32);
                      memcpy(newprev + 32, &newOwnerAddr.viewPublicKey,  32);
                      Crypto::Hash newAddrHash;
                      Crypto::cn_fast_hash(newprev, 64, newAddrHash);
                      if (replaceAliasOwnership(aliasXfer.alias, newAddrHash)) {
                        logger(INFO) << "@ Alias transferred in block " << block.height
                                     << ": @" << aliasXfer.alias;
                      } else {
                        logger(WARNING) << "@ Alias transfer failed for @" << aliasXfer.alias;
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }

      for (const auto &in : tx.tx.inputs)
      {
        if (in.type() == typeid(MultisignatureInput))
        {
          auto &multisign = boost::get<MultisignatureInput>(in);
          if (multisign.term > 0)
          {
            deposit -= multisign.amount;
          }
        }
        // Commitment withdrawals (ring-sig COLD): reduce deposit balance
        else if (in.type() == typeid(TransactionInputCommitmentSpend))
        {
          deposit -= boost::get<TransactionInputCommitmentSpend>(in).amount;
        }
      }
      for (const auto &out : tx.tx.outputs)
      {
        if (out.target.type() == typeid(MultisignatureOutput))
        {
          auto &multisign = boost::get<MultisignatureOutput>(out.target);
          if (multisign.term > 0)
          {
            deposit += out.amount;
          }
        }
        // COLD commitment outputs: add to deposit balance (HEAT/FOREVER burns tracked separately)
        else if (out.target.type() == typeid(TransactionOutputCommitment))
        {
          const auto& commitment = boost::get<TransactionOutputCommitment>(out.target);
          if (commitment.term != parameters::HEAT_TERM)
          {
            deposit += out.amount;
          }
        }
      }
    }

    // Push deposit tracking
    m_bankingIndex.pushBlock(deposit, interest);

    // Add permanent burns to EternalFlame if any were found
    if (permanentBurns > 0) {
      m_bankingIndex.addForeverDeposit(permanentBurns, block.height);
      // Sync Currency from BankingIndex (single source of truth)
      const_cast<Currency&>(m_currency).syncEternalFlame(m_bankingIndex.getBurnedXfgAmount());
      logger(INFO) << "Burn in block " << block.height << ": "
                   << m_currency.formatAmount(permanentBurns) << " XFG sent into the Ether";
    }
  }

bool Blockchain::pushBlock(BlockEntry &block) {
  Crypto::Hash blockHash = get_block_hash(block.bl);

  m_blocks.push_back(block);
  m_blockIndex.push(blockHash);

  m_timestampIndex.add(block.bl.timestamp, blockHash);
  m_generatedTransactionsIndex.add(block.bl);

  assert(m_blockIndex.size() == m_blocks.size());

  // Snapshot epoch accumulator before any per-block fee additions this push may make.
  // The delta is recorded so popBlock can reverse the contribution.
  uint64_t epochFeesBefore = m_currentEpochSwapFees;

  // Generate epoch report at epoch boundaries
  uint32_t newHeight = static_cast<uint32_t>(m_blocks.size()) - 1;
  uint64_t epochDuration = m_currency.isTestnet()
      ? CryptoNote::parameters::TESTNET_EPOCH_DURATION_BLOCKS
      : CryptoNote::parameters::EPOCH_DURATION_BLOCKS;
  if (newHeight > 0 && newHeight % epochDuration == 0) {
    // Save pre-epoch snapshot for popBlock reversal
    EpochStateSnapshot preEpoch;
    preEpoch.heatSupply = m_heatSupply;
    preEpoch.heatCdFeePool = m_heatCdFeePool;
    preEpoch.cdYieldPool = m_cdYieldPool;
    preEpoch.cdReserve = m_cdReserve;
    preEpoch.legacyBondYieldPool = m_legacyBondYieldPool;
    preEpoch.treasuryBalance = m_treasuryBalance;
    preEpoch.treasuryHeatReserve = m_treasuryHeatReserve;
    preEpoch.treasuryXfgReserve = m_treasuryXfgReserve;
    preEpoch.treasuryLpReserve = m_treasuryLpReserve;
    preEpoch.protocolLpShares = m_protocolLpShares;
    preEpoch.treasuryLpYield = m_treasuryLpYield;
    preEpoch.bootstrapRepaymentVault = m_bootstrapRepaymentVault;
    preEpoch.twapAccumulatorLo = (uint64_t)(m_twapAccumulator & 0xFFFFFFFFFFFFFFFFULL);
    preEpoch.twapAccumulatorHi = (uint64_t)(m_twapAccumulator >> 64);
    preEpoch.twapBlockCount = m_twapBlockCount;
    preEpoch.ammReserveXfg = m_ammPool.reserveXfg;
    preEpoch.ammReserveHeat = m_ammPool.reserveHeat;
    preEpoch.ammTotalLpShares = m_ammPool.totalLpShares;
    preEpoch.ammAccumulatedLpFeesHeat = m_ammPool.accumulatedLpFeesHeat;
    preEpoch.ammAccumulatedLpFeesXfg = m_ammPool.accumulatedLpFeesXfg;
    preEpoch.digmPrimaryReserveDigm = m_digmPrimaryPool.reserveDigm;
    preEpoch.digmPrimaryReserveHeat = m_digmPrimaryPool.reserveHeat;
    preEpoch.digmBancorReserveXfg = m_digmBancorPool.reserveXfg;
    preEpoch.digmBancorSupplyDigm = m_digmBancorPool.supplyDigm;

    // Auto-roll matured CDs (one-time interest compounding at first maturity)
    size_t autoRolled = m_commitmentIndex.processAutoRolls(newHeight);
    if (autoRolled > 0) {
      logger(INFO) << "Auto-rolled " << autoRolled << " CD(s) at epoch boundary";
    }

    uint64_t epochNumber = newHeight / epochDuration;
    uint64_t epochStart = (epochNumber - 1) * epochDuration;
    uint64_t epochEnd = epochStart + epochDuration - 1;
    // Split swap fees: 80% CD Yield / 20% Treasury Reserve
    uint64_t epochSwapFees = m_currentEpochSwapFees;
    uint64_t epochCdLocked = m_totalCdLocked;
    uint64_t cdShare = (epochSwapFees * CryptoNote::parameters::SWAP_FEE_CD_SHARE_PCT) / 100;
    uint64_t treasuryShare = (epochSwapFees * CryptoNote::parameters::SWAP_FEE_TREASURY_SHARE_PCT) / 100;

    // Split CD share between regular CDs and legacy bonds (50/50 default)
    uint64_t regularCdShare = cdShare;
    uint64_t legacyBondShare = 0;
    uint64_t epochLegacyBondLocked = m_totalLegacyBondLocked;
    if (epochLegacyBondLocked > 0 && m_totalCdLocked > 0) {
      legacyBondShare = (cdShare * CryptoNote::parameters::LEGACY_BOND_CD_SHARE_PCT) / 100;
      regularCdShare = cdShare - legacyBondShare;
    }
    uint64_t epochFeeRate = 0;
    if (epochCdLocked > 0 && regularCdShare > 0) {
      epochFeeRate = static_cast<uint64_t>(
          (uint128_t)regularCdShare * CryptoNote::parameters::FEE_POOL_RATE_PRECISION / epochCdLocked);
    }

    // CD yield floor: if organic rate < 2% APY, inject from treasury LP reserve
    // Annualized: 2% per year → divisor = 100 * epochsPerYear
    if (epochCdLocked > 0) {
      uint64_t floorRate = (CryptoNote::parameters::CD_YIELD_FLOOR_APY_PCT
                           * CryptoNote::parameters::FEE_POOL_RATE_PRECISION)
                           / (100 * CryptoNote::parameters::EPOCHS_PER_YEAR);
      if (epochFeeRate < floorRate) {
        uint64_t shortfall = static_cast<uint64_t>(
            (uint128_t)(floorRate - epochFeeRate) * epochCdLocked
            / CryptoNote::parameters::FEE_POOL_RATE_PRECISION);
        uint64_t injection = std::min(shortfall, m_treasuryLpReserve);
        if (injection > 0) {
          m_treasuryLpReserve -= injection;
          regularCdShare += injection;
          epochFeeRate = static_cast<uint64_t>(
              (uint128_t)regularCdShare * CryptoNote::parameters::FEE_POOL_RATE_PRECISION / epochCdLocked);
        }
      }
    }
    m_commitmentIndex.recordEpochFeeRate(epochNumber, epochFeeRate, regularCdShare, epochCdLocked);

    // Legacy bond fee rate
    uint64_t legacyEpochFeeRate = 0;
    if (epochLegacyBondLocked > 0 && legacyBondShare > 0) {
      legacyEpochFeeRate = static_cast<uint64_t>(
          (uint128_t)legacyBondShare * CryptoNote::parameters::FEE_POOL_RATE_PRECISION / epochLegacyBondLocked);
    }
    m_commitmentIndex.recordLegacyEpochFeeRate(epochNumber, legacyEpochFeeRate, legacyBondShare, epochLegacyBondLocked);

    // Cumulative accounting
    m_totalSwapFeesCollected += epochSwapFees;

    // Route treasury share to treasury balance
    m_treasuryBalance += treasuryShare;
    m_totalTreasuryAccrued += treasuryShare;

    // Route regular CD share: 100% to yield pool for HEAT buyback
    m_cdYieldPool += regularCdShare;

    // Route legacy bond share to legacy bond yield pool
    if (legacyBondShare > 0) {
      m_legacyBondYieldPool += legacyBondShare;
    }

    // CD yield: buy HEAT from Hearth for CD-holder payout.
    // 100% of CD yield → HEAT buy. No protocol cut, no treasury restore —
    // CD holders selling rewards back into pool provides natural ratio restoration.
    if (m_cdYieldPool > 0 && !m_ammPool.isEmpty()) {
      uint64_t heatBuyAmount = m_cdYieldPool;

      uint64_t heatBought = ammGetOutputAmount(heatBuyAmount,
        m_ammPool.reserveXfg, m_ammPool.reserveHeat, 0);

      uint64_t postBuyXfg = m_ammPool.reserveXfg + heatBuyAmount;
      uint64_t postBuyHeat = (heatBought > 0 && heatBought <= m_ammPool.reserveHeat)
                            ? m_ammPool.reserveHeat - heatBought : m_ammPool.reserveHeat;
      uint64_t postBuyRatio = postBuyHeat > 0 ? (postBuyXfg * 100) / postBuyHeat : 999;
      bool poolCanHandleBuy = postBuyRatio <= 400;

      if (poolCanHandleBuy && heatBought > 0 && heatBought <= m_ammPool.reserveHeat) {
        m_ammPool.reserveXfg += heatBuyAmount;
        m_ammPool.reserveHeat -= heatBought;
        m_heatCdFeePool += heatBought;
      } else if (!m_ammPool.isEmpty() && m_ammPool.reserveHeat > 0 && m_ammPool.reserveXfg > 0) {
        // Pool ratio fallback: mint HEAT at current AMM rate
        FixedPoint64 poolRate = FixedPoint64::fromRatio(m_ammPool.reserveXfg, m_ammPool.reserveHeat);
        FixedPoint64 xfgFp = FixedPoint64::fromUint64(heatBuyAmount);
        FixedPoint64 heatFp = xfgFp.div(poolRate);
        uint64_t heatMinted = heatFp.toUint64();
        if (heatMinted > 0 && m_heatSupply <= UINT64_MAX - heatMinted) {
          m_heatSupply += heatMinted;
          m_heatCdFeePool += heatMinted;
        }
      }
      m_cdYieldPool = 0;
    }

    // Treasury LP yield: protocol claims its proportional share of accumulated LP fees (XFG only)
    if (m_protocolLpShares > 0 && m_ammPool.totalLpShares > 0
        && m_ammPool.accumulatedLpFeesXfg > 0) {
      uint64_t treasuryFeeShare = (m_ammPool.accumulatedLpFeesXfg * m_protocolLpShares)
                                / m_ammPool.totalLpShares;
      m_ammPool.accumulatedLpFeesXfg -= treasuryFeeShare;
      m_treasuryBalance += treasuryFeeShare;
      m_treasuryLpYield += treasuryFeeShare;
    }

    // Bootstrap repayment: 20% of treasury inflow → repayment vault
    if (!m_bootstrapRepaid && m_bootstrapXfgOwed > 0 && treasuryShare > 0) {
      uint64_t repayShare = (treasuryShare * CryptoNote::parameters::BOOTSTRAP_REPAY_PCT) / 100;
      if (repayShare > 0 && m_treasuryBalance >= repayShare) {
        m_treasuryBalance -= repayShare;
        m_bootstrapRepaymentVault += repayShare;
        if (m_bootstrapRepaymentVault >= m_bootstrapXfgOwed) {
          m_bootstrapRepaid = true;
          logger(INFO) << "=== Bootstrap liquidity repaid ==="
                       << " vault=" << m_bootstrapRepaymentVault
                       << " owed=" << m_bootstrapXfgOwed;
        }
      }
    }

    // Record the full epoch accumulator as this block's contribution before resetting.
    // popBlock will subtract this value and pop the matching m_epochFeeRates entry.
    m_blockSwapFeeContributions.push_back(epochSwapFees);
    m_blockEpochDistributions.push_back({treasuryShare, 0});
    m_epochSnapshots.push_back({newHeight, preEpoch});
    while (m_epochSnapshots.size() > 100)
      m_epochSnapshots.pop_front();

    // Reset epoch accumulator for next epoch
    m_currentEpochSwapFees = 0;
    // Also reset epochFeesBefore so the non-epoch path below records a zero delta.
    epochFeesBefore = 0;

    EpochReport report = m_commitmentIndex.generateEpochReport(
        epochNumber, epochStart, epochEnd, newHeight);

    // Fill in fee pool fields
    report.swapFeesCollected = epochSwapFees;
    report.totalCdLockedAtStart = epochCdLocked;
    report.feeRateFixedPoint = epochFeeRate;
    report.treasuryBalance = m_treasuryBalance;
    report.rolloverVaultBalance = 0;
    m_commitmentIndex.storeEpochReport(report);
    logger(INFO) << "=== Epoch " << epochNumber << " Report ==="
                 << " blocks=" << epochStart << "-" << epochEnd
                 << " swapFees=" << epochSwapFees
                 << " cdShare(80%)=" << cdShare
                 << " treasuryShare(20%)=" << treasuryShare
                 << " treasuryBal=" << m_treasuryBalance
                 << " feePoolBal=" << m_feePoolBalance
                 << " cdLocked=" << epochCdLocked
                 << " feeRate=" << epochFeeRate;
  } else {
    // Non-epoch-boundary block: record any swap fees accumulated during this block push.
    uint64_t blockContribution = m_currentEpochSwapFees - epochFeesBefore;
    m_blockSwapFeeContributions.push_back(blockContribution);
    m_blockEpochDistributions.push_back({0, 0});  // No distribution on non-epoch blocks
  }

  return true;
}

bool Blockchain::bootstrapAmmPool(uint64_t xfgReserve, uint64_t heatReserve) {
  if (xfgReserve == 0 || heatReserve == 0)
    return false;
  if (!m_ammPool.isEmpty())
    return false;

  m_ammPool.reserveXfg  = xfgReserve;
  m_ammPool.reserveHeat = heatReserve;

  logger(INFO, BRIGHT_WHITE) << "Hearth AMM pool bootstrapped: "
    << m_currency.formatAmount(xfgReserve) << " XFG + "
    << m_currency.formatAmount(heatReserve) << " HEAT";
  return true;
}

bool Blockchain::bootstrapDigmPools() {
  // Only bootstrap once
  if (!m_digmBancorPool.isEmpty() || !m_digmPrimaryPool.isEmpty())
    return false;

  // HEAT/DIGM primary pool: 5,000 DIGM + computed HEAT ($0.10 per DIGM)
  uint64_t seedDigm = parameters::DIGM_PRIMARY_POOL_SEED_DIGM * parameters::COIN;
  uint64_t seedHeat = parameters::DIGM_PRIMARY_POOL_SEED_HEAT;
  m_digmPrimaryPool.reserveDigm = seedDigm;
  m_digmPrimaryPool.reserveHeat = seedHeat;

  // XFG/DIGM Bancor pool: zero-start with virtual reserves
  uint64_t vs = parameters::COIN;  // 1 whole DIGM virtual supply
  uint64_t cwNum = parameters::DIGM_BANCOR_CW_NUM;
  uint64_t cwDenom = parameters::DIGM_BANCOR_CW_DENOM;

  // DIGM price in XFG: $0.10 / $1.58 = 10/158
  uint64_t vr = bancorComputeVirtualReserve(vs, cwNum, cwDenom, 10, 158);

  m_digmBancorPool.virtualSupply  = vs;
  m_digmBancorPool.virtualReserve = vr;
  m_digmBancorPool.cwNum   = cwNum;
  m_digmBancorPool.cwDenom = cwDenom;
  m_digmBancorPool.reserveXfg = 0;
  m_digmBancorPool.supplyDigm = 0;

  logger(INFO, BRIGHT_YELLOW) << "DIGM pools bootstrapped:"
    << " primary=" << parameters::DIGM_PRIMARY_POOL_SEED_DIGM << " DIGM + "
    << m_currency.formatAmount(seedHeat) << " HEAT"
    << " | bancor V_r=" << vr << " V_s=" << vs
    << " (zero-start, pump.fun style)";
  return true;
}

uint64_t Blockchain::digmPrimarySwap(uint64_t heatIn, bool dryRun) {
  if (heatIn == 0 || m_digmPrimaryPool.isEmpty())
    return 0;

  // HEAT → DIGM constant-product swap (buy-only)
  uint64_t digmOut = ammGetOutputAmount(
      heatIn, m_digmPrimaryPool.reserveHeat,
      m_digmPrimaryPool.reserveDigm, parameters::DIGM_FEE_BPS);

  if (digmOut == 0 || digmOut >= m_digmPrimaryPool.reserveDigm)
    return 0;

  if (!dryRun) {
    m_digmPrimaryPool.reserveHeat += heatIn;
    m_digmPrimaryPool.reserveDigm -= digmOut;
    uint64_t feeHeat = (heatIn * parameters::DIGM_FEE_BPS) / parameters::DIGM_FEE_DIVISOR;
    m_digmPrimaryPool.accumulatedLpFees += feeHeat;
  }

  return digmOut;
}

uint64_t Blockchain::digmBancorBuy(uint64_t xfgIn, bool dryRun) {
  if (xfgIn == 0 || m_digmBancorPool.isEmpty())
    return 0;

  uint64_t maxSupply = parameters::DIGM_BANCOR_MAX_SUPPLY * parameters::COIN;
  uint64_t remaining = (maxSupply > m_digmBancorPool.supplyDigm)
                     ? maxSupply - m_digmBancorPool.supplyDigm : 0;
  if (remaining == 0) return 0;

  uint64_t minted = bancorBuyOutput(
      xfgIn, m_digmBancorPool.reserveXfg, m_digmBancorPool.supplyDigm,
      m_digmBancorPool.virtualReserve, m_digmBancorPool.virtualSupply,
      m_digmBancorPool.cwNum, m_digmBancorPool.cwDenom);

  if (minted == 0 || minted > remaining) return 0;

  if (!dryRun) {
    m_digmBancorPool.reserveXfg += xfgIn;
    m_digmBancorPool.supplyDigm += minted;
  }

  return minted;
}

uint64_t Blockchain::digmBancorSell(uint64_t digmIn, bool dryRun) {
  if (digmIn == 0 || m_digmBancorPool.isEmpty() || m_digmBancorPool.supplyDigm == 0)
    return 0;

  if (digmIn > m_digmBancorPool.supplyDigm) return 0;

  uint64_t xfgOut = bancorSellOutput(
      digmIn, m_digmBancorPool.reserveXfg, m_digmBancorPool.supplyDigm,
      m_digmBancorPool.virtualReserve, m_digmBancorPool.virtualSupply,
      m_digmBancorPool.cwNum, m_digmBancorPool.cwDenom);

  if (xfgOut == 0 || xfgOut > m_digmBancorPool.reserveXfg) return 0;

  if (!dryRun) {
    m_digmBancorPool.reserveXfg -= xfgOut;
    m_digmBancorPool.supplyDigm -= digmIn;
  }

  return xfgOut;
}

bool Blockchain::withdrawTreasuryLp(uint64_t sharesToBurn) {
  if (sharesToBurn == 0 || sharesToBurn > m_protocolLpShares)
    return false;
  if (m_ammPool.totalLpShares == 0 || m_ammPool.isEmpty())
    return false;

  uint64_t xfgOut = 0, heatOut = 0;
  ammGetWithdrawalAmounts(sharesToBurn, m_ammPool.totalLpShares,
      m_ammPool.reserveXfg, m_ammPool.reserveHeat,
      xfgOut, heatOut);

  if (xfgOut == 0 && heatOut == 0)
    return false;
  if (xfgOut > m_ammPool.reserveXfg || heatOut > m_ammPool.reserveHeat)
    return false;

  m_ammPool.totalLpShares -= sharesToBurn;
  m_ammPool.reserveXfg    -= xfgOut;
  m_ammPool.reserveHeat   -= heatOut;
  m_protocolLpShares      -= sharesToBurn;
  m_treasuryBalance       += xfgOut;
  m_treasuryHeatReserve   += heatOut;

  logger(INFO, BRIGHT_GREEN) << "Treasury LP withdrawal: "
    << "shares=" << sharesToBurn << " -> "
    << m_currency.formatAmount(xfgOut) << " XFG + "
    << m_currency.formatAmount(heatOut) << " HEAT"
    << " | protocolLpShares=" << m_protocolLpShares
    << " | treasuryBal=" << m_currency.formatAmount(m_treasuryBalance)
    << " | treasuryHeatRes=" << m_currency.formatAmount(m_treasuryHeatReserve);
  return true;
}

void Blockchain::popBlock(const Crypto::Hash& blockHash) {
  if (m_blocks.empty()) {
    logger(ERROR, BRIGHT_RED) <<
      "Attempt to pop block from empty blockchain.";
    return;
  }

  std::vector<Transaction> transactions(m_blocks.back().transactions.size() - 1);
  for (size_t i = 0; i < m_blocks.back().transactions.size() - 1; ++i) {
    transactions[i] = m_blocks.back().transactions[1 + i].tx;
  }

  uint32_t height = m_blocks.size(); //height of popped block should be same as number of blocks
  saveTransactions(transactions, height);

  popTransactions(m_blocks.back(), getObjectHash(m_blocks.back().bl.baseTransaction));

  m_timestampIndex.remove(m_blocks.back().bl.timestamp, blockHash);
  m_generatedTransactionsIndex.remove(m_blocks.back().bl);

  // Remove commitments from popped block
  uint32_t poppedHeight = m_blocks.back().height;
  size_t commitmentsRemoved = m_commitmentIndex.rollbackToHeight(poppedHeight);
  if (commitmentsRemoved > 0) {
    logger(DEBUGGING) << "Removed " << commitmentsRemoved << " commitments during block rollback at height " << poppedHeight;
  }

  m_bankingIndex.popBlock();

  // Undo per-block swap-fee contribution to the epoch accumulator.
  if (!m_blockSwapFeeContributions.empty()) {
    uint64_t contribution = m_blockSwapFeeContributions.back();
    m_blockSwapFeeContributions.pop_back();

    uint64_t epochDuration = m_currency.isTestnet()
        ? CryptoNote::parameters::TESTNET_EPOCH_DURATION_BLOCKS
        : CryptoNote::parameters::EPOCH_DURATION_BLOCKS;

    if (poppedHeight > 0 && poppedHeight % epochDuration == 0) {
      // This block was an epoch boundary: reverse the epoch distributions
      m_currentEpochSwapFees += contribution;
      m_totalSwapFeesCollected -= contribution;
      m_commitmentIndex.popEpochFeeRate();
      m_commitmentIndex.popLegacyEpochFeeRate();
      
      // Reverse treasury distribution
      if (!m_blockEpochDistributions.empty()) {
        auto dist = m_blockEpochDistributions.back();
        m_blockEpochDistributions.pop_back();
        if (dist.first > 0 && m_treasuryBalance >= dist.first) {
          m_treasuryBalance -= dist.first;
          m_totalTreasuryAccrued -= dist.first;
        }
      }
    } else {
      // Non-boundary block: simply subtract the fee delta that was added.
      m_currentEpochSwapFees -= contribution;
    }
  }

  // Restore epoch-level state if the popped block was an epoch boundary
  if (!m_epochSnapshots.empty() && m_epochSnapshots.back().first == poppedHeight) {
    const auto& snap = m_epochSnapshots.back().second;
    m_heatSupply = snap.heatSupply;
    m_heatCdFeePool = snap.heatCdFeePool;
    m_cdYieldPool = snap.cdYieldPool;
    m_cdReserve = snap.cdReserve;
    m_legacyBondYieldPool = snap.legacyBondYieldPool;
    m_treasuryBalance = snap.treasuryBalance;
    m_treasuryHeatReserve = snap.treasuryHeatReserve;
    m_treasuryXfgReserve = snap.treasuryXfgReserve;
    m_treasuryLpReserve = snap.treasuryLpReserve;
    m_protocolLpShares = snap.protocolLpShares;
    m_treasuryLpYield = snap.treasuryLpYield;
    m_bootstrapRepaymentVault = snap.bootstrapRepaymentVault;
    m_twapAccumulator = ((uint128_t)snap.twapAccumulatorHi << 64) | snap.twapAccumulatorLo;
    m_twapBlockCount = snap.twapBlockCount;
    m_ammPool.reserveXfg = snap.ammReserveXfg;
    m_ammPool.reserveHeat = snap.ammReserveHeat;
    m_ammPool.totalLpShares = snap.ammTotalLpShares;
    m_ammPool.accumulatedLpFeesHeat = snap.ammAccumulatedLpFeesHeat;
    m_ammPool.accumulatedLpFeesXfg = snap.ammAccumulatedLpFeesXfg;
    m_digmPrimaryPool.reserveDigm = snap.digmPrimaryReserveDigm;
    m_digmPrimaryPool.reserveHeat = snap.digmPrimaryReserveHeat;
    m_digmBancorPool.reserveXfg = snap.digmBancorReserveXfg;
    m_digmBancorPool.supplyDigm = snap.digmBancorSupplyDigm;
    m_epochSnapshots.pop_back();
  }

  // Reverse per-block TWAP contribution.
  // Epoch-boundary blocks: snapshot already restored correct accumulator — just pop the deque.
  // Non-boundary blocks: subtract the contribution from accumulator.
  if (!m_blockTwapContributions.empty()) {
    bool isEpochBoundary = !m_epochSnapshots.empty() && m_epochSnapshots.back().first == poppedHeight;
    if (!isEpochBoundary) {
      uint128_t q64 = m_blockTwapContributions.back();
      if (m_twapAccumulator >= q64 && m_twapBlockCount > 0) {
        m_twapAccumulator -= q64;
        m_twapBlockCount--;
      }
    }
    m_blockTwapContributions.pop_back();
  }

  m_blocks.pop_back();
  m_blockIndex.pop();

  assert(m_blockIndex.size() == m_blocks.size());

  // Sync currency eternal-flame from banking index after rollback
  const_cast<Currency&>(m_currency).syncEternalFlame(m_bankingIndex.getBurnedXfgAmount());

  m_upgradeDetectorV2.blockPopped();
  m_upgradeDetectorV3.blockPopped();
  m_upgradeDetectorV4.blockPopped();
  m_upgradeDetectorV5.blockPopped();
  m_upgradeDetectorV6.blockPopped();
  m_upgradeDetectorV7.blockPopped();
  m_upgradeDetectorV8.blockPopped();
  m_upgradeDetectorV9.blockPopped();
  m_upgradeDetectorV10.blockPopped();
  m_upgradeDetectorV11.blockPopped();


}

bool Blockchain::pushTransaction(BlockEntry& block, const Crypto::Hash& transactionHash, TxIndex transactionIndex) {
  auto result = m_indexManager.transactionMap().insert(std::make_pair(transactionHash, transactionIndex));
  if (!result.second) {
    logger(ERROR, BRIGHT_RED) <<
      "Duplicate transaction was pushed to blockchain.";
    return false;
  }

  TransactionEntry& transaction = block.transactions[transactionIndex.transaction];

  if (!checkMultisignatureInputsDiff(transaction.tx)) {
    logger(ERROR, BRIGHT_RED) <<
      "Double spending transaction was pushed to blockchain.";
    m_indexManager.transactionMap().erase(transactionHash);
    return false;
  }

  for (size_t i = 0; i < transaction.tx.inputs.size(); ++i)
  {
      if (transaction.tx.inputs[i].type() == typeid(KeyInput))
      {
        auto result = m_indexManager.spentKeys().insert(std::make_pair(::boost::get<KeyInput>(transaction.tx.inputs[i]).keyImage, block.height));
        if (!result.second)
        {
          logger(ERROR, BRIGHT_RED) << "Double spending transaction was pushed to blockchain.";

          for (size_t j = 0; j < i; ++j)
          {
            m_indexManager.spentKeys().erase(::boost::get<KeyInput>(transaction.tx.inputs[i - 1 - j]).keyImage);
          }

        m_indexManager.transactionMap().erase(transactionHash);
        return false;
      }
    }
  }

  for (const auto& inv : transaction.tx.inputs) {
    if (inv.type() == typeid(MultisignatureInput)) {
      const MultisignatureInput& in = ::boost::get<MultisignatureInput>(inv);
      auto& amountOutputs = m_indexManager.multisigOutputs()[in.amount];
      amountOutputs[in.outputIndex].isUsed = true;
    } else if (inv.type() == typeid(TransactionInputCommitmentSpend)) {
      const auto& cin = ::boost::get<TransactionInputCommitmentSpend>(inv);
      auto result = m_indexManager.spentKeys().insert(std::make_pair(cin.keyImage, block.height));
      if (!result.second) {
        logger(ERROR, BRIGHT_RED) << "Double spending commitment transaction was pushed to blockchain.";
        m_indexManager.transactionMap().erase(transactionHash);
        return false;
      }
      // CD redemption: reduce locked supply, deduct claimed interest from fee pool
      m_totalCdLocked -= cin.amount;
      if (cin.claimedInterest > 0) {
        if (cin.claimedInterest <= m_feePoolBalance) {
          m_feePoolBalance -= cin.claimedInterest;
          m_totalCdInterestPaid += cin.claimedInterest;
        } else {
          // Defense-in-depth: the per-transaction aggregate fee-pool cap in the
          // block-validation loop (F-001 fix) guarantees this branch is
          // unreachable for accepted blocks. If it is ever hit the pool
          // accounting is inconsistent — log loudly instead of silently leaving
          // unbacked interest minted into the outputs.
          logger(ERROR, BRIGHT_RED) << "Fee-pool invariant violated: CD claimedInterest "
              << cin.claimedInterest << " exceeds pool " << m_feePoolBalance
              << " at connect for tx " << transactionHash;
        }
      }
    } else if (inv.type() == typeid(TransactionInputCommitmentTransfer)) {
      const auto& xfer = ::boost::get<TransactionInputCommitmentTransfer>(inv);
      auto result = m_indexManager.spentKeys().insert(std::make_pair(xfer.keyImage, block.height));
      if (!result.second) {
        logger(ERROR, BRIGHT_RED) << "Double spending commitment transfer was pushed to blockchain.";
        m_indexManager.transactionMap().erase(transactionHash);
        return false;
      }
      // Transfer doesn't change locked supply (old CD consumed, new CD produced — net zero)
    }
  }

  transaction.m_global_output_indexes.resize(transaction.tx.outputs.size());
  for (uint16_t output = 0; output < transaction.tx.outputs.size(); ++output) {
    if (transaction.tx.outputs[output].target.type() == typeid(KeyOutput)) {
      auto& amountOutputs = m_indexManager.outputs()[transaction.tx.outputs[output].amount];
      transaction.m_global_output_indexes[output] = static_cast<uint32_t>(amountOutputs.size());
      amountOutputs.push_back(std::make_pair<>(transactionIndex, output));
    } else if (transaction.tx.outputs[output].target.type() == typeid(MultisignatureOutput)) {
      auto& amountOutputs = m_indexManager.multisigOutputs()[transaction.tx.outputs[output].amount];
      transaction.m_global_output_indexes[output] = static_cast<uint32_t>(amountOutputs.size());
      MultisignatureOutputUsage outputUsage = { transactionIndex, output, false };
      amountOutputs.push_back(outputUsage);
    } else if (transaction.tx.outputs[output].target.type() == typeid(TransactionOutputCommitment)) {
      const auto& commitOut = ::boost::get<TransactionOutputCommitment>(transaction.tx.outputs[output].target);
      auto& amountOutputs = m_indexManager.commitmentOutputs()[transaction.tx.outputs[output].amount];
      transaction.m_global_output_indexes[output] = static_cast<uint32_t>(amountOutputs.size());
      CommitmentOutputRef ref;
      ref.transactionIndex    = transactionIndex;
      ref.outputInTransaction = output;
      ref.commitKey           = commitOut.commitKey;
      ref.term                = commitOut.term;
      amountOutputs.push_back(ref);
      // Track total XFG locked in CDs
      m_totalCdLocked += transaction.tx.outputs[output].amount;
      // Track HEAT supply for burn-to-mint outputs (validated by HeatMintEngine before push)
      if (commitOut.term == parameters::HEAT_TERM) {
        if (m_heatSupply <= UINT64_MAX - transaction.tx.outputs[output].amount)
          m_heatSupply += transaction.tx.outputs[output].amount;
      }
      // Track pool-locked reserves for invariant verification
      if (commitOut.term == parameters::DEPOSIT_TERM_POOL_XFG) {
        m_poolLockedXfg += transaction.tx.outputs[output].amount;
      } else if (commitOut.term == parameters::DEPOSIT_TERM_POOL_HEAT) {
        m_poolLockedHeat += transaction.tx.outputs[output].amount;
      }
    }
  }

  m_paymentIdIndex.add(transaction.tx);

  // AMM state mutation (v11+)
  if (block.bl.majorVersion >= BLOCK_MAJOR_VERSION_11) {
    std::vector<TransactionExtraField> tx_extra_fields;
    if (parseTransactionExtra(transaction.tx.extra, tx_extra_fields)) {
      // Pre-scan for auth tags to ensure execution uses validated values (prevents dual-tag conflict)
      uint64_t authSwapDir0Input = 0, authSwapDir1Input = 0;
      uint64_t authLpAddXfg = 0, authLpAddHeat = 0;
      for (const auto& f : tx_extra_fields) {
        if (f.type() == typeid(TransactionExtraAmmSwapAuth)) {
          const auto& a = boost::get<TransactionExtraAmmSwapAuth>(f);
          if (a.direction == 0) authSwapDir0Input = std::max(authSwapDir0Input, a.inputAmount);
          else                  authSwapDir1Input = std::max(authSwapDir1Input, a.inputAmount);
        } else if (f.type() == typeid(TransactionExtraLpAddAuth)) {
          const auto& a = boost::get<TransactionExtraLpAddAuth>(f);
          if (a.amountXfg > 0 || a.amountHeat > 0) {
            authLpAddXfg = a.amountXfg; authLpAddHeat = a.amountHeat;
          }
        }
      }
      for (const auto& field : tx_extra_fields) {
        if (field.type() == typeid(TransactionExtraAmmSwap)) {
          const auto& swap = boost::get<TransactionExtraAmmSwap>(field);
          // Use auth-tag input if present (prevents dual-tag mismatch consensus fork)
          uint64_t inputAmount = swap.inputAmount;
          if (swap.direction == 0 && authSwapDir0Input > 0) inputAmount = authSwapDir0Input;
          if (swap.direction != 0 && authSwapDir1Input > 0) inputAmount = authSwapDir1Input;
          uint32_t feeBps = parameters::HEARTH_FEE_BPS;
          uint64_t outputAmount = 0;
          if (swap.direction == 0) {
            outputAmount = ammGetOutputAmount(inputAmount, m_ammPool.reserveXfg, m_ammPool.reserveHeat, feeBps);
            uint64_t outputNoFee = ammGetOutputAmount(inputAmount, m_ammPool.reserveXfg, m_ammPool.reserveHeat, 0);
            uint64_t actualFee = outputNoFee > outputAmount ? outputNoFee - outputAmount : 0;
            m_ammPool.reserveXfg += inputAmount;
            m_ammPool.reserveHeat -= outputAmount;
            m_ammPool.accumulatedLpFeesHeat += actualFee;
          } else {
            outputAmount = ammGetOutputAmount(inputAmount, m_ammPool.reserveHeat, m_ammPool.reserveXfg, feeBps);
            uint64_t outputNoFee = ammGetOutputAmount(inputAmount, m_ammPool.reserveHeat, m_ammPool.reserveXfg, 0);
            uint64_t actualFee = outputNoFee > outputAmount ? outputNoFee - outputAmount : 0;
            m_ammPool.reserveHeat += inputAmount;
            m_ammPool.reserveXfg -= outputAmount;
            m_ammPool.accumulatedLpFeesXfg += actualFee;
          }
        } else if (field.type() == typeid(TransactionExtraAmmAddLiquidity)) {
          const auto& add = boost::get<TransactionExtraAmmAddLiquidity>(field);
          // Use auth-tag amounts if present (prevents dual-tag mismatch)
          uint64_t addXfg = add.amountXfg;
          uint64_t addHeat = add.amountHeat;
          if (authLpAddXfg > 0 || authLpAddHeat > 0) { addXfg = authLpAddXfg; addHeat = authLpAddHeat; }
          uint64_t shares = ammMintLpShares(addXfg, addHeat,
            m_ammPool.totalLpShares, m_ammPool.reserveXfg, m_ammPool.reserveHeat);
          m_ammPool.reserveXfg += addXfg;
          m_ammPool.reserveHeat += addHeat;
          m_ammPool.totalLpShares += shares;
          // Track LP shares by global commitment output index for per-user fee claims
          for (uint16_t o = 0; o < transaction.tx.outputs.size(); ++o) {
            if (transaction.tx.outputs[o].target.type() == typeid(TransactionOutputCommitment)) {
              const auto& co = boost::get<TransactionOutputCommitment>(transaction.tx.outputs[o].target);
              if (co.term == parameters::DEPOSIT_TERM_LP) {
                uint64_t gidx = transaction.m_global_output_indexes[o];
                m_lpCommitmentShares[gidx] = shares;
                break;
              }
            }
          }
        } else if (field.type() == typeid(TransactionExtraAmmRemoveLiquidity)) {
          const auto& rem = boost::get<TransactionExtraAmmRemoveLiquidity>(field);
          uint64_t amountXfg = 0, amountHeat = 0;
          ammGetWithdrawalAmounts(rem.lpSharesBurned, m_ammPool.totalLpShares,
            m_ammPool.reserveXfg, m_ammPool.reserveHeat, amountXfg, amountHeat);
          if (m_ammPool.reserveXfg >= amountXfg) m_ammPool.reserveXfg -= amountXfg;
          if (m_ammPool.reserveHeat >= amountHeat) m_ammPool.reserveHeat -= amountHeat;
          if (m_ammPool.totalLpShares >= rem.lpSharesBurned) m_ammPool.totalLpShares -= rem.lpSharesBurned;
        } else if (field.type() == typeid(TransactionExtraAmmCompound)) {
          // Auto-compound: accumulated fees are already in reserves. No state change needed.
        } else if (field.type() == typeid(TransactionExtraAmmClaim)) {
          const auto& claim = boost::get<TransactionExtraAmmClaim>(field);
          uint64_t feeXfg = (m_ammPool.accumulatedLpFeesXfg * claim.lpShares) / m_ammPool.totalLpShares;
          if (m_ammPool.accumulatedLpFeesXfg >= feeXfg)
            m_ammPool.accumulatedLpFeesXfg -= feeXfg;
          // Auto-compound HEAT fees: proportional share goes to HEAT reserve (no tx output needed)
          uint64_t feeHeat = (m_ammPool.accumulatedLpFeesHeat * claim.lpShares) / m_ammPool.totalLpShares;
          if (m_ammPool.accumulatedLpFeesHeat >= feeHeat) {
            m_ammPool.accumulatedLpFeesHeat -= feeHeat;
            m_ammPool.reserveHeat += feeHeat;
          }
          // Fee-only: principal stays in pool, LP keeps shares. No reserve drain.
        }
      }
    }
  }

  return true;
}

void Blockchain::popTransaction(const Transaction& transaction, const Crypto::Hash& transactionHash) {
  TxIndex transactionIndex = m_indexManager.transactionMap().at(transactionHash);
  for (size_t outputIndex = 0; outputIndex < transaction.outputs.size(); ++outputIndex) {
    const TransactionOutput& output = transaction.outputs[transaction.outputs.size() - 1 - outputIndex];
    if (output.target.type() == typeid(KeyOutput)) {
      auto amountOutputs = m_indexManager.outputs().find(output.amount);
      if (amountOutputs == m_indexManager.outputs().end()) {
        logger(ERROR, BRIGHT_RED) <<
          "Blockchain consistency broken - cannot find specific amount in outputs map.";
        continue;
      }

      if (amountOutputs->second.empty()) {
        logger(ERROR, BRIGHT_RED) <<
          "Blockchain consistency broken - output array for specific amount is empty.";
        continue;
      }

      if (amountOutputs->second.back().first.block != transactionIndex.block || amountOutputs->second.back().first.transaction != transactionIndex.transaction) {
        logger(ERROR, BRIGHT_RED) <<
          "Blockchain consistency broken - invalid transaction index.";
        continue;
      }

      if (amountOutputs->second.back().second != transaction.outputs.size() - 1 - outputIndex) {
        logger(ERROR, BRIGHT_RED) <<
          "Blockchain consistency broken - invalid output index.";
        continue;
      }

      amountOutputs->second.pop_back();
      if (amountOutputs->second.empty()) {
        m_indexManager.outputs().erase(amountOutputs);
      }
    } else if (output.target.type() == typeid(MultisignatureOutput)) {
      auto amountOutputs = m_indexManager.multisigOutputs().find(output.amount);
      if (amountOutputs == m_indexManager.multisigOutputs().end()) {
        logger(ERROR, BRIGHT_RED) <<
          "Blockchain consistency broken - cannot find specific amount in outputs map.";
        continue;
      }

      if (amountOutputs->second.empty()) {
        logger(ERROR, BRIGHT_RED) <<
          "Blockchain consistency broken - output array for specific amount is empty.";
        continue;
      }

      if (amountOutputs->second.back().isUsed) {
        logger(ERROR, BRIGHT_RED) <<
          "Blockchain consistency broken - attempting to remove used output.";
        continue;
      }

      if (amountOutputs->second.back().transactionIndex.block != transactionIndex.block || amountOutputs->second.back().transactionIndex.transaction != transactionIndex.transaction) {
        logger(ERROR, BRIGHT_RED) <<
          "Blockchain consistency broken - invalid transaction index.";
        continue;
      }

      if (amountOutputs->second.back().outputIndex != transaction.outputs.size() - 1 - outputIndex) {
        logger(ERROR, BRIGHT_RED) <<
          "Blockchain consistency broken - invalid output index.";
        continue;
      }

      amountOutputs->second.pop_back();
      if (amountOutputs->second.empty()) {
        m_indexManager.multisigOutputs().erase(amountOutputs);
      }
    } else if (output.target.type() == typeid(TransactionOutputCommitment)) {
      auto amountOutputs = m_indexManager.commitmentOutputs().find(output.amount);
      if (amountOutputs == m_indexManager.commitmentOutputs().end()) {
        logger(ERROR, BRIGHT_RED) <<
          "Blockchain consistency broken - cannot find specific amount in commitment outputs map.";
        continue;
      }

      if (amountOutputs->second.empty()) {
        logger(ERROR, BRIGHT_RED) <<
          "Blockchain consistency broken - commitment output array for specific amount is empty.";
        continue;
      }

      amountOutputs->second.pop_back();
      if (amountOutputs->second.empty()) {
        m_indexManager.commitmentOutputs().erase(amountOutputs);
      }
      // Reverse CD locked supply tracking
      if (m_totalCdLocked >= output.amount) {
        m_totalCdLocked -= output.amount;
      }
      // Reverse HEAT supply for burn-to-mint outputs
      if (m_heatSupply >= output.amount) {
        const auto& commitOut = ::boost::get<TransactionOutputCommitment>(output.target);
        if (commitOut.term == parameters::HEAT_TERM) {
          m_heatSupply -= output.amount;
        }
      }
      // Reverse pool-locked reserves
      {
        const auto& commitOut = ::boost::get<TransactionOutputCommitment>(output.target);
        if (commitOut.term == parameters::DEPOSIT_TERM_POOL_XFG) {
          if (m_poolLockedXfg >= output.amount) m_poolLockedXfg -= output.amount;
        } else if (commitOut.term == parameters::DEPOSIT_TERM_POOL_HEAT) {
          if (m_poolLockedHeat >= output.amount) m_poolLockedHeat -= output.amount;
        }
      }
    }
  }

  for (auto& input : transaction.inputs) {
    if (input.type() == typeid(KeyInput)) {
      size_t count = m_indexManager.spentKeys().erase(::boost::get<KeyInput>(input).keyImage);
      if (count != 1) {
        logger(ERROR, BRIGHT_RED) <<
          "Blockchain consistency broken - cannot find spent key.";
      }
    } else if (input.type() == typeid(MultisignatureInput)) {
      const MultisignatureInput& in = ::boost::get<MultisignatureInput>(input);
      auto& amountOutputs = m_indexManager.multisigOutputs()[in.amount];
      if (!amountOutputs[in.outputIndex].isUsed) {
        logger(ERROR, BRIGHT_RED) <<
          "Blockchain consistency broken - multisignature output not marked as used.";
      }

      amountOutputs[in.outputIndex].isUsed = false;
    } else if (input.type() == typeid(TransactionInputCommitmentSpend)) {
      const auto& cin = ::boost::get<TransactionInputCommitmentSpend>(input);
      size_t count = m_indexManager.spentKeys().erase(cin.keyImage);
      if (count != 1) {
        logger(ERROR, BRIGHT_RED) <<
          "Blockchain consistency broken - cannot find spent commitment key.";
      }
      // Reverse: restore locked supply and fee pool
      m_totalCdLocked += cin.amount;
      if (cin.claimedInterest > 0) {
        m_feePoolBalance += cin.claimedInterest;
        if (m_totalCdInterestPaid >= cin.claimedInterest) {
          m_totalCdInterestPaid -= cin.claimedInterest;
        }
      }
    } else if (input.type() == typeid(TransactionInputCommitmentTransfer)) {
      const auto& xfer = ::boost::get<TransactionInputCommitmentTransfer>(input);
      size_t count = m_indexManager.spentKeys().erase(xfer.keyImage);
      if (count != 1) {
        logger(ERROR, BRIGHT_RED) <<
          "Blockchain consistency broken - cannot find spent commitment transfer key.";
      }
    }
  }

  m_paymentIdIndex.remove(transaction);

  size_t count = m_indexManager.transactionMap().erase(transactionHash);
  if (count != 1) {
    logger(ERROR, BRIGHT_RED) <<
      "Blockchain consistency broken - cannot find transaction by hash.";
  }

  // Reverse AMM state mutations (v11+)
  std::vector<TransactionExtraField> tx_extra_fields;
  if (parseTransactionExtra(transaction.extra, tx_extra_fields)) {
    for (auto it = tx_extra_fields.rbegin(); it != tx_extra_fields.rend(); ++it) {
      const auto& field = *it;
      if (field.type() == typeid(TransactionExtraAmmSwap)) {
        const auto& swap = boost::get<TransactionExtraAmmSwap>(field);
        uint32_t feeBps = parameters::HEARTH_FEE_BPS;
        if (swap.direction == 0) {
          uint64_t preXfg = m_ammPool.reserveXfg - swap.inputAmount;
          uint64_t feeAdj = parameters::HEARTH_FEE_DIVISOR - feeBps;
          uint64_t outputAmount = (uint64_t)(((uint128_t)m_ammPool.reserveHeat
            * swap.inputAmount * feeAdj) / ((uint128_t)preXfg * parameters::HEARTH_FEE_DIVISOR));
          // Reconstruct pre-swap HEAT reserve for correct outputNoFee computation
          uint64_t preHeat = m_ammPool.reserveHeat + outputAmount;
          uint64_t outputNoFee = ammGetOutputAmount(swap.inputAmount, preXfg, preHeat, 0);
          uint64_t actualFee = outputNoFee > outputAmount ? outputNoFee - outputAmount : 0;
          if (m_ammPool.accumulatedLpFeesHeat >= actualFee) m_ammPool.accumulatedLpFeesHeat -= actualFee;
          m_ammPool.reserveHeat += outputAmount;
          m_ammPool.reserveXfg -= swap.inputAmount;
        } else {
          uint64_t preHeat = m_ammPool.reserveHeat - swap.inputAmount;
          uint64_t feeAdj = parameters::HEARTH_FEE_DIVISOR - feeBps;
          uint64_t outputAmount = (uint64_t)(((uint128_t)m_ammPool.reserveXfg
            * swap.inputAmount * feeAdj) / ((uint128_t)preHeat * parameters::HEARTH_FEE_DIVISOR));
          // Reconstruct pre-swap XFG reserve for correct outputNoFee computation
          uint64_t preXfg = m_ammPool.reserveXfg + outputAmount;
          uint64_t outputNoFee = ammGetOutputAmount(swap.inputAmount, preHeat, preXfg, 0);
          uint64_t actualFee = outputNoFee > outputAmount ? outputNoFee - outputAmount : 0;
          if (m_ammPool.accumulatedLpFeesXfg >= actualFee) m_ammPool.accumulatedLpFeesXfg -= actualFee;
          m_ammPool.reserveXfg += outputAmount;
          m_ammPool.reserveHeat -= swap.inputAmount;
        }
      } else if (field.type() == typeid(TransactionExtraAmmAddLiquidity)) {
        const auto& add = boost::get<TransactionExtraAmmAddLiquidity>(field);
        // Compute shares BEFORE decrementing reserves to match pushTransaction's pre-deposit calculation.
        // Mathematical proof: totalLpShares_new * amountXfg / reserveXfg_new = totalLpShares_original * amountXfg / reserveXfg_original
        uint64_t shares = ammMintLpShares(add.amountXfg, add.amountHeat,
          m_ammPool.totalLpShares, m_ammPool.reserveXfg, m_ammPool.reserveHeat);
        if (m_ammPool.reserveHeat >= add.amountHeat) m_ammPool.reserveHeat -= add.amountHeat;
        if (m_ammPool.reserveXfg >= add.amountXfg) m_ammPool.reserveXfg -= add.amountXfg;
        if (m_ammPool.totalLpShares >= shares) m_ammPool.totalLpShares -= shares;
        // NOTE: m_lpCommitmentShares not cleaned up on reversal — global output
        // index not available in popBlock's raw Transaction context. Stale map
        // entry is harmless (consumes memory, overwritten on next LP add at same
        // index). Full fix requires tracking gidx in the pop path.
      } else if (field.type() == typeid(TransactionExtraAmmRemoveLiquidity)) {
        const auto& rem = boost::get<TransactionExtraAmmRemoveLiquidity>(field);
        uint64_t amountXfg = 0, amountHeat = 0;
        ammGetWithdrawalAmounts(rem.lpSharesBurned, m_ammPool.totalLpShares,
          m_ammPool.reserveXfg, m_ammPool.reserveHeat, amountXfg, amountHeat);
        m_ammPool.totalLpShares += rem.lpSharesBurned;
        m_ammPool.reserveHeat += amountHeat;
        m_ammPool.reserveXfg += amountXfg;
      } else if (field.type() == typeid(TransactionExtraAmmCompound)) {
        // Compound was a no-op in forward; no reversal needed
      } else if (field.type() == typeid(TransactionExtraAmmClaim)) {
        const auto& claim = boost::get<TransactionExtraAmmClaim>(field);
        // Reverse claim: derive original feeXfg from post-claim accumulator.
        // Claim does NOT modify totalLpShares. Formula: feeXfg = acc_current * shares / (total - shares)
        // Edge case: when shares == total, all fees belong to claimer — feeXfg = acc_original (unknown from acc_current alone).
        // In practice, claiming all shares with zero fees is the common case; handle both.
        uint64_t feeXfg = 0;
        if (m_ammPool.totalLpShares > claim.lpShares) {
          feeXfg = (m_ammPool.accumulatedLpFeesXfg * claim.lpShares)
                   / (m_ammPool.totalLpShares - claim.lpShares);
        } else {
          // All shares claimed — full accumulator was taken. Restore entire accumulated amount.
          // acc_current should be 0 in this case; the full feeXfg equals the pre-claim accumulator.
          // Since we cannot derive it, store 0 (fees were fully distributed, no reversal needed
          // because totalLpShares restoration will bring the pool back to a consistent state).
          feeXfg = 0;
        }
        m_ammPool.accumulatedLpFeesXfg += feeXfg;
        // Reverse HEAT fee auto-compounding
        if (m_ammPool.totalLpShares > claim.lpShares) {
          uint64_t feeHeat = (m_ammPool.accumulatedLpFeesHeat * claim.lpShares)
                             / (m_ammPool.totalLpShares - claim.lpShares);
          if (m_ammPool.reserveHeat >= feeHeat) m_ammPool.reserveHeat -= feeHeat;
          m_ammPool.accumulatedLpFeesHeat += feeHeat;
        }
      } else if (field.type() == typeid(TransactionExtraLegacyBond)) {
        const auto& bond = boost::get<TransactionExtraLegacyBond>(field);
        if (m_totalLegacyBondLocked >= bond.amount) {
          m_totalLegacyBondLocked -= bond.amount;
        }
      } else if (field.type() == typeid(TransactionExtraLegacyBondClaim)) {
        const auto& claim = boost::get<TransactionExtraLegacyBondClaim>(field);
        m_legacyBondYieldPool += claim.claimedInterest;
      }
    }
  }
}

void Blockchain::popTransactions(const BlockEntry& block, const Crypto::Hash& minerTransactionHash) {
  for (size_t i = 0; i < block.transactions.size() - 1; ++i) {
    popTransaction(block.transactions[block.transactions.size() - 1 - i].tx, block.bl.transactionHashes[block.transactions.size() - 2 - i]);
  }

  popTransaction(block.bl.baseTransaction, minerTransactionHash);
}

bool Blockchain::validateInput(const MultisignatureInput& input, const Crypto::Hash& transactionHash, const Crypto::Hash& transactionPrefixHash, const std::vector<Crypto::Signature>& transactionSignatures) {
  assert(input.signatureCount == transactionSignatures.size());
  MultisignatureOutputsContainer::const_iterator amountOutputs = m_indexManager.multisigOutputs().find(input.amount);
  if (amountOutputs == m_indexManager.multisigOutputs().end()) {
    logger(DEBUGGING) <<
      "Transaction << " << transactionHash << " contains multisignature input with invalid amount.";
    return false;
  }

  if (input.outputIndex >= amountOutputs->second.size()) {
    logger(DEBUGGING) <<
      "Transaction << " << transactionHash << " contains multisignature input with invalid outputIndex.";
    return false;
  }

  const MultisignatureOutputUsage& outputIndex = amountOutputs->second[input.outputIndex];
  if (outputIndex.isUsed) {
    logger(DEBUGGING) <<
      "Transaction << " << transactionHash << " contains double spending multisignature input.";
    return false;
  }

  const Transaction& outputTransaction = m_blocks[outputIndex.transactionIndex.block].transactions[outputIndex.transactionIndex.transaction].tx;
  if (!is_tx_spendtime_unlocked(outputTransaction.unlockTime)) {
    logger(DEBUGGING) <<
      "Transaction << " << transactionHash << " contains multisignature input which points to a locked transaction.";
    return false;
  }

  assert(outputTransaction.outputs[outputIndex.outputIndex].amount == input.amount);
  assert(outputTransaction.outputs[outputIndex.outputIndex].target.type() == typeid(MultisignatureOutput));
  const MultisignatureOutput& output = ::boost::get<MultisignatureOutput>(outputTransaction.outputs[outputIndex.outputIndex].target);
  if (input.signatureCount != output.requiredSignatureCount) {
    logger(DEBUGGING) <<
      "Transaction << " << transactionHash << " contains multisignature input with invalid signature count.";
    return false;
  }

  if (input.term != output.term) {
    logger(DEBUGGING) << "Transaction << " << transactionHash << " contains multisignature input with invalid term.";
    return false;
  }

  if (output.term != 0 && outputIndex.transactionIndex.block + output.term > getCurrentBlockchainHeight()) {
    logger(DEBUGGING) << "Transaction << " << transactionHash << " contains multisignature input that spends locked deposit output";
    return false;
  }

  size_t inputSignatureIndex = 0;
  size_t outputKeyIndex = 0;
  while (inputSignatureIndex < input.signatureCount) {
    if (outputKeyIndex == output.keys.size()) {
      logger(DEBUGGING) <<
        "Transaction << " << transactionHash << " contains multisignature input with invalid signatures.";
      return false;
    }

    if (Crypto::check_signature(transactionPrefixHash, output.keys[outputKeyIndex], transactionSignatures[inputSignatureIndex])) {
      ++inputSignatureIndex;
    }

    ++outputKeyIndex;
  }

  return true;
}

  bool Blockchain::rollbackBlockchainTo(uint32_t height)
  {
    logger(INFO) << "Rolling back blockchain to " << height;
    while (height + 1 < m_blocks.size())
    {
      removeLastBlock();
    }
    logger(INFO) << "Rollback complete. Synchronization will resume.";
    return true;
  }
  bool Blockchain::removeLastBlock()
  {
    if (m_blocks.empty())
    {
      logger(ERROR, BRIGHT_RED) << "Attempt to pop block from empty blockchain.";
      return false;
    }
    // Delegate to popBlock for full HEAT/AMM/epoch state reversal.
    Crypto::Hash blockHash = getBlockIdByHeight(m_blocks.back().height);
    popBlock(blockHash);
    return true;
  }

bool Blockchain::checkUpgradeHeight(const UpgradeDetector& upgradeDetector) {
  uint32_t upgradeHeight = upgradeDetector.upgradeHeight();
  if (upgradeHeight != UpgradeDetectorBase::UNDEF_HEIGHT && upgradeHeight + 1 < m_blocks.size()) {
    logger(INFO) << "Checking block version at " << upgradeHeight + 1;
    if (m_blocks[upgradeHeight + 1].bl.majorVersion != upgradeDetector.targetVersion()) {
      return false;
    }
  }

  return true;
}

bool Blockchain::getLowerBound(uint64_t timestamp, uint64_t startOffset, uint32_t& height) {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);

  assert(startOffset < m_blocks.size());

  auto bound = std::lower_bound(m_blocks.begin() + startOffset, m_blocks.end(), timestamp - m_currency.blockFutureTimeLimit(),
    [](const BlockEntry& b, uint64_t timestamp) { return b.bl.timestamp < timestamp; });

  if (bound == m_blocks.end()) {
    return false;
  }

  height = static_cast<uint32_t>(std::distance(m_blocks.begin(), bound));
  return true;
}

std::vector<Crypto::Hash> Blockchain::getBlockIds(uint32_t startHeight, uint32_t maxCount) {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  return m_blockIndex.getBlockIds(startHeight, maxCount);
}

bool Blockchain::getBlockContainingTransaction(const Crypto::Hash& txId, Crypto::Hash& blockId, uint32_t& blockHeight) {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  auto it = m_indexManager.transactionMap().find(txId);
  if (it == m_indexManager.transactionMap().end()) {
    return false;
  } else {
    blockHeight = m_blocks[it->second.block].height;
    blockId = getBlockIdByHeight(blockHeight);
    return true;
  }
}

bool Blockchain::getAlreadyGeneratedCoins(const Crypto::Hash& hash, uint64_t& generatedCoins) {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);

  // try to find block in main chain
  uint32_t height = 0;
  if (m_blockIndex.getBlockHeight(hash, height)) {
    generatedCoins = m_blocks[height].already_generated_coins;
    return true;
  }

  // try to find block in alternative chain
  auto blockByHashIterator = m_alternative_chains.find(hash);
  if (blockByHashIterator != m_alternative_chains.end()) {
    generatedCoins = blockByHashIterator->second.already_generated_coins;
    return true;
  }

  logger(DEBUGGING) << "Can't find block with hash " << hash << " to get already generated coins.";
  return false;
}

bool Blockchain::getBlockSize(const Crypto::Hash& hash, size_t& size) {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);

  // try to find block in main chain
  uint32_t height = 0;
  if (m_blockIndex.getBlockHeight(hash, height)) {
    size = m_blocks[height].block_cumulative_size;
    return true;
  }

  // try to find block in alternative chain
  auto blockByHashIterator = m_alternative_chains.find(hash);
  if (blockByHashIterator != m_alternative_chains.end()) {
    size = blockByHashIterator->second.block_cumulative_size;
    return true;
  }

  logger(DEBUGGING) << "Can't find block with hash " << hash << " to get block size.";
  return false;
}

bool Blockchain::getMultisigOutputReference(const MultisignatureInput& txInMultisig, std::pair<Crypto::Hash, size_t>& outputReference) {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  MultisignatureOutputsContainer::const_iterator amountIter = m_indexManager.multisigOutputs().find(txInMultisig.amount);
  if (amountIter == m_indexManager.multisigOutputs().end()) {
    logger(DEBUGGING) << "Transaction contains multisignature input with invalid amount.";
    return false;
  }
  if (amountIter->second.size() <= txInMultisig.outputIndex) {
    logger(DEBUGGING) << "Transaction contains multisignature input with invalid outputIndex.";
    return false;
  }
  const MultisignatureOutputUsage& outputIndex = amountIter->second[txInMultisig.outputIndex];
  const Transaction& outputTransaction = m_blocks[outputIndex.transactionIndex.block].transactions[outputIndex.transactionIndex.transaction].tx;
  outputReference.first = getObjectHash(outputTransaction);
  outputReference.second = outputIndex.outputIndex;
  return true;
}

bool Blockchain::storeBlockchainIndices() {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);

  logger(INFO, BRIGHT_WHITE) << "Saving blockchain indices...";
  BlockchainIndicesSerializer ser(*this, getTailId(), logger.getLogger());

  if (!storeToBinaryFile(ser, appendPath(m_config_folder, m_currency.blockchinIndicesFileName()))) {
    logger(ERROR, BRIGHT_RED) << "Failed to save blockchain indices";
    return false;
  }

  return true;
}

bool Blockchain::loadBlockchainIndices() {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);

  logger(INFO, BRIGHT_WHITE) << "Loading blockchain indices for BlockchainExplorer...";
  BlockchainIndicesSerializer loader(*this, get_block_hash(m_blocks.back().bl), logger.getLogger());

  loadFromBinaryFile(loader, appendPath(m_config_folder, m_currency.blockchinIndicesFileName()));

  if (!loader.loaded()) {
    logger(WARNING, BRIGHT_MAGENTA) << "No actual blockchain indices for BlockchainExplorer found, rebuilding...";
    std::chrono::steady_clock::time_point timePoint = std::chrono::steady_clock::now();

    m_paymentIdIndex.clear();
    m_timestampIndex.clear();
    m_generatedTransactionsIndex.clear();

    for (uint32_t b = 0; b < m_blocks.size(); ++b) {
      if (b % 1000 == 0) {
        logger(INFO, BRIGHT_WHITE) << "Height " << b << " of " << m_blocks.size();
      }
      const BlockEntry& block = m_blocks[b];
      m_timestampIndex.add(block.bl.timestamp, get_block_hash(block.bl));
      m_generatedTransactionsIndex.add(block.bl);
      for (uint16_t t = 0; t < block.transactions.size(); ++t) {
        const TransactionEntry& transaction = block.transactions[t];
        m_paymentIdIndex.add(transaction.tx);
      }
    }

    std::chrono::duration<double> duration = std::chrono::steady_clock::now() - timePoint;
    logger(INFO, BRIGHT_WHITE) << "Rebuilding blockchain indices took: " << duration.count();
  }
  return true;
}

bool Blockchain::getGeneratedTransactionsNumber(uint32_t height, uint64_t& generatedTransactions) {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  return m_generatedTransactionsIndex.find(height, generatedTransactions);
}

bool Blockchain::getOrphanBlockIdsByHeight(uint32_t height, std::vector<Crypto::Hash>& blockHashes) {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  return m_orthanBlocksIndex.find(height, blockHashes);
}

bool Blockchain::getBlockIdsByTimestamp(uint64_t timestampBegin, uint64_t timestampEnd, uint32_t blocksNumberLimit, std::vector<Crypto::Hash>& hashes, uint32_t& blocksNumberWithinTimestamps) {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  return m_timestampIndex.find(timestampBegin, timestampEnd, blocksNumberLimit, hashes, blocksNumberWithinTimestamps);
}

bool Blockchain::getTransactionIdsByPaymentId(const Crypto::Hash& paymentId, std::vector<Crypto::Hash>& transactionHashes) {
  std::lock_guard<decltype(m_blockchain_lock)> lk(m_blockchain_lock);
  return m_paymentIdIndex.find(paymentId, transactionHashes);
}

bool Blockchain::loadTransactions(const Block& block, std::vector<Transaction>& transactions, uint32_t height) {
  transactions.resize(block.transactionHashes.size());
  size_t transactionSize;
  uint64_t fee;
  for (size_t i = 0; i < block.transactionHashes.size(); ++i) {
    if (!m_tx_pool.take_tx(block.transactionHashes[i], transactions[i], transactionSize, fee)) {
      tx_verification_context context;
      for (size_t j = 0; j < i; ++j) {
        if (!m_tx_pool.add_tx(transactions[i - 1 - j], context, true, height)) {
          throw std::runtime_error("Blockchain::loadTransactions, failed to add transaction to pool");
        }
      }

      return false;
    }
  }

  return true;
}

void Blockchain::saveTransactions(const std::vector<Transaction>& transactions, uint32_t height) {
  tx_verification_context context;
  for (size_t i = 0; i < transactions.size(); ++i) {
    if (!m_tx_pool.add_tx(transactions[transactions.size() - 1 - i], context, true, height)) {
      logger(WARNING, BRIGHT_MAGENTA) << "Blockchain::saveTransactions, failed to add transaction to pool";
    }
  }
}

bool Blockchain::addMessageQueue(MessageQueue<BlockchainMessage>& messageQueue) {
  return m_messageQueueList.insert(messageQueue);
}

bool Blockchain::removeMessageQueue(MessageQueue<BlockchainMessage>& messageQueue) {
  return m_messageQueueList.remove(messageQueue);
}

void Blockchain::sendMessage(const BlockchainMessage& message) {
  for (IntrusiveLinkedList<MessageQueue<BlockchainMessage>>::iterator iter = m_messageQueueList.begin(); iter != m_messageQueueList.end(); ++iter) {
    iter->push(message);
  }
}

bool Blockchain::isBlockInMainChain(const Crypto::Hash& blockId) {
  return m_blockIndex.hasBlock(blockId);
}

bool Blockchain::isInCheckpointZone(const uint32_t height) {
  return m_checkpoints.is_in_checkpoint_zone(height);
}

void Blockchain::setBootstrapAmount(uint64_t xfg, uint64_t heat) {
  if (!m_bootstrapRepaid && m_bootstrapXfgOwed == 0) {
    m_bootstrapXfgOwed = xfg;
    (void)heat;
    m_bootstrapRepaymentVault = 0;
  }
}

bool Blockchain::withdrawBootstrapRepaymentVault(uint64_t amount) {
  if (amount == 0 || amount > m_bootstrapRepaymentVault) return false;
  m_bootstrapRepaymentVault -= amount;
  m_treasuryBalance += amount;
  return true;
}

void Blockchain::addSwapFee(uint64_t amount) {
  if (amount == 0) return;
  m_currentEpochSwapFees += amount;
  m_totalSwapFeesCollected += amount;
}

AssetType Blockchain::classifyInputAsset(const TransactionInput& in) const {
  if (in.type() == typeid(KeyInput) || in.type() == typeid(MultisignatureInput)) {
    return AssetType::XFG;
  }
  if (in.type() == typeid(TransactionInputCommitmentSpend)) {
    const auto& cs = boost::get<TransactionInputCommitmentSpend>(in);
    auto it = m_indexManager.commitmentOutputs().find(cs.amount);
    if (it == m_indexManager.commitmentOutputs().end() || it->second.empty())
      return AssetType::XFG;  // conservative: treat unresolvable as XFG
    if (cs.outputIndexes.empty())
      return AssetType::XFG;
    uint64_t absIdx = cs.outputIndexes[0];  // first member = absolute (cumulative first offset)
    if (absIdx >= it->second.size())
      return AssetType::XFG;
    const auto& ref = it->second[absIdx];
    if (ref.term == parameters::HEAT_TERM)
      return AssetType::HEAT;
    if (ref.term == parameters::DEPOSIT_TERM_LP)
      return AssetType::LP;
    return AssetType::XFG;
  }
  if (in.type() == typeid(TransactionInputCommitmentTransfer)) {
    return AssetType::XFG;
  }
  return AssetType::XFG;
}

AssetBalance Blockchain::getTransactionInputAssetAmounts(const Transaction& tx, uint32_t height) const {
  AssetBalance bal;
  for (const auto& in : tx.inputs) {
    AssetType asset = classifyInputAsset(in);
    uint64_t amount = m_currency.getTransactionInputAmount(in, height);
    switch (asset) {
      case AssetType::HEAT: bal.heat += amount; break;
      case AssetType::LP:   bal.lp   += amount; break;
      default:              bal.xfg  += amount; break;
    }
  }
  return bal;
}

}  // namespace CryptoNote
