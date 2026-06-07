# XMR Trustless-Claim — Safe-Slice Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`). Use @superpowers:test-driven-development for every task and @superpowers:verification-before-completion before any "done" claim.

**Goal:** Land the fund-safe, model-independent mechanics of the XMR swap leg — Monero address encoding, the A+B (2-term) key operations, and the Monero-RPC correctness fixes — all unit-verified, with the wrong 3-term key helper deprecated.

**Architecture:** Per the spec ([2026-06-06-xmr-trustless-claim-design.md](../specs/2026-06-06-xmr-trustless-claim-design.md), §9 authoritative). The shared Monero spend key is `A+B` and the adaptor secret is one party's spend share — so the 2-term helpers are correct and the 3-term `combineSpendKeys` is wrong. This cycle implements only pieces that are correct regardless of the full protocol wiring (which, plus refund/punish, is deferred to a cycle with live monerod verification).

**Tech Stack:** C++14, CryptoNote CMake. Reuse `Common::Base58` (block base58), `src/crypto` `keccak` + `crypto-ops` (`ge_frombytes_vartime`, `ge_add`, `ge_p3_tobytes`, `sc_add`), `Common::JsonValue`. Tests are plain `assert()`+`int main()` in `src/SwapDaemon/tests/`, declared in `src/CMakeLists.txt`. Test doubles named `Test…`, never "mock".

---

## File Structure

| Action | Path | Responsibility |
|--------|------|----------------|
| Create | `src/SwapDaemon/Monero/MoneroAddress.{h,cpp}` | `encode(spendPub, viewPub, networkByte) → address`; ed25519 point-add `sharedSpendPub(A,B)` |
| Modify | `src/SwapDaemon/Monero/AdaptorSignature.h` | mark `combineSpendKeys` `[[deprecated]]` with a comment pointing to `computeFullSpendKey` |
| Modify | `src/SwapDaemon/Monero/MoneroRpcClient.{h,cpp}` | `verifyLock` unlocked-only; `sweepSharedAddress` sync-before-sweep + per-swap wallet name + restore_height; add a `walletRpc` seam for testing |
| Create | `src/SwapDaemon/tests/TestMoneroWalletRpc.h` | `MoneroRpcClient` subclass overriding the `walletRpc` seam with canned responses + call recorder (`Test…`) |
| Create | `src/SwapDaemon/tests/test_monero_address.cpp` | address known-answer (round-trip a real Monero address) + `sharedSpendPub` point-add |
| Create | `src/SwapDaemon/tests/test_xmr_keys.cpp` | `computeSharedSpendPub`=A+B and `computeFullSpendKey`=2-term known-answers |
| Create | `src/SwapDaemon/tests/test_xmr_sweep_sequence.cpp` | sweep issues `generate_from_keys` → poll `get_height` → `sweep_all`, never sweeps before synced; verifyLock unlocked-only |
| Modify | `src/CMakeLists.txt` | add `MoneroAddress.cpp` to `SwapDaemonLib`+`SwapDaemon`; add 3 test targets |

Build rule: add `Monero/MoneroAddress.cpp` to both the `SwapDaemonLib` source list and the `SwapDaemon` executable list. Test target template:
```cmake
add_executable(test_xxx SwapDaemon/tests/test_xxx.cpp)
target_link_libraries(test_xxx PRIVATE SwapDaemonLib Common Crypto OpenSSL::SSL OpenSSL::Crypto)
```
Run: `cmake --build build --target test_xxx && ./build/src/test_xxx ; echo exit=$?`.

---

## Chunk 1: Monero address encoder

### Task 1: `MoneroAddress::encode` + `sharedSpendPub`

**Files:** Create `src/SwapDaemon/Monero/MoneroAddress.{h,cpp}`; Test `src/SwapDaemon/tests/test_monero_address.cpp`

Monero/CryptoNote address = `Base58::encode( tag_varint || spendPub(32) || viewPub(32) || keccak(tag||spend||view)[0:4] )`. For mainnet the tag is `0x12` (single byte). `Common::Base58` is the same block-base58 Monero uses.

- [ ] **Step 1: Write the failing test** — round-trip a *real* published Monero mainnet address as the known-answer fixture (decode it to get tag/spend/view, assert re-encode matches). This validates the encoder against ground truth AND that `Common::Base58` is Monero-compatible.

