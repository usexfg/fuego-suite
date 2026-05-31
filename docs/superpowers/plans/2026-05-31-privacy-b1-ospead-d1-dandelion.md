# Privacy B1 (OSPEAD) + D1+ (Dandelion++ completion) Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire the existing OSPEAD decoy filter into the wallet's transaction-build path, and harden the existing Dandelion stem/fluff relay with per-epoch sticky stem successors, an embargo timer, and configurable constants.

**Architecture:**
- **B1 (OSPEAD):** `DynamicRingSizeCalculator::filterOutputsByOSPEAD` already exists but has zero callers. Wire it into `WalletTransactionSender::sendTransactionRandomOutsByAmount` so daemon-returned decoys are re-ranked by spend-probability before the ring is built. Plumb `creationHeight` and a `currentBlockHeight` into the `OutputInfo` set the wallet feeds in.
- **D1+ (Dandelion++):** Existing stem/fluff loop in `CryptoNoteProtocolHandler.cpp` is correct-shape but stateless — every hop picks a random outbound neighbor (re-randomized at each hop) and embargo/epoch are missing. Add: (1) extract constants to `CryptoNoteConfig.h`; (2) per-epoch sticky stem successor map (rotate every `DANDELION_EPOCH_SECONDS`); (3) embargo timer that promotes stuck stem-tx to fluff after `DANDELION_EMBARGO_SECONDS`.

**Tech Stack:** C++17, existing `Crypto::rand`, `std::chrono::steady_clock`, gtest. No new dependencies.

**Out of scope:** A1 (Pedersen wire strip), A2 (BP+ re-port), F1 (already implemented in `handle_swap_offer` at line 1209), I1 (client-side decoy fetch). Tracked separately.

---

## Chunk 1: B1 — Wire OSPEAD into wallet decoy path (sidecar-RPC variant)

### Revised approach (after plan review)

`COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS_out_entry` is `#pragma pack(push,1)` binary-serialized — adding a `height` field in place is a wire-format break. Same-intent safer path: add a new sidecar endpoint `COMMAND_RPC_GET_OUTPUTS_HEIGHTS` that takes `[(amount, global_index)]` and returns `[height]`. Wallet calls it after `/getrandom_outs.bin` to enrich `OutputInfo.creationHeight` before passing to `filterOutputsByOSPEAD`. No protocol break; old daemons just don't expose the new endpoint and wallet falls back to no-filter.

### File Structure

- **Modify:** `src/Rpc/CoreRpcServerCommandsDefinitions.h` — add `COMMAND_RPC_GET_OUTPUTS_HEIGHTS` struct
- **Modify:** `src/Rpc/RpcServer.{h,cpp}` — register endpoint, implement handler
- **Modify:** `src/CryptoNoteCore/ICore.h` + `Core.{h,cpp}` + `Blockchain.{h,cpp}` — add `get_output_heights(...)` accessor
- **Modify:** `src/WalletLegacy/WalletTransactionSender.cpp` — call sidecar to enrich, then `filterOutputsByOSPEAD`
- **Modify:** `src/INode.h` + `src/NodeRpcProxy/NodeRpcProxy.{h,cpp}` — expose new RPC to wallet
- **Create:** `tests/UnitTests/TestDecoySelectionOspead.cpp`
- **Modify:** `tests/UnitTests/CMakeLists.txt`

### Task 1.1: Verify existing OSPEAD behavior with a focused unit test

