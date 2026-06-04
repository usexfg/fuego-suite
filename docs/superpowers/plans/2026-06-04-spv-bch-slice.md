# SPV Light-Client Foundation — BCH Proving Slice — Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking. Use @superpowers:test-driven-development for every task and @superpowers:verification-before-completion before any "done" claim.

**Goal:** Build a reusable UTXO `ISpvClient` foundation and prove it end-to-end on BCH via an Electrum SPV backend, so cross-chain lock verification no longer requires a BCH full node — with the existing `BchRpcClient` retained as a differential-test oracle.

**Architecture:** Approach A from [ADR-0001](../../design/2026-06-04-spv-light-client-integration-adr.md): a new `ISpvClient` (UTXO-shaped, no script semantics) is injected *optionally* into `BchChainClient`. When present, `verifyLock` and on-chain secret extraction route through SPV (header-chain sync + Merkle-proof verification + multi-server cross-check); when absent, the RPC path is used unchanged. An additive `ADAPTOR_WAITING_SPV_CONFIRMATIONS` state tracks confirmation depth.

**Tech Stack:** C++14, CryptoNote CMake build. Reuse in-tree `Common::JsonValue` (JSON), POSIX sockets (mirror `BchRpcClient::httpPost`), `BchHtlcScript` hashing/hex helpers, OpenSSL, `secp256k1`. Tests are plain `assert()`+`int main()` executables declared in `src/CMakeLists.txt` (no gtest).

**Spec:** [2026-06-04-spv-bch-slice-design.md](../specs/2026-06-04-spv-bch-slice-design.md). Honor the user rule: test doubles are named `Test…`, never "mock".

---

## File Structure

| Action | Path | Responsibility |
|--------|------|----------------|
| Create | `src/SwapDaemon/Spv/ISpvClient.h` | Interface + `SpvTxInclusion`/`SpvSpend` result structs |
| Create | `src/SwapDaemon/Spv/SpvMerkle.{h,cpp}` | Merkle-branch fold + verify (pure) |
| Create | `src/SwapDaemon/Spv/SpvHeader.{h,cpp}` | 80-byte header parse, hash, nBits→target, PoW check, work |
| Create | `src/SwapDaemon/Spv/SpvHeaderStore.{h,cpp}` | Header-chain validate/persist/select/reorg, checkpoint anchor |
| Create | `src/SwapDaemon/Spv/ElectrumConnection.{h,cpp}` | One server: TCP + newline-delimited JSON-RPC |
| Create | `src/SwapDaemon/Spv/ElectrumSpvClient.{h,cpp}` | `ISpvClient` impl; multi-server cross-check; orchestration |
| Modify | `src/SwapDaemon/ChainClientResult.h` | Additive `pending`/`confirmations`/`requiredConfirmations` + `pendingConfs()` |
| Modify | `src/SwapDaemon/BitcoinCash/HtlcScript.{h,cpp}` | `parseClaimPreimage()`, `redeemScriptToP2shScriptPubKey()` |
| Modify | `src/SwapDaemon/BitcoinCash/BchChainClient.{h,cpp}` | Optional `ISpvClient`; SPV `verifyLock`; `extractSecret()` |
| Modify | `src/SwapDaemon/SwapTypes.{h,cpp}` | `ADAPTOR_WAITING_SPV_CONFIRMATIONS` + string |
| Modify | `src/SwapDaemon/SwapStateMachine.cpp` | `isValidTransition` for the new state |
| Modify | `src/SwapDaemon/SwapDaemon.{h,cpp}` + `ChainClientConfig.cpp` | config fields/parse/validate; registration; WAITING branch; Bob extract poll |
| Modify | `src/CMakeLists.txt` | add `Spv/*.cpp` to `SwapDaemonLib` (~:268) + `SwapDaemon` exe (~:392); add 4 test targets (~:298) |
| Create | `src/SwapDaemon/tests/test_spv_merkle.cpp` | Known-answer Merkle |
| Create | `src/SwapDaemon/tests/test_spv_headers.cpp` | Header link/PoW/reorg/checkpoint |
| Create | `src/SwapDaemon/tests/test_spv_extract_secret.cpp` | Preimage parse + SHA256 gate |
| Create | `src/SwapDaemon/tests/test_spv_electrum.cpp` | Electrum connection + syncHeaders + verifyTxInclusion + findSpend + cross-check (Tasks 5–9) |
| Create | `src/SwapDaemon/tests/test_spv_vs_rpc_difftest.cpp` | Differential `verifyLock`: SPV (`TestElectrumServer`) vs RPC (`TestBchRpcServer`), same canned chain state |
| Create | `src/SwapDaemon/tests/TestElectrumServer.h` | In-process server replaying canned Electrum responses (named `Test…`, not "mock") |
| Create | `src/SwapDaemon/tests/TestBchRpcServer.h` | In-process canned HTTP JSON-RPC server so `BchRpcClient` runs against fixtures in CI (named `Test…`) |

