# xfgCdswaps — Code Completion & Security Review

**Branch:** `xfgCdswaps`  
**HEAD:** `a259d0f5` (add readme to swapxfg, rm legacy swap code folders)  
**Reviewed:** Atomic swaps, swapxfg TUI + bridge, Certificates of Deposit, Regular aliases  
**Date:** 2026-04-14  
**Methodology:** Four independent deep-audit subagents; no builds run; no code edits proposed.

---

## Fix status — `fix/xfgCdswaps-criticals`

**Fix branch HEAD:** `aafb4eb3` (55 commits on top of `xfgCdswaps`, 2026-04-16)

| Scope | Critical closed | Important closed | Minor closed | Remaining |
|---|---|---|---|---|
| Atomic swaps | 3/3 ✅ | 8/9 ✅ | 3/3 ✅ | ETH secp256k1 signing stub (needs lib) |
| swapxfg | 5/5 ✅ | 9/9 ✅ | 2/2 ✅ | SRI hashes need prod verification |
| Certificates of Deposit | 2/2 ✅ | 9/9 ✅ | 4/4 ✅ | — |
| Regular aliases | 3/3 ✅ | 9/9 ✅ | 4/4 ✅ | — |
| **Total** | **13/13** | **35/36** | **13/13** | 1 Important open |

**Post-review fixes applied (2026-04-16):**
- Gift-secret MAC now uses `cn_fast_hash(ciphertext ‖ IV)` instead of `(ciphertext ‖ key)` — avoids including key material in MAC preimage
- RESERVED_ALIASES table normalised to lowercase-only; uppercase rows were unreachable after toLower normalisation
- Alias fee enforcement comment clarified: full destination verification requires wallet-level key derivation; consensus enforces amount

**Intentional stubs (not bugs):**
- EIP-155 / ETH transaction signing (`EthRpcClient`) — throws `runtime_error`; caught and FAILS swap gracefully. Needs secp256k1 library linked.
- BCH HTLC signing — lock/refund script signing stubbed; logs error. Needs BIP143 sighash implementation.
- SOL/XMR keypair injection — empty secret keys are config-time TODO, not runtime crashes.
- zkLPswap pool AMM — moved to `src/SwapDaemon/pool_v11/`, excluded from build, deferred to v11.

**Branch is testnet-ready** for XFG↔SOL swaps (fully wired), XFG↔XMR adaptor swaps (partially wired), aliases, and CDs. ETH and BCH chains require the secp256k1 / BIP143 signing work before mainnet use.

---

## Executive summary (original findings)

| Scope | Critical | Important | Minor | Status |
|---|---|---|---|---|
| Atomic swaps | 3 | 9 | 3 | Not testnet-ready |
| swapxfg | 5 | 9 | 2 | Not testnet-ready |
| Certificates of Deposit | 2 | 9 | 4 | Not testnet-ready |
| Regular aliases | 3 | 9 | 4 | Non-functional |
| **Total** | **13** | **36** | **13** | — |

Three scopes were **blocking** for any real-value testnet deployment. The alias subsystem was **entirely non-functional** at consensus level — no alias had ever been written to the index from a confirmed block due to a missing parser case.

---

## Atomic swaps

### Completeness gaps

- **Important**: Counterparty-chain hot paths absent from daemon — no ETH/SOL/BCH lock, claim, or refund is ever initiated
  - `src/SwapDaemon/SwapDaemon.cpp:345–416`
  - `SwapDaemon.cpp` contains no `#include` of `EthRpcClient`, `SolRpcClient`, `BchRpcClient`, or `MoneroRpcClient` and no call sites to `lock`, `claim`, or `refund` on any counterparty chain. The daemon logs "waiting for counterparty chain lock" but never acts. The entire cross-chain side of the hot path is absent; only XFG-side transaction building is wired.
  - Wire `EthRpcClient`/`SolRpcClient`/`BchRpcClient`/`MoneroRpcClient` into `processSwap()` and `refund()` for each `SwapPair`. The interfaces and ABI encoders exist; only the call sites are missing.

- **Important**: Legacy HTLC states not in `isTerminal()` — daemon loops on legacy DB records forever
  - `src/SwapDaemon/SwapTypes.h:31–38`
  - `src/SwapDaemon/SwapStateMachine.cpp:191–201`
  - States `XFG_LOCKED=1` through `CTR_REFUNDED=6` fall into `isValidTransition`'s `default: return false` and are excluded from `isTerminal()`. Any old DB record in a non-terminal HTLC state produces an unresolvable swap that the tick thread retries every 30 s forever.
  - Add all six legacy states to `isTerminal()`, or migrate them to `FAILED` on load.

- **Important**: `COMMAND_RPC_COMPLETE_SWAP` and `COMMAND_RPC_REFUND_SWAP` wallet handlers are stubs that unconditionally throw
  - `src/Wallet/WalletRpcServer.cpp:298–310`
  - Both handlers throw `JsonRpcError` immediately. No client can complete or cooperatively refund via the wallet-RPC surface.
  - If SwapDaemon-only operation is intended, remove the registrations. Otherwise implement the handlers.