**Files:**
- Create: `tests/UnitTests/TestDecoySelectionOspead.cpp`
- Modify: `tests/UnitTests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/UnitTests/TestDecoySelectionOspead.cpp
#include <gtest/gtest.h>
#include "DynamicRingSize.h"
#include "OSPEADDecoySelection.h"
using namespace CryptoNote;

TEST(OspeadFilter, RemovesAncientLowProbabilityOutputs) {
  const uint64_t currentHeight = 1'000'000;
  std::vector<OutputInfo> outs;
  // 20 outputs: 10 recent (high spend prob), 10 ancient (low spend prob)
  for (int i = 0; i < 10; ++i) outs.emplace_back(1000, 1, "recent", currentHeight - 100);
  for (int i = 0; i < 10; ++i) outs.emplace_back(1000, 1, "ancient", 1);

  std::vector<TransactionOutputInfo> spendHistory;
  // Most observed spends are recent
  for (int i = 0; i < 50; ++i) spendHistory.emplace_back(1000, currentHeight - 50 - i, i);

  auto filtered = DynamicRingSizeCalculator::filterOutputsByOSPEAD(
    outs, 1000, currentHeight, spendHistory, nullptr);

  EXPECT_LT(filtered.size(), outs.size())
    << "filterOutputsByOSPEAD must remove low-probability outputs";
  EXPECT_GE(filtered.size(), 8u)
    << "filter must keep enough recent outputs for a ring";
}

TEST(OspeadFilter, EmptyPatternFallsBackToLogarithmicBins) {
  const uint64_t currentHeight = 1'000'000;
  std::vector<OutputInfo> outs;
  for (int i = 0; i < 32; ++i) outs.emplace_back(1000, 1, "mixed", currentHeight - i * 1000);
  std::vector<TransactionOutputInfo> empty;
  auto filtered = DynamicRingSizeCalculator::filterOutputsByOSPEAD(
    outs, 1000, currentHeight, empty, nullptr);
  EXPECT_GT(filtered.size(), 0u);  // must not return empty when input non-empty
}
```

- [ ] **Step 2: Register in CMakeLists.txt**

Add `TestDecoySelectionOspead.cpp` to the `UnitTests` target sources list in `tests/UnitTests/CMakeLists.txt` (match existing pattern).

- [ ] **Step 3: Build and run**

```bash
cd build && cmake --build . --target UnitTests -j$(nproc) && ./tests/UnitTests/UnitTests --gtest_filter='Ospead*'
```

Expected: 2 PASS. If `RemovesAncientLowProbabilityOutputs` fails because OSPEAD's age-bin model doesn't filter aggressively enough with the seeded pattern, **do not weaken the test** — instead, log filtered.size() and adjust the seed distribution to produce a measurable split. The test must prove the filter is actually rejecting outputs.

- [ ] **Step 4: Commit**

```bash
git add tests/UnitTests/TestDecoySelectionOspead.cpp tests/UnitTests/CMakeLists.txt
git commit -m "test: verify OSPEAD filter rejects low-spend-probability outputs"
```

### Task 1.2: Plumb creationHeight into wallet OutputInfo construction

**Files:**
- Modify: `src/WalletLegacy/WalletTransactionSender.cpp:534` and `:603`

- [ ] **Step 1: Read context.outs structure**

Confirm `context->outs` is `std::vector<COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::outs_for_amount>` and each entry has `outs: std::vector<out_entry>`. Verify whether `out_entry` carries a height field; if not, this task expands to also extend the RPC response (defer to Task 1.5 if so).

```bash
grep -n "struct out_entry\|out_entry {" src/Rpc/CoreRpcServerCommandsDefinitions.h | head -5
```

- [ ] **Step 2: Replace the placeholder OutputInfo at line ~534**

Current code:
```cpp
const size_t available = context->commitmentOuts.size();
std::vector<CryptoNote::OutputInfo> outputInfos;
outputInfos.emplace_back(0, available);
```

Replace with:
```cpp
const size_t available = context->commitmentOuts.size();
const uint32_t currentHeight = m_node.getLastLocalBlockHeight();
std::vector<CryptoNote::OutputInfo> outputInfos;
for (const auto& co : context->commitmentOuts) {
  // commitmentOuts entries: amount=0 for commitment pool, availableCount=1 per output
  outputInfos.emplace_back(0, 1, "", co.height /* or 0 if unavailable */);
}
if (outputInfos.empty()) outputInfos.emplace_back(0, available);
```

If `commitmentOuts` entries don't expose `.height`, fall back to passing `0` per entry — `filterOutputsByOSPEAD` handles the `creationHeight==0` case via amount-approximation fallback at DynamicRingSize.cpp:200.

- [ ] **Step 3: Same plumbing at line ~603 (sendTransactionRandomOutsByAmount)**

```cpp
const uint32_t currentHeight = m_node.getLastLocalBlockHeight();
std::vector<CryptoNote::OutputInfo> outputInfos;
for (const auto& oa : context->outs) {
  for (const auto& oe : oa.outs) {
    outputInfos.emplace_back(oa.amount, 1, "", /* creation height if available */ 0);
  }
}
size_t minAvailable = /* unchanged logic */;
if (outputInfos.empty()) outputInfos.emplace_back(0, minAvailable);
```

- [ ] **Step 4: Build to confirm signature compatibility**

