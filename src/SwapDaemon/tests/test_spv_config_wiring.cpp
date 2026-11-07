// Copyright (c) 2017-2026 Fuego Developers
//
// Tests for BCH SPV config parsing and SwapDaemon wiring.
// Verifies that ChainClientConfig correctly parses SPV fields from JSON
// and that SwapDaemon creates an ElectrumSpvClient when mode == "spv".

#include <cassert>
#include <iostream>
#include <iterator>
#include <set>
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

static std::string exampleConfigPath() {
  const std::string thisFile = __FILE__;
  const auto suffix = thisFile.rfind("/src/SwapDaemon/tests/");
  return suffix == std::string::npos
      ? "swap_config.example.json"
      : thisFile.substr(0, suffix) + "/swap_config.example.json";
}

// Brace-matched, so it assumes no braces inside the object's strings or comments.
static std::string jsonObjectAfterKey(const std::string& text, const std::string& key) {
  const auto keyPos = text.find("\"" + key + "\"");
  if (keyPos == std::string::npos) return "";
  const auto open = text.find('{', keyPos);
  if (open == std::string::npos) return "";
  int depth = 0;
  for (size_t i = open; i < text.size(); ++i) {
    if (text[i] == '{') ++depth;
    if (text[i] == '}' && --depth == 0) return text.substr(open, i - open + 1);
  }
  return "";
}

// Flat per-chain EVM fields are retired (plan R5). A config carrying one,
// including the names the old example used, is refused with the evm_chains
// key that replaces it.
static void test_retired_flat_evm_keys_rejected() {
  const std::pair<std::string, std::string> cases[] = {
    {"{\"pulsechain_rpc_host\":\"rpc.pulsechain.com\"}", "evm_chains.pls"},
    {"{\"eth_host\":\"eth.llamarpc.com\"}", "evm_chains.eth"},
    {"{\"gleec_htlc_registry\":\"0x0123456789abcdef0123456789abcdef01234567\"}", "evm_chains.gleec"},
    {"{\"plasma_priv_key\":\"" + std::string(64, '1') + "\"}", "evm_chains.xpl"},
  };
  for (const auto& c : cases) {
    const std::string path = writeTempConfig(c.first);
    ChainClientConfig cfg;
    std::string errMsg;
    expectConfig(!loadChainClientConfig(path, cfg, errMsg), "retired flat EVM key accepted");
    expectConfig(errMsg.find(c.second) != std::string::npos, "retirement error names the wrong evm_chains key");
    std::remove(path.c_str());
  }
  std::cout << "  PASS: test_retired_flat_evm_keys_rejected" << std::endl;
}

// evm_chains is the only config path for generic-adapter pairs, and the
// example is the operator's only template for it.
static void test_example_evm_chains_cover_every_adapter_pair() {
  std::ifstream example(exampleConfigPath());
  expectConfig(example.good(), "Cannot read swap_config.example.json");
  const std::string text((std::istreambuf_iterator<char>(example)),
                         std::istreambuf_iterator<char>());
  const std::string block = jsonObjectAfterKey(text, "evm_chains");
  expectConfig(!block.empty(), "example has no evm_chains object");

  const std::string path = writeTempConfig("{\"evm_chains\": " + block + "}");
  ChainClientConfig cfg;
  std::string errMsg;
  expectConfig(loadChainClientConfig(path, cfg, errMsg), "example evm_chains did not load");
  std::remove(path.c_str());

  std::set<SwapPair> configured;
  for (const auto& entry : cfg.evmChains) {
    expectConfig(!entry.rpcUrl.empty(), "example evm_chains entry has no rpc_url");
    expectConfig(entry.privateKeyHex.empty(), "example evm_chains ships a signing key");
    configured.insert(entry.pair);
  }
  // No public endpoint is shipped for these; the example says so.
  const std::set<SwapPair> withoutExampleEndpoint = {SwapPair::GLEEC, SwapPair::ROBINHOOD};
  for (const auto& descriptor : SWAP_PAIR_CATALOG) {
    const bool executableEvm = descriptor.family == SwapChainFamily::EVM &&
                               isExecutableSwapPair(descriptor.id) &&
                               withoutExampleEndpoint.count(descriptor.pair) == 0;
    expectConfig(configured.count(descriptor.pair) == (executableEvm ? 1u : 0u),
                 "example evm_chains drifted from the catalog's executable EVM pairs");
  }
  std::cout << "  PASS: test_example_evm_chains_cover_every_adapter_pair" << std::endl;
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
        "{\"evm_chains\":{\"gleec\":{\"rpc_url\":\"http://127.0.0.1:8545\",\"chain_id\":11169,"
        "\"htlc_registry\":\"" + registry + "\"}}}");
    ChainClientConfig cfg;
    std::string errMsg;
    const bool loaded = loadChainClientConfig(path, cfg, errMsg);
    expectConfig(loaded == (registry != "0x1234"), "GLEEC registry syntax not enforced at load");
    if (loaded) {
      expectConfig(cfg.evmChains.size() == 1 && cfg.evmChains[0].htlcRegistry == registry,
                   "GLEEC registry value mismatch");
    }
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
  expectConfig(!BtcChainClient(spv, "").isReadyForNewSwap(), "BTC SPV mode admitted a new swap");
  expectConfig(!BchChainClient(spv, "").isReadyForNewSwap(), "BCH SPV mode admitted a new swap");
  expectConfig(!LtcChainClient(spv, "").isReadyForNewSwap(), "LTC SPV mode admitted a new swap");
  expectConfig(!KmdChainClient(spv, "").isReadyForNewSwap(), "KMD SPV mode admitted a new swap");
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

