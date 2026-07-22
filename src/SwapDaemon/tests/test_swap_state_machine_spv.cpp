// Copyright (c) 2017-2026 Fuego Developers
//
// Tests for ADAPTOR_WAITING_SPV / ADAPTOR_SECRET_CONFIRMED_SPV state transitions.

#include <iostream>
#include <cstring>
#include "SwapDaemon/SwapTypes.h"
#include "SwapDaemon/SwapStateMachine.h"

using namespace XfgSwap;

static bool test_spv_waiting_to_confirmed() {
  std::cout << "  test_spv_waiting_to_confirmed... ";

  SwapParams params;
  params.swapId = "test01";
  params.pair = SwapPair::BCH;
  params.role = SwapRole::BOB;
  params.requiredConfirmations = 6;

  SwapStateMachine sm(params);
  // Fast-forward to ADAPTOR_SECRET_REVEALED (14)
  sm.transition(SwapState::ADAPTOR_KEYS_EXCHANGED);
  sm.transition(SwapState::ADAPTOR_ESCROW_FUNDED);
  sm.transition(SwapState::ADAPTOR_PRESIGS_READY);
  sm.transition(SwapState::ADAPTOR_CTR_LOCKED);
  sm.transition(SwapState::ADAPTOR_SECRET_REVEALED);

  // Transition to ADAPTOR_WAITING_SPV
  if (!sm.transition(SwapState::ADAPTOR_WAITING_SPV)) {
    std::cout << "FAIL: cannot transition to ADAPTOR_WAITING_SPV\n";
    return false;
  }
  if (sm.currentState() != SwapState::ADAPTOR_WAITING_SPV) {
    std::cout << "FAIL: state is not ADAPTOR_WAITING_SPV\n";
    return false;
  }

  // Transition to ADAPTOR_SECRET_CONFIRMED_SPV (simulating SPV confirmed)
  if (!sm.transition(SwapState::ADAPTOR_SECRET_CONFIRMED_SPV)) {
    std::cout << "FAIL: cannot transition to ADAPTOR_SECRET_CONFIRMED_SPV\n";
    return false;
  }
  if (sm.currentState() != SwapState::ADAPTOR_SECRET_CONFIRMED_SPV) {
    std::cout << "FAIL: state is not ADAPTOR_SECRET_CONFIRMED_SPV\n";
    return false;
  }

  // Transition to ADAPTOR_XFG_SPENT (terminal)
  if (!sm.transition(SwapState::ADAPTOR_XFG_SPENT)) {
    std::cout << "FAIL: cannot transition to ADAPTOR_XFG_SPENT\n";
    return false;
  }
  if (!sm.isTerminal()) {
    std::cout << "FAIL: ADAPTOR_XFG_SPENT should be terminal\n";
    return false;
  }

  std::cout << "PASS\n";
  return true;
}

static bool test_spv_waiting_stays_on_retry() {
  std::cout << "  test_spv_waiting_stays_on_retry... ";

  SwapParams params;
  params.swapId = "test02";
  params.pair = SwapPair::BCH;
  params.role = SwapRole::BOB;
  params.requiredConfirmations = 6;

  SwapStateMachine sm(params);
  sm.transition(SwapState::ADAPTOR_KEYS_EXCHANGED);
  sm.transition(SwapState::ADAPTOR_ESCROW_FUNDED);
  sm.transition(SwapState::ADAPTOR_PRESIGS_READY);
  sm.transition(SwapState::ADAPTOR_CTR_LOCKED);
  sm.transition(SwapState::ADAPTOR_SECRET_REVEALED);
  sm.transition(SwapState::ADAPTOR_WAITING_SPV);

  // Cannot transition back to ADAPTOR_WAITING_SPV (same state)
  if (sm.transition(SwapState::ADAPTOR_WAITING_SPV)) {
    std::cout << "FAIL: should not be able to transition to same state\n";
    return false;
  }

  // State should remain ADAPTOR_WAITING_SPV
  if (sm.currentState() != SwapState::ADAPTOR_WAITING_SPV) {
    std::cout << "FAIL: state changed unexpectedly\n";
    return false;
  }

  std::cout << "PASS\n";
  return true;
}

