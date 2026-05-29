# swapxfg Chain Interface Refactor — Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace hardcoded per-chain switch blocks in SwapDaemon with a polymorphic `IChainClient` interface and chain registry, so adding a new counterparty chain requires only writing one new class and registering it.

**Architecture:** Introduce `IChainClient` as an abstract base with 4 operations (lock, verifyLock, claim, refund). Each existing chain backend (BCH, ETH, SOL, XMR) becomes a concrete implementation adapting its current RPC client. SwapDaemon replaces its 4 `unique_ptr<XxxRpcClient>` members with a single `map<SwapPair, unique_ptr<IChainClient>>` registry, collapsing ~5 repeated switch blocks into single-dispatch calls. Chain-specific per-swap state moves from `SwapParams` into an opaque `chainState` blob owned by each `IChainClient`.

**Tech Stack:** C++14, CryptoNote build system (CMake), existing JSON-RPC clients over httplib.

---

## File Map

| Action | Path | Responsibility |
|--------|------|----------------|
| Create | `src/SwapDaemon/IChainClient.h` | Abstract interface: lock, verifyLock, claim, refund, chainName |
| Create | `src/SwapDaemon/ChainClientResult.h` | Unified result type replacing per-chain result structs |
| Create | `src/SwapDaemon/ChainRegistry.h` | `map<SwapPair, unique_ptr<IChainClient>>` holder + factory |
| Create | `src/SwapDaemon/ChainRegistry.cpp` | Registration from ChainClientConfig |
| Create | `src/SwapDaemon/BitcoinCash/BchChainClient.h` | IChainClient adapter wrapping BchRpcClient |
| Create | `src/SwapDaemon/BitcoinCash/BchChainClient.cpp` | Translates IChainClient calls → BchRpcClient methods |
| Create | `src/SwapDaemon/Ethereum/EthChainClient.h` | IChainClient adapter wrapping EthRpcClient |
| Create | `src/SwapDaemon/Ethereum/EthChainClient.cpp` | Translates IChainClient calls → EthRpcClient methods |
| Create | `src/SwapDaemon/Solana/SolChainClient.h` | IChainClient adapter wrapping SolRpcClient |
| Create | `src/SwapDaemon/Solana/SolChainClient.cpp` | Translates IChainClient calls → SolRpcClient methods |
| Create | `src/SwapDaemon/Monero/XmrChainClient.h` | IChainClient adapter wrapping MoneroRpcClient |
| Create | `src/SwapDaemon/Monero/XmrChainClient.cpp` | Translates IChainClient calls → MoneroRpcClient methods |
| Modify | `src/SwapDaemon/SwapTypes.h` | Add chainState blob to SwapParams; keep SwapPair extensible |
| Modify | `src/SwapDaemon/SwapTypes.cpp` | Update swapPairToString/FromString to be registry-driven |
| Modify | `src/SwapDaemon/SwapDaemon.h` | Replace 4 client ptrs with ChainRegistry; remove per-chain includes |
| Modify | `src/SwapDaemon/SwapDaemon.cpp` | Replace all 5 switch blocks with registry dispatch |
| Modify | `src/SwapDaemon/ChainClientConfig.cpp` | Parse per-chain config sections from nested JSON |
| Modify | `src/CryptoNoteCore/SwapOfferRelay.h` | Use SwapPair enum for pair field instead of raw uint8_t |
| Modify | `src/CryptoNoteCore/SwapOfferRelay.cpp` | Make getSeedRate/getCtrUsdPrice data-driven |
| Modify | `src/SimpleWallet/SimpleWallet.cpp:1522-1550` | Use swapPairFromString instead of hardcoded if-chain |
| Create | `tests/SwapDaemon/ChainRegistryTests.cpp` | Unit tests for registry + test chain client |
| Create | `tests/SwapDaemon/TestChainClient.h` | Test IChainClient for testing dispatch without real RPCs |

---

## Chunk 1: Interface + Result Type + Registry

### Task 1: Define IChainClient interface and ChainClientResult

**Files:**
- Create: `src/SwapDaemon/ChainClientResult.h`
- Create: `src/SwapDaemon/IChainClient.h`

- [ ] **Step 1: Create ChainClientResult**

This is the unified return type for all chain operations, replacing `SolTxResult`, `MoneroTransferResult`, and ad-hoc bool+string pairs.

```cpp
// src/SwapDaemon/ChainClientResult.h
#pragma once

#include <string>

namespace XfgSwap {

struct ChainClientResult {
  bool success = false;
  std::string txId;     // lock tx hash, claim tx hash, refund tx hash, contract address
  std::string error;    // human-readable error if !success

  static ChainClientResult ok(const std::string& txId) {
    return {true, txId, ""};
  }
  static ChainClientResult fail(const std::string& error) {
    return {false, "", error};
  }
};

} // namespace XfgSwap
```

- [ ] **Step 2: Create IChainClient**

All 4 operations take `SwapParams` (read-only except for `chainState`) plus the credential/config they need. The caller (SwapDaemon) no longer knows *how* each chain locks funds.

```cpp
// src/SwapDaemon/IChainClient.h
#pragma once

#include "SwapTypes.h"
#include "ChainClientResult.h"
#include <string>
#include <memory>

namespace XfgSwap {

class IChainClient {
public:
  virtual ~IChainClient() = default;

  // Human-readable chain name for logging ("BCH", "ETH", "SOL", "XMR")
  virtual std::string chainName() const = 0;

  // Lock funds on the counterparty chain.
  // On success, result.txId contains the lock tx id / contract address.
  virtual ChainClientResult lock(const SwapParams& params) = 0;

  // Verify that a lock exists on the counterparty chain for the expected amount.
  virtual ChainClientResult verifyLock(const SwapParams& params) = 0;

  // Claim locked funds (reveals adaptor secret on HTLC chains).
  virtual ChainClientResult claim(const SwapParams& params) = 0;

  // Refund locked funds after timeout.
  virtual ChainClientResult refund(const SwapParams& params) = 0;
};

} // namespace XfgSwap
```

