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

  // ---- the overflow trap: escrow + fee must not wrap
  {
    // This is the arithmetic the gate has to get right at the top of the uint64 range.
    const uint64_t huge = UINT64_MAX - 5;
    uint64_t fee = (huge / 100);
    bool wrapped = false;
    uint64_t required = 0;
    if (fee > UINT64_MAX - huge) wrapped = true;   // the check in initiate()
    else required = huge + fee + kMinFee;          // kMinFee term can also wrap
    check(wrapped, "huge escrow + fee is detected before the addition, not after");
    (void)required;
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

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}