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
#include "Common/StringTools.h"
#include "crypto/hash.h"

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

struct PinnedContract {
  const char* name;
  const char* creationPath;
  const char* runtimePath;
  const char* codeHash;
  const char* address;
};

static const PinnedContract PINNED_CONTRACTS[] = {
    {"HashedTimelock", FUEGO_EVM_ARTIFACT_DIR "/HashedTimelock_sol_HashedTimelock.bin",
     FUEGO_EVM_ARTIFACT_DIR "/HashedTimelock_sol_HashedTimelock.bin-runtime", CANONICAL_HTLC_REGISTRY_CODE_HASH,
     CANONICAL_HTLC_REGISTRY_ADDRESS},
    {"PointTimelock", FUEGO_EVM_ARTIFACT_DIR "/PointTimelock_sol_PointTimelock.bin",
     FUEGO_EVM_ARTIFACT_DIR "/PointTimelock_sol_PointTimelock.bin-runtime", CANONICAL_PTLC_REGISTRY_CODE_HASH,
     CANONICAL_PTLC_REGISTRY_ADDRESS},
};

static std::string readArtifact(const char* path) {
  std::ifstream in(path);
  std::stringstream buffer;
  buffer << in.rdbuf();
  std::string hex = buffer.str();
  while (!hex.empty() && (hex.back() == '\n' || hex.back() == '\r')) hex.pop_back();
  return hex;
}

static std::vector<uint8_t> bytesFromHex(const std::string& hex) {
  std::vector<uint8_t> bytes;
  for (size_t i = 0; i + 1 < hex.size(); i += 2)
    bytes.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
  return bytes;
}

static std::string keccakHex(const std::vector<uint8_t>& bytes) {
  Crypto::Hash digest;
  Crypto::cn_fast_hash(bytes.data(), bytes.size(), digest);
  return Common::podToHex(digest);
}

static void testPinnedContract(const PinnedContract& contract) {
  const std::string label = contract.name;
  const std::string creation = readArtifact(contract.creationPath);
  const std::string runtime = readArtifact(contract.runtimePath);
  check(!creation.empty() && !runtime.empty(), (label + ": committed artifacts readable").c_str());
  check(creation.find(runtime) != std::string::npos,
        (label + ": the creation code deploys the committed runtime code").c_str());
  const bool hashPinned = runtimeCodeMatchesHash("0x" + runtime, contract.codeHash);
  if (!hashPinned) std::printf("      %s runtime keccak256 = %s\n", contract.name, keccakHex(bytesFromHex(runtime)).c_str());
  check(hashPinned, (label + ": runtime code hashes to the pinned value").c_str());
  const std::vector<uint8_t> zeroSalt(32, 0);
  const std::string address = create2Address(DETERMINISTIC_DEPLOYMENT_PROXY, zeroSalt, bytesFromHex(creation));
  if (address != contract.address) std::printf("      %s CREATE2 address = %s\n", contract.name, address.c_str());
  check(address == contract.address, (label + ": canonical address is the artifact's CREATE2 address").c_str());

  std::string tampered = "0x" + runtime;
  tampered[tampered.size() - 1] = tampered.back() == '0' ? '1' : '0';
  check(!runtimeCodeMatchesHash(tampered, contract.codeHash), (label + ": one changed byte fails the pin").c_str());
  check(!runtimeCodeMatchesHash("0x" + runtime.substr(0, runtime.size() - 2), contract.codeHash),
        (label + ": truncated code fails the pin").c_str());
}

static void testPinComparison() {
  const std::string runtime = "0x" + readArtifact(PINNED_CONTRACTS[0].runtimePath);
  std::string upper = CANONICAL_HTLC_REGISTRY_CODE_HASH;
  for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  check(runtimeCodeMatchesHash(runtime, upper), "hash comparison ignores hex case");
  check(!runtimeCodeMatchesHash("0x", CANONICAL_HTLC_REGISTRY_CODE_HASH), "empty code fails the pin");
  check(!runtimeCodeMatchesHash(runtime, "6ac1"), "short pin never matches");
  check(!runtimeCodeMatchesHash("0xzz", CANONICAL_HTLC_REGISTRY_CODE_HASH), "non-hex code fails");
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
  for (const PinnedContract& contract : PINNED_CONTRACTS) testPinnedContract(contract);
  testPinComparison();
  testConfigOverride();
  testCreate2Address();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
