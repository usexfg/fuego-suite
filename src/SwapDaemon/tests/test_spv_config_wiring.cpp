// Copyright (c) 2017-2026 Fuego Developers
//
// Tests for BCH SPV config parsing and SwapDaemon wiring.
// Verifies that ChainClientConfig correctly parses SPV fields from JSON
// and that SwapDaemon creates an ElectrumSpvClient when mode == "spv".

#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <cstdio>
#include <stdexcept>

#include "SwapDaemon/SwapDaemon.h"
#include "SwapDaemon/Ethereum/EthRpcClient.h"
#include "SwapDaemon/Spv/ElectrumSpvClient.h"
#include "SwapDaemon/BitcoinCash/BchChainClient.h"
#include "SwapDaemon/Bitcoin/BtcChainClient.h"
#include "SwapDaemon/Litecoin/LtcChainClient.h"
#include "SwapDaemon/Komodo/KmdChainClient.h"
#include "SwapDaemon/Decred/DcrChainClient.h"
#include "SwapDaemon/Monero/XmrChainClient.h"
#include "SwapDaemon/Doge/DogeChainClient.h"
#include "SwapDaemon/Dash/DashChainClient.h"
#include "SwapDaemon/Zec/ZecChainClient.h"

using namespace XfgSwap;

static void expectConfig(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

// =============================================================================
// Helper: write a temp JSON config file and return its path
// =============================================================================

static std::string writeTempConfig(const std::string& jsonContent) {
#ifdef _WIN32
  char name[L_tmpnam] = {};
  if (std::tmpnam(name) == nullptr) {
    return "";
  }
  std::string path(name);
  std::ofstream f(path, std::ios::out);
  f << jsonContent;
  f.close();
  return path;
#else
  char tmpl[] = "/tmp/spv_config_test_XXXXXX";
  int fd = mkstemp(tmpl);
  assert(fd >= 0);
  std::ofstream f;
  f.open(tmpl, std::ios::out);
  f << jsonContent;
  f.close();
  close(fd);
  return std::string(tmpl);
#endif
}

// =============================================================================
// Test: SPV fields parsed from flat JSON config
// =============================================================================

static void test_config_parse_spv_mode() {
  std::string json = R"({
    "bch_mode": "spv",
    "bch_wif": "test-wif",
    "bch_spv_server_0": "electroncash.org:50002",
    "bch_spv_server_1": "bch.imaginary.cash:50002",
    "bch_spv_min_servers": 2,
    "bch_spv_checkpoint_height": 586670,
    "bch_spv_checkpoint_hash": "0000000000000000016b5e0b8a70a85812e6546c2c7e0b52c7719c0e194677a2"
  })";

  std::string path = writeTempConfig(json);

  ChainClientConfig cfg;
  std::string errMsg;
  bool ok = loadChainClientConfig(path, cfg, errMsg);
  assert(ok);
  assert(errMsg.empty());

  assert(cfg.bchMode == "spv");
  assert(cfg.bchSpvServers.size() == 2);
  assert(cfg.bchSpvServers[0] == "electroncash.org:50002");
  assert(cfg.bchSpvServers[1] == "bch.imaginary.cash:50002");
  assert(cfg.bchSpvMinServers == 2);
  assert(cfg.bchSpvCheckpointHeight == 586670);
  assert(cfg.bchSpvCheckpointHash == "0000000000000000016b5e0b8a70a85812e6546c2c7e0b52c7719c0e194677a2");

  std::remove(path.c_str());
  std::cout << "  PASS: test_config_parse_spv_mode" << std::endl;
}

// =============================================================================
// Test: default values when SPV fields are absent (RPC mode)
// =============================================================================

static void test_config_defaults_rpc_mode() {
  std::string json = R"({
    "bch_rpc_host": "127.0.0.1",
    "bch_rpc_port": 8332
  })";

  std::string path = writeTempConfig(json);

  ChainClientConfig cfg;
  std::string errMsg;
  bool ok = loadChainClientConfig(path, cfg, errMsg);
  assert(ok);

  assert(cfg.bchMode.empty());
  assert(cfg.bchSpvServers.empty());
  assert(cfg.bchSpvMinServers == 1);
  assert(cfg.bchSpvCheckpointHeight == 0);
  assert(cfg.bchSpvCheckpointHash.empty());
  assert(cfg.bchHost == "127.0.0.1");
  assert(cfg.bchPort == 8332);

  std::remove(path.c_str());
  std::cout << "  PASS: test_config_defaults_rpc_mode" << std::endl;
}