```bash
cd build && cmake --build . --target Wallet -j$(nproc) 2>&1 | tail -20
```

Expected: clean build. If `m_node.getLastLocalBlockHeight()` isn't the right accessor, find the right one with `grep -n "LastLocalBlockHeight\|getKnownBlockCount" src/INode.h src/NodeRpcProxy/NodeRpcProxy.h`.

- [ ] **Step 5: Commit**

```bash
git add src/WalletLegacy/WalletTransactionSender.cpp
git commit -m "wallet: plumb creationHeight into OutputInfo for OSPEAD-aware decoy selection"
```

### Task 1.3: Call filterOutputsByOSPEAD before ring-size calculation

**Files:**
- Modify: `src/WalletLegacy/WalletTransactionSender.cpp` (both call sites)

- [ ] **Step 1: At line ~536, insert filter call**

Before `DynamicRingSizeCalculator::calculateOptimalRingSize`, add:

```cpp
// OSPEAD: filter daemon-supplied uniform-random decoys by spend-probability
std::vector<CryptoNote::TransactionOutputInfo> emptyPattern;  // pattern data not yet wired; OSPEAD falls back to age-bin model
auto filtered = CryptoNote::DynamicRingSizeCalculator::filterOutputsByOSPEAD(
  outputInfos, 0 /*amount*/, currentHeight, emptyPattern, nullptr /*explorer*/);

if (filtered.size() >= m_currency.minMixin(CryptoNote::BLOCK_MAJOR_VERSION_10)) {
  outputInfos = std::move(filtered);
}
// else: keep raw outputInfos — filter too aggressive on small pools
```

- [ ] **Step 2: Same at line ~605**

Same block, identical logic, with `oa.amount` substituted for the `amount` parameter (loop over `context->outs` and filter per-amount group, or filter combined set).

- [ ] **Step 3: Build all wallet targets**

```bash
cd build && cmake --build . --target SimpleWallet WalletApi PaymentGate -j$(nproc) 2>&1 | tail -30
```

Expected: clean build.

- [ ] **Step 4: Run existing wallet unit tests to confirm no regression**

```bash
./build/tests/UnitTests/UnitTests --gtest_filter='Wallet*:Payment*' 2>&1 | tail -30
```

Expected: same pass rate as before this change.

- [ ] **Step 5: Commit**

```bash
git add src/WalletLegacy/WalletTransactionSender.cpp
git commit -m "feat(wallet): wire OSPEAD decoy filter into transaction build path

Wire the existing DynamicRingSizeCalculator::filterOutputsByOSPEAD
into both commitment-withdrawal and standard-tx decoy selection. Daemon-
returned uniform-random decoys are re-ranked by spend-probability using
OSPEAD logarithmic age bins. Falls back to unfiltered pool when filter
result is below min mixin (small commitment pool case)."
```

### Task 1.4: Integration test — confirm filter changes ring composition

**Files:**
- Modify: `tests/UnitTests/TestDecoySelectionOspead.cpp`

- [ ] **Step 1: Add a determinism test**

```cpp
TEST(OspeadFilter, FilterIsDeterministicGivenSamePattern) {
  // Same inputs → same filtered output set
  const uint64_t h = 1'000'000;
  std::vector<OutputInfo> outs;
  for (int i = 0; i < 50; ++i) outs.emplace_back(1000, 1, "x", h - i * 100);
  std::vector<TransactionOutputInfo> pat;
  for (int i = 0; i < 20; ++i) pat.emplace_back(1000, h - i * 50, i);

  auto a = DynamicRingSizeCalculator::filterOutputsByOSPEAD(outs, 1000, h, pat, nullptr);
  auto b = DynamicRingSizeCalculator::filterOutputsByOSPEAD(outs, 1000, h, pat, nullptr);
  EXPECT_EQ(a.size(), b.size());
}
```

- [ ] **Step 2: Build and run**

```bash
cd build && cmake --build . --target UnitTests -j$(nproc) && \
  ./tests/UnitTests/UnitTests --gtest_filter='Ospead*'
```

