// Copyright (c) 2017-2026 Fuego Developers
//
// Tests for ADAPTOR_WAITING_SPV / ADAPTOR_SECRET_CONFIRMED_SPV state transitions.

#include <iostream>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <fstream>
#include "Common/JsonValue.h"
#include "SwapDaemon/SwapDatabase.h"
#include "SwapDaemon/SwapTypes.h"
#include "SwapDaemon/SwapStateMachine.h"
#include "SwapDaemon/SwapDaemon.h"
#include "Logging/ConsoleLogger.h"

using namespace XfgSwap;

namespace XfgSwap {
struct SwapDaemonTestAccess {
  static bool handlePeerMessage(SwapDaemon& daemon, const PeerMessage& message) {
    return daemon.handlePeerMessage(message);
  }
  static bool preparePeerIdentity(SwapParams& params) {
    return SwapDaemon::preparePeerIdentity(params);
  }
};
}

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
  params.ctrAmount = AtomicAmount(100) * 1000000000000000000ULL;
  params.requiredConfirmations = 6;
  params.useSpvVerification = true;
  params.ctrClaimAttempted = true;
  params.ctrClaimTxId = "claim-tx";
  params.ctrRefundAttempted = true;
  params.ctrRefundSubmitted = true;
  params.ctrRefundTxId = "refund-tx";
  params.escrowRefundBroadcast = true;
  params.escrowClaimTxHex = "signed-claim";
  params.escrowClaimTxId = "claim-id";
  params.escrow_claim_fee_report_attempted = true;
  params.escrow_refund_fee_report_attempted = true;
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
  if (restored.params().ctrAmount != params.ctrAmount ||
      restored.params().amountProtocolVersion != 2) {
    std::cout << "FAIL: full-width amount/version lost in serialization\n";
    return false;
  }
  if (restored.currentState() != SwapState::ADAPTOR_WAITING_SPV) {
    std::cout << "FAIL: deserialized state is " << swapStateToString(restored.currentState())
              << ", expected ADAPTOR_WAITING_SPV\n";
    return false;
  }
  if (!restored.params().useSpvVerification ||
      !restored.params().ctrClaimAttempted ||
      restored.params().ctrClaimTxId != "claim-tx" ||
      !restored.params().ctrRefundAttempted ||
      !restored.params().ctrRefundSubmitted ||
      restored.params().ctrRefundTxId != "refund-tx" ||
      !restored.params().escrowRefundBroadcast ||
      restored.params().escrowClaimTxHex != "signed-claim" ||
      restored.params().escrowClaimTxId != "claim-id" ||
      !restored.params().escrow_claim_fee_report_attempted ||
      !restored.params().escrow_refund_fee_report_attempted) {
    std::cout << "FAIL: refund and claim leg status lost on deserialize\n";
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

static bool test_legacy_claim_records_fail_closed() {
  std::cout << "  test_legacy_claim_records_fail_closed... ";
  SwapParams params{};
  params.swapId = "legacy-claim";
  params.pair = SwapPair::BCH;
  params.role = SwapRole::BOB;
  SwapStateMachine sm(params);
  sm.transition(SwapState::ADAPTOR_KEYS_EXCHANGED);
  sm.transition(SwapState::ADAPTOR_ESCROW_FUNDED);
  sm.transition(SwapState::ADAPTOR_PRESIGS_READY);
  sm.transition(SwapState::ADAPTOR_CTR_LOCKED);

  for (SwapState state : {SwapState::ADAPTOR_CTR_LOCKED,
                          SwapState::ADAPTOR_WAITING_SPV,
                          SwapState::ADAPTOR_SECRET_CONFIRMED_SPV}) {
    if (state != sm.currentState()) sm.transition(state);
    Common::JsonValue root = Common::JsonValue::fromString(sm.serialize());
    root.erase("ctrClaimAttempted");
    root.erase("useSpvVerification");
    const SwapStateMachine restored = SwapStateMachine::deserialize(root.toString());
    if (!restored.params().ctrClaimAttempted ||
        (state != SwapState::ADAPTOR_CTR_LOCKED &&
         !restored.params().useSpvVerification)) {
      std::cout << "FAIL: legacy record could authorize XFG refund\n";
      return false;
    }
  }
  std::cout << "PASS\n";
  return true;
}

static bool test_terminal_archive_masks_stale_active_record() {
  std::cout << "  test_terminal_archive_masks_stale_active_record... ";
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() /
      ("xfg-swap-archive-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  SwapDatabase db(dir.string());
  SwapParams params{};
  params.swapId = "archive-test";
  params.pair = SwapPair::BCH;
  params.role = SwapRole::BOB;
  params.xfgTimeoutHeight = 10;
  SwapStateMachine sm(params);
  sm.transition(SwapState::ADAPTOR_KEYS_EXCHANGED);
  sm.transition(SwapState::ADAPTOR_ESCROW_FUNDED);
  sm.transition(SwapState::ADAPTOR_PRESIGS_READY);
  sm.transition(SwapState::ADAPTOR_CTR_LOCKED);
  const bool activeSaved = db.saveSwap(sm);
  const std::string staleJson = sm.serialize();
  const bool terminalSaved = sm.transition(SwapState::ADAPTOR_REFUNDED, 11) &&
      db.saveSwap(sm);
  const fs::path active = dir / "swaps" / "archive-test.json";
  const fs::path archive = dir / "archive" / "archive-test.json";
  SwapStateMachine loaded;
  bool ok = activeSaved && terminalSaved && fs::exists(archive) &&
      !fs::exists(active) && !db.loadSwap(params.swapId, loaded);
  // Simulate a crash that left the old active file beside a committed archive.
  { std::ofstream out(active); out << staleJson; }
  SwapStateMachine stale = SwapStateMachine::deserialize(staleJson);
  ok = ok && !db.loadSwap(params.swapId, loaded) && db.listSwaps().empty() &&
      !db.saveSwap(stale);
  db.migrateTerminalSwaps();
  ok = ok && !fs::exists(active);
  fs::remove_all(dir);
  if (!ok) { std::cout << "FAIL\n"; return false; }
  std::cout << "PASS\n";
  return true;
}

static bool test_uint256_amount_survives_database_restart() {
  std::cout << "  test_uint256_amount_survives_database_restart... ";
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() /
      ("xfg-swap-uint256-restart-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  SwapParams params{};
  params.swapId = "uint256-restart";
  params.pair = SwapPair::ETH;
  params.role = SwapRole::BOB;
  params.ctrAmount = AtomicAmount("100000000000000000000");
  bool ok = false;
  {
    SwapDatabase db(dir.string());
    SwapStateMachine sm(params);
    ok = db.saveSwap(sm);
  }
  {
    SwapDatabase restarted(dir.string());
    SwapStateMachine restored;
    ok = ok && restarted.loadSwap(params.swapId, restored) &&
        restored.params().ctrAmount == params.ctrAmount &&
        restored.params().amountProtocolVersion == 2;
  }
  fs::remove_all(dir);
  if (!ok) { std::cout << "FAIL\n"; return false; }
  std::cout << "PASS\n";
  return true;
}

static bool test_amount_protocol_rejects_downgrade_mismatch_and_replay() {
  std::cout << "  test_amount_protocol_rejects_downgrade_mismatch_and_replay... ";
  namespace fs = std::filesystem;
  const fs::path base = fs::temp_directory_path() /
      ("xfg-swap-amount-gate-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directories(base);
  Logging::ConsoleLogger logger(Logging::ERROR);
  const AtomicAmount ctrAmount = AtomicAmount("100000000000000000000");

  auto rejectedWithoutAdvance = [&](const std::string& id, bool replay,
                                    bool downgrade, bool mismatch) {
    const fs::path dir = base / id;
    SwapParams params{};
    params.swapId = id;
    params.pair = SwapPair::ETH;
    params.role = SwapRole::BOB;
    params.xfgAmount = 1000000000;
    params.ctrAmount = ctrAmount;
    if (replay) params.peerSwapPubKey.data[0] = 1;
    {
      SwapDatabase db(dir.string());
      if (!db.saveSwap(SwapStateMachine(params))) {
        std::cout << " [" << id << ":seed-failed]";
        return false;
      }
    }
    PeerMessage message{};
    message.type = PeerMessageType::KEY_EXCHANGE;
    message.swapId = id;
    message.keyExchange.amountProtocolVersion = downgrade ? 1 : 2;
    message.keyExchange.pair = SwapPair::ETH;
    message.keyExchange.xfgAmount = params.xfgAmount;
    message.keyExchange.ctrAmount = ctrAmount + (mismatch ? 1 : 0);
    {
      SwapDaemon daemon("127.0.0.1", 1, dir.string(), logger);
      if (SwapDaemonTestAccess::handlePeerMessage(daemon, message)) {
        std::cout << " [" << id << ":unexpected-accept]";
        return false;
      }
    }
    SwapDatabase db(dir.string());
    SwapStateMachine restored;
    const bool remainsInitiated = db.loadSwap(id, restored) &&
        restored.currentState() == SwapState::INITIATED &&
        (replay || restored.params().peerSwapPubKey.data[0] == 0);
    if (!remainsInitiated) std::cout << " [" << id << ":not-initiated-or-load-failed]";
    return remainsInitiated;
  };

  const bool downgradeOk = rejectedWithoutAdvance("downgrade", false, true, false);
  const bool mismatchOk = rejectedWithoutAdvance("mismatch", false, false, true);
  const bool replayOk = rejectedWithoutAdvance("replay", true, false, false);
  const bool ok = downgradeOk && mismatchOk && replayOk;
  if (!ok) {
    std::cout << " (downgrade=" << downgradeOk << ", mismatch=" << mismatchOk
              << ", replay=" << replayOk << ")";
  }
  fs::remove_all(base);
  if (!ok) { std::cout << "FAIL\n"; return false; }
  std::cout << "PASS\n";
  return true;
}

static bool test_non_evm_overflow_rejected_before_record_creation() {
  std::cout << "  test_non_evm_overflow_rejected_before_record_creation... ";
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() /
      ("xfg-swap-non-evm-range-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  Logging::ConsoleLogger logger(Logging::ERROR);
  SwapParams params{};
  params.swapId = "non-evm-overflow";
  params.pair = SwapPair::BCH;
  params.role = SwapRole::BOB;
  params.xfgAmount = 1000000000;
  params.ctrAmount = AtomicAmount(UINT64_MAX) + 1;
  bool rejected = false;
  {
    SwapDaemon daemon("127.0.0.1", 1, dir.string(), logger);
    rejected = !daemon.initiate(params);
  }
  SwapDatabase db(dir.string());
  const bool noRecord = db.listSwaps().empty();
  fs::remove_all(dir);
  if (!(rejected && noRecord)) { std::cout << "FAIL\n"; return false; }
  std::cout << "PASS\n";
  return true;
}

static bool test_expected_peer_key_is_not_a_completed_exchange() {
  std::cout << "  test_expected_peer_key_is_not_a_completed_exchange... ";
  SwapParams params{};
  Crypto::PublicKey expected{}, other{};
  Crypto::SecretKey expectedSecret{}, otherSecret{};
  const Crypto::PublicKey zero{};
  Crypto::generate_keys(expected, expectedSecret);
  Crypto::generate_keys(other, otherSecret);
  params.peerSwapPubKey = expected;
  if (!SwapDaemonTestAccess::preparePeerIdentity(params) ||
      std::memcmp(&params.expectedPeerSwapPubKey, &expected, sizeof(expected)) != 0 ||
      std::memcmp(&params.peerSwapPubKey, &zero, sizeof(expected)) != 0) {
    std::cout << "FAIL: expected key was marked exchanged\n";
    return false;
  }
  params.peerSwapPubKey = other;
  if (SwapDaemonTestAccess::preparePeerIdentity(params) ||
      std::memcmp(&params.expectedPeerSwapPubKey, &expected, sizeof(expected)) != 0) {
    std::cout << "FAIL: conflicting peer key was accepted\n";
    return false;
  }
  params.peerSwapPubKey = zero;
  params.swapId = "peer-first-kx";
  params.pair = SwapPair::ETH;
  params.role = SwapRole::BOB;
  params.xfgAmount = 1000000000;
  params.ctrAmount = AtomicAmount("100000000000000000000");
  Crypto::SecretKey ourSecret{};
  Crypto::generate_keys(params.ourSwapPubKey, ourSecret);

  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() /
      ("xfg-swap-first-kx-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  SwapDatabase db(dir.string());
  if (!db.saveSwap(SwapStateMachine(params))) {
    std::cout << "FAIL: could not seed pending swap\n";
    fs::remove_all(dir);
    return false;
  }
  PeerMessage message{};
  message.type = PeerMessageType::KEY_EXCHANGE;
  message.swapId = params.swapId;
  message.keyExchange.swapPubKey = expected;
  message.keyExchange.amountProtocolVersion = 2;
  message.keyExchange.pair = params.pair;
  message.keyExchange.xfgAmount = params.xfgAmount;
  message.keyExchange.ctrAmount = params.ctrAmount;
  Logging::ConsoleLogger logger(Logging::ERROR);
  bool accepted = signPeerMessage(message, expected, expectedSecret);
  {
    SwapDaemon daemon("127.0.0.1", 1, dir.string(), logger);
    accepted = accepted && SwapDaemonTestAccess::handlePeerMessage(daemon, message);
  }
  SwapStateMachine restored;
  accepted = accepted && db.loadSwap(params.swapId, restored) &&
      restored.currentState() == SwapState::ADAPTOR_KEYS_EXCHANGED &&
      std::memcmp(&restored.params().peerSwapPubKey, &expected, sizeof(expected)) == 0;
  fs::remove_all(dir);
  if (!accepted) {
    std::cout << "FAIL: first signed KEY_EXCHANGE was rejected\n";
    return false;
  }
  std::cout << "PASS\n";
  return true;
}

int main() {
  std::cout << "=== SwapStateMachine SPV state tests ===\n\n";
  int pass = 0, total = 16;
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
  if (test_legacy_claim_records_fail_closed()) ++pass;
  if (test_terminal_archive_masks_stale_active_record()) ++pass;
  if (test_uint256_amount_survives_database_restart()) ++pass;
  if (test_amount_protocol_rejects_downgrade_mismatch_and_replay()) ++pass;
  if (test_non_evm_overflow_rejected_before_record_creation()) ++pass;
  if (test_expected_peer_key_is_not_a_completed_exchange()) ++pass;

  std::cout << "\n=== " << pass << "/" << total << " tests passed ===\n";
  return (pass == total) ? 0 : 1;
}