```cpp
// src/SwapDaemon/tests/test_monero_address.cpp
#include <cassert>
#include <string>
#include <vector>
#include <iostream>
#include "SwapDaemon/Monero/MoneroAddress.h"
#include "Common/Base58.h"
using namespace XfgSwap;

int main() {
  // A real Monero MAINNET address (95 chars, tag 0x12). TODO(impl): paste a
  // known-good mainnet address here as the fixture.
  const std::string KNOWN = "4<...real mainnet address...>";

  // Decode with the in-tree block-base58 → [tag][spend32][view32][csum4]
  std::string blob;
  bool ok = Common::Base58::decode(KNOWN, blob);
  assert(ok && blob.size() == 1 + 32 + 32 + 4 && "in-tree Base58 must decode a Monero address");
  uint8_t tag = static_cast<uint8_t>(blob[0]);
  std::vector<uint8_t> spend(blob.begin()+1,  blob.begin()+33);
  std::vector<uint8_t> view (blob.begin()+33, blob.begin()+65);

  std::string got = MoneroAddress::encode(spend, view, tag);
  assert(got == KNOWN && "re-encode must reproduce the known address (checksum+base58)");
  std::cout << "  [1] MoneroAddress round-trip OK\n";

  // sharedSpendPub(A,B) = A+B (ed25519 point add). Known-answer: A+0 == A.
  std::vector<uint8_t> identity(32,0); identity[0]=1; // not a valid point; see impl test note
  // TODO(impl): use two real points A,B and a precomputed A+B vector.
  std::cout << "=== test_monero_address: passed ===\n";
  return 0;
}
```

- [ ] **Step 2:** Add the test target to `src/CMakeLists.txt`, build → **FAIL** (MoneroAddress.h missing).
- [ ] **Step 3: Implement `MoneroAddress`**:
```cpp
// MoneroAddress.h
#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace XfgSwap {
class MoneroAddress {
public:
  // Monero/CryptoNote address: Base58(tag || spend(32) || view(32) || keccak(...)[0:4]).
  static std::string encode(const std::vector<uint8_t>& spendPub,
                            const std::vector<uint8_t>& viewPub,
                            uint8_t networkByte);
  // Shared spend pub = A + B (ed25519). Returns false on invalid point bytes.
  static bool sharedSpendPub(const std::vector<uint8_t>& A,
                             const std::vector<uint8_t>& B,
                             std::vector<uint8_t>& out);
};
} // namespace XfgSwap
```
`encode`: build the blob, compute `keccak(blob_without_checksum)` → first 4 bytes, append, `Common::Base58::encode`. `sharedSpendPub`: `ge_frombytes_vartime(A)`, `ge_frombytes_vartime(B)`, `ge_add`→`ge_p1p1_to_p3`→`ge_p3_tobytes`. (Reference `src/CryptoNoteCore/CryptoNoteBasicImpl.cpp` `getAccountAddressAsStr` for the exact base58/checksum pattern Fuego already uses.)
- [ ] **Step 4:** Build + run → **PASS**. If the round-trip fails, `Common::Base58` differs from Monero's → implement Monero block-base58 in `MoneroAddress` (note in spec §8.3).
- [ ] **Step 5: Commit** `feat(xmr): Monero address encoder + shared spend pub (A+B)`.

---

## Chunk 2: Key operations (A+B 2-term) + deprecate the wrong helper

### Task 2: Known-answer tests for the correct (2-term) helpers

**Files:** Test `src/SwapDaemon/tests/test_xmr_keys.cpp`

- [ ] **Step 1: Failing test** — `MoneroSwapProtocol::computeSharedSpendPub(A,B)` equals `MoneroAddress::sharedSpendPub(A,B)` (both = A+B), and `computeFullSpendKey(own, extracted)` equals `sc_add(own, extracted)` (2-term), for known scalars/points. Generate a keypair via `Crypto::generate_keys` for valid points.
- [ ] **Step 2:** Build → FAIL. **Step 3:** (helpers already exist — the test just pins them; if a discrepancy is found, fix the helper.) **Step 4:** PASS. **Step 5:** Commit `test(xmr): pin A+B shared pub + 2-term full spend key`.

### Task 3: Deprecate the 3-term `combineSpendKeys`

**Files:** Modify `src/SwapDaemon/Monero/AdaptorSignature.h`

