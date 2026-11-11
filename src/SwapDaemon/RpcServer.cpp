// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even
// the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You can redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation. Fuego includes elements written
// by third parties. See file labeled LICENSE for more details.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

#include "RpcServer.h"
#include "SwapDaemon.h"
#include "SwapTypes.h"
#include "Common/JsonValue.h"
#include "Common/StringTools.h"
#include <HTTP/httplib.h>
#include <sstream>
#include <mutex>
#include <stdexcept>
#include <charconv>
#include <cctype>
#include <limits>

namespace XfgSwap {

namespace {

bool parseUint64Param(const Common::JsonValue& value, uint64_t& out) {
  if (value.isInteger()) {
    const int64_t parsed = value.getInteger();
    if (parsed < 0) return false;
    out = static_cast<uint64_t>(parsed);
    return true;
  }
  if (!value.isString()) return false;
  const std::string& text = value.getString();
  if (text.empty()) return false;
  uint64_t parsed = 0;
  const char* begin = text.data();
  const char* end = begin + text.size();
  const auto result = std::from_chars(begin, end, parsed, 10);
  if (result.ec != std::errc{} || result.ptr != end) return false;
  out = parsed;
  return true;
}

bool parseAtomicAmountParam(const Common::JsonValue& value, AtomicAmount& out) {
  if (value.isString()) return parseAtomicAmount(value.getString(), out);
  // JSON numbers beyond 2^53-1 may already have been rounded by a browser.
  // Require canonical decimal strings for the full uint256 range.
  if (value.isInteger()) {
    const int64_t number = value.getInteger();
    if (number < 0 || number > 9007199254740991LL) return false;
    out = static_cast<uint64_t>(number);
    return true;
  }
  return false;
}

bool isLoopbackBrowserOrigin(const std::string& origin) {
  if (origin.empty()) return true; // CLI/server-to-server request
  const size_t schemeEnd = origin.find("://");
  if (schemeEnd == std::string::npos) return false;
  std::string scheme = origin.substr(0, schemeEnd);
  for (char& c : scheme)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (scheme != "http" && scheme != "https") return false;

  const std::string authority = origin.substr(schemeEnd + 3);
  if (authority.empty() || authority.find_first_of("/?#@") != std::string::npos)
    return false;

  std::string host;
  std::string port;
  if (authority.front() == '[') {
    const size_t close = authority.find(']');
    if (close == std::string::npos) return false;
    host = authority.substr(1, close - 1);
    if (close + 1 < authority.size()) {
      if (authority[close + 1] != ':') return false;
      port = authority.substr(close + 2);
    }
  } else {
    const size_t colon = authority.rfind(':');
    if (colon != std::string::npos) {
      if (authority.find(':') != colon) return false; // unbracketed IPv6
      host = authority.substr(0, colon);
      port = authority.substr(colon + 1);
    } else {
      host = authority;
    }
  }
  for (char& c : host)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (host != "localhost" && host != "127.0.0.1" && host != "::1")
    return false;
  if (!port.empty()) {
    uint32_t parsed = 0;
    const auto result = std::from_chars(port.data(), port.data() + port.size(), parsed, 10);
    if (result.ec != std::errc{} || result.ptr != port.data() + port.size() ||
        parsed == 0 || parsed > 65535) return false;
  }
  return true;
}

void setLoopbackCors(const httplib::Request& req, httplib::Response& res) {
  const std::string origin = req.get_header_value("Origin");
  if (!origin.empty()) {
    res.set_header("Access-Control-Allow-Origin", origin);
    res.set_header("Vary", "Origin");
  }
}

Common::JsonValue buildPublicSwapRecord(SwapDaemon& daemon,
                                        const SwapStateMachine& stateMachine) {
  const SwapParams& params = stateMachine.params();
  Common::JsonValue record(Common::JsonValue::OBJECT);
  record.insert("swapId", params.swapId);
  record.insert("pair", static_cast<int64_t>(static_cast<uint8_t>(params.pair)));
  record.insert("pairName", std::string(swapPairToString(params.pair)));
  record.insert("role", static_cast<int64_t>(static_cast<uint8_t>(params.role)));
  record.insert("roleName", std::string(params.role == SwapRole::ALICE ? "ALICE" : "BOB"));
  record.insert("xfgAmountAtomic", std::to_string(params.xfgAmount));
  if (params.xfgAmount <= static_cast<uint64_t>((std::numeric_limits<int64_t>::max)())) {
    record.insert("xfgAmount", static_cast<int64_t>(params.xfgAmount));
  } else {
    record.insert("xfgAmount", std::to_string(params.xfgAmount));
  }
  record.insert("ctrAmount", atomicAmountToString(params.ctrAmount));
  record.insert("amountProtocolVersion", static_cast<int64_t>(params.amountProtocolVersion));
  record.insert("state", static_cast<int64_t>(static_cast<uint8_t>(stateMachine.currentState())));
  record.insert("stateName", std::string(swapStateToString(stateMachine.currentState())));
  record.insert("createdAt", static_cast<int64_t>(stateMachine.createdAt()));
  record.insert("updatedAt", static_cast<int64_t>(stateMachine.updatedAt()));
  record.insert("recordVersion", static_cast<int64_t>(stateMachine.recordVersion()));
  record.insert("xfgTimeoutHeight", static_cast<int64_t>(params.xfgTimeoutHeight));
  record.insert("ctrTimeoutBlock", static_cast<int64_t>(params.ctrTimeoutBlock));
  record.insert("requiredConfirmations",
                static_cast<int64_t>(params.requiredConfirmations ? params.requiredConfirmations : 6));
  record.insert("ctrLockTxId", params.ctrLockTxId);
  record.insert("ctrClaimTxId", params.ctrClaimTxId);
  record.insert("ctrRefundTxId", params.ctrRefundTxId);
  record.insert("escrowClaimTxId", params.escrowClaimTxId);
  record.insert("escrowRefundTxId", params.escrowRefundTxId);
  record.insert("peerEndpoint", params.peerEndpoint);
  record.insert("ourSwapPubKey", Common::podToHex(params.ourSwapPubKey));
  record.insert("peerSwapPubKey", Common::podToHex(params.peerSwapPubKey));
  record.insert("expectedPeerSwapPubKey", Common::podToHex(params.expectedPeerSwapPubKey));

  int64_t confirmations = 0;
  int64_t blockHeight = 0;
  int64_t spvVerified = 0;
  int64_t confirmed = 0;
  if (!params.ctrLockTxId.empty()) {
    IChainClient* client = daemon.getChainClient(params.pair);
    if (client) {
      ChainClientResult chainResult;
      client->getTransactionDetails(params.ctrLockTxId, chainResult);
      confirmations = static_cast<int64_t>(chainResult.confirmations);
      blockHeight = static_cast<int64_t>(chainResult.blockHeight);
      spvVerified = chainResult.spvVerified ? 1 : 0;
      confirmed = chainResult.confirmed ? 1 : 0;
      if (!chainResult.success && !chainResult.error.empty()) {
        record.insert("spvError", chainResult.error);
      }
      uint64_t currentHeight = 0;
      if (client->getCurrentHeight(currentHeight) &&
          currentHeight <= static_cast<uint64_t>((std::numeric_limits<int64_t>::max)())) {
        record.insert("currentHeight", static_cast<int64_t>(currentHeight));
      }
    } else {
      record.insert("spvError", std::string("chain client not configured"));
    }
  }
  record.insert("confirmations", confirmations);
  record.insert("blockHeight", blockHeight);
  record.insert("spvVerified", spvVerified);
  record.insert("confirmed", confirmed);
  return record;
}

} // namespace

RpcServer::RpcServer(SwapDaemon& daemon, Logging::ILogger& logger,
                     const std::string& controlToken)
  : m_daemon(daemon)
  , m_logger(logger, "RpcServer")
  , m_controlToken(controlToken) {
  if (m_controlToken.empty()) {
    throw std::invalid_argument("swap daemon RPC control token is required");
  }
}

RpcServer::~RpcServer() {
  stop();
}

bool RpcServer::start(uint16_t port) {
  m_server = std::make_unique<httplib::Server>();
  registerRoutes();

  // Bind to loopback only
  if (!m_server->bind_to_port("127.0.0.1", port)) {
    m_logger(Logging::ERROR) << "Failed to bind RPC server to port " << port;
    return false;
  }

  m_running = true;
  m_thread = std::thread([this, port]() {
    m_logger(Logging::INFO) << "JSON-RPC server listening on 127.0.0.1:" << port;
    m_server->listen_after_bind();
    m_running = false;
  });

  // Give the server a moment to start
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  return m_running.load();
}

void RpcServer::stop() {
  if (m_server) {
    m_server->stop();
  }
  if (m_thread.joinable()) {
    m_thread.join();
  }
  m_running = false;
}

bool RpcServer::authorize(const httplib::Request& req) const {
  if (m_controlToken.empty()) return false;
  auto it = req.headers.find("X-Swap-Token");
  if (it != req.headers.end() && it->second == m_controlToken) return true;
  auto auth = req.headers.find("Authorization");
  if (auth != req.headers.end()) {
    const std::string& v = auth->second;
    const std::string bearer = "Bearer ";
    if (v.size() > bearer.size() && v.compare(0, bearer.size(), bearer) == 0 &&
        v.substr(bearer.size()) == m_controlToken) {
      return true;
    }
  }
  return false;
}

void RpcServer::registerRoutes() {
  m_server->Post("/", [this](const httplib::Request& req, httplib::Response& res) {
    if (!isLoopbackBrowserOrigin(req.get_header_value("Origin"))) {
      res.status = 403;
      res.set_content(R"({"jsonrpc":"2.0","error":{"code":-32002,"message":"Browser origin not allowed"},"id":null})",
                      "application/json");
      return;
    }
    if (!authorize(req)) {
      res.status = 401;
      res.set_content(R"({"jsonrpc":"2.0","error":{"code":-32001,"message":"Unauthorized"},"id":null})",
                      "application/json");
      return;
    }
    std::string response = dispatch(req.body);
    res.set_content(response, "application/json");
    setLoopbackCors(req, res);
  });

  m_server->Options(".*", [](const httplib::Request& req, httplib::Response& res) {
    if (!isLoopbackBrowserOrigin(req.get_header_value("Origin"))) {
      res.status = 403;
      return;
    }
    setLoopbackCors(req, res);
    res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type, X-Swap-Token, Authorization");
    res.status = 204;
  });

  // Health check (no auth)
  m_server->Get("/health", [](const httplib::Request& req, httplib::Response& res) {
    if (!isLoopbackBrowserOrigin(req.get_header_value("Origin"))) {
      res.status = 403;
      return;
    }
    res.set_content(R"({"status":"ok"})", "application/json");
    setLoopbackCors(req, res);
  });
}

std::string RpcServer::dispatch(const std::string& body) {
  int id = 0;
  std::string method;
  std::string params;

  try {
    auto root = Common::JsonValue::fromString(body);
    if (!root.isObject()) {
      return rpcError(-32600, "Invalid Request");
    }

    if (root.contains("id")) {
      id = static_cast<int>(root("id").getInteger());
    }

    if (!root.contains("method") || !root("method").isString()) {
      return rpcError(-32600, "Missing method", id);
    }
    method = root("method").getString();

    if (root.contains("params")) {
      params = root("params").toString();
    } else {
      params = "{}";
    }
  } catch (const std::exception&) {
    return rpcError(-32700, "Parse error", id);
  }

  m_logger(Logging::INFO) << "RPC method: " << method;

  std::string result;
  if (method == "status") {
    result = m_daemon.buildStatusJson();
  } else if (method == "initiate_swap") {
    result = handleInitiateSwap(params);
  } else if (method == "accept") {
    result = handleAccept(params);
  } else if (method == "get_reserve_proof") {
    result = handleGetReserveProof(params);
  } else if (method == "list_swaps") {
    result = handleListSwaps(params);
  } else if (method == "swap_status") {
    result = handleSwapStatus(params);
  } else if (method == "refund") {
    result = handleRefund(params);
  } else if (method == "check_timeouts") {
    result = handleCheckTimeouts(params);
  } else if (method == "list_chains") {
    result = handleListChains(params);
  } else {
    return rpcError(-32601, "Method not found: " + method, id);
  }

  try {
    Common::JsonValue handlerResult = Common::JsonValue::fromString(result);
    if (handlerResult.isObject() && handlerResult.contains("error")) {
      handlerResult.set("id", static_cast<int64_t>(id));
      return handlerResult.toString();
    }
  } catch (const std::exception&) {
    return rpcError(-32603, "Internal error", id);
  }

  return rpcSuccess(result, id);
}

std::string RpcServer::handleListChains(const std::string& /*params*/) {
  return std::string("{\"chains\":") + m_daemon.buildChainCatalogJson() + "}";
}

std::string RpcServer::handleInitiateSwap(const std::string& params) {
  try {
    auto root = Common::JsonValue::fromString(params);
    if (!root.isObject()) {
      return rpcError(-32602, "Invalid params");
    }

    SwapPair pair = SwapPair::SOL;
    uint64_t xfgAmount = 0;
    AtomicAmount ctrAmount = 0;
    std::string peer;

    if (root.contains("pair")) {
      std::string pairStr = root("pair").getString();
      if (!swapPairFromString(pairStr, pair)) {
        return rpcError(-32602, "Invalid pair: " + pairStr);
      }
    }
    if (root.contains("xfg_amount")) {
      if (!parseUint64Param(root("xfg_amount"), xfgAmount)) {
        return rpcError(-32602, "xfg_amount must be a uint64 integer or decimal string");
      }
    }
    if (root.contains("ctr_amount")) {
      if (!parseAtomicAmountParam(root("ctr_amount"), ctrAmount)) {
        return rpcError(-32602, "ctr_amount must be a uint256 decimal string (or safe JSON integer)");
      }
    }
    if (root.contains("peer")) {
      peer = root("peer").getString();
    }
    std::string expectedPeerHex;
    if (root.contains("expected_peer_pubkey")) {
      expectedPeerHex = root("expected_peer_pubkey").getString();
    }

    // Role: "alice" (taker — has counterparty coin, wants XFG) or "bob"
    // (maker — has XFG). Defaults to bob (the CLI's historic default).
    SwapRole role = SwapRole::BOB;
    if (root.contains("role")) {
      std::string roleStr = root("role").getString();
      if (roleStr == "alice" || roleStr == "ALICE") {
        role = SwapRole::ALICE;
      } else if (roleStr == "bob" || roleStr == "BOB") {
        role = SwapRole::BOB;
      } else {
        return rpcError(-32602, "Invalid role (use 'alice' or 'bob'): " + roleStr);
      }
    }

    if (xfgAmount == 0) {
      return rpcError(-32602, "xfg_amount must be > 0");
    }
    if (ctrAmount == 0) {
      return rpcError(-32602, "ctr_amount must be > 0");
    }
    if (peer.empty()) {
      return rpcError(-32602, "peer endpoint is required");
    }
    // expected_peer_pubkey is optional: when absent, the first KEY_EXCHANGE
    // binds the peer (the daemon logs an anti-griefing warning). Wallets
    // that have the counterparty key (from an offer) SHOULD pass it.
    if (!expectedPeerHex.empty() && expectedPeerHex.size() != 64) {
      return rpcError(-32602,
        "expected_peer_pubkey must be empty or 64-char hex Ed25519 pubkey of counterparty swap key");
    }

    SwapParams swapParams;
    swapParams.pair = pair;
    swapParams.role = role;
    swapParams.xfgAmount = xfgAmount;
    swapParams.ctrAmount = ctrAmount;
    swapParams.peerEndpoint = peer;

    // Zero-init crypto fields
    std::memset(&swapParams.aliceXfgPubKey, 0, sizeof(swapParams.aliceXfgPubKey));
    std::memset(&swapParams.bobXfgPubKey, 0, sizeof(swapParams.bobXfgPubKey));
    std::memset(&swapParams.ourSwapSecKey, 0, sizeof(swapParams.ourSwapSecKey));
    std::memset(&swapParams.ourSwapPubKey, 0, sizeof(swapParams.ourSwapPubKey));
    std::memset(&swapParams.peerSwapPubKey, 0, sizeof(swapParams.peerSwapPubKey));
    std::memset(&swapParams.expectedPeerSwapPubKey, 0, sizeof(swapParams.expectedPeerSwapPubKey));
    std::memset(&swapParams.escrowPubKey, 0, sizeof(swapParams.escrowPubKey));
    std::memset(&swapParams.adaptorPoint, 0, sizeof(swapParams.adaptorPoint));
    std::memset(&swapParams.adaptorSecret, 0, sizeof(swapParams.adaptorSecret));
    std::memset(&swapParams.adaptorDleqQ, 0, sizeof(swapParams.adaptorDleqQ));
    std::memset(&swapParams.escrowTxHash, 0, sizeof(swapParams.escrowTxHash));
    std::memset(&swapParams.hashLock, 0, sizeof(swapParams.hashLock));
    std::memset(&swapParams.preimage, 0, sizeof(swapParams.preimage));
    swapParams.xfgTimeoutHeight = 0;
    swapParams.ctrTimeoutBlock = 0;
    swapParams.escrowOutputIndex = 0;
    swapParams.htlcOutputIndex = 0;

    if (!Common::podFromHex(expectedPeerHex, swapParams.expectedPeerSwapPubKey)) {
      if (!expectedPeerHex.empty()) {
        return rpcError(-32602, "expected_peer_pubkey must be 64-char hex");
      }
    }

    // Optional taker identity injection: an AFK taker publishes an Ed25519
    // identity in /requestswap; the maker binds it as the expected peer key.
    // Inject the same secret here so this daemon's messages sign under the
    // exact key the maker expects.
    std::string ourSwapSecHex;
    if (root.contains("our_swap_secret_key")) {
      ourSwapSecHex = root("our_swap_secret_key").getString();
    }
    if (!ourSwapSecHex.empty()) {
      if (ourSwapSecHex.size() != 64) {
        return rpcError(-32602, "our_swap_secret_key must be 64-char hex");
      }
      if (!Common::podFromHex(ourSwapSecHex, swapParams.ourSwapSecKey) ||
          !Crypto::secret_key_to_public_key(swapParams.ourSwapSecKey, swapParams.ourSwapPubKey)) {
        return rpcError(-32602, "our_swap_secret_key is not a valid Ed25519 secret key");
      }
    }

    // Optional swap id (AFK takers pass the maker's lockId so both daemons
    // track the same swap).
    if (root.contains("swap_id") && !root("swap_id").getString().empty()) {
      swapParams.swapId = root("swap_id").getString();
      if (!isValidSwapId(swapParams.swapId)) {
        return rpcError(-32602, "swap_id must contain 1-64 ASCII letters, digits, hyphens, or underscores");
      }
    }

    // AFK mode: after initiate, park the record at AFK_OFFER_LOCKED so
    // accept() drives it to AFK_OFFER_ACCEPTED (non-interactive flow).
    bool afkMode = false;
    if (root.contains("afk")) {
      afkMode = root("afk").getBool();
    }

    // AFK pre-lock material (from the maker's fill result): adaptor point T,
    // hashlock H(t), the maker's pre-signature, and the maker's CTR receive
    // address. Required for the taker to lock the counterparty HTLC.
    if (root.contains("adaptor_point")) {
      if (!Common::podFromHex(root("adaptor_point").getString(), swapParams.adaptorPoint)) {
        return rpcError(-32602, "adaptor_point must be 64-char hex");
      }
    }
    if (root.contains("hash_lock")) {
      if (!Common::podFromHex(root("hash_lock").getString(), swapParams.hashLock)) {
        return rpcError(-32602, "hash_lock must be 64-char hex");
      }
    }
    if (root.contains("pre_sig")) {
      swapParams.afkPreSigHex = root("pre_sig").getString();
    }
    if (root.contains("ctr_address")) {
      swapParams.ctrAddress = root("ctr_address").getString();
    }

    if (!m_daemon.initiate(swapParams)) {
      return rpcError(-32000, "Failed to initiate swap");
    }

    // AFK mode: park at AFK_OFFER_LOCKED so accept() drives the record to
    // AFK_OFFER_ACCEPTED (the non-interactive flow's taker-side entry).
    if (afkMode) {
      SwapStateMachine sm;
      if (!m_daemon.database().loadSwap(swapParams.swapId, sm)) {
        return rpcError(-32000, "AFK initiate failed: record not found");
      }
      if (!sm.transition(SwapState::AFK_OFFER_LOCKED) ||
          !m_daemon.database().saveSwap(sm)) {
        return rpcError(-32000, "AFK initiate failed: transition to AFK_OFFER_LOCKED rejected");
      }
    }

    std::ostringstream oss;
    oss << R"({"swap_id": ")" << swapParams.swapId
        << R"(", "our_swap_pubkey": ")" << Common::podToHex(swapParams.ourSwapPubKey)
        << R"("})";
    return oss.str();
  } catch (const std::exception& e) {
    return rpcError(-32602, std::string("Parameter error: ") + e.what());
  }
}