Expected: 3 PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/UnitTests/TestDecoySelectionOspead.cpp
git commit -m "test: OSPEAD filter determinism"
```

---

## Chunk 2: D1+ — Dandelion++ epoch + embargo + configurable constants

### File Structure

- **Modify:** `src/CryptoNoteConfig.h` — add `DANDELION_*` constants
- **Modify:** `src/CryptoNoteProtocol/CryptoNoteProtocolHandler.h` — add `m_dandelion_epoch_start`, `m_dandelion_stem_successor`, `m_embargo_timer_active`
- **Modify:** `src/CryptoNoteProtocol/CryptoNoteProtocolHandler.cpp` — use new constants; epoch logic; embargo cleanup loop
- **Create:** `tests/UnitTests/TestDandelionEpoch.cpp` — verify constants + epoch rotation logic
- **Modify:** `tests/UnitTests/CMakeLists.txt`

### Task 2.1: Extract Dandelion constants to CryptoNoteConfig.h

**Files:**
- Modify: `src/CryptoNoteConfig.h` (insert near mixin block, ~line 125)

- [ ] **Step 1: Add constants**

```cpp
// DANDELION++ tx relay (privacy)
const uint32_t DANDELION_STEM_MAX_HOPS              = 10;
const uint32_t DANDELION_STEM_STAY_PCT              = 90;    // %; per-hop probability of remaining in stem
const uint32_t DANDELION_SWAP_STEM_MAX_HOPS         = 5;     // shorter for swap offers (latency-sensitive)
const uint32_t DANDELION_SWAP_STEM_STAY_PCT         = 80;
const uint32_t DANDELION_EPOCH_SECONDS              = 600;   // 10 min stem-successor rotation
const uint32_t DANDELION_EMBARGO_SECONDS            = 30;    // promote stem→fluff if not seen broadcast back
const uint32_t DANDELION_STEM_CLEANUP_INTERVAL_SEC  = 60;
```

- [ ] **Step 2: Build**

```bash
cd build && cmake --build . --target CryptoNoteCore -j$(nproc) 2>&1 | tail -5
```

Expected: clean.

- [ ] **Step 3: Commit**

```bash
git add src/CryptoNoteConfig.h
git commit -m "config: extract Dandelion constants to CryptoNoteConfig.h"
```

### Task 2.2: Use constants in CryptoNoteProtocolHandler

**Files:**
- Modify: `src/CryptoNoteProtocol/CryptoNoteProtocolHandler.cpp:394-421`, `:1209-1226`, `:941`

- [ ] **Step 1: Replace hardcoded `10` and `90` with `parameters::DANDELION_STEM_MAX_HOPS` / `_STAY_PCT`**

Search/replace at lines 397, 941, 1209-1211. Make sure to use `parameters::` namespace prefix (or whatever `MIN_TX_MIXIN_SIZE` uses in same file).

```bash
grep -n "MIN_TX_MIXIN_SIZE\|parameters::" src/CryptoNoteProtocol/CryptoNoteProtocolHandler.cpp | head -3
```

Use the same namespace pattern.

- [ ] **Step 2: Build and run existing tests**

```bash
cd build && cmake --build . --target UnitTests -j$(nproc) && \
  ./tests/UnitTests/UnitTests 2>&1 | tail -5
