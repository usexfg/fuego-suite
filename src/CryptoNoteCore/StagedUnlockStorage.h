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

#pragma once

#include <string>
#include <unordered_map>
#include <memory>

namespace CryptoNote {

/**
 * Manages staged unlock preferences for deposits
 * 
 * This class provides storage and retrieval of staged unlock preferences
 * for deposit transactions. It maintains a mapping of transaction hashes
 * to their staged unlock settings.
 */
class StagedUnlockStorage {
public:
  StagedUnlockStorage();
  ~StagedUnlockStorage();

  /**
   * Initialize the storage with a file path
   * 
   * @param filePath Path to the storage file
   */
  void init(const std::string& filePath);
  
  /**
   * Save the storage to file
   */
  void save();
  
  /**
   * Load the storage from file
   */
  void load();

  /**
   * Set staged unlock preference for a transaction
   * 
   * @param transactionHash The transaction hash
   * @param useStagedUnlock Whether to use staged unlock
   */
  void setStagedUnlockPreference(const std::string& transactionHash, bool useStagedUnlock);

  /**
   * Get staged unlock preference for a transaction
   * 
   * @param transactionHash The transaction hash
   * @return true if staged unlock is enabled, false otherwise
   */
  bool getStagedUnlockPreference(const std::string& transactionHash) const;

  /**
   * Remove a transaction from the storage
   * 
   * @param transactionHash The transaction hash to remove
   */
  void removeTransaction(const std::string& transactionHash);

  /**
   * Clear all stored preferences
   */
  void clear();

private:
  std::string m_filePath;
  std::unordered_map<std::string, bool> m_stagedUnlockMap;
  bool m_initialized;
};

} // namespace CryptoNote