static bool test_spv_waiting_to_refunded() {
  std::cout << "  test_spv_waiting_to_refunded... ";

  SwapParams params;
  params.swapId = "test03";
  params.pair = SwapPair::BCH;
  params.role = SwapRole::BOB;
  params.xfgTimeoutHeight = 100;
  params.requiredConfirmations = 6;

  SwapStateMachine sm(params);
  sm.transition(SwapState::ADAPTOR_KEYS_EXCHANGED);
  sm.transition(SwapState::ADAPTOR_ESCROW_FUNDED);
  sm.transition(SwapState::ADAPTOR_PRESIGS_READY);
  sm.transition(SwapState::ADAPTOR_CTR_LOCKED);
  sm.transition(SwapState::ADAPTOR_SECRET_REVEALED);
  sm.transition(SwapState::ADAPTOR_WAITING_SPV);

  // Timeout: transition to ADAPTOR_REFUNDED with sufficient height
  if (!sm.transition(SwapState::ADAPTOR_REFUNDED, 200)) {
    std::cout << "FAIL: cannot transition to ADAPTOR_REFUNDED\n";
    return false;
  }
  if (sm.currentState() != SwapState::ADAPTOR_REFUNDED) {
    std::cout << "FAIL: state is not ADAPTOR_REFUNDED\n";
    return false;
  }

  std::cout << "PASS\n";
  return true;
}

static bool test_spv_waiting_to_failed() {
  std::cout << "  test_spv_waiting_to_failed... ";

  SwapParams params;
  params.swapId = "test04";
  params.pair = SwapPair::BCH;
  params.role = SwapRole::BOB;
  params.requiredConfirmations = 6;

  SwapStateMachine sm(params);
  sm.transition(SwapState::ADAPTOR_KEYS_EXCHANGED);
  sm.transition(SwapState::ADAPTOR_ESCROW_FUNDED);
  sm.transition(SwapState::ADAPTOR_PRESIGS_READY);
  sm.transition(SwapState::ADAPTOR_CTR_LOCKED);
  sm.transition(SwapState::ADAPTOR_SECRET_REVEALED);
  sm.transition(SwapState::ADAPTOR_WAITING_SPV);

  if (!sm.transition(SwapState::FAILED)) {
    std::cout << "FAIL: cannot transition to FAILED\n";
    return false;
  }
  if (sm.currentState() != SwapState::FAILED) {
    std::cout << "FAIL: state is not FAILED\n";
    return false;
  }

  std::cout << "PASS\n";
  return true;
}

static bool test_secret_revealed_can_skip_spv() {
  std::cout << "  test_secret_revealed_can_skip_spv... ";

  SwapParams params;
  params.swapId = "test05";
  params.pair = SwapPair::SOL;  // SPV not available for SOL
  params.role = SwapRole::BOB;
  params.requiredConfirmations = 6;

  SwapStateMachine sm(params);
  sm.transition(SwapState::ADAPTOR_KEYS_EXCHANGED);
  sm.transition(SwapState::ADAPTOR_ESCROW_FUNDED);
  sm.transition(SwapState::ADAPTOR_PRESIGS_READY);
  sm.transition(SwapState::ADAPTOR_CTR_LOCKED);
  sm.transition(SwapState::ADAPTOR_SECRET_REVEALED);

  // Can go directly to ADAPTOR_XFG_SPENT (skip SPV)
  if (!sm.transition(SwapState::ADAPTOR_XFG_SPENT)) {
    std::cout << "FAIL: cannot skip SPV and go to ADAPTOR_XFG_SPENT\n";
    return false;
  }
  if (sm.currentState() != SwapState::ADAPTOR_XFG_SPENT) {
    std::cout << "FAIL: state is not ADAPTOR_XFG_SPENT\n";
    return false;
  }

  std::cout << "PASS\n";
  return true;
}

