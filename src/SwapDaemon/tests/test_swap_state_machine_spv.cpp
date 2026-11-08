// Copyright (c) 2017-2026 Fuego Developers
//
// Tests for ADAPTOR_WAITING_SPV / ADAPTOR_SECRET_CONFIRMED_SPV state transitions.

#include <iostream>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include "Common/JsonValue.h"
#include "SwapDaemon/SwapDatabase.h"
#include "SwapDaemon/SwapTypes.h"
#include "SwapDaemon/SwapStateMachine.h"
#include "SwapDaemon/SwapDaemon.h"
#include "SwapDaemon/RpcServer.h"
#include "SwapDaemon/IChainClient.h"
#include "SwapDaemon/AdaptorSwap.h"
#include "SwapDaemon/SwapPeerProtocol.h"
#include "Logging/ConsoleLogger.h"
#include <HTTP/httplib.h>

using namespace XfgSwap;

namespace XfgSwap {
struct SwapDaemonTestAccess {
  static bool handlePeerMessage(SwapDaemon& daemon, const PeerMessage& message) {
    return daemon.handlePeerMessage(message);
  }
  static bool preparePeerIdentity(SwapParams& params) {
    return SwapDaemon::preparePeerIdentity(params);
  }
  static void registerChain(SwapDaemon& daemon, SwapPair pair, std::unique_ptr<IChainClient> client) {
    daemon.m_chainRegistry.registerChain(pair, std::move(client));
  }
  static bool handleCtrLocked(SwapDaemon& daemon, SwapStateMachine& sm) { return daemon.handleCtrLocked(sm); }
  static bool handleWaitingSpv(SwapDaemon& daemon, SwapStateMachine& sm) { return daemon.handleWaitingSpv(sm); }
  static bool handlePreSigsReady(SwapDaemon& daemon, SwapStateMachine& sm) { return daemon.handlePreSigsReady(sm); }
};
struct RpcServerTestAccess {
  static std::string dispatch(RpcServer& server, const std::string& request) {
    return server.dispatch(request);
  }

