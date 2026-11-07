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
#include "Utxo/UtxoFullNode.h"

#include <fstream>
#include <sstream>
#include <cctype>
#include <map>
#include <set>
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

static bool jsonGetBool(const std::string& json, const std::string& key,
                         bool defaultVal = false) {
  std::string needle = "\"" + key + "\"";
  auto pos = json.find(needle);
  if (pos == std::string::npos) return defaultVal;
  pos = json.find(':', pos + needle.size());
  if (pos == std::string::npos) return defaultVal;
  ++pos;
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                                json[pos] == '\n' || json[pos] == '\r')) ++pos;
  if (pos + 4 <= json.size() && json.substr(pos, 4) == "true") return true;
  if (pos + 5 <= json.size() && json.substr(pos, 5) == "false") return false;
  // Also accept 1/0
  if (pos < json.size() && json[pos] == '1') return true;
  if (pos < json.size() && json[pos] == '0') return false;
  return defaultVal;
}

// Both `swap_config.example.json` and the output of
// `xfg-swapd --generate-spv-config` are JSONC: they carry `//` guidance for
// the operator, which is the whole point of an annotated template. The reader
// below is strict JSON, so those files were rejected outright and the
// documented quickstart could not be followed.
//
// String-aware by design — a `//` inside a quoted value such as
// "https://rpc.example" must survive. Line breaks are preserved so parse
// errors still point at the right line.
static std::string stripJsonComments(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  bool inString = false;
  bool inEscape = false;

  for (size_t i = 0; i < in.size();) {
    const char c = in[i];

    if (inString) {
      out += c;
      ++i;
      if (inEscape) {
        inEscape = false;
      } else if (c == '\\') {
        inEscape = true;
      } else if (c == '"') {
        inString = false;
      }
      continue;
    }

    if (c == '"') {
      inString = true;
      out += c;
      ++i;
      continue;
    }

    if (c == '/' && i + 1 < in.size() && in[i + 1] == '/') {
      while (i < in.size() && in[i] != '\n') ++i;
      if (i < in.size()) {  // keep the newline for line numbering
        out += '\n';
        ++i;
      }
      continue;
    }

    out += c;
    ++i;
  }

  return out;
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

static bool validateEvmAddress(const std::string& address,
                               const std::string& fieldName,
                               std::string& errorMsg) {
  if (address.empty()) return true;
  if (address.size() != 42 || address.compare(0, 2, "0x") != 0) {
    errorMsg = fieldName + " must be 0x followed by 40 hex chars";
    return false;
  }
  for (size_t i = 2; i < address.size(); ++i) {
    if (!std::isxdigit(static_cast<unsigned char>(address[i]))) {
      errorMsg = fieldName + " contains non-hex character";
      return false;
    }
  }
  return true;
}

static bool getOptionalString(const Common::JsonValue& object,
                              const std::string& key,
                              std::string& value,
                              const std::string& context,
                              std::string& errorMsg) {
  if (!object.contains(key)) return true;
  if (!object(key).isString()) {
    errorMsg = context + "." + key + " must be a string";
    return false;
  }
  value = object(key).getString();
  return true;
}

static bool getOptionalUint(const Common::JsonValue& object,
                            const std::string& key,
                            uint64_t& value,
                            bool& present,
                            const std::string& context,
                            std::string& errorMsg) {
  present = object.contains(key);
  if (!present) return true;
  if (!object(key).isInteger() || object(key).getInteger() <= 0) {
    errorMsg = context + "." + key + " must be a positive integer";
    return false;
  }
  value = static_cast<uint64_t>(object(key).getInteger());
  return true;
}

// ─── loadChainClientConfig ────────────────────────────────────────────────────

namespace {

// Flat per-chain EVM fields are retired (remediation plan R5): every EVM pair
// is configured under evm_chains. A config still carrying one is refused so
// an operator never runs with a chain silently unconfigured.
const char* const RETIRED_EVM_PREFIXES[] = {
  "eth", "arb", "base", "bsc", "poly", "gleec", "rh", "avax", "cro",
  "bob", "uni", "plasma", "pulsechain", "monad", "op"};
const char* const RETIRED_EVM_SUFFIXES[] = {
  "_rpc_host", "_rpc_port", "_host", "_port", "_priv_key", "_wif", "_address",
  "_chain_id", "_htlc_bin", "_htlc_bin_path", "_htlc_registry", "_ptlc_registry", "_mode"};

bool retiredEvmKey(const std::string& key, std::string& prefix) {
  for (const char* candidate : RETIRED_EVM_PREFIXES) {
    for (const char* suffix : RETIRED_EVM_SUFFIXES) {
      if (key == std::string(candidate) + suffix) {
        prefix = candidate;
        return true;
      }
    }
  }
  return false;
}

std::string evmChainsKeyFor(const std::string& retiredPrefix) {
  static const std::map<std::string, std::string> renamed = {
    {"plasma", "xpl"}, {"pulsechain", "pls"}};
  const auto it = renamed.find(retiredPrefix);
  return it == renamed.end() ? retiredPrefix : it->second;
}

bool rejectRetiredEvmKeys(const Common::JsonValue& json, std::string& errorMsg) {
  for (const auto& field : json.getObject()) {
    std::string prefix;
    if (retiredEvmKey(field.first, prefix)) {
      errorMsg = field.first + " is retired; configure evm_chains." + evmChainsKeyFor(prefix) +
                 " with rpc_url, chain_id, private_key, address and htlc_registry";
      return false;
    }
  }
  return true;
}

}  // namespace

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
  const std::string json = stripJsonComments(ss.str());

  // Validate the file is actually parseable JSON. Previously the loader used
  // `string::find()` lookups that silently treat malformed JSON as missing
  // keys → every field falls through to defaults → daemon registered every
  // chain at hardcoded loopback ports with no warning. An operator with a
  // typo would never know.
  Common::JsonValue root;
  try {
    root = Common::JsonValue::fromString(json);
  } catch (const std::exception& e) {
    errorMsg = "Config file is not valid JSON: " + std::string(e.what());
    return false;
  }
  if (!root.isObject()) {
    errorMsg = "Config root must be a JSON object";
    return false;
  }
  if (!rejectRetiredEvmKeys(root, errorMsg)) return false;

  out.pythRpcUrl = jsonGetStr(json, "pyth_rpc_url", "");
  out.pythContract = jsonGetStr(json, "pyth_contract", "");
  out.pythChainId = jsonGetUint(json, "pyth_chain_id", 0);
  const bool anyPyth = !out.pythRpcUrl.empty() || !out.pythContract.empty() || out.pythChainId != 0;
  if (anyPyth && (out.pythRpcUrl.empty() || out.pythContract.empty() || out.pythChainId == 0)) {
    errorMsg = "pyth_rpc_url, pyth_contract and pyth_chain_id must be set together";
    return false;
  }
  if (!validateEvmAddress(out.pythContract, "pyth_contract", errorMsg)) return false;

  out.network = "mainnet";
  if (root.contains("network")) {
    if (!root("network").isString()) {
      errorMsg = "network must be a string";
      return false;
    }
    out.network = root("network").getString();
  }
  if (out.network != "mainnet" && out.network != "testnet") {
    errorMsg = "network must be exactly mainnet or testnet";
    return false;
  }

  // Preferred data-driven EVM map. A catalog entry fixes consensus-facing
  // values (pair id and chain id); config only supplies endpoint, signer, and
  // this chain's own deployed registry address.
  out.evmChains.clear();
  if (root.contains("evm_chains")) {
    const auto& evmChains = root("evm_chains");
    if (!evmChains.isObject()) {
      errorMsg = "evm_chains must be a JSON object";
      return false;
    }

    static const std::set<std::string> allowedFields = {
      "rpc_url", "private_key", "address", "htlc_registry", "htlc_registry_code_hash",
      "ptlc_registry", "htlc_bin_path", "chain_id"
    };
    std::set<SwapPair> seenPairs;
    for (const auto& namedEntry : evmChains.getObject()) {
      const std::string& key = namedEntry.first;
      const auto& entry = namedEntry.second;
      const std::string context = "evm_chains." + key;
      if (!entry.isObject()) {
        errorMsg = context + " must be a JSON object";
        return false;
      }

      SwapPair pair;
      if (!swapPairFromString(key, pair)) {
        errorMsg = context + " is not a catalogued swap chain";
        return false;
      }
      const auto* descriptor = swapPairDescriptor(pair);
      if (!descriptor || descriptor->family != SwapChainFamily::EVM) {
        errorMsg = context + " is not an EVM chain";
        return false;
      }
      if (descriptor->support == SwapPairSupport::STAGED ||
          (descriptor->txEnvelope != EvmTxEnvelope::LEGACY &&
           descriptor->txEnvelope != EvmTxEnvelope::EIP1559)) {
        errorMsg = context + " requires a specialized adapter and cannot use evm_chains";
        return false;
      }
      if (!seenPairs.insert(pair).second) {
        errorMsg = context + " duplicates another EVM chain entry";
        return false;
      }
      for (const auto& field : entry.getObject()) {
        if (allowedFields.count(field.first) == 0) {
          errorMsg = context + " has unknown field: " + field.first;
          return false;
        }
      }

      EvmChainConfig cfg;
      cfg.pair = pair;
      cfg.chainId = descriptor->chainId;
      bool hasChainId = false;
      if (!getOptionalString(entry, "rpc_url", cfg.rpcUrl, context, errorMsg) ||
          !getOptionalString(entry, "private_key", cfg.privateKeyHex, context, errorMsg) ||
          !getOptionalString(entry, "address", cfg.address, context, errorMsg) ||
          !getOptionalString(entry, "htlc_registry", cfg.htlcRegistry, context, errorMsg) ||
          !getOptionalString(entry, "htlc_registry_code_hash", cfg.htlcRegistryCodeHash, context, errorMsg) ||
          !getOptionalString(entry, "ptlc_registry", cfg.ptlcRegistry, context, errorMsg) ||
          !getOptionalString(entry, "htlc_bin_path", cfg.htlcBinPath, context, errorMsg) ||
          !getOptionalUint(entry, "chain_id", cfg.chainId, hasChainId, context, errorMsg)) {
        return false;
      }
      if (out.network == "testnet" && !hasChainId) {
        errorMsg = context + ".chain_id is required on testnet";
        return false;
      }
      if (out.network == "mainnet" && cfg.chainId != descriptor->chainId) {
        errorMsg = context + ".chain_id does not match the mainnet catalog";
        return false;
      }
      if (cfg.rpcUrl.empty()) {
        errorMsg = context + ".rpc_url is required";
        return false;
      }
      if (!validateHex(cfg.privateKeyHex, 32, context + ".private_key", errorMsg) ||
          !validateHex(cfg.htlcRegistryCodeHash, 32, context + ".htlc_registry_code_hash", errorMsg) ||
          !validateEvmAddress(cfg.address, context + ".address", errorMsg) ||
          !validateEvmAddress(cfg.htlcRegistry, context + ".htlc_registry", errorMsg) ||
          !validateEvmAddress(cfg.ptlcRegistry, context + ".ptlc_registry", errorMsg)) {
        return false;
      }
      if (cfg.privateKeyHex.empty() != cfg.address.empty()) {
        errorMsg = context +
                   " must configure private_key and address together";
        return false;
      }
      out.evmChains.push_back(std::move(cfg));
    }
  }

  // ── RPC endpoints ──
  // Hostname defaults are deliberately EMPTY: the SwapDaemon constructor only
  // registers a chain client when the corresponding host is non-empty. Empty
  // means "this chain is not configured" — matches the existing BASE pattern.

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
  const uint64_t solPort = jsonGetUint(json, "sol_rpc_port", 8899);
  if (!out.solHost.empty() && out.solHost != "127.0.0.1") {
    errorMsg = "sol_rpc_host must be 127.0.0.1 until authenticated SOL RPC transport is available";
    return false;
  }
  if (!out.solHost.empty() && (solPort == 0 || solPort > 65535)) {
    errorMsg = "sol_rpc_port must be between 1 and 65535";
    return false;
  }
  out.solPort       = static_cast<uint16_t>(solPort);
  out.solProgramId  = jsonGetStr(json, "sol_program_id");
  out.solKeypairPath = jsonGetStr(json, "sol_keypair_path");
  out.solKeypairBase58 = jsonGetStr(json, "sol_keypair");
  out.bchWif          = jsonGetStr(json, "bch_wif");
  out.bchNetwork = jsonGetStr(json, "bch_network", "main");

  // Sia (SC)
  out.siaHost         = jsonGetStr(json, "sia_rpc_host", "");
  out.siaPort         = static_cast<uint16_t>(jsonGetUint(json, "sia_rpc_port", 9980));
  out.siaApiPassword  = jsonGetStr(json, "sia_api_password", "");

  // DOGE (Dogecoin — pre-SegWit UTXO, P2SH + legacy sighash)
  out.dogeHost    = jsonGetStr(json, "doge_rpc_host", "");
  out.dogePort    = static_cast<uint16_t>(jsonGetUint(json, "doge_rpc_port", 22556));
  out.dogeRpcUser = jsonGetStr(json, "doge_rpc_user");
  out.dogeRpcPass = jsonGetStr(json, "doge_rpc_pass");
  out.dogeWif     = jsonGetStr(json, "doge_wif", "");

  // DASH (Dash — pre-SegWit UTXO, P2SH + legacy sighash)
  out.dashHost    = jsonGetStr(json, "dash_rpc_host", "");
  out.dashPort    = static_cast<uint16_t>(jsonGetUint(json, "dash_rpc_port", 9998));
  out.dashRpcUser = jsonGetStr(json, "dash_rpc_user");
  out.dashRpcPass = jsonGetStr(json, "dash_rpc_pass");
  out.dashWif     = jsonGetStr(json, "dash_wif", "");
  out.dashTestnet = jsonGetBool(json, "dash_testnet", false);

  // ZEC (Zcash — transparent-only, v4 tx serialization + legacy sighash)
  out.zecHost    = jsonGetStr(json, "zec_rpc_host", "");
  out.zecPort    = static_cast<uint16_t>(jsonGetUint(json, "zec_rpc_port", 8232));
  out.zecRpcUser = jsonGetStr(json, "zec_rpc_user");
  out.zecRpcPass = jsonGetStr(json, "zec_rpc_pass");
  out.zecWif     = jsonGetStr(json, "zec_wif", "");
  out.zecTestnet = jsonGetBool(json, "zec_testnet", false);

  // ZANO (CryptoNote — shared 2-of-2 address via view-key adaptor scheme)
  out.zanoDaemonHost = jsonGetStr(json, "zano_daemon_host", "");
  out.zanoDaemonPort = static_cast<uint16_t>(jsonGetUint(json, "zano_daemon_port", 11211));
  out.zanoWalletHost = jsonGetStr(json, "zano_wallet_host", "");
  out.zanoWalletPort = static_cast<uint16_t>(jsonGetUint(json, "zano_wallet_port", 0));
  out.zanoSpendKeyHex = jsonGetStr(json, "zano_spend_key");
  out.zanoViewKeyHex  = jsonGetStr(json, "zano_view_key");

  // TON (The Open Network — TVM, account-based)
  out.tonHost       = jsonGetStr(json, "ton_rpc_host", "");
  out.tonPort       = static_cast<uint16_t>(jsonGetUint(json, "ton_rpc_port", 2990));
  out.tonRpcUser    = jsonGetStr(json, "ton_rpc_user");
  out.tonRpcPass    = jsonGetStr(json, "ton_rpc_pass");
  out.tonWalletKey  = jsonGetStr(json, "ton_wallet_key");
  out.tonHtlcAddress= jsonGetStr(json, "ton_htlc_address");
  out.tonWorkchain  = static_cast<int>(jsonGetUint(json, "ton_workchain", 0));

  // Monad (EVM L1, chain id 143)

  // Optimism (EVM L2, OP Stack, chain id 10)

  // DOT (Polkadot Relay — Substrate)
  out.dotHost       = jsonGetStr(json, "dot_rpc_host", "");
  out.dotPort       = static_cast<uint16_t>(jsonGetUint(json, "dot_rpc_port", 9933));
  out.dotAddress    = jsonGetStr(json, "dot_address");
  out.dotSeedPhrase = jsonGetStr(json, "dot_seed_phrase");

  out.xmrSpendKeyHex = jsonGetStr(json, "xmr_spend_key");
  out.xmrViewKeyHex  = jsonGetStr(json, "xmr_view_key");

  // XFG wallet key for signing managed offers
  out.xfgSecretKeyHex = jsonGetStr(json, "xfg_secret_key");
  out.xfgViewKeyHex   = jsonGetStr(json, "xfg_view_key");

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
  out.btcNetwork = jsonGetStr(json, "btc_network", "main");

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
  out.ltcNetwork = jsonGetStr(json, "ltc_network", "main");

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

  for (const auto& network : {std::make_pair(std::string("btc"), out.btcNetwork),
                              std::make_pair(std::string("ltc"), out.ltcNetwork),
                              std::make_pair(std::string("bch"), out.bchNetwork)}) {
    XfgSwap::UtxoNetwork parsed;
    if (!XfgSwap::parseUtxoNetwork(network.second, parsed)) {
      errorMsg = network.first + "_network must be main, test, signet or regtest";
      return false;
    }
  }

  // ── Validate ──
  if (!validateHex(out.xmrSpendKeyHex, 32, "xmr_spend_key", errorMsg)) return false;
  if (!validateHex(out.xmrViewKeyHex,  32, "xmr_view_key",  errorMsg)) return false;

  if (!validateHex(out.zanoSpendKeyHex, 32, "zano_spend_key", errorMsg)) return false;
  if (!validateHex(out.zanoViewKeyHex,  32, "zano_view_key",  errorMsg)) return false;

  // Monad (EVM)

  // Optimism (EVM)


  return true;
}

} // namespace XfgSwap