- [ ] **Step 3: Commit**

```bash
git add src/SwapDaemon/IChainClient.h src/SwapDaemon/ChainClientResult.h
git commit -m "feat(swap): add IChainClient interface and ChainClientResult type"
```

---

### Task 2: Add chainState to SwapParams

**Files:**
- Modify: `src/SwapDaemon/SwapTypes.h:94-156`

- [ ] **Step 1: Add opaque chain-state blob to SwapParams**

This replaces `bchRedeemScriptHex` and any future per-chain fields. Each `IChainClient` serializes its own state into this string (JSON, hex, whatever it needs). The field is persisted alongside the rest of SwapParams.

In `SwapTypes.h`, inside `struct SwapParams`, replace:

```cpp
  // BCH-specific: hex-encoded P2SH redeem script for the HTLC.
  // Set when the BCH HTLC is created (lockHtlc) and read on claim/refund.
  // Must be persisted so claim/refund work after a daemon restart.
  std::string bchRedeemScriptHex;
```

with:

```cpp
  // Per-chain opaque state, serialized by the IChainClient implementation.
  // Persisted across daemon restarts. Each chain client owns its format.
  // (Replaces former bchRedeemScriptHex and similar per-chain fields.)
  std::string chainState;
```

- [ ] **Step 2: Commit**

```bash
git add src/SwapDaemon/SwapTypes.h
git commit -m "refactor(swap): replace bchRedeemScriptHex with generic chainState blob"
```

---

### Task 3: Create ChainRegistry

**Files:**
- Create: `src/SwapDaemon/ChainRegistry.h`
- Create: `src/SwapDaemon/ChainRegistry.cpp`

- [ ] **Step 1: Create ChainRegistry header**

```cpp
// src/SwapDaemon/ChainRegistry.h
#pragma once

#include "IChainClient.h"
#include "SwapTypes.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace XfgSwap {

class ChainRegistry {
public:
  // Register a chain client for a given SwapPair.
  // Takes ownership of the pointer.
  void registerChain(SwapPair pair, std::unique_ptr<IChainClient> client);

  // Get the client for a pair. Returns nullptr if not registered.
  IChainClient* getClient(SwapPair pair) const;

  // Check if a pair has a registered client.
  bool hasChain(SwapPair pair) const;

  // List all registered pairs.
  std::vector<SwapPair> registeredPairs() const;

private:
  std::map<SwapPair, std::unique_ptr<IChainClient>> m_clients;
};

} // namespace XfgSwap
```

- [ ] **Step 2: Create ChainRegistry implementation**

```cpp
// src/SwapDaemon/ChainRegistry.cpp
#include "ChainRegistry.h"

namespace XfgSwap {

void ChainRegistry::registerChain(SwapPair pair, std::unique_ptr<IChainClient> client) {
  m_clients[pair] = std::move(client);
}

IChainClient* ChainRegistry::getClient(SwapPair pair) const {
  auto it = m_clients.find(pair);
  return (it != m_clients.end()) ? it->second.get() : nullptr;
}

bool ChainRegistry::hasChain(SwapPair pair) const {
  return m_clients.count(pair) > 0;
}

std::vector<SwapPair> ChainRegistry::registeredPairs() const {
  std::vector<SwapPair> pairs;
  pairs.reserve(m_clients.size());
  for (const auto& kv : m_clients) {
    pairs.push_back(kv.first);
  }
  return pairs;
}

} // namespace XfgSwap
```

- [ ] **Step 3: Commit**

```bash
git add src/SwapDaemon/ChainRegistry.h src/SwapDaemon/ChainRegistry.cpp
git commit -m "feat(swap): add ChainRegistry for polymorphic chain client dispatch"
```

---

### Task 4: Create TestChainClient and test the registry

**Files:**
- Create: `tests/SwapDaemon/TestChainClient.h`
- Create: `tests/SwapDaemon/ChainRegistryTests.cpp`

- [ ] **Step 1: Create TestChainClient**

```cpp
// tests/SwapDaemon/TestChainClient.h
#pragma once

#include "SwapDaemon/IChainClient.h"

namespace XfgSwap {

class TestChainClient : public IChainClient {
public:
  explicit TestChainClient(const std::string& name) : m_name(name) {}

  std::string chainName() const override { return m_name; }

  ChainClientResult lock(const SwapParams&) override {
    ++lockCalls;
    return lockResult;
  }
  ChainClientResult verifyLock(const SwapParams&) override {
    ++verifyLockCalls;
    return verifyLockResult;
  }
  ChainClientResult claim(const SwapParams&) override {
    ++claimCalls;
    return claimResult;
  }
  ChainClientResult refund(const SwapParams&) override {
    ++refundCalls;
    return refundResult;
  }

  // Configurable results
  ChainClientResult lockResult       = ChainClientResult::ok("test_lock_tx");
  ChainClientResult verifyLockResult = ChainClientResult::ok("test_verify");
  ChainClientResult claimResult      = ChainClientResult::ok("test_claim_tx");
  ChainClientResult refundResult     = ChainClientResult::ok("test_refund_tx");

  // Call counters
  int lockCalls = 0;
  int verifyLockCalls = 0;
  int claimCalls = 0;
  int refundCalls = 0;

private:
  std::string m_name;
};

} // namespace XfgSwap
```

- [ ] **Step 2: Create ChainRegistryTests**

