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

#include "SpvHeaderStore.h"
#include <algorithm>
#include <cassert>

namespace XfgSwap {

// =============================================================================
// Construction
// =============================================================================

SpvHeaderStore::SpvHeaderStore() : m_bestTipHeight(0), m_maxHeightDelta(0) {}

// =============================================================================
// Public API
// =============================================================================

void SpvHeaderStore::setMaxHeightDelta(uint64_t depth) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  m_maxHeightDelta = depth;
  pruneOldHeaders();
}

// =============================================================================
// Checkpoint
// =============================================================================

bool SpvHeaderStore::anchor(uint64_t checkpointHeight,
                            const std::string& checkpointHashDisplay) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  m_checkpointHeight = checkpointHeight;
  m_checkpointHash = checkpointHashDisplay;
  m_hasCheckpoint = true;
  return true;
}

// =============================================================================
// Add header (best-chain extension)
// =============================================================================

bool SpvHeaderStore::addHeader(const SpvHeader& header) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  std::string headerHash = header.hashDisplay();
  std::string prevDisplay = header.prevHashDisplay();

  // Special case: empty store — accept as genesis at height 0
  if (m_entries.empty()) {
    if (!header.meetsPoW()) {
      return false;
    }

    ChainEntry entry;
    entry.header = header;
    entry.hashDisplay = headerHash;
    entry.prevHashDisplay = prevDisplay;
    entry.cumulativeWork = header.work();

    m_entries[0].push_back(entry);
    m_bestTipHeight = 0;
    m_bestTipHash = headerHash;
    return true;
  }

  // Find the parent — must exist
  auto parentLoc = findHeaderByHash(prevDisplay);
  if (parentLoc.first < 0) {
    return false;
  }

  uint64_t parentHeight = static_cast<uint64_t>(parentLoc.first);

  // Parent must be on the best chain for a simple add
  if (!isOnBestChain(parentHeight, prevDisplay)) {
    return false;
  }

  uint64_t height = parentHeight + 1;

  // Checkpoint validation
  if (m_hasCheckpoint) {
    if (height <= m_checkpointHeight) {
      return false;
    }
    if (height == m_checkpointHeight + 1) {
      if (prevDisplay != m_checkpointHash) {
        return false;
      }
    }
  }

  // PoW validation
  if (!header.meetsPoW()) {
    return false;
  }

  // Build chain entry
  long double parentWork = m_entries[parentHeight][parentLoc.second].cumulativeWork;
  ChainEntry entry;
  entry.header = header;
  entry.hashDisplay = headerHash;
  entry.prevHashDisplay = prevDisplay;
  entry.cumulativeWork = parentWork + header.work();

  m_entries[height].push_back(entry);

  // Update best tip
  if (height > m_bestTipHeight) {
    m_bestTipHeight = height;
    m_bestTipHash = headerHash;
  } else {
    updateBestTip();
  }
  pruneOldHeaders();
  return true;
}

// =============================================================================
// Add header at specific height (for testing)
// =============================================================================

bool SpvHeaderStore::addHeaderAtHeight(const SpvHeader& header, uint64_t height) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  std::string headerHash = header.hashDisplay();
  std::string prevDisplay = header.prevHashDisplay();

  // Checkpoint validation: reject headers strictly below checkpoint height
  if (m_hasCheckpoint && height < m_checkpointHeight) {
    return false;
  }

  // Height 0 = genesis
  if (height == 0) {
    if (!header.meetsPoW()) {
      return false;
    }

    ChainEntry entry;
    entry.header = header;
    entry.hashDisplay = headerHash;
    entry.prevHashDisplay = prevDisplay;
    entry.cumulativeWork = header.work();

    m_entries[0].push_back(entry);

    if (m_bestTipHeight == 0 && m_bestTipHash.empty()) {
      m_bestTipHeight = 0;
      m_bestTipHash = headerHash;
    } else {
      updateBestTip();
    }
    return true;
  }

  // At checkpoint+1: enforce prevHash == checkpointHash, but don't require
  // a parent entry at height-1 (the checkpoint IS the implicit parent).
  if (m_hasCheckpoint && height == m_checkpointHeight + 1) {
    if (prevDisplay != m_checkpointHash) {
      return false;
    }
  } else {
    // Height > 0 (not checkpoint+1): parent must exist at height-1
    auto parentLoc = findHeaderByHash(prevDisplay);
    if (parentLoc.first < 0 || static_cast<uint64_t>(parentLoc.first) != height - 1) {
      return false;
    }
  }

  // PoW validation
  if (!header.meetsPoW()) {
    return false;
  }

  // Compute cumulative work
  long double parentWork = 0;
  if (m_hasCheckpoint && height == m_checkpointHeight + 1) {
    // No parent entry; cumulative work starts from zero at checkpoint
    parentWork = 0;
  } else {
    auto parentLoc2 = findHeaderByHash(prevDisplay);
    if (parentLoc2.first >= 0 && static_cast<uint64_t>(parentLoc2.first) == height - 1) {
      parentWork = m_entries[height - 1][parentLoc2.second].cumulativeWork;
    }
  }

  ChainEntry entry;
  entry.header = header;
  entry.hashDisplay = headerHash;
  entry.prevHashDisplay = prevDisplay;
  entry.cumulativeWork = parentWork + header.work();

  m_entries[height].push_back(entry);

  // Update best tip
  if (height > m_bestTipHeight) {
    m_bestTipHeight = height;
    m_bestTipHash = headerHash;
  } else {
    updateBestTip();
  }

  return true;
}