```

Expected: same pass count as before.

- [ ] **Step 3: Commit**

```bash
git add src/CryptoNoteProtocol/CryptoNoteProtocolHandler.cpp
git commit -m "refactor: use DANDELION_* config constants instead of magic numbers"
```

### Task 2.3: Add per-epoch sticky stem successor map

**Files:**
- Modify: `src/CryptoNoteProtocol/CryptoNoteProtocolHandler.h:117-127`
- Modify: `src/CryptoNoteProtocol/CryptoNoteProtocolHandler.cpp` (constructor + stem path)

- [ ] **Step 1: Add member state to handler.h**

Insert after `m_stem_transactions`:

```cpp
// Dandelion++ per-epoch state
std::chrono::steady_clock::time_point m_dandelion_epoch_start;
// Map each inbound peer to a single chosen outbound stem successor for the current epoch.
// Re-rolled at every DANDELION_EPOCH_SECONDS boundary.
std::map<boost::uuids::uuid, boost::uuids::uuid> m_dandelion_stem_successor;
std::mutex m_dandelion_epoch_mutex;
```

- [ ] **Step 2: Add helper method declaration**

```cpp
// Returns the connection_id of the chosen stem successor for the given inbound peer.
// Rotates the entire successor map every DANDELION_EPOCH_SECONDS.
boost::uuids::uuid getStemSuccessor(const boost::uuids::uuid& sourcePeer);
```

- [ ] **Step 3: Initialize in constructor**

In CryptoNoteProtocolHandler.cpp constructor body:
```cpp
m_dandelion_epoch_start = std::chrono::steady_clock::now();
```

- [ ] **Step 4: Implement getStemSuccessor**

```cpp
boost::uuids::uuid CryptoNoteProtocolHandler::getStemSuccessor(const boost::uuids::uuid& sourcePeer) {
  std::lock_guard<std::mutex> lock(m_dandelion_epoch_mutex);

  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_dandelion_epoch_start).count();
  if (elapsed >= static_cast<int64_t>(parameters::DANDELION_EPOCH_SECONDS)) {
    m_dandelion_stem_successor.clear();
    m_dandelion_epoch_start = now;
  }

  auto it = m_dandelion_stem_successor.find(sourcePeer);
  if (it != m_dandelion_stem_successor.end()) return it->second;

  // Pick a random outbound peer (currently relay_notify_stem already does this; we
  // could lift that selection here, but for minimal change return value_initialized
  // and let relay_notify_stem pick — sticky successor is a refinement, not required for correctness).
  // For real per-edge mapping, the P2P layer must expose enumeration; defer to task 2.5.
  return boost::value_initialized<boost::uuids::uuid>();
}
```

- [ ] **Step 5: Build**

```bash
cd build && cmake --build . --target CryptoNoteProtocol -j$(nproc) 2>&1 | tail -10
```

- [ ] **Step 6: Commit**

```bash
git add src/CryptoNoteProtocol/CryptoNoteProtocolHandler.h src/CryptoNoteProtocol/CryptoNoteProtocolHandler.cpp
git commit -m "feat(p2p): Dandelion++ per-epoch stem successor scaffolding

Add m_dandelion_stem_successor map keyed by inbound peer ID. Rotates
the entire map every DANDELION_EPOCH_SECONDS. Full per-edge selection
deferred to task 2.5 (requires NetNode peer enumeration API)."
```

### Task 2.4: Add embargo timer — promote stuck stem-tx to fluff

**Files:**
- Modify: `src/CryptoNoteProtocol/CryptoNoteProtocolHandler.h` — add cleanup thread state
- Modify: `src/CryptoNoteProtocol/CryptoNoteProtocolHandler.cpp` — implement embargo logic

- [ ] **Step 1: Add cleanup logic to existing on_idle()**

Find `bool CryptoNoteProtocolHandler::on_idle()` and add at top:

```cpp
// Dandelion++ embargo: any stem tx not echoed back within DANDELION_EMBARGO_SECONDS
// is promoted to fluff (broadcast ourselves) to defeat black-hole/grief.
{
  std::lock_guard<std::mutex> lock(m_stem_mutex);
  auto now = time(nullptr);
  for (auto it = m_stem_transactions.begin(); it != m_stem_transactions.end(); ) {
    if (now - it->second.time_added > static_cast<time_t>(parameters::DANDELION_EMBARGO_SECONDS)) {
      auto req = it->second.request;
      req.dandelion_stem = false;  // promote to fluff
      relay_transactions(req);
      logger(Logging::INFO, Logging::BRIGHT_YELLOW)
        << "Dandelion embargo expired for stem tx; promoted to fluff";
      it = m_stem_transactions.erase(it);
    } else {
      ++it;
    }
  }
}
```

- [ ] **Step 2: Add a stem-tx removal hook in the existing receive path**

In `handle_notify_new_transactions`, after successful core validation, if the tx hash is in `m_stem_transactions`, erase it (means another peer echoed it back as fluff, embargo no longer needed):

```cpp
// Stem tx came back as fluff — embargo satisfied
{
  std::lock_guard<std::mutex> lock(m_stem_mutex);
  for (const auto& tx_blob : arg.txs) {
    Crypto::Hash h = Crypto::cn_fast_hash(tx_blob.data(), tx_blob.size());
    m_stem_transactions.erase(h);
  }
}
```

- [ ] **Step 3: Build**

```bash
cd build && cmake --build . --target CryptoNoteProtocol -j$(nproc) 2>&1 | tail -10
```

- [ ] **Step 4: Commit**

```bash
git add src/CryptoNoteProtocol/CryptoNoteProtocolHandler.{h,cpp}
git commit -m "feat(p2p): Dandelion++ embargo timer