std::string RpcServer::handleGetReserveProof(const std::string& params) {
  try {
    auto root = Common::JsonValue::fromString(params);
    if (!root.isObject() || !root.contains("address") || !root.contains("message")) {
      return rpcError(-32602, "Missing address or message");
    }
    std::string address = root("address").getString();
    std::string message = root("message").getString();
    if (address.empty() || message.empty()) {
      return rpcError(-32602, "address and message must be non-empty");
    }

    std::string signature;
    if (!m_daemon.getXmrReserveProof(address, message, signature)) {
      return rpcError(-32000,
        "XMR reserve proof unavailable — is monero-wallet-rpc configured in the swap config?");
    }
    return R"({"signature": ")" + signature + R"("})";
  } catch (const std::exception& e) {
    return rpcError(-32602, std::string("Parameter error: ") + e.what());
  }
}

std::string RpcServer::handleAccept(const std::string& params) {  try {
    auto root = Common::JsonValue::fromString(params);
    if (!root.isObject() || !root.contains("swap_id")) {
      return rpcError(-32602, "Missing swap_id");
    }
    std::string swapId = root("swap_id").getString();
    if (!isValidSwapId(swapId)) {
      return rpcError(-32602, "Invalid swap_id");
    }

    auto result = m_daemon.accept(swapId);
    if (!result.success) {
      return rpcError(-32000, result.warning.empty() ? "Accept failed" : result.warning);
    }

    SwapStateMachine sm;
    std::string stateName = "UNKNOWN";
    if (m_daemon.database().loadSwap(swapId, sm)) {
      stateName = swapStateToString(sm.currentState());
    }
    return R"({"success": true, "swap_id": ")" + swapId +
           R"(", "state": ")" + stateName + R"("})";
  } catch (const std::exception& e) {
    return rpcError(-32602, std::string("Parameter error: ") + e.what());
  }
}