// =============================================================================
// Queries
// =============================================================================

bool SpvHeaderStore::bestTip(uint64_t& height, std::string& hashDisplay) const {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (m_bestTipHeight == 0 && m_bestTipHash.empty()) {
    return false;
  }
  height = m_bestTipHeight;
  hashDisplay = m_bestTipHash;
  return true;
}

bool SpvHeaderStore::merkleRootAt(uint64_t height, std::vector<uint8_t>& rootLE) const {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  auto it = m_entries.find(height);
  if (it == m_entries.end()) {
    return false;
  }

  for (const auto& entry : it->second) {
    if (isOnBestChain(height, entry.hashDisplay)) {
      rootLE = entry.header.merkleRoot;
      return true;
    }
  }

  return false;
}

uint32_t SpvHeaderStore::depthOf(uint64_t height) const {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (height > m_bestTipHeight) {
    return 0;
  }
  return static_cast<uint32_t>(m_bestTipHeight - height + 1);
}

bool SpvHeaderStore::headerAtHeight(uint64_t height, SpvHeader& header) const {
  auto it = m_entries.find(height);
  if (it == m_entries.end()) {
    return false;
  }

  for (const auto& entry : it->second) {
    if (isOnBestChain(height, entry.hashDisplay)) {
      header = entry.header;
      return true;
    }
  }

  return false;
}

long double SpvHeaderStore::cumulativeWorkAt(uint64_t height) const {
  auto it = m_entries.find(height);
  if (it == m_entries.end()) {
    return 0;
  }

  for (const auto& entry : it->second) {
    if (isOnBestChain(height, entry.hashDisplay)) {
      return entry.cumulativeWork;
    }
  }

  return 0;
}

// =============================================================================
// Internal helpers
// =============================================================================

std::pair<int64_t, size_t> SpvHeaderStore::findHeaderByHash(
    const std::string& hashDisplay) const {
  for (const auto& heightPair : m_entries) {
    for (size_t i = 0; i < heightPair.second.size(); ++i) {
      if (heightPair.second[i].hashDisplay == hashDisplay) {
        return {static_cast<int64_t>(heightPair.first), i};
      }
    }
  }
  return {-1, 0};
}

bool SpvHeaderStore::isOnBestChain(uint64_t height,
                                   const std::string& hashDisplay) const {
  if (m_bestTipHash.empty()) {
    return false;
  }

  // Walk from tip back to the target height following prevHash pointers
  std::string curHash = m_bestTipHash;
  uint64_t curHeight = m_bestTipHeight;

  while (curHeight >= height) {
    if (curHeight == height) {
      return curHash == hashDisplay;
    }

    // Find entry for curHash at curHeight
    auto loc = findHeaderByHash(curHash);
    if (loc.first < 0 || static_cast<uint64_t>(loc.first) != curHeight) {
      return false;
    }

    const ChainEntry& e = m_entries.at(curHeight)[loc.second];
    curHash = e.prevHashDisplay;
    curHeight--;
  }

  return false;
}

void SpvHeaderStore::updateBestTip() {
  if (m_entries.empty()) {
    m_bestTipHeight = 0;
    m_bestTipHash.clear();
    return;
  }

  // Find the highest height with entries
  uint64_t maxHeight = m_entries.rbegin()->first;

  // Find the entry with highest cumulative work at maxHeight
  std::string bestHash;
  long double bestWork = -1;

  auto tipIt = m_entries.find(maxHeight);
  if (tipIt != m_entries.end()) {
    for (const auto& entry : tipIt->second) {
      if (entry.cumulativeWork > bestWork) {
        bestWork = entry.cumulativeWork;
        bestHash = entry.hashDisplay;
      }
    }
  }

  if (!bestHash.empty()) {
    m_bestTipHeight = maxHeight;
    m_bestTipHash = bestHash;
  }
}

// Remove headers older than the retention limit.
void SpvHeaderStore::pruneOldHeaders() {
  if (m_maxHeightDelta == 0) {
    return; // no pruning
  }
  // Determine the lowest height to keep
  uint64_t keepMinHeight = 0;
  if (m_bestTipHeight > m_maxHeightDelta) {
    keepMinHeight = m_bestTipHeight - m_maxHeightDelta;
  }
  // Ensure we keep at least the checkpoint height (if set)
  if (m_hasCheckpoint && m_checkpointHeight > keepMinHeight) {
    keepMinHeight = m_checkpointHeight;
  }
  // Remove all entries with height < keepMinHeight
  for (auto it = m_entries.begin(); it != m_entries.end(); ) {
    if (it->first < keepMinHeight) {
      it = m_entries.erase(it);
    } else {
      ++it;
    }
  }
}

} // namespace XfgSwap
