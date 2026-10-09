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

// Tests for the bounded poll worker.
//
// The worker is driven synchronously through pollOnce() with stub feeds, so these cover
// the scheduling and degradation logic with no thread and no network. The behaviour that
// matters most here is that a rate-limited or failing second source degrades the quote to
// SINGLE_SOURCE and keeps trading alive, rather than halting the daemon.

#include "PriceWorker.h"

#include <cstdio>
#include <string>

using namespace XfgSwap;

static int g_failures = 0;
static int g_checks = 0;

static void check(bool cond, const std::string& what) {
  ++g_checks;
  if (!cond) { ++g_failures; std::printf("FAIL  %s\n", what.c_str()); }
}

// Stub feed with a scripted outcome and a call counter.
class StubFeed : public PriceFeed {
public:
  const char* name() const override { return m_name.c_str(); }
  std::vector<std::string> assets() const override { return {}; }

  FeedResult fetch(const PricePolicy&) override {
    ++calls;
    FeedResult r;
    r.feedName = m_name;
    if (!fail) {
      r.transportOk = true;
      AssetPrice ap;
      ap.ok = true;
      ap.observation.price = price;
      ap.observation.providerTimestamp = stamp;
      ap.observation.fetchTimestamp = stamp;
      r.assets["ETH"] = ap;
      r.assets["BTC"] = ap;
    } else {
      r.transportOk = false;
      r.transportReason = reason;
    }
    return r;
  }

  std::string m_name = "stub";
  bool   fail = false;
  double price = 2583.12;
  int64_t stamp = 0;
  std::string reason = "HTTP 429 rate limited";
  int    calls = 0;
};

static PriceSnapshot feedWith(const std::string& feedName, double price, int64_t stamp) {
  PriceSnapshot s;
  s.primary.feedName = feedName;
  s.primary.transportOk = true;
  AssetPrice ap;
  ap.ok = true;
  ap.observation.price = price;
  ap.observation.providerTimestamp = stamp;
  ap.observation.fetchTimestamp = stamp;
  s.primary.assets["ETH"] = ap;
  return s;
}