// =============================================================================
// Test: SPV mode with single server and no checkpoint
// =============================================================================

static void test_config_spv_single_server_no_checkpoint() {
  std::string json = R"({
    "bch_mode": "spv",
    "bch_wif": "test-wif",
    "bch_spv_server_0": "electrum.imaginary.cash:50002"
  })";

  std::string path = writeTempConfig(json);

  ChainClientConfig cfg;
  std::string errMsg;
  bool ok = loadChainClientConfig(path, cfg, errMsg);
  assert(ok);

  assert(cfg.bchMode == "spv");
  assert(cfg.bchSpvServers.size() == 1);
  assert(cfg.bchSpvServers[0] == "electrum.imaginary.cash:50002");
  assert(cfg.bchSpvMinServers == 1);
  assert(cfg.bchSpvCheckpointHeight == 0);
  assert(cfg.bchSpvCheckpointHash.empty());

  std::remove(path.c_str());
  std::cout << "  PASS: test_config_spv_single_server_no_checkpoint" << std::endl;
}

// =============================================================================
// Test: SPV server list stops at gap (server_0 and server_2 without server_1)
// =============================================================================

static void test_config_spv_server_gap() {
  std::string json = R"({
    "bch_mode": "spv",
    "bch_wif": "test-wif",
    "bch_spv_server_0": "server-a:50002",
    "bch_spv_server_2": "server-b:50002"
  })";

  std::string path = writeTempConfig(json);

  ChainClientConfig cfg;
  std::string errMsg;
  bool ok = loadChainClientConfig(path, cfg, errMsg);
  assert(ok);

  // Parser stops at first empty key — server_1 is missing so only server_0 parsed
  assert(cfg.bchSpvServers.size() == 1);
  assert(cfg.bchSpvServers[0] == "server-a:50002");

  std::remove(path.c_str());
  std::cout << "  PASS: test_config_spv_server_gap" << std::endl;
}

// =============================================================================
// Test: SPV mode takes priority when both bch_rpc_host and bch_mode=spv set
// =============================================================================

static void test_config_spv_overrides_rpc() {
  std::string json = R"({
    "bch_rpc_host": "127.0.0.1",
    "bch_rpc_port": 8332,
    "bch_mode": "spv",
    "bch_wif": "test-wif",
    "bch_spv_server_0": "electroncash.org:50002",
    "bch_spv_checkpoint_height": 586670,
    "bch_spv_checkpoint_hash": "0000000000000000016b5e0b8a70a85812e6546c2c7e0b52c7719c0e194677a2"
  })";

  std::string path = writeTempConfig(json);

  ChainClientConfig cfg;
  std::string errMsg;
  bool ok = loadChainClientConfig(path, cfg, errMsg);
  assert(ok);

  // Both are populated — SwapDaemon constructor will choose SPV because bchMode == "spv"
  assert(cfg.bchMode == "spv");
  assert(!cfg.bchHost.empty());
  assert(cfg.bchSpvServers.size() == 1);

  std::remove(path.c_str());
  std::cout << "  PASS: test_config_spv_overrides_rpc" << std::endl;
}

// The example is commented and contains placeholder keys. Exercise its
// PulseChain host and port entries as a minimal valid operator config.
static void test_pulsechain_example_keys() {
  const std::string thisFile = __FILE__;
  const auto suffix = thisFile.rfind("/src/SwapDaemon/tests/");
  const std::string examplePath = suffix == std::string::npos
      ? "swap_config.example.json"
      : thisFile.substr(0, suffix) + "/swap_config.example.json";
  std::ifstream example(examplePath);
  expectConfig(example.good(), "Cannot read swap_config.example.json");
  std::string hostLine;
  std::string portLine;
  for (std::string line; std::getline(example, line); ) {
    if (line.find("\"pulsechain_rpc_host\"") != std::string::npos) hostLine = line;
    if (line.find("\"pulsechain_rpc_port\"") != std::string::npos) portLine = line;
  }
  expectConfig(!hostLine.empty() && !portLine.empty(), "PulseChain example keys missing");
  const auto trailingComma = portLine.rfind(',');
  if (trailingComma != std::string::npos) portLine.erase(trailingComma);
  const std::string json = "{" + hostLine + "\n" + portLine + "\n}";
  const std::string path = writeTempConfig(json);

  ChainClientConfig cfg;
  std::string errMsg;
  expectConfig(loadChainClientConfig(path, cfg, errMsg), "PulseChain example keys did not load");
  expectConfig(cfg.pulsechain_host == "rpc.pulsechain.com", "PulseChain host mismatch");
  expectConfig(cfg.pulsechain_port == 443, "PulseChain port mismatch");

  std::remove(path.c_str());
  std::cout << "  PASS: test_pulsechain_example_keys" << std::endl;
}

