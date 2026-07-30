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
#ifdef _WIN32
#include <filesystem>
namespace fs = std::filesystem;
#else
#include <dirent.h>
#endif
#include <cstring>
#include <algorithm>

namespace XfgSwap {

SwapDatabase::SwapDatabase(const std::string& dataDir)
  : m_dataDir(dataDir)
  , m_swapsDir(dataDir + "/swaps")
  , m_archiveDir(dataDir + "/archive") {
  ensureDirectory();
}

bool SwapDatabase::ensureDirectory() {
#ifdef _WIN32
  std::error_code ec;
  fs::create_directories(m_dataDir, ec);
  fs::create_directories(m_swapsDir, ec);
  fs::create_directories(m_archiveDir, ec);
  return true;
#else
  mkdir(m_dataDir.c_str(), 0700);
  mkdir(m_swapsDir.c_str(), 0700);
  int ret = mkdir(m_archiveDir.c_str(), 0700);
  return (ret == 0 || errno == EEXIST);
#endif
}

std::string SwapDatabase::swapFilePath(const std::string& swapId) const {
  return m_swapsDir + "/" + swapId + ".json";
}

std::string SwapDatabase::archiveFilePath(const std::string& swapId) const {
  return m_archiveDir + "/" + swapId + ".json";
}

bool SwapDatabase::saveSwapLocked(const SwapStateMachine& sm) {
  try {
    auto& mutableSm = const_cast<SwapStateMachine&>(sm);
    if (!m_encKey.empty()) mutableSm.setEncryptionKey(m_encKey);
    std::string json = mutableSm.serialize();
    // Archive terminal swaps so the tick loop never loads them again.
    std::string path = sm.isTerminal()
        ? archiveFilePath(sm.params().swapId)
        : swapFilePath(sm.params().swapId);

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

#ifndef _WIN32
    chmod(tmpPath.c_str(), S_IRUSR | S_IWUSR);
#endif

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
    if (!m_encKey.empty()) {
      sm.setEncryptionKey(m_encKey);
      sm.decryptStoredSecret();
    }
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

#ifdef _WIN32
  if (!fs::exists(m_swapsDir)) return swapIds;
  for (const auto& entry : fs::directory_iterator(m_swapsDir)) {
    std::string name = entry.path().filename().string();
    if (name.size() > 5 && name.substr(name.size() - 5) == ".json") {
      swapIds.push_back(name.substr(0, name.size() - 5));
    }
  }
#else
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
#endif

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

void SwapDatabase::setEncryptionKey(const std::string& key) {
  m_encKey = key;
}

void SwapDatabase::migrateTerminalSwaps() {
  std::lock_guard<std::mutex> lock(m_mutex);
#ifdef _WIN32
  if (!fs::exists(m_swapsDir)) return;
  for (const auto& entry : fs::directory_iterator(m_swapsDir)) {
    std::string name = entry.path().filename().string();
    if (name.size() <= 5 || name.substr(name.size() - 5) != ".json") continue;
    std::string swapId = name.substr(0, name.size() - 5);
    SwapStateMachine sm;
    if (loadSwapLocked(swapId, sm) && sm.isTerminal()) {
      std::string src = swapFilePath(swapId);
      std::string dst = archiveFilePath(swapId);
      if (std::rename(src.c_str(), dst.c_str()) != 0) {
        // Fallback: delete from active dir
        std::remove(src.c_str());
      }
    }
  }
#else
  DIR* dir = opendir(m_swapsDir.c_str());
  if (!dir) return;
  struct dirent* entry;
  while ((entry = readdir(dir)) != nullptr) {
    std::string name = entry->d_name;
    if (name.size() <= 5 || name.substr(name.size() - 5) != ".json") continue;
    std::string swapId = name.substr(0, name.size() - 5);
    SwapStateMachine sm;
    if (loadSwapLocked(swapId, sm) && sm.isTerminal()) {
      std::string src = swapFilePath(swapId);
      std::string dst = archiveFilePath(swapId);
      if (std::rename(src.c_str(), dst.c_str()) != 0) {
        std::remove(src.c_str());
      }
    }
  }
  closedir(dir);
#endif
}

} // namespace XfgSwap
