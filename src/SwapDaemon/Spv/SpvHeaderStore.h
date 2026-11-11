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

#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <mutex>
#include "SpvHeader.h"

namespace XfgSwap {

// In-memory chain of validated block headers with checkpoint anchoring,
// cumulative-work best-chain selection, and reorg support.
//
// Usage:
//   SpvHeaderStore store;
//   store.anchor(0, "0000...0000");
//   store.addHeader(genesis);
//   store.addHeader(header1);
//   ...
//
// The store tracks the best chain tip and can answer height queries,
// merkle root lookups, and depth calculations.
//
// Old headers can be pruned automatically to limit memory usage.
// By default, no pruning is performed (retains all headers).
// Use setMaxHeightDelta() to limit how many blocks behind the tip are kept.
class SpvHeaderStore {
public:
  struct Checkpoint {
    uint64_t height;
    std::string hash;  // display (big-endian) hex
  };

  SpvHeaderStore();

  // Set the checkpoint anchor. Headers at or below checkpointHeight that
  // disagree with checkpointHash are rejected.  checkpointHash is in
  // display (big-endian) hex.
  bool anchor(uint64_t checkpointHeight, const std::string& checkpointHashDisplay);

  // Add a header that chains onto the current best tip.
  // Validates: link (prevHash == parent.hash()), PoW, checkpoint.
  // Returns true if header was accepted.
  bool addHeader(const SpvHeader& header);

  // Add a header at a specific height (for testing).
  // The header's prevHash must match an existing header hash at height-1.
  bool addHeaderAtHeight(const SpvHeader& header, uint64_t height);

  // Get the best tip height and hash (display hex).
  bool bestTip(uint64_t& height, std::string& hashDisplay) const;

  // Get the merkle root at a given height (internal LE bytes).
  bool merkleRootAt(uint64_t height, std::vector<uint8_t>& rootLE) const;

  // Get depth of a block height relative to current tip.
  // Returns 0 if height > tip height.
  uint32_t depthOf(uint64_t height) const;

  // Get header at height on the best chain.
  bool headerAtHeight(uint64_t height, SpvHeader& header) const;

  // Get the cumulative work up to and including the given height on the
  // best chain.  Used for fork comparison.
  long double cumulativeWorkAt(uint64_t height) const;

  // Set the maximum number of blocks to keep below the tip.
  // If depth > 0, headers older than (tipHeight - depth) will be removed.
  // If depth == 0, no pruning is performed (default).
  void setMaxHeightDelta(uint64_t depth);

private:
  struct ChainEntry {
    SpvHeader header;
    std::string hashDisplay;        // display hex of this header
    std::string prevHashDisplay;    // display hex of prevHash
    long double cumulativeWork = 0; // total work from genesis to here
  };

  // height → list of headers at that height (may be >1 during a fork)
  std::map<uint64_t, std::vector<ChainEntry>> m_entries;
  mutable std::recursive_mutex m_mutex;

  // Best chain state
  uint64_t m_bestTipHeight = 0;
  std::string m_bestTipHash;

  // Checkpoint
  uint64_t m_checkpointHeight = 0;
  std::string m_checkpointHash;
  bool m_hasCheckpoint = false;

  // Maximum number of blocks to keep below the tip (0 = keep all).
  uint64_t m_maxHeightDelta = 0;

  // Find a header by display hash across all heights.
  // Returns (height, index) or (-1, 0) if not found.
  std::pair<int64_t, size_t> findHeaderByHash(const std::string& hashDisplay) const;

  // Check if a given header hash belongs to the current best chain
  // at the given height.
  bool isOnBestChain(uint64_t height, const std::string& hashDisplay) const;

  // Rebuild the best chain tip after a potential reorg.
  void updateBestTip();

  // Remove headers older than the retention limit.
  void pruneOldHeaders();
};

} // namespace XfgSwap
