// Copyright (c) 2017-2022 Fuego Developers
// Copyright (c) 2018-2019 Conceal Network & Conceal Devs
// Copyright (c) 2016-2019 The Karbowanec developers
// Copyright (c) 2012-2018 The CryptoNote developers
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

#include "ElderfierAudioIndex.h"
#include <algorithm>

namespace CryptoNote {

ElderfierAudioIndex::ElderfierAudioIndex() {
}

ElderfierAudioIndex::~ElderfierAudioIndex() {
}

bool ElderfierAudioIndex::addAudioService(const ElderfierAudioEntry& entry) {
  if (!isValidEntry(entry)) {
    return false;
  }

  // Check if commitment hash already exists
  if (m_commitmentIndex.find(entry.commitmentHash) != m_commitmentIndex.end()) {
    return false; // Duplicate commitment
  }

  // Add entry
  size_t index = m_entries.size();
  m_entries.push_back(entry);
  
  // Update indices
  m_elderfierIndex[entry.elderfierID].push_back(index);
  m_trackIndex[entry.trackId].push_back(index);
  m_albumIndex[entry.albumId].push_back(index);
  m_commitmentIndex[entry.commitmentHash] = index;

  return true;
}

bool ElderfierAudioIndex::removeAudioService(const std::string& commitmentHash) {
  auto it = m_commitmentIndex.find(commitmentHash);
  if (it == m_commitmentIndex.end()) {
    return false;
  }

  size_t index = it->second;
  if (index >= m_entries.size()) {
    return false;
  }

  // Mark as removed (set endHeight to indicate removal)
  m_entries[index].endHeight = 1; // Use 1 to indicate manually removed
  
  // Remove from commitment index
  m_commitmentIndex.erase(it);
  
  // Rebuild other indices to remove stale references
  rebuildIndices();

  return true;
}

bool ElderfierAudioIndex::updateServicePerformance(const std::string& commitmentHash, uint32_t performanceScore) {
  auto it = m_commitmentIndex.find(commitmentHash);
  if (it == m_commitmentIndex.end()) {
    return false;
  }

  size_t index = it->second;
  if (index >= m_entries.size()) {
    return false;
  }

  m_entries[index].performanceScore = std::min(performanceScore, static_cast<uint32_t>(1000));
  return true;
}

bool ElderfierAudioIndex::endAudioService(const std::string& commitmentHash, uint64_t endHeight) {
  auto it = m_commitmentIndex.find(commitmentHash);
  if (it == m_commitmentIndex.end()) {
    return false;
  }

  size_t index = it->second;
  if (index >= m_entries.size()) {
    return false;
  }

  m_entries[index].endHeight = endHeight;
  return true;
}

std::vector<ElderfierAudioEntry> ElderfierAudioIndex::getServicesByElderfier(const std::string& elderfierID) const {
  std::vector<ElderfierAudioEntry> result;
  
  auto it = m_elderfierIndex.find(elderfierID);
  if (it != m_elderfierIndex.end()) {
    for (size_t index : it->second) {
      if (index < m_entries.size() && m_entries[index].endHeight == 0) {
        result.push_back(m_entries[index]);
      }
    }
  }
  
  return result;
}

std::vector<ElderfierAudioEntry> ElderfierAudioIndex::getServicesByTrack(const std::string& trackId) const {
  std::vector<ElderfierAudioEntry> result;
  
  auto it = m_trackIndex.find(trackId);
  if (it != m_trackIndex.end()) {
    for (size_t index : it->second) {
      if (index < m_entries.size() && m_entries[index].endHeight == 0) {
        result.push_back(m_entries[index]);
      }
    }
  }
  
  return result;
}

std::vector<ElderfierAudioEntry> ElderfierAudioIndex::getServicesByAlbum(const std::string& albumId) const {
  std::vector<ElderfierAudioEntry> result;
  
  auto it = m_albumIndex.find(albumId);
  if (it != m_albumIndex.end()) {
    for (size_t index : it->second) {
      if (index < m_entries.size() && m_entries[index].endHeight == 0) {
        result.push_back(m_entries[index]);
      }
    }
  }
  
  return result;
}

std::vector<ElderfierAudioEntry> ElderfierAudioIndex::getActiveServices() const {
  std::vector<ElderfierAudioEntry> result;
  
  for (const auto& entry : m_entries) {
    if (entry.endHeight == 0) {
      result.push_back(entry);
    }
  }
  
  return result;
}

std::vector<ElderfierAudioEntry> ElderfierAudioIndex::getServicesAtHeight(uint64_t height) const {
  std::vector<ElderfierAudioEntry> result;
  
  for (const auto& entry : m_entries) {
    if (entry.startHeight <= height && (entry.endHeight == 0 || entry.endHeight > height)) {
      result.push_back(entry);
    }
  }
  
  return result;
}

ElderfierAudioEntry* ElderfierAudioIndex::getServiceByCommitment(const std::string& commitmentHash) {
  auto it = m_commitmentIndex.find(commitmentHash);
  if (it != m_commitmentIndex.end() && it->second < m_entries.size()) {
    return &m_entries[it->second];
  }
  return nullptr;
}

const ElderfierAudioEntry* ElderfierAudioIndex::getServiceByCommitment(const std::string& commitmentHash) const {
  auto it = m_commitmentIndex.find(commitmentHash);
  if (it != m_commitmentIndex.end() && it->second < m_entries.size()) {
    return &m_entries[it->second];
  }
  return nullptr;
}

bool ElderfierAudioIndex::hasActiveSeeding(const std::string& trackId) const {
  auto services = getServicesByTrack(trackId);
  for (const auto& service : services) {
    if (service.serviceType == 1 || service.serviceType == 3) { // seeding or both
      return true;
    }
  }
  return false;
}

bool ElderfierAudioIndex::hasDecryptionService(const std::string& trackId) const {
  auto services = getServicesByTrack(trackId);
  for (const auto& service : services) {
    if (service.serviceType == 2 || service.serviceType == 3) { // decryption or both
      return true;
    }
  }
  return false;
}

uint32_t ElderfierAudioIndex::getServiceCount(const std::string& elderfierID) const {
  auto services = getServicesByElderfier(elderfierID);
  return static_cast<uint32_t>(services.size());
}

uint64_t ElderfierAudioIndex::getTotalStakedFees(const std::string& elderfierID) const {
  uint64_t total = 0;
  auto services = getServicesByElderfier(elderfierID);
  for (const auto& service : services) {
    total += service.stakeFee;
  }
  return total;
}

size_t ElderfierAudioIndex::removeExpiredServices(uint64_t currentHeight) {
  size_t removed = 0;
  
  for (auto& entry : m_entries) {
    if (entry.endHeight > 0 && entry.endHeight <= currentHeight) {
      // Mark as expired
      if (entry.endHeight != 1) { // Don't count manually removed ones
        removed++;
      }
    }
  }
  
  // Clean up expired entries
  m_entries.erase(
    std::remove_if(m_entries.begin(), m_entries.end(),
      [currentHeight](const ElderfierAudioEntry& entry) {
        return entry.endHeight > 0 && entry.endHeight <= currentHeight;
      }),
    m_entries.end()
  );
  
  rebuildIndices();
  return removed;
}

size_t ElderfierAudioIndex::removeServicesByElderfier(const std::string& elderfierID) {
  size_t removed = 0;
  
  // Mark all services by this Elderfier as removed
  for (auto& entry : m_entries) {
    if (entry.elderfierID == elderfierID && entry.endHeight == 0) {
      entry.endHeight = 1; // Mark as manually removed
      removed++;
    }
  }
  
  // Remove from entries
  m_entries.erase(
    std::remove_if(m_entries.begin(), m_entries.end(),
      [&elderfierID](const ElderfierAudioEntry& entry) {
        return entry.elderfierID == elderfierID;
      }),
    m_entries.end()
  );
  
  rebuildIndices();
  return removed;
}

uint32_t ElderfierAudioIndex::getTotalActiveServices() const {
  uint32_t count = 0;
  for (const auto& entry : m_entries) {
    if (entry.endHeight == 0) {
      count++;
    }
  }
  return count;
}

uint32_t ElderfierAudioIndex::getUniqueElderfierCount() const {
  std::unordered_set<std::string> uniqueElderfiers;
  for (const auto& entry : m_entries) {
    if (entry.endHeight == 0) {
      uniqueElderfiers.insert(entry.elderfierID);
    }
  }
  return static_cast<uint32_t>(uniqueElderfiers.size());
}

uint32_t ElderfierAudioIndex::getServicedTrackCount() const {
  std::unordered_set<std::string> uniqueTracks;
  for (const auto& entry : m_entries) {
    if (entry.endHeight == 0) {
      uniqueTracks.insert(entry.trackId);
    }
  }
  return static_cast<uint32_t>(uniqueTracks.size());
}

void ElderfierAudioIndex::pushBlock(uint64_t height) {
  // Remove any services that ended at this height
  removeExpiredServices(height);
}

void ElderfierAudioIndex::popBlock(uint64_t height) {
  // Remove any services that started at this height
  m_entries.erase(
    std::remove_if(m_entries.begin(), m_entries.end(),
      [height](const ElderfierAudioEntry& entry) {
        return entry.startHeight == height;
      }),
    m_entries.end()
  );
  
  rebuildIndices();
}

void ElderfierAudioIndex::clear() {
  m_entries.clear();
  m_elderfierIndex.clear();
  m_trackIndex.clear();
  m_albumIndex.clear();
  m_commitmentIndex.clear();
}

uint32_t ElderfierAudioIndex::size() const {
  return static_cast<uint32_t>(m_entries.size());
}

bool ElderfierAudioIndex::empty() const {
  return m_entries.empty();
}

void ElderfierAudioIndex::serialize(ISerializer& s) {
  // Serialization implementation would go here
  // For now, just rebuild indices
  rebuildIndices();
}

bool ElderfierAudioIndex::isValidEntry(const ElderfierAudioEntry& entry) const {
  // Basic validation
  if (entry.elderfierID.empty() || entry.elderfierID.length() > 8) {
    return false;
  }
  
  if (entry.trackId.empty() || entry.albumId.empty()) {
    return false;
  }
  
  if (entry.commitmentHash.empty()) {
    return false;
  }
  
  if (entry.serviceType == 0 || entry.serviceType > 3) {
    return false;
  }
  
  return true;
}

void ElderfierAudioIndex::updateIndices() {
  // This method could be used for incremental index updates
  // For now, we rebuild indices when needed
}

void ElderfierAudioIndex::rebuildIndices() {
  // Clear all indices
  m_elderfierIndex.clear();
  m_trackIndex.clear();
  m_albumIndex.clear();
  m_commitmentIndex.clear();
  
  // Rebuild indices from entries
  for (size_t i = 0; i < m_entries.size(); ++i) {
    const auto& entry = m_entries[i];
    
    if (entry.endHeight == 0) { // Only index active services
      m_elderfierIndex[entry.elderfierID].push_back(i);
      m_trackIndex[entry.trackId].push_back(i);
      m_albumIndex[entry.albumId].push_back(i);
    }
    
    // Always index by commitment hash for lookup
    m_commitmentIndex[entry.commitmentHash] = i;
  }
}

} // namespace CryptoNote
