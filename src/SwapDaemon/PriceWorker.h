// Copyright (c) 2017-2026, Fuego Developers
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

#include "PriceFeed.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

namespace XfgSwap {

// One coherent read of both feeds. Primary is DeFiLlama; secondary is CoinGecko.
struct PriceSnapshot {
  int64_t    takenAt = 0;
  FeedResult primary;
  FeedResult secondary;
};

// Bounded poll worker.
//
// It exists because a DeFiLlama /prices/current call can block for the read timeout,
// and the swap-progress thread must never wait on the network. The worker polls on its
// own thread and publishes immutable snapshots; readers copy one out in microseconds.
//
// Two things it deliberately does not do:
//   * It never retries a rate-limited source inside the window. CoinGecko's keyless
//     tier allows 3 requests/minute and a retry extends the block, so a failing source
//     is skipped until its next due time instead of being hammered.
//   * It never requires both sources. A secondary failure degrades the quote to
//     SINGLE_SOURCE, which still permits trading, rather than halting the daemon.
class PriceWorker {
public:
  // CoinGecko refuses a 4th request inside a minute on the keyless tier, so the
  // secondary is floored at 60s regardless of policy.pollIntervalSec (30s default).
  static constexpr uint32_t kSecondaryMinIntervalSec = 60;
  static constexpr size_t   kMaxHistory = 8;

  PriceWorker(PriceFeed* primary, PriceFeed* secondary, PricePolicy policy);
  ~PriceWorker();

  PriceWorker(const PriceWorker&) = delete;
  PriceWorker& operator=(const PriceWorker&) = delete;

  void start();
  void stop();

  // One cycle. Public so tests can drive the worker with no thread and no network.
  // Returns true if at least one source answered.
  bool pollOnce(int64_t now);

  // Copy the latest snapshot out. Cheap; never touches the network.
  bool snapshot(PriceSnapshot& out) const;

  bool secondaryDue(int64_t now) const;
  size_t historySize() const { return m_history.size(); }
  uint32_t consecutiveSecondaryFailures() const { return m_secondaryFailures; }
  uint64_t cycles() const { return m_cycles; }

private:
  void loop();

  PriceFeed*   m_primary;
  PriceFeed*   m_secondary;
  PricePolicy  m_policy;

  std::atomic<bool> m_running{false};
  std::thread        m_thread;

  mutable std::mutex                       m_mutex;
  std::shared_ptr<const PriceSnapshot>      m_latest;
  std::deque<PriceSnapshot>                 m_history;
  // Explicit "never polled" flags rather than sentinel timestamps: subtracting from
  // INT64_MIN overflows and made every due-check fail.
  bool     m_polledPrimary       = false;
  bool     m_polledSecondary     = false;
  int64_t  m_lastPrimaryAttempt   = 0;
  int64_t  m_lastSecondaryAttempt = 0;
  uint32_t m_secondaryFailures    = 0;
  uint64_t m_cycles               = 0;
};

} // namespace XfgSwap