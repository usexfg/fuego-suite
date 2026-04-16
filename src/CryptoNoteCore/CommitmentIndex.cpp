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

#include <set>
#include <algorithm>
#include <cstring>
#include <ctime>

#include "CommitmentIndex.h"
#include "TransactionExtra.h"
#include "../Serialization/ISerializer.h"
#include "../Serialization/SerializationOverloads.h"
#include "../Common/StringTools.h"
#include "../crypto/hash.h"
#include "../CryptoNoteConfig.h"

namespace CryptoNote {

void CommitmentEntry::serialize(ISerializer& s) {
  s.binary(&commitment, sizeof(commitment), "commitment");
  s.binary(&txHash, sizeof(txHash), "tx_hash");
  s(blockHeight, "block_height");
  s(amount, "amount");
  s(term, "term");
  s(targetChainId, "target_chain_id");
  s(isLegacyMigration, "is_legacy_migration");
  uint8_t typeVal = static_cast<uint8_t>(type);
  s(typeVal, "type");
  if (s.type() == ISerializer::INPUT) type = static_cast<Type>(typeVal);
}

CommitmentIndex::CommitmentIndex(const CryptoNote::Currency& currency) : m_currency(currency) {
}

CommitmentIndex::~CommitmentIndex() {
}

void CommitmentIndex::addCommitment(const CommitmentEntry& entry) {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::string commitHex = Common::podToHex(entry.commitment);

  if (m_commitments.find(commitHex) != m_commitments.end()) {
    return;
  }

  m_commitments[commitHex] = entry;
  m_merkle_leaves.push_back(entry.commitment);
  m_heightIndex[entry.blockHeight].push_back(commitHex);

  std::string txHashHex = Common::podToHex(entry.txHash);
  m_txHashToCommitHash[txHashHex] = commitHex;

  switch (entry.type) {
    case CommitmentEntry::Type::HEAT:
      m_heat_count++;
      break;
    case CommitmentEntry::Type::COLD:
      m_cold_count++;
      break;
  }

  if (entry.blockHeight > m_current_block_height) {
    m_current_block_height = entry.blockHeight;
  }

  m_merkleDirty = true;
}

// ============================================================================
// COMMITMENT STORAGE AND MERKLE TREE
// ============================================================================

Crypto::Hash CommitmentIndex::computeMerkleRoot() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_merkleDirty) {
    m_current_merkle_root = computeMerkleRootInternal();
    m_merkleDirty = false;
  }
  return m_current_merkle_root;
}

std::vector<Crypto::Hash> CommitmentIndex::getAllLeaves() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_merkle_leaves;
}

Crypto::Hash CommitmentIndex::computeMerkleRootInternal() const {
  if (m_merkle_leaves.empty()) {
    return Crypto::Hash();
  }

  std::vector<Crypto::Hash> level = m_merkle_leaves;

  while (level.size() > 1) {
    std::vector<Crypto::Hash> next_level;

    for (size_t i = 0; i < level.size(); i += 2) {
      if (i + 1 < level.size()) {
        uint8_t combined[64];
        memcpy(combined, level[i].data, 32);
        memcpy(combined + 32, level[i + 1].data, 32);
        Crypto::Hash parent;
        Crypto::cn_fast_hash(combined, 64, parent);
        next_level.push_back(parent);
      } else {
        uint8_t combined[64];
        memcpy(combined, level[i].data, 32);
        memcpy(combined + 32, level[i].data, 32);
        Crypto::Hash parent;
        Crypto::cn_fast_hash(combined, 64, parent);
        next_level.push_back(parent);
      }
    }

    level = next_level;
  }

  return level[0];
}

