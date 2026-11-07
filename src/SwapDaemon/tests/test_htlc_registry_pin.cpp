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

#include "SwapDaemon/Ethereum/EthRpcClient.h"
#include "SwapDaemon/SwapDaemon.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace XfgSwap;

static int g_failures = 0;
static int g_checks = 0;

static void check(bool condition, const char* label) {
  ++g_checks;
  if (!condition) {
    ++g_failures;
    std::printf("FAIL  %s\n", label);
  }
}

// HashedTimelock's creation code: a 28-byte prologue CODECOPYs the runtime
// that follows it and returns it.
static const size_t CONSTRUCTOR_PROLOGUE_BYTES = 28;

static std::string committedRuntimeCode() {
  std::ifstream in(FUEGO_HTLC_BIN_PATH);
  std::stringstream buffer;
  buffer << in.rdbuf();
  std::string creation = buffer.str();
  while (!creation.empty() && (creation.back() == '\n' || creation.back() == '\r')) creation.pop_back();
  if (creation.size() <= CONSTRUCTOR_PROLOGUE_BYTES * 2) return "";
  return "0x" + creation.substr(CONSTRUCTOR_PROLOGUE_BYTES * 2);
}

static void testCanonicalHash() {
  const std::string runtime = committedRuntimeCode();
  check(!runtime.empty(), "committed HashedTimelock artifact readable");
  check(runtimeCodeMatchesHash(runtime, CANONICAL_HTLC_REGISTRY_CODE_HASH),
        "committed runtime code hashes to the pinned value");

  std::string upper = CANONICAL_HTLC_REGISTRY_CODE_HASH;
  for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  check(runtimeCodeMatchesHash(runtime, upper), "hash comparison ignores hex case");

  std::string tampered = runtime;
  tampered[tampered.size() - 1] = tampered.back() == '0' ? '1' : '0';
  check(!runtimeCodeMatchesHash(tampered, CANONICAL_HTLC_REGISTRY_CODE_HASH),
        "one changed byte fails the pin");
  check(!runtimeCodeMatchesHash(runtime.substr(0, runtime.size() - 2), CANONICAL_HTLC_REGISTRY_CODE_HASH),
        "truncated code fails the pin");
  check(!runtimeCodeMatchesHash("0x", CANONICAL_HTLC_REGISTRY_CODE_HASH), "empty code fails the pin");
  check(!runtimeCodeMatchesHash(runtime, "6ac1"), "short pin never matches");
  check(!runtimeCodeMatchesHash("0xzz", CANONICAL_HTLC_REGISTRY_CODE_HASH), "non-hex code fails");
}

static std::vector<uint8_t> bytesFromHex(const std::string& hex) {
  std::vector<uint8_t> bytes;
  for (size_t i = 0; i + 1 < hex.size(); i += 2)
    bytes.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
  return bytes;
}

static void testCreate2Address() {
  // EIP-1014 examples 0 and 1.
  const std::vector<uint8_t> zeroSalt(32, 0);
  check(create2Address("0x0000000000000000000000000000000000000000", zeroSalt, {0x00}) ==
            "0x4d1a2e2bb4f88f0250f26ffff098b0b30b26bf38",
        "EIP-1014 example 0");
  check(create2Address("0xdeadbeef00000000000000000000000000000000", zeroSalt, {0x00}) ==
            "0xb928f69bb1d91cd65274e3c79d8986362984fda3",
        "EIP-1014 example 1");
  const std::string runtime = committedRuntimeCode();
  std::ifstream in(FUEGO_HTLC_BIN_PATH);
  std::stringstream buffer;
  buffer << in.rdbuf();
  std::string creation = buffer.str();
  while (!creation.empty() && (creation.back() == '\n' || creation.back() == '\r')) creation.pop_back();
  check(create2Address(DETERMINISTIC_DEPLOYMENT_PROXY, zeroSalt, bytesFromHex(creation)) ==
            CANONICAL_HTLC_REGISTRY_ADDRESS,
        "canonical registry address is the committed artifact's CREATE2 address");
  check(create2Address("0x1234", zeroSalt, {0x00}).empty(), "malformed deployer refused");
}

static bool loadEvmChains(const std::string& linea, ChainClientConfig& cfg, std::string& error) {
  const std::string path = "test_htlc_registry_pin.json";
  {
    std::ofstream out(path);
    out << "{\"xfg_secret_key\":\"" << std::string(64, '1') << "\",\"evm_chains\":{\"linea\":" << linea << "}}";
  }
  const bool ok = loadChainClientConfig(path, cfg, error);
  std::remove(path.c_str());
  return ok;
}

static void testConfigOverride() {
  ChainClientConfig cfg;
  std::string error;
  const std::string override(64, 'a');
  check(loadEvmChains("{\"rpc_url\":\"https://rpc.linea.build\",\"chain_id\":59144,"
                      "\"htlc_registry_code_hash\":\"" + override + "\"}", cfg, error) &&
            cfg.evmChains.size() == 1 && cfg.evmChains[0].htlcRegistryCodeHash == override,
        "per-chain code hash override parses");
  ChainClientConfig defaulted;
  check(loadEvmChains("{\"rpc_url\":\"https://rpc.linea.build\",\"chain_id\":59144}", defaulted, error) &&
            defaulted.evmChains.size() == 1 && defaulted.evmChains[0].htlcRegistryCodeHash.empty(),
        "absent override leaves the canonical pin");
  ChainClientConfig rejected;
  check(!loadEvmChains("{\"rpc_url\":\"https://rpc.linea.build\",\"chain_id\":59144,"
                       "\"htlc_registry_code_hash\":\"abcd\"}", rejected, error),
        "short code hash override rejected");
}

int main() {
  testCanonicalHash();
  testConfigOverride();
  testCreate2Address();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