```cpp
// tests/SwapDaemon/ChainRegistryTests.cpp
#include <gtest/gtest.h>
#include "SwapDaemon/ChainRegistry.h"
#include "TestChainClient.h"

using namespace XfgSwap;

TEST(ChainRegistryTest, RegisterAndRetrieve) {
  ChainRegistry registry;
  auto testClient = std::make_unique<TestChainClient>("TEST");
  auto* rawPtr = testClient.get();
  registry.registerChain(SwapPair::BCH, std::move(testClient));

  EXPECT_TRUE(registry.hasChain(SwapPair::BCH));
  EXPECT_EQ(registry.getClient(SwapPair::BCH), rawPtr);
  EXPECT_EQ(registry.getClient(SwapPair::BCH)->chainName(), "TEST");
}

TEST(ChainRegistryTest, UnregisteredPairReturnsNull) {
  ChainRegistry registry;
  EXPECT_FALSE(registry.hasChain(SwapPair::ETH));
  EXPECT_EQ(registry.getClient(SwapPair::ETH), nullptr);
}

TEST(ChainRegistryTest, ListRegisteredPairs) {
  ChainRegistry registry;
  registry.registerChain(SwapPair::BCH, std::make_unique<TestChainClient>("BCH"));
  registry.registerChain(SwapPair::ETH, std::make_unique<TestChainClient>("ETH"));

  auto pairs = registry.registeredPairs();
  EXPECT_EQ(pairs.size(), 2u);
}

TEST(ChainRegistryTest, DispatchLockViaRegistry) {
  ChainRegistry registry;
  auto testClient = std::make_unique<TestChainClient>("BCH");
  testClient->lockResult = ChainClientResult::ok("abc123");
  auto* rawPtr = testClient.get();
  registry.registerChain(SwapPair::BCH, std::move(testClient));

  SwapParams params;
  params.pair = SwapPair::BCH;
  auto result = registry.getClient(SwapPair::BCH)->lock(params);

  EXPECT_TRUE(result.success);
  EXPECT_EQ(result.txId, "abc123");
  EXPECT_EQ(rawPtr->lockCalls, 1);
}
```

- [ ] **Step 3: Build and run tests**

```bash
cd build && cmake .. && make -j$(nproc) ChainRegistryTests
./tests/SwapDaemon/ChainRegistryTests
```

Expected: all 4 tests PASS.

- [ ] **Step 4: Commit**

```bash
git add tests/SwapDaemon/TestChainClient.h tests/SwapDaemon/ChainRegistryTests.cpp
git commit -m "test(swap): add TestChainClient and ChainRegistry unit tests"
```

---

## Chunk 2: Adapt Existing Chain Backends

Each existing chain client gets a thin adapter class implementing `IChainClient`.
The adapters own the underlying `XxxRpcClient` and translate between the unified
interface and the chain-specific method signatures. The existing `XxxRpcClient`
classes are **not modified** — the adapters wrap them.

### Task 5: BchChainClient adapter

**Files:**
- Create: `src/SwapDaemon/BitcoinCash/BchChainClient.h`
- Create: `src/SwapDaemon/BitcoinCash/BchChainClient.cpp`

- [ ] **Step 1: Create BchChainClient header**

```cpp
// src/SwapDaemon/BitcoinCash/BchChainClient.h
#pragma once

#include "../IChainClient.h"
#include "BchRpcClient.h"
#include <string>
#include <memory>

namespace XfgSwap {

class BchChainClient : public IChainClient {
public:
  // wif: WIF-encoded private key for HTLC signing (from config)
  BchChainClient(std::unique_ptr<BchRpcClient> rpc, const std::string& wif);

  std::string chainName() const override { return "BCH"; }
  ChainClientResult lock(const SwapParams& params) override;
  ChainClientResult verifyLock(const SwapParams& params) override;
  ChainClientResult claim(const SwapParams& params) override;
  ChainClientResult refund(const SwapParams& params) override;

private:
  std::unique_ptr<BchRpcClient> m_rpc;
  std::string m_wif;
};

} // namespace XfgSwap
```

- [ ] **Step 2: Create BchChainClient implementation**

Key translations:
- `lock()` calls `m_rpc->lockHtlc()`, stores redeem script in `params.chainState`
- `claim()` calls `m_rpc->claim()`, reads redeem script from `params.chainState`
- `refund()` calls `m_rpc->refundHtlc()`, reads redeem script from `params.chainState`
- `verifyLock()` calls `m_rpc->verifyLock()`

```cpp
// src/SwapDaemon/BitcoinCash/BchChainClient.cpp
#include "BchChainClient.h"
#include "Common/StringTools.h"

namespace XfgSwap {

BchChainClient::BchChainClient(std::unique_ptr<BchRpcClient> rpc, const std::string& wif)
  : m_rpc(std::move(rpc)), m_wif(wif) {}

ChainClientResult BchChainClient::lock(const SwapParams& params) {
  std::string lockTxId;
  bool ok = m_rpc->lockHtlc(
      m_wif,
      params.ctrAddress,
      Common::podToHex(params.adaptorPoint),
      static_cast<uint32_t>(params.ctrTimeoutBlock),
      params.ctrAmount,
      lockTxId);
  if (!ok) return ChainClientResult::fail("BCH lockHtlc failed");
  return ChainClientResult::ok(lockTxId);
}

ChainClientResult BchChainClient::verifyLock(const SwapParams& params) {
  bool ok = m_rpc->verifyLock(params.ctrLockTxId, params.ctrAmount);
  if (!ok) return ChainClientResult::fail("BCH lock not verified");
  return ChainClientResult::ok(params.ctrLockTxId);
}

ChainClientResult BchChainClient::claim(const SwapParams& params) {
  std::string claimTxId;
  bool ok = m_rpc->claim(
      m_wif,
      params.ctrLockTxId, 0, params.ctrAmount,
      params.chainState,  // redeem script hex (formerly bchRedeemScriptHex)
      Common::podToHex(params.adaptorSecret),
      params.ctrAddress,
      claimTxId);
  if (!ok) return ChainClientResult::fail("BCH claim failed");
  return ChainClientResult::ok(claimTxId);
}

ChainClientResult BchChainClient::refund(const SwapParams& params) {
  std::string refundTxId;
  bool ok = m_rpc->refundHtlc(
      m_wif,
      params.ctrLockTxId, 0, params.ctrAmount,
      params.chainState,  // redeem script hex
      params.ctrAddress,
      refundTxId);
  if (!ok) return ChainClientResult::fail("BCH refundHtlc failed");
  return ChainClientResult::ok(refundTxId);
}

} // namespace XfgSwap
```

