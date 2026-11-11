# xfgCdswaps Remaining Fixes Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the genuinely-open items on `fix/xfgCdswaps-criticals` so the atomic-swap subsystem can complete every supported pair on testnet.

**Plan version:** 2 (2026-04-16). Supersedes v1 after codebase audit confirmed the ETH/BCH/secp256k1/ChainClientConfig/FAILED-transition/XMR-address-validator work had already landed on this branch. Only XMR adaptor combine, web-bridge SRI, gift-secret heuristic, TUI TODO cleanup, and testnet validation remain.

**Architecture context:** Daemon-side C++ in `src/SwapDaemon/` with per-chain adapters (`Ethereum/`, `BitcoinCash/`, `Monero/`, `Solana/`). The TUI is Go (Bubble Tea) under `swapxfg/`. External crypto: vendored `external/secp256k1/` (already linked), OpenSSL, libsodium (implicit via Monero tooling).

**Tech stack:** C++17, CMake, libsecp256k1, OpenSSL, Go 1.25, Monero `monero-wallet-rpc` (adaptor path).

**Workspace paths in this plan use `/home/ar/fuego/…`.** (v1 referenced `/Users/aejt/fuego/…`; that was incorrect for this machine.)

---

## Audit: what v1 called out that is already done

| v1 phase | Status on this branch | Evidence |
|---|---|---|
| A — vendor secp256k1 | ✅ | `.gitmodules`, `external/secp256k1/`, linked in SwapDaemon CMake |
| A2 — `Secp256k1Signer` wrapper | ✅ | `src/SwapDaemon/Crypto/Secp256k1Signer.{h,cpp}` |
| B1 — minimal RLP encoder | ✅ | `src/SwapDaemon/Crypto/RlpEncoder.{h,cpp}` |
| B2 — EIP-155 sign-and-send | ✅ | `EthRpcClient::buildSignedTx` at `EthRpcClient.cpp:389-440` |
| B3 — ETH refund → FAILED | ✅ | `SwapDaemon.cpp:953,956`; universal rule at `SwapStateMachine.cpp:60-61` |
| C1 — BIP143 sighash | ✅ | `src/SwapDaemon/Crypto/Bip143Sighash.{h,cpp}` |
| C2 — BCH lock/claim/refund real signing | ✅ | `BchRpcClient.cpp:387-616` (lock/claim/refundHtlc) |
| C3 — persist `bchRedeemScriptHex` | ✅ | `SwapTypes.h:161`, `SwapDaemon.cpp:581,927`, `SwapStateMachine.cpp:254,311` |
| D — `ChainClientConfig` wiring | ✅ | `src/SwapDaemon/ChainClientConfig.{h,cpp}` |
| F4 — XMR base58 checksum validator | ✅ | `swapxfg/app/validate.go:131-159` + inline base58 decoder at line 226+ |

Do **not** redo any of the above. If a subagent thinks a v1 task is still open, stop and re-audit — the plan says it is closed.

## Scope of v2

Only these chunks. They are ordered by build-time dependency (crypto library first, then daemon logic, then TUI cleanup, then validation).

1. **Phase X — XMR adaptor spend-key combine** (real work)
2. **Phase S — Web-bridge SRI hashes** (small)
3. **Phase E — `isDummyGiftSecret` cleanup** (small)
4. **Phase T — TUI stale TODO removal** (trivial)
5. **Phase V — Testnet walkthrough + final review-doc update**

### Files this plan touches

- `src/SwapDaemon/Monero/AdaptorSignature.{h,cpp}` — add `combineSpendKeys()`
- `src/SwapDaemon/Monero/MoneroRpcClient.cpp:439-457` — wire real combine into `claimAdaptor` + `refundAdaptor`
- `tests/SwapDaemon/XmrClaimAdaptorTests.cpp` — new
- `swapxfg/app/bridge_eth.go:24` — real SRI
- `swapxfg/app/bridge_sol.go:20` — real SRI
- `src/CryptoNoteCore/TransactionExtra.cpp:1259-1281` — remove or re-scope heuristic
- `src/CryptoNoteCore/TransactionExtra.h:337` — matching header update
- `swapxfg/app/tui.go:499,523,565,590,608,636` — remove 6 stale `// TODO: remaining goroutines need tea.Cmd conversion` comments (code is already `tea.Cmd`)
- `docs/runbooks/testnet-swap-walkthrough.md` — new
- `docs/review/xfgCdswaps-review.md` — Phase V results

### Files explicitly out of scope

- Anything under `src/SwapDaemon/pool_v11/**` (deferred to v11 per user direction)
- Anything in `src/CryptoNoteCore/Blockchain.cpp` or consensus rules
- Wallet GUI (separate repo)
- Mainnet cutover / coordinated fork

---

## Chunk 1 — Phase X: XMR adaptor spend-key combine

