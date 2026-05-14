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
//
// File-based pool persistence.
// Each pool is stored as a JSON file in <dataDir>/pools/<pool_id>.json
// LP shares are stored in <dataDir>/pools/<pool_id>/lp/<owner_pubkey>.json

#pragma once

#include "PoolTypes.h"
#include "PoolOrganizer.h"
#include <string>
#include <vector>

namespace XfgSwap {

class PoolDatabase {
public:
  explicit PoolDatabase(const std::string& dataDir);

  // Save pool state to disk. Creates or overwrites the file.
  bool savePool(const PoolState& state);

  // Load pool state from disk by pool ID.
  bool loadPool(const PoolId& poolId, PoolState& state);

  // List all pool IDs stored on disk.
  std::vector<PoolId> listPools();

  // Delete a pool file from disk.
  bool deletePool(const PoolId& poolId);

  // Save LP share state for an owner in a pool.
  bool saveLPShare(const PoolId& poolId, const LPShare& share);

  // Load LP share state for an owner in a pool.
  bool loadLPShare(const PoolId& poolId, const Crypto::PublicKey& owner, LPShare& share);

  // List all LP shares for a pool.
  std::vector<LPShare> listLPShares(const PoolId& poolId);

  // Save checkpoint history.
  bool saveCheckpoint(const PoolId& poolId, const PoolCheckpoint& checkpoint);

  // Load latest checkpoint for a pool.
  bool loadLatestCheckpoint(const PoolId& poolId, PoolCheckpoint& checkpoint);

  // Get the data directory path.
  const std::string& dataDir() const;

private:
  std::string poolFilePath(const PoolId& poolId) const;
  std::string lpShareFilePath(const PoolId& poolId, const Crypto::PublicKey& owner) const;
  std::string checkpointDir(const PoolId& poolId) const;
  bool ensureDirectory(const std::string& dir);

  std::string m_dataDir;
  std::string m_poolsDir;
};

} // namespace XfgSwap