- [ ] **Step 3: Verify it compiles**

```bash
cd build && cmake .. && make -j$(nproc) SwapDaemon
```

- [ ] **Step 4: Commit**

```bash
git add src/SwapDaemon/BitcoinCash/BchChainClient.h src/SwapDaemon/BitcoinCash/BchChainClient.cpp
git commit -m "feat(swap): add BchChainClient adapter implementing IChainClient"
```

---

### Task 6: EthChainClient adapter

**Files:**
- Create: `src/SwapDaemon/Ethereum/EthChainClient.h`
- Create: `src/SwapDaemon/Ethereum/EthChainClient.cpp`

- [ ] **Step 1: Create EthChainClient header**

```cpp
// src/SwapDaemon/Ethereum/EthChainClient.h
#pragma once

#include "../IChainClient.h"
#include "EthRpcClient.h"
#include <string>
#include <memory>

namespace XfgSwap {

class EthChainClient : public IChainClient {
public:
  EthChainClient(std::unique_ptr<EthRpcClient> rpc, const std::string& address);

  std::string chainName() const override { return "ETH"; }
  ChainClientResult lock(const SwapParams& params) override;
  ChainClientResult verifyLock(const SwapParams& params) override;
  ChainClientResult claim(const SwapParams& params) override;
  ChainClientResult refund(const SwapParams& params) override;

private:
  std::unique_ptr<EthRpcClient> m_rpc;
  std::string m_address;  // "0x..." signer address
};

} // namespace XfgSwap
```

- [ ] **Step 2: Create EthChainClient implementation**

ETH is the only chain that throws `std::runtime_error` for unimplemented signing.
The adapter catches these and returns `ChainClientResult::fail()` — the caller
no longer needs try/catch for chain-specific exceptions.

```cpp
// src/SwapDaemon/Ethereum/EthChainClient.cpp
#include "EthChainClient.h"
#include "Common/StringTools.h"
#include <stdexcept>

namespace XfgSwap {

EthChainClient::EthChainClient(std::unique_ptr<EthRpcClient> rpc, const std::string& address)
  : m_rpc(std::move(rpc)), m_address(address) {}

ChainClientResult EthChainClient::lock(const SwapParams& params) {
  try {
    std::string contractAddress;
    bool ok = m_rpc->deployHtlc(
        m_address,
        params.ctrAddress,
        Common::podToHex(params.adaptorPoint),
        params.ctrTimeoutBlock,
        params.ctrAmount,
        contractAddress);
    if (!ok) return ChainClientResult::fail("ETH deployHtlc failed");
    return ChainClientResult::ok(contractAddress);
  } catch (const std::runtime_error& e) {
    return ChainClientResult::fail(std::string("ETH lock error: ") + e.what());
  }
}

ChainClientResult EthChainClient::verifyLock(const SwapParams& params) {
  bool ok = m_rpc->verifyLock(params.ctrLockTxId, params.ctrAmount);
  if (!ok) return ChainClientResult::fail("ETH lock not verified");
  return ChainClientResult::ok(params.ctrLockTxId);
}

ChainClientResult EthChainClient::claim(const SwapParams& params) {
  try {
    std::string claimTxHash;
    bool ok = m_rpc->claimHtlc(
        m_address,
        params.ctrLockTxId,
        Common::podToHex(params.adaptorSecret),
        claimTxHash);
    if (!ok) return ChainClientResult::fail("ETH claimHtlc failed");
    return ChainClientResult::ok(claimTxHash);
  } catch (const std::runtime_error& e) {
    return ChainClientResult::fail(std::string("ETH claim error: ") + e.what());
  }
}

ChainClientResult EthChainClient::refund(const SwapParams& params) {
  try {
    std::string refundTxHash;
    bool ok = m_rpc->refundHtlc(
        m_address,
        params.ctrLockTxId,
        refundTxHash);
    if (!ok) return ChainClientResult::fail("ETH refundHtlc failed");
    return ChainClientResult::ok(refundTxHash);
  } catch (const std::runtime_error& e) {
    return ChainClientResult::fail(std::string("ETH refund error: ") + e.what());
  }
}

} // namespace XfgSwap
```

- [ ] **Step 3: Verify it compiles**

```bash
cd build && cmake .. && make -j$(nproc) SwapDaemon
```

- [ ] **Step 4: Commit**

```bash
git add src/SwapDaemon/Ethereum/EthChainClient.h src/SwapDaemon/Ethereum/EthChainClient.cpp
git commit -m "feat(swap): add EthChainClient adapter implementing IChainClient"
```

---

### Task 7: SolChainClient adapter

**Files:**
- Create: `src/SwapDaemon/Solana/SolChainClient.h`
- Create: `src/SwapDaemon/Solana/SolChainClient.cpp`

- [ ] **Step 1: Create SolChainClient header**