Context: `MoneroRpcClient::claimAdaptor` today delegates to `sweepSharedAddress` using only Alice's key share (`MoneroRpcClient.cpp:439-448`), which cannot spend the joint shared address. The real behaviour requires combining (aliceShare + bobShare + adaptorSecret) mod ℓ on the Ed25519 scalar field and importing the combined scalar as the wallet spend key before sweeping. `refundAdaptor` (`MoneroRpcClient.cpp:450-457`) has the analogous gap on the cooperative-refund path.

### Task X1: `AdaptorSignature::combineSpendKeys`

**Files**
- Modify: `src/SwapDaemon/Monero/AdaptorSignature.h` — declare `static bool combineSpendKeys(const std::array<uint8_t,32>& alice, const std::array<uint8_t,32>& bob, const std::array<uint8_t,32>& adaptor, std::array<uint8_t,32>& outCombined);`
- Modify: `src/SwapDaemon/Monero/AdaptorSignature.cpp` — implement using Ed25519 scalar addition (`sc_add` / `sc_reduce32` from the existing Monero crypto bindings already pulled in by this file)
- Create: `tests/SwapDaemon/XmrClaimAdaptorTests.cpp`
- Modify: `src/SwapDaemon/CMakeLists.txt` — register the new test source

- [ ] **Step 1: Failing test for `combineSpendKeys`**
  - In `XmrClaimAdaptorTests.cpp`, assert `(alice + bob + adaptor) mod ℓ` matches a vector computed with a reference implementation (e.g. Python `nacl` or a hard-coded Monero test vector). Also assert the function rejects a 0-scalar result.
  - Expected to fail at link time — symbol does not exist yet.

- [ ] **Step 2: Implement the function**
  - Call the same `sc_add` helper that the rest of `AdaptorSignature.cpp` already uses. Reject a 0 output (means someone passed `-a + -b + -c` and the sweep would brick).
  - Keep the function `static` and free of RPC concerns.

- [ ] **Step 3: Run**
  ```bash
  cmake --build /home/ar/fuego/build --target SwapDaemonTests -j$(nproc) && \
    ctest --test-dir /home/ar/fuego/build -R XmrClaimAdaptor --output-on-failure
  ```
  Expected: passes.

- [ ] **Step 4: Commit**
  ```bash
  git add src/SwapDaemon/Monero/AdaptorSignature.{h,cpp} \
          tests/SwapDaemon/XmrClaimAdaptorTests.cpp \
          src/SwapDaemon/CMakeLists.txt
  git commit -m "feat(swap-xmr): AdaptorSignature::combineSpendKeys (Alice+Bob+adaptor mod ℓ)"
  ```

### Task X2: wire `combineSpendKeys` into `claimAdaptor`

**Files**
- Modify: `src/SwapDaemon/Monero/MoneroRpcClient.cpp:439-448`

- [ ] **Step 1: Extend the test to cover `claimAdaptor`**
  - Mock `sweepSharedAddress` (or use a fake RPC layer the existing tests already use) and assert that the scalar passed to `sweepSharedAddress` equals `combineSpendKeys(alice, bob, adaptor)`.
  - Drive the test via the `bobSpendKeyHex` parameter that is currently ignored (note the `/*bobSpendKeyHex*/` comment-out at line 440) — this confirms we stop ignoring it.
  - Expected to fail: current code delegates with Alice's key only.

- [ ] **Step 2: Implement**
  - Parse `aliceSpendKeyHex`, `bobSpendKeyHex`, and the adaptor secret into `std::array<uint8_t,32>` scalars. The adaptor secret comes from the swap's revealed preimage — confirm by reading `SwapStateMachine` for how it is surfaced (if it is stored in `SwapParams` already, read it from there; if not, widen the `claimAdaptor` signature to accept it).
  - Call `AdaptorSignature::combineSpendKeys`, hex-encode the output, then pass to `sweepSharedAddress`.
  - On scalar-combine failure, return `false` — do **not** fall back to Alice-only sweep (that would drain the wrong wallet on chain).

- [ ] **Step 3: Run the same ctest target as X1**

- [ ] **Step 4: Commit**
  ```bash
  git commit -m "feat(swap-xmr): claimAdaptor combines Alice+Bob+adaptor before sweep"
  ```

### Task X3: symmetric fix for `refundAdaptor`

**Files**
- Modify: `src/SwapDaemon/Monero/MoneroRpcClient.cpp:450-457`

- [ ] **Step 1: Failing test — cooperative refund sweeps with combined key, not with the single share currently passed in.**

- [ ] **Step 2: Implement** — same shape as X2 but for the refund counterpart. Both parties' key shares are required for a cooperative refund; if the caller only has one share, the function must fail fast (return `false`, no sweep).

