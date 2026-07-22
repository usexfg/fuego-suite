// Copyright (c) 2017-2026 Fuego Developers
//
// ETH HashedTimelock protocol unit tests (offline):
//   - ethHashLockHex = keccak256(t)  (Alice-locks hashlock)
//   - computeContractId matches Solidity abi.encodePacked layout
//   - ABI selectors for lock/claim/getContract
//   - RLP empty access list is 0xc0 (EIP-1559)

#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "SwapDaemon/SwapHashLock.h"
#include "SwapDaemon/Ethereum/EthRpcClient.h"
#include "SwapDaemon/Ethereum/ContractAbi.h"
#include "SwapDaemon/Crypto/RlpEncoder.h"
#include "Common/StringTools.h"
#include "crypto/crypto.h"

extern "C" {
#include "crypto/keccak.h"
}

using namespace XfgSwap;

static int g_pass = 0, g_fail = 0;
#define CHECK(c, m) do { if (c) { ++g_pass; std::cout << "  PASS: " << m << "\n"; } \
  else { ++g_fail; std::cerr << "  FAIL: " << m << "\n"; } } while (0)

static std::string toHex(const uint8_t* p, size_t n) {
  static const char* h = "0123456789abcdef";
  std::string s; s.reserve(n * 2);
  for (size_t i = 0; i < n; ++i) { s += h[p[i] >> 4]; s += h[p[i] & 0xf]; }
  return s;
}

int main() {
  std::cout << "=== ETH protocol unit tests ===\n";

  // ── hashlock ─────────────────────────────────────────────────────────
  {
    Crypto::PublicKey T;
    Crypto::SecretKey t;
    Crypto::generate_keys(T, t);
    std::string hl = ethHashLockHex(t);
    uint8_t md[32];
    keccak(reinterpret_cast<const uint8_t*>(&t), 32, md, 32);
    CHECK(hl == toHex(md, 32), "ethHashLockHex == keccak256(t)");
    CHECK(hl != Common::podToHex(T), "ethHashLockHex != adaptor point T");
  }

  // ── contractId packing (Solidity encodePacked) ───────────────────────
  {
    // Fixed vectors so we can recompute independently
    const std::string sender = "1111111111111111111111111111111111111111";
    const std::string recip  = "2222222222222222222222222222222222222222";
    const uint64_t value = 1000000000000000000ULL; // 1 eth wei
    const std::string hashLock =
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    const uint64_t timeout = 12345678ULL;

    std::string id = EthRpcClient::computeContractId(sender, recip, value, hashLock, timeout);
    CHECK(id.size() == 64, "contractId is 32-byte hex");

    // Independent pack + keccak
    std::vector<uint8_t> packed;
    auto pushHex = [&](const std::string& hx) {
      for (size_t i = 0; i + 1 < hx.size(); i += 2) {
        unsigned v = 0;
        sscanf(hx.c_str() + i, "%2x", &v);
        packed.push_back(static_cast<uint8_t>(v));
      }
    };
    pushHex(sender);
    pushHex(recip);
    std::vector<uint8_t> val(32, 0);
    for (int i = 0; i < 8; ++i) val[31 - i] = static_cast<uint8_t>((value >> (i * 8)) & 0xff);
    packed.insert(packed.end(), val.begin(), val.end());
    pushHex(hashLock);
    std::vector<uint8_t> to(32, 0);
    for (int i = 0; i < 8; ++i) to[31 - i] = static_cast<uint8_t>((timeout >> (i * 8)) & 0xff);
    packed.insert(packed.end(), to.begin(), to.end());
    CHECK(packed.size() == 20 + 20 + 32 + 32 + 32, "packed size 136 bytes");
    uint8_t digest[32];
    keccak(packed.data(), static_cast<int>(packed.size()), digest, 32);
    CHECK(id == toHex(digest, 32), "computeContractId matches independent keccak");

    // Mutating amount must change id
    std::string id2 = EthRpcClient::computeContractId(sender, recip, value + 1, hashLock, timeout);
    CHECK(id != id2, "contractId changes with amount");
  }

  // ── ABI selectors ────────────────────────────────────────────────────
  {
    auto sel = [](const std::string& sig) {
      uint8_t md[32];
      keccak(reinterpret_cast<const uint8_t*>(sig.data()), static_cast<int>(sig.size()), md, 32);
      return toHex(md, 4);
    };
    std::string lockSel = EthAbi::functionSelector("lock(address,bytes32,uint256)");
    // functionSelector returns 0x-prefixed 4-byte hex typically
    CHECK(lockSel.size() >= 8, "lock selector non-empty");
    std::string claimSel = EthAbi::functionSelector("claim(bytes32,bytes32)");
    CHECK(claimSel.size() >= 8, "claim selector non-empty");
    std::string getSel = EthAbi::functionSelector("getContract(bytes32)");
    CHECK(getSel.size() >= 8, "getContract selector non-empty");

    // encodeLock starts with lock selector
    Crypto::Hash hl{};
    std::memset(hl.data, 0xab, 32);
    std::string enc = EthAbi::encodeLock(
        "0x2222222222222222222222222222222222222222", hl, 999);
    CHECK(enc.size() > 10 && enc[0] == '0' && enc[1] == 'x', "encodeLock returns 0x hex");
  }

  // ── RLP empty list = 0xc0 ────────────────────────────────────────────
  {
    CryptoNote::SwapDaemon::Crypto::RlpEncoder bare;
    bare.writeEmptyList();
    auto out = bare.finalize();
    CHECK(out.size() == 1 && out[0] == 0xc0, "writeEmptyList encodes 0xc0 not 0x80");
  }

  std::cout << "\nResults: " << g_pass << " passed, " << g_fail << " failed\n";
  return g_fail == 0 ? 0 : 1;
}