```cpp
// src/SwapDaemon/Solana/SolChainClient.h
#pragma once

#include "../IChainClient.h"
#include "SolRpcClient.h"
#include <string>
#include <memory>

namespace XfgSwap {

class SolChainClient : public IChainClient {
public:
  SolChainClient(std::unique_ptr<SolRpcClient> rpc, const std::string& keypairBase58);

  std::string chainName() const override { return "SOL"; }
  ChainClientResult lock(const SwapParams& params) override;
  ChainClientResult verifyLock(const SwapParams& params) override;
  ChainClientResult claim(const SwapParams& params) override;
  ChainClientResult refund(const SwapParams& params) override;

private:
  std::unique_ptr<SolRpcClient> m_rpc;
  std::string m_keypairBase58;
};

} // namespace XfgSwap
```

- [ ] **Step 2: Create SolChainClient implementation**

SOL uses `SolTxResult` for all operations. The adapter unpacks this into `ChainClientResult`.

```cpp
// src/SwapDaemon/Solana/SolChainClient.cpp
#include "SolChainClient.h"
#include "Common/StringTools.h"

namespace XfgSwap {

SolChainClient::SolChainClient(std::unique_ptr<SolRpcClient> rpc, const std::string& keypairBase58)
  : m_rpc(std::move(rpc)), m_keypairBase58(keypairBase58) {}

ChainClientResult SolChainClient::lock(const SwapParams& params) {
  SolTxResult solResult;
  bool ok = m_rpc->lock(
      m_keypairBase58,
      params.ctrAddress,
      Common::podToHex(params.adaptorPoint),
      params.ctrTimeoutBlock,
      params.ctrAmount,
      solResult);
  if (!ok || !solResult.confirmed)
    return ChainClientResult::fail("SOL lock failed: " + solResult.error);
  return ChainClientResult::ok(solResult.signature);
}

ChainClientResult SolChainClient::verifyLock(const SwapParams& params) {
  bool ok = m_rpc->verifyLock(params.ctrLockTxId, params.ctrAmount);
  if (!ok) return ChainClientResult::fail("SOL lock not verified");
  return ChainClientResult::ok(params.ctrLockTxId);
}

ChainClientResult SolChainClient::claim(const SwapParams& params) {
  SolTxResult solResult;
  bool ok = m_rpc->claim(
      m_keypairBase58,
      params.ctrLockTxId,
      Common::podToHex(params.adaptorSecret),
      solResult);
  if (!ok || !solResult.confirmed)
    return ChainClientResult::fail("SOL claim failed: " + solResult.error);
  return ChainClientResult::ok(solResult.signature);
}

ChainClientResult SolChainClient::refund(const SwapParams& params) {
  SolTxResult solResult;
  bool ok = m_rpc->refund(
      m_keypairBase58,
      params.ctrLockTxId,
      solResult);
  if (!ok || !solResult.confirmed)
    return ChainClientResult::fail("SOL refund failed: " + solResult.error);
  return ChainClientResult::ok(solResult.signature);
}

} // namespace XfgSwap
```

- [ ] **Step 3: Commit**

```bash
git add src/SwapDaemon/Solana/SolChainClient.h src/SwapDaemon/Solana/SolChainClient.cpp
git commit -m "feat(swap): add SolChainClient adapter implementing IChainClient"
```

---

### Task 8: XmrChainClient adapter

**Files:**
- Create: `src/SwapDaemon/Monero/XmrChainClient.h`
- Create: `src/SwapDaemon/Monero/XmrChainClient.cpp`

- [ ] **Step 1: Create XmrChainClient header**

```cpp
// src/SwapDaemon/Monero/XmrChainClient.h
#pragma once

#include "../IChainClient.h"
#include "MoneroRpcClient.h"
#include <string>
#include <memory>

namespace XfgSwap {

class XmrChainClient : public IChainClient {
public:
  XmrChainClient(std::unique_ptr<MoneroRpcClient> rpc,
                 const std::string& spendKeyHex,
                 const std::string& viewKeyHex);

  std::string chainName() const override { return "XMR"; }
  ChainClientResult lock(const SwapParams& params) override;
  ChainClientResult verifyLock(const SwapParams& params) override;
  ChainClientResult claim(const SwapParams& params) override;
  ChainClientResult refund(const SwapParams& params) override;

private:
  std::unique_ptr<MoneroRpcClient> m_rpc;
  std::string m_spendKeyHex;
  std::string m_viewKeyHex;
};

} // namespace XfgSwap
```

- [ ] **Step 2: Create XmrChainClient implementation**

XMR uses adaptor signatures instead of HTLCs, so its method names differ
(`lockAdaptor`, `claimAdaptor`, `refundAdaptor`). It also passes `ctrAddress`
to `verifyLock` instead of `ctrLockTxId`.

```cpp
// src/SwapDaemon/Monero/XmrChainClient.cpp
#include "XmrChainClient.h"
#include "Common/StringTools.h"

namespace XfgSwap {

XmrChainClient::XmrChainClient(std::unique_ptr<MoneroRpcClient> rpc,
                               const std::string& spendKeyHex,
                               const std::string& viewKeyHex)
  : m_rpc(std::move(rpc)), m_spendKeyHex(spendKeyHex), m_viewKeyHex(viewKeyHex) {}

ChainClientResult XmrChainClient::lock(const SwapParams& params) {
  MoneroTransferResult xmrResult;
  bool ok = m_rpc->lockAdaptor(
      params.ctrAddress,
      params.ctrAmount,
      xmrResult);
  if (!ok || !xmrResult.success)
    return ChainClientResult::fail("XMR lockAdaptor failed: " + xmrResult.error);
  return ChainClientResult::ok(xmrResult.txHash);
}

ChainClientResult XmrChainClient::verifyLock(const SwapParams& params) {
  bool ok = m_rpc->verifyLock(params.ctrAddress, params.ctrAmount);
  if (!ok) return ChainClientResult::fail("XMR lock not verified");
  return ChainClientResult::ok(params.ctrAddress);
}

ChainClientResult XmrChainClient::claim(const SwapParams& params) {
  MoneroTransferResult xmrResult;
  bool ok = m_rpc->claimAdaptor(
      m_spendKeyHex,
      Common::podToHex(params.peerSwapPubKey),
      Common::podToHex(params.adaptorSecret),
      m_viewKeyHex,
      params.ctrAddress,
      xmrResult);
  if (!ok || !xmrResult.success)
    return ChainClientResult::fail("XMR claimAdaptor failed: " + xmrResult.error);
  return ChainClientResult::ok(xmrResult.txHash);
}

ChainClientResult XmrChainClient::refund(const SwapParams& params) {
  MoneroTransferResult xmrResult;
  bool ok = m_rpc->refundAdaptor(
      m_spendKeyHex,
      Common::podToHex(params.peerSwapPubKey),
      m_viewKeyHex,
      params.ctrAddress,
      xmrResult);
  if (!ok || !xmrResult.success)
    return ChainClientResult::fail("XMR refundAdaptor failed: " + xmrResult.error);
  return ChainClientResult::ok(xmrResult.txHash);
}

} // namespace XfgSwap
```

