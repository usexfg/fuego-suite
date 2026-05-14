# xfgCdswaps — Comprehensive Fix Plan

**Branch:** `xfgCdswaps`
**Sources:** `docs/review/xfgCdswaps-review.md` (62 findings) + `docs/DEVELOPER_GUIDE_swapxfg.md` (dev guide additions)
**Methodology:** subagent-driven-development with two-stage review per task; dispatching-parallel-agents across truly independent subsystems.

## Additions from dev guide (not previously in review)

1. **CRITICAL — Gift-secret "encryption" is XOR, not ChaCha20** — `src/CryptoNoteCore/TransactionExtra.cpp:1216` — `gift_secret[i] = secret[i] ^ chachaKey[i % 32] ^ nonce[i % 8]` is repeating-key XOR. The 32-byte key plus 8-byte nonce repeats every 32 bytes (LCM); ciphertext is trivially decryptable for plaintexts > 32 bytes via known-plaintext or reuse across deposits.
2. **CRITICAL — PoolAMM 64-bit integer overflow** — `src/SwapDaemon/PoolAMM.cpp:31, 32` — `inputWithFee * reserveOut` and `reserveIn * 10000 + inputWithFee` both `uint64_t`. Overflow trivially with realistic LP reserves.
3. **MEDIUM — `computeMerkleRootInternal` recomputes from scratch O(n²)** — `src/CryptoNoteCore/CommitmentIndex.cpp:78, 234, 375` — three call sites all rebuild the entire merkle tree per commitment add. Append-only structure needed.

## Total fix count

**66 fixes** across 4 subsystems (62 from review + 4 from dev guide; 1 dev guide item dedup with my CD overflow finding).

---

## Execution strategy

### Workspace
Branch off `xfgCdswaps` to a fix branch:
```
git checkout -b fix/xfgCdswaps-criticals xfgCdswaps
```
No worktree (user did not request one). All implementer subagents work on this branch sequentially per phase; spec/quality reviewer subagents run after each implementer.

### Parallelization model
- **Within a phase:** implementer subagents are sequential within a shared file, but parallel across independent files/subsystems.
- **Across phases:** strictly sequential — Phase 1 unblocks Phase 4; Phase 2/3/4 are independent and run after Phase 1.
- **Reviewers:** spec + quality reviewer per task in series; reviewers across tasks can be parallel.

### Model assignment (per subagent-driven-development guidance)
- **Mechanical, single-file fixes** (e.g., add `case`, add `return`, change comparison operator) → **Haiku**
- **Multi-file, integration, or design judgment** (e.g., expose CommitmentIndex via INode, rewire CD RPCs, EIP-155 signing path) → **Sonnet**
- **Cryptographic work** (DLEQ verification, ChaCha20-Poly1305 swap, MuSig2 nonce guards) → **Sonnet** (Opus if available)

---

## Phase 1 — Foundation (sequential, blocks everything else)

These three fixes either unblock other fixes or eliminate latent UB that affects every other test.

### 1.1 Aliases parser fix + default skip-TLV
- **File:** `src/CryptoNoteCore/TransactionExtra.cpp`
- **Change:** Add `case TX_EXTRA_ALIAS:` to the `parseTransactionExtra` switch (pattern: read varint length, deserialise into `TransactionExtraAliasRegistration`, push to `transactionExtraFields`). Add `default:` case that reads varint length and skips that many bytes (TLV unknown-tag skip pattern, identical to 0xD5 case).
- **Why first:** Unblocks the entire alias subsystem AND closes the stream-desync attack on every other TX_EXTRA tag (0xEB/0xEC/0xEF). Phase 5 alias fixes are blocked on this.
- **Model:** Haiku

### 1.2 Aliases fee enforcement at block-import
- **File:** `src/CryptoNoteCore/Blockchain.cpp` near `pushToBankingIndex` alias handling (around line 3046–3066)
- **Change:** After parsing `TransactionExtraAliasRegistration`, verify TX outputs include ≥`ALIAS_REGISTRATION_FEE` to `FUEGO_DEV_FUND_ADDRESS` (mainnet) or self-transfer (testnet). Reject silently if not.
- **Depends on:** 1.1
- **Model:** Sonnet (touches consensus + needs mainnet/testnet branching)