std::vector<Crypto::Hash> CommitmentIndex::getMerkleProof(const Crypto::Hash& commitment) const {
  std::lock_guard<std::mutex> lock(m_mutex);

  if (m_merkle_leaves.empty()) {
    return {};
  }

  size_t leaf_idx = SIZE_MAX;
  for (size_t i = 0; i < m_merkle_leaves.size(); ++i) {
    if (m_merkle_leaves[i] == commitment) {
      leaf_idx = i;
      break;
    }
  }

  if (leaf_idx == SIZE_MAX) {
    return {};
  }

  std::vector<Crypto::Hash> proof;
  std::vector<Crypto::Hash> level = m_merkle_leaves;
  size_t idx = leaf_idx;

  while (level.size() > 1) {
    size_t sibling_idx;
    if (idx % 2 == 0) {
      sibling_idx = (idx + 1 < level.size()) ? idx + 1 : idx;
    } else {
      sibling_idx = idx - 1;
    }
    proof.push_back(level[sibling_idx]);

    std::vector<Crypto::Hash> next_level;
    for (size_t i = 0; i < level.size(); i += 2) {
      uint8_t combined[64];
      memcpy(combined, level[i].data, 32);
      if (i + 1 < level.size()) {
        memcpy(combined + 32, level[i + 1].data, 32);
      } else {
        memcpy(combined + 32, level[i].data, 32);
      }
      Crypto::Hash parent;
      Crypto::cn_fast_hash(combined, 64, parent);
      next_level.push_back(parent);
    }

    idx = idx / 2;
    level = next_level;
  }

  return proof;
}

size_t CommitmentIndex::getLeafIndex(const Crypto::Hash& commitment) const {
  std::lock_guard<std::mutex> lock(m_mutex);

  for (size_t i = 0; i < m_merkle_leaves.size(); ++i) {
    if (m_merkle_leaves[i] == commitment) {
      return i;
    }
  }
  return SIZE_MAX;
}

CommitmentIndex::Height CommitmentIndex::highestBlock() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return static_cast<Height>(m_current_block_height);
}

size_t CommitmentIndex::rollbackToHeight(Height h) {
  std::lock_guard<std::mutex> lock(m_mutex);

  size_t removed = 0;

  auto height_it = m_heightIndex.upper_bound(h);
  while (height_it != m_heightIndex.end()) {
    for (const auto& commitHex : height_it->second) {
      auto it = m_commitments.find(commitHex);
      if (it != m_commitments.end()) {
        switch (it->second.type) {
          case CommitmentEntry::Type::HEAT: m_heat_count--; break;
          case CommitmentEntry::Type::COLD: m_cold_count--; break;
        }
        m_commitments.erase(it);
        removed++;
      }
    }
    height_it = m_heightIndex.erase(height_it);
  }

  m_merkle_leaves.clear();
  for (const auto& height_pair : m_heightIndex) {
    for (const auto& commitHex : height_pair.second) {
      auto it = m_commitments.find(commitHex);
      if (it != m_commitments.end()) {
        m_merkle_leaves.push_back(it->second.commitment);
      }
    }
  }

  if (!m_heightIndex.empty()) {
    m_current_block_height = m_heightIndex.rbegin()->first;
  } else {
    m_current_block_height = 0;
  }
  m_merkleDirty = true;

  return removed;
}

void CommitmentIndex::clear() {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_commitments.clear();
  m_merkle_leaves.clear();
  m_heightIndex.clear();
  m_heat_count = 0;
  m_cold_count = 0;
  m_blockBankingFees.clear();
  m_current_merkle_root = Crypto::Hash();
  m_merkleDirty = false;  // leaves are empty; root is already the zero hash
  m_current_block_height = 0;
}

CommitmentEntry CommitmentIndex::getByCommitment(const Crypto::Hash& commitment) const {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::string commitHex = Common::podToHex(commitment);
  auto it = m_commitments.find(commitHex);
  if (it != m_commitments.end()) {
    return it->second;
  }
  return CommitmentEntry();
}

bool CommitmentIndex::hasCommitment(const Crypto::Hash& commitment) const {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::string commitHex = Common::podToHex(commitment);
  return m_commitments.find(commitHex) != m_commitments.end();
}

size_t CommitmentIndex::size() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_commitments.size();
}

size_t CommitmentIndex::heatCount() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_heat_count;
}

size_t CommitmentIndex::coldCount() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_cold_count;
}