std::string RpcServer::handleListSwaps(const std::string& /*params*/) {
  auto ids = m_daemon.database().listSwaps();

  std::ostringstream oss;
  oss << R"({"swaps": [)";
  bool first = true;
  for (const auto& id : ids) {
    SwapStateMachine sm;
    if (m_daemon.database().loadSwap(id, sm)) {
      if (!first) oss << ",";
      first = false;
      oss << buildPublicSwapRecord(m_daemon, sm).toString();
    }
  }
  oss << R"(]})";
  return oss.str();
}

std::string RpcServer::handleSwapStatus(const std::string& params) {
  try {
    auto root = Common::JsonValue::fromString(params);
    if (!root.isObject() || !root.contains("swap_id")) {
      return rpcError(-32602, "Missing swap_id");
    }
    std::string swapId = root("swap_id").getString();
    if (!isValidSwapId(swapId)) {
      return rpcError(-32602, "Invalid swap_id");
    }

    SwapStateMachine sm;
    if (!m_daemon.database().loadSwap(swapId, sm)) {
      return rpcError(-32000, "Swap not found: " + swapId);
    }

    Common::JsonValue rec = buildPublicSwapRecord(m_daemon, sm);
    return R"({"swap": )" + rec.toString() + "}";
  } catch (const std::exception& e) {
    return rpcError(-32602, std::string("Parameter error: ") + e.what());
  }
}