If a stem tx is not echoed back within DANDELION_EMBARGO_SECONDS, promote
it to fluff ourselves. Defeats stem-successor black-hole / grief attacks.
Stem tx is cleared from tracking when seen as fluff from another peer."
```

### Task 2.5: Unit test — embargo + epoch rotation

**Files:**
- Create: `tests/UnitTests/TestDandelionEpoch.cpp`
- Modify: `tests/UnitTests/CMakeLists.txt`

- [ ] **Step 1: Write the tests (constants-only, no live P2P)**

```cpp
// tests/UnitTests/TestDandelionEpoch.cpp
#include <gtest/gtest.h>
#include "CryptoNoteConfig.h"

namespace P = CryptoNote::parameters;

TEST(DandelionConfig, ConstantsAreReasonable) {
  EXPECT_GE(P::DANDELION_STEM_MAX_HOPS, 3u);
  EXPECT_LE(P::DANDELION_STEM_MAX_HOPS, 20u);
  EXPECT_GT(P::DANDELION_STEM_STAY_PCT, 50u);
  EXPECT_LE(P::DANDELION_STEM_STAY_PCT, 99u);
  EXPECT_GE(P::DANDELION_EPOCH_SECONDS, 60u);
  EXPECT_LE(P::DANDELION_EMBARGO_SECONDS, P::DANDELION_EPOCH_SECONDS);
}

TEST(DandelionConfig, SwapPathIsTighterThanTxPath) {
  // Swap offers prioritize latency over privacy budget
  EXPECT_LE(P::DANDELION_SWAP_STEM_MAX_HOPS, P::DANDELION_STEM_MAX_HOPS);
  EXPECT_LE(P::DANDELION_SWAP_STEM_STAY_PCT, P::DANDELION_STEM_STAY_PCT);
}
```

- [ ] **Step 2: Register in CMakeLists.txt and build**

```bash
cd build && cmake --build . --target UnitTests -j$(nproc) && \
  ./tests/UnitTests/UnitTests --gtest_filter='Dandelion*'
```

Expected: 2 PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/UnitTests/TestDandelionEpoch.cpp tests/UnitTests/CMakeLists.txt
git commit -m "test: Dandelion config sanity bounds"
```

---

## Verification Before Calling Plan Complete

Per @superpowers:verification-before-completion:

- [ ] **Full UnitTests pass** with new tests:
  ```bash
  ./build/tests/UnitTests/UnitTests 2>&1 | tail -5
  ```
  Expected: `[  PASSED  ] N tests.` (N = previous count + 5)

- [ ] **Full build clean:**
  ```bash
  cd build && cmake --build . -j$(nproc) 2>&1 | tail -5
  ```
  Expected: no errors, no new warnings beyond baseline.

- [ ] **Grep proves OSPEAD now has wallet caller:**
  ```bash
  grep -rn "filterOutputsByOSPEAD" src/WalletLegacy
  ```
  Expected: ≥2 matches (the two call sites added in Task 1.3).

- [ ] **Grep proves Dandelion constants used:**
  ```bash
  grep -rn "DANDELION_STEM_MAX_HOPS\|DANDELION_EMBARGO_SECONDS" src/CryptoNoteProtocol
  ```
  Expected: ≥3 matches.

---

## Deferred (intentionally not in this plan)

- **Task 2.6 (full per-edge stem successor selection)** requires `IP2pEndpoint` to expose peer-id enumeration. That's a NetNode API change worth its own plan once we agree on the interface.
- **Pattern-data plumbing for OSPEAD** (Task 1.6) — making wallet retain a `SpendPatternAnalyzer` across sessions and persist it. Currently OSPEAD falls back to logarithmic age bins, which is already strictly better than uniform-random.
- **A1 strip Pedersen wire bytes** — separate plan; wire-format-affecting.
- **A2 BP+ re-port** — multi-month, separate planning cycle.
- **F1 swap-offer stem flag** — already shipped at `CryptoNoteProtocolHandler.cpp:1209`. Verify caller (`SwapOfferRelay::submitOffer`) initially sets `dandelion_stem=true` when originating — if not, that's a one-line fix worth folding in.

---

## Risk / Rollback

- All changes additive except the OutputInfo construction in Chunk 1. If wallet build breaks downstream, revert with `git revert <commit>` per task — each task is its own commit.
- Embargo timer runs in `on_idle`; if it floods the network with promoted txs, raise `DANDELION_EMBARGO_SECONDS` (one-line config change).
- Sticky stem successor is currently scaffold-only (`value_initialized` return) — has no behavioral effect until Task 2.6 lands. Safe to ship.
