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

  out.xmrSpendKeyHex = jsonGetStr(json, "xmr_spend_key");
  out.xmrViewKeyHex  = jsonGetStr(json, "xmr_view_key");

  // XFG wallet key for signing managed offers
  out.xfgSecretKeyHex = jsonGetStr(json, "xfg_secret_key");

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

  return true;
}

} // namespace XfgSwap