int main() {
  const int64_t t0 = 1791423568;

  // ---- no snapshot before the first poll.
  {
    StubFeed p, s;
    PriceWorker w(&p, &s, PricePolicy());
    PriceSnapshot out;
    check(!w.snapshot(out), "a fresh worker has no snapshot");
    check(w.historySize() == 0, "and no history");
  }

  // ---- both sources answer: one fetch each, corroborated quote.
  {
    StubFeed p, s;
    p.m_name = "defillama"; p.price = 2583.12; p.stamp = t0 - 30;
    s.m_name = "coingecko"; s.price = 2584.00; s.stamp = t0 - 30;
    PricePolicy pol;
    pol.pollIntervalSec = 30;
    PriceWorker w(&p, &s, pol);

    check(w.pollOnce(t0), "a cycle where both answer reports success");
    check(p.calls == 1, "primary fetched exactly once");
    check(s.calls == 1, "secondary fetched exactly once");
    check(w.cycles() == 1, "one cycle counted");

    PriceSnapshot snap;
    check(w.snapshot(snap), "a snapshot is available");
    check(snap.primary.transportOk && snap.secondary.transportOk, "both sides captured");

    PairQuote q = buildPairQuote(1, snap.primary, &snap.secondary, 0.158, pol, t0);
    check(q.status == QuoteStatus::OK, "the quote is corroborated, not single-source");
  }

  // ---- a rate-limited secondary degrades to SINGLE_SOURCE and keeps trading alive.
  {
    StubFeed p, s;
    p.m_name = "defillama"; p.price = 2583.12; p.stamp = t0 - 30;
    s.m_name = "coingecko"; s.fail = true; s.reason = "HTTP 429 rate limited";
    PricePolicy pol;
    PriceWorker w(&p, &s, pol);

    check(w.pollOnce(t0), "the cycle still succeeds on the primary alone");
    check(w.consecutiveSecondaryFailures() == 1, "the secondary failure is counted");

    PriceSnapshot snap;
    w.snapshot(snap);
    check(snap.primary.transportOk, "primary data is present");
    check(!snap.secondary.transportOk, "secondary reports its failure");

    PairQuote q = buildPairQuote(1, snap.primary, &snap.secondary, 0.158, pol, t0);
    check(q.status == QuoteStatus::SINGLE_SOURCE, "the quote degrades to single-source");
    check(q.managedOffersAllowed, "managed offers still allowed");
    check(q.newSwapAllowed, "new swaps still allowed -- a 429 must not halt trading");
    check(q.rateNum > 0, "and a real rate is still produced");
  }

  // ---- the failing secondary is NOT retried before its floor.
  {
    StubFeed p, s;
    p.m_name = "defillama"; p.price = 2583.12; p.stamp = t0 - 30;
    s.m_name = "coingecko"; s.fail = true;
    PricePolicy pol;
    pol.pollIntervalSec = 30;   // below the CoinGecko floor on purpose
    PriceWorker w(&p, &s, pol);

    w.pollOnce(t0);
    check(s.calls == 1, "secondary attempted once");

    // Inside the 60s floor: no second attempt, which is what stops the 429 from extending.
    for (int64_t t = t0 + 1; t <= t0 + 59; t += 10) {
      w.pollOnce(t);
      p.stamp = t - 5;
    }
    check(s.calls == 1, "no secondary retry inside the 60s floor");
    check(w.consecutiveSecondaryFailures() == 1, "and the failure count did not inflate");

    // Past the floor it is tried again.
    w.pollOnce(t0 + 61);
    check(s.calls == 2, "the secondary is retried once its floor passes");
  }

  // ---- the primary keeps its shorter policy interval.
  {
    StubFeed p, s;
    p.m_name = "defillama"; p.stamp = t0; p.price = 2583.12;
    s.m_name = "coingecko"; s.stamp = t0; s.price = 2584.0;
    PricePolicy pol;
    pol.pollIntervalSec = 30;
    PriceWorker w(&p, &s, pol);
    w.pollOnce(t0);
    w.pollOnce(t0 + 31);
    check(p.calls == 2, "primary repolls at the 30s policy interval");
    check(s.calls == 1, "secondary has not reached its 60s floor yet");
  }

  // ---- a primary outage must not blank a healthy secondary.
  {
    StubFeed p, s;
    p.m_name = "defillama"; p.fail = true;
    s.m_name = "coingecko"; s.price = 2584.0; s.stamp = t0 - 30;
    PricePolicy pol;
    PriceWorker w(&p, &s, pol);
    // pollOnce reports true if EITHER source answered, so a primary outage with a
    // healthy secondary is still a productive cycle.
    check(w.pollOnce(t0), "a cycle where only the secondary answers still counts as productive");

    PriceSnapshot snap;
    w.snapshot(snap);
    check(!snap.primary.transportOk, "primary failure is visible");
    check(snap.secondary.transportOk, "secondary data survives the primary outage");
  }

  // ---- history is bounded.
  {
    StubFeed p, s;
    p.stamp = t0; s.stamp = t0;
    PricePolicy pol;
    PriceWorker w(&p, &s, pol);
    const int64_t span = 40 * 60;
    const int64_t step = 30;
    const int64_t expected = span / step;   // 80 cycles over 40 minutes
    for (int64_t t = t0; t < t0 + span; t += step) {
      p.stamp = t; s.stamp = t; s.price = 2500.0 + (t - t0);
      w.pollOnce(t);
    }
    check(w.historySize() == PriceWorker::kMaxHistory, "history is capped at 8");
    check(w.cycles() == static_cast<uint64_t>(expected),
          "every cycle was counted (" + std::to_string(expected) + ")");
  }

  // ---- a returned snapshot is a copy, so a reader cannot corrupt the worker.
  {
    StubFeed p, s;
    p.stamp = t0; s.stamp = t0;
    PriceWorker w(&p, &s, PricePolicy());
    w.pollOnce(t0);
    PriceSnapshot a, b;
    w.snapshot(a);
    a.primary.assets.clear();
    a.takenAt = 999;
    w.snapshot(b);
    check(!b.primary.assets.empty(), "mutating a returned snapshot does not affect the worker");
    check(b.takenAt == t0, "and neither does its timestamp");
  }

  // ---- recovery: the secondary coming back clears the failure count and re-corroborates.
  {
    StubFeed p, s;
    p.m_name = "defillama"; p.price = 2583.12; p.stamp = t0 - 30;
    s.m_name = "coingecko"; s.fail = true;
    PricePolicy pol;
    PriceWorker w(&p, &s, pol);
    w.pollOnce(t0);
    w.pollOnce(t0 + 61);
    check(w.consecutiveSecondaryFailures() == 2, "two failures counted");

    s.fail = false; s.price = 2584.0; s.stamp = t0 + 61;
    w.pollOnce(t0 + 122);
    check(w.consecutiveSecondaryFailures() == 0, "recovery resets the failure count");

    PriceSnapshot snap;
    w.snapshot(snap);
    PairQuote q = buildPairQuote(1, snap.primary, &snap.secondary, 0.158, pol, t0 + 122);
    check(q.status == QuoteStatus::OK, "and quotes are corroborated again");
  }

  // ---- an absent secondary is tolerated.
  {
    StubFeed p;
    p.m_name = "defillama"; p.price = 2583.12; p.stamp = t0 - 30;
    PricePolicy pol;
    PriceWorker w(&p, nullptr, pol);
    check(w.pollOnce(t0), "a primary-only worker still polls");
    PriceSnapshot snap;
    w.snapshot(snap);
    PairQuote q = buildPairQuote(1, snap.primary, nullptr, 0.158, pol, t0);
    check(q.status == QuoteStatus::SINGLE_SOURCE, "and yields a single-source quote");
  }

  // ---- start()/stop() are safe with no sources and do not hang.
  {
    PriceWorker w(nullptr, nullptr, PricePolicy());
    w.start();
    w.stop();
    check(true, "start/stop on an empty worker does not hang");
  }

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}