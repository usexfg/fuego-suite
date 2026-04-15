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

#include "PoolDatabase.h"

#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <cstring>
#include <algorithm>
#include <cerrno>

namespace XfgSwap {

static std::string pubkeyToHex(const Crypto::PublicKey& pub) {
  static const char hex[] = "0123456789abcdef";
  std::string out;
  out.reserve(64);
  const uint8_t* data = reinterpret_cast<const uint8_t*>(&pub);
  for (int i = 0; i < 32; ++i) {
    out += hex[(data[i] >> 4) & 0xf];
    out += hex[data[i] & 0xf];
  }
  return out;
}

PoolDatabase::PoolDatabase(const std::string& dataDir)
  : m_dataDir(dataDir)
  , m_poolsDir(dataDir + "/pools") {
  ensureDirectory(m_dataDir);
  ensureDirectory(m_poolsDir);
}

bool PoolDatabase::ensureDirectory(const std::string& dir) {
  mkdir(dir.c_str(), 0700);
  return true;
}

std::string PoolDatabase::poolFilePath(const PoolId& poolId) const {
  return m_poolsDir + "/" + poolIdToHex(poolId) + ".json";
}

std::string PoolDatabase::lpShareFilePath(const PoolId& poolId, const Crypto::PublicKey& owner) const {
  std::string poolDir = m_poolsDir + "/" + poolIdToHex(poolId);
  return poolDir + "/lp/" + pubkeyToHex(owner) + ".json";
}

std::string PoolDatabase::checkpointDir(const PoolId& poolId) const {
  return m_poolsDir + "/" + poolIdToHex(poolId) + "/checkpoints";
}

bool PoolDatabase::savePool(const PoolState& state) {
  try {
    std::string path = poolFilePath(state.id);

    // Ensure pool directory exists
    std::string poolDir = m_poolsDir + "/" + poolIdToHex(state.id);
    ensureDirectory(poolDir);
    ensureDirectory(poolDir + "/lp");
    ensureDirectory(poolDir + "/checkpoints");

    // Simple JSON serialization
    std::ostringstream json;
    json << "{";
    json << "\"reserveA\":" << state.reserveA << ",";
    json << "\"reserveB\":" << state.reserveB << ",";
    json << "\"totalLPShares\":" << state.totalLPShares << ",";
    json << "\"feeAccumulatorA\":" << state.feeAccumulatorA << ",";
    json << "\"feeAccumulatorB\":" << state.feeAccumulatorB << ",";
    json << "\"blockHeight\":" << state.blockHeight << ",";
    json << "\"timestamp\":" << state.timestamp << ",";
    json << "\"totalVolumeA\":" << state.totalVolumeA << ",";
    json << "\"totalVolumeB\":" << state.totalVolumeB << ",";
    json << "\"totalFeesA\":" << state.totalFeesA << ",";
    json << "\"totalFeesB\":" << state.totalFeesB << ",";
    json << "\"feeBps\":" << state.id.feeBps;
    json << "}";

    std::string tmpPath = path + ".tmp";
    std::ofstream ofs(tmpPath, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
      return false;
    }
    ofs << json.str();
    ofs.close();

    if (ofs.fail()) {
      return false;
    }

    if (std::rename(tmpPath.c_str(), path.c_str()) != 0) {
      std::remove(tmpPath.c_str());
      return false;
    }

    return true;
  } catch (const std::exception&) {
    return false;
  }
}

bool PoolDatabase::loadPool(const PoolId& poolId, PoolState& state) {
  try {
    std::string path = poolFilePath(poolId);
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) {
      return false;
    }

    std::ostringstream ss;
    ss << ifs.rdbuf();
    std::string json = ss.str();

    if (json.empty()) {
      return false;
    }

    // Simple JSON parsing (production should use a proper JSON library)
    auto parseUint64 = [&json](const std::string& key) -> uint64_t {
      std::string search = "\"" + key + "\":";
      size_t pos = json.find(search);
      if (pos == std::string::npos) return 0;
      pos += search.size();
      return std::stoull(json.substr(pos));
    };

    state.id = poolId;
    state.reserveA = parseUint64("reserveA");
    state.reserveB = parseUint64("reserveB");
    state.totalLPShares = parseUint64("totalLPShares");
    state.feeAccumulatorA = parseUint64("feeAccumulatorA");
    state.feeAccumulatorB = parseUint64("feeAccumulatorB");
    state.blockHeight = static_cast<uint32_t>(parseUint64("blockHeight"));
    state.timestamp = parseUint64("timestamp");
    state.totalVolumeA = parseUint64("totalVolumeA");
    state.totalVolumeB = parseUint64("totalVolumeB");
    state.totalFeesA = parseUint64("totalFeesA");
    state.totalFeesB = parseUint64("totalFeesB");
    state.id.feeBps = static_cast<uint32_t>(parseUint64("feeBps"));

    return true;
  } catch (const std::exception&) {
    return false;
  }
}

std::vector<PoolId> PoolDatabase::listPools() {
  std::vector<PoolId> pools;

  DIR* dir = opendir(m_poolsDir.c_str());
  if (!dir) {
    return pools;
  }

  struct dirent* entry;
  while ((entry = readdir(dir)) != nullptr) {
    std::string name = entry->d_name;
    if (name.size() > 5 && name.substr(name.size() - 5) == ".json") {
      std::string hexId = name.substr(0, name.size() - 5);
      pools.push_back(poolIdFromHex(hexId));
    }
  }

  closedir(dir);
  return pools;
}