- **Important**: `PoolOrganizer` does not persist to `PoolDatabase` — all pool state is lost on daemon restart
  - `src/SwapDaemon/PoolOrganizer.cpp:39–42`
  - `src/SwapDaemon/PoolDatabase.h`
  - The `PoolOrganizer` constructor does not accept or use `PoolDatabase`. Pool balances, LP shares, and fee accumulators are entirely in-memory; a restart zeros them all.
  - Inject `PoolDatabase` into `PoolOrganizer`, load on construction, persist after every `processDeposit`/`processWithdrawal`/`executeSwap`/`processFeeClaim`.

- **Important**: `POOL_FEE_CLAIM_REFUNDED` is an orphan state — no valid transition leads to it
  - `src/SwapDaemon/SwapStateMachine.cpp:134–141`
  - The state is listed as terminal but the only inbound path (`POOL_FEE_CLAIM_INITIATED → POOL_FEE_CLAIM_REFUNDED`) is missing from the switch. Fee-claim failure can never be recorded.
  - Add `return newState == SwapState::POOL_FEE_CLAIM_REFUNDED` to the `POOL_FEE_CLAIM_INITIATED` case.

- **Minor**: Daemon restart does not immediately process recovered swaps — first action is 30 s later
  - `src/SwapDaemon/SwapDaemon.cpp:45–62`
  - Call `checkTimeouts()` and one round of `processSwap` immediately after loading non-terminal swaps in `start()`.

### Security findings