static void test_gleec_registry_syntax_preserves_recovery_config() {
  const std::string validAddress = "0x0123456789abcdef0123456789abcdef01234567";
  expectConfig(EthRpcClient::isValidEvmAddress(validAddress), "valid GLEEC registry rejected");
  expectConfig(!EthRpcClient::isValidEvmAddress(""), "empty registry accepted");
  expectConfig(!EthRpcClient::isValidEvmAddress("0x1234"), "short registry accepted");
  expectConfig(!EthRpcClient::isValidEvmAddress("0123456789abcdef0123456789abcdef01234567"),
      "registry without 0x accepted");
  expectConfig(!EthRpcClient::isValidEvmAddress("0x0123456789abcdef0123456789abcdef0123456g"),
      "nonhex registry accepted");

  EthRpcClient rpc("127.0.0.1", 0);
  rpc.setHtlcRegistry("0x1234");
  expectConfig(!rpc.hasDeployedHtlcRegistry(), "malformed registry reached RPC");
  rpc.setHtlcRegistry(validAddress);
  expectConfig(!rpc.hasDeployedHtlcRegistry(), "registry accepted during RPC outage");

  for (const std::string& registry : {std::string(), std::string("0x1234"), validAddress}) {
    const std::string path = writeTempConfig(
        "{\"gleec_rpc_host\":\"127.0.0.1\",\"gleec_htlc_registry\":\"" +
        registry + "\"}");
    ChainClientConfig cfg;
    std::string errMsg;
    expectConfig(loadChainClientConfig(path, cfg, errMsg), "recovery config failed to load");
    expectConfig(cfg.gleecHost == "127.0.0.1", "GLEEC host mismatch");
    expectConfig(cfg.gleecHtlcRegistry == registry, "GLEEC registry value mismatch");
    expectConfig(EthRpcClient::isValidEvmAddress(cfg.gleecHtlcRegistry) ==
           (registry == validAddress), "GLEEC registry syntax result mismatch");
    std::remove(path.c_str());
  }
  std::cout << "  PASS: test_gleec_registry_syntax_preserves_recovery_config" << std::endl;
}

static void test_chain_clients_expose_spv_mode() {
  auto spv = std::make_shared<ElectrumSpvClient>(
      std::vector<std::string>{"127.0.0.1:1"}, 1, 0, "");
  expectConfig(BchChainClient(spv, "").usesSpvVerification(), "BCH SPV mode hidden");
  expectConfig(BtcChainClient(spv, "").usesSpvVerification(), "BTC SPV mode hidden");
  expectConfig(LtcChainClient(spv, "").usesSpvVerification(), "LTC SPV mode hidden");
  expectConfig(KmdChainClient(spv, "").usesSpvVerification(), "KMD SPV mode hidden");
  expectConfig(BtcChainClient(spv, "").isReadyForNewSwap(), "BTC SPV new swaps blocked");
  expectConfig(BchChainClient(spv, "").isReadyForNewSwap(), "BCH SPV new swaps blocked");
  expectConfig(LtcChainClient(spv, "").isReadyForNewSwap(), "LTC SPV new swaps blocked");
  expectConfig(KmdChainClient(spv, "").isReadyForNewSwap(), "KMD SPV new swaps blocked");
  expectConfig(DcrChainClient(spv, nullptr, "").usesSpvVerification(), "DCR SPV mode hidden");
  expectConfig(!DcrChainClient(spv, nullptr, "").isReadyForNewSwap(),
      "unwired DCR SPV client admitted a new swap");
  expectConfig(!DcrChainClient(std::unique_ptr<DcrRpcClient>{}, "")
                    .isReadyForNewSwap(),
      "unverified DCR full-node client admitted a new swap");
  expectConfig(!XmrChainClient(std::unique_ptr<MoneroRpcClient>{}, "", "")
                    .isReadyForNewSwap(),
      "XMR client without signed-claim discovery admitted a new swap");
  expectConfig(!DogeChainClient(std::unique_ptr<DogeRpcClient>{}, "")
                    .isReadyForNewSwap() &&
                   !DashChainClient(std::unique_ptr<DashRpcClient>{}, "")
                    .isReadyForNewSwap() &&
                   !ZecChainClient(std::unique_ptr<ZecRpcClient>{}, "")
                    .isReadyForNewSwap(),
      "full-node staged UTXO client admitted a new swap");
  expectConfig(!BchChainClient(std::unique_ptr<BchRpcClient>{}, "").usesSpvVerification(),
      "BCH RPC mode mislabeled SPV");
  expectConfig(!BtcChainClient(std::unique_ptr<BtcRpcClient>{}, "").isReadyForNewSwap() &&
                   !BchChainClient(std::unique_ptr<BchRpcClient>{}, "").isReadyForNewSwap() &&
                   !LtcChainClient(std::unique_ptr<LtcRpcClient>{}, "").isReadyForNewSwap() &&
                   !KmdChainClient(std::unique_ptr<KmdRpcClient>{}, "").isReadyForNewSwap(),
      "full-node UTXO mode without independent spend discovery admitted a new swap");
  std::cout << "  PASS: test_chain_clients_expose_spv_mode" << std::endl;
}