static bool test_confirmed_spv_to_xfg_spent() {
  std::cout << "  test_confirmed_spv_to_xfg_spent... ";

  SwapParams params;
  params.swapId = "test06";
  params.pair = SwapPair::BCH;
  params.role = SwapRole::BOB;
  params.requiredConfirmations = 6;

  SwapStateMachine sm(params);
  sm.transition(SwapState::ADAPTOR_KEYS_EXCHANGED);
  sm.transition(SwapState::ADAPTOR_ESCROW_FUNDED);
  sm.transition(SwapState::ADAPTOR_PRESIGS_READY);
  sm.transition(SwapState::ADAPTOR_CTR_LOCKED);
  sm.transition(SwapState::ADAPTOR_SECRET_REVEALED);
  sm.transition(SwapState::ADAPTOR_WAITING_SPV);
  sm.transition(SwapState::ADAPTOR_SECRET_CONFIRMED_SPV);

  if (!sm.transition(SwapState::ADAPTOR_XFG_SPENT)) {
    std::cout << "FAIL: cannot transition to ADAPTOR_XFG_SPENT\n";
    return false;
  }
  if (sm.currentState() != SwapState::ADAPTOR_XFG_SPENT) {
    std::cout << "FAIL: state is not ADAPTOR_XFG_SPENT\n";
    return false;
  }

  std::cout << "PASS\n";
  return true;
}

static bool test_confirmed_spv_to_refunded() {
  std::cout << "  test_confirmed_spv_to_refunded... ";

  SwapParams params;
  params.swapId = "test07";
  params.pair = SwapPair::BCH;
  params.role = SwapRole::BOB;
  params.xfgTimeoutHeight = 100;
  params.requiredConfirmations = 6;

  SwapStateMachine sm(params);
  sm.transition(SwapState::ADAPTOR_KEYS_EXCHANGED);
  sm.transition(SwapState::ADAPTOR_ESCROW_FUNDED);
  sm.transition(SwapState::ADAPTOR_PRESIGS_READY);
  sm.transition(SwapState::ADAPTOR_CTR_LOCKED);
  sm.transition(SwapState::ADAPTOR_SECRET_REVEALED);
  sm.transition(SwapState::ADAPTOR_WAITING_SPV);
  sm.transition(SwapState::ADAPTOR_SECRET_CONFIRMED_SPV);

  if (!sm.transition(SwapState::ADAPTOR_REFUNDED, 200)) {
    std::cout << "FAIL: cannot transition to ADAPTOR_REFUNDED\n";
    return false;
  }
  if (sm.currentState() != SwapState::ADAPTOR_REFUNDED) {
    std::cout << "FAIL: state is not ADAPTOR_REFUNDED\n";
    return false;
  }

  std::cout << "PASS\n";
  return true;
}

static bool test_invalid_transitions() {
  std::cout << "  test_invalid_transitions... ";

  SwapParams params;
  params.swapId = "test08";
  params.pair = SwapPair::BCH;
  params.role = SwapRole::BOB;
  params.requiredConfirmations = 6;

  // Cannot go to ADAPTOR_WAITING_SPV from ADAPTOR_ESCROW_FUNDED
  {
    SwapStateMachine sm(params);
    sm.transition(SwapState::ADAPTOR_KEYS_EXCHANGED);
    sm.transition(SwapState::ADAPTOR_ESCROW_FUNDED);
    if (sm.transition(SwapState::ADAPTOR_WAITING_SPV)) {
      std::cout << "FAIL: ADAPTOR_ESCROW_FUNDED -> ADAPTOR_WAITING_SPV should be invalid\n";
      return false;
    }
  }

  // Cannot go to ADAPTOR_SECRET_CONFIRMED_SPV from ADAPTOR_SECRET_REVEALED
  {
    SwapStateMachine sm(params);
    sm.transition(SwapState::ADAPTOR_KEYS_EXCHANGED);
    sm.transition(SwapState::ADAPTOR_ESCROW_FUNDED);
    sm.transition(SwapState::ADAPTOR_PRESIGS_READY);
    sm.transition(SwapState::ADAPTOR_CTR_LOCKED);
    sm.transition(SwapState::ADAPTOR_SECRET_REVEALED);
    if (sm.transition(SwapState::ADAPTOR_SECRET_CONFIRMED_SPV)) {
      std::cout << "FAIL: ADAPTOR_SECRET_REVEALED -> ADAPTOR_SECRET_CONFIRMED_SPV should be invalid\n";
      return false;
    }
  }

  std::cout << "PASS\n";
  return true;
}