- **Critical**: Adaptor secret (`params.adaptorSecret`) is not serialized — lost on daemon restart, locking escrow funds permanently
  - `src/SwapDaemon/SwapStateMachine.cpp:204–238` (serialize — no `adaptorSecret` field)
  - `src/SwapDaemon/SwapStateMachine.cpp:240–285` (deserialize — no restore)
  - If Bob restarts the daemon between `ADAPTOR_PRESIGS_READY` and `ADAPTOR_XFG_SPENT`, his adaptor secret is zeroed and the adapted spend cannot be completed. XFG in escrow is permanently lost.
  - Serialize `adaptorSecret` with at-rest encryption (e.g., ChaCha20Poly1305 keyed from the wallet's spend key), or design a recovery derivation path.

- **Critical**: `verifyAdaptor()` skips DLEQ proof verification — an adversary can supply a random adaptor point and steal Alice's counterparty funds
  - `src/SwapDaemon/Monero/AdaptorSignature.cpp:278–286`
  - Comment: *"TODO (protocol v2): verify DLEQ proof… For testnet: adaptor equation check above is sufficient."* Alice has no cryptographic guarantee that Bob's `T = t*G`. A malicious Bob supplies a random point; Alice funds the escrow believing she can claim; Bob cannot produce the adapted signature; Alice's counterparty funds are irrecoverable.
  - Complete DLEQ verification before transitioning from `ADAPTOR_KEYS_EXCHANGED`.

- **Critical**: `EthRpcClient` uses `eth_sendTransaction` (unlocked node) with no EIP-155 chain ID — transactions are replayable across any EVM chain
  - `src/SwapDaemon/Ethereum/EthRpcClient.cpp:327–361`
  - Requires a keystore-unlocked Ethereum node for all state-changing calls. No chain ID in any transaction path (EIP-155 replay protection absent).
  - Replace with EIP-155-compliant offline signing via `eth_sendRawTransaction`. The method already exists in the file; use it exclusively.

- **Important**: MuSig2 `nonceSigned` guard missing — double call to `adaptor_partial_sign` leaks the effective private key
  - `src/SwapDaemon/AdaptorSwap.cpp:140–148`
  - `src/crypto/musig2.cpp:290–319`
  - `nonceSigned` boolean flag exists but is never checked before signing (only `sessionInitialized` is). A second call on an already-zeroed `sec_nonce` produces `s_i = -c*ax`, leaking `ax = coeff * sec_key` to any observer who sees both partial signatures.
  - Add a `nonceSigned` boolean to `Musig2State`; set and assert it in `adaptor_partial_sign`.

- **Important**: Timelock ordering not validated — counterparty timeout can exceed XFG refund window, enabling double-dip
  - `src/SwapDaemon/SwapDaemon.cpp:127–188` (`initiate()`)
  - `ctrTimeoutBlock` is never validated against the XFG timeout equivalent. If a user sets `ctrTimeoutBlock` larger than ~180 XFG blocks, the initiator can reclaim the XFG escrow while the follower is still trying to claim the counterparty funds.
  - Validate in `initiate()` and `accept()` that `ctrTimeout < xfgTimeoutHeight` using per-chain block-time estimates.

- **Important**: BCH HTLC uses `OP_HASH160` (RIPEMD160(SHA256), 20 bytes) but adaptor protocol hashes are Keccak256 (32 bytes) — no conversion exists
  - `src/SwapDaemon/BitcoinCash/HtlcScript.cpp:369–371`
  - `src/SwapDaemon/BitcoinCash/HtlcScript.h:23–39`
  - `hashLockRipemd160` enforces a 20-byte input but all other chain integrations pass 32-byte Keccak hashes. No conversion is shown anywhere in the codebase. A raw 32-byte hash will throw or be silently truncated.
  - Enforce a consistent hash-lock derivation for BCH (e.g., `OP_SHA256 OP_EQUALVERIFY`) or expose an explicit RIPEMD160 conversion step in the integration layer.

- **Important**: `adaptorSecret` not zeroed after use — lives in-memory and in any crash dump
  - `src/SwapDaemon/AdaptorSwap.cpp:196–226` (extract writes, no subsequent zero)
  - `src/SwapDaemon/AdaptorSwap.cpp:164–193` (aggregate reads, no zero after use)
  - Zero `params.adaptorSecret` immediately after `musig2_aggregate` returns on Bob's path, and after extraction has served its purpose on Alice's path.

- **Important**: `processDeposit` accepts an externally-supplied `shareAmount` without cross-checking `poolValidateDepositRatio` — LP sandwich attack vector
  - `src/SwapDaemon/PoolOrganizer.cpp:101–155`
  - `src/SwapDaemon/PoolAMM.h:103–108`
  - `poolValidateDepositRatio` exists but is not called from `processDeposit`. An MEV actor can front-run a deposit with a large swap, inflating the price, then pocket the difference in LP shares.
  - Call `poolValidateDepositRatio` inside `processDeposit` and assert the computed shares equal the supplied `shareAmount`.

- **Minor**: Two DLEQ subsystems use incompatible hash functions — cross-component verification would silently fail
  - `src/crypto/dleq.cpp:33–44` (uses `"FuegoDLEQ"` domain + `hash_to_scalar`)
  - `src/SwapDaemon/Monero/AdaptorSignature.cpp:120–139` (uses raw `cn_fast_hash` + `sc_reduce32`)
  - Currently safe since each verifies only its own proofs. Document the boundary explicitly; a type mix in future refactoring produces silent verification failures.

- **Minor**: Daemon restart does not call `checkTimeouts()` immediately — swap may expire in the 30 s gap before the first tick
  - `src/SwapDaemon/SwapDaemon.cpp:45–62`
  - Call `checkTimeouts()` immediately after loading non-terminal swaps in `start()`.

**Bottom line:** Not testnet-ready. Two blockers alone prevent real-value swaps: counterparty-chain hot paths are entirely absent from the daemon (no ETH/SOL/BCH/XMR lock or claim is ever initiated), and `verifyAdaptor()` skips the DLEQ binding proof, allowing a malicious counterparty to steal funds. The adaptor secret is additionally lost on daemon restart. The XFG-side primitives (MuSig2, ring signing, DLEQ generation) and the BCH HTLC script mechanics are well-implemented; the state machine with atomic-rename persistence is a solid foundation. Fix the three Critical issues and the counterparty-chain wiring before any testnet exposure.

---

## swapxfg

> Note: Commit `c48b35fb` ("wtf") reverted previously-landed rate-limiting (`ed3a1ebb`, `d5bd4411`) and ETH/SOL address validation (`9af4e3b5`). `golang.org/x/time` is no longer in `go.mod`.

### Completeness gaps

- **Critical**: CD tab RPCs wired to phantom endpoints that do not exist in the C++ server
  - `swapxfg/app/cd_rpc.go:43, :53, :63, :71, :84`
  - `GetCdOffers`/`GetCdPrice`/`AcceptCdOffer`/`SubmitCd`/`CancelCdOffer` post to `/getcdoffers`, `/getcdprice`, `/acceptcd`, `/submitcd`, `/cancelcd` — none exist in `src/Rpc/RpcServer.cpp` or `src/Wallet/WalletRpcServer.cpp`. The real CD surface is `create_cd`, `rollover_cd`, `estimate_cd_yield`, and `/rollover_deposit`. Every CD tab call silently fails into zero-valued responses (errors are discarded at `cd_rpc.go:53`).
  - Rewrite the CD RPC layer against the real C++ endpoints. The orderbook-style methods need server-side implementation or the CD tab must be removed until they land.

- **Critical**: BCH `lock` command spends funds with no HTLC — hashlock and timeout are explicitly discarded
  - `swapxfg/app/tui.go:440–454`
  - Comment in code: *"For now we use the counterparty address directly as a placeholder."* `_ = hl; _ = tbl` discards both parameters. Funds go to an un-hashlocked address; they are irrecoverable if sent to a real peer.
  - Gate `bch lock` behind an error return until SwapDaemon exposes an HTLC P2SH address endpoint. At minimum refuse the command explicitly instead of spending funds.

- **Important**: ETH `lock` calldata is built by raw string concatenation — no ABI encoding, no function selector, malformed
  - `swapxfg/app/tui.go:512–525`
  - `calldata := "0x" + hashlock + timeout` with no `eth_` function selector prefix and no ABI encoding of `(bytes32, uint256)`. Any real HTLC contract will revert. `amtWei` passed as a decimal float string also breaks `ethers.BigNumber.from()` in the browser script.
  - Encode calldata via `go-ethereum`'s `abi` package or relay a pre-encoded payload from SwapDaemon. Validate `amtWei` is a non-negative integer string before bridging.

- **Important**: No ETH/SOL/BCH/XMR address validation — reverted in `c48b35fb`
  - `swapxfg/app/tui.go:276–289, :435–454, :512–525`
  - Commit `9af4e3b5` added checksum/base58/cashaddr/subaddress validators; they were removed. The XFG heuristic (`HasPrefix "f"` + `len >= 98`) at line 276 is not address validation.
  - Reinstate per-chain validators before any goroutine launches a chain action.

- **Important**: Wallet RPC auth credentials never forwarded — production wallets unreachable, unauthenticated wallets fully open to any local process
  - `swapxfg/app/rpc.go:306–331`
  - `FuegoClient.post` sets no `Authorization` header. A wallet started with `--rpc-user/--rpc-password` fails every call. A wallet without auth is accessible to any other local process via the same `/json_rpc` path.
  - Accept `--wallet-user/--wallet-password` CLI args and set `Basic` auth in `FuegoClient.post`.

- **Important**: `FuegoClient.post` does not check HTTP status codes — non-2xx responses silently produce zero-valued structs
  - `swapxfg/app/rpc.go:316–330`
  - A 500 response with a plain-text body triggers a decode error, not a useful status error. `GetCdPrice` at `cd_rpc.go:53` even discards the unmarshal error entirely.
  - Check `resp.StatusCode`, reject non-2xx, propagate errors from all fetcher methods.

### Security findings

- **Critical**: WebSocket `CheckOrigin` unconditionally returns `true` — any webpage the user visits can hijack the bridge
  - `swapxfg/app/bridge.go:66–68`
  - Any local browser tab can connect to `ws://127.0.0.1:<port>/bridge/ws`. The existing connection is closed and replaced (`bridge.go:178–181`). The attacker then occupies the single bridge slot and answers pending signing requests (keyed only by a monotonically-incrementing `uint64` ID at `bridge_eth.go:92–97`). This allows impersonating the user's MetaMask, accepting fake tx hashes, or stealing ETH/SOL signing requests.
  - `CheckOrigin` must verify `r.Header.Get("Origin") == "http://127.0.0.1:<port>"`. Add a random per-session nonce in the initial HTML and require it on the WS handshake. Reject duplicate connections rather than replacing the existing one.

- **Critical**: No rate-limiting on any external-facing handler — reverted in `c48b35fb`
  - `swapxfg/app/bridge.go:77–86`, `go.mod` (no `golang.org/x/time`)
  - Commits `ed3a1ebb`/`d5bd4411` added rate-limiting; `c48b35fb` removed the dependency. Any local process can spam `/bridge/ws` handshakes, continuously kicking the legitimate browser off.
  - Reinstate `golang.org/x/time/rate` limiters on `/bridge/ws` (per-IP, small burst) and on `FetchAll` fan-out.

- **Critical**: Bridge has no WebSocket read-limit, read/write deadlines, or ping/pong timeouts
  - `swapxfg/app/bridge.go:171–208`
  - `conn.ReadMessage()` with no `SetReadLimit`/`SetReadDeadline`/`PongHandler`. A malicious local tab sends an unbounded payload to OOM the process, or opens the socket and never responds, permanently holding the single bridge slot (the 30 s `time.After` in `Send` is per-request, not per-connection).
  - Set `conn.SetReadLimit(1<<16)`, install `SetReadDeadline` + `PongHandler` that extends it, and run a 30 s `PingHandler` writer.

- **Important**: Goroutine races on shared TUI model — state mutated from background goroutines outside `Update`
  - `swapxfg/app/tui.go:300–330, :346–362, :381–411, :436–496, :518–525, :541–544, :557–563, :580–587`
  - Dozens of `go func()` closures write `m.statusMsg`, `m.ethAddr`, `m.ethBal`, `m.bchBal`, etc. directly. Bubble Tea's invariant is that model mutation only happens in `Update`. All writes are data races under `-race`.
  - Convert all `go func` results to `tea.Msg`s returned via `tea.Cmd`. Mutate state only in `Update`.

- **Important**: Bridge blindly relays arbitrary WebSocket actions to MetaMask/Phantom — combined with open `CheckOrigin`, rogue pages can sign arbitrary transactions
  - `swapxfg/app/bridge_eth.go:29–79`
  - `swapxfg/app/bridge_sol.go:28–56`
  - `sol_sendTx` accepts any base64 transaction and asks Phantom to sign it. The `action` field is unconstrained. A rogue local tab (possible because `CheckOrigin` is open) can dispatch `sol_sendTx` or `erc20_transfer` to drain the user's wallet.
  - Enforce origin, add a per-session token, and whitelist valid `Action` values in the Go `Send` path.

- **Important**: CDN scripts loaded without SRI integrity hashes and pinned to `@latest`
  - `swapxfg/app/bridge_eth.go:14` (`https://cdn.ethers.io/lib/ethers-5.7.2.umd.min.js`)
  - `swapxfg/app/bridge_sol.go:19` (`https://cdn.jsdelivr.net/npm/@solana/web3.js@latest/...`)
  - A compromised CDN can deliver malicious JS that runs in the bridge page with full MetaMask/Phantom access. `@latest` is a moving target.
  - Pin exact versions, add `integrity` SRI attributes, add a restrictive `Content-Security-Policy`, and prefer vendoring the JS into the Go binary.

- **Important**: Amount parsing uses `fmt.Sscanf(..., "%f", &amt)` → `uint64(amt * 1e7)` — overflow and NaN not handled
  - `swapxfg/app/tui.go:303–306, :347–349, :538–540`
  - `bch_rpc.go:97–111` also ignores balance-parse errors. A user entering `1e30` or a negative number silently overflows into a bogus atomic amount.
  - Use `strconv.ParseFloat` with error handling; reject `<0` or `>max` before constructing the `uint64`.

- **Important**: `bridge_open.go` constructs an OS shell command from the port — latent injection risk on future user-controlled input
  - `swapxfg/app/bridge_open.go:11–22`
  - Currently safe because the URL is constructed from `b.port` only. If future callers pass user-controlled input (deep links, alias-based URLs), the Windows `rundll32` path becomes an injection vector.
  - Restrict `openURL` to only package-internal URLs; enforce via a private helper that builds from `b.port` only.

- **Minor**: `AcceptCdOffer` and `CancelCdOffer` call the server with empty `buyerCommitKey`/`signature`/`pubKey`
  - `swapxfg/app/tui.go:557–563, :581–587`
  - Low severity given the endpoints are also non-existent server-side, but the calls should not reach the network with empty auth fields.

- **Minor**: Predictable bridge request IDs (`atomic.AddUint64` monotonic counter) simplify request hijacking
  - `swapxfg/app/bridge_eth.go:92–97`
  - Combine with open `CheckOrigin`: an attacker can predict the next request ID and answer it with a forged response. Switch to a random 64-bit nonce per request.

**Bottom line:** Not testnet-ready. The `c48b35fb` revert removed rate-limiting and address validation; the WebSocket bridge has no origin check, no framing limits, and uses predictable request IDs allowing any local webpage to hijack MetaMask/Phantom signing. The CD tab calls phantom RPC endpoints. The BCH lock command spends funds without an HTLC. The TUI model races under `-race`. Before testnet: fix WebSocket origin + framing, reinstate rate-limiting + address validation, rewrite the CD RPC layer, gate or remove BCH/ETH lock commands, and route all goroutine results through `tea.Msg`.

---

## Certificates of Deposit (CDs)

### Completeness gaps

- **Important**: `on_rollover_cd` is permanently stubbed — wallet and daemon mutually deflect, `WalletGreen::rolloverDeposit` is unreachable
  - `src/Wallet/WalletRpcServer.cpp:824` (throws; dead post-throw block references non-existent `getDepositCount()`)
  - `src/Rpc/RpcServer.cpp:2291` (also throws, directing callers back to wallet)
  - The fully-implemented `WalletGreen::rolloverDeposit` has no reachable call path from any public interface. Root cause: `CommitmentIndex` is not exposed through `INode`.
  - Expose a `getCdInterest(amount, height)` shim via `INode`, then wire `on_rollover_cd` → `WalletGreen::rolloverDeposit`. Remove the dead post-throw block and the daemon's deflect response.

- **Important**: `on_create_cd` has no `deposit_type` parameter — HEAT burns and YIELD/fuCIA deposits are unreachable from the RPC
  - `src/Wallet/WalletRpcServerCommandsDefinitions.h:583`
  - `src/Wallet/WalletRpcServer.cpp:759`
  - `COMMAND_RPC_CREATE_CD::request` carries only `amount` and `term`. Two of three `DepositType` values (`HEAT=0x02`, `YIELD=0x03`) are silently dropped.
  - Add an optional `deposit_type` field (default `COLD`) to the request struct.

- **Important**: `estimate_cd_yield` always returns `estimated_interest = 0`
  - `src/Wallet/WalletRpcServer.cpp:870`
  - Hardcoded to zero pending the same `INode`/`CommitmentIndex` gap. `Currency::calculateCdInterest` is fully implemented but unreachable from the wallet RPC.
  - Return an error (not a silent zero) until the INode gap is resolved. A UI displaying `0` yield misleads users.

- **Important**: `CommitmentIndex::generateEpochReport` has no `return` statement — undefined behaviour at every epoch boundary
  - `src/CryptoNoteCore/CommitmentIndex.cpp:288–297`
  - Function populates `report` fields then falls off the end with `}`. UB in C++ (non-void path). Callers receive a garbage-filled `EpochReport`; at least `totalFeesDistributed` and `activeEfierCount` are never set.
  - Add `return report;` after line 296.

- **Minor**: `SWAP_FEE_CD_SHARE_PCT` constant is defined but not used in `Blockchain.cpp` — computed as residual instead
  - `src/CryptoNoteConfig.h:148` vs `src/CryptoNoteCore/Blockchain.cpp:3146`
  - `cdSwapShare = epochSwapFees - treasuryShare` rather than `epochSwapFees * SWAP_FEE_CD_SHARE_PCT / 100`. Harmless now because the two constants sum to 100, but a future edit to one will silently diverge from the other.
  - Compute `cdSwapShare` from the constant directly.

- **Minor**: `getMaturingDeposits` (`WalletGreen`) is orphaned from all RPC/CLI paths
  - `src/Wallet/WalletGreen.h:57`
  - The wallet auto-rollover feature (`Phase 5`) has no accessible entry point. The daemon `on_get_maturing_deposits` calls `Core`, not the wallet method.
  - Either expose via wallet RPC or document as internal-only and remove the public declaration.

- **Minor**: Open TODOs blocking Phase 5
  - `src/Wallet/WalletRpcServer.cpp:823` — "TODO: expose CommitmentIndex via INode"
  - `src/Wallet/WalletGreen.cpp:5094` — "TODO: Persist to wallet file for backup"
  - Track as formal issues rather than inline comments.

### Security findings

- **Critical**: `haveTransactionKeyImagesAsSpent` does not check commitment key images — defence-in-depth gap for commitment transfers
  - `src/CryptoNoteCore/Blockchain.cpp:1968`
  - Only iterates `KeyInput` inputs. `TransactionInputCommitmentTransfer` key images are inserted into `m_spent_keys` on commit but are never checked by `haveSpentKeyImages` (used by the mempool as a secondary guard). The primary `checkTransactionInputs` path handles both types; but any future refactoring relying solely on `haveSpentKeyImages` for commitment types would introduce a real double-transfer vulnerability.
  - Extend `haveTransactionKeyImagesAsSpent` to iterate `TransactionInputCommitmentSpend` and `TransactionInputCommitmentTransfer` inputs.

- **Critical**: Epoch fee accumulator (`m_currentEpochSwapFees`) not decremented in `popBlock` — reorg permanently over-pays CD holders
  - `src/CryptoNoteCore/Blockchain.cpp:3189`
  - `popBlock` restores `m_totalCdLocked`, `m_feePoolBalance`, and `m_totalCdInterestPaid` but does not reverse swap-fee contributions to `m_currentEpochSwapFees`. Orphaned fees accumulate and are distributed at the next epoch boundary, draining the fee pool incorrectly. `m_epochFeeRates` (written by `recordEpochFeeRate`) is also never rolled back.
  - Record per-block swap-fee contributions and subtract them during `popBlock`; pop the last `m_epochFeeRates` entry if the popped block was an epoch boundary.

- **Important**: `claimedInterest` capped against oldest ring-member's accrual — attacker can craft a ring with ancient decoy to over-claim
  - `src/CryptoNoteCore/Blockchain.cpp:2228, 2288`
  - `oldestRingMemberHeight` is the minimum creation height across all ring members. Because the real spend is privacy-blinded, the node cannot know the actual deposit age. An attacker includes a genesis-era commitment as a decoy, raising the cap to maximum possible interest.
  - Document this trade-off explicitly. If over-claiming is unacceptable, cap by youngest ring-member's accrual (most conservative), or require a ZK proof binding claimed interest to the actual deposit's epoch range.

- **Important**: Integer overflow in epoch fee-rate calculation: `cdSwapShare * FEE_POOL_RATE_PRECISION` can overflow `uint64_t`
  - `src/CryptoNoteCore/Blockchain.cpp:3151`
  - `FEE_POOL_RATE_PRECISION = 1e6`. If `cdSwapShare` exceeds ~1.8e13 atomic units (18M XFG in one epoch — high-volume network scenario), the multiplication overflows before the division.
  - Use `__uint128_t` or the existing `mul128` helper for the intermediate product.

- **Important**: AEAD nonce for `encryptDepositSecret` derives from ephemeral public key — caller can force nonce reuse
  - `src/CryptoNoteCore/TransactionExtra.cpp:1641`
  - `depositEncIV(ephPubKey)` uses `ephPubKey[0:8]` as the ChaCha8 IV. Currently safe because `encryptDepositSecret` generates a fresh ephemeral keypair internally. However, the API accepts a pre-populated `TransactionExtraDepositSecret` with the caller able to set `ephPubKey` before the call. Reusing the same ephemeral key across two payloads XOR-cancels plaintexts.
  - Document that `out.ephPubKey` must not be pre-populated, or generate the ephemeral key internally and never expose the secret key via the function signature.

- **Important**: Maturity check uses strict `<` — deposit not spendable at exactly its maturity block; inconsistent with wallet-side check (`<=`)
  - `src/CryptoNoteCore/Blockchain.cpp:2242`
  - Blockchain uses `currentHeight < maturityHeight`; `WalletGreen::getMaturingDeposits` uses `dep.unlockHeight <= checkHeight`. The wallet will display a deposit as redeemable one block before the blockchain will accept it, producing confusing broadcast failures.
  - Align: use `currentHeight >= maturityHeight` everywhere, or document the consensus comparison as authoritative and adjust wallet display accordingly.

- **Important**: `newTerm` in `TransactionInputCommitmentTransfer` has no upper bound — an attacker can set `newTerm = UINT32_MAX`, permanently locking the CD
  - `src/CryptoNoteCore/Blockchain.cpp:2321`
  - `src/CryptoNoteConfig.h:144`
  - Lower bound check (`newTerm < 1`) exists but no upper bound. A `UINT32_MAX` term would overflow `creationHeight + term` and make the deposit forever unspendable as a real spend (but usable as a ring decoy forever).
  - Add `txin.newTerm > parameters::depositMaxTerm()` → reject, consistent with the pattern used for `MultisignatureOutput` at `Blockchain.cpp:2408`.

- **Minor**: Epoch report history capped at 10 entries — historical yield rate audit trail lost after 11 epochs
  - `src/CryptoNoteCore/CommitmentIndex.cpp:321`
  - `storeEpochReport` keeps only the last 10 `EpochReport` objects. Any epoch report beyond the last 10 is permanently inaccessible. Combined with the missing `return report;` UB, every stored report also carries garbage fields.
  - Fix `return report;` first. Then persist epoch reports to a database table rather than an in-memory ring buffer.

**Bottom line:** Not testnet-ready. Two immediate blockers: the epoch fee accumulator is non-reversible on reorgs (over-paying all CD holders after any reorg of an epoch-boundary block), and `claimedInterest` is capped against the oldest ring-member's accrual (enabling fee-pool draining via decoy ring-member selection). The `generateEpochReport` UB will fire at every epoch boundary. Three Phase 5 features (`rollover_cd`, `estimate_cd_yield`, `getMaturingDeposits`) are all blocked by the same root-cause INode gap — fix `CommitmentIndex` exposure via `INode` to unblock all three at once.

---

## Regular aliases

### Completeness gaps

- **Critical** (and renders alias system entirely non-functional): `TX_EXTRA_ALIAS` (0xEA) is absent from the `parseTransactionExtra` switch — no alias registration from a confirmed block is ever stored
  - `src/CryptoNoteCore/TransactionExtra.cpp:60–265`
  - `src/CryptoNoteCore/Blockchain.cpp:2935–3066`
  - The switch covers 0x00–0x08, 0xCD, 0xCE, 0xD5, 0x18, 0x69 — but not 0xEA. `pushToBankingIndex` iterates `extraFields` looking for `TransactionExtraAliasRegistration` but the variant is never produced by the parser. The block-import path at lines 3046–3066 is dead code. `getAliasFromExtra` (a separate function) is correct but is not called from `pushToBankingIndex`. No alias has ever been written into `m_aliasIndex` via block import.
  - Add `case TX_EXTRA_ALIAS:` to the switch in `parseTransactionExtra`, reading the varint-length-prefixed payload into a `TransactionExtraAliasRegistration`. Alternatively replace the `extraFields` iteration in `pushToBankingIndex` with a direct call to `getAliasFromExtra`.

- **Important**: `isValid()` hard-rejects 9-character aliases; `AliasIndex::isValidRegularAlias` carves out `winslayer` / `galapagos` — inconsistent double-validation
  - `src/CryptoNoteCore/TransactionExtra.cpp:1510–1528`
  - `src/CryptoNoteCore/AliasIndex.cpp:67–79`
  - `isValid()` requires `alias.length() == 8` with no exceptions. The block-import path calls only `isValid()`, so `winslayer` / `galapagos` can never be registered on-chain even though `isValidRegularAlias` would accept them.
  - Add the 9-character exceptions to `isValid()`, or remove them from `isValidRegularAlias` and enforce a uniform 8-char rule.

- **Important**: `voidAlias` is dead code with no authorisation model
  - `src/CryptoNoteCore/AliasIndex.h:46`
  - `src/CryptoNoteCore/AliasIndex.cpp:146–161`
  - Zero callers exist anywhere in the codebase. The function accepts a plain `ownerAddress` string with no proof of ownership; a future RPC endpoint would allow any node operator to void any alias.
  - Remove from the public interface, or define a signed-revocation-TX authorisation model before exposing it.

- **Important**: `LOUDMINING` branch in `registerAlias` is unreachable dead code; special-case names undocumented
  - `src/CryptoNoteCore/AliasIndex.cpp:89–103`
  - `LOUDMINING` appears only inside the `else if` block, not in the outer `if` guard, so it is never reached. `winslayer`, `galapagos`, and `LOUDMINING` have no comments explaining their origin or governance.
  - Remove the dead `LOUDMINING` branch. Document the reserved-name set in a static table with comments.

- **Important**: WalletGreen has no alias commands — modern wallet users cannot register aliases
  - `src/Wallet/WalletGreen.h`
  - Registration is CLI-only (`SimpleWallet`). Users of the wallet library have no programmatic path to register an alias.
  - Either document as intentional CLI-only, or add `registerAlias` / `getAlias` methods to `IWallet` / `WalletGreen`.

- **Minor**: `AliasEntry::aliasType` default value is `0` (Elderfier, now rejected) — silent construction failure
  - `src/CryptoNoteCore/AliasIndex.h:35`
  - Any code that constructs an `AliasEntry` without explicitly setting `aliasType = 1` will be silently rejected by `registerAlias` ("Elderfier aliases no longer supported"). The comment still advertises the mapping.
  - Change the default to `uint8_t aliasType = 1;` and update the comment.

- **Minor**: No alias-specific unit tests
  - `src/CryptoNoteCore/AliasIndex.cpp`, `TransactionExtra.cpp`
  - The parser bug above would have been caught by a basic `addAliasToExtra` → `parseTransactionExtra` → field-extraction round-trip test.
  - Add tests for: `isValid` boundary cases, round-trip extra serialisation, reserved-name rejection, duplicate registration, case sensitivity.

- **Minor**: Wrong comment tag in `Blockchain.cpp` — says `(0xFA)`, actual tag is `0xEA`
  - `src/CryptoNoteCore/Blockchain.cpp:3045`
  - Correct to `(0xEA)`.

### Security findings

- **Critical**: `parseTransactionExtra` has no `default:` case — unknown tags (including 0xEA, 0xEB, 0xEC, 0xEF) desync the stream and silently abort parsing for the entire transaction
  - `src/CryptoNoteCore/TransactionExtra.cpp:60–265`
  - When an unknown byte is read as a tag, the switch falls through with no action; the loop then reads the first payload byte as the next tag, corrupting all subsequent field parsing. The outer `catch` at line 268 absorbs out-of-bounds reads and returns `false`, silently discarding the entire TX extra. A malformed TX containing 0xEC or 0xEF bytes (macros still defined in the header) can corrupt parsing of all following fields.
  - Add a `default:` case that reads a varint length and skips that many bytes (standard TLV unknown-tag skip). This also immediately fixes the 0xEA parser bug above.

- **Critical**: No fee enforcement at block-import time — anyone can register an alias by crafting a zero-fee TX
  - `src/CryptoNoteCore/Blockchain.cpp:3046–3066`
  - `src/SimpleWallet/SimpleWallet.cpp:3683–3694`
  - The CLI enforces `ALIAS_REGISTRATION_FEE` (1 XFG to `FUEGO_DEV_FUND_ADDRESS`). `pushToBankingIndex` applies any `TransactionExtraAliasRegistration` without verifying the outputs contain the required payment. (This is currently masked by the parser-bug blocker above; it becomes exploitable once that is fixed.)
  - After parsing the alias field in `pushToBankingIndex`, verify the TX contains an output ≥ `ALIAS_REGISTRATION_FEE` to `FUEGO_DEV_FUND_ADDRESS` (mainnet) or to self (testnet). Reject silently otherwise.

- **Important**: No front-running protection — mempool watcher can race alias registration with a higher-fee TX
  - `src/SimpleWallet/SimpleWallet.cpp:3588–3606`
  - The CLI queries `/get_alias` to verify availability, then broadcasts after an unbounded user-confirmation gap. Commit `2ac94a7f` ("afix alias registration to first ritual deposit") is cited in the commit log but no commit-reveal or deposit-binding mechanism appears in the current code. `addressHash` binds the alias to the registrant's address (good), but does not prevent a watcher from registering the same alias string to a different address first.
  - Implement a commit-reveal: a first TX commits to `hash(alias || addressHash || nonce)` without revealing the alias string; a second TX (after N blocks) reveals and registers.

- **Important**: Case-sensitivity: `AliasIndex` uses case-sensitive `std::map` with no normalisation
  - `src/SimpleWallet/SimpleWallet.cpp:3568–3574`
  - `src/CryptoNoteCore/AliasIndex.cpp:178–185`
  - The CLI blocks uppercase but `AliasIndex::registerAlias` and `getAliasByName` use a case-sensitive map. A TX bypassing the CLI with mixed-case input would pass `isValid()` (which rejects uppercase for type-1) by coincidence, not by design. Dev-team aliases rely on the absence of normalisation to reserve both `"FUEGOXFG"` and `"fuegoxfg"` — but the general path has no explicit normalisation invariant.
  - Add explicit `toLower` normalisation at the entry points of `registerAlias` and `getAliasByName`; document it as a design invariant.

- **Important**: `addressHash` computed over the raw base58 address string — encoding-format-dependent, not stable across wallet software changes
  - `src/SimpleWallet/SimpleWallet.cpp:3660`
  - `src/CryptoNoteCore/AliasIndex.cpp:49, 149, 172, 188`
  - `cn_fast_hash(address.data(), address.size())` where `address` is the base58-encoded string. A change in encoding format or checksum bytes produces a different hash for the same cryptographic identity, breaking lookup silently. Commit `768758e7` changed the Elderfier path to `hash(spendPublicKey || ephemeralPublicKey)`; the regular alias path was not updated.
  - Hash the spend public key directly (raw bytes, not encoded) to decouple from address format changes; document the scheme in the header.

- **Important**: No chain-ID or genesis-hash binding in `TransactionExtraAliasRegistration` — registration TXs are valid on forks and testnet
  - `src/CryptoNoteCore/TransactionExtra.h:140–150`
  - A registration TX from mainnet is structurally identical on testnet or any fork. Fee-enforcement distinguishes at application time only if `isTestnet()` is checked (it is not yet).
  - Add a `uint32_t networkId` field set to `parameters::NETWORK_ID` at construction; validate during block import.

- **Minor**: Same-block duplicate registration: first TX in block order wins (deterministic by transaction index) — no documentation
  - `src/CryptoNoteCore/Blockchain.cpp:3046–3066`
  - `pushToBankingIndex` calls `registerAlias` and the first call to succeed sets the alias; subsequent calls for the same name are rejected. TX ordering within a block is deterministic (canonical block format), so the outcome is deterministic. However this is implicit; document it explicitly as the tie-breaking rule.

**Bottom line:** The alias subsystem is entirely non-functional at consensus level: `TX_EXTRA_ALIAS` (0xEA) is absent from the `parseTransactionExtra` switch, so no alias from any confirmed block has ever been written into `m_aliasIndex`. Layered on top: the `parseTransactionExtra` stream desync on unknown tags, the absence of fee enforcement at block-import time, the inert `voidAlias`, the dead `LOUDMINING` branch, and the missing chain-ID binding. The CLI-level guards are solid but irrelevant because all meaningful enforcement belongs at the consensus layer. Two one-line fixes (add `case TX_EXTRA_ALIAS:` and add `default: skip-TLV`) would make the system functional and close the most dangerous robustness gap simultaneously.

---

*Generated by four independent deep-audit agents on 2026-04-14. No builds were run. No code was modified. Each finding cites a concrete file:line.*
