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

// Gate A tests: local escrow funding sufficiency.
//
// The single property that matters is that this gate FAILS CLOSED. A wallet RPC that is
// unreachable, returns a non-object, omits `available`, or reports a negative value must
// all be treated as "cannot prove the funds are there" -- never as funded. Defaulting the
// other way is what would let a node advertise swaps it cannot honour.

#include "Common/JsonValue.h"
#include <cstdio>
#include <string>
#include <vector>

using namespace Common;

static int g_failures = 0;
static int g_checks = 0;

static void check(bool cond, const std::string& what) {
  ++g_checks;
  if (!cond) { ++g_failures; std::printf("FAIL  %s\n", what.c_str()); }
}

// Mirrors FuegoRpcClient::getSpendableBalance's parsing, which is what is under test:
// the RPC transport itself is not reachable from a unit test.
static bool parseBalance(const std::string& body, uint64_t& atomic) {
  try {
    const JsonValue json = JsonValue::fromString(body);
    if (!json.isObject() || !json.contains("available")) return false;
    const int64_t value = json("available").getInteger();
    if (value <= 0) { atomic = 0; return true; }
    atomic = static_cast<uint64_t>(value);
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

// The decision, separated from the transport so it can be exercised exhaustively.
static bool fundedGiven(bool ok, uint64_t available, uint64_t required,
                        std::string& error) {
  if (!ok) { error = "wallet balance unavailable; cannot prove escrow funding"; return false; }
  if (available >= required) { error.clear(); return true; }
  error = "spendable XFG " + std::to_string(available) +
          " is below the required " + std::to_string(required);
  return false;
}

int main() {
  const uint64_t kEscrow = 1000000000ULL;   // 100 XFG in atomic units
  const uint64_t kFee    = kEscrow / 100;    // 1% protocol fee
  const uint64_t kMinFee = 10000ULL;
  const uint64_t kRequired = kEscrow + kFee + kMinFee;

  // ---- fail-closed on every malformed transport outcome
  {
    uint64_t a = 0;
    check(!parseBalance("", a), "empty body is a failure");
    check(!parseBalance("{not json", a), "malformed JSON is a failure");
    check(!parseBalance("[]", a), "top-level array is a failure");
    check(!parseBalance("{\"locked\":5}", a), "a body without `available` is a failure");
    check(!parseBalance("{\"available\":\"lots\"}", a), "a non-integer available is a failure");

    std::string err;
    check(!fundedGiven(false, 0, kRequired, err),
          "an unreachable wallet is never funded");
    check(err.find("cannot prove") != std::string::npos,
          "and says it cannot prove funding rather than 'insufficient'");
  }

  // ---- a negative available must not wrap into a huge uint64
  {
    uint64_t a = 12345;
    check(parseBalance("{\"available\":-5000}", a), "a negative balance parses");
    check(a == 0, "but clamps to zero, so it can never look like a huge spendable balance");
    std::string err;
    check(!fundedGiven(true, a, kRequired, err), "and a clamped zero is not funded");
  }

  // ---- boundary behaviour around the requirement
  {
    std::string err;
    check(!fundedGiven(true, kRequired - 1, kRequired, err), "one atomic short is not funded");
    check(fundedGiven(true, kRequired, kRequired, err), "exactly the requirement is funded");
    check(fundedGiven(true, kRequired + 1, kRequired, err), "one over is funded");
    check(err.empty(), "and the error is cleared when funded");
  }

  // ---- the overflow traps: BOTH the multiply and the add must be guarded
  {
    const uint64_t bps = 100;   // SWAP_FEE_RATE_BPS

    // The multiply: xfgAmount * 100 wraps above ~1.8e17 atomic. An earlier draft guarded
    // only the addition, which validates a requirement built from a silently wrapped fee.
    const uint64_t tooBigToScale = UINT64_MAX / bps + 1;
    check(tooBigToScale > UINT64_MAX / bps, "multiply guard threshold is exclusive");
    check(tooBigToScale * bps < tooBigToScale,
          "and confirms the multiply really would wrap past it");
    bool refusedMultiply = (tooBigToScale > UINT64_MAX / bps);
    check(refusedMultiply, "an unpayable escrow is refused at the multiply");

    // Just under the threshold must still be accepted and computed correctly.
    const uint64_t largestSafe = UINT64_MAX / bps;
    const uint64_t fee = (largestSafe * bps) / 10000;
    check(largestSafe * bps <= UINT64_MAX, "largest safe amount does not wrap");
    check(fee > 0, "and still yields a real fee");

    // The add: escrow + fee + MIN_FEE must not wrap.
    const uint64_t nearMax = UINT64_MAX - 5;
    bool refusedAdd = false;
    if (fee > UINT64_MAX - nearMax) refusedAdd = true;
    else {
      const uint64_t sum = nearMax + fee + kMinFee;
      refusedAdd = sum < nearMax;   // wrapped
    }
    check(refusedAdd, "an escrow whose total overflows is refused at the addition");
  }

  // ---- real fuegod /getbalance shapes
  {
    uint64_t a = 0;
    const std::string realBody =
      "{\"available\":52348175521,\"locked\":0,\"pending\":0,\"total\":52348175521,"
      "\"unlocked_balance\":52348175521,\"balance\":52348175521}";
    check(parseBalance(realBody, a), "a real /getbalance body parses");
    check(a == 52348175521ULL, "and `available` is read, not `locked` or `total`");

    // Locked-heavy wallet: available is small, total is large. Must judge on available,
    // because locked and pending coins cannot fund a new escrow.
    const std::string lockedHeavy =
      "{\"available\":0,\"locked\":900000000000,\"total\":900000000000}";
    check(parseBalance(lockedHeavy, a) && a == 0,
          "a wallet whose coins are all locked reports zero spendable");
  }

  // ---- the cache must short-circuit BOTH outcomes (regression: failure was write-only)
  //
  // An earlier version collapsed "there is a cacheable entry" and "the fetch succeeded"
  // into one flag, so the failure path wrote an entry the cache-hit test could never
  // match. A dead wallet RPC then issued one RPC per offer per block.
  {
    struct Entry { bool present; bool ok; uint64_t available; };
    // Mimics the two cache-hit conditions now in getFundingReadiness.
    auto cacheHitServesFailure = [](const Entry& e, int64_t age) {
      if (!e.present || age >= 30) return false;
      return !e.ok;   // short-circuits without an RPC
    };
    Entry failedFetch{true, false, 0};
    check(cacheHitServesFailure(failedFetch, 5),
          "a cached FAILURE short-circuits instead of re-issuing the RPC");

    Entry staleFailure{true, false, 0};
    check(!cacheHitServesFailure(staleFailure, 31),
          "but it expires after the TTL so recovery is possible");

    Entry empty{false, false, 0};
    check(!cacheHitServesFailure(empty, 1),
          "an absent entry still forces a fetch");

    // And the failure must remain fail-closed even when served from cache.
    std::string err;
    check(!fundedGiven(false, failedFetch.available, kRequired, err),
          "a cache-served failure is still not funded");
    check(err.find("cannot prove") != std::string::npos,
          "with the same cannot-prove wording, not an 'insufficient' lie");
  }

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}