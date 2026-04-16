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

#include <unordered_map>
#include <vector>
#include <map>
#include <set>
#include <mutex>
#include <cstdint>
#include <string>
#include <optional>
#include "../crypto/hash.h"
#include "../Serialization/ISerializer.h"
#include "AliasIndex.h"
#include "Currency.h"

namespace CryptoNote {

struct CommitmentEntry {
  Crypto::Hash commitment;
  Crypto::Hash txHash;
  uint32_t blockHeight = 0;
  uint64_t amount = 0;
  uint32_t term = 0;

  enum class Type : uint8_t {
    HEAT = 0,
    COLD = 1
  };

  Type type = Type::HEAT;

  uint32_t targetChainId = 0;

  bool isLegacyMigration = false;

  void serialize(ISerializer& s);
};

// Per-epoch activity report
struct EpochReport {
  uint64_t epochNumber = 0;
  uint64_t epochStartBlock = 0;
  uint64_t epochEndBlock = 0;
  uint64_t generatedAtBlock = 0;

  uint64_t totalFeesDistributed = 0;

  uint64_t swapFeesCollected = 0;
  uint64_t totalCdLockedAtStart = 0;
  uint64_t feeRateFixedPoint = 0;
  uint64_t activeEfierCount = 0;

  void serialize(ISerializer& s) {
    s(epochNumber, "epoch_number");
    s(epochStartBlock, "epoch_start_block");
    s(epochEndBlock, "epoch_end_block");
    s(generatedAtBlock, "generated_at_block");
    s(totalFeesDistributed, "total_fees_distributed");
    s(swapFeesCollected, "swap_fees_collected");
    s(totalCdLockedAtStart, "total_cd_locked_at_start");
    s(feeRateFixedPoint, "fee_rate_fixed_point");
    s(activeEfierCount, "active_efier_count");
  }
};

class CommitmentIndex {
public:
  CommitmentIndex(const CryptoNote::Currency& currency);
  ~CommitmentIndex();

  void addCommitment(const CommitmentEntry& entry);

  typedef uint32_t Height;

  Crypto::Hash computeMerkleRoot() const;
  std::vector<Crypto::Hash> getMerkleProof(const Crypto::Hash& commitment) const;
  size_t getLeafIndex(const Crypto::Hash& commitment) const;
  std::vector<Crypto::Hash> getAllLeaves() const;
  Height highestBlock() const;
  size_t rollbackToHeight(Height h);

  void clear();
  CommitmentEntry getByCommitment(const Crypto::Hash& commitment) const;
  bool hasCommitment(const Crypto::Hash& commitment) const;
  size_t size() const;
  size_t heatCount() const;
  size_t coldCount() const;

  // Generate an epoch report covering [startBlock, endBlock]
  // Called by Blockchain.cpp at each EPOCH_DURATION_BLOCKS boundary
  EpochReport generateEpochReport(uint64_t epochNumber, uint64_t startBlock, uint64_t endBlock,
                                  uint64_t generatedAtBlock) const;

  void recordEpochFeeRate(uint64_t epochNumber, uint64_t feeRate,
                           uint64_t feesCollected, uint64_t totalLocked);
  uint64_t getEpochFeeRate(uint64_t epochNumber) const;
  uint64_t getEpochCount() const;
  // Remove the most-recently recorded epoch fee rate (used by popBlock rollback).
  void popEpochFeeRate();

  void storeEpochReport(const EpochReport& report);
  std::optional<EpochReport> getEpochReport(uint64_t epochNumber) const;
  std::optional<EpochReport> getLatestEpochReport() const;

  void serialize(ISerializer& s);

private:
  mutable std::mutex m_mutex;

  std::vector<uint64_t> m_epochFeeRates;

  mutable Crypto::Hash m_current_merkle_root;
  mutable bool m_merkleDirty = true;
  uint64_t m_current_block_height = 0;

  std::map<uint64_t, uint64_t> m_blockBankingFees;

  AliasIndex* m_aliasIndex = nullptr;

  std::map<std::string, CommitmentEntry> m_commitments;
  std::vector<Crypto::Hash> m_merkle_leaves;
  std::map<uint32_t, std::vector<std::string>> m_heightIndex;
  size_t m_heat_count = 0;
  size_t m_cold_count = 0;

  const CryptoNote::Currency& m_currency;

  std::vector<EpochReport> m_epochReports;

  std::map<std::string, std::string> m_txHashToCommitHash;

  Crypto::Hash computeMerkleRootInternal() const;
};

}  // namespace CryptoNote
