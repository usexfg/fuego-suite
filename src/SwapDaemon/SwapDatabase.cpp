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

#include "SwapDatabase.h"

#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <cstring>
#include <algorithm>

namespace XfgSwap {

SwapDatabase::SwapDatabase(const std::string& dataDir)
  : m_dataDir(dataDir)
  , m_swapsDir(dataDir + "/swaps") {
  ensureDirectory();
}

bool SwapDatabase::ensureDirectory() {
  // Create data dir if it doesn't exist
  mkdir(m_dataDir.c_str(), 0700);
  // Create swaps subdir
  int ret = mkdir(m_swapsDir.c_str(), 0700);
  // ret == 0 means created, EEXIST means already exists -- both are fine
  return (ret == 0 || errno == EEXIST);
}

std::string SwapDatabase::swapFilePath(const std::string& swapId) const {
  return m_swapsDir + "/" + swapId + ".json";
}

bool SwapDatabase::saveSwapLocked(const SwapStateMachine& sm) {
  try {
    std::string json = sm.serialize();
    std::string path = swapFilePath(sm.params().swapId);

    // Write to a temp file first, then rename for atomicity
    std::string tmpPath = path + ".tmp";
    std::ofstream ofs(tmpPath, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
      return false;
    }
    ofs << json;
    ofs.close();

    if (ofs.fail()) {
      return false;
    }

    // Atomic rename
    if (std::rename(tmpPath.c_str(), path.c_str()) != 0) {
      // Fallback: try to remove tmp file
      std::remove(tmpPath.c_str());
      return false;
    }

    return true;
  } catch (const std::exception&) {
    return false;
  }
}

bool SwapDatabase::loadSwapLocked(const std::string& swapId, SwapStateMachine& sm) {
  try {
    std::string path = swapFilePath(swapId);
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

    sm = SwapStateMachine::deserialize(json);
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

bool SwapDatabase::saveSwap(const SwapStateMachine& sm) {
  std::lock_guard<std::mutex> lock(m_mutex);
  return saveSwapLocked(sm);
}

bool SwapDatabase::loadSwap(const std::string& swapId, SwapStateMachine& sm) {
  std::lock_guard<std::mutex> lock(m_mutex);
  return loadSwapLocked(swapId, sm);
}

bool SwapDatabase::updateSwap(const std::string& swapId,
                              const std::function<bool(SwapStateMachine&)>& fn) {
  std::lock_guard<std::mutex> lock(m_mutex);
  SwapStateMachine sm;
  if (!loadSwapLocked(swapId, sm)) return false;
  if (!fn(sm)) return false;
  return saveSwapLocked(sm);
}

std::vector<std::string> SwapDatabase::listSwaps() {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::vector<std::string> swapIds;

  DIR* dir = opendir(m_swapsDir.c_str());
  if (!dir) {
    return swapIds;
  }

  struct dirent* entry;
  while ((entry = readdir(dir)) != nullptr) {
    std::string name = entry->d_name;
    // Filter for .json files
    if (name.size() > 5 && name.substr(name.size() - 5) == ".json") {
      // Strip .json extension to get swap ID
      swapIds.push_back(name.substr(0, name.size() - 5));
    }
  }

  closedir(dir);

  std::sort(swapIds.begin(), swapIds.end());
  return swapIds;
}

bool SwapDatabase::deleteSwap(const std::string& swapId) {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::string path = swapFilePath(swapId);
  return std::remove(path.c_str()) == 0;
}

const std::string& SwapDatabase::dataDir() const {
  return m_dataDir;
}

} // namespace XfgSwap