### 1.3 `generateEpochReport` missing return
- **File:** `src/CryptoNoteCore/CommitmentIndex.cpp:288–297`
- **Change:** Add `return report;` at end. Populate `totalFeesDistributed` and `activeEfierCount` (or remove fields if unused).
- **Why first:** UB fires at every epoch boundary; affects all CD calculations downstream.
- **Model:** Haiku

---

## Phase 2 — Atomic-swap criticals (parallel-safe across files)

### 2.1 DLEQ proof verification in `verifyAdaptor`
- **File:** `src/SwapDaemon/Monero/AdaptorSignature.cpp:278–286`
- **Change:** Implement DLEQ verification with distinct generators G and H. Add `P2 = k*H` to wire format; call `verifyDleqProof()` with the transmitted `P2`.
- **Model:** Sonnet (cryptographic)

### 2.2 Adaptor-secret persistence in `SwapStateMachine`
- **Files:** `src/SwapDaemon/SwapStateMachine.cpp:204–238` (serialize), `:240–285` (deserialize)
- **Change:** Persist `params.adaptorSecret` with at-rest encryption (ChaCha20-Poly1305 keyed from wallet spend key). Restore on deserialize.
- **Model:** Sonnet

### 2.3 EthRpcClient EIP-155 signing
- **File:** `src/SwapDaemon/Ethereum/EthRpcClient.cpp:327–361`
- **Change:** Replace `eth_sendTransaction` with `eth_sendRawTransaction` (already exists in same file). Build EIP-155 signed transactions including chainId.
- **Model:** Sonnet

### 2.4 PoolAMM 128-bit overflow protection
- **File:** `src/SwapDaemon/PoolAMM.cpp:31–40` (and `poolGetInputAmount` similarly)
- **Change:** Use `__uint128_t` for `numerator` and `denominator` intermediate products.
- **Model:** Haiku

---

## Phase 3 — swapxfg criticals (parallel-safe — Go subsystem, no C++ overlap)

### 3.1 WebSocket origin pin + per-session token + reject duplicate connections
- **File:** `swapxfg/app/bridge.go:66–68, 171–208`
- **Change:** `CheckOrigin` validates `Origin` header equals `http://127.0.0.1:<port>`. Inject random nonce into served HTML; require it as query token on WS handshake. Reject duplicate connections instead of replacing.
- **Model:** Sonnet

### 3.2 Reinstate rate-limiter (`golang.org/x/time/rate`)
- **Files:** `swapxfg/go.mod`, `swapxfg/app/bridge.go:77–86`, `swapxfg/app/rpc.go`
- **Change:** Re-add dependency removed in `c48b35fb`. Apply per-IP limiter to `/bridge/ws`, per-method limiter to `FetchAll` fan-out.
- **Model:** Sonnet

### 3.3 WebSocket framing limits + deadlines + ping/pong
- **File:** `swapxfg/app/bridge.go:171–208`
- **Change:** `conn.SetReadLimit(1<<16)`, install `SetReadDeadline` + `PongHandler` extending it, run a 30s `PingHandler` writer goroutine.
- **Model:** Sonnet

### 3.4 BCH `lock` command — refuse instead of spending
- **File:** `swapxfg/app/tui.go:440–454`
- **Change:** Return error from `bch lock` until SwapDaemon exposes an HTLC P2SH address endpoint. Do not call `payto`.
- **Model:** Haiku

### 3.5 CD tab — wire to real RPCs or hide
- **File:** `swapxfg/app/cd_rpc.go` (entire file)
- **Change:** Replace fictitious `/getcdoffers`, `/getcdprice`, `/acceptcd`, `/submitcd`, `/cancelcd` calls with real wallet RPC methods (`create_cd`, `rollover_cd`, `estimate_cd_yield`) and daemon `/rollover_deposit`. Where no equivalent exists (orderbook semantics), gate the UI feature.
- **Model:** Sonnet
- **Depends on:** Phase 4.3 (CommitmentIndex via INode) for `rollover_cd` to actually work

---

## Phase 4 — CD criticals (sequential — Blockchain.cpp shared)

### 4.1 `haveTransactionKeyImagesAsSpent` — extend to commitment inputs
- **File:** `src/CryptoNoteCore/Blockchain.cpp:1968`
- **Change:** Iterate `TransactionInputCommitmentSpend` and `TransactionInputCommitmentTransfer` inputs alongside `KeyInput`.
- **Model:** Haiku

