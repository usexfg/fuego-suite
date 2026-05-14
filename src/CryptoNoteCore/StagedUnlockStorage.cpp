// Copyright (c) 2017-2025 Elderfire Privacy Council
// Copyright (c) 2018-2019 Conceal Network & Conceal Devs
// Copyright (c) 2014-2017 The XDN developers
// Copyright (c) 2012-2018 The CryptoNote developers
//
// This file is part of Fuego.
//
// Fuego is free & open source software distributed in the hope
// that it will be useful, but WITHOUT ANY WARRANTY; without even
// the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You may redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation. Fuego includes elements written
// by third parties. See file labeled LICENSE for more details.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

#include "StagedUnlockStorage.h"
#include <fstream>
#include <sstream>
#include <iostream>

namespace CryptoNote {

StagedUnlockStorage::StagedUnlockStorage() : m_initialized(false) {
}

StagedUnlockStorage::~StagedUnlockStorage() {
  if (m_initialized) {
    try {
      save();
    } catch (const std::exception& e) {
      // Can't throw in destructor, so we just log
      std::cerr << "Failed to save StagedUnlockStorage: " << e.what() << std::endl;
    }
  }
}

void StagedUnlockStorage::init(const std::string& filePath) {
  m_filePath = filePath;
  m_initialized = true;
  
  try {
    load();
  } catch (const std::exception& e) {
    // It's ok if the file doesn't exist yet
    std::cerr << "Could not load StagedUnlockStorage file, starting with empty storage: " << e.what() << std::endl;
  }
}

void StagedUnlockStorage::save() {
  if (!m_initialized) {
    throw std::runtime_error("StagedUnlockStorage not initialized");
  }
  
  std::ofstream file(m_filePath);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open file for writing: " + m_filePath);
  }
  
  // Simple format: transactionHash:useStagedUnlock on each line
  for (const auto& pair : m_stagedUnlockMap) {
    file << pair.first << ":" << (pair.second ? "1" : "0") << std::endl;
  }
  
  file.close();
}

void StagedUnlockStorage::load() {
  if (!m_initialized) {
    throw std::runtime_error("StagedUnlockStorage not initialized");
  }
  
  std::ifstream file(m_filePath);
  if (!file.is_open()) {
    // File doesn't exist yet, which is fine for a new wallet
    return;
  }
  
  m_stagedUnlockMap.clear();
  
  std::string line;
  while (std::getline(file, line)) {
    size_t colonPos = line.find(':');
    if (colonPos != std::string::npos) {
      std::string txHash = line.substr(0, colonPos);
      std::string value = line.substr(colonPos + 1);
      bool useStagedUnlock = (value == "1");
      m_stagedUnlockMap[txHash] = useStagedUnlock;
    }
  }
  
  file.close();
}

void StagedUnlockStorage::setStagedUnlockPreference(const std::string& transactionHash, bool useStagedUnlock) {
  if (!m_initialized) {
    throw std::runtime_error("StagedUnlockStorage not initialized");
  }
  
  m_stagedUnlockMap[transactionHash] = useStagedUnlock;
}

bool StagedUnlockStorage::getStagedUnlockPreference(const std::string& transactionHash) const {
  if (!m_initialized) {
    // Default to false if not initialized
    return false;
  }
  
  auto it = m_stagedUnlockMap.find(transactionHash);
  return (it != m_stagedUnlockMap.end()) ? it->second : false;
}

void StagedUnlockStorage::removeTransaction(const std::string& transactionHash) {
  if (!m_initialized) {
    throw std::runtime_error("StagedUnlockStorage not initialized");
  }
  
  m_stagedUnlockMap.erase(transactionHash);
}

void StagedUnlockStorage::clear() {
  if (!m_initialized) {
    throw std::runtime_error("StagedUnlockStorage not initialized");
  }
  
  m_stagedUnlockMap.clear();
}

} // namespace CryptoNote