static void test_generic_evm_config() {
  const std::string json = R"({
    "evm_chains": {
      "peaq": {
        "rpc_url": "https://peaq.api.onfinality.io/public",
        "private_key": "1111111111111111111111111111111111111111111111111111111111111111",
        "address": "0x1111111111111111111111111111111111111111",
        "htlc_registry": "0x2222222222222222222222222222222222222222"
      },
      "beam": {
        "rpc_url": "https://build.onbeam.com/rpc",
        "htlc_registry": "0x3333333333333333333333333333333333333333"
      }
    }
  })";
  const std::string path = writeTempConfig(json);
  ChainClientConfig cfg;
  std::string errMsg;
  expectConfig(loadChainClientConfig(path, cfg, errMsg), "generic EVM config rejected");
  expectConfig(cfg.evmChains.size() == 2, "generic EVM chain count mismatch");
  expectConfig(cfg.evmChains[0].pair == SwapPair::BEAM ||
               cfg.evmChains[1].pair == SwapPair::BEAM, "Beam missing from config");
  expectConfig(cfg.evmChains[0].pair == SwapPair::PEAQ ||
               cfg.evmChains[1].pair == SwapPair::PEAQ, "peaq missing from config");
  std::remove(path.c_str());
  std::cout << "  PASS: test_generic_evm_config" << std::endl;
}

static void test_generic_evm_config_rejects_unsafe_entries() {
  for (const std::string& json : {
      std::string(R"({"evm_chains":{"zano":{"rpc_url":"https://example"}}})"),
      std::string(R"({"evm_chains":{"peaq":{"rpc_url":"https://example","chain_id":3339}}})"),
      std::string(R"({"evm_chains":{"peaq":{"rpc_url":"https://example","private_key":"1111111111111111111111111111111111111111111111111111111111111111"}}})"),
      std::string(R"({"evm_chains":{"peaq":{"rpc_url":"https://example","address":"0x1111111111111111111111111111111111111111"}}})")}) {
    const std::string path = writeTempConfig(json);
    ChainClientConfig cfg;
    std::string errMsg;
    expectConfig(!loadChainClientConfig(path, cfg, errMsg), "unsafe generic EVM entry accepted");
    expectConfig(!errMsg.empty(), "unsafe generic EVM entry had no error");
    std::remove(path.c_str());
  }
  std::cout << "  PASS: test_generic_evm_config_rejects_unsafe_entries" << std::endl;
}

// =============================================================================
// Main
// =============================================================================

int main() {
  std::cout << "Running SPV config wiring tests..." << std::endl;
  test_config_parse_spv_mode();
  test_config_defaults_rpc_mode();
  test_config_spv_single_server_no_checkpoint();
  test_config_spv_server_gap();
  test_config_spv_overrides_rpc();
  test_pulsechain_example_keys();
  test_gleec_registry_syntax_preserves_recovery_config();
  test_chain_clients_expose_spv_mode();
  test_generic_evm_config();
  test_generic_evm_config_rejects_unsafe_entries();
  std::cout << "All SPV config wiring tests passed." << std::endl;
  return 0;
}