### 4.2 Epoch fee accumulator rollback in `popBlock`
- **File:** `src/CryptoNoteCore/Blockchain.cpp:3189`
- **Change:** Track per-block swap-fee contributions in a sidecar structure; subtract them from `m_currentEpochSwapFees` during `popBlock`. Pop the last `m_epochFeeRates` entry if popped block was an epoch boundary.
- **Model:** Sonnet (consensus-critical, needs careful reorg testing)

### 4.3 Expose `CommitmentIndex` via `INode`
- **Files:** `src/INode.h`, `src/Wallet/WalletGreen.cpp`, daemon `INode` impl
- **Change:** Add `getCdInterest(amount, height)` and `getEpochFeeRate(epoch)` methods to `INode`. Wire daemon-side to `CommitmentIndex`. Wallet-side then calls through.
- **Why critical:** Unblocks `on_rollover_cd`, `estimate_cd_yield`, `getMaturingDeposits` simultaneously.
- **Model:** Sonnet

---

## Phase 5 — Important fixes (parallel across subsystems)

### 5.1 Atomic swaps (Sonnet — cryptographic) [9 fixes]
- MuSig2 `nonceSigned` guard (`AdaptorSwap.cpp:140-148`, `musig2.cpp:290-319`)
- Timelock ordering validation in `initiate()`/`accept()` (`SwapDaemon.cpp:127-188`)
- BCH HTLC hash type — switch to `OP_SHA256 OP_EQUALVERIFY` or expose RIPEMD160 step (`HtlcScript.cpp:369-371`)
- Zero `params.adaptorSecret` after use (`AdaptorSwap.cpp:164-226`)
- `PoolAMM::poolValidateDepositRatio` called from `processDeposit` (`PoolOrganizer.cpp:101-155`)
- Add legacy HTLC states to `isTerminal()` or migrate to `FAILED` (`SwapStateMachine.cpp:191-201`)
- Remove `COMPLETE_SWAP`/`REFUND_SWAP` wallet stubs (or implement) (`WalletRpcServer.cpp:298-310`)
- Inject `PoolDatabase` into `PoolOrganizer` (persistence) (`PoolOrganizer.cpp:39-42`)
- Add `POOL_FEE_CLAIM_INITIATED → POOL_FEE_CLAIM_REFUNDED` transition (`SwapStateMachine.cpp:134-141`)
- Counterparty-chain hot paths (ETH/SOL/BCH/XMR `lock`/`claim`/`refund` calls) — biggest single fix; may warrant its own phase

### 5.2 swapxfg (Sonnet — Go) [9 fixes]
- ETH calldata via `go-ethereum/abi` (`tui.go:512-525`)
- Address validators per chain (`tui.go:276-289`, etc.)
- Wallet RPC auth headers (`rpc.go:306-331`)
- HTTP status code checks (`rpc.go:316-330`)
- TUI goroutine→`tea.Msg` conversion (`tui.go` many sites)
- Bridge `Action` whitelist (`bridge_eth.go:29-79`, `bridge_sol.go:28-56`)
- Pin CDN scripts + SRI + CSP + vendor (`bridge_eth.go:14`, `bridge_sol.go:19`)
- Amount parse via `strconv.ParseFloat` with bounds (`tui.go:303-540`)
- Random per-request nonces instead of monotonic IDs (`bridge_eth.go:92-97`)

### 5.3 CDs (Sonnet — consensus) [7 fixes]
- `claimedInterest` cap policy (youngest ring member or ZK proof) (`Blockchain.cpp:2228, 2288`)
- `cdSwapShare * FEE_POOL_RATE_PRECISION` overflow → `__uint128_t` (`Blockchain.cpp:3151`)
- `encryptDepositSecret` API: generate ephemeral key internally, document non-reuse (`TransactionExtra.cpp:1641`)
- Maturity comparison alignment (consensus `<` vs wallet `<=`) (`Blockchain.cpp:2242` + `WalletGreen.cpp`)
- `newTerm` upper bound in `TransactionInputCommitmentTransfer` (`Blockchain.cpp:2321`)
- `on_create_cd` `deposit_type` parameter (`WalletRpcServerCommandsDefinitions.h:583`, `WalletRpcServer.cpp:759`)
- `on_rollover_cd` real implementation + `estimate_cd_yield` real value — depends on 4.3