**Build wiring rule (applies whenever a new `Spv/*.cpp` is added):** add the path to the `SwapDaemonLib` source list *and* the `SwapDaemon` executable source list in `src/CMakeLists.txt`. Each new test gets:
```cmake
add_executable(test_spv_xxx SwapDaemon/tests/test_spv_xxx.cpp)
target_link_libraries(test_spv_xxx PRIVATE SwapDaemonLib Common Crypto OpenSSL::SSL OpenSSL::Crypto)
```
Build+run a single test: `cmake --build build --target test_spv_xxx && ./build/src/test_spv_xxx ; echo "exit=$?"` (exit 0 = pass; `assert` aborts on fail).

---

## Chunk 1: SPV crypto primitives (no network)

Security-critical and fully deterministic. Implement with @superpowers:test-driven-development. Every function here is pure → known-answer tests are the contract.

### Task 1: `ISpvClient` interface + result structs

**Files:** Create `src/SwapDaemon/Spv/ISpvClient.h`

- [ ] **Step 1: Write the header (no test — pure interface; compilation is the check)**

```cpp
// src/SwapDaemon/Spv/ISpvClient.h
#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace XfgSwap {

struct SpvTxInclusion {
  bool included = false;        // tx found in a block (not just mempool)
  uint64_t blockHeight = 0;
  uint32_t depth = 0;           // tipHeight - blockHeight + 1
  bool merkleVerified = false;  // Merkle proof checked against our header store
};

struct SpvSpend {
  bool spent = false;
  std::string spendingTxid;
  std::vector<uint8_t> rawSpendingTx;  // for scriptSig parsing
  SpvTxInclusion inclusion;
};

class ISpvClient {
public:
  virtual ~ISpvClient() = default;
  virtual std::string protocolName() const = 0;               // "electrum"
  virtual bool syncHeaders() = 0;                             // advance + cross-check to tip
  virtual bool getTipHeight(uint64_t& height) = 0;
  virtual bool verifyTxInclusion(const std::string& txid, SpvTxInclusion& out) = 0;
  // Derives the watched scripthash from the funding output internally
  // (getRawTx(txid) -> output[vout].scriptPubKey -> scripthash -> get_history).
  virtual bool findSpend(const std::string& txid, uint32_t vout, SpvSpend& out) = 0;
  virtual bool getRawTx(const std::string& txid, std::vector<uint8_t>& rawTx) = 0;
};

} // namespace XfgSwap
```

- [ ] **Step 2: Commit** — `git add src/SwapDaemon/Spv/ISpvClient.h && git commit -m "feat(spv): add ISpvClient interface and result structs"`

### Task 2: Merkle proof verification (`SpvMerkle`)

**Files:** Create `src/SwapDaemon/Spv/SpvMerkle.{h,cpp}`; Test `src/SwapDaemon/tests/test_spv_merkle.cpp`

Electrum `get_merkle` returns `merkle[]` (hex hashes, display/big-endian) + `pos`. Fold rule: start from txid (internal little-endian bytes); for each branch hash, if `pos & 1` then `cur = dsha256(branch || cur)` else `cur = dsha256(cur || branch)`; `pos >>= 1`. Result compared to the block's `merkleRoot` (internal byte order).