- [ ] **Step 3: Commit**

```bash
git add src/SwapDaemon/Monero/XmrChainClient.h src/SwapDaemon/Monero/XmrChainClient.cpp
git commit -m "feat(swap): add XmrChainClient adapter implementing IChainClient"
```

---

## Chunk 3: Rewire SwapDaemon to Use Registry

### Task 9: Refactor SwapDaemon to use ChainRegistry

**Files:**
- Modify: `src/SwapDaemon/SwapDaemon.h:27-31, 217-225`
- Modify: `src/SwapDaemon/SwapDaemon.cpp:100-140, 530-780, 1180-1262`

This is the core payoff: replacing ~250 lines of switch blocks with ~30 lines
of registry dispatch.

- [ ] **Step 1: Update SwapDaemon.h**

Replace the 4 individual chain client includes and member pointers with the
registry. Keep `ChainClientConfig` and `m_chainCfg` for now (config parsing
is refactored in Task 11).

In the includes section, replace:

```cpp
#include "BitcoinCash/BchRpcClient.h"
#include "Ethereum/EthRpcClient.h"
#include "Solana/SolRpcClient.h"
#include "Monero/MoneroRpcClient.h"
```

with:

```cpp
#include "ChainRegistry.h"
```

In the private members section, replace:

```cpp
   std::unique_ptr<BchRpcClient>    m_bchClient;
   std::unique_ptr<EthRpcClient>    m_ethClient;
   std::unique_ptr<SolRpcClient>    m_solClient;
   std::unique_ptr<MoneroRpcClient> m_xmrClient;
```

with:

```cpp
   ChainRegistry m_chainRegistry;
```

Also remove `m_solKeypairBase58` (it moves into `SolChainClient`).

- [ ] **Step 2: Update SwapDaemon constructor to populate registry**

In `SwapDaemon.cpp`, replace the constructor body that creates individual clients
(lines ~107-140) with registry registration. Include the new adapter headers at
the top of the file:

```cpp
#include "BitcoinCash/BchChainClient.h"
#include "Ethereum/EthChainClient.h"
#include "Solana/SolChainClient.h"
#include "Monero/XmrChainClient.h"
```

Constructor body becomes:

```cpp
  m_chainCfg = chainCfg;
  if (!chainCfg.bchHost.empty()) {
    auto rpc = std::make_unique<BchRpcClient>(
        chainCfg.bchHost, chainCfg.bchPort,
        chainCfg.bchRpcUser, chainCfg.bchRpcPass);
    m_chainRegistry.registerChain(SwapPair::BCH,
        std::make_unique<BchChainClient>(std::move(rpc), chainCfg.bchWif));
    m_logger(Logging::INFO) << "BCH chain client registered";
  }
  if (!chainCfg.ethHost.empty() &&
      !chainCfg.ethPrivKeyHex.empty() && !chainCfg.ethAddress.empty()) {
    auto rpc = std::make_unique<EthRpcClient>(
        chainCfg.ethHost, chainCfg.ethPort,
        chainCfg.ethPrivKeyHex, chainCfg.ethAddress, chainCfg.ethChainId);
    m_chainRegistry.registerChain(SwapPair::ETH,
        std::make_unique<EthChainClient>(std::move(rpc), chainCfg.ethAddress));
    m_logger(Logging::INFO) << "ETH chain client registered";
  }
  if (!chainCfg.solHost.empty()) {
    auto rpc = std::make_unique<SolRpcClient>(chainCfg.solHost, chainCfg.solPort,
                                               chainCfg.solProgramId);
    std::string keypairBase58 = loadSolKeypairBase58(chainCfg.solKeypairPath);
    m_chainRegistry.registerChain(SwapPair::SOL,
        std::make_unique<SolChainClient>(std::move(rpc), keypairBase58));
    m_logger(Logging::INFO) << "SOL chain client registered";
  }
  if (!chainCfg.xmrDaemonHost.empty()) {
    auto rpc = std::make_unique<MoneroRpcClient>(
        chainCfg.xmrDaemonHost, chainCfg.xmrDaemonPort,
        chainCfg.xmrWalletHost, chainCfg.xmrWalletPort);
    m_chainRegistry.registerChain(SwapPair::XMR,
        std::make_unique<XmrChainClient>(std::move(rpc),
            chainCfg.xmrSpendKeyHex, chainCfg.xmrViewKeyHex));
    m_logger(Logging::INFO) << "XMR chain client registered";
  }
```

- [ ] **Step 3: Replace the lock switch block (lines 539-634)**

Replace the entire `switch (params.pair)` block inside `ADAPTOR_PRESIGS_READY` /
Bob's lock path with:

```cpp
        auto* client = m_chainRegistry.getClient(params.pair);
        if (!client) {
          m_logger(Logging::ERROR) << "  " << swapPairToString(params.pair)
            << " client not configured — cannot lock";
        } else {
          auto result = client->lock(params);
          if (result.success) {
            m_logger(Logging::INFO) << "  " << client->chainName()
              << " locked, txid: " << result.txId;
            params.ctrLockTxId = result.txId;
            sm.transition(SwapState::ADAPTOR_CTR_LOCKED);
            m_db.saveSwap(sm);
            lockOk = true;
          } else {
            m_logger(Logging::ERROR) << "  " << client->chainName()
              << " lock failed: " << result.error;
          }
        }
```

- [ ] **Step 4: Replace the verifyLock switch block (lines 641-670)**

```cpp
        auto* client = m_chainRegistry.getClient(params.pair);
        if (!client) {
          m_logger(Logging::WARNING) << "  " << swapPairToString(params.pair)
            << " client not configured — cannot verify lock";
        } else {
          auto result = client->verifyLock(params);
          verified = result.success;
        }
```

- [ ] **Step 5: Replace the claim switch block (lines 689-779)**

```cpp
        auto* client = m_chainRegistry.getClient(params.pair);
        if (!client) {
          m_logger(Logging::ERROR) << "  " << swapPairToString(params.pair)
            << " client not configured — cannot claim";
        } else {
          auto result = client->claim(params);
          if (result.success) {
            m_logger(Logging::INFO) << "  " << client->chainName()
              << " claimed, txid: " << result.txId;
            sm.transition(SwapState::ADAPTOR_SECRET_REVEALED);
            m_db.saveSwap(sm);
            claimOk = true;
          } else {
            m_logger(Logging::ERROR) << "  " << client->chainName()
              << " claim failed: " << result.error;
          }
        }
```

- [ ] **Step 6: Replace the refund switch block (lines 1181-1262)**

```cpp
    auto* client = m_chainRegistry.getClient(params.pair);
    if (!client) {
      m_logger(Logging::ERROR) << "  " << swapPairToString(params.pair)
        << " client not configured — cannot refund";
    } else {
      auto result = client->refund(params);
      if (result.success) {
        m_logger(Logging::INFO) << "  " << client->chainName()
          << " refunded, txid: " << result.txId;
        ctrRefundOk = true;
      } else {
        m_logger(Logging::ERROR) << "  " << client->chainName()
          << " refund failed: " << result.error;
      }
    }
```

- [ ] **Step 7: Handle ETH-specific FAILED transition**

The old ETH code had a special case: `runtime_error` → `FAILED` state. This
is now handled by `EthChainClient` returning `ChainClientResult::fail()`. But
the old lock block also set `sm.transition(SwapState::FAILED)` for ETH errors.

To preserve this: in the unified lock dispatch, check if the error message
starts with "ETH lock error:" (indicating an unrecoverable signer issue) and
transition to FAILED. Better yet, add a `bool fatal` field to `ChainClientResult`:

In `ChainClientResult.h`, add:

```cpp
  bool fatal = false;  // if true, swap should transition to FAILED (unrecoverable)
```

In `EthChainClient::lock()`, when catching `runtime_error`:

```cpp
    auto r = ChainClientResult::fail(...);
    r.fatal = true;
    return r;
```

In the unified lock dispatch in SwapDaemon, after `!result.success`:

```cpp
          if (result.fatal) {
            sm.transition(SwapState::FAILED);
            m_db.saveSwap(sm);
          }
```

- [ ] **Step 8: Build full project to verify compilation**

```bash
cd build && cmake .. && make -j$(nproc)
```

- [ ] **Step 9: Run existing tests**

```bash
cd build && ctest --output-on-failure
```

- [ ] **Step 10: Commit**

```bash
git add src/SwapDaemon/SwapDaemon.h src/SwapDaemon/SwapDaemon.cpp \
        src/SwapDaemon/ChainClientResult.h
git commit -m "refactor(swap): replace chain switch blocks with ChainRegistry dispatch"
```

---

## Chunk 4: Clean Up Peripheral Code

### Task 10: Update SwapOfferRelay pair handling

**Files:**
- Modify: `src/CryptoNoteCore/SwapOfferRelay.h:42,70-71`
- Modify: `src/CryptoNoteCore/SwapOfferRelay.cpp:32-50`
- Modify: `src/SwapDaemon/SwapTypes.h`
- Modify: `src/SwapDaemon/SwapTypes.cpp`

- [ ] **Step 1: Make SwapPair a registry-friendly enum**

Currently `SwapPair` is a fixed enum with 4 values. For extensibility,
keep the enum but add a general `swapPairFromUint8` function so the P2P
layer (which uses raw `uint8_t` pair bytes) can handle new pairs without
code changes to the relay. In `SwapTypes.h`:

```cpp
enum class SwapPair : uint8_t {
  SOL = 0,
  ETH = 1,
  XMR = 2,
  BCH = 3,
  // New chains: assign sequential IDs here.
  // P2P wire format uses the uint8_t value directly.
};
```

In `SwapTypes.cpp`, change `swapPairFromString` to not throw but return
an optional or use a bool out-param:

```cpp
bool swapPairFromString(const std::string& s, SwapPair& out) {
  std::string upper = s;
  std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
  if (upper == "SOL") { out = SwapPair::SOL; return true; }
  if (upper == "ETH") { out = SwapPair::ETH; return true; }
  if (upper == "XMR") { out = SwapPair::XMR; return true; }
  if (upper == "BCH") { out = SwapPair::BCH; return true; }
  return false;
}
```

Keep the throwing overload for backward compat but have it call the
bool version internally.

- [ ] **Step 2: Make getSeedRate/getCtrUsdPrice data-driven**

In `SwapOfferRelay.cpp`, replace the switch-based seed rates with a
static map so new chains just need a new entry:

```cpp
static const std::map<uint8_t, double> kSeedRates = {
  {1, 214000.0},  // ETH
  {2, 46900.0},   // BCH
  {0, 34300.0},   // XMR
};

double SwapOfferRelay::getSeedRate(uint8_t pair) {
  auto it = kSeedRates.find(pair);
  return (it != kSeedRates.end()) ? it->second : 0.0;
}
```

Same pattern for `getCtrUsdPrice`.

- [ ] **Step 3: Commit**

```bash
git add src/SwapDaemon/SwapTypes.h src/SwapDaemon/SwapTypes.cpp \
        src/CryptoNoteCore/SwapOfferRelay.cpp
git commit -m "refactor(swap): make pair handling data-driven for extensibility"
```

---

### Task 11: Update SimpleWallet pair parsing

**Files:**
- Modify: `src/SimpleWallet/SimpleWallet.cpp:1522-1550`

- [ ] **Step 1: Replace hardcoded if-chain with swapPairFromString**

Replace:

```cpp
    if (pairStr == "XMR") pair = XfgSwap::SwapPair::XMR;
    else if (pairStr == "ETH") pair = XfgSwap::SwapPair::ETH;
    else if (pairStr == "BCH") pair = XfgSwap::SwapPair::BCH;
    else {
      fail_msg_writer() << "Invalid pair: " << args[2] << ". Use XMR, ETH, or BCH.";
```

with:

```cpp
    XfgSwap::SwapPair pair;
    if (!XfgSwap::swapPairFromString(pairStr, pair)) {
      fail_msg_writer() << "Invalid pair: " << args[2]
        << ". Supported: XMR, ETH, BCH, SOL";
```

Also update the help text at line 1522 to list all supported pairs dynamically
(or at minimum add SOL which was missing).

- [ ] **Step 2: Commit**

```bash
git add src/SimpleWallet/SimpleWallet.cpp
git commit -m "refactor(swap): use swapPairFromString in SimpleWallet"
```

---

### Task 12: Update RPC layer

**Files:**
- Modify: `src/Rpc/RpcServer.cpp:841,860,954,985`

- [ ] **Step 1: Verify RPC already uses swapPairToString/FromString**

The RPC layer at lines 841, 860, 954, 985 already calls `swapPairToString` and
`swapPairFromString`. Since we kept the function signatures backward-compatible
(the bool overload is new, the throwing overload still exists), no changes are
needed here. Just verify compilation.

- [ ] **Step 2: Commit** (skip if no changes)

---

### Task 13: Final build + full test pass

- [ ] **Step 1: Full clean build**

```bash
cd build && rm -rf * && cmake .. && make -j$(nproc)
```

- [ ] **Step 2: Run all tests**

```bash
cd build && ctest --output-on-failure
```

- [ ] **Step 3: Verify SwapDaemon binary starts**

```bash
./build/src/SwapDaemon/swapxfg --help
```

- [ ] **Step 4: Commit any fixups**

---

## Chunk 5: Adding a New Chain (Template)

This is the payoff — a reference checklist for adding any new chain after the
refactor. Future chains (DCR, LTC, BTC, DOGE, etc.) follow this template.

### Template: Adding chain `XXX`

**Estimated time: 2-3 days.**

**Step 1: Create the RPC client (~1-2 days)**

```
src/SwapDaemon/Xxx/
  XxxRpcClient.h     — JSON-RPC wrapper for the chain's node
  XxxRpcClient.cpp   — lock/verify/claim/refund over RPC
  XxxChainClient.h   — IChainClient adapter (thin, ~50 lines)
  XxxChainClient.cpp — translates IChainClient → XxxRpcClient calls
```

For Bitcoin-script chains (LTC, DCR, DOGE, BTC), copy `BchRpcClient` and
`BchChainClient` as a starting point — the HTLC script structure is nearly
identical. Adjust:
- Address format / prefix bytes
- Transaction serialization (witness vs legacy)
- RPC method names (some chains rename `sendrawtransaction`)
- Timeout semantics (block height vs UNIX timestamp for CSV/CLTV)

For CryptoNote/privacy chains, copy `MoneroRpcClient` and `XmrChainClient`.

For EVM chains / L2s, copy `EthRpcClient` and `EthChainClient`.

**Step 2: Register the chain (~30 minutes)**

1. Add `XXX = N` to `SwapPair` enum in `SwapTypes.h`
2. Add `"XXX"` to `swapPairToString` and `swapPairFromString` in `SwapTypes.cpp`
3. Add `xxx_*` fields to `ChainClientConfig` in `SwapDaemon.h`
4. Add `xxx_*` parsing to `loadChainClientConfig` in `ChainClientConfig.cpp`
5. Add construction + `m_chainRegistry.registerChain()` in `SwapDaemon` constructor
6. Add seed rate + USD price entry to `SwapOfferRelay.cpp` maps

**Step 3: Test (~0.5 day)**

1. Unit test the RPC client against a regtest/testnet node
2. End-to-end swap test on testnet

**That's it.** No switch blocks to update. No `SwapDaemon.cpp` dispatch changes.
No `SimpleWallet` if-chains. The registry + interface handle everything.

---

## Dependency Graph

```
Task 1 (IChainClient + Result)
  └── Task 2 (chainState in SwapParams)
  └── Task 3 (ChainRegistry)
        └── Task 4 (TestChainClient + tests)
        └── Tasks 5-8 (4 adapters, can be parallelized)
              └── Task 9 (rewire SwapDaemon — depends on all adapters)
                    └── Tasks 10-12 (peripheral cleanup, can be parallelized)
                          └── Task 13 (final build + test)
```

Tasks 5, 6, 7, 8 are independent and can be done in parallel.
Tasks 10, 11, 12 are independent and can be done in parallel.
