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

#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace CryptoNote {

class ISerializer;

/**
 * ElderfierAudioIndex - Separate index for DIGM audio services
 * 
 * This index is distinct from ENindex (burn verification) and tracks:
 * - Audio seeding commitments by Elderfiers
 * - Track-to-Elderfier mappings for decryption services  
 * - Service performance metrics and fee tracking
 * - Content availability guarantees
 */

struct ElderfierAudioEntry {
  std::string elderfierID;      // 8-character Elderfier identifier (e.g., "AUDINODE")
  std::string trackId;          // DIGM track identifier
  std::string albumId;          // DIGM album identifier
  std::string contentHash;      // SHA256 hash of original audio content (as hex string)
  std::string commitmentHash;   // Hash of seeding commitment transaction (as hex string)
  uint64_t stakeFee;           // Fee paid for this service (in atomic XFG units)
  uint64_t startHeight;        // Block height when service started
  uint64_t endHeight;          // Block height when service ends (0 = ongoing)
  uint32_t serviceType;        // 1=seeding, 2=decryption, 3=both
  uint32_t performanceScore;   // Performance rating (0-1000)
  uint64_t timestamp;          // Unix timestamp of commitment
  
  // Constructor
  ElderfierAudioEntry() : 
    stakeFee(0), startHeight(0), endHeight(0), 
    serviceType(0), performanceScore(500), timestamp(0) {}
    
  ElderfierAudioEntry(
    const std::string& _elderfierID,
    const std::string& _trackId, 
    const std::string& _albumId,
    const std::string& _contentHash,
    const std::string& _commitmentHash,
    uint64_t _stakeFee,
    uint64_t _startHeight,
    uint32_t _serviceType,
    uint64_t _timestamp
  ) : elderfierID(_elderfierID), trackId(_trackId), albumId(_albumId),
      contentHash(_contentHash), commitmentHash(_commitmentHash),
      stakeFee(_stakeFee), startHeight(_startHeight), endHeight(0),
      serviceType(_serviceType), performanceScore(500), timestamp(_timestamp) {}
};

class ElderfierAudioIndex {
public:
  
  ElderfierAudioIndex();
  ~ElderfierAudioIndex();

  // Core operations
  bool addAudioService(const ElderfierAudioEntry& entry);
  bool removeAudioService(const std::string& commitmentHash);
  bool updateServicePerformance(const std::string& commitmentHash, uint32_t performanceScore);
  bool endAudioService(const std::string& commitmentHash, uint64_t endHeight);

  // Query operations
  std::vector<ElderfierAudioEntry> getServicesByElderfier(const std::string& elderfierID) const;
  std::vector<ElderfierAudioEntry> getServicesByTrack(const std::string& trackId) const;
  std::vector<ElderfierAudioEntry> getServicesByAlbum(const std::string& albumId) const;
  std::vector<ElderfierAudioEntry> getActiveServices() const;
  std::vector<ElderfierAudioEntry> getServicesAtHeight(uint64_t height) const;
  
  ElderfierAudioEntry* getServiceByCommitment(const std::string& commitmentHash);
  const ElderfierAudioEntry* getServiceByCommitment(const std::string& commitmentHash) const;

  // Validation and verification
  bool hasActiveSeeding(const std::string& trackId) const;
  bool hasDecryptionService(const std::string& trackId) const;
  uint32_t getServiceCount(const std::string& elderfierID) const;
  uint64_t getTotalStakedFees(const std::string& elderfierID) const;

  // Cleanup operations
  size_t removeExpiredServices(uint64_t currentHeight);
  size_t removeServicesByElderfier(const std::string& elderfierID);

  // Statistics
  uint32_t getTotalActiveServices() const;
  uint32_t getUniqueElderfierCount() const;
  uint32_t getServicedTrackCount() const;

  // Block operations
  void pushBlock(uint64_t height);
  void popBlock(uint64_t height);
  void clear();
  
  uint32_t size() const;
  bool empty() const;

  // Serialization
  void serialize(ISerializer& s);

private:
  std::vector<ElderfierAudioEntry> m_entries;
  
  // Hash maps for fast lookup
  std::unordered_map<std::string, std::vector<size_t> > m_elderfierIndex;  // elderfierID -> entry indices
  std::unordered_map<std::string, std::vector<size_t> > m_trackIndex;      // trackId -> entry indices  
  std::unordered_map<std::string, std::vector<size_t> > m_albumIndex;      // albumId -> entry indices
  std::unordered_map<std::string, size_t> m_commitmentIndex;              // commitmentHash -> entry index
  
  // Internal helper methods
  bool isValidEntry(const ElderfierAudioEntry& entry) const;
  void updateIndices();
  void rebuildIndices();
};

} // namespace CryptoNote 