- [ ] **Step 1: Write the failing test** (known-answer from a real BCH block — fill the fixture from a block explorer's `get_merkle` during implementation; values below are placeholders marked TODO)

```cpp
// src/SwapDaemon/tests/test_spv_merkle.cpp
#include <cassert>
#include <string>
#include <vector>
#include "SwapDaemon/Spv/SpvMerkle.h"
using namespace XfgSwap;
int main() {
  // TODO(impl): replace with a real BCH block: txid, merkle branch (display order),
  // pos, and that block header's merkleRoot (display order).
  std::string txidDisplay   = "<txid hex>";
  std::vector<std::string> branchDisplay = { "<h0>", "<h1>", "<h2>" };
  uint32_t pos = 5;
  std::string merkleRootDisplay = "<root hex>";

  std::string computed = SpvMerkle::computeRootHexDisplay(txidDisplay, branchDisplay, pos);
  assert(computed == merkleRootDisplay && "Merkle root must match known block");

  // Tamper: flip one branch hash -> must NOT match.
  auto bad = branchDisplay; bad[1][0] = (bad[1][0]=='a'?'b':'a');
  assert(SpvMerkle::computeRootHexDisplay(txidDisplay, bad, pos) != merkleRootDisplay);
  return 0;
}
```

- [ ] **Step 2: Add the test target to `src/CMakeLists.txt`, build, verify it FAILS** (link error / assert) — `cmake --build build --target test_spv_merkle` → expect FAIL.

- [ ] **Step 3: Implement `SpvMerkle`** (reuse `BchHtlcScript::doubleSha256`, `hexToBytes`, `bytesToHex`)

```cpp
// src/SwapDaemon/Spv/SpvMerkle.h
#pragma once
#include <string>
#include <vector>
#include <cstdint>
namespace XfgSwap {
class SpvMerkle {
public:
  // Inputs in display (big-endian) hex; internal byte-reversal handled inside.
  static std::string computeRootHexDisplay(const std::string& txidDisplay,
      const std::vector<std::string>& branchDisplay, uint32_t pos);
};
} // namespace XfgSwap
```
```cpp
// src/SwapDaemon/Spv/SpvMerkle.cpp
#include "SpvMerkle.h"
#include "../BitcoinCash/HtlcScript.h"
#include <algorithm>
namespace XfgSwap {
static std::vector<uint8_t> rev(std::vector<uint8_t> v){ std::reverse(v.begin(),v.end()); return v; }
std::string SpvMerkle::computeRootHexDisplay(const std::string& txidDisplay,
    const std::vector<std::string>& branchDisplay, uint32_t pos) {
  std::vector<uint8_t> cur = rev(BchHtlcScript::hexToBytes(txidDisplay)); // -> internal LE
  for (const auto& bh : branchDisplay) {
    std::vector<uint8_t> b = rev(BchHtlcScript::hexToBytes(bh));
    std::vector<uint8_t> cat;
    if (pos & 1u) { cat = b; cat.insert(cat.end(), cur.begin(), cur.end()); }
    else          { cat = cur; cat.insert(cat.end(), b.begin(), b.end()); }
    cur = BchHtlcScript::doubleSha256(cat);
    pos >>= 1;
  }
  return BchHtlcScript::bytesToHex(rev(cur)); // back to display
}
} // namespace XfgSwap
```

- [ ] **Step 4: Build + run, verify PASS** — `cmake --build build --target test_spv_merkle && ./build/src/test_spv_merkle ; echo exit=$?` → `exit=0`.
- [ ] **Step 5: Commit** — `git commit -am "feat(spv): Merkle branch verification with known-answer test"`

### Task 3: Header parse + PoW (`SpvHeader`)

**Files:** Create `src/SwapDaemon/Spv/SpvHeader.{h,cpp}`; Test in `src/SwapDaemon/tests/test_spv_headers.cpp`

- [ ] **Step 1: Write failing test** — parse a real 80-byte BCH header (hex), assert `hashDisplay()` equals the known block hash; assert `prevHashDisplay`/`merkleRootDisplay` match; assert `meetsPoW()` true for the real header and false after corrupting the nonce.

```cpp
// (excerpt) src/SwapDaemon/tests/test_spv_headers.cpp
SpvHeader h = SpvHeader::parse(BchHtlcScript::hexToBytes("<80-byte header hex>"));
assert(h.hashDisplay() == "<known block hash>");
assert(h.meetsPoW());
```

- [ ] **Step 2: Build, verify FAIL.**
- [ ] **Step 3: Implement `SpvHeader`** — fields `version,prevHash[32],merkleRoot[32],time,bits,nonce`; `hash = dsha256(80 bytes)`; `nBitsToTarget(bits)` (compact: `exp=bits>>24; mant=bits&0xffffff`; target = mant << (8*(exp-3))) as 32-byte big-endian; `meetsPoW()` = `hashBE <= targetBE` (32-byte big-endian compare); `work()` ≈ `2^256/(target+1)` (return as `long double` for chain selection — **TODO:** note precision; acceptable for short near-tip reorgs).

```cpp
// src/SwapDaemon/Spv/SpvHeader.h  (signatures)
struct SpvHeader {
  uint32_t version=0, time=0, bits=0, nonce=0;
  std::vector<uint8_t> prevHash, merkleRoot;     // internal LE (32)
  static SpvHeader parse(const std::vector<uint8_t>& raw80);
  std::vector<uint8_t> serialize() const;        // 80 bytes
  std::vector<uint8_t> hash() const;             // dsha256, internal LE
  std::string hashDisplay() const, prevHashDisplay() const, merkleRootDisplay() const;
  static std::vector<uint8_t> nBitsToTargetBE(uint32_t bits); // 32 bytes BE
  bool meetsPoW() const;                          // hashBE <= targetBE
  long double work() const;                       // ~2^256/(target+1)
};
```

- [ ] **Step 4: Build + run PASS. Step 5: Commit** `feat(spv): block header parse, nBits→target, PoW check`.

### Task 4: Header store — link, checkpoint anchor, selection, reorg

**Files:** Create `src/SwapDaemon/Spv/SpvHeaderStore.{h,cpp}`; extend `test_spv_headers.cpp`

- [ ] **Step 1: Write failing tests** (synthetic chain so PoW targets are easy):
  - `addHeader` rejects a header whose `prevHash != tip.hash()`.
  - rejects a header below the configured checkpoint height, and any chain not descending from the checkpoint hash.
  - given two competing branches from a fork, `bestTipHeight()` follows the **greater cumulative work**.
  - after a heavier branch arrives, `depthOf(txHeight)` recomputes against the new tip.

- [ ] **Step 2: Build, verify FAIL.**
- [ ] **Step 3: Implement `SpvHeaderStore`** — in-memory `map<height, vector<SpvHeader>>` + best-chain index; validate link + `meetsPoW()` on insert; checkpoint `{height,hashDisplay}` from config: reject headers at/below checkpoint that disagree, require ancestry; chain selection by accumulated `work()`; `reorg` walks back to fork on `prevHash` mismatch and switches if heavier. Persistence: **TODO(plan §9.6)** — start in-memory + a flat append-only file of best-chain headers under the daemon data dir; reload on start. Expose: `bool anchor(checkpointHeight, checkpointHashDisplay)`, `bool addHeader(const SpvHeader&)`, `bool bestTip(uint64_t& h, std::vector<uint8_t>& hash)`, `bool merkleRootAt(uint64_t height, std::vector<uint8_t>& rootLE)`, `uint32_t depthOf(uint64_t height)`.

- [ ] **Step 4: Build + run PASS. Step 5: Commit** `feat(spv): header store with checkpoint anchor, work-based selection, reorg`.

> **Chunk 1 review gate:** run inline self-review (interface minimalism, Merkle byte-order correctness, PoW compare direction, reorg safety). All four tests green before Chunk 2.

---

## Chunk 2: Electrum client (network I/O)

Mirror `BchRpcClient::httpPost` for the socket, but Electrum is **newline-delimited JSON-RPC over raw TCP** (not HTTP). Parse with `Common::JsonValue`. Drive tests with `TestElectrumServer` (in-process; no real network).

### Task 5: `TestElectrumServer` + `ElectrumConnection`

**Files:** Create `src/SwapDaemon/Spv/ElectrumConnection.{h,cpp}`, `src/SwapDaemon/tests/TestElectrumServer.h`

- [ ] **Step 1: Write `TestElectrumServer.h`** — binds `127.0.0.1:0` (ephemeral), accepts one connection on a thread, reads newline-delimited requests, replies with a caller-registered canned response keyed by JSON `"method"`. Named `Test…` per the user rule.
- [ ] **Step 2: Write failing test** (in `test_spv_vs_rpc_difftest.cpp` scaffold or a small `test_spv_electrum.cpp`): start `TestElectrumServer` with a canned `server.version` reply; `ElectrumConnection::call("server.version", "[]")` returns the `result`.
- [ ] **Step 3: Build, verify FAIL.**
- [ ] **Step 4: Implement `ElectrumConnection`** — `connect(host,port)` (POSIX socket like `BchRpcClient::httpPost`, no HTTP headers), `std::string call(method, paramsJson)` writes `{"id":N,"method":...,"params":...}\n`, reads until `\n`, returns the `result` substring via `Common::JsonValue`. Per-call id counter; connect/read timeouts (`setsockopt SO_RCVTIMEO`). **No TLS** (plaintext; see spec §7).
- [ ] **Step 5: Build + run PASS. Step 6: Commit** `feat(spv): Electrum TCP JSON-RPC connection + TestElectrumServer`.

### Task 6: `ElectrumSpvClient::syncHeaders` (single server) + getTipHeight

**Files:** Create `src/SwapDaemon/Spv/ElectrumSpvClient.{h,cpp}`

- [ ] **Step 1: Failing test** — `TestElectrumServer` canned `blockchain.headers.subscribe` (tip) + `blockchain.block.header(h)` for a synthetic chain anchored at a checkpoint; `syncHeaders()` populates the store; `getTipHeight()` returns the tip.
- [ ] **Step 2: FAIL. Step 3: Implement** — ctor takes `(servers[], minServers, checkpointHeight, checkpointHashDisplay)`; owns an `SpvHeaderStore` + one `ElectrumConnection` per server. `syncHeaders()`: subscribe for tip, fetch headers forward from `max(store tip, checkpoint)` via `blockchain.block.headers(start,count)`, `SpvHeader::parse` + `store.addHeader`. (Cross-check added in Task 9.)
- [ ] **Step 4: PASS. Step 5: Commit** `feat(spv): ElectrumSpvClient header sync`.

### Task 7: `verifyTxInclusion`

- [ ] **Step 1: Failing test** — canned `blockchain.transaction.get_merkle(txid)`; with the store synced, `verifyTxInclusion(txid)` returns `{included, blockHeight, depth, merkleVerified=true}`; a proof that folds to the wrong root → `merkleVerified=false`/`included=false`.
- [ ] **Step 2: FAIL. Step 3: Implement** — call `get_merkle`, get `{block_height, merkle[], pos}`, `store.merkleRootAt(block_height)`, `SpvMerkle::computeRootHexDisplay(...)`, compare; `depth = store.depthOf(block_height)`. Never trust a server-supplied root.
- [ ] **Step 4: PASS. Step 5: Commit** `feat(spv): Merkle-verified tx inclusion`.

### Task 8: `getRawTx` + `findSpend`

- [ ] **Step 1: Failing test** — canned `blockchain.transaction.get(txid)` (raw hex) and `blockchain.scripthash.get_history`; `findSpend(fundingTxid,0)` finds the spending tx that consumes `fundingTxid:0`, returns its raw bytes + Merkle-verified inclusion.
- [ ] **Step 2: FAIL. Step 3: Implement** — `getRawTx`: `blockchain.transaction.get`. `findSpend`: `getRawTx(fundingTxid)` → read `output[vout].scriptPubKey` → Electrum scripthash = `reverse(sha256(scriptPubKey))` (**single** SHA256 per Electrum spec, then byte-reversed — not double-SHA256) → `get_history` → for each candidate `getRawTx` and scan inputs for the outpoint `fundingTxid:vout` → on match set `spendingTxid`+`rawSpendingTx` and `verifyTxInclusion(spendingTxid)`. Minimal tx parser for inputs/outputs: **TODO** add `SpvTxParse` helper (varint, input outpoints, output scripts) — pure, add a tiny known-answer test.
- [ ] **Step 4: PASS. Step 5: Commit** `feat(spv): raw tx fetch + spend detection`.

### Task 9: Multi-server cross-check (eclipse mitigation)

- [ ] **Step 1: Failing test** — two `TestElectrumServer`s: (a) both agree on header at `tip-k` → `syncHeaders()` advances; (b) one returns a divergent buried header → `syncHeaders()` refuses to advance and `verifyTxInclusion` does **not** report verified (no false positive).
- [ ] **Step 2: FAIL. Step 3: Implement** — require ≥`minServers` reachable; fetch the header hash at `tip-k` from each; if any buried-header disagreement, log eclipse-risk and return false from `syncHeaders()` (stay put). Proofs only trusted against the cross-checked store.
- [ ] **Step 4: PASS. Step 5: Commit** `feat(spv): multi-server header cross-check`.

> **Chunk 2 review gate:** inline self-review — no path emits "verified" without (store synced ∧ quorum met ∧ Merkle folds to stored root). Confirm with the Task 9 negative test.

---

## Chunk 3: Result type + state machine

### Task 10: `ChainClientResult` pending fields

**Files:** Modify `src/SwapDaemon/ChainClientResult.h`

- [ ] **Step 1: Failing test** (`test_spv_extract_secret.cpp` scaffold or inline): `ChainClientResult::pendingConfs("tx",2,6)` → `!success && pending && confirmations==2 && requiredConfirmations==6`; existing `ok()/fail()` unchanged (`!pending`).
- [ ] **Step 2: FAIL. Step 3: Implement** — add `bool pending=false; uint32_t confirmations=0; uint32_t requiredConfirmations=0;` and `static ChainClientResult pendingConfs(const std::string& txId,uint32_t c,uint32_t req){ ChainClientResult r; r.txId=txId; r.pending=true; r.confirmations=c; r.requiredConfirmations=req; return r; }`. Keep existing factories (set new fields to defaults via aggregate updates).
- [ ] **Step 4: PASS. Step 5: Commit** `feat(swap): ChainClientResult pending-confirmations signal`.

### Task 11: `ADAPTOR_WAITING_SPV_CONFIRMATIONS`

**Files:** Modify `src/SwapDaemon/SwapTypes.{h,cpp}`, `src/SwapDaemon/SwapStateMachine.cpp`

- [ ] **Step 1: Failing test** — `swapStateToString(ADAPTOR_WAITING_SPV_CONFIRMATIONS)` non-empty; a `SwapStateMachine` in `ADAPTOR_PRESIGS_READY` allows `transition(WAITING_SPV_CONFIRMATIONS)`, then `WAITING→CTR_LOCKED`, `WAITING→PRESIGS_READY`, `WAITING→FAILED`; an illegal jump (e.g. `WAITING→XFG_SPENT`) is rejected; serialize→deserialize round-trips the new state.
- [ ] **Step 2: FAIL. Step 3: Implement** — add enum `ADAPTOR_WAITING_SPV_CONFIRMATIONS = 17`; `swapStateToString` case; extend `isValidTransition` with the four edges above; ensure (de)serialization covers value 17.
- [ ] **Step 4: PASS. Step 5: Commit** `feat(swap): add ADAPTOR_WAITING_SPV_CONFIRMATIONS state`.

---

## Chunk 4: BCH integration

### Task 12: Resolve open-question #1 (hashlock semantic) — SPIKE, gates Task 14

**Files:** read `src/SwapDaemon/BitcoinCash/BchRpcClient.cpp` (`lockHtlc`, `claim`), `HtlcScript.cpp` (`createRedeemScript`, `createClaimScriptSig`).

- [ ] **Step 1:** Determine what `lockHtlc` actually commits as the `OP_SHA256 <hashlock>` value and what `claim` pushes as the preimage. `BchChainClient::lock` passes `podToHex(adaptorPoint)` (T=t·G, 32B) as `hashLockSha256Hex`; `claim` reveals `adaptorSecret` (t).
- [ ] **Step 2:** Decide the equality check for extraction and record it in the spec §9.1:
  - **If** the redeem script commits `H = SHA256(t)` and lock currently mis-passes `T` → that is a **pre-existing bug**; file it (do not fix in this slice unless trivial), and make `extractSecret` verify `SHA256(preimage) == committedHashlock` where `committedHashlock` is whatever the redeem script in `chainState` actually contains.
  - **Else if** the script commits `T` directly and "preimage" is checked another way → extraction verifies against the redeem script's committed bytes, not an assumed `SHA256(t)`.
  - **Canonical rule for Task 14:** parse the committed hashlock **out of the redeem script in `params.chainState`** and require `SHA256(preimage) == thatHashlock`. This is correct regardless of which value lock commits, and surfaces the mismatch as a real verification failure rather than a wrong assumption.
- [ ] **Step 3: Commit** the spec update — `docs: resolve SPV secret-extraction hashlock semantics`.

### Task 13: BCH SPV `verifyLock` (inject `ISpvClient` + bind to our HTLC)

**Files:** Modify `BchChainClient.{h,cpp}`; `HtlcScript.{h,cpp}` (`redeemScriptToP2shScriptPubKey`); Test `test_spv_vs_rpc_difftest.cpp`

- [ ] **Step 1: Failing differential test** — `TestElectrumServer` (canned headers + `get_merkle` + `get` for a funded HTLC) on the SPV side and `TestBchRpcServer` (canned `getrawtransaction`/`gettransaction`/`getblockcount` for the *same* chain state) on the RPC side; assert `BchChainClient(spv).verifyLock` and a plain `BchRpcClient::verifyLock` agree for: (a) confirmed ≥minConfs → `success`; (b) 1-conf shallow → `pending` (SPV) / not-yet (RPC `minConfs`); (c) absent → `fail`; (d) wrong amount → `fail`. (An opt-in live tier runs both against a regtest node.)
- [ ] **Step 2: FAIL. Step 3: Implement**:
  - `HtlcScript::redeemScriptToP2shScriptPubKey(redeem)` = `buildP2shScriptPubKey(hash160(redeem))`.
  - `BchChainClient` ctor gains `std::unique_ptr<ISpvClient> spv=nullptr, uint32_t spvMinConfs=6`.
  - `verifyLock`: if `m_spv`: `m_spv->syncHeaders()`; `verifyTxInclusion(ctrLockTxId)`; if `!included` → `fail`; `getRawTx(ctrLockTxId)`, confirm some output `scriptPubKey == redeemScriptToP2shScriptPubKey(chainState)` and `value >= ctrAmount` (else `fail`); if `depth >= m_spvMinConfs && merkleVerified` → `ok`; else `pendingConfs(ctrLockTxId, depth, m_spvMinConfs)`. Else current `m_rpc->verifyLock` path.
- [ ] **Step 4: PASS. Step 5: Commit** `feat(bch): SPV-backed verifyLock with RPC differential parity`.

### Task 14: On-chain secret extraction

**Files:** Modify `HtlcScript.{h,cpp}` (`parseClaimPreimage`), `BchChainClient.{h,cpp}` (`extractSecret`); Test `test_spv_extract_secret.cpp`

- [ ] **Step 1: Failing test** — build a claim tx scriptSig via `BchHtlcScript::createClaimScriptSig(sig, preimage, redeem)`, wrap as a raw tx, run `parseClaimPreimage(rawTx, vinIndex)` → recovers `preimage`; `extractSecret` returns it and enforces the Task-12 rule `SHA256(preimage)==committedHashlock(redeem)`; a refund scriptSig (`<sig> OP_FALSE <redeem>`) → no preimage.
- [ ] **Step 2: FAIL. Step 3: Implement** — `parseClaimPreimage`: locate the input spending the HTLC outpoint, parse its scriptSig pushes; claim path is `<sig> <preimage> OP_TRUE <redeem>` → return the second push when followed by `OP_TRUE`+redeem; refund path returns empty. `BchChainClient::extractSecret(params, secretOut)`: `m_spv->findSpend(ctrLockTxId, 0)`; if `spent && inclusion.merkleVerified` → `parseClaimPreimage` → check SHA256 gate vs hashlock parsed from `chainState` redeem script → set `secretOut`. Returns false (not crash) on refund/malformed.
- [ ] **Step 4: PASS. Step 5: Commit** `feat(bch): SPV on-chain adaptor-secret extraction`.

---

## Chunk 5: Config, registration, daemon wiring

### Task 15: Config fields + parse + validation

**Files:** Modify `src/SwapDaemon/SwapDaemon.h` (`ChainClientConfig`), `src/SwapDaemon/ChainClientConfig.cpp`

- [ ] **Step 1: Failing test** — parse a JSON snippet with `bch_spv_enabled/bch_electrum_servers/bch_spv_min_confs/bch_spv_min_servers/bch_spv_checkpoint_height/bch_spv_checkpoint_hash`; assert fields populate; assert validation fails (enabled but servers.size() < min_servers).
- [ ] **Step 2: FAIL. Step 3: Implement** — add fields (`bool bchSpvEnabled=false; std::vector<std::string> bchElectrumServers; uint32_t bchSpvMinConfs=6; uint32_t bchSpvMinServers=2; uint64_t bchSpvCheckpointHeight=0; std::string bchSpvCheckpointHash;`). Add a `jsonGetStrArray(json,key)` helper (parse a `[ "a","b" ]` via `Common::JsonValue`) since the existing string-based helpers don't do arrays. Add a `bool validate(std::string& err)` rule for the quorum/checkpoint guard.
- [ ] **Step 4: PASS. Step 5: Commit** `feat(swap): bch_spv_* config parsing + validation`.

### Task 16: BCH registration wiring

**Files:** Modify `src/SwapDaemon/SwapDaemon.cpp` (BCH constructor block)

- [ ] **Step 1:** In the BCH registration block, after building `BchRpcClient`: if `chainCfg.bchSpvEnabled` and `validate()` passes, build `std::make_unique<ElectrumSpvClient>(servers, minServers, checkpointHeight, checkpointHash)` and pass into `BchChainClient(std::move(rpc), wif, std::move(spv), bchSpvMinConfs)`; else construct `BchChainClient(std::move(rpc), wif)` (RPC-only, unchanged). Log which mode.
- [ ] **Step 2:** Build the full `SwapDaemon` target; verify it links (new `Spv/*.cpp` already in both targets per the build rule).
- [ ] **Step 3: Commit** `feat(swap): register BCH with optional Electrum SPV backend`.

### Task 17: Driver — WAITING branch + Bob extract poll

**Files:** Modify `src/SwapDaemon/SwapDaemon.cpp` (`handlePreSigsReady` Alice path ~:697; `handleCtrLocked` Bob path ~:708)

- [ ] **Step 1:** In `handlePreSigsReady` Alice branch, replace the binary success/retry with: `result.success` → `CTR_LOCKED` (as today); `result.pending` → `sm.transition(ADAPTOR_WAITING_SPV_CONFIRMATIONS)` + log `confirmations/requiredConfirmations` + save; else retry. Add a handler for `ADAPTOR_WAITING_SPV_CONFIRMATIONS` that re-calls `verifyLock` and advances to `CTR_LOCKED` (or reverts to `PRESIGS_READY` on reorg `fail`). RPC pairs never set `pending` → unchanged.
- [ ] **Step 2:** In `handleCtrLocked` Bob branch (currently just "waiting"), if `m_spv`-backed: call `extractSecret`; on success set `params.adaptorSecret` and `transition(ADAPTOR_SECRET_REVEALED)`. Keep the cooperative P2P path intact as the primary; SPV is the fallback that removes the liveness dependency.
- [ ] **Step 3:** Build; run the full existing test suite to confirm no regression (`for t in test_adaptor_roundtrip test_price_oracle_arb XmrClaimAdaptorTests; do ./build/src/$t; done`).
- [ ] **Step 4: Commit** `feat(swap): SPV confirmation-wait state and Bob on-chain secret fallback`.

### Task 18: Full build + slice verification

- [ ] **Step 1:** Clean build: `cmake --build build -j` → compiles.
- [ ] **Step 2:** Run all SPV tests + regressions; expected all `exit=0`:
  `for t in test_spv_merkle test_spv_headers test_spv_extract_secret test_spv_vs_rpc_difftest test_adaptor_roundtrip test_price_oracle_arb; do echo "== $t =="; ./build/src/$t; echo exit=$?; done`
- [ ] **Step 3:** Manual: start `SwapDaemon` with `bch_spv_enabled:false` (RPC-only, regression) and with `true` + two local Electrum servers (or `TestElectrumServer` harness) — confirm logs show SPV mode and a testnet `verifyLock` reaches `CTR_LOCKED`.
- [ ] **Step 4:** Use @superpowers:verification-before-completion. Commit any fixups. Final commit `test(spv): full slice build + differential verification green`.

---

## Dependency Graph
```
Chunk 1 (Tasks 1–4: primitives)        ── no deps
Chunk 2 (Tasks 5–9: electrum)          ── depends on Chunk 1
Chunk 3 (Tasks 10–11: result + state)  ── independent of 1–2 (can parallel)
Chunk 4 (Tasks 12–14: BCH)             ── depends on Chunks 1,2,3
Chunk 5 (Tasks 15–18: wiring)          ── depends on Chunk 4
```
Task 12 (hashlock spike) gates Task 14 only. Chunk 3 may be built in parallel with Chunks 1–2.

## Open items carried from spec §9
1. **Hashlock semantic** — resolved procedurally in Task 12 (parse committed hashlock from the redeem script; `SHA256(preimage)==that`).
2. **DAA recomputation** — deferred; security boundary documented (Task 3 `work()` TODO).
3. **TLS** — deferred; plaintext TCP (Task 5).
4. **`vout=0`** — assumed in Tasks 13/14; confirm during Task 12/13 against `lockHtlc` output ordering.
5. **Test harness** — plain `assert()`+`main()`, declared in `src/CMakeLists.txt` (confirmed).
6. **Header persistence** — Task 4 TODO: in-memory + append-only file under daemon data dir.
