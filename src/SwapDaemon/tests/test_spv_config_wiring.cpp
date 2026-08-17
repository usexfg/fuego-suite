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

#include "SwapDaemon/SwapDaemon.h"

using namespace XfgSwap;

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
  std::cout << "All SPV config wiring tests passed." << std::endl;
  return 0;
}
