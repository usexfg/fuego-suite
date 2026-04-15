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

// Monero RPC client for atomic swaps.
// Talks to monerod (daemon) and monero-wallet-rpc via JSON-RPC 2.0.
// Uses POSIX sockets for HTTP — no external dependencies.

#include "MoneroRpcClient.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

#include <cstring>
#include <sstream>
#include <stdexcept>

#include "Common/JsonValue.h"

namespace XfgSwap {

MoneroRpcClient::MoneroRpcClient(const std::string& daemonHost, uint16_t daemonPort,
                                 const std::string& walletHost, uint16_t walletPort)
    : m_daemonHost(daemonHost)
    , m_daemonPort(daemonPort)
    , m_walletHost(walletHost)
    , m_walletPort(walletPort) {
}

// ---------------------------------------------------------------------------
// Low-level HTTP/JSON-RPC helpers
// ---------------------------------------------------------------------------

std::string MoneroRpcClient::httpPost(const std::string& host, uint16_t port,
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
  struct timeval tv;
  tv.tv_sec = 10;
  tv.tv_usec = 0;
  ::setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  ::setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

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
  ssize_t sent = ::send(sockfd, request.c_str(), request.size(), 0);
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

std::string MoneroRpcClient::jsonRpc(const std::string& host, uint16_t port,
                                     const std::string& method, const std::string& params) {
  // Monero JSON-RPC 2.0 envelope
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
  } catch (...) {
    return false;
  }
}

// ---------------------------------------------------------------------------
// Daemon RPC (monerod, default port 18081)
// ---------------------------------------------------------------------------

bool MoneroRpcClient::getHeight(uint64_t& height) {
  // monerod "get_info" returns { ..., "height": N, ... }
  std::string raw = jsonRpc(m_daemonHost, m_daemonPort, "get_info", "{}");

  Common::JsonValue result(Common::JsonValue::NIL);
  if (!parseJsonRpcResult(raw, result)) return false;

  if (!result.isObject() || !result.contains("height")) return false;
  height = static_cast<uint64_t>(result("height").getInteger());
  return true;
}

bool MoneroRpcClient::getTransaction(const std::string& txHash, MoneroTxInfo& info) {
  // get_transactions is a non-JSON-RPC endpoint on monerod
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
  } catch (...) {
    return false;
  }
}

// ---------------------------------------------------------------------------
// Wallet RPC (monero-wallet-rpc, default port 18082)
// ---------------------------------------------------------------------------

bool MoneroRpcClient::createSharedAddress(const std::string& aliceSpendPub,
                                          const std::string& bobSpendPub,
                                          const std::string& aliceViewPub,
                                          const std::string& bobViewPub,
                                          std::string& sharedAddress) {
  // TODO: The shared address for the XMR atomic swap is computed off-chain
  // by adding the public keys (A_spend + B_spend, A_view + B_view) and then
  // encoding the result as a standard Monero address. This does NOT go through
  // monero-wallet-rpc — it is pure Ed25519 point addition done locally.
  //
  // For now, we use generate_from_keys to import the shared view key and
  // the shared spend public key into a watch-only wallet so we can monitor
  // incoming funds. The actual spend requires both parties' secrets.
  //
  // Steps (to be wired up with AdaptorSignature key exchange):
  //   1. Compute shared_spend_pub = point_add(alice_spend_pub, bob_spend_pub)
  //   2. Compute shared_view_key  = scalar_add(alice_view_sec, bob_view_sec)
  //      (both parties share view keys during negotiation)
  //   3. Derive shared_view_pub   = shared_view_key * G
  //   4. Encode as Monero mainnet address (network byte 0x12)
  //
  // This method is a placeholder that will create a watch-only wallet
  // via monero-wallet-rpc's "generate_from_keys" once the key exchange
  // is integrated.

  (void)aliceSpendPub;
  (void)bobSpendPub;
  (void)aliceViewPub;
  (void)bobViewPub;
  sharedAddress = "";
  return false;  // Not yet implemented
}

bool MoneroRpcClient::transferToShared(const std::string& address, uint64_t amount,
                                       MoneroTransferResult& result) {
  // monero-wallet-rpc "transfer" method
  // Sends XMR from the currently opened wallet to the specified address
  std::ostringstream params;
  params << "{\"destinations\":[{\"amount\":" << amount
         << ",\"address\":\"" << address << "\"}]"
         << ",\"priority\":1"       // default priority
         << ",\"ring_size\":16"     // current Monero ring size
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
      } catch (...) {}
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

