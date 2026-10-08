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

#include "PriceWorker.h"

#include <chrono>

namespace XfgSwap {

PriceWorker::PriceWorker(PriceFeed* primary, PriceFeed* secondary, PricePolicy policy)
  : m_primary(primary), m_secondary(secondary), m_policy(std::move(policy)) {}

PriceWorker::~PriceWorker() {
  stop();
}

bool PriceWorker::secondaryDue(int64_t now) const {
  if (m_secondary == nullptr) return false;
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_polledSecondary) return true;
  const int64_t wanted = m_policy.pollIntervalSec > kSecondaryMinIntervalSec
                             ? static_cast<int64_t>(m_policy.pollIntervalSec)
                             : static_cast<int64_t>(kSecondaryMinIntervalSec);
  return now - m_lastSecondaryAttempt >= wanted;
}

bool PriceWorker::pollOnce(int64_t now) {
  // Each source is fetched AT MOST ONCE per cycle. Fetching twice would double the
  // CoinGecko request rate against a 3/minute keyless ceiling.
  bool fetchedPrimary = false;
  bool fetchedSecondary = false;
  FeedResult primaryResult;
  FeedResult secondaryResult;
  bool any = false;

  {
    std::lock_guard<std::mutex> lock(m_mutex);
    fetchedPrimary = (m_primary != nullptr) &&
                     (!m_polledPrimary ||
                      now - m_lastPrimaryAttempt >= static_cast<int64_t>(m_policy.pollIntervalSec));
  }
  if (fetchedPrimary) {
    primaryResult = m_primary->fetch(m_policy);
    fetchedPrimary = true;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_lastPrimaryAttempt = now;
    m_polledPrimary = true;
    if (primaryResult.transportOk) any = true;
  }

  // The secondary is rate limited, so it runs on its own longer floor and is never
  // retried early. A 429 simply leaves it skipped until it is next due.
  fetchedSecondary = secondaryDue(now);
  if (fetchedSecondary) {
    secondaryResult = m_secondary->fetch(m_policy);
    std::lock_guard<std::mutex> lock(m_mutex);
    m_lastSecondaryAttempt = now;
    m_polledSecondary = true;
    if (secondaryResult.transportOk) {
      m_secondaryFailures = 0;
      any = true;
    } else {
      ++m_secondaryFailures;
    }
  }

  // Publish this cycle, carrying forward the last good value for a source that was not
  // due. A cycle where only one side answered still publishes, so a primary outage
  // cannot blank the secondary or the reverse.
  auto snap = std::make_shared<PriceSnapshot>();
  snap->takenAt = now;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_latest) {
      snap->primary   = m_latest->primary;
      snap->secondary = m_latest->secondary;
    }
  }
  if (fetchedPrimary)   snap->primary   = primaryResult;
  if (fetchedSecondary) snap->secondary = secondaryResult;

  {
    std::lock_guard<std::mutex> lock(m_mutex);
    ++m_cycles;
    m_latest = snap;
    m_history.push_back(*snap);
    while (m_history.size() > kMaxHistory) m_history.pop_front();
  }
  return any;
}

bool PriceWorker::snapshot(PriceSnapshot& out) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_latest) return false;
  out = *m_latest;
  return true;
}

void PriceWorker::start() {
  if (m_primary == nullptr && m_secondary == nullptr) return;
  if (m_running.exchange(true)) return;
  m_thread = std::thread(&PriceWorker::loop, this);
}

void PriceWorker::stop() {
  if (!m_running.exchange(false)) return;
  if (m_thread.joinable()) m_thread.join();
}

void PriceWorker::loop() {
  int64_t last = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::system_clock::now().time_since_epoch()).count());

  // Wake at least once a second so stop() is responsive even with a long interval.
  const uint32_t tick = m_policy.pollIntervalSec < 5 ? 1 : 5;

  while (m_running.load()) {
    const int64_t now = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    if (now != last) {
      last = now;
      pollOnce(now);
    }
    std::this_thread::sleep_for(std::chrono::seconds(tick));
  }
}

} // namespace XfgSwap