- [ ] **Step 3: Run + commit**
  ```bash
  git commit -m "feat(swap-xmr): refundAdaptor requires both shares, combines before sweep"
  ```

---

## Chunk 2 — Phase S: web-bridge SRI hashes

Context: the SRI integrity attributes in `bridge_eth.go:24` (`sha384-KiZhooPaHFaFiXrJzCLPkiV6FwP5e3T1KxCPq0EAK5q6d2MkiLfYuA5KBqALqcX`) and `bridge_sol.go:20` (`sha384-t6eXk3KnnVF8BXZ7KRdyBGriL3ZYWL5xtfkiV6FwP5e3T1KxCPq0EAK5q6d2MkiL`) share a suspicious substring — they are placeholders, not real digests. Any browser that enforces SRI will refuse to load the CDN script and the bridge page will be non-functional.

### Task S1: compute real SRI for ethers 5.7.2 and web3.js 1.95.3

**Files**
- Modify: `swapxfg/app/bridge_eth.go:24`
- Modify: `swapxfg/app/bridge_sol.go:20`

- [ ] **Step 1: Fetch and hash**
  ```bash
  curl -sSL https://cdn.ethers.io/lib/ethers-5.7.2.umd.min.js | \
    openssl dgst -sha384 -binary | openssl base64 -A; echo
  curl -sSL https://cdn.jsdelivr.net/npm/@solana/web3.js@1.95.3/lib/index.iife.min.js | \
    openssl dgst -sha384 -binary | openssl base64 -A; echo
  ```
  Record both base64 strings.

- [ ] **Step 2: Replace the two placeholders**
  - `bridge_eth.go:24` → `integrity="sha384-<real hash>"`
  - `bridge_sol.go:20` → `integrity="sha384-<real hash>"`
  - Do **not** bump the pinned versions; the SRI must match the versions we already ship.

- [ ] **Step 3: Sanity run**
  ```bash
  cd /home/ar/fuego/swapxfg && go build ./... && go vet ./...
  ```
  Expected: builds clean.

- [ ] **Step 4: Optional — manual smoke test**
  - Launch the Go TUI, open the bridge page in a browser, confirm no SRI violation in DevTools console.
  - This step is optional if no browser is available in the working environment; the CDN hash check above is the normative one.

- [ ] **Step 5: Commit**
  ```bash
  git commit -m "fix(swapxfg): real SRI hashes for ethers 5.7.2 and web3.js 1.95.3"
  ```

---

## Chunk 3 — Phase E: `isDummyGiftSecret` heuristic cleanup

Context: `TransactionExtra.cpp:1259-1281` classifies a 32-byte gift secret as "dummy" when >50% of bytes equal the first byte. That statistical heuristic is incorrect in both directions — a real encrypted secret can hit the pattern by chance, and a crafted dummy can avoid it.

### Task E1: audit callers

- [ ] **Step 1: List every caller in the tree** — at least:
  ```bash
  git grep -n isDummyGiftSecret
  ```
  Capture the list and classify each caller as (a) consensus-affecting, (b) wallet-UI only, or (c) test-only.

- [ ] **Step 2: Decide replacement path**
  - If every caller is wallet-UI / test-only → delete the function and its header declaration; update callers to either drop the branch or replace with `gift_secret.empty()`.
  - If any caller is consensus-affecting → replace with an explicit versioned magic-byte prefix (e.g. first two bytes `0xDD 0xDD`) and update `createDummyGiftSecret()` at `TransactionExtra.cpp:1283+` to emit the same prefix so the two functions stay in lock-step. Mark the version byte clearly so future rotations are possible.
  - Either way, write the decision into the commit message so it is discoverable later.

### Task E2: implement the decision and test it

**Files (one path or the other):**
- Path A (delete): `src/CryptoNoteCore/TransactionExtra.{h,cpp}` + every caller found in Task E1
- Path B (versioned prefix): `src/CryptoNoteCore/TransactionExtra.cpp:1259-1310` (both `isDummyGiftSecret` and `createDummyGiftSecret`)
- Test: `tests/UnitTests/TransactionExtraTests.cpp`

- [ ] **Step 1: Failing test**
  - If path A: assert the symbol no longer links. (Enforce by not re-declaring it; the linker enforces the test.)
  - If path B: assert `isDummyGiftSecret(createDummyGiftSecret())` is true and a random 32-byte buffer is classified dummy with probability ≤ 2⁻¹⁶ over 10k samples.

- [ ] **Step 2: Implement the chosen path**

- [ ] **Step 3: Build + test**
  ```bash
  cmake --build /home/ar/fuego/build -j$(nproc)
  ctest --test-dir /home/ar/fuego/build -R TransactionExtra --output-on-failure
  ```

- [ ] **Step 4: Commit**
  ```bash
  git commit -m "fix(extra): replace isDummyGiftSecret statistical heuristic with <chosen strategy>"
  ```

