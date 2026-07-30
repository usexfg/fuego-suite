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

// Loader for ChainClientConfig (defined in SwapDaemon.h).
// Reads a JSON config file and populates ChainClientConfig struct.

#include "SwapDaemon.h"
#include "Common/JsonValue.h"

#include <fstream>
#include <sstream>
#include <cctype>
#include <stdexcept>

namespace XfgSwap {

// ─── Minimal JSON key-value parser ──────────────────────────────────────────

static std::string jsonGetStr(const std::string& json, const std::string& key,
                               const std::string& defaultVal = "") {
  std::string needle = "\"" + key + "\"";
  auto pos = json.find(needle);
  if (pos == std::string::npos) return defaultVal;
  pos = json.find(':', pos + needle.size());
  if (pos == std::string::npos) return defaultVal;
  ++pos;
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                                json[pos] == '\n' || json[pos] == '\r')) ++pos;
  if (pos >= json.size() || json[pos] != '"') return defaultVal;
  ++pos;
  std::string result;
  while (pos < json.size() && json[pos] != '"') {
    if (json[pos] == '\\' && pos + 1 < json.size()) { ++pos; }
    result += json[pos++];
  }
  return result;
}

static uint64_t jsonGetUint(const std::string& json, const std::string& key,
                             uint64_t defaultVal = 0) {
  std::string needle = "\"" + key + "\"";
  auto pos = json.find(needle);
  if (pos == std::string::npos) return defaultVal;
  pos = json.find(':', pos + needle.size());
  if (pos == std::string::npos) return defaultVal;
  ++pos;
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                                json[pos] == '\n' || json[pos] == '\r')) ++pos;
  if (pos >= json.size() || !std::isdigit(static_cast<unsigned char>(json[pos]))) return defaultVal;
  uint64_t val = 0;
  while (pos < json.size() && std::isdigit(static_cast<unsigned char>(json[pos]))) {
    val = val * 10 + (json[pos++] - '0');
  }
  return val;
}

static bool validateHex(const std::string& s, size_t expectedBytes,
                          const std::string& fieldName, std::string& errorMsg) {
  if (s.empty()) return true;  // optional fields allowed to be empty
  if (s.size() != expectedBytes * 2) {
    errorMsg = fieldName + " must be " + std::to_string(expectedBytes * 2) +
               " hex chars, got " + std::to_string(s.size());
    return false;
  }
  for (char c : s) {
    if (!std::isxdigit(static_cast<unsigned char>(c))) {
      errorMsg = fieldName + " contains non-hex character";
      return false;
    }
  }
  return true;
}

// ─── loadChainClientConfig ────────────────────────────────────────────────────

