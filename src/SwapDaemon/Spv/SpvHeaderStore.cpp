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

#include <ctime>
#include <algorithm>
#include <cassert>

namespace XfgSwap {

// =============================================================================
// Construction
// =============================================================================

SpvHeaderStore::SpvHeaderStore() = default;

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

void SpvHeaderStore::setRules(const SpvChainRules& rules) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  m_rules = rules;
  m_hasRules = true;
}

bool SpvHeaderStore::hasRules() const {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  return m_hasRules;
}

std::string SpvHeaderStore::lastRejection() const {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  return m_lastRejection;
}

bool SpvHeaderStore::addCheckpointHeader(const SpvHeader& header) {
  return addCheckpointHistory({header});
}

bool SpvHeaderStore::addCheckpointHistory(const std::vector<SpvHeader>& headers) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (!m_hasCheckpoint || !m_chain.entries.empty() || headers.empty() || headers.size() > m_checkpointHeight + 1 ||
      headers.back().hashDisplay() != m_checkpointHash) {
    m_lastRejection = "header is not the configured checkpoint";
    return false;
  }
  for (size_t i = 1; i < headers.size(); ++i) {
    if (headers[i].prevHash != headers[i - 1].hash()) {
      m_lastRejection = "checkpoint history does not link to the checkpoint";
      return false;
    }
  }
  uint64_t height = m_checkpointHeight + 1 - headers.size();
  long double work = 0;
  for (const SpvHeader& header : headers) {
    ChainEntry entry;
    entry.header = header;
    entry.hashDisplay = header.hashDisplay();
    entry.prevHashDisplay = header.prevHashDisplay();
    work += header.work();
    entry.cumulativeWork = work;
    m_chain.entries[height++].push_back(entry);
  }
  m_chain.bestTipHeight = m_checkpointHeight;
  m_chain.bestTipHash = m_checkpointHash;
  m_lastRejection.clear();
  return true;
}

bool SpvHeaderStore::rejectsUnderRules(const SpvHeader& header, uint64_t height, const SpvHeader& parent) {
  const SpvAncestorLookup ancestorAt = [this](uint64_t h, SpvHeader& out) { return headerAtHeight(h, out); };
  m_lastRejection = headerRuleViolation(m_rules, height, header, parent, ancestorAt,
                                        static_cast<int64_t>(std::time(nullptr)));
  return !m_lastRejection.empty();
}

// =============================================================================
// Add header (best-chain extension)
// =============================================================================