std::string RpcServer::handleRefund(const std::string& params) {
  try {
    auto root = Common::JsonValue::fromString(params);
    if (!root.isObject() || !root.contains("swap_id")) {
      return rpcError(-32602, "Missing swap_id");
    }
    std::string swapId = root("swap_id").getString();
    if (!isValidSwapId(swapId)) {
      return rpcError(-32602, "Invalid swap_id");
    }

    if (!m_daemon.refund(swapId)) {
      return rpcError(-32000, "Refund failed for swap: " + swapId);
    }

    return R"({"success": true, "swap_id": ")" + swapId + R"("})";
  } catch (const std::exception& e) {
    return rpcError(-32602, std::string("Parameter error: ") + e.what());
  }
}

std::string RpcServer::handleCheckTimeouts(const std::string& /*params*/) {
  // Process all active swaps and refund any that timed out
  auto ids = m_daemon.database().listSwaps();
  std::vector<std::string> refunded;

  for (const auto& id : ids) {
    SwapStateMachine sm;
    if (m_daemon.database().loadSwap(id, sm)) {
      if (!sm.isTerminal()) {
        m_daemon.processSwap(sm);
        // Check if it transitioned to REFUNDED
        if (sm.currentState() == SwapState::ADAPTOR_REFUNDED ||
            sm.currentState() == SwapState::XFG_REFUNDED) {
          refunded.push_back(id);
        }
      }
    }
  }

  // Also run the dedicated timeout check
  m_daemon.checkTimeouts();

  std::ostringstream oss;
  oss << R"({"processed": )" << ids.size()
      << R"(, "refunded": [)";
  bool first = true;
  for (const auto& id : refunded) {
    if (!first) oss << ",";
    first = false;
    oss << R"(")" << id << R"(")";
  }
  oss << R"(]})";
  return oss.str();
}

std::string RpcServer::rpcError(int code, const std::string& message, int id) {
  Common::JsonValue error(Common::JsonValue::OBJECT);
  error.insert("code", static_cast<int64_t>(code));
  error.insert("message", message);
  Common::JsonValue response(Common::JsonValue::OBJECT);
  response.insert("jsonrpc", std::string("2.0"));
  response.insert("error", std::move(error));
  response.insert("id", static_cast<int64_t>(id));
  return response.toString();
}

std::string RpcServer::rpcSuccess(const std::string& result, int id) {
  Common::JsonValue response(Common::JsonValue::OBJECT);
  response.insert("jsonrpc", std::string("2.0"));
  response.insert("result", Common::JsonValue::fromString(result));
  response.insert("id", static_cast<int64_t>(id));
  return response.toString();
}

} // namespace XfgSwap