bool loadChainClientConfig(const std::string& path,
                            ChainClientConfig& out,
                            std::string& errorMsg) {
  std::ifstream f(path);
  if (!f.is_open()) {
    errorMsg = "Cannot open config file: " + path;
    return false;
  }
  std::ostringstream ss;
  ss << f.rdbuf();
  const std::string json = ss.str();

  // Validate the file is actually parseable JSON. Previously the loader used
  // `string::find()` lookups that silently treat malformed JSON as missing
  // keys → every field falls through to defaults → daemon registered every
  // chain at hardcoded loopback ports with no warning. An operator with a
  // typo would never know.
  try {
    Common::JsonValue::fromString(json);
  } catch (const std::exception& e) {
    errorMsg = "Config file is not valid JSON: " + std::string(e.what());
    return false;
  }

  // ── RPC endpoints ──
  // Hostname defaults are deliberately EMPTY: the SwapDaemon constructor only
  // registers a chain client when the corresponding host is non-empty. Empty
  // means "this chain is not configured" — matches the existing BASE pattern.
  out.ethHost    = jsonGetStr(json, "eth_rpc_host", "");
  out.ethPort    = static_cast<uint16_t>(jsonGetUint(json, "eth_rpc_port", 8545));

  out.bchHost    = jsonGetStr(json, "bch_rpc_host", "");
  out.bchPort    = static_cast<uint16_t>(jsonGetUint(json, "bch_rpc_port", 8332));
  out.bchRpcUser = jsonGetStr(json, "bch_rpc_user");
  out.bchRpcPass = jsonGetStr(json, "bch_rpc_pass");

  // BCH SPV mode configuration
  out.bchMode = jsonGetStr(json, "bch_mode", "");
  out.bchSpvMinServers = static_cast<size_t>(jsonGetUint(json, "bch_spv_min_servers", 1));
  out.bchSpvCheckpointHeight = jsonGetUint(json, "bch_spv_checkpoint_height", 0);
  out.bchSpvCheckpointHash = jsonGetStr(json, "bch_spv_checkpoint_hash", "");

  // Parse SPV server list: bch_spv_server_0, bch_spv_server_1, ...
  for (size_t i = 0; i < 16; ++i) {
    std::string key = "bch_spv_server_" + std::to_string(i);
    std::string server = jsonGetStr(json, key, "");
    if (server.empty()) break;
    out.bchSpvServers.push_back(server);
  }

  out.xmrDaemonHost = jsonGetStr(json, "xmr_daemon_host", "");
  out.xmrDaemonPort = static_cast<uint16_t>(jsonGetUint(json, "xmr_daemon_port", 18081));
  out.xmrWalletHost = jsonGetStr(json, "xmr_wallet_host", "");
  out.xmrWalletPort = static_cast<uint16_t>(jsonGetUint(json, "xmr_wallet_port", 18082));

  out.solHost       = jsonGetStr(json, "sol_rpc_host", "");
  out.solPort       = static_cast<uint16_t>(jsonGetUint(json, "sol_rpc_port", 8899));
  out.solProgramId  = jsonGetStr(json, "sol_program_id");
  out.solKeypairPath = jsonGetStr(json, "sol_keypair_path");
  out.bchWif          = jsonGetStr(json, "bch_wif");

  // ── Signer credentials ──
  out.ethPrivKeyHex  = jsonGetStr(json, "eth_priv_key");
  out.ethAddress     = jsonGetStr(json, "eth_address");
  out.ethChainId     = jsonGetUint(json, "eth_chain_id", 1);
  out.ethHtlcBinPath = jsonGetStr(json, "eth_htlc_bin_path");
  out.ethHtlcRegistry = jsonGetStr(json, "eth_htlc_registry");

  // ARB
  out.arbHost       = jsonGetStr(json, "arb_rpc_host", "");
  out.arbPort       = static_cast<uint16_t>(jsonGetUint(json, "arb_rpc_port", 8547));
  out.arbPrivKeyHex = jsonGetStr(json, "arb_priv_key");
  out.arbAddress    = jsonGetStr(json, "arb_address");
  out.arbChainId    = jsonGetUint(json, "arb_chain_id", 42161);
  out.arbHtlcBinPath = jsonGetStr(json, "arb_htlc_bin_path");

  // BASE
  out.baseHost       = jsonGetStr (json, "base_rpc_host", "");
  out.basePort       = static_cast<uint16_t>(jsonGetUint(json, "base_rpc_port", 8545));
  out.baseAddress    = jsonGetStr (json, "base_address",  out.ethAddress);
  out.basePrivKeyHex = jsonGetStr (json, "base_priv_key", out.ethPrivKeyHex);
  out.baseChainId    = jsonGetUint(json, "base_chain_id", 8453);
  out.baseHtlcBinPath= jsonGetStr (json, "base_htlc_bin", out.ethHtlcBinPath);

  // POLYGON (Polygon PoS — EVM, EIP-1559)
  out.polyHost       = jsonGetStr (json, "poly_rpc_host", "");
  out.polyPort       = static_cast<uint16_t>(jsonGetUint(json, "poly_rpc_port", 8545));
  out.polyPrivKeyHex = jsonGetStr (json, "poly_priv_key");
  out.polyAddress    = jsonGetStr (json, "poly_address");
  out.polyChainId    = jsonGetUint(json, "poly_chain_id", 137);
  out.polyHtlcBinPath= jsonGetStr (json, "poly_htlc_bin");

  out.xmrSpendKeyHex = jsonGetStr(json, "xmr_spend_key");
  out.xmrViewKeyHex  = jsonGetStr(json, "xmr_view_key");

  // XFG wallet key for signing managed offers
  out.xfgSecretKeyHex = jsonGetStr(json, "xfg_secret_key");
  out.xfgViewKeyHex   = jsonGetStr(json, "xfg_view_key");

  // BSC (Binance Smart Chain)
  out.bscHost       = jsonGetStr(json, "bsc_rpc_host", "");
  out.bscPort       = static_cast<uint16_t>(jsonGetUint(json, "bsc_rpc_port", 8545));
  out.bscPrivKeyHex = jsonGetStr(json, "bsc_priv_key");
  out.bscAddress    = jsonGetStr(json, "bsc_address");
  out.bscChainId    = jsonGetUint(json, "bsc_chain_id", 56);
  out.bscHtlcBinPath = jsonGetStr(json, "bsc_htlc_bin");

  // DCR (Decred)
  out.dcrHost    = jsonGetStr(json, "dcr_rpc_host", "");
  out.dcrPort    = static_cast<uint16_t>(jsonGetUint(json, "dcr_rpc_port", 9108));
  out.dcrRpcUser = jsonGetStr(json, "dcr_rpc_user");
  out.dcrRpcPass = jsonGetStr(json, "dcr_rpc_pass");
  out.dcrWif     = jsonGetStr(json, "dcr_wif", "");

  // DCR SPV mode configuration
  out.dcrMode = jsonGetStr(json, "dcr_mode", "");
  out.dcrSpvMinServers = static_cast<size_t>(jsonGetUint(json, "dcr_spv_min_servers", 1));
  out.dcrSpvCheckpointHeight = jsonGetUint(json, "dcr_spv_checkpoint_height", 0);
  out.dcrSpvCheckpointHash = jsonGetStr(json, "dcr_spv_checkpoint_hash", "");

  // Parse DCR SPV server list: dcr_spv_server_0, dcr_spv_server_1, ...
  for (size_t i = 0; i < 16; ++i) {
    std::string key = "dcr_spv_server_" + std::to_string(i);
    std::string server = jsonGetStr(json, key, "");
    if (server.empty()) break;
    out.dcrSpvServers.push_back(server);
  }

  // BTC
  out.btcHost    = jsonGetStr(json, "btc_rpc_host", "");
  out.btcPort    = static_cast<uint16_t>(jsonGetUint(json, "btc_rpc_port", 8332));
  out.btcRpcUser = jsonGetStr(json, "btc_rpc_user");
  out.btcRpcPass = jsonGetStr(json, "btc_rpc_pass");
  out.btcWif     = jsonGetStr(json, "btc_wif");

  // BTC SPV mode configuration
  out.btcMode = jsonGetStr(json, "btc_mode", "");
  out.btcSpvMinServers = static_cast<size_t>(jsonGetUint(json, "btc_spv_min_servers", 1));
  out.btcSpvCheckpointHeight = jsonGetUint(json, "btc_spv_checkpoint_height", 0);
  out.btcSpvCheckpointHash = jsonGetStr(json, "btc_spv_checkpoint_hash", "");

  // Parse BTC SPV server list: btc_spv_server_0, btc_spv_server_1, ...
  for (size_t i = 0; i < 16; ++i) {
    std::string key = "btc_spv_server_" + std::to_string(i);
    std::string server = jsonGetStr(json, key, "");
    if (server.empty()) break;
    out.btcSpvServers.push_back(server);
  }

  // LTC
  out.ltcHost    = jsonGetStr(json, "ltc_rpc_host", "");
  out.ltcPort    = static_cast<uint16_t>(jsonGetUint(json, "ltc_rpc_port", 9332));
  out.ltcRpcUser = jsonGetStr(json, "ltc_rpc_user");
  out.ltcRpcPass = jsonGetStr(json, "ltc_rpc_pass");
  out.ltcWif     = jsonGetStr(json, "ltc_wif");

  // LTC SPV mode configuration
  out.ltcMode = jsonGetStr(json, "ltc_mode", "");
  out.ltcSpvMinServers = static_cast<size_t>(jsonGetUint(json, "ltc_spv_min_servers", 1));
  out.ltcSpvCheckpointHeight = jsonGetUint(json, "ltc_spv_checkpoint_height", 0);
  out.ltcSpvCheckpointHash = jsonGetStr(json, "ltc_spv_checkpoint_hash", "");

  // Parse LTC SPV server list: ltc_spv_server_0, ltc_spv_server_1, ...
  for (size_t i = 0; i < 16; ++i) {
    std::string key = "ltc_spv_server_" + std::to_string(i);
    std::string server = jsonGetStr(json, key, "");
    if (server.empty()) break;
    out.ltcSpvServers.push_back(server);
  }

  // KMD
  out.kmdHost    = jsonGetStr(json, "kmd_rpc_host", "");
  out.kmdPort    = static_cast<uint16_t>(jsonGetUint(json, "kmd_rpc_port", 7771));
  out.kmdRpcUser = jsonGetStr(json, "kmd_rpc_user");
  out.kmdRpcPass = jsonGetStr(json, "kmd_rpc_pass");
  out.kmdWif     = jsonGetStr(json, "kmd_wif");

  // KMD SPV mode configuration
  out.kmdMode = jsonGetStr(json, "kmd_mode", "");
  out.kmdSpvMinServers = static_cast<size_t>(jsonGetUint(json, "kmd_spv_min_servers", 1));
  out.kmdSpvCheckpointHeight = jsonGetUint(json, "kmd_spv_checkpoint_height", 0);
  out.kmdSpvCheckpointHash = jsonGetStr(json, "kmd_spv_checkpoint_hash", "");

  // Parse KMD SPV server list: kmd_spv_server_0, kmd_spv_server_1, ...
  for (size_t i = 0; i < 16; ++i) {
    std::string key = "kmd_spv_server_" + std::to_string(i);
    std::string server = jsonGetStr(json, key, "");
    if (server.empty()) break;
    out.kmdSpvServers.push_back(server);
  }

  out.xfgWalletRpcHost = jsonGetStr(json, "xfg_wallet_rpc_host", "");
  out.xfgWalletRpcPort = static_cast<uint16_t>(jsonGetUint(json, "xfg_wallet_rpc_port", 0));
  out.xfgWalletRpcUser = jsonGetStr(json, "xfg_wallet_rpc_user");
  out.xfgWalletRpcPass = jsonGetStr(json, "xfg_wallet_rpc_pass");

  // ── SPV mode validation ──
  auto spvCheck = [&](const std::string& mode, const std::string& wif,
                       const std::vector<std::string>& servers,
                       const std::string& prefix) -> bool {
    if (mode == "spv") {
      if (wif.empty()) {
        errorMsg = prefix + "_wif is required when " + prefix + "_mode is spv";
        return false;
      }
      if (servers.empty()) {
        errorMsg = prefix + "_spv_server_0 is required when " + prefix + "_mode is spv";
        return false;
      }
    }
    return true;
  };
  if (!spvCheck(out.btcMode, out.btcWif, out.btcSpvServers, "btc")) return false;
  if (!spvCheck(out.ltcMode, out.ltcWif, out.ltcSpvServers, "ltc")) return false;
  if (!spvCheck(out.kmdMode, out.kmdWif, out.kmdSpvServers, "kmd")) return false;
  if (!spvCheck(out.bchMode, out.bchWif, out.bchSpvServers, "bch")) return false;
  if (!spvCheck(out.dcrMode, out.dcrWif, out.dcrSpvServers, "dcr")) return false;

  // ── Validate ──
  if (!validateHex(out.ethPrivKeyHex, 32, "eth_priv_key", errorMsg)) return false;
  if (!out.ethAddress.empty() && (out.ethAddress.size() < 2 || out.ethAddress.substr(0, 2) != "0x")) {
    errorMsg = "eth_address must start with 0x";
    return false;
  }
  if (!validateHex(out.arbPrivKeyHex, 32, "arb_priv_key", errorMsg)) return false;
  if (!out.arbAddress.empty() && (out.arbAddress.size() < 2 || out.arbAddress.substr(0, 2) != "0x")) {
    errorMsg = "arb_address must start with 0x";
    return false;
  }
  if (!validateHex(out.basePrivKeyHex, 32, "base_priv_key", errorMsg)) return false;
  if (!out.baseAddress.empty() && (out.baseAddress.size() < 2 || out.baseAddress.substr(0, 2) != "0x")) {
    errorMsg = "base_address must start with 0x";
    return false;
  }
  if (!validateHex(out.xmrSpendKeyHex, 32, "xmr_spend_key", errorMsg)) return false;
  if (!validateHex(out.xmrViewKeyHex,  32, "xmr_view_key",  errorMsg)) return false;

  if (!validateHex(out.bscPrivKeyHex, 32, "bsc_priv_key", errorMsg)) return false;
  if (!out.bscAddress.empty() && (out.bscAddress.size() < 2 || out.bscAddress.substr(0, 2) != "0x")) {
    errorMsg = "bsc_address must start with 0x";
    return false;
  }
  if (!validateHex(out.polyPrivKeyHex, 32, "poly_priv_key", errorMsg)) return false;
  if (!out.polyAddress.empty() && (out.polyAddress.size() < 2 || out.polyAddress.substr(0, 2) != "0x")) {
    errorMsg = "poly_address must start with 0x";
    return false;
  }

  return true;
}

} // namespace XfgSwap