// ============================================================================
// EPOCH TRACKING
// ============================================================================

EpochReport CommitmentIndex::generateEpochReport(uint64_t epochNumber, uint64_t startBlock,
                                                  uint64_t endBlock, uint64_t generatedAtBlock) const {
  std::lock_guard<std::mutex> lock(m_mutex);

  EpochReport report;
  report.epochNumber = epochNumber;
  report.epochStartBlock = startBlock;
  report.epochEndBlock = endBlock;
  report.generatedAtBlock = generatedAtBlock;

  // TODO: totalFeesDistributed — not tracked in CommitmentIndex; caller (Blockchain.cpp) should
  // populate this field after the call using the epoch fee-distribution totals it computes.
  report.totalFeesDistributed = 0;

  // swapFeesCollected, totalCdLockedAtStart, feeRateFixedPoint are filled by the caller
  // (Blockchain.cpp) immediately after this call — leave them zero here.

  // Count active Elderfier aliases (aliasType == 0) via m_aliasIndex if available.
  if (m_aliasIndex != nullptr) {
    const auto allAliases = m_aliasIndex->getAllAliases();
    uint64_t efierCount = 0;
    for (const auto& a : allAliases) {
      if (a.aliasType == 0) {
        ++efierCount;
      }
    }
    report.activeEfierCount = efierCount;
  }

  return report;
}

void CommitmentIndex::recordEpochFeeRate(uint64_t epochNumber, uint64_t feeRate,
                                           uint64_t feesCollected, uint64_t totalLocked) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (epochNumber >= m_epochFeeRates.size()) {
    m_epochFeeRates.resize(epochNumber + 1, 0);
  }
  m_epochFeeRates[epochNumber] = feeRate;
}



uint64_t CommitmentIndex::getEpochFeeRate(uint64_t epochNumber) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (epochNumber >= m_epochFeeRates.size()) return 0;
  return m_epochFeeRates[epochNumber];
}

uint64_t CommitmentIndex::getEpochCount() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_epochFeeRates.size();
}

void CommitmentIndex::popEpochFeeRate() {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_epochFeeRates.empty()) {
    m_epochFeeRates.pop_back();
  }
}

void CommitmentIndex::storeEpochReport(const EpochReport& report) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_epochReports.push_back(report);
  if (m_epochReports.size() > 10) {
    m_epochReports.erase(m_epochReports.begin());
  }
}

std::optional<EpochReport> CommitmentIndex::getEpochReport(uint64_t epochNumber) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  for (auto it = m_epochReports.rbegin(); it != m_epochReports.rend(); ++it) {
    if (it->epochNumber == epochNumber) return *it;
  }
  return std::nullopt;
}

std::optional<EpochReport> CommitmentIndex::getLatestEpochReport() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_epochReports.empty()) return std::nullopt;
  return m_epochReports.back();
}

// ============================================================================
// SERIALIZATION
// ============================================================================

void CommitmentIndex::serialize(ISerializer& s) {
  s(m_commitments, "commitments");

  s(m_epochReports, "epoch_reports");

  s(m_epochFeeRates, "epoch_fee_rates");

  if (s.type() == ISerializer::INPUT) {
    m_merkle_leaves.clear();
    m_heightIndex.clear();
    m_txHashToCommitHash.clear();
    m_heat_count = 0;
    m_cold_count = 0;
    m_current_block_height = 0;

    for (const auto& kv : m_commitments) {
      const CommitmentEntry& entry = kv.second;
      m_merkle_leaves.push_back(entry.commitment);
      m_heightIndex[entry.blockHeight].push_back(kv.first);
      m_txHashToCommitHash[Common::podToHex(entry.txHash)] = kv.first;
      switch (entry.type) {
        case CommitmentEntry::Type::HEAT: m_heat_count++; break;
        case CommitmentEntry::Type::COLD: m_cold_count++; break;
      }
      if (entry.blockHeight > m_current_block_height)
        m_current_block_height = entry.blockHeight;
    }

    m_merkleDirty = true;
  }
}

}  // namespace CryptoNote