  static bool authorize(const RpcServer& server, const httplib::Request& request) {
    return server.authorize(request);
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

static bool test_swap_id_stays_inside_database() {
  std::cout << "  test_swap_id_stays_inside_database... ";
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() /
      ("xfg-swap-id-boundary-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  SwapDatabase db(dir.string());
  SwapParams params{};
  params.swapId = "../outside";
  params.pair = SwapPair::ETH;
  params.role = SwapRole::BOB;
  params.xfgAmount = 10000000;
  params.ctrAmount = 1;
  SwapStateMachine loaded;
  const bool ok = !db.saveSwap(SwapStateMachine(params)) &&
      !db.loadSwap(params.swapId, loaded) && !db.deleteSwap(params.swapId) &&
      !fs::exists(dir / "outside.json") &&
      isValidSwapId("safe-ID_1") && !isValidSwapId("../outside") &&
      !isValidSwapId("with.dot");
  fs::remove_all(dir);
  if (!ok) { std::cout << "FAIL\n"; return false; }
  std::cout << "PASS\n";
  return true;
}

static bool test_rpc_status_record_excludes_persistence_secrets() {
  std::cout << "  test_rpc_status_record_excludes_persistence_secrets... ";
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() /
      ("xfg-swap-public-status-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  Logging::ConsoleLogger logger(Logging::ERROR);
  SwapDaemon daemon("127.0.0.1", 1, dir.string(), logger, "testnet", "testnet");
  daemon.database().setEncryptionKey("status-test-key");

  SwapParams params{};
  params.swapId = "public-status-1";
  params.pair = SwapPair::ETH;
  params.role = SwapRole::BOB;
  params.xfgAmount = 10000000;
  params.ctrAmount = AtomicAmount("1000000000000000000");
  Crypto::generate_keys(params.ourSwapPubKey, params.ourSwapSecKey);
  const bool saved = daemon.database().saveSwap(SwapStateMachine(params));

  bool rejectedEmptyToken = false;
  try {
    RpcServer unauthenticatedServer(daemon, logger, {});
  } catch (const std::invalid_argument&) {
    rejectedEmptyToken = true;
  }

  const std::string controlToken(64, 'a');
  RpcServer server(daemon, logger, controlToken);
  httplib::Request noToken;
  httplib::Request wrongToken;
  wrongToken.headers.emplace("X-Swap-Token", std::string(64, 'b'));
  httplib::Request headerToken;
  headerToken.headers.emplace("X-Swap-Token", controlToken);
  httplib::Request bearerToken;
  bearerToken.headers.emplace("Authorization", "Bearer " + controlToken);
  const bool authorizationOk =
      !RpcServerTestAccess::authorize(server, noToken) &&
      !RpcServerTestAccess::authorize(server, wrongToken) &&
      RpcServerTestAccess::authorize(server, headerToken) &&
      RpcServerTestAccess::authorize(server, bearerToken);
  bool ok = saved && rejectedEmptyToken && authorizationOk;
  try {
    const std::string listResponse = RpcServerTestAccess::dispatch(
        server, R"({"jsonrpc":"2.0","id":41,"method":"list_swaps","params":{}})");
    const Common::JsonValue root = Common::JsonValue::fromString(listResponse);
    const Common::JsonValue& record = root("result")("swaps")[0];
    const bool publicRecordOk = root("id").getInteger() == 41 &&
        record("xfgAmountAtomic").getString() == "10000000" &&
        record("ctrAmount").getString() == "1000000000000000000" &&
        record.contains("ourSwapPubKey") &&
        !record.contains("ourSwapSecKeyEnc") &&
        !record.contains("adaptorSecretEnc") &&
        !record.contains("musig2OurSecNonceEnc");
    if (!publicRecordOk) std::cout << " [list=" << listResponse << "]";
    ok = ok && publicRecordOk;

    const std::string errorResponse = RpcServerTestAccess::dispatch(
        server, R"({"jsonrpc":"2.0","id":77,"method":"bad\"method","params":{}})");
    const Common::JsonValue errorRoot = Common::JsonValue::fromString(errorResponse);
    const std::string errorMessage = errorRoot("error")("message").getString();
    const bool escapedErrorOk = errorRoot("id").getInteger() == 77 &&
        errorMessage == "Method not found: bad\\\"method";
    if (!escapedErrorOk) {
      std::cout << " [error=" << errorResponse << ", parsed-message="
                << errorMessage << ", size=" << errorMessage.size() << "]";
    }
    ok = ok && escapedErrorOk;
  } catch (const std::exception& error) {
    std::cout << " [exception=" << error.what() << "]";
    ok = false;
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

namespace {
struct ScriptedClaim {
  ChainClientResult claimResult = ChainClientResult::notSent("BTC: lock is not buried in a quorum-verified block");
  std::string deadlineError;
  int claimCalls = 0;
};

class ScriptedClaimClient : public IChainClient {
public:
  explicit ScriptedClaimClient(std::shared_ptr<ScriptedClaim> script) : m_script(std::move(script)) {}
  std::string chainName() const override { return "BTC"; }
  ChainClientResult lock(const SwapParams&) override { return ChainClientResult::fail("not scripted"); }
  ChainClientResult verifyLock(const SwapParams&) override { return ChainClientResult::fail("not scripted"); }
  ChainClientResult claim(const SwapParams&) override {
    ++m_script->claimCalls;
    return m_script->claimResult;
  }
  ChainClientResult refund(const SwapParams&) override { return ChainClientResult::fail("not scripted"); }
  ChainClientResult verifyReserveProof(const std::string&, uint64_t, const std::string&) override {
    return ChainClientResult::fail("not scripted");
  }
  std::string claimDeadlineError(const SwapParams&) override { return m_script->deadlineError; }

private:
  std::shared_ptr<ScriptedClaim> m_script;
};

SwapStateMachine bobAwaitingClaim(const std::string& swapId, bool spv) {
  SwapParams params{};
  params.swapId = swapId;
  params.pair = SwapPair::BTC;
  params.role = SwapRole::BOB;
  params.useSpvVerification = spv;
  params.requiredConfirmations = 6;
  std::memset(&params.adaptorSecret, 0x11, sizeof(params.adaptorSecret));
  SwapStateMachine sm(params);
  sm.transition(SwapState::ADAPTOR_KEYS_EXCHANGED);
  sm.transition(SwapState::ADAPTOR_ESCROW_FUNDED);
  sm.transition(SwapState::ADAPTOR_PRESIGS_READY);
  sm.transition(SwapState::ADAPTOR_CTR_LOCKED);
  if (spv) sm.transition(SwapState::ADAPTOR_WAITING_SPV);
  return sm;
}

bool claimIntentAfterOneTick(const std::filesystem::path& dir, const std::string& swapId, bool spv,
                             const std::shared_ptr<ScriptedClaim>& script, bool& claimAttempted) {
  Logging::ConsoleLogger logger(Logging::FATAL);
  SwapDaemon daemon("127.0.0.1", 1, dir.string(), logger);
  SwapDaemonTestAccess::registerChain(daemon, SwapPair::BTC, std::make_unique<ScriptedClaimClient>(script));
  daemon.database().setEncryptionKey("unsent-claim-test-key");
  if (!daemon.database().saveSwap(bobAwaitingClaim(swapId, spv))) return false;
  SwapStateMachine loaded;
  if (!daemon.database().loadSwap(swapId, loaded)) return false;
  if (spv) SwapDaemonTestAccess::handleWaitingSpv(daemon, loaded);
  else SwapDaemonTestAccess::handleCtrLocked(daemon, loaded);
  SwapStateMachine persisted;
  if (!daemon.database().loadSwap(swapId, persisted)) return false;
  claimAttempted = persisted.params().ctrClaimAttempted;
  return true;
}
} // namespace

static bool test_unsent_claim_keeps_xfg_refund_open() {
  std::cout << "  test_unsent_claim_keeps_xfg_refund_open... ";
  namespace fs = std::filesystem;
  const fs::path base = fs::temp_directory_path() /
      ("xfg-unsent-claim-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directories(base);
  bool ok = true;
  auto expect = [&](bool condition, const std::string& label) {
    if (!condition) { std::cout << " [" << label << "]"; ok = false; }
  };
  for (const bool spv : {false, true}) {
    const std::string mode = spv ? "spv" : "full";
    bool attempted = true;

    auto unsent = std::make_shared<ScriptedClaim>();
    expect(claimIntentAfterOneTick(base / (mode + "-unsent"), "unsent", spv, unsent, attempted) &&
               unsent->claimCalls == 1 && !attempted,
           mode + ": a claim refused before sending withdraws the claim intent");

    auto ambiguous = std::make_shared<ScriptedClaim>();
    ambiguous->claimResult = ChainClientResult::fail("BTC: no server accepted the transaction");
    expect(claimIntentAfterOneTick(base / (mode + "-ambiguous"), "ambiguous", spv, ambiguous, attempted) &&
               ambiguous->claimCalls == 1 && attempted,
           mode + ": a claim that may have reached the network keeps the XFG refund held");

    auto late = std::make_shared<ScriptedClaim>();
    late->deadlineError = "BTC: lock timeout 120 is within the 18-block claim runway of tip 110";
    expect(claimIntentAfterOneTick(base / (mode + "-late"), "late", spv, late, attempted) &&
               late->claimCalls == 0 && !attempted,
           mode + ": a claim inside the runway is never attempted");
  }
  std::error_code ignored;
  fs::remove_all(base, ignored);
  std::cout << (ok ? "PASS\n" : "FAIL\n");
  return ok;
}

namespace {
struct AdaptorPair {
  SwapParams alice{};
  SwapParams bob{};
};

AdaptorPair adaptorPairFor(const std::string& swapId, SwapPair swapPair) {
  AdaptorPair pair;
  pair.alice.role = SwapRole::ALICE;
  pair.bob.role = SwapRole::BOB;
  adaptor_generate_keys(pair.alice);
  adaptor_generate_keys(pair.bob);
  pair.alice.peerSwapPubKey = pair.bob.ourSwapPubKey;
  pair.bob.peerSwapPubKey = pair.alice.ourSwapPubKey;
  for (SwapParams* side : {&pair.alice, &pair.bob}) {
    side->swapId = swapId;
    side->pair = swapPair;
    side->xfgAmount = 1000000000;
    side->ctrAmount = AtomicAmount("100000");
    adaptor_key_aggregate(*side);
  }
  adaptor_generate_adaptor(pair.bob, pair.bob.escrowPubKey);
  return pair;
}

AdaptorPair btcAdaptorPair(const std::string& swapId) { return adaptorPairFor(swapId, SwapPair::BTC); }

PeerMessage signedAdaptorExchange(const AdaptorPair& pair, SwapLockType lockType) {
  PeerMessage message{};
  message.type = PeerMessageType::ADAPTOR_EXCHANGE;
  message.swapId = pair.bob.swapId;
  message.adaptorExchange.adaptorPoint = pair.bob.adaptorPoint;
  message.adaptorExchange.adaptorDleqQ = pair.bob.adaptorDleqQ;
  message.adaptorExchange.dleqProof = pair.bob.adaptorDleqProof;
  message.adaptorExchange.htlcHashLock = pair.bob.hashLock;
  message.adaptorExchange.lockType = lockType;
  message.adaptorExchange.ptlcPoint = pair.bob.adaptorPoint;
  message.adaptorExchange.secpPubHex = pair.bob.secpPubHex;
  signPeerMessage(message, pair.bob.ourSwapPubKey, pair.bob.ourSwapSecKey);
  return message;
}

bool aliceAcceptsAdaptorExchange(const std::filesystem::path& dir, SwapLockType lockType, SwapLockType& stored) {
  const AdaptorPair pair = btcAdaptorPair("ptlc-gate");
  Logging::ConsoleLogger logger(Logging::FATAL);
  SwapDaemon daemon("127.0.0.1", 1, dir.string(), logger);
  daemon.database().setEncryptionKey("ptlc-gate-test-key");
  if (!daemon.database().saveSwap(SwapStateMachine(pair.alice))) return false;
  const bool accepted = SwapDaemonTestAccess::handlePeerMessage(daemon, signedAdaptorExchange(pair, lockType));
  SwapStateMachine persisted;
  daemon.database().loadSwap("ptlc-gate", persisted);
  stored = persisted.params().lockType;
  return accepted;
}
} // namespace

static bool test_peer_cannot_select_disabled_pure_ptlc() {
  std::cout << "  test_peer_cannot_select_disabled_pure_ptlc... ";
  namespace fs = std::filesystem;
  const fs::path base = fs::temp_directory_path() /
      ("xfg-ptlc-gate-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directories(base);
  bool ok = true;
  SwapLockType stored = SwapLockType::HTLC;
  if (!aliceAcceptsAdaptorExchange(base / "bridge", SwapLockType::PTLC_HTLC_BRIDGE, stored) ||
      stored != SwapLockType::PTLC_HTLC_BRIDGE) {
    std::cout << " [a valid bridge bundle was refused]";
    ok = false;
  }
  if (aliceAcceptsAdaptorExchange(base / "pure", SwapLockType::PTLC, stored) || stored == SwapLockType::PTLC) {
    std::cout << " [a peer selected pure PTLC for BTC while it is disabled]";
    ok = false;
  }
  std::error_code ignored;
  fs::remove_all(base, ignored);
  std::cout << (ok ? "PASS\n" : "FAIL\n");
  return ok;
}

namespace {
class PointLockClient : public IChainClient {
public:
  explicit PointLockClient(int& lockCalls) : m_lockCalls(lockCalls) {}
  std::string chainName() const override { return "ETH"; }
  bool supportsPurePtlc() const override { return true; }
  ChainClientResult lock(const SwapParams&) override {
    ++m_lockCalls;
    return ChainClientResult::fail("not scripted");
  }
  ChainClientResult verifyLock(const SwapParams&) override { return ChainClientResult::fail("not scripted"); }
  ChainClientResult claim(const SwapParams&) override { return ChainClientResult::fail("not scripted"); }
  ChainClientResult refund(const SwapParams&) override { return ChainClientResult::fail("not scripted"); }
  ChainClientResult verifyReserveProof(const std::string&, uint64_t, const std::string&) override {
    return ChainClientResult::fail("not scripted");
  }

private:
  int& m_lockCalls;
};

struct PointLockOutcome {
  bool accepted = false;
  bool proofVerified = false;
  bool hashLockBound = true;
  SwapLockType storedLockType = SwapLockType::HTLC;
};

PointLockOutcome deliverToPointLockAlice(const std::filesystem::path& dir, const AdaptorPair& pair,
                                         const PeerMessage& message) {
  Logging::ConsoleLogger logger(Logging::FATAL);
  int lockCalls = 0;
  SwapDaemon daemon("127.0.0.1", 1, dir.string(), logger);
  SwapDaemonTestAccess::registerChain(daemon, SwapPair::ETH, std::make_unique<PointLockClient>(lockCalls));
  daemon.database().setEncryptionKey("point-lock-test-key");
  PointLockOutcome outcome;
  if (!daemon.database().saveSwap(SwapStateMachine(pair.alice))) return outcome;
  outcome.accepted = SwapDaemonTestAccess::handlePeerMessage(daemon, message);
  SwapStateMachine persisted;
  daemon.database().loadSwap(pair.alice.swapId, persisted);
  outcome.proofVerified = persisted.params().crossCurveProofVerified;
  outcome.storedLockType = persisted.params().lockType;
  static const Crypto::Hash ZERO_HASH{};
  outcome.hashLockBound = std::memcmp(&persisted.params().hashLock, &ZERO_HASH, sizeof(ZERO_HASH)) != 0;
  return outcome;
}

PeerMessage provenExchange(const AdaptorPair& pair, SwapLockType lockType, const AdaptorPair* proofSource = nullptr) {
  PeerMessage message = signedAdaptorExchange(pair, lockType);
  adaptor_prove_cross_curve((proofSource ? *proofSource : pair).bob, message.adaptorExchange.crossCurveProof);
  signPeerMessage(message, pair.bob.ourSwapPubKey, pair.bob.ourSwapSecKey);
  return message;
}
} // namespace

static bool test_point_lock_requires_cross_curve_proof() {
  std::cout << "  test_point_lock_requires_cross_curve_proof... ";
  namespace fs = std::filesystem;
  const fs::path base = fs::temp_directory_path() /
      ("xfg-point-lock-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directories(base);
  bool ok = true;
  auto expect = [&](bool condition, const std::string& label) {
    if (!condition) { std::cout << " [" << label << "]"; ok = false; }
  };
  const AdaptorPair pair = adaptorPairFor("point-lock", SwapPair::ETH);
  const AdaptorPair otherSecret = adaptorPairFor("point-lock", SwapPair::ETH);

  const PointLockOutcome valid = deliverToPointLockAlice(base / "valid", pair, provenExchange(pair, SwapLockType::PTLC));
  expect(valid.accepted && valid.proofVerified && valid.storedLockType == SwapLockType::PTLC,
         "a proven point-lock exchange is accepted and recorded as verified");
  expect(!valid.hashLockBound, "a point lock binds no hash lock that could veto the true t");

  const PointLockOutcome downgrade =
      deliverToPointLockAlice(base / "downgrade", pair, provenExchange(pair, SwapLockType::PTLC_HTLC_BRIDGE));
  expect(!downgrade.accepted && !downgrade.proofVerified,
         "a hash lock is refused for a pair this node locks by point");

  const PointLockOutcome unproven =
      deliverToPointLockAlice(base / "unproven", pair, signedAdaptorExchange(pair, SwapLockType::PTLC));
  expect(!unproven.accepted && !unproven.proofVerified, "a point lock without the cross-curve proof is refused");

  const PointLockOutcome forged =
      deliverToPointLockAlice(base / "forged", pair, provenExchange(pair, SwapLockType::PTLC, &otherSecret));
  expect(!forged.accepted && !forged.proofVerified, "a proof made for another secret is refused");

  PeerMessage tampered = provenExchange(pair, SwapLockType::PTLC);
  tampered.adaptorExchange.crossCurveProof[100] ^= 1;
  const PointLockOutcome altered = deliverToPointLockAlice(base / "altered", pair, tampered);
  expect(!altered.accepted, "a proof changed after signing is refused");

  PeerMessage roundTrip;
  const PeerMessage original = provenExchange(pair, SwapLockType::PTLC);
  expect(deserializePeerMessage(serializePeerMessage(original), roundTrip) &&
             roundTrip.adaptorExchange.crossCurveProof == original.adaptorExchange.crossCurveProof,
         "the proof survives the wire encoding");

  std::error_code ignored;
  fs::remove_all(base, ignored);
  std::cout << (ok ? "PASS\n" : "FAIL\n");
  return ok;
}

static bool test_point_lock_gate_needs_verified_proof() {
  std::cout << "  test_point_lock_gate_needs_verified_proof... ";
  namespace fs = std::filesystem;
  const fs::path base = fs::temp_directory_path() /
      ("xfg-point-gate-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directories(base);
  bool ok = true;
  for (const bool verified : {false, true}) {
    AdaptorPair pair = adaptorPairFor(verified ? "gate-verified" : "gate-unverified", SwapPair::ETH);
    SwapParams alice = pair.alice;
    alice.adaptorPoint = pair.bob.adaptorPoint;
    alice.secpPubHex = pair.bob.secpPubHex;
    alice.lockType = SwapLockType::PTLC;
    alice.crossCurveProofVerified = verified;
    SwapStateMachine sm(alice);
    sm.transition(SwapState::ADAPTOR_KEYS_EXCHANGED);
    sm.transition(SwapState::ADAPTOR_ESCROW_FUNDED);
    sm.transition(SwapState::ADAPTOR_PRESIGS_READY);
    Logging::ConsoleLogger logger(Logging::FATAL);
    int lockCalls = 0;
    SwapDaemon daemon("127.0.0.1", 1, (base / alice.swapId).string(), logger);
    SwapDaemonTestAccess::registerChain(daemon, SwapPair::ETH, std::make_unique<PointLockClient>(lockCalls));
    SwapDaemonTestAccess::handlePreSigsReady(daemon, sm);
    if ((lockCalls == 1) != verified) {
      std::cout << (verified ? " [a verified point lock was not funded]" : " [an unverified point lock was funded]");
      ok = false;
    }
  }
  std::error_code ignored;
  fs::remove_all(base, ignored);
  std::cout << (ok ? "PASS\n" : "FAIL\n");
  return ok;
}

int main() {
  std::cout << "=== SwapStateMachine SPV state tests ===\n\n";
  int pass = 0, total = 22;
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
  if (test_swap_id_stays_inside_database()) ++pass;
  if (test_rpc_status_record_excludes_persistence_secrets()) ++pass;
  if (test_amount_protocol_rejects_downgrade_mismatch_and_replay()) ++pass;
  if (test_non_evm_overflow_rejected_before_record_creation()) ++pass;
  if (test_expected_peer_key_is_not_a_completed_exchange()) ++pass;
  if (test_unsent_claim_keeps_xfg_refund_open()) ++pass;
  if (test_peer_cannot_select_disabled_pure_ptlc()) ++pass;
  if (test_point_lock_requires_cross_curve_proof()) ++pass;
  if (test_point_lock_gate_needs_verified_proof()) ++pass;

  std::cout << "\n=== " << pass << "/" << total << " tests passed ===\n";
  return (pass == total) ? 0 : 1;
}
