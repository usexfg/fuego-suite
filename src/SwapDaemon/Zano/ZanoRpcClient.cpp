// Copyright (c) 2017-2026 Fuego Developers
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

// Zano RPC client for atomic swaps.
// Talks to zanod (daemon) and zano-wallet-rpc (simplewallet) via JSON-RPC 2.0.
// Uses POSIX sockets for HTTP — no external dependencies.

#include "ZanoRpcClient.h"
#include "ZanoAddress.h"
#include "Common/WinCompat.h"

#include <array>
#include <cstring>
#include <sstream>
#include <stdexcept>

#include "Common/JsonValue.h"
#include "AdaptorSignature.h"

extern "C" {
#include "crypto/crypto-ops.h"
}

namespace XfgSwap {

ZanoRpcClient::ZanoRpcClient(const std::string& daemonHost, uint16_t daemonPort,
                                 const std::string& walletHost, uint16_t walletPort)
    : m_daemonHost(daemonHost)
    , m_daemonPort(daemonPort)
    , m_walletHost(walletHost)
    , m_walletPort(walletPort) {
}

// ---------------------------------------------------------------------------
// Low-level HTTP/JSON-RPC helpers
// ---------------------------------------------------------------------------

std::string ZanoRpcClient::httpPost(const std::string& host, uint16_t port,
                                      const std::string& path, const std::string& body) {
  int sockfd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    return {};
  }

  struct hostent* server = ::gethostbyname(host.c_str());
  if (!server) {
    ::close(sockfd);
    return {};
  }

  struct sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  std::memcpy(&addr.sin_addr.s_addr, server->h_addr, server->h_length);
  addr.sin_port = htons(port);

  // Set a 10-second connect/read timeout
#ifdef _WIN32
  DWORD tvMs = 10000;
  ::setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tvMs), sizeof(tvMs));
  ::setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tvMs), sizeof(tvMs));
#else
  struct timeval tv;
  tv.tv_sec = 10;
  tv.tv_usec = 0;
  ::setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  ::setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif

  if (::connect(sockfd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
    ::close(sockfd);
    return {};
  }

  // Build HTTP request
  std::ostringstream req;
  req << "POST " << path << " HTTP/1.1\r\n";
  req << "Host: " << host << ":" << port << "\r\n";
  req << "Content-Type: application/json\r\n";
  req << "Content-Length: " << body.size() << "\r\n";
  req << "Connection: close\r\n";
  req << "\r\n";
  req << body;

  std::string request = req.str();
  ssize_t sent = ::send(sockfd, request.c_str(), request.size(),
#ifndef _WIN32
                       MSG_NOSIGNAL
#else
                       0
#endif
                       );
  if (sent < 0 || static_cast<size_t>(sent) != request.size()) {
    ::close(sockfd);
    return {};
  }

  // Read response
  std::string response;
  char buf[4096];
  for (;;) {
    ssize_t n = ::recv(sockfd, buf, sizeof(buf), 0);
    if (n <= 0) break;
    response.append(buf, static_cast<size_t>(n));
  }

  ::close(sockfd);

  // Strip HTTP headers — find the blank line separating headers from body
  auto headerEnd = response.find("\r\n\r\n");
  if (headerEnd == std::string::npos) {
    return {};
  }
  return response.substr(headerEnd + 4);
}

std::string ZanoRpcClient::jsonRpc(const std::string& host, uint16_t port,
                                     const std::string& method, const std::string& params) {
  // Zano JSON-RPC 2.0 envelope
  std::ostringstream body;
  body << "{\"jsonrpc\":\"2.0\",\"id\":\"0\",\"method\":\"" << method << "\"";
  if (!params.empty()) {
    body << ",\"params\":" << params;
  } else {
    body << ",\"params\":{}";
  }
  body << "}";

  return httpPost(host, port, "/json_rpc", body.str());
}

// ---------------------------------------------------------------------------
// JSON parsing helpers (using Common::JsonValue from the Fuego codebase)
// ---------------------------------------------------------------------------

