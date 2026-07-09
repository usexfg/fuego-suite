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

#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include "crypto/hash.h"
#include "crypto/crypto.h"

namespace XfgSwap {

struct NodeInfo {
  uint32_t height;
  uint64_t difficulty;
  uint64_t txCount;
  std::string status;
};

// Live Hearth pool price from fuegod /get_fuego_price
struct FuegoPrice {
  uint64_t reserveXfg;
  uint64_t reserveHeat;
  uint64_t spotPrice;           // XFG/HEAT × 10^18
  uint64_t redemptionPriceNum;
  uint64_t redemptionPriceDenom;
  double   xfgHeatRatio;        // parsed from string
  double   heatPegUsd;          // parsed from string
  double   xfgSpotUsd;          // parsed from string
};

// Result from a wallet RPC transfer call.
struct TransferResult {
  std::string txHash;       // hex-encoded transaction hash
  std::string txSecretKey;  // hex-encoded tx secret key
};

// Minimal parsed output from a fetched transaction.
struct TxOutputInfo {
  uint64_t amount;
  Crypto::PublicKey targetKey;  // KeyOutput target public key
};

// Decoy output for ring signature construction.
struct RandomOutputEntry {
  uint64_t globalIndex;
  Crypto::PublicKey outKey;
};

class FuegoRpcClient {
public:
  FuegoRpcClient(const std::string& host, uint16_t port);

  // Configure the wallet RPC endpoint (separate from fuegod).
  // Must be called before sendTransfer().
  void setWalletRpc(const std::string& host, uint16_t port);

  // Set authentication credentials for wallet RPC.
  void setWalletAuth(const std::string& user, const std::string& pass) {
    m_walletUser = user; m_walletPass = pass;
  }

  // Run wallet optimize/fusion to consolidate dust outputs.
  // Returns tx_hash + tx_secret_key in TransferResult.
  bool optimizeWallet(uint64_t threshold, TransferResult& result);

  // Convenience: POST to fuegod
  std::string daemonPost(const std::string& path, const std::string& body);

  // Query /getheight
  bool getHeight(uint32_t& height);

  // Relay via /sendrawtransaction
  bool sendRawTransaction(const std::string& txHex);

  // Report atomic swap fee to daemon → blockchain fee pool
  bool addSwapFee(uint64_t amount);

  // Query /getinfo
  bool getInfo(NodeInfo& info);

  // Query /get_fuego_price — live Hearth pool XFG/USD price
  bool getFuegoPrice(FuegoPrice& price);

  // Feed XFG/USD market value back to fuegod (PI controller removed — value is ignored)
  bool setXfgMarketValue(uint64_t val);

  // ── Wallet RPC (talks to fire_wallet --rpc-bind-port) ──

  // Send XFG via wallet RPC "transfer" method.
  // address: base58-encoded destination, amount: atomic units.
  // Returns true on success, fills result with tx hash and secret key.
  bool sendTransfer(const std::string& address, uint64_t amount,
                    uint64_t mixin, TransferResult& result);

  // Call create_afk_lock on the wallet RPC
  bool createAfkLock(uint64_t amount, uint32_t timeout_hours, uint8_t pair, std::string& lockId, std::string& adaptorPoint, std::string& preSig);

  // ── Daemon RPC for tx inspection ──

  // Fetch a transaction by hash and extract its outputs.
  // txHashHex: 64-char hex hash. Returns false if tx not found.
  bool getTransactionOutputs(const std::string& txHashHex,
                             std::vector<TxOutputInfo>& outputs);

  // Get random outputs for ring decoys at a given amount.
  // Returns `count` random {global_index, public_key} pairs via the JSON endpoint.
  bool getRandomOutputs(uint64_t amount, uint64_t count,
                        std::vector<RandomOutputEntry>& entries);

  // Resolve an alias name to an XFG address. Returns false if not found.
  bool resolveAlias(const std::string& alias, std::string& addressOut);

  bool checkReserveProof(const std::string& address, const std::string& message,
                         const std::string& signature, bool& good, uint64_t& total);

private:
  // Synchronous HTTP POST to an arbitrary host:port
  std::string httpPost(const std::string& host, uint16_t port,
                       const std::string& path, const std::string& body);

  // Convenience: JSON-RPC call to wallet RPC
  std::string walletJsonRpc(const std::string& method, const std::string& params);

  std::string m_host;
  uint16_t m_port;

  // Wallet RPC endpoint (optional, needed for sendTransfer)
  std::string m_walletHost;
  uint16_t m_walletPort = 0;
  std::string m_walletUser;
  std::string m_walletPass;
};

} // namespace XfgSwap