- [ ] **Step 1:** Mark `combineSpendKeys` `[[deprecated("3-term a+b+adaptor is NOT the XMR swap key model; use MoneroSwapProtocol::computeFullSpendKey (a+b, adaptor secret IS a spend share). See spec 2026-06-06 §9")]]`. Add a header comment. Do NOT rewire `claimAdaptor` yet (claim wiring is deferred; `createSharedAddress` is still stubbed so claim can't run end-to-end).
- [ ] **Step 2:** Build the daemon — expect a deprecation **warning** at the existing `claimAdaptor` call site (acceptable; documents the deferred work). **Step 3:** Commit `refactor(xmr): deprecate 3-term combineSpendKeys (wrong key model)`.

---

## Chunk 3: Monero-RPC correctness fixes

### Task 4: `verifyLock` unlocked-only + `walletRpc` test seam

**Files:** Modify `MoneroRpcClient.{h,cpp}`; Create `src/SwapDaemon/tests/TestMoneroWalletRpc.h`, `src/SwapDaemon/tests/test_xmr_sweep_sequence.cpp`

- [ ] **Step 1:** Add a testable seam: `protected: virtual std::string walletRpc(const std::string& method, const std::string& params);` in `MoneroRpcClient` (wraps the existing `jsonRpc(m_walletHost, m_walletPort, …)`). Route `verifyLock`/`sweepSharedAddress`/`checkAddressBalance` wallet calls through it.
- [ ] **Step 2: Failing test** — `TestMoneroWalletRpc` overrides `walletRpc` to return a canned `get_balance` with `balance > unlocked`. Assert `verifyLock` returns **false** when only locked funds cover the amount, **true** when unlocked covers it.
- [ ] **Step 3: Implement** — `verifyLock` compares against **unlocked** only (`return unlocked >= expectedPiconero;`).
- [ ] **Step 4:** PASS. **Step 5:** Commit `fix(xmr): verifyLock requires unlocked balance + add walletRpc seam`.

### Task 5: `sweepSharedAddress` sync-before-sweep + per-swap wallet + restore height

**Files:** Modify `MoneroRpcClient.{h,cpp}`; extend `test_xmr_sweep_sequence.cpp`

- [ ] **Step 1: Failing test** — `TestMoneroWalletRpc` records the ordered list of `walletRpc` method names and serves canned responses: `get_height` returns wallet-height < daemon-height twice, then equal. Assert the recorded sequence is `generate_from_keys`, then ≥1 `get_height` polls, then `sweep_all` — and that **no `sweep_all` appears before the synced `get_height`**. Also assert `generate_from_keys` params carry a per-swap `filename` (passed in) and a non-zero `restore_height`.
- [ ] **Step 2:** Build → FAIL (current code sweeps immediately, hardcoded name, restore_height 0).
- [ ] **Step 3: Implement** — add `swapId`/`restoreHeight` params to `sweepSharedAddress`; filename `swap_sweep_<swapId>`; after `generate_from_keys`, **poll `get_height` until wallet height ≥ daemon height (bounded retries) before `sweep_all`**; fail clearly on timeout. Thread `swapId`/`restoreHeight` from `XmrChainClient` callers (claim/refund) — claim/refund stay otherwise as-is (deferred), so pass the swap's id + the XMR lock height.
- [ ] **Step 4:** PASS. **Step 5:** Commit `fix(xmr): sweep waits for wallet sync; per-swap wallet name + restore height`.

---

## Chunk 4: build + verify

### Task 6: Full build + regression
- [ ] Build `SwapDaemon` + all new + existing swap tests: `cmake --build build -j`.
- [ ] Run: `for t in test_monero_address test_xmr_keys test_xmr_sweep_sequence test_swap_hashlock test_sol_htlc_address test_adaptor_roundtrip test_price_oracle_arb; do ./build/src/$t; echo "$t exit=$?"; done` — all `exit=0`.
- [ ] @superpowers:verification-before-completion. Commit any fixups.

## Deferred (NOT in this plan — see spec §2/§9)
Full `createSharedAddress` wiring into the live flow, the `T=S_m` XFG-adaptor binding, the claim-extraction path, refund/punish, the negotiation-layer view-secret-share field, and live monerod/regtest e2e.

## Dependency graph
```
Task 1 (MoneroAddress) ─┐
Task 2 (key tests) ──────┼── independent
Task 3 (deprecate) ──────┘
Task 4 (verifyLock + seam) ── Task 5 (sweep sequence) [needs the seam]
Task 6 (build+verify) ── after all
```
