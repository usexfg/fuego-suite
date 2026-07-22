// Copyright (c) 2017-2026 Fuego Developers
//
// XFG spend path state-machine gates (no live node):
//   ADAPTOR_SECRET_REVEALED → ADAPTOR_XFG_SPENT
//   ADAPTOR_SECRET_CONFIRMED_SPV → ADAPTOR_XFG_SPENT
//   serialization of terminal ADAPTOR_XFG_SPENT with enc key

#include <cstring>
#include <iostream>
#include "SwapDaemon/SwapStateMachine.h"
#include "SwapDaemon/SwapTypes.h"

using namespace XfgSwap;

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) { ++g_pass; std::cout << "  PASS: " << m << "\n"; } \
  else { ++g_fail; std::cerr << "  FAIL: " << m << "\n"; } } while (0)

static bool advanceTo(SwapStateMachine& sm, SwapState target) {
  // Linear happy-path from INITIATED
  const SwapState path[] = {
    SwapState::ADAPTOR_KEYS_EXCHANGED,
    SwapState::ADAPTOR_ESCROW_FUNDED,
    SwapState::ADAPTOR_PRESIGS_READY,
    SwapState::ADAPTOR_CTR_LOCKED,
    SwapState::ADAPTOR_SECRET_REVEALED,
    SwapState::ADAPTOR_WAITING_SPV,
    SwapState::ADAPTOR_SECRET_CONFIRMED_SPV,
    SwapState::ADAPTOR_XFG_SPENT,
  };
  for (auto s : path) {
    if (sm.currentState() == target) return true;
    if (sm.currentState() == s) continue;
    // Only transition if next
    if (!sm.transition(s)) {
      // SPV branch optional: SECRET_REVEALED can go straight to XFG_SPENT
      if (sm.currentState() == SwapState::ADAPTOR_SECRET_REVEALED &&
          target == SwapState::ADAPTOR_XFG_SPENT) {
        return sm.transition(SwapState::ADAPTOR_XFG_SPENT);
      }
      return false;
    }
    if (sm.currentState() == target) return true;
  }
  return sm.currentState() == target;
}

int main() {
  std::cout << "=== XFG spend state machine gates ===\n";

  {
    SwapParams p{};
    p.swapId = "xfg-spend-1";
    p.pair = SwapPair::SOL;
    p.role = SwapRole::BOB;
    SwapStateMachine sm(p);
    sm.setEncryptionKey("xfg-spend-test-key");
    CHECK(sm.transition(SwapState::ADAPTOR_KEYS_EXCHANGED), "keys");
    CHECK(sm.transition(SwapState::ADAPTOR_ESCROW_FUNDED), "escrow");
    CHECK(sm.transition(SwapState::ADAPTOR_PRESIGS_READY), "presigs");
    CHECK(sm.transition(SwapState::ADAPTOR_CTR_LOCKED), "ctr locked");
    CHECK(sm.transition(SwapState::ADAPTOR_SECRET_REVEALED), "secret revealed");
    // Direct spend without SPV
    CHECK(sm.transition(SwapState::ADAPTOR_XFG_SPENT), "SECRET_REVEALED → XFG_SPENT");
    CHECK(sm.isTerminal(), "XFG_SPENT is terminal");
    auto json = sm.serialize();
    CHECK(!json.empty(), "serialize completed swap");
    auto loaded = SwapStateMachine::deserialize(json);
    CHECK(loaded.currentState() == SwapState::ADAPTOR_XFG_SPENT, "roundtrip terminal state");
  }

  {
    SwapParams p{};
    p.swapId = "xfg-spend-spv";
    p.pair = SwapPair::BCH;
    p.role = SwapRole::ALICE;
    p.useSpvVerification = true;
    SwapStateMachine sm(p);
    sm.setEncryptionKey("xfg-spend-test-key");
    CHECK(sm.transition(SwapState::ADAPTOR_KEYS_EXCHANGED), "spv keys");
    CHECK(sm.transition(SwapState::ADAPTOR_ESCROW_FUNDED), "spv escrow");
    CHECK(sm.transition(SwapState::ADAPTOR_PRESIGS_READY), "spv presigs");
    CHECK(sm.transition(SwapState::ADAPTOR_CTR_LOCKED), "spv ctr");
    CHECK(sm.transition(SwapState::ADAPTOR_SECRET_REVEALED), "spv secret");
    CHECK(sm.transition(SwapState::ADAPTOR_WAITING_SPV), "spv wait");
    CHECK(sm.transition(SwapState::ADAPTOR_SECRET_CONFIRMED_SPV), "spv confirmed");
    CHECK(sm.transition(SwapState::ADAPTOR_XFG_SPENT), "CONFIRMED_SPV → XFG_SPENT");
  }

  {
    // Invalid: cannot spend before secret
    SwapParams p{};
    p.swapId = "xfg-bad";
    SwapStateMachine sm(p);
    sm.transition(SwapState::ADAPTOR_KEYS_EXCHANGED);
    sm.transition(SwapState::ADAPTOR_ESCROW_FUNDED);
    sm.transition(SwapState::ADAPTOR_PRESIGS_READY);
    sm.transition(SwapState::ADAPTOR_CTR_LOCKED);
    CHECK(!sm.transition(SwapState::ADAPTOR_XFG_SPENT),
          "cannot XFG_SPENT before secret reveal");
  }

  std::cout << "\nResults: " << g_pass << " passed, " << g_fail << " failed\n";
  return g_fail == 0 ? 0 : 1;
}