// Extract a nested field from a JSON-RPC response.
// Returns false if parsing fails or "result" is absent.
static bool parseJsonRpcResult(const std::string& raw, Common::JsonValue& result) {
  if (raw.empty()) return false;

  try {
    Common::JsonValue root = Common::JsonValue::fromString(raw);
    if (!root.isObject()) return false;
    if (!root.contains("result")) return false;
    result = root("result");
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

// ---------------------------------------------------------------------------
// Daemon RPC (zanod, default port 11211)
// ---------------------------------------------------------------------------

std::string ZanoRpcClient::walletRpc(const std::string& method, const std::string& params) {
  return jsonRpc(m_walletHost, m_walletPort, method, params);
}

void ZanoRpcClient::syncPollDelay() {
  // ~1.5s between sync polls so the from-keys wallet scan can make progress.
#ifdef _WIN32
  Sleep(1500);
#else
  usleep(1500 * 1000);
#endif
}

bool ZanoRpcClient::getHeight(uint64_t& height) {
  // zanod "get_info" returns { ..., "height": N, ... }
  std::string raw = jsonRpc(m_daemonHost, m_daemonPort, "get_info", "{}");

  Common::JsonValue result(Common::JsonValue::NIL);
  if (!parseJsonRpcResult(raw, result)) return false;

  if (!result.isObject() || !result.contains("height")) return false;
  height = static_cast<uint64_t>(result("height").getInteger());
  return true;
}

bool ZanoRpcClient::getTransaction(const std::string& txHash, ZanoTxInfo& info) {
  // get_transactions is a non-JSON-RPC endpoint on zanod
  // POST /get_transactions with {"txs_hashes": ["<hash>"], "decode_as_json": true}
  std::ostringstream body;
  body << "{\"txs_hashes\":[\"" << txHash << "\"],\"decode_as_json\":true}";

  std::string raw = httpPost(m_daemonHost, m_daemonPort, "/get_transactions", body.str());
  if (raw.empty()) return false;

  try {
    Common::JsonValue root = Common::JsonValue::fromString(raw);
    if (!root.isObject()) return false;

    // Check status
    if (root.contains("status")) {
      std::string status = root("status").getString();
      if (status != "OK") return false;
    }

    if (!root.contains("txs") || !root("txs").isArray()) return false;
    const auto& txs = root("txs").getArray();
    if (txs.empty()) return false;

    const auto& tx = txs[0];
    info.txHash = txHash;
    info.inPool = false;

    if (tx.contains("in_pool") && tx("in_pool").isBool()) {
      info.inPool = tx("in_pool").getBool();
    }

    if (tx.contains("block_height") && tx("block_height").isInteger()) {
      // To get confirmations we need the current height
      uint64_t txHeight = static_cast<uint64_t>(tx("block_height").getInteger());
      uint64_t curHeight = 0;
      if (getHeight(curHeight) && curHeight >= txHeight) {
        info.confirmations = static_cast<uint32_t>(curHeight - txHeight + 1);
      } else {
        info.confirmations = 0;
      }
    } else {
      info.confirmations = 0;
    }

    // Amount is not directly available from get_transactions for incoming;
    // the caller should check balance on the shared address instead.
    info.amount = 0;

    return true;
  } catch (const std::exception&) {
    return false;
  }
}

// ---------------------------------------------------------------------------
// Wallet RPC (zano-wallet-rpc / simplewallet, operator --rpc-bind-port)
// ---------------------------------------------------------------------------

// Decode a 64-char hex string to 32 bytes. Returns false on bad input.
static bool hexTo32(const std::string& h, std::vector<uint8_t>& out) {
  if (h.size() != 64) return false;
  out.resize(32);
  for (size_t i = 0; i < 32; ++i) {
    auto nib = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return -1;
    };
    int hi = nib(h[2 * i]), lo = nib(h[2 * i + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return true;
}

bool ZanoRpcClient::createSharedAddress(const std::string& aliceSpendPub,
                                          const std::string& bobSpendPub,
                                          const std::string& aliceViewPub,
                                          const std::string& bobViewPub,
                                          std::string& sharedAddress,
                                          uint64_t networkPrefix) {
  sharedAddress.clear();
  std::vector<uint8_t> aS, bS, aV, bV;
  if (!hexTo32(aliceSpendPub, aS) || !hexTo32(bobSpendPub, bS) ||
      !hexTo32(aliceViewPub, aV) || !hexTo32(bobViewPub, bV)) {
    return false;
  }
  // shared spend pub = A_spend + B_spend ; shared view pub = A_view + B_view
  std::vector<uint8_t> sharedSpend, sharedView;
  if (!ZanoAddress::sharedSpendPub(aS, bS, sharedSpend)) return false;
  if (!ZanoAddress::sharedSpendPub(aV, bV, sharedView)) return false;
  sharedAddress = ZanoAddress::encode(sharedSpend, sharedView, networkPrefix);
  return !sharedAddress.empty();
}

bool ZanoRpcClient::transferToShared(const std::string& address, uint64_t amount,
                                       ZanoTransferResult& result) {
  // zano-wallet-rpc "transfer" method
  // Sends ZANO from the currently opened wallet to the specified address
  std::ostringstream params;
  params << "{\"destinations\":[{\"amount\":" << amount
         << ",\"address\":\"" << address << "\"}]"
         << ",\"priority\":1"       // default priority
         << ",\"ring_size\":16"     // Zano ring size
         << ",\"get_tx_hex\":false"
         << ",\"get_tx_key\":true"
         << "}";

  std::string raw = jsonRpc(m_walletHost, m_walletPort, "transfer", params.str());

  Common::JsonValue res(Common::JsonValue::NIL);
  if (!parseJsonRpcResult(raw, res)) {
    result.success = false;
    result.error = "JSON-RPC call failed or returned error";
    // Try to extract error message
    if (!raw.empty()) {
      try {
        Common::JsonValue root = Common::JsonValue::fromString(raw);
        if (root.isObject() && root.contains("error")) {
          const auto& err = root("error");
          if (err.isObject() && err.contains("message")) {
            result.error = err("message").getString();
          }
        }
      } catch (const std::exception&) {}
    }
    return false;
  }

  result.success = true;
  if (res.contains("tx_hash")) {
    result.txHash = res("tx_hash").getString();
  }
  if (res.contains("fee")) {
    result.fee = static_cast<uint64_t>(res("fee").getInteger());
  } else {
    result.fee = 0;
  }
  result.error.clear();
  return true;
}

bool ZanoRpcClient::sweepSharedAddress(const std::string& spendKeyHex,
                                         const std::string& viewKeyHex,
                                         const std::string& destAddress,
                                         ZanoTransferResult& result,
                                         const std::string& walletName,
                                         uint64_t restoreHeight,
                                         uint64_t targetHeight,
                                         uint64_t networkPrefix) {
  // 1. generate_from_keys (per-swap wallet name; restore from the lock height
  //    to avoid a full-chain rescan), 2. WAIT FOR SYNC, 3. sweep_all.

  // Derive the wallet's primary address from the (secret) spend/view keys:
  // pub = sec*G for each, then encode(spendPub, viewPub, prefix). Some
  // zano-wallet-rpc builds reject an empty address in generate_from_keys, so
  // we always pass the matching address. This also asserts the keys are valid
  // ed25519 scalars before we touch the wallet.
  std::vector<uint8_t> spendSec, viewSec;
  if (!hexTo32(spendKeyHex, spendSec) || !hexTo32(viewKeyHex, viewSec)) {
    result.success = false;
    result.error = "Invalid spend/view key hex";
    return false;
  }
  std::string derivedAddr;
  {
    ge_p3 p;
    unsigned char spendPub[32], viewPub[32];
    ge_scalarmult_base(&p, spendSec.data());
    ge_p3_tobytes(spendPub, &p);
    ge_scalarmult_base(&p, viewSec.data());
    ge_p3_tobytes(viewPub, &p);
    derivedAddr = ZanoAddress::encode(
        std::vector<uint8_t>(spendPub, spendPub + 32),
        std::vector<uint8_t>(viewPub, viewPub + 32), networkPrefix);
    if (derivedAddr.empty()) {
      result.success = false;
      result.error = "Failed to derive wallet address from keys";
      return false;
    }
  }

  // Close any currently open wallet first.
  walletRpc("close_wallet", "{}");

  // Per-swap filename so concurrent swaps don't collide on one temp wallet.
  const std::string filename =
      walletName.empty() ? std::string("swap_sweep_default")
                         : std::string("swap_sweep_") + walletName;

  std::ostringstream genParams;
  genParams << "{\"filename\":\"" << filename << "\""
            << ",\"address\":\"" << derivedAddr << "\""
            << ",\"spendkey\":\"" << spendKeyHex << "\""
            << ",\"viewkey\":\"" << viewKeyHex << "\""
            << ",\"password\":\"\""
            << ",\"restore_height\":" << restoreHeight
            << "}";

  std::string genRaw = walletRpc("generate_from_keys", genParams.str());
  Common::JsonValue genRes(Common::JsonValue::NIL);
  if (!parseJsonRpcResult(genRaw, genRes)) {
    result.success = false;
    result.error = "Failed to generate wallet from keys";
    return false;
  }

  // 2. Wait for the from-keys wallet to scan/sync BEFORE sweeping — otherwise
  //    sweep_all sees no outputs. Each iteration DRIVES the scan with a
  //    synchronous "refresh" (the wallet does not advance on its own between
  //    instant RPC calls), then checks get_height. If targetHeight is known
  //    (the daemon height) wait until the wallet reaches it; otherwise wait
  //    until the scan height stabilizes. A delay between polls gives the scan
  //    time to make progress. Never sweep an unsynced wallet.
  {
    const int kMaxPolls = 120;       // bounded
    uint64_t prevHeight = 0;
    bool synced = false;
    for (int i = 0; i < kMaxPolls; ++i) {
      // Drive the wallet scan forward synchronously.
      walletRpc("refresh", "{}");

      std::string hRaw = walletRpc("get_height", "{}");
      Common::JsonValue hRes(Common::JsonValue::NIL);
      uint64_t wh = 0;
      if (parseJsonRpcResult(hRaw, hRes) && hRes.contains("height")) {
        wh = static_cast<uint64_t>(hRes("height").getInteger());
      }
      if (targetHeight > 0) {
        if (wh >= targetHeight) { synced = true; break; }
      } else {
        if (i > 0 && wh == prevHeight && wh > 0) { synced = true; break; }
      }
      prevHeight = wh;
      syncPollDelay();
    }
    if (!synced) {
      result.success = false;
      result.error = "Wallet did not sync before sweep deadline";
      return false;
    }
  }

  // 3. sweep_all to destination.
  std::ostringstream sweepParams;
  sweepParams << "{\"address\":\"" << destAddress << "\""
              << ",\"priority\":1"
              << ",\"ring_size\":16"
              << "}";

  std::string sweepRaw = walletRpc("sweep_all", sweepParams.str());
  Common::JsonValue sweepRes(Common::JsonValue::NIL);
  if (!parseJsonRpcResult(sweepRaw, sweepRes)) {
    result.success = false;
    result.error = "sweep_all failed";
    // Try to extract error
    if (!sweepRaw.empty()) {
      try {
        Common::JsonValue root = Common::JsonValue::fromString(sweepRaw);
        if (root.isObject() && root.contains("error")) {
          const auto& err = root("error");
          if (err.isObject() && err.contains("message")) {
            result.error = err("message").getString();
          }
        }
      } catch (const std::exception&) {}
    }
    return false;
  }

  result.success = true;
  if (sweepRes.contains("tx_hash_list") && sweepRes("tx_hash_list").isArray()) {
    const auto& hashes = sweepRes("tx_hash_list").getArray();
    if (!hashes.empty()) {
      result.txHash = hashes[0].getString();
    }
  }
  if (sweepRes.contains("fee_list") && sweepRes("fee_list").isArray()) {
    const auto& fees = sweepRes("fee_list").getArray();
    if (!fees.empty()) {
      result.fee = static_cast<uint64_t>(fees[0].getInteger());
    }
  } else {
    result.fee = 0;
  }
  result.error.clear();
  return true;
}

bool ZanoRpcClient::openWatchOnly(const std::string& address,
                                    const std::string& viewKeyHex,
                                    const std::string& walletName,
                                    uint64_t restoreHeight) {
  if (address.empty() || viewKeyHex.size() != 64) return false;

  walletRpc("close_wallet", "{}");

  const std::string filename =
      walletName.empty() ? std::string("swap_watch_default")
                         : std::string("swap_watch_") + walletName;

  // Watch-only: spendkey empty. zano-wallet-rpc opens a view-only wallet.
  std::ostringstream genParams;
  genParams << "{\"filename\":\"" << filename << "\""
            << ",\"address\":\"" << address << "\""
            << ",\"viewkey\":\"" << viewKeyHex << "\""
            << ",\"spendkey\":\"\""
            << ",\"password\":\"\""
            << ",\"restore_height\":" << restoreHeight
            << "}";

  std::string genRaw = walletRpc("generate_from_keys", genParams.str());
  Common::JsonValue genRes(Common::JsonValue::NIL);
  if (!parseJsonRpcResult(genRaw, genRes)) {
    // Wallet may already exist — try open_wallet
    std::ostringstream openParams;
    openParams << "{\"filename\":\"" << filename << "\",\"password\":\"\"}";
    std::string openRaw = walletRpc("open_wallet", openParams.str());
    Common::JsonValue openRes(Common::JsonValue::NIL);
    if (!parseJsonRpcResult(openRaw, openRes)) return false;
  }

  // Drive a short refresh so outputs are visible
  for (int i = 0; i < 5; ++i) {
    walletRpc("refresh", "{}");
    syncPollDelay();
  }
  return true;
}

bool ZanoRpcClient::checkAddressBalance(const std::string& address,
                                          const std::string& viewKeyHex,
                                          uint64_t& balance, uint64_t& unlocked,
                                          uint64_t restoreHeight) {
  balance = 0;
  unlocked = 0;
  if (!openWatchOnly(address, viewKeyHex, /*walletName=*/address.substr(0, 16),
                     restoreHeight)) {
    return false;
  }

  std::string raw = walletRpc("get_balance", "{\"account_index\":0}");
  Common::JsonValue res(Common::JsonValue::NIL);
  if (!parseJsonRpcResult(raw, res)) {
    walletRpc("close_wallet", "{}");
    return false;
  }

  if (res.contains("balance"))
    balance = static_cast<uint64_t>(res("balance").getInteger());
  if (res.contains("unlocked_balance"))
    unlocked = static_cast<uint64_t>(res("unlocked_balance").getInteger());

  walletRpc("close_wallet", "{}");
  return true;
}

// ─── Adaptor-signature operations (CLSAG adaptor path) ──────────────────────

bool ZanoRpcClient::lockAdaptor(const std::string& sharedAddress,
                                   uint64_t amountAtoms,
                                   ZanoTransferResult& result) {
  // Delegate to transferToShared — the lock IS the transfer to the shared address.
  // No on-chain script; the adaptor secret reveals the spend key.
  return transferToShared(sharedAddress, amountAtoms, result);
}

bool ZanoRpcClient::verifyLock(const std::string& sharedAddress,
                                  const std::string& viewKeyHex,
                                  uint64_t expectedAtoms) {
  if (viewKeyHex.empty()) return false;
  uint64_t balance = 0, unlocked = 0;
  if (!checkAddressBalance(sharedAddress, viewKeyHex, balance, unlocked))
    return false;
  // Require UNLOCKED balance: ZANO needs ~10 confirmations to become spendable.
  return unlocked >= expectedAtoms;
}

bool ZanoRpcClient::claimAdaptor(const std::string& aliceSpendKeyHex,
                                    const std::string& bobSpendKeyHex,
                                    const std::string& adaptorSecretHex,
                                    const std::string& viewKeyHex,
                                    const std::string& destAddress,
                                    ZanoTransferResult& result) {
  // Helper: hex decode string to 32-byte array
  auto hexDecode = [](const std::string& hex, std::array<uint8_t, 32>& out) -> bool {
    if (hex.length() != 64) return false;
    for (size_t i = 0; i < 32; ++i) {
      uint8_t high, low;
      if (hex[2*i] >= '0' && hex[2*i] <= '9') high = hex[2*i] - '0';
      else if (hex[2*i] >= 'a' && hex[2*i] <= 'f') high = hex[2*i] - 'a' + 10;
      else if (hex[2*i] >= 'A' && hex[2*i] <= 'F') high = hex[2*i] - 'A' + 10;
      else return false;
      if (hex[2*i+1] >= '0' && hex[2*i+1] <= '9') low = hex[2*i+1] - '0';
      else if (hex[2*i+1] >= 'a' && hex[2*i+1] <= 'f') low = hex[2*i+1] - 'a' + 10;
      else if (hex[2*i+1] >= 'A' && hex[2*i+1] <= 'F') low = hex[2*i+1] - 'A' + 10;
      else return false;
      out[i] = (high << 4) | low;
    }
    return true;
  };

  // Decode all three scalars from hex
  std::array<uint8_t, 32> alice, bob, adaptor;
  if (!hexDecode(aliceSpendKeyHex, alice)) return false;
  if (!hexDecode(bobSpendKeyHex, bob)) return false;
  if (!hexDecode(adaptorSecretHex, adaptor)) return false;

  // 2-term key model: shared spend key = alice + bob (mod l).
  // The adaptor secret is NOT added — the shared address was funded to
  // A+B (2-term), so the sweep key must be the 2-term scalar a+b.
  std::array<uint8_t, 32> combined;
  sc_add(combined.data(), alice.data(), bob.data());
  sc_reduce32(combined.data());
  // Check for zero-result (would brick the wallet)
  bool isZero = true;
  for (size_t i = 0; i < 32; ++i) {
    if (combined[i] != 0) { isZero = false; break; }
  }
  if (isZero) return false;

  // Translate combined scalar back to hex for sweepSharedAddress
  static const char* hex = "0123456789abcdef";
  std::string combinedHex(64, '\0');
  for (size_t i = 0; i < 32; ++i) {
    combinedHex[2*i]     = hex[(combined[i] >> 4) & 0xF];
    combinedHex[2*i + 1] = hex[combined[i] & 0xF];
  }

  // Sweep using the combined spend key
  return sweepSharedAddress(combinedHex, viewKeyHex, destAddress, result);
}

bool ZanoRpcClient::refundAdaptor(const std::string& aliceShareHex,
                                     const std::string& bobShareHex,
                                     const std::string& viewKeyHex,
                                     const std::string& destAddress,
                                     ZanoTransferResult& result) {
  // Helper: hex decode string to 32-byte array
  auto hexDecode = [](const std::string& hex, std::array<uint8_t, 32>& out) -> bool {
    if (hex.length() != 64) return false;
    for (size_t i = 0; i < 32; ++i) {
      uint8_t high, low;
      if (hex[2*i] >= '0' && hex[2*i] <= '9') high = hex[2*i] - '0';
      else if (hex[2*i] >= 'a' && hex[2*i] <= 'f') high = hex[2*i] - 'a' + 10;
      else if (hex[2*i] >= 'A' && hex[2*i] <= 'F') high = hex[2*i] - 'A' + 10;
      else return false;
      if (hex[2*i+1] >= '0' && hex[2*i+1] <= '9') low = hex[2*i+1] - '0';
      else if (hex[2*i+1] >= 'a' && hex[2*i+1] <= 'f') low = hex[2*i+1] - 'a' + 10;
      else if (hex[2*i+1] >= 'A' && hex[2*i+1] <= 'F') low = hex[2*i+1] - 'A' + 10;
      else return false;
      out[i] = (high << 4) | low;
    }
    return true;
  };

  // Decode both party shares from hex
  std::array<uint8_t, 32> aliceShare, bobShare;
  if (!hexDecode(aliceShareHex, aliceShare)) return false;
  if (!hexDecode(bobShareHex, bobShare)) return false;

  // 2-term key model: combined = aliceShare + bobShare (mod l).
  // No adaptor secret involved in cooperative refund.
  std::array<uint8_t, 32> combined;
  sc_add(combined.data(), aliceShare.data(), bobShare.data());
  sc_reduce32(combined.data());
  // Check for zero-result (would brick the wallet)
  bool isZero = true;
  for (size_t i = 0; i < 32; ++i) {
    if (combined[i] != 0) { isZero = false; break; }
  }
  if (isZero) return false;

  // Translate combined scalar back to hex for sweepSharedAddress
  static const char* hex = "0123456789abcdef";
  std::string combinedHex(64, '\0');
  for (size_t i = 0; i < 32; ++i) {
    combinedHex[2*i]     = hex[(combined[i] >> 4) & 0xF];
    combinedHex[2*i + 1] = hex[combined[i] & 0xF];
  }

  // Sweep using the combined spend key
  return sweepSharedAddress(combinedHex, viewKeyHex, destAddress, result);
}

bool ZanoRpcClient::checkReserveProof(const std::string& address, const std::string& message,
                                        const std::string& signature, bool& good, uint64_t& total) {
  std::ostringstream params;
  params << "{\"address\":\"" << address << "\""
         << ",\"message\":\"" << message << "\""
         << ",\"signature\":\"" << signature << "\"}";

  std::string raw = jsonRpc(m_walletHost, m_walletPort, "check_reserve_proof", params.str());

  Common::JsonValue res(Common::JsonValue::NIL);
  if (!parseJsonRpcResult(raw, res)) return false;
  if (!res.isObject()) return false;

  if (res.contains("good")) good = res("good").getBool();
  if (res.contains("total")) total = static_cast<uint64_t>(res("total").getInteger());
  return true;
}

} // namespace XfgSwap