bool PoolDatabase::deletePool(const PoolId& poolId) {
  std::string path = poolFilePath(poolId);
  return std::remove(path.c_str()) == 0;
}

bool PoolDatabase::saveLPShare(const PoolId& poolId, const LPShare& share) {
  try {
    std::string path = lpShareFilePath(poolId, share.owner);
    std::string dir = path.substr(0, path.find_last_of('/'));
    ensureDirectory(dir);

    std::ostringstream json;
    json << "{";
    json << "\"shareAmount\":" << share.shareAmount << ",";
    json << "\"feeClaimedA\":" << share.feeClaimedA << ",";
    json << "\"feeClaimedB\":" << share.feeClaimedB << ",";
    json << "\"depositHeight\":" << share.depositHeight << ",";
    json << "\"depositTimestamp\":" << share.depositTimestamp;
    json << "}";

    std::string tmpPath = path + ".tmp";
    std::ofstream ofs(tmpPath, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
      return false;
    }
    ofs << json.str();
    ofs.close();

    if (ofs.fail()) {
      return false;
    }

    std::rename(tmpPath.c_str(), path.c_str());
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

bool PoolDatabase::loadLPShare(const PoolId& poolId, const Crypto::PublicKey& owner, LPShare& share) {
  try {
    std::string path = lpShareFilePath(poolId, owner);
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) {
      return false;
    }

    std::ostringstream ss;
    ss << ifs.rdbuf();
    std::string json = ss.str();

    if (json.empty()) {
      return false;
    }

    auto parseUint64 = [&json](const std::string& key) -> uint64_t {
      std::string search = "\"" + key + "\":";
      size_t pos = json.find(search);
      if (pos == std::string::npos) return 0;
      pos += search.size();
      return std::stoull(json.substr(pos));
    };

    share.owner = owner;
    share.poolId = poolId;
    share.shareAmount = parseUint64("shareAmount");
    share.feeClaimedA = parseUint64("feeClaimedA");
    share.feeClaimedB = parseUint64("feeClaimedB");
    share.depositHeight = static_cast<uint32_t>(parseUint64("depositHeight"));
    share.depositTimestamp = parseUint64("depositTimestamp");

    return true;
  } catch (const std::exception&) {
    return false;
  }
}

std::vector<LPShare> PoolDatabase::listLPShares(const PoolId& poolId) {
  std::vector<LPShare> shares;

  std::string lpDir = m_poolsDir + "/" + poolIdToHex(poolId) + "/lp";
  DIR* dir = opendir(lpDir.c_str());
  if (!dir) {
    return shares;
  }

  struct dirent* entry;
  while ((entry = readdir(dir)) != nullptr) {
    std::string name = entry->d_name;
    if (name.size() > 5 && name.substr(name.size() - 5) == ".json") {
      std::string hexPub = name.substr(0, name.size() - 5);
      Crypto::PublicKey owner = {};
      // Parse hex pubkey (simplified)
      LPShare share = {};
      if (loadLPShare(poolId, owner, share)) {
        shares.push_back(share);
      }
    }
  }

  closedir(dir);
  return shares;
}

bool PoolDatabase::saveCheckpoint(const PoolId& poolId, const PoolCheckpoint& checkpoint) {
  try {
    std::string dir = checkpointDir(poolId);
    ensureDirectory(dir);

    std::string path = dir + "/" + std::to_string(checkpoint.blockHeight) + ".json";

    std::ostringstream json;
    json << "{";
    json << "\"totalLPShares\":" << checkpoint.totalLPShares << ",";
    json << "\"blockHeight\":" << checkpoint.blockHeight << ",";
    json << "\"timestamp\":" << checkpoint.timestamp << ",";
    json << "\"eventCount\":" << checkpoint.eventCount;
    json << "}";

    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
      return false;
    }
    ofs << json.str();
    ofs.close();

    return !ofs.fail();
  } catch (const std::exception&) {
    return false;
  }
}

bool PoolDatabase::loadLatestCheckpoint(const PoolId& poolId, PoolCheckpoint& checkpoint) {
  try {
    std::string dir = checkpointDir(poolId);
    DIR* d = opendir(dir.c_str());
    if (!d) {
      return false;
    }

    uint32_t maxBlock = 0;
    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr) {
      std::string name = entry->d_name;
      if (name.size() > 5 && name.substr(name.size() - 5) == ".json") {
        uint32_t block = std::stoul(name.substr(0, name.size() - 5));
        if (block > maxBlock) {
          maxBlock = block;
        }
      }
    }
    closedir(d);

    if (maxBlock == 0) {
      return false;
    }

    std::string path = dir + "/" + std::to_string(maxBlock) + ".json";
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) {
      return false;
    }

    std::ostringstream ss;
    ss << ifs.rdbuf();
    std::string json = ss.str();

    auto parseUint64 = [&json](const std::string& key) -> uint64_t {
      std::string search = "\"" + key + "\":";
      size_t pos = json.find(search);
      if (pos == std::string::npos) return 0;
      pos += search.size();
      return std::stoull(json.substr(pos));
    };

    checkpoint.totalLPShares = parseUint64("totalLPShares");
    checkpoint.blockHeight = static_cast<uint32_t>(parseUint64("blockHeight"));
    checkpoint.timestamp = parseUint64("timestamp");
    checkpoint.eventCount = static_cast<uint32_t>(parseUint64("eventCount"));

    return true;
  } catch (const std::exception&) {
    return false;
  }
}

const std::string& PoolDatabase::dataDir() const {
  return m_dataDir;
}

} // namespace XfgSwap
