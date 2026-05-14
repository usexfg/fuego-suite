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

#include "SwapStateMachine.h"
#include <string>
#include <vector>
#include <mutex>
#include <functional>

namespace XfgSwap {

// File-based swap persistence.
// Each swap is stored as a JSON file in <dataDir>/swaps/<swap_id>.json
//
// All public operations acquire m_mutex, serializing tick-thread and
// user-command access to the same swap files. Atomic rename alone is
// not sufficient: a concurrent load+save pair from two threads can
// lose updates (stale copy overwriting a newer state).
class SwapDatabase {
public:
  explicit SwapDatabase(const std::string& dataDir);

  // Save swap state to disk. Creates or overwrites the file.
  bool saveSwap(const SwapStateMachine& sm);

  // Load swap state from disk by swap ID.
  bool loadSwap(const std::string& swapId, SwapStateMachine& sm);

  // List all swap IDs stored on disk.
  std::vector<std::string> listSwaps();

  // Delete a swap file from disk.
  bool deleteSwap(const std::string& swapId);

  // Atomically load a swap, mutate it via fn, and save the result.
  // fn must return true on success; if false, no save occurs.
  // The mutex is held across load+mutate+save so no concurrent writer
  // can clobber the update. Returns false on load/mutate/save failure.
  bool updateSwap(const std::string& swapId,
                  const std::function<bool(SwapStateMachine&)>& fn);

  // Get the data directory path.
  const std::string& dataDir() const;

private:
  // Get the full path to a swap's JSON file.
  std::string swapFilePath(const std::string& swapId) const;

  // Ensure the swaps directory exists.
  bool ensureDirectory();

  // Unlocked helpers (must be called with m_mutex held).
  bool saveSwapLocked(const SwapStateMachine& sm);
  bool loadSwapLocked(const std::string& swapId, SwapStateMachine& sm);

  std::string m_dataDir;
  std::string m_swapsDir;
  mutable std::mutex m_mutex;
};

} // namespace XfgSwap