bool SpvHeaderStore::addHeader(const SpvHeader& header) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  std::string headerHash = header.hashDisplay();
  std::string prevDisplay = header.prevHashDisplay();

  // Special case: empty store — accept as genesis at height 0. An anchored
  // store starts from its checkpoint header instead.
  if (m_chain.entries.empty()) {
    if (m_hasCheckpoint || m_hasRules) {
      m_lastRejection = "store must start from its checkpoint header";
      return false;
    }
    if (!header.meetsPoW()) {
      return false;
    }

    ChainEntry entry;
    entry.header = header;
    entry.hashDisplay = headerHash;
    entry.prevHashDisplay = prevDisplay;
    entry.cumulativeWork = header.work();

    m_chain.entries[0].push_back(entry);
    m_chain.bestTipHeight = 0;
    m_chain.bestTipHash = headerHash;
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

  // PoW and the chain's difficulty schedule
  const SpvHeader& parentHeader = m_chain.entries[parentHeight][parentLoc.second].header;
  if (m_hasRules ? rejectsUnderRules(header, height, parentHeader) : !header.meetsPoW()) {
    return false;
  }

  // Build chain entry
  long double parentWork = m_chain.entries[parentHeight][parentLoc.second].cumulativeWork;
  ChainEntry entry;
  entry.header = header;
  entry.hashDisplay = headerHash;
  entry.prevHashDisplay = prevDisplay;
  entry.cumulativeWork = parentWork + header.work();

  m_chain.entries[height].push_back(entry);

  // Update best tip
  if (height > m_chain.bestTipHeight) {
    m_chain.bestTipHeight = height;
    m_chain.bestTipHash = headerHash;
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

    m_chain.entries[0].push_back(entry);

    if (m_chain.bestTipHeight == 0 && m_chain.bestTipHash.empty()) {
      m_chain.bestTipHeight = 0;
      m_chain.bestTipHash = headerHash;
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
      parentWork = m_chain.entries[height - 1][parentLoc2.second].cumulativeWork;
    }
  }

  ChainEntry entry;
  entry.header = header;
  entry.hashDisplay = headerHash;
  entry.prevHashDisplay = prevDisplay;
  entry.cumulativeWork = parentWork + header.work();

  m_chain.entries[height].push_back(entry);

  // Update best tip
  if (height > m_chain.bestTipHeight) {
    m_chain.bestTipHeight = height;
    m_chain.bestTipHash = headerHash;
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
  if (m_chain.bestTipHeight == 0 && m_chain.bestTipHash.empty()) {
    return false;
  }
  height = m_chain.bestTipHeight;
  hashDisplay = m_chain.bestTipHash;
  return true;
}

bool SpvHeaderStore::merkleRootAt(uint64_t height, std::vector<uint8_t>& rootLE) const {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  auto it = m_chain.entries.find(height);
  if (it == m_chain.entries.end()) {
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
  if (height > m_chain.bestTipHeight) {
    return 0;
  }
  return static_cast<uint32_t>(m_chain.bestTipHeight - height + 1);
}

bool SpvHeaderStore::headerAtHeight(uint64_t height, SpvHeader& header) const {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  auto it = m_chain.entries.find(height);
  if (it == m_chain.entries.end()) {
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
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  auto it = m_chain.entries.find(height);
  if (it == m_chain.entries.end()) {
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
  for (auto level = m_chain.entries.rbegin(); level != m_chain.entries.rend(); ++level) {
    for (size_t i = 0; i < level->second.size(); ++i) {
      if (level->second[i].hashDisplay == hashDisplay) {
        return {static_cast<int64_t>(level->first), i};
      }
    }
  }
  return {-1, 0};
}

const SpvHeaderStore::ChainEntry* SpvHeaderStore::entryAt(uint64_t height,
                                                          const std::string& hashDisplay) const {
  auto level = m_chain.entries.find(height);
  if (level == m_chain.entries.end()) return nullptr;
  for (const auto& entry : level->second)
    if (entry.hashDisplay == hashDisplay) return &entry;
  return nullptr;
}

bool SpvHeaderStore::isOnBestChain(uint64_t height,
                                   const std::string& hashDisplay) const {
  if (m_chain.bestTipHash.empty()) {
    return false;
  }

  // Walk from tip back to the target height following prevHash pointers
  std::string curHash = m_chain.bestTipHash;
  uint64_t curHeight = m_chain.bestTipHeight;

  while (curHeight >= height) {
    if (curHeight == height) {
      return curHash == hashDisplay;
    }

    const ChainEntry* entry = entryAt(curHeight, curHash);
    if (!entry) {
      return false;
    }
    curHash = entry->prevHashDisplay;
    curHeight--;
  }

  return false;
}

void SpvHeaderStore::updateBestTip() {
  if (m_chain.entries.empty()) {
    m_chain.bestTipHeight = 0;
    m_chain.bestTipHash.clear();
    return;
  }

  // Find the highest height with entries
  uint64_t maxHeight = m_chain.entries.rbegin()->first;

  // Find the entry with highest cumulative work at maxHeight
  std::string bestHash;
  long double bestWork = -1;

  auto tipIt = m_chain.entries.find(maxHeight);
  if (tipIt != m_chain.entries.end()) {
    for (const auto& entry : tipIt->second) {
      if (entry.cumulativeWork > bestWork) {
        bestWork = entry.cumulativeWork;
        bestHash = entry.hashDisplay;
      }
    }
  }

  if (!bestHash.empty()) {
    m_chain.bestTipHeight = maxHeight;
    m_chain.bestTipHash = bestHash;
  }
}

SpvHeaderStore::Snapshot SpvHeaderStore::snapshot() const {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  Snapshot saved;
  saved.state = m_chain;
  return saved;
}

void SpvHeaderStore::restore(const Snapshot& saved) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  m_chain = saved.state;
}

bool SpvHeaderStore::rewindTo(uint64_t height) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  SpvHeader kept;
  if ((m_hasCheckpoint && height < m_checkpointHeight) || !headerAtHeight(height, kept)) return false;
  m_chain.entries.erase(m_chain.entries.upper_bound(height), m_chain.entries.end());
  m_chain.bestTipHeight = height;
  m_chain.bestTipHash = kept.hashDisplay();
  return true;
}

long double SpvHeaderStore::tipWork() const {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  return cumulativeWorkAt(m_chain.bestTipHeight);
}

bool SpvHeaderStore::lowestRewindHeight(uint64_t& height) const {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (m_chain.entries.empty()) return false;
  height = std::max(m_chain.entries.begin()->first, m_hasCheckpoint ? m_checkpointHeight : uint64_t{0});
  return true;
}

// Remove headers older than the retention limit.
void SpvHeaderStore::pruneOldHeaders() {
  if (m_maxHeightDelta == 0) {
    return; // no pruning
  }
  // Determine the lowest height to keep
  uint64_t keepMinHeight = 0;
  if (m_chain.bestTipHeight > m_maxHeightDelta) {
    keepMinHeight = m_chain.bestTipHeight - m_maxHeightDelta;
  }
  // Remove all entries with height < keepMinHeight
  for (auto it = m_chain.entries.begin(); it != m_chain.entries.end(); ) {
    if (it->first < keepMinHeight) {
      it = m_chain.entries.erase(it);
    } else {
      ++it;
    }
  }
}

} // namespace XfgSwap