static void test_testnet_evm_config_requires_deployment_identity() {
  const std::string validEntry = R"({
    "network": "testnet",
    "evm_chains": {
      "eth": {
        "rpc_url": "http://127.0.0.1:8545",
        "private_key": "1111111111111111111111111111111111111111111111111111111111111111",
        "address": "0x1111111111111111111111111111111111111111",
        "htlc_registry": "0x2222222222222222222222222222222222222222",
        "chain_id": 11155111
      }
    }
  })";
  const std::string validPath = writeTempConfig(validEntry);
  ChainClientConfig validConfig;
  std::string error;
  expectConfig(loadChainClientConfig(validPath, validConfig, error),
      "testnet EVM deployment config rejected");
  expectConfig(validConfig.network == "testnet", "testnet network was not retained");
  expectConfig(validConfig.evmChains.size() == 1, "testnet EVM entry missing");
  expectConfig(validConfig.evmChains[0].chainId == 11155111,
      "testnet EVM chain ID mismatch");
  std::remove(validPath.c_str());

  const std::string missingChainId = R"({
    "network": "testnet",
    "evm_chains": {
      "eth": {
        "rpc_url": "http://127.0.0.1:8545",
        "private_key": "1111111111111111111111111111111111111111111111111111111111111111",
        "address": "0x1111111111111111111111111111111111111111",
        "htlc_registry": "0x2222222222222222222222222222222222222222"
      }
    }
  })";
  const std::string missingPath = writeTempConfig(missingChainId);
  ChainClientConfig missingConfig;
  error.clear();
  expectConfig(!loadChainClientConfig(missingPath, missingConfig, error),
      "testnet EVM config without chain ID accepted");
  expectConfig(!error.empty(), "missing testnet chain ID had no error");
  std::remove(missingPath.c_str());
  std::cout << "  PASS: test_testnet_evm_config_requires_deployment_identity" << std::endl;
}

static void test_solana_inline_keypair_config() {
  const std::string json = R"({
    "sol_rpc_host": "127.0.0.1",
    "sol_rpc_port": 8899,
    "sol_program_id": "11111111111111111111111111111111",
    "sol_keypair": "unit-test-keypair"
  })";
  const std::string path = writeTempConfig(json);
  ChainClientConfig config;
  std::string error;
  expectConfig(loadChainClientConfig(path, config, error),
      "Solana inline keypair config rejected");
  expectConfig(config.solKeypairBase58 == "unit-test-keypair",
      "Solana inline keypair not retained");
  std::remove(path.c_str());
  std::cout << "  PASS: test_solana_inline_keypair_config" << std::endl;
}

static void test_solana_rejects_nonlocal_rpc_config() {
  const std::string json = R"({
    "sol_rpc_host": "api.mainnet-beta.solana.com",
    "sol_rpc_port": 443,
    "sol_program_id": "11111111111111111111111111111111",
    "sol_keypair": "unit-test-keypair"
  })";
  const std::string path = writeTempConfig(json);
  ChainClientConfig config;
  std::string error;
  expectConfig(!loadChainClientConfig(path, config, error),
      "remote plaintext Solana RPC config accepted");
  expectConfig(!error.empty(), "remote Solana RPC config had no error");
  std::remove(path.c_str());
  std::cout << "  PASS: test_solana_rejects_nonlocal_rpc_config" << std::endl;
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
  test_retired_flat_evm_keys_rejected();
  test_example_evm_chains_cover_every_adapter_pair();
  test_gleec_registry_syntax_preserves_recovery_config();
  test_chain_clients_expose_spv_mode();
  test_generic_evm_config();
  test_generic_evm_config_rejects_unsafe_entries();
  test_testnet_evm_config_requires_deployment_identity();
  test_solana_inline_keypair_config();
  test_solana_rejects_nonlocal_rpc_config();
  std::cout << "All SPV config wiring tests passed." << std::endl;
  return 0;
}