---

## Chunk 4 — Phase T: TUI stale TODO cleanup

Context: v1 Task F2 claimed six goroutines in `swapxfg/app/tui.go` still needed `tea.Cmd` conversion. They are already converted — every site returns `func() tea.Msg { ... }` (the canonical `tea.Cmd` form). Only the `// TODO: remaining goroutines need tea.Cmd conversion` comments are stale.

### Task T1: delete the stale comments

**Files**
- Modify: `swapxfg/app/tui.go` lines 499, 523, 565, 590, 608, 636

- [ ] **Step 1: Verify each site is already a `tea.Cmd`**
  - Read each line and confirm the next non-blank line is `return func() tea.Msg {`.
  - If any one of them is not, stop and flag it — do not silently delete a TODO that still reflects real work.

- [ ] **Step 2: Remove the six comment lines only**
  - Leave surrounding blank lines untouched so the file keeps the same visual rhythm.

- [ ] **Step 3: Build**
  ```bash
  cd /home/ar/fuego/swapxfg && go build ./... && go vet ./...
  ```

- [ ] **Step 4: Commit**
  ```bash
  git commit -m "chore(swapxfg): drop stale tea.Cmd TODO comments (already converted)"
  ```

---

## Chunk 5 — Phase V: testnet validation + review doc

### Task V1: testnet walkthrough runbook

**Files**
- Create: `docs/runbooks/testnet-swap-walkthrough.md`

- [ ] **Step 1: Write a per-pair runbook** for XFG↔ETH (Sepolia), XFG↔BCH (chipnet), XFG↔XMR (stagenet), XFG↔SOL (devnet). Each pair section must include:
  - Faucet URLs.
  - A `swap_config.json` template with placeholder keys (never real ones).
  - Expected daemon log lines per state transition.
  - Rollback steps if a swap stalls at each state.

- [ ] **Step 2: Commit**
  ```bash
  git commit -m "docs(swap): testnet swap walkthrough runbook"
  ```

### Task V2: manual end-to-end run per pair

No code changes — signoff only.

- [ ] **Step 1: XFG↔ETH (Sepolia)** — initiate, lock, claim, confirm on-chain. Record tx hashes.
- [ ] **Step 2: XFG↔BCH (chipnet)** — initiate, fund HTLC, claim with secret, confirm.
- [ ] **Step 3: XFG↔XMR (stagenet)** — initiate, claimAdaptor (this exercises Phase X end-to-end), confirm XMR sweep lands.
- [ ] **Step 4: XFG↔SOL (devnet)** — initiate, lock, claim, confirm.
- [ ] **Step 5: For each pair, run the refund path** — let the lock expire, refund, confirm.

### Task V3: update review doc

**Files**
- Modify: `docs/review/xfgCdswaps-review.md`

- [ ] **Step 1: Add a "Phase V (v2 plan) — testnet results" section** listing pass/fail per pair with tx hashes, and any new issues uncovered.

- [ ] **Step 2: Close out the "intentional stubs" section** — mark each item with the commit SHA that closed it (or explicitly re-defer with rationale).

- [ ] **Step 3: Commit**
  ```bash
  git commit -m "docs(review): record Phase V testnet swap results"
  ```

---

## Plan review loop

After each chunk lands, dispatch a plan-document-reviewer subagent against that chunk to confirm:

1. Every task names exact file paths with line numbers where relevant.
2. Tests are written before implementation (TDD discipline).
3. Commits happen at task boundaries, not at the end.
4. No skipped abstractions — "add validation" without a concrete check is a red flag.
5. Each task is bite-sized (2–5 minutes per step).

If the reviewer flags issues, fix in place before the next chunk.

---

## Whole-plan verification

After all five chunks complete:

1. `cmake --build /home/ar/fuego/build -j$(nproc)` builds clean with secp256k1 still linked and no new warnings above baseline.
2. `ctest --test-dir /home/ar/fuego/build` passes, including the new `XmrClaimAdaptorTests` and `TransactionExtraTests`.
3. `cd /home/ar/fuego/swapxfg && go test ./...` passes.
4. `docs/review/xfgCdswaps-review.md` shows every previously-open intentional stub either closed with a SHA or explicitly re-deferred.
5. Runbook (`docs/runbooks/testnet-swap-walkthrough.md`) records at least one successful swap and one successful refund for every supported pair.

---

## History

- **v1 (2026-04-16, superseded):** 7-phase plan assuming secp256k1 wiring, ETH/BCH signing, ChainClientConfig, and XMR-address validation were all still open. Subsequent audit found that work had already merged in commit `b2bb1de4` and related precursors.
- **v2 (2026-04-16, this file):** scoped to the five items actually still open: XMR adaptor combine, web-bridge SRI, `isDummyGiftSecret` heuristic, TUI TODO cleanup, testnet validation.