bool MoneroRpcClient::sweepSharedAddress(const std::string& spendKeyHex,
                                         const std::string& viewKeyHex,
                                         const std::string& destAddress,
                                         MoneroTransferResult& result) {
  // To sweep the shared XMR address, we need to:
  //   1. Open (or create) a wallet from the full spend+view key pair
  //   2. Wait for it to sync
  //   3. sweep_all to destAddress
  //
  // Step 1: generate_from_keys (creates a wallet with the given keys)
  // TODO: The wallet filename should be unique per swap to avoid collisions.
  //       For now we use a hardcoded name.

  // Close any currently open wallet first
  jsonRpc(m_walletHost, m_walletPort, "close_wallet", "{}");

  // Open wallet from the combined spend key and view key
  std::ostringstream genParams;
  genParams << "{\"filename\":\"swap_sweep_temp\""
            << ",\"address\":\"\"" // Will be derived from keys
            << ",\"spendkey\":\"" << spendKeyHex << "\""
            << ",\"viewkey\":\"" << viewKeyHex << "\""
            << ",\"password\":\"\""
            << ",\"restore_height\":0"  // TODO: Use a reasonable restore height
            << "}";

  std::string genRaw = jsonRpc(m_walletHost, m_walletPort, "generate_from_keys", genParams.str());
  Common::JsonValue genRes(Common::JsonValue::NIL);
  if (!parseJsonRpcResult(genRaw, genRes)) {
    result.success = false;
    result.error = "Failed to generate wallet from keys";
    return false;
  }

  // TODO: Wait for wallet to sync. In production, poll get_height until
  // the wallet height matches the daemon height. For now, we proceed
  // immediately — the caller must ensure sync is complete.

  // Step 2: sweep_all to destination
  std::ostringstream sweepParams;
  sweepParams << "{\"address\":\"" << destAddress << "\""
              << ",\"priority\":1"
              << ",\"ring_size\":16"
              << "}";

  std::string sweepRaw = jsonRpc(m_walletHost, m_walletPort, "sweep_all", sweepParams.str());
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
      } catch (...) {}
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

bool MoneroRpcClient::checkAddressBalance(const std::string& address,
                                          uint64_t& balance, uint64_t& unlocked) {
  // This requires the watch-only wallet for the shared address to be open.
  // Use get_balance from monero-wallet-rpc.
  // TODO: In a real flow, the caller must first open the watch-only wallet
  // created during createSharedAddress(). For now, we just call get_balance
  // on whatever wallet is currently open.

  (void)address;  // The open wallet determines the address

  std::string raw = jsonRpc(m_walletHost, m_walletPort, "get_balance", "{\"account_index\":0}");

  Common::JsonValue res(Common::JsonValue::NIL);
  if (!parseJsonRpcResult(raw, res)) {
    balance = 0;
    unlocked = 0;
    return false;
  }

  if (res.contains("balance")) {
    balance = static_cast<uint64_t>(res("balance").getInteger());
  } else {
    balance = 0;
  }

  if (res.contains("unlocked_balance")) {
    unlocked = static_cast<uint64_t>(res("unlocked_balance").getInteger());
  } else {
    unlocked = 0;
  }

  return true;
}

// ─── Adaptor-signature stubs (CLSAG adaptor path — TODO) ─────────────────────

bool MoneroRpcClient::lockAdaptor(const std::string& sharedAddress,
                                   uint64_t amountPiconero,
                                   MoneroTransferResult& result) {
  // Delegate to transferToShared — the lock IS the transfer to the shared address.
  // No on-chain script; the adaptor secret reveals the spend key.
  return transferToShared(sharedAddress, amountPiconero, result);
}

bool MoneroRpcClient::verifyLock(const std::string& sharedAddress,
                                  uint64_t expectedPiconero) {
  uint64_t balance = 0, unlocked = 0;
  if (!checkAddressBalance(sharedAddress, balance, unlocked)) return false;
  // Accept either locked or unlocked balance — XMR may take time to unlock.
  return balance >= expectedPiconero;
}

bool MoneroRpcClient::claimAdaptor(const std::string& aliceSpendKeyHex,
                                    const std::string& /*bobSpendKeyHex*/,
                                    const std::string& viewKeyHex,
                                    const std::string& destAddress,
                                    MoneroTransferResult& result) {
  // TODO: combine Alice + Bob spend keys with adaptor secret to form the
  // combined spend key, then call sweepSharedAddress.
  // For now delegate with Alice's key as a placeholder.
  return sweepSharedAddress(aliceSpendKeyHex, viewKeyHex, destAddress, result);
}

bool MoneroRpcClient::refundAdaptor(const std::string& spendKeyHex,
                                     const std::string& viewKeyHex,
                                     const std::string& destAddress,
                                     MoneroTransferResult& result) {
  // Cooperative refund: sweep back to the original sender using both keys.
  // TODO: requires both parties to cooperate and provide their key shares.
  return sweepSharedAddress(spendKeyHex, viewKeyHex, destAddress, result);
}

} // namespace XfgSwap