static bool test_state_string_mapping() {
  std::cout << "  test_state_string_mapping... ";

  if (std::strcmp(swapStateToString(SwapState::ADAPTOR_WAITING_SPV), "ADAPTOR_WAITING_SPV") != 0) {
    std::cout << "FAIL: ADAPTOR_WAITING_SPV string mismatch\n";
    return false;
  }
  if (std::strcmp(swapStateToString(SwapState::ADAPTOR_SECRET_CONFIRMED_SPV), "ADAPTOR_SECRET_CONFIRMED_SPV") != 0) {
    std::cout << "FAIL: ADAPTOR_SECRET_CONFIRMED_SPV string mismatch\n";
    return false;
  }

  std::cout << "PASS\n";
  return true;
}

static bool test_serialization_roundtrip() {
  std::cout << "  test_serialization_roundtrip... ";
  // Fail-closed serialize: live secrets require an encryption key.

  SwapParams params{};
  params.swapId = "test_serial";
  params.pair = SwapPair::BCH;
  params.role = SwapRole::BOB;
  params.requiredConfirmations = 6;
  // Value-init zeros secret pods so needEnc is false without a key; still set
  // a key so tests match production (enc key always present when persisting).
  SwapStateMachine sm(params);
  sm.setEncryptionKey("test-spv-serial-enc-key");
  sm.transition(SwapState::ADAPTOR_KEYS_EXCHANGED);
  sm.transition(SwapState::ADAPTOR_ESCROW_FUNDED);
  sm.transition(SwapState::ADAPTOR_PRESIGS_READY);
  sm.transition(SwapState::ADAPTOR_CTR_LOCKED);
  sm.transition(SwapState::ADAPTOR_SECRET_REVEALED);
  sm.transition(SwapState::ADAPTOR_WAITING_SPV);

  std::string serialized = sm.serialize();
  if (serialized.empty()) {
    std::cout << "FAIL: serialization returned empty string\n";
    return false;
  }

  SwapStateMachine restored = SwapStateMachine::deserialize(serialized);
  if (restored.currentState() != SwapState::ADAPTOR_WAITING_SPV) {
    std::cout << "FAIL: deserialized state is " << swapStateToString(restored.currentState())
              << ", expected ADAPTOR_WAITING_SPV\n";
    return false;
  }

  // Also test ADAPTOR_SECRET_CONFIRMED_SPV roundtrip
  sm.transition(SwapState::ADAPTOR_SECRET_CONFIRMED_SPV);
  serialized = sm.serialize();
  restored = SwapStateMachine::deserialize(serialized);
  if (restored.currentState() != SwapState::ADAPTOR_SECRET_CONFIRMED_SPV) {
    std::cout << "FAIL: deserialized state is " << swapStateToString(restored.currentState())
              << ", expected ADAPTOR_SECRET_CONFIRMED_SPV\n";
    return false;
  }

  std::cout << "PASS\n";
  return true;
}

int main() {
  std::cout << "=== SwapStateMachine SPV state tests ===\n\n";
  int pass = 0, total = 10;
  if (test_spv_waiting_to_confirmed())    ++pass;
  if (test_spv_waiting_stays_on_retry())  ++pass;
  if (test_spv_waiting_to_refunded())     ++pass;
  if (test_spv_waiting_to_failed())       ++pass;
  if (test_secret_revealed_can_skip_spv()) ++pass;
  if (test_confirmed_spv_to_xfg_spent())  ++pass;
  if (test_confirmed_spv_to_refunded())   ++pass;
  if (test_invalid_transitions())         ++pass;
  if (test_state_string_mapping())        ++pass;
  if (test_serialization_roundtrip())     ++pass;

  std::cout << "\n=== " << pass << "/" << total << " tests passed ===\n";
  return (pass == total) ? 0 : 1;
}