### 5.4 Aliases (Haiku/Sonnet) [6 fixes]
- `isValid()` 9-char exception alignment with `isValidRegularAlias` (Haiku) (`TransactionExtra.cpp:1510-1528`)
- Remove `voidAlias` from public interface or add signed-revocation auth (Sonnet) (`AliasIndex.cpp:146-161`)
- Remove dead `LOUDMINING` branch + document reserved set (Haiku) (`AliasIndex.cpp:89-103`)
- Add alias methods to `WalletGreen` (Sonnet) (`WalletGreen.h/cpp`)
- `toLower` normalisation in `registerAlias`/`getAliasByName` (Haiku) (`AliasIndex.cpp:178-185`)
- `addressHash` over spend pubkey, not base58 string (Sonnet — consensus change) (`SimpleWallet.cpp:3660`, `AliasIndex.cpp` callers)
- `networkId` field in `TransactionExtraAliasRegistration` (Sonnet) (`TransactionExtra.h:140-150`)

---

## Phase 6 — Dev-guide criticals not covered above

### 6.1 Replace XOR "ChaCha20" with real ChaCha20-Poly1305 for gift secrets
- **File:** `src/CryptoNoteCore/TransactionExtra.cpp:1216, 1238` (encrypt + decrypt)
- **Change:** Use the existing ChaCha8/ChaCha20 implementation already in the codebase (used by `encryptDepositSecret`). Add Poly1305 MAC. Remove the XOR loop.
- **Model:** Sonnet (cryptographic)

### 6.2 Append-only Merkle tree for `CommitmentIndex`
- **File:** `src/CryptoNoteCore/CommitmentIndex.cpp:78, 234, 375`
- **Change:** Replace full-rebuild `computeMerkleRootInternal` with append-only structure (cache intermediate nodes; only recompute changed branches).
- **Model:** Sonnet

---

## Phase 7 — Minor cleanups (fully parallel, Haiku)

- `AliasEntry::aliasType` default → 1, update comment (`AliasIndex.h:35`)
- Wrong tag comment `0xFA` → `0xEA` (`Blockchain.cpp:3045`)
- `SWAP_FEE_CD_SHARE_PCT` actually used in computation (`Blockchain.cpp:3146`)
- Add alias unit tests (round-trip, isValid edges, reserved names, duplicates, case)
- Two DLEQ subsystems documentation (`dleq.cpp` vs `Monero/AdaptorSignature.cpp`)
- Daemon restart `checkTimeouts()` immediately (`SwapDaemon.cpp:45-62`)
- `bridge_open.go` injection guard (`swapxfg/app/bridge_open.go:11-22`)
- `AcceptCdOffer`/`CancelCdOffer` empty-field guard (`tui.go:557-587`)

---

## Phase 8 — Counterparty-chain wiring (own batch — largest single fix)

This is the swap-daemon’s biggest gap and must be done carefully.

- **Files:** `src/SwapDaemon/SwapDaemon.cpp` (`processSwap`, `refund`), per-chain RPC clients
- **Change:** For each `SwapPair` value, dispatch to the appropriate chain RPC client to `lock`/`claim`/`refund` on the counterparty side. Wire `EthRpcClient`/`SolRpcClient`/`BchRpcClient`/`MoneroRpcClient` into the state machine transitions.
- **Model:** Sonnet (multi-file integration, requires careful state-machine coordination)
- **Why last:** Depends on EIP-155 (2.3), DLEQ (2.1), and adaptor-secret persistence (2.2) being correct first.

---

## Final phase — Full review

After all task phases:
1. Dispatch `superpowers:code-reviewer` over the entire diff `xfgCdswaps...fix/xfgCdswaps-criticals`.
2. Run any existing test suites; flag any new failures.
3. Update `docs/review/xfgCdswaps-review.md` with status: which findings are now closed.

---

## Verification per phase

Each implementer subagent must verify:
- Builds (or call out if build fails).
- Existing tests still pass (`make test` or equivalent).
- New behaviour test where reasonable (TDD per superpowers:test-driven-development).
- Self-review noted in commit message.

Each spec-reviewer subagent must verify:
- Change matches the spec text above (no scope creep, no missing requirement).
- File:line citation in spec matches actual edited location.

Each code-quality reviewer subagent must verify:
- No magic numbers, no commented-out code.
- Naming consistent with file conventions.
- Error paths handled.
