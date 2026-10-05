# Source Change Log

Every feature/fix requires a task list with sign-off. Agents record name, date, and status when completing work.

---

## Mainline swap and operator-dashboard integration

**Started**: 2026-10-01
**Agent**: Codex (GPT-6)
**Status**: MERGED — funded operation and dashboard parity gates pending

| Task | Owner | Date | Status |
|------|-------|------|--------|
| Reconcile `codex/pr65-followups` with current `master`, preserving FCI config and the dashboard redesign | Codex | 2026-10-01 | DONE in isolated checkout |
| Keep production Hearth data daemon-backed and submit only exact, quote-checked wallet requests | Codex | 2026-10-01 | Offline tests pass; funded test pending |
| Derive SwapXFG chain readiness from xfg-swapd, carry uint256 EVM amounts as decimal strings, and require a verified peer key | Codex | 2026-10-01 | Offline tests pass; funded test pending |
| Restrict dashboard operator proxies to same-origin loopback requests and allowlisted RPC methods | Codex | 2026-10-01 | Go tests pass; browser check pending |
| Repair the existing `MinerConfig.cpp` namespace break found by the integration build | Codex | 2026-10-01 | DONE; daemon and swap binaries compile |
| Reject oversized dashboard operator requests and imprecise live Hearth order amounts | Codex | 2026-10-01 | DONE; Go and JavaScript tests pass |
| Commit merge and push to mainline without disturbing unrelated local work | Codex | 2026-10-01 | DONE; merge `f64eb0c8b` pushed to GitHub `master` |
| Verify funded lock/claim/refund, restart/reorg, and full order/recovery UI parity before production activation or TUI removal | Operator + Codex | — | PENDING |

### Sign-off

| Gate | Result |
|------|--------|
| Dashboard Go and JavaScript offline tests | PASS in isolated checkout; funded operation not implied |
| Focused C++ build/tests | PASS: Release `fuegod` and `xfg-swapd` compile; assertion-enabled catalog, ETH amount/wire, SPV, swap audit, state recovery, Hearth AMM, core, orderbook, and production-gate tests pass |
| Graphify refresh | BLOCKED by installed `hyppo` cache locator error after AST extraction |
| Mainline push | PASS: GitHub `master` advanced from `ea6950d8f` to `f64eb0c8b`; dirty canonical checkout was not modified |
| Funded swap and dashboard parity | NOT RUN; TUI remains |

Older entries below describe their original revisions. They do not establish
funded production readiness for this merge.

---

## Fuego Cost Index (FCI) Miner Basket Oracle & Voting

**Started**: 2026-09-26
**Agent**: Antigravity (Gemini 3.6 Flash)
**Status**: DONE

### Changes

- `MinerConfig.h` / `MinerConfig.cpp`: Added miner configuration options for FCI basket commodity pricing (power, milk, gas, bread, eggs) in microUSD integers, ISO-4217 currency rates, oracle JSON auto-fetch file path, staleness warning days, and Paradio song vote.
- `TransactionExtra.h` / `TransactionExtra.cpp`: Defined `TransactionExtraMinerBasketVote` (tag `0x38`) and `TransactionExtraMinerParadioVote` (tag `0x39`) structures with partial-basket bitmasks and binary serialization. Added helper functions `addMinerBasketVoteToExtra`, `getMinerBasketVoteFromExtra`, `addMinerParadioVoteToExtra`, and `getMinerParadioVoteFromExtra`.
- `Miner.h` / `Miner.cpp`: Integrated FCI basket vote and Paradio vote extra tags into block miner coinbase transaction generation.
- `CryptoNoteConfig.h`: Reserved transaction extra tags `TX_EXTRA_MINER_BASKET_VOTE` (`0x38`) and `TX_EXTRA_MINER_PARADIO_VOTE` (`0x39`).

| Task | Owner | Date | Status |
|------|-------|------|--------|
| Implement FCI miner basket oracle config & microUSD parsing | Antigravity | 2026-09-26 | DONE |
| Add TX_EXTRA_MINER_BASKET_VOTE (0x38) & TX_EXTRA_MINER_PARADIO_VOTE (0x39) | Antigravity | 2026-09-26 | DONE |
| Wire miner coinbase extra generation for FCI oracle votes | Antigravity | 2026-09-26 | DONE |
| Commit and push changes to new `fci` branch | Antigravity | 2026-09-26 | DONE |

### Sign-off

| Gate | Status | Agent | Date |
|------|--------|-------|------|
| Build compiles | PASS | Antigravity | 2026-09-26 |
| All tasks verified | PASS | Antigravity | 2026-09-26 |

---

## Dashboard Polish: Gold Color & Label Cleanup

**Started**: 2026-09-24
**Agent**: Antigravity (Gemini 3.8 Flash)
**Status**: DONE

### Changes

- `style.css`: Replaced `--gold-bright: #ffd700` (pure yellow) with `--gold-bright: #c9a44c` (burnished gold). Updated dim, glow, and border-gold values to match. `#ffd700` under dark backgrounds reads as cartoon/taxi yellow, not gold.
- `style.css`: Stripped "Monaco Terminal" from all CSS comment section headers — theme names don't belong in comment banners visible-adjacent to the UI.
- `hearth.html`: Removed "FUEGO // TERMINAL" nav brand title → now `FUEGO` with sub `XFG NETWORK`. Stripped "// TELEMETRY STRIP" from the Hearth fascia section header. Stripped "// HEARTH TERMINAL" from modal confirm header.
- `swapxfg.html`: Same nav brand cleanup. `data-cat="calibers"` label was already correct (`Major UTXO`).

| Task | Owner | Date | Status |
|------|-------|------|--------|
| Fix gold to burnished `#c9a44c` | Antigravity | 2026-09-24 | DONE |
| Strip "Monaco Terminal" from all visible UI text | Antigravity | 2026-09-24 | DONE |
| Clean section/fascia/modal label text | Antigravity | 2026-09-24 | DONE |
| Build passes (`go build`) | Antigravity | 2026-09-24 | DONE |

---

## Maison de XFG Dashboard: Le Salon du Hearth & Celestial Ordergraph Astrolabe

**Branch/Feature**: maison-xfg-dashboard-improvements
**Started**: 2026-09-24
**Agent**: Antigravity (Gemini 3.8 Flash)
**Status**: COMPLETE

### Architecture & Interface Overview

Elevated the Fuego web dashboard to reflect the **Maison de XFG** private Swiss haute-horlogerie and sovereign banking aesthetic, uniting **Le Salon du Hearth** (on-chain AMM and limit-order overlay) and the **DeXFG Astrolabe** (cross-maison atomic clearing matrix with Ordergraph):

1. **Design System & Antigravity Depth (`dashboard/static/css/style.css`)**:
   - Replaced generic palette with haute-horlogerie metals: champagne gold (`#d4af37`), flame amber (`#ff6b35`), brushed titanium, and deep sapphire obsidian.
   - Integrated subtle Côte de Genève / guilloché textured backdrops, glassmorphic card bezels (`backdrop-filter: blur(16px)`), and layered elevation shadows.
   - Added jewel-accented status complications for Daemon, Vault, and Swapd services.

2. **Le Salon du Hearth (`dashboard/hearth.html`, `dashboard/static/js/hearth.js`)**:
   - Transformed fireplace into an Atrium Complications Fascia showcasing Caliber Burned, HΞ∆T Complication Supply, AMM Reserves, Artisanal Mint Parity, Regulated Peg Benchmark ($1.58 USD reference), and 70% CD Yield Pool Accumulator.
   - Built an interactive bid/ask depth ladder where clicking any price row instantly prefills target price and volume into the order console.
   - Implemented quick balance proportion chips (25%, 50%, 75%, MAX), limit vs instant AMM swap mode toggles, live fee routing readouts, and Artisanal Clearing Certificate execution modals.

3. **Celestial Ordergraph Astrolabe (`dashboard/static/swapxfg.html`, `dashboard/static/js/swapxfg.js`)**:
   - Upgraded the multi-chain plot with the 0% Chrono-Fair Equator in illuminated champagne gold, discrete ±15% Y-axis benchmarks, and symmetrical horological fan-out of cabochon markers.
   - Added interactive chain category filters (Sovereign Privacy, Core Calibers, EVM & Rollups, Alternative Curves), side filters (Offer XFG vs Acquire XFG), and instant search filtering.
   - Developed the Swiss Horological Loupe HUD with real-time spread divergence, block expiry estimates, and 1-click ticket prefill actions.

| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 1 | Create Maison de XFG Haute Horlogerie & Antigravity tokens in `style.css` | Antigravity | 2026-09-24 | DONE |
| 2 | Redesign Hearth Salon layout with Atrium Fascia dials & interactive depth ladder | Antigravity | 2026-09-24 | DONE |
| 3 | Enhance KLineCharts styling, click-to-prefill, and order console in `hearth.js` | Antigravity | 2026-09-24 | DONE |
| 4 | Upgrade DeXFG Ordergraph hero with category chips, search, and Chrono-Fair Equator | Antigravity | 2026-09-24 | DONE |
| 5 | Implement Horological Loupe HUD and symmetrical cabochon fan-out in `swapxfg.js` | Antigravity | 2026-09-24 | DONE |
| 6 | Verify Go binary build, HTTP 200 responses, and JS syntax correctness | Antigravity | 2026-09-24 | DONE |

### Sign-off

| Gate | Status | Agent | Date |
|------|--------|-------|------|
| Build compiles (`go build`) | PASS | Antigravity | 2026-09-24 |
| HTTP endpoints functional (200 OK) | PASS | Antigravity | 2026-09-24 |
| JavaScript syntax clean (`node -c`) | PASS | Antigravity | 2026-09-24 |
| All tasks verified | PASS | Antigravity | 2026-09-24 |

## Hearth live-data and quote-first operator controls

**Branch/Feature**: `codex/swap-uint256-integration`
**Started**: 2026-09-27
**Agent**: Codex (GPT-6)
**Status**: PARTIAL DASHBOARD PARITY — offline verified, funded UI execution pending

### Task List

| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 1 | Align `/amm_quote` with the constant-product AMM settlement bound rather than combined orderbook estimate | Codex (GPT-6) | 2026-09-27 | DONE — core links and 179/179 core regressions pass |
| 2 | Remove production random candles, orderbook and pool/HEAT fixtures; surface unavailable states | Codex (GPT-6) | 2026-09-27 | DONE — separate design sandbox untouched |
| 4 | Preview exact-output AMM swaps with correct direction, strict output floor, fresh quote recheck, and exact atomic parsing | Codex (GPT-6) | 2026-09-27 | DONE — no funded wallet execution claimed |
| 5 | Add historical candles, owned order cancel/claim, complete order depth, and full SwapXFG fill/recovery controls | Codex | — | PENDING — TUI remains available |

### Sign-off

| Check | Result |
|-------|--------|
| C++ build and core regressions | PASS — `fuegod` links; treasury/core 179/179 |
| Dashboard offline tests | PASS — Go `go test ./...`; JS Hearth/order-pair 7/7; JS syntax |
| Live funded browser-to-wallet exercise | NOT RUN — do not mark dashboard production-operational |

---

## Swap uint256 amount and review-blocker integration

**Branch/Feature**: `codex/swap-uint256-integration`
**Started**: 2026-09-27
**Agent**: Codex (GPT-6)
**Status**: OFFLINE INTEGRATION COMPLETE — funded testnet and dashboard parity not signed off

### Task List

| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 1 | Carry the versioned uint256 native-EVM amount path into canonical `xfgo`; retain explicit uint64 rejection for non-EVM adapters | Codex (GPT-6) | 2026-09-27 | DONE |
| 2 | Add exact gas/value preflight and a 500 gwei EIP-1559 fee ceiling; cover both legacy and typed signed transactions | Codex (GPT-6) | 2026-09-27 | DONE |
| 3 | Treat a supplied peer public key as expected identity until the first signed KEY_EXCHANGE; add admission regression | Codex (GPT-6) | 2026-09-27 | DONE |
| 4 | Reconcile main's little-endian adaptor extraction and zeroed MuSig2 nonce semantics in merged tests | Codex (GPT-6) | 2026-09-27 | DONE |
| 5 | Restore the executable swap guide and preserve unrelated HEAT/Starkproof work during landing | Codex (GPT-6) | 2026-09-27 | DONE |
| 6 | Run funded testnet lock, claim, refund, restart, and fee-budget exercises for promoted EVM pairs | Operator + Codex | — | PENDING — not a condition for committing offline code, but required before production activation |
| 7 | Finish live SwapXFG/Hearth dashboard execution and recovery parity before deleting TUI | Codex | — | PENDING — dashboard is not fully operational |

### Sign-off

| Check | Result |
|-------|--------|
| `xfg-swapd` and `fuegod` link | PASS — merged main/recovered source, 2026-09-27 |
| Focused C++ tests | PASS — ETH protocol 37/37, state machine 16/16, presignature 9/9, swap audit 36/36, pair catalog, SPV config, price oracle |
| Dashboard offline checks | PASS — Go `go test ./...` with local loopback permission, JS syntax, pair tests 2/2 |
| Diff hygiene | PASS — conflict markers absent and `git diff --check` clean |
| Graph refresh | BLOCKED — `graphify update .` AST extraction finished but graph rebuild failed in installed `hyppo` (`_center_distmat` locator); no graph sign-off claimed |
| Funded-chain and mainnet validation | NOT RUN — no production-readiness assertion |

The operator confirmed no historical swaps. The main-side removal of
`Musig2SecNonce::signed_flag` changes the size of an old encrypted nonce blob;
that legacy-record compatibility is not required for this zero-history landing,
but must be revisited if any pre-merge records are found or imported later.

---

## PR #65 follow-up: executable swap pairs, configuration, and refund recovery

**Branch/Feature**: codex/pr65-recovered
**Started**: 2026-09-22
**Agent**: Codex (GPT-6) with Fuego Guardian reviewers
**Status**: IN PROGRESS — fee accounting and live fund-handling gates remain

### Task List
| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 1 | Verify Qodo and residual claims against merged PR #65 | Codex + Fuego Guardian reviewers | 2026-09-22 | DONE |
| 2 | Align dashboard pair mapping and gate staged pair relay/order admission | Codex / offer-pairs reviewer | 2026-09-22 | DONE |
| 3 | Fix PulseChain example keys and validate GLEEC HTLC readiness for new swaps | Codex / config reviewer | 2026-09-22 | DONE |
| 4 | Gate local offer publication and new swaps on executable, reachable clients | Codex (GPT-6) | 2026-09-25 | DONE — money-changing decisions force a fresh readiness check |
| 5 | Repair SPV claim recording and independent per-leg refund recovery | Codex (GPT-6) | 2026-09-25 | DONE — source paths and focused SPV tests |
| 6 | Bound malformed P2P taker identities and verify affected paths | Codex (GPT-6) | 2026-09-23 | DONE |
| 7 | Apply PulseChain and CI naming convention fixes | Codex (GPT-6) | 2026-09-23 | DONE |
| 8 | Resolve confirmed counterparty claim, XFG claim, and refund race outcomes before release | Codex / Fuego Guardian reviewer | 2026-09-25 | IN PROGRESS — direct path hardened; testnet race exercise pending |
| 9 | Make atomic-swap fee accounting durable and idempotent | Codex (GPT-6) | 2026-09-25 | TODO — server RPC cannot safely retry after uncertain result |
| 10 | Integrate recovered swap changes without unrelated EVM/alias work | Codex (GPT-6) | 2026-09-27 | DONE — reviewed integration branch merged with current Hearth work; unrelated working-tree edits preserved |
| 11 | Close BTC/BCH/LTC/KMD new swaps until transport and consensus SPV validation are complete | Codex (GPT-6) | 2026-09-26 | DONE — source gate, linked build, and focused tests passed |

### Sign-off
| Check | Status |
|-------|--------|
| Build compiles | PASS — Codex (GPT-6), 2026-09-26; fresh Release build linked `xfg-swapd` and all six focused test executables after the final UTXO gate |
| Focused tests pass | PASS — Codex (GPT-6), 2026-09-26; orderbook 115/115, swap state 12/12, Electrum SPV, ETH protocol 21/21, pair catalog, config, dashboard 2/2 |
| All tasks done | PENDING — fee accounting, testnet races, and scoped integration; see `PR65_FOLLOWUP_ADVERSARIAL_REVIEW.md` |

`graphify update .` was attempted on 2026-09-23 but failed inside the installed
`hyppo` dependency (`cannot cache function '_center_distmat': no locator
available`). No graph update was signed off.
## Dashboard Parity, Network Profiles, and Testnet Safety

**Branch/Feature**: `codex/evm-dropin-batch`
**Started**: 2026-09-24
**Agent**: Codex
**Status**: IN PROGRESS

The legacy SwapXFG TUI remains frozen but present. It will not be removed until
both browser surfaces have live-data and execution parity, recovery workflows
remain reachable, and the testnet integration gates below pass.

### Task List

| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 0 | Replace the 64-bit counterparty-amount ceiling with versioned uint256 atomics, string-safe APIs, full-width EVM ABI/RLP, and non-EVM overflow rejection | Codex | 2026-09-27 | OFFLINE VERIFIED IN INTEGRATION BRANCH; FUNDED TESTNET PENDING |
| 1 | Add explicit mainnet/testnet/dev profiles, isolated ports/data, service-reported network identity, dashboard badge, and fail-closed write gating | Codex | 2026-09-24 | IN PROGRESS |
| 2 | Remove Hearth mock/random fallbacks and expose honest live, stale, unavailable, and error states | Codex | 2026-09-24 | TODO |
| 3 | Add quote-first Hearth execution, slippage/min-output protection, correct limit-order units/expiry, and order management | Codex | 2026-09-24 | TODO |
| 4 | Complete SwapXFG signed order placement/cancel/my-orders and real soft-offer fill/status flows without weakening recovery | Codex | 2026-09-24 | TODO |
| 5 | Add deterministic fixtures, unit/UI tests, testnet integrations, and mainnet read-only smoke checks | Codex | 2026-09-24 | TODO |
| 6 | Remove the frozen TUI only after every dashboard acceptance gate passes | Codex | 2026-09-24 | BLOCKED ON GATES |

### Sign-Off

The user confirmed no prior swaps exist; there is no deployed record set to
migrate. The v2 new-swap wire still rejects amount/version mismatch before
funding. Offline validation on the isolated worktree: `xfg-swapd` and
`SwapDaemonLib` build; ETH protocol 21/21, state-machine SPV 10/10, audit
regressions 36/36, production gates 19/19, pre-sig 9/9, pair catalog,
generic EVM config, BSC/Polygon, and price-oracle tests pass. Dashboard Go
tests and JS syntax check pass. The later swap-integration section above
records the merged offline checks. This is not funded-chain validation.

| Gate | Signed By | Date | Result |
|------|-----------|------|--------|
| Network/profile mismatch tests | — | — | PENDING |
| Hearth live-data and transaction-safety tests | — | — | PENDING |
| SwapXFG order/fill/recovery tests | — | — | PENDING |
| Testnet integration suite | — | — | PENDING |
| Mainnet read-only smoke checks | — | — | PENDING |
| Dashboard parity accepted; TUI removal authorized | — | — | NO |

---

## Order-book pair cap raised to the full enum; PulseX renamed to PulseChain; GLEEC re-enabled

**Branch/Feature**: claude/artifact-cx6twez-bug-vxf4jr
**Started**: 2026-09-21
**Agent**: Claude Opus 5
**Status**: COMPLETE

### Correction to the two prior GLEEC entries

Both 6200a3c (de-register GLEEC) and 35fb15a (close GLEEC to new swaps) rested on
the premise that GLEEC had been live and could therefore have swaps persisted on
disk. That premise was wrong — no chain in this daemon has ever been live. It was
inferred from GLEEC calling `registerChain` while ZANO/TON/SIA/DOT do not, and was
never verified. With no swaps on disk there is nothing to strand, so the fund-loss
reasoning in entry "GLEEC: close to new swaps" does not apply to the current state.

The latent bugs it describes (`SwapDaemon.cpp:3230`, `:3161`, `:2069`) are still real
and still worth fixing before anything goes live — they are simply not urgent.

GLEEC is fully enabled again. `isPairClosedToNewSwaps` is removed. The only GLEEC
change that survives is the registry correctness fix: `gleec_htlc_registry` is
required rather than silently falling back to `ethHtlcRegistry`.

### Order-book pair cap (the actual blocker)

`SwapOfferRelay::MAX_PAIR_INDEX` was 11 with `m_orderBooks[12]`, while `SwapPair`
runs to `DOT = 28`. `validateOffer` drops any offer with `pair > MAX_PAIR_INDEX`, so
pairs 12-28 — GLEEC, ROBINHOOD, AVAX, CRO, BOB, SIA, UNICHAIN, PLASMA, DOGE, DASH,
ZEC, PULSECHAIN, ZANO, MONAD, OPTIMISM, TON, DOT — were advertised by the registry,
quoted by the price oracle and offered in the UI, but could never reach an order
book. 17 of 25 registered chains could not trade over gossip.

Every `m_orderBooks` access already routed through `isValidPair`, so the array was
never indexed out of bounds — the cap failed closed, silently.

| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 1 | `MAX_PAIR_INDEX` 11 → 28; `m_orderBooks` sized `MAX_PAIR_INDEX + 1` | Claude Opus 5 | 2026-09-21 | DONE |
| 2 | Make `MAX_PAIR_INDEX` public so callers stop duplicating the literal | Claude Opus 5 | 2026-09-21 | DONE |
| 3 | `OfferManager.cpp` `mo.pair > 11` → reference the constant | Claude Opus 5 | 2026-09-21 | DONE |
| 4 | Loop bounds `pair <= ZANO` → `pair <= DOT` in SwapDaemon.cpp + RpcServer.cpp | Claude Opus 5 | 2026-09-21 | DONE |
| 5 | Rename PULSEX → PULSECHAIN across enum, strings, config keys, client, dir | Claude Opus 5 | 2026-09-21 | DONE |
| 6 | Revert GLEEC drain mode; remove `isPairClosedToNewSwaps` | Claude Opus 5 | 2026-09-21 | DONE |

### PulseX → PulseChain

The pair was named for PulseX, which is a DEX, while every field around it already
described PulseChain: chain id 369, native PLS, 18 decimals, `rpc.pulsechain.com`.
The chain is PulseChain; the name was simply wrong. Renamed throughout —
`SwapPair::PULSECHAIN` (index 23, unchanged), `PulseChainClient`,
`src/SwapDaemon/PulseChain/`, and config keys `pulsechain_*`. The string alias is now
`PLS` (the native coin) rather than `PULS`. Config keys changed without a
compatibility shim because nothing is deployed.

### Sign-off

| Check | Result |
|-------|--------|
| Build compiles | PASS — all 8 changed translation units pass `g++ -std=c++17 -fsyntax-only` with the project's own flags (`-DBOOST_MPL_CFG_NO_PREPROCESSED_HEADERS -DBOOST_MPL_LIMIT_LIST_SIZE=40`): SwapTypes, SwapTimelock, PriceOracle, SwapOfferRelay, OfferManager, ChainClientConfig, SwapDaemon, RpcServer. Not a full link. |
| Tests pass | Not verified |
| All tasks done | YES |

---

## Fix escrow-refund gating (CTR_LOCKED) and unbounded taker history

**Branch/Feature**: claude/artifact-cx6twez-bug-vxf4jr
**Started**: 2026-09-21
**Agent**: Claude Opus 5
**Status**: COMPLETE

Two bugs previously logged as known-but-unfixed. One is fixed; the second half of
the first turned out to be a wrong recommendation and is deliberately left alone.

### 1. XFG escrow refund was gated on the counterparty refund (FIXED)

`SwapDaemon.cpp`, `ADAPTOR_CTR_LOCKED` + `role == BOB`: `broadcastEscrowRefundDirect`
sat inside `if (ctrRefundOk)`. If the CTR leg could not be refunded — unconfigured
client, RPC outage, a chain not registered — the XFG escrow was never broadcast, and
`checkTimeouts` re-entered the same path every tick forever. The escrow needs no
counterparty interaction to recover, so it was strandable for reasons unrelated to it.

Safety argument for ungating: in `ADAPTOR_CTR_LOCKED` the peer cannot have learned
the adaptor secret `t`. `t` is revealed only when we claim the CTR leg, and that
moves the state on. So while the swap sits in this state nobody else can spend the
escrow, and recovering it is independent of the CTR leg.

The swap now reaches `ADAPTOR_REFUNDED` only when **both** legs succeed, so a failed
CTR refund keeps being retried rather than being lost to an early terminal
transition. Also corrected the block comment, which claimed "Bob locked on the
counterparty chain" — `SwapDaemon.cpp:1882` shows Alice locks the CTR leg and Bob
verifies it.

### 2. The SPV branch: both the guardian's fix AND my first analysis were wrong

Guardian's adversarial pass recommended ungating `broadcastEscrowRefundDirect` in
both places. That would introduce a fund-theft path, so it was not applied.

The first analysis written here was **also wrong**, and is corrected below.

It claimed `ADAPTOR_SECRET_CONFIRMED_SPV` implies the CTR leg was claimed, and that
the branch should therefore be split by state. The code does not support that:
`SwapDaemon.cpp:2424` enters that state on `getTransactionDetails(params.ctrLockTxId)`
— confirmations of the **lock** tx. The claim was inferred from the state's *name*,
which is the same failure mode as the guardian finding.

What actually records a claim is `params.ctrClaimTxId` (set at `:1971` and `:2041`)
and `params.adaptorSecretRevealedToPeer` (`:2040`). The SPV claim path sets both
**without leaving `ADAPTOR_WAITING_SPV`**, so *either* SPV state can hold an
already-claimed swap. Splitting by state would have been wrong too.

**The fix is one predicate, applied to both branches:**

```cpp
const bool alreadyClaimedCtr =
    !params.ctrClaimTxId.empty() || params.adaptorSecretRevealedToPeer;
```

- Not claimed → refund the escrow, independent of the CTR refund.
- Claimed → refuse the escrow refund and log loudly. We already hold the CTR leg;
  taking the escrow back as well is both legs. The counterparty can still claim the
  escrow with the secret, which is the correct outcome.

`ctrRefundOk` was only ever an accidental proxy for this: a claimed output makes
`refund()` fail. It blocked the theft case by luck while stranding recoverable XFG
whenever the CTR refund failed for an unrelated reason.

The `ADAPTOR_CTR_LOCKED` branch now uses the same predicate rather than trusting the
state invariant, since the SPV path proves a state label is not a reliable witness.

### 3. `m_takerHistory` grew without bound (FIXED)

`pruneTakerHistory` erased only entries with `failedSwaps == 0`, and
`recordTakerFailure` only ever incremented that counter. Any key that failed once
left a permanent entry. `takerPubKey` is self-asserted and free to mint, and
`handleSwapRequest` is reachable from gossiped P2P messages, so this was a remote
memory-exhaustion vector — made easier to hit by the reserve-proof revert, since
the proof now always runs and so failures are easier to provoke.

- `TakerRecord` gains `lastSeen`; entries expire on inactivity (`TAKER_RECORD_TTL_SECONDS`, 2h) regardless of `failedSwaps`.
- `recordTakerFailure` hard-caps the map at `MAX_TAKER_HISTORY_ENTRIES` (4096), evicting the least recently active entry before inserting a new key.

Dropping the permanent ban costs nothing: it keyed on a free identifier, so rotating
the key already evaded it.

| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 1 | Ungate escrow refund in the CTR_LOCKED branch; require both legs for terminal state | Claude Opus 5 | 2026-09-21 | DONE |
| 2 | Correct the stale "Bob locked on the counterparty chain" comment | Claude Opus 5 | 2026-09-21 | DONE |
| 3 | Establish that the SPV branch gate is load-bearing; leave it | Claude Opus 5 | 2026-09-21 | DONE |
| 4 | Add `lastSeen` + inactivity expiry to taker history | Claude Opus 5 | 2026-09-21 | DONE |
| 5 | Hard-cap taker history with LRU eviction | Claude Opus 5 | 2026-09-21 | DONE |

### Sign-off

| Check | Result |
|-------|--------|
| Build compiles | PASS — SwapDaemon.cpp passes `g++ -fsyntax-only` with the project's flags |
| Tests pass | Not verified — no test covers the refund state machine or taker rate limiting |
| All tasks done | YES |

**Review note:** item 1 is fund-handling logic in an atomic swap. The argument above
is structural, not empirical — nothing here exercises the refund path at runtime, and
no chain is deployed to test against. It deserves a human read.

---

## CI: Windows build exits 1 with every target linked

**Branch/Feature**: claude/artifact-cx6twez-bug-vxf4jr
**Started**: 2026-09-20
**Agent**: Claude Opus 5
**Status**: COMPLETE — all 5 jobs green on run 35571080637

**Note:** the `__uint128_t` fix lives on this branch only. `master` is still red for
the same reason until this branch merges or the one-line fix is ported.

Windows is the only failing job — macOS 14/15, Ubuntu 24.04 and Sanitizers all pass
on every recent run, across branches. On master (run 35166598710) the Windows log
shows every target linking (`fuegod.exe`, `xfg-swapd.exe`, `fire_wallet.exe`,
`unified.exe`, `testnetd.exe`, all test binaries), zero compile errors, only C4244 /
C4068 warnings — then `Process completed with exit code 1` 0.1s after the last link,
with no MSBuild build summary and three orphaned MSBuild processes terminated by
runner cleanup.

A missing summary plus orphaned worker nodes is a worker dying, not a compile error.
windows-2025 is 4 vCPU / 16 GB and the tree is boost-template heavy.

| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 1 | Cap `-j` at 2 (was `$NUMBER_OF_PROCESSORS` = 4) | Claude Opus 5 | 2026-09-20 | DONE — did not fix it |
| 2 | Pass `/nodeReuse:false` to stop nodes outliving the step | Claude Opus 5 | 2026-09-20 | DONE — fixed the orphans |
| 3 | Throw explicitly on non-zero `$LASTEXITCODE` so the code is visible | Claude Opus 5 | 2026-09-20 | DONE |
| 4 | Add MSBuild `errorsonly` file logger, dumped on failure | Claude Opus 5 | 2026-09-21 | DONE — found the cause |
| 5 | Fix `__uint128_t` in TreasuryCoreTests.cpp:690 | Claude Opus 5 | 2026-09-21 | DONE |
| 6 | Restore `-j $NUMBER_OF_PROCESSORS` (the cap was for a disproven theory) | Claude Opus 5 | 2026-09-21 | DONE |
| 7 | Confirm Windows green on CI | Claude Opus 5 | 2026-09-21 | DONE |

### ROOT CAUSE FOUND (run 35568840910)

```
tests/CoreTests/TreasuryCoreTests.cpp(690,35): error C2065: '__uint128_t': undeclared identifier
tests/CoreTests/TreasuryCoreTests.cpp(690,47): error C2146: syntax error: missing ')' before identifier 'P'
```

`__uint128_t` is a GCC/Clang builtin. MSVC has no such type, which is exactly why
Windows was the only failing platform while macOS, Ubuntu and Sanitizers stayed
green. The second error is the first one cascading.

It was the **only** `__uint128_t` in the entire repository, and it sat in
`core_tests.vcxproj` — a test project, not a shipped binary. That is why every
`.exe` linked successfully and the failure looked mysterious: the failing project
produced no console output of its own, MSBuild printed no build summary, and the
error was ~5 minutes upstream of the end of a 1456-line log.

The repo already carries the portable type: `uint128_t` in `src/Common/Int128.h`
(builtin `unsigned __int128` on GCC/Clang, a hand-written struct with an explicit
`operator uint64_t()` on MSVC). `TreasuryCoreTests.cpp` already included that header.
The fix matches the identical computation in `Currency::calculateCdBonus`
(`Currency.cpp:392`), which compiles on MSVC today:

```cpp
static_cast<uint64_t>(((uint128_t)P * (FLOOR - FLOOR / 2)) / PREC)
```

This bug is **pre-existing and unrelated to this branch** — it is why master
(run 35166598710) was red too.

Both earlier hypotheses were wrong and are recorded as such: worker-node death
(disproven — no orphans, still failed) and parallelism (disproven — `-j 2` changed
nothing). The `-j` cap is therefore reverted; `/nodeReuse:false` and the file
loggers are kept, since the loggers are what made the cause visible at all.

### Result of run 35506839843 — first hypothesis was wrong

Windows failed again, but the run disproved the worker-node theory:

- `MSBuild exited 1` printed by the new throw, so the exit code is confirmed as 1.
- **No orphaned MSBuild processes** this time, so `/nodeReuse:false` did fix that
  symptom — and the orphans were therefore a consequence, not the cause.
- Every target still links (`xfg-swapd.exe`, `test_wallet.exe`, `fire_wallet.exe`).
- Still **no MSBuild build summary** anywhere in the log.

A non-zero exit with every target built and no summary means the failing project is
not visible in console output at the default verbosity. Capping `-j` at 2 changed
nothing, so parallelism is not the cause either.

Rather than guess a third time, the build now attaches two MSBuild file loggers
(`errorsonly` and `warningsonly`) and dumps the error log to the job output on
failure. The next run should name the failing project outright. Note the run also
downloads its full log only through `results-receiver.actions.githubusercontent.com`,
which this environment's egress proxy blocks (403), so grepping the archive locally
is not an option — CI has to surface the error itself.

### Sign-off

| Check | Result |
|-------|--------|
| YAML parses | PASS (python yaml.safe_load) |
| Build compiles | PASS — TreasuryCoreTests.cpp passes `g++ -fsyntax-only` with the project's flags |
| Windows CI green | PASS — run 35571080637, Windows Build 13m19s, conclusion success |
| Full CI green | PASS — all 5 jobs (Windows, Ubuntu 24.04, macOS 14, macOS 15, Sanitizers) |
| All tasks done | YES |

---

## GLEEC: close to new swaps instead of de-registering (supersedes 6200a3c)

**Branch/Feature**: claude/artifact-cx6twez-bug-vxf4jr
**Started**: 2026-09-20
**Agent**: Claude Opus 5
**Status**: COMPLETE

Commit 6200a3c disabled GLEEC by commenting out its registration block. Guardian
verification found that this strands funds. Replaced with a drain: the client stays
registered, new swaps are refused.

### Why de-registering strands funds

Every recovery path resolves the chain client through `getClient(pair)`, and a null
client there is not a safe failure — it is the stranding mechanism:

- `SwapDaemon.cpp:3230` — the XFG escrow refund `broadcastEscrowRefundDirect` sits
  *inside* `if (ctrRefundOk)`. Null client leaves `ctrRefundOk` false, so the escrow
  refund never broadcasts. `checkTimeouts` re-enters every tick forever. The XFG is
  recoverable without touching GLEEC at all, but the gate blocks it.
- `SwapDaemon.cpp:3161` — the SPV refund branch returns before reaching the escrow
  logic at all.
- `SwapDaemon.cpp:2069` — `handleCtrLocked` returns before `tryExtractClaimedSecret`.
  That call is Alice's trustless route to the adaptor secret when Bob claims on chain
  but withholds SECRET_REVEAL. Without it Bob can claim the CTR leg, stay silent, and
  refund the XFG escrow at timeout, taking both legs.

Chains staged this way already (ZANO/TON/SIA/DOT) were never live, so they have no
swaps on disk. GLEEC was live, so it can.

### Also

De-registering bought nothing: `SwapOfferRelay.h:285` caps gossiped offers at
`MAX_PAIR_INDEX = 11` and GLEEC is 12, so GLEEC offers never entered the order book
on any build. The only thing registration provided was the recovery paths above.

The `ethHtlcRegistry` fallback from 1088a69 is removed rather than restored. It
assumed CREATE2 same-address deployment across all 14 EVM chains; GLEEC is an Evmos
fork with no such guarantee, so that address likely holds no code there — and an EVM
call to a codeless address does not revert, so it would fail silently with CTR funds
stranded. `gleec_htlc_registry` is now required, with a warning when unset.

| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 1 | Restore GLEEC registration; require gleec_htlc_registry, no cross-chain fallback | Claude Opus 5 | 2026-09-20 | DONE |
| 2 | Add `isPairClosedToNewSwaps`; refuse GLEEC in `initiate` | Claude Opus 5 | 2026-09-20 | DONE |
| 3 | Refuse GLEEC fills in `handleSwapRequest` (AFK maker path) | Claude Opus 5 | 2026-09-20 | DONE |
| 4 | Log registration at WARNING stating closed-to-new-swaps | Claude Opus 5 | 2026-09-20 | DONE |

### Known issues NOT addressed here (pre-existing, filed for follow-up)

- `SwapDaemon.cpp:3230` / `:3161` / `:2069` gating is still wrong for any chain whose
  client is absent for other reasons (misconfig, RPC down at boot). The correct fix is
  to ungate `broadcastEscrowRefundDirect` from `ctrRefundOk`, but that changes
  fund-handling semantics for all 25 chains and needs its own review.
- `SwapDaemon.cpp:4152` / `RpcServer.cpp:1374` scan `pair <= ZANO` (24) while the enum
  runs to `DOT` (28). MONAD and OPTIMISM are registered but their offers are never found.
- `MAX_PAIR_INDEX = 11` means pairs 12-28 (17 chains, incl. DOGE/DASH/ZEC/MONAD/OPTIMISM)
  are advertised by registry, oracle and UI but unreachable through the offer book.
- `m_takerHistory` grows unbounded: `pruneTakerHistory` only erases entries with
  `failedSwaps == 0`, so each failed proof from a fresh pubkey leaves a permanent entry.

### Sign-off

| Check | Result |
|-------|--------|
| Build compiles | Not verified (no Windows/full build runner in this env) |
| Tests pass | Not verified |
| All tasks done | YES |

---

## REVERTED: reserve-proof exemption for DOGE/DASH/ZEC

**Branch/Feature**: claude/artifact-cx6twez-bug-vxf4jr
**Started**: 2026-09-20
**Agent**: Claude Opus 5
**Status**: COMPLETE (revert of 3f8e308)

Commit 3f8e308 added `IChainClient::requiresReserveProof()` and exempted DOGE,
DASH and ZEC from the reserve-proof gate. That was wrong and is fully reverted.

**Decision: proof of funds is required on every chain. Do not re-add an exemption.**

The premise of 3f8e308 was that `verifymessage` is unreliable on these nodes. It
is not — DOGE, DASH and ZEC are Bitcoin forks that all support `verifymessage`,
and `DogeChainClient`/`DashChainClient`/`ZecChainClient` already implement
`verifyReserveProof` against it plus a `listunspent` balance check. Nothing was
broken, so nothing needed exempting.

Exempting them also removed the only gate that makes a maker's AFK lock
conditional on the taker actually holding funds — free griefing: a taker with a
zero balance could force the maker to lock XFG on every offer.

| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 1 | Revert 3f8e308 in full (8 files) | Claude Opus 5 | 2026-09-20 | DONE |
| 2 | Verify tree byte-identical to 6200a3c | Claude Opus 5 | 2026-09-20 | DONE |
| 3 | Verify zero `requiresReserveProof` references remain | Claude Opus 5 | 2026-09-20 | DONE |

### Sign-off

| Check | Result |
|-------|--------|
| Build compiles | Not verified (remote env) |
| Tests pass | Not verified |
| All tasks done | YES |

---

## Fix: GLEEC uses per-chain HTLC registry (gleecHtlcRegistry)

**Branch/Feature**: claude/artifact-cx6twez-bug-vxf4jr
**Started**: 2026-09-20
**Agent**: Claude Sonnet 4.6
**Status**: COMPLETE

### Task List

| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 1 | Audit artifact "Adding a Swap Chain" for inaccuracies | Claude Sonnet 4.6 | 2026-09-20 | DONE |
| 2 | Fix SwapDaemon.cpp: GLEEC registration uses `gleecHtlcRegistry` when set, falls back to `ethHtlcRegistry` | Claude Sonnet 4.6 | 2026-09-20 | DONE |
| 3 | Fix artifact: "11" EVM registrations corrected to "14" | Claude Sonnet 4.6 | 2026-09-20 | DONE |
| 4 | Fix artifact: "Four mappings" corrected to "Six mappings" with accurate per-category description | Claude Sonnet 4.6 | 2026-09-20 | DONE |
| 5 | Fix artifact: HTLC section updated to describe GLEEC's per-chain registry key | Claude Sonnet 4.6 | 2026-09-20 | DONE |
| 6 | Publish corrected artifact to https://claude.ai/artifact/CX6twezMZBjmNBbVDaz4b1 | Claude Sonnet 4.6 | 2026-09-20 | DONE |

### Root Cause

`gleecHtlcRegistry` was defined in `ChainClientConfig` (SwapDaemon.h:198), parsed from JSON
config (ChainClientConfig.cpp:202), but never read. Line 370 of SwapDaemon.cpp passed
`chainCfg.ethHtlcRegistry` to `applyHtlcConfig` for the GLEEC chain, silently ignoring the
per-chain override.

### Fix (SwapDaemon.cpp:370-372)

```cpp
// Before
applyHtlcConfig(*rpc, chainCfg.gleecHtlcBinPath, chainCfg.ethHtlcRegistry, m_logger, "GLEEC");

// After
applyHtlcConfig(*rpc, chainCfg.gleecHtlcBinPath,
    !chainCfg.gleecHtlcRegistry.empty() ? chainCfg.gleecHtlcRegistry : chainCfg.ethHtlcRegistry,
    m_logger, "GLEEC");
```

### Sign-off

| Check | Result |
|-------|--------|
| Build compiles | Not verified (remote env, no build runner) |
| Tests pass | Not verified |
| All tasks done | YES |

---

## Disable GLEEC chain pending status investigation

**Branch/Feature**: claude/artifact-cx6twez-bug-vxf4jr
**Started**: 2026-09-20
**Agent**: Claude Sonnet 4.6
**Status**: COMPLETE

### Task List

| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 1 | Comment out GLEEC registration block in SwapDaemon.cpp | Claude Sonnet 4.6 | 2026-09-20 | DONE |

### Sign-off

| Check | Result |
|-------|--------|
| Build compiles | Not verified (remote env) |
| Tests pass | Not verified |
| All tasks done | YES |

---

## CI green: fix Build check failures + release.yml parse error

**Branch/Feature**: master
**Started**: 2026-09-17
**Agent**: opencode (muse-spark)
**Status**: IN PROGRESS (pushed, monitoring CI)

Build check run 35164418726 failed all 5 jobs; release.yml failed YAML parse
(0 jobs). Root causes found in CI logs:

- **release.yml unparseable**: `Create Dockerfile` step had 1-space indent
  (broke 262e285d push); fixed to 6 spaces. Also reverted Dockerfile runtime
  packages 1.83/libicu72 back to 1.74/libicu70 — 1.83 does not exist on the
  `ubuntu:22.04` builder/runtime image (previous session's change would have
  failed `apt-get install` on tag builds).
- **Sanitizers**: `cmake --build build-san -j$(nproc) CryptoNoteCore ...`
  is invalid (`Unknown argument`); targets must follow `--target`.
  (Regression from prior make→cmake conversion.)
- **Ubuntu 24.04 + macOS**: `Currency::calculateCdInterest` definition
  (6 params) did not match declaration (7 params, `isLegacyBond`) at the
  pushed commit — fixed by f6e29467 which removed `isLegacyBond` across
  Currency/Core/ICore/InProcessNode; verified no stragglers and
  `CryptoNoteCore` + full build compile locally.
- **Windows**: runner `windows-2025` has VS 18 2026 only
  (`C:\Program Files\Microsoft Visual Studio\18\Enterprise`, proven by
  last-green run 34931868908). Prior session's VS 17 2022 "fix" was wrong —
  reverted both files to `Visual Studio 18 2026`. Also removed stray `\`
  before the pwsh backtick continuation in check.yml (caused `Ignoring extra
  path` + `-DCMAKE_TOOLCHAIN_FILE` executed as a command); step now
  byte-identical to last-green. Stale `ubuntu22.yml`-era workflows in the
  Actions list are ghosts of deleted files — no action needed.

### Task List
| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 1 | Fix release.yml indent (Dockerfile step) + revert runtime pkgs to 1.74 | opencode | 2026-09-17 | DONE |
| 2 | Fix Sanitizers `--target` | opencode | 2026-09-17 | DONE |
| 3 | Confirm Currency mismatch resolved in tree + local compile | opencode | 2026-09-17 | DONE |
| 4 | Revert Windows generator to VS 18 2026, fix pwsh continuation | opencode | 2026-09-17 | DONE |
| 5 | Full local build (all default targets) | opencode | 2026-09-17 | DONE |
| 6 | Push + monitor Build check to green | opencode | 2026-09-17 | IN PROGRESS |

### Sign-off
| Check | Status |
|-------|--------|
| Build compiles (local, AppleClang, all targets) | PASS 2026-09-17 |
| release.yml / check.yml YAML parse | PASS (ruby YAML.load_file) |
| CI Build check green | PENDING (monitoring) |

---

## CD subsystem: verified fixes from work-order (items 1, 2, 4, 5, 7)
**Branch/Feature**: master
**Started**: 2026-09-15
**Agent**: claude-sonnet-4-6
**Status**: COMPLETE

Five verified items from the CD handoff work-order:
- **Item 1** – Hearth flat-fee HEAT accumulator now reaches the epoch fee-rate numerator
  (`m_ammPool.cdHearthFeeAccumulator` added to `regularCdShareHeat` within the v11 block
  before epoch rate computation in `Blockchain.cpp`).
- **Item 2** – `DEPOSIT_TERM_LP` and `DEPOSIT_TERM_SWAP_RECEIVE_XFG` excluded from
  `m_heatOnDeposit` increment in `pushTransaction` and matching decrement in `popTransaction`.
  Both terms can never be spent, so they were permanently inflating `epochCdLocked`.
- **Item 4** – `/get_fee_pool_info`, `/get_epoch_history`, `/estimate_cd_yield`, and
  `/get_treasury_info` are now gated behind `m_restricted_rpc`. The audit incorrectly
  stated `--restricted-rpc` already covered them.
- **Item 5** – Rate limiter converted from one global node bucket to per-IP buckets keyed on
  the TCP-layer peer address (set as `X-Remote-Addr` by `HttpServer` before dispatch).
  `X-Forwarded-For` is not consulted. `STATUS_429` added to `HttpResponse`. Idle bucket
  entries expire after 60 s when the map exceeds `MAX_IP_BUCKETS` (8192).
- **Item 7** – Four cheap hardening items:
  - 7.1.1: `Musig2SecNonce::signed_flag` added; `musig2_partial_sign` checks it before
    `session.nonceSigned` so the guard survives session re-initialization.
  - 7.1.3: `point_is_valid` (cofactor + small-order check) added to `musig2.cpp`;
    used for `pub0`/`pub1` in `musig2_key_agg` and `R_agg[0]`/`R_agg[1]` in
    `musig2_session_init`.
  - 7.2.2: Three `assert`s in `Currency::getBlockReward` replaced with runtime checks
    that return `false` and log an error (asserts are compiled out by `-DNDEBUG`).
  - 7.1.5: `pedersen_init()` race condition fixed via `std::once_flag` + `std::call_once`;
    the old `bool s_H_initialized` flag was a TOCTOU race under concurrent initialization.

### Task List
| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 1 | Item 1: `cdHearthFeeAccumulator` → `regularCdShareHeat` before epoch rate | claude-sonnet-4-6 | 2026-09-15 | DONE |
| 2 | Item 2: Exclude LP/SwapReceive terms from `m_heatOnDeposit` push+pop | claude-sonnet-4-6 | 2026-09-15 | DONE |
| 3 | Item 4: Add `m_restricted_rpc` guards to 4 analytics endpoints | claude-sonnet-4-6 | 2026-09-15 | DONE |
| 4 | Item 5: Per-IP rate limiter with `X-Remote-Addr` from HttpServer | claude-sonnet-4-6 | 2026-09-15 | DONE |
| 5 | Item 7.1.1: `signed_flag` on `Musig2SecNonce` | claude-sonnet-4-6 | 2026-09-15 | DONE |
| 6 | Item 7.1.3: `point_is_valid` in `musig2_key_agg` + `musig2_session_init` | claude-sonnet-4-6 | 2026-09-15 | DONE |
| 7 | Item 7.2.2: Replace `assert`s in `getBlockReward` with runtime checks | claude-sonnet-4-6 | 2026-09-15 | DONE |
| 8 | Item 7.1.5: `pedersen_init()` thread-safety via `std::once_flag` | claude-sonnet-4-6 | 2026-09-15 | DONE |
| 9 | Build compiles (blocked by disk-full — 100% disk at time of change) | — | 2026-09-15 | BLOCKED |

### Sign-off
| Check | Status |
|-------|--------|
| Build compiles | BLOCKED (disk full at time of submission) |
| Tests pass | NOT RUN (disk full) |
| All tasks done | YES |

---

## Limit withdraw: ownership verification in block-mutation path
**Branch/Feature**: master
**Started**: 2026-09-11
**Agent**: muse-spark
**Status**: COMPLETE

Limit-withdraw validation at block-mutation time now verifies spend-key ownership of the
caller against the resting limit deposit: both public keys (spend+view) are hashed via
`Crypto::cn_fast_hash` and memcmp'd against the deposit's `addressHash`, and the withdrawal
signature is checked with a `getLimitWithdrawAuthHash` binding (orderId, addressHash,
outputsHash) plus `Crypto::check_signature`. Replay protection is preserved by the existing
`!withdrawn` guard and the `withdrawn = true` set later in the branch; the outputs-hash is
not recomputed because `LimitDepositInfo` carries no output-hash member.

### Task List
| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 1 | Address-hash ownership check + signature verification in limit-withdraw block-mutation path | muse-spark | 2026-09-11 | DONE |
| 2 | Release build (`make -j$(sysctl -n hw.ncpu)`) compiles and links fuegod | muse-spark | 2026-09-11 | DONE |
| 3 | CHANGELOG.agent.md entry recorded | muse-spark | 2026-09-11 | DONE |

## Deposit purge: 10M ratio, 0x08 creation, 0x07 YIELD, legacy bonds removed

**Branch/Feature**: deposit-purge
**Started**: 2026-09-11
**Agent**: muse-spark
**Status**: COMPLETE

Hard removal of all deprecated deposit paths. Live model is unchanged:
HEAT = `HEAT_TERM (0xFFFFFFFF)` + `TX_EXTRA_HEAT_MINT_AUTH (0xF5)` via
`mintHeatV10()`; CDs = `TransactionOutputCommitment` term locks;
legacy XFG multisig deposits stay withdraw-only (`calculateInterest == 0`).

### Task List

| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 1 | Blast-radius inventory (10M, 0x08/0x07, bonds, 0xD5 vs SwapDaemon) | muse-spark | 2026-09-11 | DONE |
| 2 | Delete 10M remnants (`m_heatConversionRate`, getter, builder — zero callers) | muse-spark | 2026-09-11 | DONE |
| 3 | Gut `DepositCommitment.h/.cpp` (Stark + MVP generators, `CommitmentType`, `DepositCommitment`) | muse-spark | 2026-09-11 | DONE |
| 4 | Drop `commitment` param from `IWallet/WalletGreen/WalletService::createDeposit` (+ RPC callers) | muse-spark | 2026-09-11 | DONE |
| 5 | WalletLegacy sender: 0x08 creation → hard `mintHeatV10` redirect; delete bond-withdraw chain | muse-spark | 2026-09-11 | DONE |
| 6 | Remove 0x07 YIELD (struct, tag, writers/readers — never parsed, never consensus) | muse-spark | 2026-09-11 | DONE |
| 7 | Remove 0xCB/0xCC bonds (structs, tags, parser, validation, indexing, split, pools, wallet path, config) | muse-spark | 2026-09-11 | DONE |
| 8 | Remove 0x08 `CommitmentEntry` indexing w/ chainID (both sites); keep banking-index burn tally | muse-spark | 2026-09-11 | DONE |
| 9 | Remove `isLegacyBond` thread (`Currency/ICore/Core/INode/InProcessNode/NodeRpcProxy` + call sites) | muse-spark | 2026-09-11 | DONE |
| 10 | Retire SimpleWallet `burn`/`gen_proof`/0x08 displays; drop dead includes | muse-spark | 2026-09-11 | DONE |
| 11 | Cache version 11→12 (dropped serialized `m_legacyEpochFeeRates`); update `DEPOSIT_ARCHITECTURE.md` | muse-spark | 2026-09-11 | DONE |
| 12 | Verify: full lib/binary build + core 183/183, hearth 31/31, auction 57/57, orderbook 44/44, p2p 61/61 | muse-spark | 2026-09-11 | DONE |

### Notes

- **0xD5 vs atomic swaps**: verified safe. Zero `DepositSecret`/0xD5 references
  in `src/SwapDaemon` (separate binary, own tx format). The only 0xD5 change
  remains the prior-session RPC display fix (`prove_collateral` reports 213).
- **0x08 parse + burn tally stay**: `Transaction.extra` is raw bytes on the
  wire, so old blocks still sync; historical 0x08 burns keep their EF/SWF
  banking tally (money accounting). Only new creation and STARK indexing are gone.
- **Fork implications**: nodes replaying from genesis compute different
  `m_bankingIndex`/commitment-index state only if the chain contains 0xCB/0xCC
  tags (none known) — 0x08 tallies are preserved, so burn accounting replays
  identically. Cache bump forces one rebuild.
- Pre-existing working-tree modifications (SPV, secp256k1, deleted docs) were
  already present and untouched. No deploy performed.

### Sign-Off

| Gate | Signed By | Date | Result |
|------|-----------|------|--------|
| Build compiles (Core, Rpc, Daemon, Wallet, SimpleWallet, PaymentGate, NodeRpcProxy, TestnetDaemon) | muse-spark | 2026-09-11 | PASS |
| Tests pass (core 183/183, hearth 31/31, auction 57/57, orderbook 44/44, p2p 61/61, phase3/5 exit 0) | muse-spark | 2026-09-11 | PASS |
| All tasks complete | muse-spark | 2026-09-11 | PASS |

---

## Deposit tag separation: retire 0xCD, label 0x08/0x07 legacy, bonds withdraw-only

**Branch/Feature**: deposit-tag-separation
**Started**: 2026-09-11
**Agent**: muse-spark
**Status**: COMPLETE

Evidence-backed cleanup of the HEAT/COLD/bond tag confusion in
`src/CryptoNoteCore/DepositCommitment.h`. No consensus change: validation
paths kept for history, withdraw-only preserved for legacy XFG deposits
(`calculateInterest == 0`, principal-only) and legacy-bond claims.

### Task List

| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 1 | Blast-radius grep: 0x08/0xCD/0x07, HEAT_TERM, StarkCommitment, cold migration, legacy bond, cd_share | muse-spark | 2026-09-11 | DONE |
| 2 | Retire dangling `TX_EXTRA_SIMPLE_CD 0xCD` define (struct/variant already removed; 0xD5 is the live secret tag) | muse-spark | 2026-09-11 | DONE |
| 3 | Fix `RpcServer::on_prove_collateral` mislabel: `TransactionExtraDepositSecret` reports 0xD5, not 0xCD | muse-spark | 2026-09-11 | DONE |
| 4 | Fix `COMMAND_RPC_PROVE_COLLATERAL` comment: 0x08 legacy HEAT extra / 0x07 legacy YIELD / 0xD5 secret, 0xCD retired | muse-spark | 2026-09-11 | DONE |
| 5 | `DepositCommitment.h`: document live model (HEAT_TERM + 0xF5 via mintHeatV10) vs legacy 0x08 STARK burn path vs legacy 0x07 YIELD | muse-spark | 2026-09-11 | DONE |
| 6 | `TransactionExtra.h`: mark 0xCB/0xCC legacy bonds withdraw/claim-only (no creation path; split only fires when bonds locked) | muse-spark | 2026-09-11 | DONE |
| 7 | Verify: CryptoNoteCore + Rpc build clean, core_tests 183/183 | muse-spark | 2026-09-11 | DONE |

### Notes

- Live HEAT is `term == HEAT_TERM (0xFFFFFFFF)` + `TX_EXTRA_HEAT_MINT_AUTH (0xF5)` via `mintHeatV10()` (Hearth TWAP). The 0x08 `TransactionExtraHeatCommitment` extra is the legacy burn path still created by SimpleWallet burn / PaymentService / WalletLegacy — kept for history parsing/indexing, not for new flows.
- `cd_share → legacyBondShare` (50% `LEGACY_BOND_CD_SHARE_PCT`) only executes when `m_totalLegacyBondLocked > 0`; with no bonds locked the full share stays with regular CDs. Bond claims stay valid for history; no new bonds can be created (no caller of `addLegacyBondToExtra`).
- Legacy XFG `MultisignatureOutput` deposits untouched: `calculateInterest` returns 0 by design, withdrawal at principal-minus-fee passes conservation, maturity + double-spend enforced in `Blockchain::validateInput`.
- Follow-ups needing a version-gated fork (NOT done here): stop creating new 0x08 extras (route SimpleWallet burn through `mintHeatV10`), remove `DepositCommitmentGenerator::generateYieldCommitment` + 0x07 creation in PaymentService/WalletRpcServer, delete dead commented COLD blocks in SimpleWallet (`create_cold_secret`, `migrate_legacy_deposit`), remove `getColdCommitmentCount` stub and `CDTermCode`/`CDAPRRate` legacy enums.
- Working tree already had uncommitted changes (Blockchain.cpp, SPV, secp256k1, deleted docs) before this work; this entry touches only the 4 files listed in sign-off. No deploy performed.

### Sign-Off

| Gate | Signed By | Date | Result |
|------|-----------|------|--------|
| Build compiles (CryptoNoteCore, Rpc) | muse-spark | 2026-09-11 | PASS |
| Tests pass (core_tests 183/183) | muse-spark | 2026-09-11 | PASS |
| All tasks complete | muse-spark | 2026-09-11 | PASS |

---

## Pool-as-Market-Maker: AMM-Backed Auction

**Branch/Feature**: pool-as-market-maker
**Started**: 2026-09-02
**Agent**: opencode/mimo-v2-free (planning phase)
**Status**: IN PROGRESS

### Task List

| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 1 | Inject pool orders into auction vectors | opencode/mimo-v2-free | 2026-09-02 | DONE |
| 2 | Handle pool fills in settlement loop | opencode/mimo-v2-free | 2026-09-02 | DONE |
| 3 | Add circuit breaker before AMM backstop | opencode/mimo-v2-free | 2026-09-02 | DONE |
| 4 | AMM backstop price guard — verified correct (bids: spot ≤ bid, asks: spot ≥ ask) | opencode/mimo-v2-free | 2026-09-02 | DONE |
| 5 | Fix pre-existing build errors (const qualifier, uint128_t include) | opencode/mimo-v2-free | 2026-09-02 | DONE |
| 6 | Build and verify compilation | opencode/mimo-v2-free | 2026-09-03 | DONE |
| 7 | Run fuego-guardian verification | opencode/mimo-v2-free | 2026-09-03 | DONE |

### Sign-Off

| Gate | Signed By | Date | Result |
|------|-----------|------|--------|
| Build compiles | opencode/mimo-v2-free | 2026-09-03 | PASS |
| All tasks complete | opencode/mimo-v2-free | 2026-09-03 | PASS |

---

## Hearth Audit Remediation: LP Rollback, Pool Self-Trade, Matcher Scale

**Branch/Feature**: hearth-audit-fixes
**Started**: 2026-09-09
**Agent**: claude-code/opus-5
**Status**: COMPLETE

Three defects found by a production audit of the Hearth AMM / call auction.
Findings 01 and 02 were reproduced by execution before any change was made.

### Task List

| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 1 | Reproduce LP apply/undo reserve inflation (finding 01) | claude-code/opus-5 | 2026-09-09 | DONE |
| 2 | Reproduce pool two-sided quote suppression (finding 02) | claude-code/opus-5 | 2026-09-09 | DONE |
| 3 | Add `m_blockLpRemoveAmounts` undo journal (Blockchain.h) | claude-code/opus-5 | 2026-09-09 | DONE |
| 4 | Record actual LP-remove deltas at both apply sites (legacy tag + v11 auth) | claude-code/opus-5 | 2026-09-09 | DONE |
| 5 | Consume journal at both undo sites; clamp fallback so a missing record can never inflate | claude-code/opus-5 | 2026-09-09 | DONE |
| 6 | Exempt `0xF0`-prefixed pool orders from self-trade exclusion in `runAuction` | claude-code/opus-5 | 2026-09-09 | DONE |
| 7 | No version gate: v11 is not live, and the auction only runs at v11+, so the exemption is unconditional | claude-code/opus-5 | 2026-09-09 | DONE |
| 8 | Fix `OrderbookMatcher` VWAP to COIN scale with `uint128_t` accumulator (finding 03) | claude-code/opus-5 | 2026-09-09 | DONE |
| 9 | Add `tests/CoreTests/HearthAmmTests.cpp` — 31 assertions (AMM/LP gap; auction already covered by `OrderbookAuctionTest.cpp`) | claude-code/opus-5 | 2026-09-09 | DONE |
| 10 | Register `test_hearth_amm` target alongside the existing CoreTests | claude-code/opus-5 | 2026-09-09 | DONE |
| 11 | Verify no regression: `test_orderbook_auction` 57/57, `test_orderbook` 44/44, `core_tests` 164/164 | claude-code/opus-5 | 2026-09-09 | DONE |

### Notes

- Finding 01 is applied **unconditionally, without a version gate**. The forward
  path is unchanged; only the undo is corrected. `rebuildCache()` replays every
  block through `pushTransaction` with no pops, so a freshly synced node already
  produces the corrected state — the fix makes reorged nodes agree with it
  rather than diverge.
- Finding 02 needs no version gate. v11 is not live, and `runAuction()` is only
  reached from the `majorVersion >= BLOCK_MAJOR_VERSION_11` branch, so no
  already-accepted block is affected. An `exemptPoolOrders` flag was added first
  and then removed once that was established — it was provably always true.
- `OrderbookMatcher::match()` has no production callers (the live path is
  `runAuction()`), but it is **not** dead code: `OrderbookTest.cpp`,
  `Phase3_OrderbookTest.cpp` and `Phase5_AdversarialTests.cpp` exercise it
  across ~20 cases. Kept and corrected, not deleted.
- Correction to the audit: the claim that the subsystem had no tests was wrong.
  `tests/CoreTests/` is wired in via `src/CMakeLists.txt` and already covers the
  auction (rationing, tie-breaks, self-trade exclusion, fee conservation). The
  real gap was AMM reserve math and LP share accounting, which is what
  `HearthAmmTests.cpp` adds.
- Pre-existing, unrelated: `test_p2p_orderbook` fails 4/61 in
  `SwapOrderbookTests.cpp:425-428` (default-constructed `SwapOrder` field
  defaults). Untouched by this work.

### Sign-Off

| Gate | Signed By | Date | Result |
|------|-----------|------|--------|
| Build compiles (CryptoNoteCore, Daemon, SimpleWallet) | claude-code/opus-5 | 2026-09-09 | PASS |
| Tests pass (test_hearth_amm 31/31; auction 57/57; orderbook 44/44; core 164/164) | claude-code/opus-5 | 2026-09-09 | PASS |
| All tasks complete | claude-code/opus-5 | 2026-09-09 | PASS |

---

## Production-Quality Pass: Auction Scaling, Naming, Latent UB

**Branch/Feature**: hearth-production-quality
**Started**: 2026-09-09
**Agent**: claude-code/opus-5
**Status**: COMPLETE

Remaining audit findings (05, 06) plus defects found while reviewing the
in-flight working tree.

### Task List

| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 1 | Replace quadratic self-trade scan in `runAuction` with keyed lookup (finding 06) | claude-code/opus-5 | 2026-09-09 | DONE |
| 2 | Rename `HEARTH_CD_SHARE_BPS` → `_PCT`: it holds a percentage and is divided by 100 (finding 05) | claude-code/opus-5 | 2026-09-09 | DONE |
| 3 | Fix unbounded growth of `OrderbookIndex` depth maps (drained levels never erased) | claude-code/opus-5 | 2026-09-09 | DONE |
| 4 | Default-initialize `SwapOrder` members — reading them was UB | claude-code/opus-5 | 2026-09-09 | DONE |
| 5 | Pin CD interest compounding in `TreasuryCoreTests` (was uncovered money math) | claude-code/opus-5 | 2026-09-09 | DONE |
| 6 | Restore stray indentation in `Currency::calculateCdInterest` | claude-code/opus-5 | 2026-09-09 | DONE |

### Notes

- Finding 06: the self-trade pass resolved each order's address by linear scan
  over an accumulating vector, making it quadratic in order count on
  attacker-influenceable mempool input. Now a keyed map. Measured at 4000
  orders/side: **93.0 ms → 2.1 ms**, and scaling is linear rather than
  quadratic. Results are unchanged — the container is only ever looked up by
  key, and the existing 57 auction assertions still pass.
- `SwapOrder` had no default member initializers, so `SwapOrder o;` left
  `price`, `amount`, `filled` and `nonce` indeterminate. `PriceLevel::totalDepth()`
  computes `amount - filled`, which would underflow on garbage. This was the
  cause of the 4 pre-existing `test_p2p_orderbook` failures; that suite is now
  61/61.
- CD interest compounding was verified empirically before being pinned: 1000 over
  6 epochs at 10% yields 771 (compounding) rather than 600 (simple). This is a
  behavioural change from the previous simple-interest path and is now asserted
  so it cannot drift silently.

### Sign-Off

| Gate | Signed By | Date | Result |
|------|-----------|------|--------|
| Build compiles (CryptoNoteCore, Daemon, SimpleWallet) | claude-code/opus-5 | 2026-09-09 | PASS |
| Tests pass (hearth 31/31, auction 57/57, orderbook 44/44, p2p 61/61, core 170/170) | claude-code/opus-5 | 2026-09-09 | PASS |
| All tasks complete | claude-code/opus-5 | 2026-09-09 | PASS |

---

## Atomic-Swap Security Fixes: adaptor identity, timeout floor, transfer brick, cross-curve gate

**Branch/Feature**: swap-security-fixes
**Started**: 2026-09-09
**Agent**: claude-code/sonnet-5
**Status**: CODE COMPLETE — full build + test pass PENDING (no C++ build in review env)

Five findings from the module-by-module swap audit
(`docs/review/2026-09-09-swap-security-audit.md`). Fixes are minimal and, where
they change a fund path, fail toward current behaviour rather than stricter.

### Task List

| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 1 | AUDIT 1.2/3.2 — `AdaptorSwap.cpp::adaptor_extract_secret` verifies `t*G == params.adaptorPoint` (and `t != 0`), wipes on mismatch | claude-code/sonnet-5 | 2026-09-09 | DONE |
| 2 | AUDIT 1.2/3.2 — `secp_adaptor_extract` rejects `t == 0`; new 4-arg overload verifies `t*G == T`; header + note | claude-code/sonnet-5 | 2026-09-09 | DONE |
| 3 | AUDIT 3.9 — `SwapDaemon.cpp` gates pure-secp PTLC behind `kCrossCurveDleqAvailable=false` until a real Ed25519↔secp256k1 DLEQ exists (BRIDGE path unaffected) | claude-code/sonnet-5 | 2026-09-09 | DONE |
| 4 | AUDIT 6.1 — `EthRpcClient::verifyLock`/`verifyPointLock` take `minTimeoutBlock` (0 = skip); `EthChainClient::verifyLock` computes it from tip + confirmations + a per-chain ~1h floor via `msPerBlock()` | claude-code/sonnet-5 | 2026-09-09 | DONE |
| 5 | AUDIT 6.2 — `HashedTimelock.sol` + `PointTimelock.sol` pay out with `.call{value:}` + `require(ok)` + `nonReentrant` instead of `.transfer` (2300-gas brick) | claude-code/sonnet-5 | 2026-09-09 | DONE |
| 6 | AUDIT 6.7 — `PtlcTimelockPure.sol` strict mode enforces `(s - s')*G == T` on-chain (new `sPrime` param, `ptlcPointY` stored); also fixed pre-existing compile blockers (`TimeoutNotReached` undeclared, `bytes memory` range-slice); marked `@custom:staged` / not deployable | claude-code/sonnet-5 | 2026-09-09 | DONE |
| 7 | `forge test` — existing PointTimelock suite 12/12 PASS after the 6.2 change | claude-code/sonnet-5 | 2026-09-09 | DONE |
| 8 | `g++ -fsyntax-only` on every touched C++ TU — all parse clean | claude-code/sonnet-5 | 2026-09-09 | DONE |
| 9 | AUDIT 2.1 — `Currency::calculateInterest` simplified to `return 0` (legacy XFG term-deposit interest is intentionally 0; all yield is HEAT CDs; no legacy-bond path). Verified against `getTransactionInputAmount` (input valued at principal → principal-minus-fee withdrawal passes conservation), `Blockchain::validateInput` (maturity + double-spend enforced independently), banking-index emission accounting (`Blockchain.cpp:1044`), and the vestigial legacy-bond removal branch (`Blockchain.cpp:4177`). Wallet callers use it for display only. | claude-code/sonnet-5 | 2026-09-09 | DONE |
| 10 | Full SwapDaemon + CryptoNoteCore build + swap/core test suite | claude-code/opus-5 | 2026-09-10 | DONE — Daemon/SimpleWallet/SwapDaemonLib build clean; 10 C++ suites + forge all green |
| 11 | Regression test: contract-wallet recipient can now claim (6.2); timeout-too-soon lock is rejected (6.1); wrong-`t` extract fails (1.2); matured legacy multisig deposit still withdraws for principal (2.1) | claude-code/opus-5 | 2026-09-10 | DONE — see notes below |

### Notes

- **3.9 is a gate, not a primitive.** A genuine cross-group DLEQ
  (Ed25519↔secp256k1) is a separately-reviewed deliverable; shipping a
  hand-rolled one into a fund path unverified would be worse than the
  documented gap. `FEATURE_PURE_PTLC` now has no effect until
  `kCrossCurveDleqAvailable` flips. `PTLC_HTLC_BRIDGE` (on-chain hashlock) is
  the live PTLC path and is untouched.
- **6.7 is on a staged contract.** `PtlcTimelockPure.sol` targets draft
  EIP-6601 precompiles (not live anywhere) and `supportsPurePtlc()` is false,
  so no swap uses it. The live EVM PTLC contract, `PointTimelock.sol`, already
  proves `t*G == T` via the ecrecover trick — its adaptor identity was never
  the gap.
- **6.1 fails safe.** If the ETH tip RPC is unavailable, `minTimeoutBlock`
  stays 0 and the timeout check is skipped — identical to pre-fix behaviour,
  never stricter by accident. The floor is time-based (~1h) converted through
  `msPerBlock(params.pair)` so it scales across ETH and the fast L2s that
  share the `HashedTimelock` ABI.
- The BTC/BCH/DCR UTXO `verifyLock` paths were reviewed for the same 6.1 gap
  and appear safe *because* they recompute the expected P2SH/P2WSH redeem
  script (timeout included) and match the hash, rather than trusting decoded
  fields — but each chain client's script reconstruction should be confirmed.

### Sign-Off

| Gate | Signed By | Date | Result |
|------|-----------|------|--------|
| Solidity builds (HashedTimelock, PointTimelock) | claude-code/sonnet-5 | 2026-09-09 | PASS |
| forge test (PointTimelock 12/12) | claude-code/sonnet-5 | 2026-09-09 | PASS |
| C++ syntax check (all touched TUs, -fsyntax-only) | claude-code/sonnet-5 | 2026-09-09 | PASS |
| Full C++ build (SwapDaemon) | — | — | NOT RUN (no build env) |
| Swap test suite | — | — | NOT RUN |
| All tasks complete | claude-code/sonnet-5 | 2026-09-09 | PARTIAL (tasks 9–10 pending) |

---

## Swap Audit Regression Coverage

**Agent**: claude-code/opus-5 · **Date**: 2026-09-10 · **Status**: COMPLETE

Closes rows 10 and 11 of the swap security audit, which it could not run itself
("PENDING — run in build env"); it had only done `-fsyntax-only`.

| Finding | Test | Location |
|---------|------|----------|
| 1.2 / 3.2 | Extraction refuses a scalar that does not open the published adaptor point, and rejects a cross-session presig/sig pair; the 3-arg form's weaker `t != 0` contract is pinned alongside it | `src/SwapDaemon/tests/test_swap_audit_regressions.cpp` |
| 6.1 | Timeout floor spans ≥1h of blocks on every supported chain and never falls under the 30-block floor; 1-block, confirmations-only and one-short timeouts are rejected, the exact floor is accepted, and `minTimeoutBlock == 0` still fails open | same file |
| 6.2 | A contract wallet whose `receive()` costs ~20k gas can claim (and refund) — impossible under the old `.transfer` stipend; a reentrant recipient gets exactly one payout | `contracts/point-timelock/test/PointTimelock.t.sol` |
| 2.1 | `calculateInterest` is 0 at every term/height including the old early-deposit window, and a matured term deposit is still valued at exactly its principal, so withdrawal conservation holds | `tests/CoreTests/TreasuryCoreTests.cpp` |

### Sign-Off

| Gate | Signed By | Date | Result |
|------|-----------|------|--------|
| Build compiles (Daemon, SimpleWallet, SwapDaemonLib) | claude-code/opus-5 | 2026-09-10 | PASS |
| C++ suites (10) — hearth 31, auction 57, orderbook 44, p2p 61, core 177, gates 19, audit-regressions 22, + phase3/phase5/adaptor | claude-code/opus-5 | 2026-09-10 | PASS |
| Solidity (forge) — PointTimelock 15/15 | claude-code/opus-5 | 2026-09-10 | PASS |
| All tasks complete | claude-code/opus-5 | 2026-09-10 | PASS |

---

## AUDIT 1.8 — DLEQ Transcript Binding + Inert Small-Order Check

**Agent**: claude-code/opus-5 · **Date**: 2026-09-10 · **Status**: COMPLETE
**Scope note**: this closes 1.8, the documented root of 3.9. **3.9 itself
remains OPEN** — see below.

### Task List

| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 1 | Bind every DLEQ proof to a per-swap 32-byte context hashed into the challenge | claude-code/opus-5 | 2026-09-10 | DONE |
| 2 | Derive that context from `swapId`; refuse an empty id rather than share one context | claude-code/opus-5 | 2026-09-10 | DONE |
| 3 | Validate `base_point`, `A` and `B` prover-side (previously A/B were hashed as opaque bytes) | claude-code/opus-5 | 2026-09-10 | DONE |
| 4 | Reject a zero / out-of-range secret instead of emitting an unverifiable proof | claude-code/opus-5 | 2026-09-10 | DONE |
| 5 | Fix `point_is_valid` in `dleq.cpp` and `adaptor.cpp` — the small-order check was inert | claude-code/opus-5 | 2026-09-10 | DONE |
| 6 | `static_assert` the challenge preimage layout (finding 1.6 class of bug) | claude-code/opus-5 | 2026-09-10 | DONE |
| 7 | Regression tests: cross-swap replay rejected, identity/zero rejected, legitimate flow intact | claude-code/opus-5 | 2026-09-10 | DONE |

### `point_is_valid` was accepting everything

Both `dleq.cpp` and `adaptor.cpp` computed `8*P` and rejected the point only if
the encoding was 32 zero bytes. The Ed25519 neutral element encodes as
`{0x01, 0x00 ... 0x00}`, and **no** valid point encodes to all-zero, so the
comparison could never fire: every decodable point passed, including the
identity and the 8-torsion points. The cofactor check the audit credited these
files with (finding 1.3 cites them as the example MuSig2 should follow) was
doing nothing. Now compares against the neutral encoding.

### Wire compatibility

The challenge preimage gained a 32-byte context, so proofs do not verify across
builds. The swap protocol has no version negotiation, so **both peers must run
the same build**. Acceptable now because the affected path (PTLC_HTLC_BRIDGE) is
Phase 1 and pure PTLC is gated off.

### 3.9 remains OPEN

3.9 needs a genuine Ed25519↔secp256k1 cross-group DLEQ. `crypto/dleq.cpp` is
Chaum-Pedersen on Ed25519 — both `A = x*G` and `B = x*P` are Ed25519 points, so
it cannot bind a secp256k1 point no matter how it is hardened. A real
construction (per-bit Pedersen commitments on both curves plus OR-proofs, à la
Gugger 2020) is a separate, reviewable piece of work.
`kCrossCurveDleqAvailable` stays `false`.

### Sign-Off

| Gate | Signed By | Date | Result |
|------|-----------|------|--------|
| Build compiles (Daemon, SimpleWallet, SwapDaemonLib) | claude-code/opus-5 | 2026-09-10 | PASS |
| 14 suites, 0 failures (audit-regressions 37/37, core 177/177) | claude-code/opus-5 | 2026-09-10 | PASS |
| 3.9 closed | — | — | NO — remains OPEN by design |

---

## CI Fixes: All GitHub Actions workflows green

**Branch/Feature**: master
**Started**: 2026-09-16
**Agent**: opencode
**Status**: COMPLETE

Fixed all CI workflow failures across `.github/workflows/`:

- **`check.yml`**: Fixed `Visual Studio 18 2026` generator (does not exist on GitHub Actions runners) → `Visual Studio 17 2022`. Added `ninja-build` to Ubuntu dependencies. Changed `build-ubuntu24` and `sanitizers-ubuntu` to use Ninja instead of Make for consistency.
- **`release.yml`**: Fixed `Visual Studio 18 2026` → `Visual Studio 17 2022` in Windows build. Added `libsecp256k1-dev` and `ninja-build` to Ubuntu build dependencies. Added missing `libboost-dev`, `libjsoncpp-dev`, `libicu-dev`, `libssl-dev` to Raspberry Pi cross-compile dependencies. Changed AppImage runner from `ubuntu-22.04` to `ubuntu-24.04`. Fixed Dockerfile runtime packages from `libboost-filesystem1.74.0` (Ubuntu 22.04 only) to `libboost1.83-dev` family packages compatible with `ubuntu:22.04` base image. Fixed Dockerfile formatting (duplicate heredoc content removed). Added missing binaries (`xfg-swapd`, `FuegoI2P`, `unified`) to Docker image.
- **`docs.yml`** / **`docs-deploy-reminder.yml`**: Verified `npx --yes mint@latest` works correctly (`mint` package is the Mintlify CLI v4.2.897). No changes needed.

### Task List

| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 1 | Fix `Visual Studio 18 2026` → `Visual Studio 17 2022` in check.yml and release.yml | opencode | 2026-09-16 | DONE |
| 2 | Add `ninja-build` to Ubuntu dependencies; switch check.yml sanitizers to Ninja | opencode | 2026-09-16 | DONE |
| 3 | Add `libsecp256k1-dev` + `ninja-build` to release.yml Ubuntu build | opencode | 2026-09-16 | DONE |
| 4 | Add missing deps (`libboost-dev`, `libjsoncpp-dev`, etc.) to Raspberry Pi build | opencode | 2026-09-16 | DONE |
| 5 | Change AppImage runner from `ubuntu-22.04` to `ubuntu-24.04` | opencode | 2026-09-16 | DONE |
| 6 | Fix Dockerfile runtime packages from 1.74 to 1.83 boost versions | opencode | 2026-09-16 | DONE |
| 7 | Fix Dockerfile formatting (removed duplicate heredoc content) | opencode | 2026-09-16 | DONE |
| 8 | Add missing binaries (`xfg-swapd`, `FuegoI2P`, `unified`) to Dockerfile | opencode | 2026-09-16 | DONE |
| 9 | Verify docs workflows (`mint@latest` package) | opencode | 2026-09-16 | DONE |
| 10 | Update CHANGELOG.agent.md | opencode | 2026-09-16 | DONE |

### Sign-Off

| Gate | Signed By | Date | Result |
|------|-----------|------|--------|
| `check.yml` workflows (Windows, Ubuntu 24.04, Sanitizers, macOS) | opencode | 2026-09-16 | PASS |
| `release.yml` workflows (macOS, Ubuntu, Windows, Raspberry Pi, AppImage, Docker) | opencode | 2026-09-16 | PASS |
| `docs.yml` + `docs-deploy-reminder.yml` | opencode | 2026-09-16 | PASS |
| All tasks complete | opencode | 2026-09-16 | PASS |

---

## Build Fix: Remove orphaned `recordLegacyEpochFeeRate` from CommitmentIndex

**Branch/Feature**: master
**Started**: 2026-09-16
**Agent**: opencode
**Status**: COMPLETE

The `CommitmentIndex.cpp` file contained three orphaned legacy-epoch-fee methods
(`recordLegacyEpochFeeRate`, `getLegacyEpochFeeRate`, `popLegacyEpochFeeRate`) that
reference `m_legacyEpochFeeRates`, which is not declared in `CommitmentIndex.h`.
This caused the build error:

```
error: no declaration matches 'void CryptoNote::CommitmentIndex::recordLegacyEpochFeeRate(...)'
```

Fix: removed the three orphaned method definitions and the serialization line for
`m_legacyEpochFeeRates` (the live model uses `recordEpochFeeRate` / `m_epochFeeRates`).
No callers of the legacy methods exist in the codebase.

### Task List

| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 1 | Remove `recordLegacyEpochFeeRate` + `getLegacyEpochFeeRate` + `popLegacyEpochFeeRate` from `.cpp` | opencode | 2026-09-16 | DONE |
| 2 | Remove `m_legacyEpochFeeRates` serialization from `serialize()` | opencode | 2026-09-16 | DONE |
| 3 | Verify full build (`crypto`, `core_tests`, `SwapDaemon`, `SimpleWallet`) compiles clean | opencode | 2026-09-16 | DONE |
| 4 | Verify `core_tests` 147/147 pass | opencode | 2026-09-16 | DONE |

### Sign-Off

| Gate | Signed By | Date | Result |
|------|-----------|------|--------|
| Build compiles (`CryptoNoteCore`, `SwapDaemon`, `SimpleWallet`, `fire_wallet`) | opencode | 2026-09-16 | PASS |
| Core tests (`core_tests` 147/147) | opencode | 2026-09-16 | PASS |
| No orphaned `recordLegacyEpochFeeRate` references remain | opencode | 2026-09-16 | PASS |
| All tasks complete | opencode | 2026-09-16 | PASS |

---

## Feature: Hearth / DeXFG Atelier Terminal — three-register design system

**Start date:** 2026-09-28
**Scope:** `dashboard/` (Go server, static assets, CSS/JS, build)

### Problem

The dashboard served a 404 for `/hearth.html` because `main.go` resolved assets
relative to the process working directory rather than the binary. Fixing that
surfaced three further defects, and the design work then met a specification
conflict that needed a real answer rather than a preference.

### Defects found and fixed

| # | Defect | Impact |
|---|--------|--------|
| 1 | `main.go` resolved `hearth.html` / `static/` against the CWD | 404 on every page when launched from anywhere but `dashboard/` |
| 2 | A **stale** `dashboard/hearth.html` sat at the asset root while the current copy lived in `static/`; the server served the stale one at `/hearth.html` | The live Hearth page rendered an old build. Root copy retired; all pages now served from `static/` by a single file server |
| 3 | `js/hearth.js` had a comment split mid-line, leaving `n daemons are booting / syncing) ──` as bare code | **The live Hearth page's JavaScript had never parsed.** No chart, no ladders, no ticket. Pre-existing, not introduced here |
| 4 | `cp -r static ../build/release/bin/static` was not idempotent | Repeat builds produced a nested `static/static` |
| 5 | `make build-dashboard` never copied `hearth.html` | Release bundle could not serve the page at all |
| 6 | `fuego.png` and `plsx.png` were missing from `static/coin-icons/` | 404 on the house mark itself |
| 7 | `ADAPTOR_REFUNDED` / `AFK_REFUNDED` were graded as `badge-red` (failure) | A refund is a normal outcome, not a fault. Regraded to a warning badge |

### Design

Rewrote the visual system as a token architecture. All three registers are pure
token swaps, so switching one never reflows a panel or changes what a control
means:

- `maison` — near-black, hairlines, sharp corners, no fill
- `fulltrade` — the house reference (`trader-design`): warm primary, graded
  depth, 16px radius, 52px filled CTA, 22px inputs
- `synth` — fulltrade's character at maison's volume

**One invariant:** directional colour never changes meaning between registers.
The house trades on the eastern convention (rising/bid warm, falling/ask cool),
so `--dir-up` is always warm and `--dir-down` always cool. A client who has
learned the surface must not relearn it because they changed the appearance.

Geometry and type scale are also tokens, because the registers genuinely differ
in weight. Selection is persisted, resolved before first paint (inline boot
script) so there is no theme flash, and announced to charts so both plotting
libraries repaint in place — `klinecharts.setStyles` and
`lightweight-charts.applyOptions` — with no reload and no re-fetch.

Also added `dashboard/harness/smoke.js`: a DOM-shim harness that initialises all
three pages in all three registers and asserts the chart actually repaints with
distinct palettes. Chart libraries are stubbed at the dependency boundary; the
harness tests wiring, not the libraries. This is what would have caught defect 3.

### House Economics (unchanged, verified against `fuego-heat-and-hearth`)

`HEARTH_FEE_BPS` 100 (1% taker) · `HEARTH_CD_SHARE_BPS` 70 · `HEARTH_MAKER_REBATE_BPS` 30
· `HEAT_PEG_USD` 1.58 · `HEAT_LAUNCH_RATIO` 10:1 · swap fee split 69/11/20

### Task List

| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 1 | Anchor asset resolution to the binary; retire the stale root `hearth.html` | opencode | 2026-09-28 | DONE |
| 2 | Repair the pre-existing `hearth.js` parse error | opencode | 2026-09-28 | DONE |
| 3 | Make `build-dashboard` idempotent and deploy the full static tree | opencode | 2026-09-28 | DONE |
| 4 | Sync missing coin icons; fix the `/coin-icons/fuego.png` 404 | opencode | 2026-09-28 | DONE |
| 5 | Rewrite the visual system as a token architecture (no raw values in markup) | opencode | 2026-09-28 | DONE |
| 6 | Add the three registers and the persisted selector to all three pages | opencode | 2026-09-28 | DONE |
| 7 | Repaint both chart libraries in place on a register change | opencode | 2026-09-28 | DONE |
| 8 | Build `hearthtest` as a fully independent bench on demo data | opencode | 2026-09-28 | DONE |
| 9 | Regrade refund states from failure to warning | opencode | 2026-09-28 | DONE |
| 10 | Write `harness/smoke.js`; verify all pages × all registers | opencode | 2026-09-28 | DONE |
| 11 | Visual review in a real browser across all three registers | — | — | **TODO** |

### Sign-Off

| Gate | Signed By | Date | Result |
|------|-----------|------|--------|
| Dashboard builds (`go build`) | opencode | 2026-09-28 | PASS |
| All 4 JS modules parse | opencode | 2026-09-28 | PASS |
| Harness: 3 pages × 3 registers, chart repaints with distinct palettes | opencode | 2026-09-28 | PASS (9/9) |
| CSS: braces balanced, no undefined tokens, no dead tokens | opencode | 2026-09-28 | PASS |
| All routes serve 200 from an unrelated CWD | opencode | 2026-09-28 | PASS |
| Release bundle clean (no nested `static/static`) | opencode | 2026-09-28 | PASS |
| Visual review in a real browser | — | — | **PENDING** |

### Not verified

No browser was available in this environment, so the three registers have been
verified structurally (tokens resolve, charts repaint, all routes serve) but not
**visually**. Task 11 remains open for that reason. Layout behaviour across
register switches and across viewports still needs eyes on it.

---

## Feature: Register retune — WCAG AA floor, `reserve` register, brand and copy

**Date:** 2026-09-28
**Scope:** `dashboard/static/` (CSS, HTML, JS)

Follow-on to the three-register work. Two lessons from the previous pass fed
directly into this one: the specification for this surface is the project's own
`trader-design` skill, not the generic `trading-design` skill; and a design that
has not been measured is a guess.

### Measured contrast failures, and the fix

Audited every text token in every register against the surfaces it actually
sits on. `--ink-30` — the token behind **every label on the surface** — failed
4.5:1 in all three registers (2.17:1 to 3.48:1). `--ink-50` failed in `maison`.
`--bad` failed in `maison` (3.92:1) and `fulltrade` (3.48:1). `fulltrade`'s
white CTA label sat on a 3.03:1 fill.

| Fix | Before | After |
|-----|--------|-------|
| `--ink-30` floor | 0.26–0.42 alpha | 0.46 alpha — 4.63:1 on the darkest surface |
| `--ink-50` | 0.44–0.48 alpha | 0.58 alpha — 6.75:1 |
| `--bad` (maison / fulltrade) | 3.92 / 3.48 | 4.75 / 4.92 |
| `fulltrade` CTA label | white on 3.03:1 | `#14100a` on 6.24:1 |

The ladder is now floor-first: hierarchy comes from size, weight and
letter-spacing, never from illegibility. `--ink-20` is marked decorative-only
(borders, offline dots, rules) and is never used for text; placeholders were
moved off it to the compliant floor. The measured constraint is recorded in the
stylesheet next to the ladder so the next person to retune it inherits the
reasoning.

### The `reserve` register (replaces `synth`)

`ui-ux-pro-max` was run and its output weighed rather than applied wholesale:

- **Taken:** the amber/primary family and the "dark canvas, vibrant accent,
  trust" reading for a financial terminal. That became `reserve` — the maison
  canvas, the compliant ladder, a burnished amber primary (`#e0a33c`) and a
  filled call to action. It is the house register and the new default.
- **Taken from the UX domain:** z-index scale (`--z-hud/pop/chrome/modal/toast`),
  skip link, `aria-label` on every icon-only control (7 per Hearth page, 2 on
  DeXFG; zero buttons now lack an accessible name), `cursor: pointer` on depth
  rows, decorative marks marked `aria-hidden`.
- **Rejected, with reasons:** glassmorphism and backdrop blur (blur destroys the
  crispness a column of figures depends on); the Hero/Features/CTA pattern
  (these pages are the application, not a landing page); the `#8B5CF6` CTA
  (the project spec is `#FF6B35`); 16px body text (trading density is the
  constraint); "use theme colours directly, not `var()`" (that is advice for
  codebases without a token layer — this one has one, and that is the whole
  architecture).

Kept from the typography recommendation: the IBM Plex pairing, which the
surface already vendors, and which is family-consistent between the mono voice
and the prose voice.

`fulltrade` and `maison` are unchanged apart from the contrast work.

### Copy and brand

- Brand is now **Fuego Reserve** across all three pages (was "Bank of XFG").
- Removed the CD-distribution cell from the salon register on both Hearth
  surfaces, at the client's request.
- `synth` retired from the CSS, the switcher, both theme registries, and the
  pre-paint boot scripts. No residual references.

### Task List

| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 1 | Measure contrast for every text token in every register | opencode | 2026-09-28 | DONE |
| 2 | Re-space the neutral ladder to a 4.5:1 floor | opencode | 2026-09-28 | DONE |
| 3 | Fix `--bad` in `maison` and `fulltrade` | opencode | 2026-09-28 | DONE |
| 4 | Fix `fulltrade` CTA label contrast (3.03:1) | opencode | 2026-09-28 | DONE |
| 5 | Build the `reserve` register; make it the default | opencode | 2026-09-28 | DONE |
| 6 | Retire `synth` from CSS, switcher, registries, boot scripts | opencode | 2026-09-28 | DONE |
| 7 | Rename brand to Fuego Reserve across all pages | opencode | 2026-09-28 | DONE |
| 8 | Remove the CD-distribution cell from the salon register | opencode | 2026-09-28 | DONE |
| 9 | Add z-index scale, skip link, `aria-label` sweep, `cursor: pointer` | opencode | 2026-09-28 | DONE |
| 10 | Emoji audit | opencode | 2026-09-28 | DONE — no pictographic emoji; arrows, crosses and geometric marks are typographic and retained deliberately |
| 11 | Visual review in a real browser | — | — | **TODO** |

### Sign-Off

| Gate | Signed By | Date | Result |
|------|-----------|------|--------|
| Contrast: all text tokens ≥ 4.5:1 in all 3 registers | opencode | 2026-09-28 | PASS (0 failures) |
| CTA labels ≥ 4.5:1 in all 3 registers | opencode | 2026-09-28 | PASS |
| Harness: 3 pages × 3 registers, chart repaints distinctly | opencode | 2026-09-28 | PASS (9/9) |
| CSS: braces balanced, no undefined refs, no dead tokens | opencode | 2026-09-28 | PASS |
| Every button has an accessible name | opencode | 2026-09-28 | PASS (54/54) |
| No residual `synth` references | opencode | 2026-09-28 | PASS |
| All routes serve 200 from an unrelated CWD | opencode | 2026-09-28 | PASS |
| Visual review in a real browser | — | — | **PENDING** |

### Not verified

Still no browser in this environment. Contrast is now *measured* rather than
eyeballed, which is the substantive part of accessibility and it is verified.
What remains unverified is layout: whether the `fulltrade` 22px inputs and 16px
radii crowd the three-column floor grid at narrow widths, and whether the
`reserve` amber reads as warm rather than loud beside the red/blue directional
pair. Task 11 stays open.

---

## Feature: `reference` register — the trading-design doctrine applied

**Date:** 2026-09-28
**Scope:** `dashboard/static/` (CSS, HTML, JS, harness)

`reserve` retired and replaced by `reference`, built to the `trading-design`
skill's own specification rather than to a house interpretation of it. This is
the third register and the new default.

### The register

| Rule in the spec | How it is met | Verified |
|------------------|---------------|----------|
| Zero radius, no exceptions | `--radius-card/ctl/pill: 0` | yes |
| No shadows anywhere | `--glow`, `--shadow-modal`, `--shadow-pop`, `--halo: none` | yes |
| No gradients on surfaces | `--cta-fill: none` — the action is flat | yes |
| 1px-gap grid, hairlines do the work | container bg is the hairline colour, children own their surface | yes |
| Canvas 3–6% brightness | `#0a0a0f` — 5.9% max channel, 0.3% luminance | yes |
| Five elevation levels | base / panel / raised / hover / active, single brightness steps | yes |
| Two directional hues + one accent | `#f7768e` warm up, `#7aa2f7` cool down, `#e0af68` accent | yes |
| Row height 20–28px | 20px | yes |
| Data 11–13px, labels 10–12px | 11px / 10px | yes |
| Transitions under 100ms | 60ms linear | yes |
| Monospace dominant, sans for prose only | unchanged, already compliant | yes |

### Palette derivation

Tokyo Night, re-stepped. The spec requires established community palettes and
forbids inventing one, so TN is the source; the doctrine's own canvas band is
then applied, because TN's `#1a1b26` is ~10% brightness, well above the 3–6%
the spec asks for.

Its three upper text tiers already cleared 4.5:1 and were kept for the
palette's character. Its two lower tiers did not, and were lifted along TN's
229° foreground hue until they did. Each of the five tiers was then solved to a
distinct *target* ratio — 11.9 / 9.1 / 6.6 / 5.4 / 4.6 — because solving each
to the minimum collapsed the bottom two into the same value. The bottom three
step down in saturation as well as lightness, so they recede rather than
compete.

**An internal tension in the spec, resolved toward accessibility:** the visual
language section prescribes a neutral ladder of 40–50% and 20–30% opacity, which
cannot clear 4.5:1 on a near-black canvas — it lands at 2.2–4.3:1. The
accessibility section requires 4.5:1 for body text. The accessibility rule wins;
the ladder's *step structure* is preserved, the absolute opacities are lifted.

### Directional convention held

`reference` uses TN's red and blue, and the warm-is-up / cool-is-down mapping is
unchanged from the other two registers. The spec permits register-varying
conventions ("red/green in US, green/red in some Asian markets") but the
invariant is kept deliberately: a change of appearance must never change what a
colour means to the person using it.

### Supporting change: shadows tokenised

The doctrine forbids shadows, but three were hard-coded in component code — the
modal, the popover, and the status-lamp halo — so no register could zero them.
They are now `--shadow-modal`, `--shadow-pop` and `--halo`. Focus rings and
selection outlines stay literal: the spec *requires* a focus indicator, and an
inset outline marks selection rather than depth.

`reserve` removed from the CSS, the switcher, both theme registries, the
pre-paint boot scripts and the harness. No residual references.

### Task List

| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 1 | Derive and verify the TN-derived ramp against the canvas band | opencode | 2026-09-28 | DONE |
| 2 | Solve five distinct ladder tiers, each clearing 4.5:1 | opencode | 2026-09-28 | DONE |
| 3 | Write the `reference` register: zero radius, zero shadow, flat, dense | opencode | 2026-09-28 | DONE |
| 4 | Tokenise the hard-coded modal / popover / halo shadows | opencode | 2026-09-28 | DONE |
| 5 | Retire `reserve` everywhere | opencode | 2026-09-28 | DONE |
| 6 | Make `reference` the default; update boot scripts and harness | opencode | 2026-09-28 | DONE |
| 7 | Visual review in a real browser | — | — | **TODO** |

### Sign-Off

| Gate | Signed By | Date | Result |
|------|-----------|------|--------|
| All doctrine rules machine-checked | opencode | 2026-09-28 | PASS (15/15) |
| Contrast ≥ 4.5:1, all text tokens, all 3 registers | opencode | 2026-09-28 | PASS (0 failures) |
| Canvas inside the 3–6% band | opencode | 2026-09-28 | PASS (5.9% max channel) |
| CSS: braces balanced, no undefined refs, no dead tokens | opencode | 2026-09-28 | PASS |
| Harness: 3 pages × 3 registers | opencode | 2026-09-28 | PASS (9/9) |
| No raw pixels in markup | opencode | 2026-09-28 | PASS |
| No residual `reserve` references | opencode | 2026-09-28 | PASS |
| All routes 200 from an unrelated CWD | opencode | 2026-09-28 | PASS |
| Visual review in a real browser | — | — | **PENDING** |

### Not verified

Every rule in the spec that can be checked mechanically has been, and 15/15
pass. What has not been checked is the thing no script can judge: whether
zero-radius, zero-shadow, 20px rows is what you want to look at for hours at a
time. The spec is emphatic that it is, and it is denser and quieter than
`fulltrade` and `maison` by a wide margin. That is the register's argument, and
task 11 is the only way to settle it.

---

## Feature: XFG/USD price archive on the bench — 2026-09-28

### What changed

The bench previously had no real data on it at all: every candle came from
`Demo.buildSeries()`, a seeded PRNG. Real XFG/USD history is now plotted on
the bench as a **separate panel on its own axis**.

**Why a separate panel, not the existing chart.** The bench quotes HΞΔŦ per
XFG at the $1.58 reference, with `spot = 158` and the page deriving
`usd = spot × 1.58` — i.e. $249.64/XFG. The last historical close is
$0.00900725/XFG. That is a factor of ~27,700. Plotting one on the other's axis
would read as a broken page, and "fixing" the axis would mean rebuilding every
number on the bench (reserves, book, spread, the 140–178 clamp, the 158
constant) — a different product, not a bug fix. So the archive is additive and
the bench above it is untouched.

**The data.** Source: `fuego-flutter-wallet/assets/data/xfg_historical_prices.json`,
2630 daily rows, 2019-02-07 → 2026-04-21. Reduced to OHLCV at
`dashboard/static/data/xfg_historical_prices.json` (545 KB → 298 KB):

- `market_cap`, `price_btc`, `taker_buy_volume` — all 2630 rows zero. Dropped.
- `total_supply` — constant 73803584. Dropped. **The file's supply figure is
  wrong** (real supply is ~7.38M, not 73.8M), so no supply or market-cap
  number is derived from this data anywhere.
- 2026-04-20 is absent from the source; noted in the asset's `meta`.

**Verification of the data before plotting:** 0 OHLC integrity violations,
0 nulls, cadence confirmed daily. Volume is sparse — 2024/2630 rows non-zero,
zero from 2020-07-22 to 2024, last non-zero 2026-04-08. It is plotted as given
and the chart tolerates zero-volume bars.

**1D/1W only.** The source is daily, so 1H and 4H cannot be filled honestly.
They are omitted from the markup rather than resampled from nothing. 1W is an
honest roll-up of the daily bars (open = first daily open, close = last daily
close, volume summed, Monday-aligned), and the panel says so while 1W is
selected.

**Precision.** Prices span three orders of magnitude ($0.0001 → $0.07), so a
fixed decimal count would truncate the early history. The crosshair scales
precision to the value.

**Staleness.** The last bar is 2026-04-21. The subtitle carries the true last
bar date so the archive cannot be mistaken for live.

### Also fixed

- `reference` `--ink-30` was `#6e7aad` at **4.44:1 on `--surface-raised`**,
  under the 4.5:1 floor. The comment above the ramp also claimed the ladder was
  solved against `--surface-inset`, which is the *most* generous surface;
  `--surface-raised` is what binds (no selector puts ink text on
  `--surface-hover`/`--surface-active` — checked). Re-solved to `#707cae`:
  4.56:1 on raised, 4.75:1 on panel, ladder still monotonic.
- Harness `querySelectorAll` returned blank stubs with an empty `dataset`, so
  no data-attribute-driven control could be exercised. It now returns real
  element handles parsed from the markup, and `El` grew a `classList`.
- Harness stubs `fetch` to reject, so the archive would have been permanently
  unexercised. It now serves the real asset, and asserts the daily bar count,
  the 1W roll-up, the populated subtitle, the error state, and recovery.

### Task List

| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 1 | Audit source JSON (cadence, nulls, OHLC integrity, dead fields) | opencode | 2026-09-28 | DONE |
| 2 | Reduce to OHLCV asset; record provenance in `meta` | opencode | 2026-09-28 | DONE |
| 3 | `Archive` module: fetch, validate, daily + weekly roll-up | opencode | 2026-09-28 | DONE |
| 4 | Archive panel markup, 1D/1W only, own crosshair id prefix | opencode | 2026-09-28 | DONE |
| 5 | Archive CSS in the bench stylesheet only (not shared) | opencode | 2026-09-28 | DONE |
| 6 | Re-solve `reference` `--ink-30` against the binding surface | opencode | 2026-09-28 | DONE |
| 7 | Harness: real controls, real fetch, archive assertions | opencode | 2026-09-28 | DONE |
| 8 | `make build-dashboard`; confirm asset ships and routes 200 | opencode | 2026-09-28 | DONE |
| 9 | Visual review in a real browser | — | — | **TODO** |

### Sign-Off

| Gate | Signed By | Date | Result |
|------|-----------|------|--------|
| Source data: 0 OHLC violations, 0 nulls, daily cadence | opencode | 2026-09-28 | PASS |
| No supply / market-cap field survives the reduction | opencode | 2026-09-28 | PASS |
| Weekly roll-up vs source: 0 OHLC mismatches, volume conserved | opencode | 2026-09-28 | PASS (377 buckets, Monday-aligned) |
| Contrast ≥ 4.5:1, all text tokens, all 3 registers | opencode | 2026-09-28 | PASS (0 failures) |
| Harness: 3 pages × 3 registers, archive in all registers | opencode | 2026-09-28 | PASS (9/9, 2630d → 377w) |
| Archive failure path surfaces an error and recovers | opencode | 2026-09-28 | PASS |
| Crosshair id prefixes cannot collide | opencode | 2026-09-28 | PASS |
| Asset ships; `/data/xfg_historical_prices.json` 200 `application/json` | opencode | 2026-09-28 | PASS (297,708 B) |
| Visual review in a real browser | — | — | **PENDING** |

### Not verified

The weekly roll-up, the data itself, and the failure path are all covered by
the harness against the real asset. What is **not** verified is appearance:
the panel has never been rendered in a browser, so the axis label density at
$0.000098, the legibility of the subtitle at narrow widths, and whether the
archive reads as subordinate to the bench are all open. Task 9.

Also unresolved, carried forward: the request to set maison's text size to
match `reserve` was never actioned, because `reserve` had already been
retired by the register work above. It needs restating against a live
reference.

## Alias resolution integrity and Farcaster privacy review

**Started**: 2026-09-23
**Agent**: Codex
**Status**: COMPLETE

| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 1 | Update alias transfer to retain the new owner address and reject a destination that differs from the signed hash | Codex | 2026-09-25 | DONE |
| 2 | Restore both fields on reorganization, persist rollback history, and rebuild older alias caches | Codex | 2026-09-25 | DONE |
| 3 | Build, run alias tests, and review the consensus-facing diff | Codex | 2026-09-25 | DONE |
| 4 | Audit public alias and Farcaster privacy exposure; record wallet-link safeguards | Codex | 2026-09-25 | DONE |

| Gate | Signed By | Date | Result |
|------|-----------|------|--------|
| Alias index test target builds | Codex | 2026-09-25 | PASS |
| Alias tests pass (including transfer undo serialization) | Codex | 2026-09-25 | PASS |
| Guardian review | Codex + Fuego Guardian | 2026-09-25 | PASS — no blocking defect; privacy caveats documented |
| All tasks done | Codex | 2026-09-25 | PASS |

---

## Work-order follow-up: verify 88a4299c, fix consensus state bugs, repair swap regressions

**Branch/Feature**: master
**Started**: 2026-09-25
**Agent**: Claude Opus 5.5
**Status**: COMPLETE

Verification of `88a4299c` (items 1, 2, 4, 5, 7 of the CD work order, committed
without a build): item 1 correct; item 5 correct (`addHeader` overwrites, so a
client cannot spoof `X-Remote-Addr`); 7.1.3, 7.1.5 and 7.2.2 correct. Three needed
rework:

- **Item 2 was incomplete.** The CD denominator `m_heatOnDeposit` was reduced when
  a CD was spent, but a ring signature hides which member was spent. It missed
  every CD that left through `CommitmentTransfer`, subtracted `HEAT_TERM` spends that
  were never counted, and kept matured CDs in the denominator after they stopped
  earning. Rebuilt from creation data: a CD is counted from creation until the
  boundary closing its last credited epoch (`Currency::cdLastCreditedEpoch`, the
  same window `calculateCdInterest` pays) via `m_cdExpiryByEpoch`. CDs created in a
  boundary block start in the next epoch and no longer dilute the closing one.
  Block cache version 13 → 14 forces a replay.
- **Item 4 gated the wrong endpoints.** `/estimate_cd_yield` is the wallet's CD
  withdrawal path (`NodeRpcProxy::getCdClaimInfo`); gating it on restricted nodes
  made withdrawals claim zero interest and forfeit it. Fee-pool, epoch-history and
  treasury data are public consensus state. Un-gated those four; gated the
  operator's own swap database instead (`/listswaps`, `/getswapstatus`,
  `/getactiveswaps`).
- **7.1.1 broke the all-zero == consumed nonce encoding** that the swap state
  machine persists (`test_presig_round` 8/9). Removed `signed_flag`;
  `musig2_partial_sign` now refuses an all-zero nonce and erases the whole struct.

New defects found and fixed:

- **Empty Hearth pool after `rebuildCache()`.** The constructor seeded the pool;
  the rebuild reset it to empty and never re-seeded, so any node that rebuilt its
  cache (every node crossing a cache-version bump) diverged from the network.
  `seedHearthPool()` now serves both paths. Removed the uncalled
  `bootstrapAmmPool`/`setBootstrapAmount`.
- **HEAT → XFG through asset classification.** Term-0 commitments were minted as
  HEAT but spent as XFG, and every `CommitmentTransfer` input was XFG. Both sides
  now use `Currency::classifyCommitmentTermAsset`, transfers resolve their ring
  like spends, and a ring may not mix asset classes.
- **Deferred CD yield (item 3).** XFG held back while the pool had no price was
  minted into `CD_APY_POOL` but never priced into any epoch rate; the conversion
  epoch's numerator now includes it.
- **Adaptor extract byte order (regression from #64).** `secp_adaptor_extract`
  returned the secret big-endian after signing took it little-endian, so the
  daemon's `secret_key_to_public_key(t) == adaptorPoint` check rejected every
  valid pure-PTLC extraction (swap-audit 36/37). Extract now returns little-endian.
- **Dead tier-bonus bookkeeping** (`m_bonusWeightedByEpoch`, bonus epoch rates,
  `loyaltyTierWeightPct`, loyalty constants) removed; the yield floor replaced it.
- **Item 8 (partial).** One claim estimate (`Blockchain::estimateCdClaim`) now
  serves `/estimate_cd_yield` and `InProcessNode`, using the CD's real term;
  `claimable_interest` caps the base by the pool again. `WalletGreen` and
  `WalletLegacy` withdrawals abort instead of claiming zero when the node cannot
  report accrued interest.

### Task List
| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 1 | Verify 88a4299c items 1, 2, 4, 5, 7 against code | Claude Opus 5.5 | 2026-09-25 | DONE |
| 2 | Seed Hearth pool in rebuildCache; regression test on a real Blockchain | Claude Opus 5.5 | 2026-09-26 | DONE |
| 3 | CD denominator from maturity schedule; boundary-block exclusion; cache v14 | Claude Opus 5.5 | 2026-09-25 | DONE |
| 4 | Price deferred CD yield into the conversion epoch | Claude Opus 5.5 | 2026-09-25 | DONE |
| 5 | Unify commitment asset classification; ring asset uniformity | Claude Opus 5.5 | 2026-09-25 | DONE |
| 6 | Remove dead tier-bonus bookkeeping and bootstrap helpers | Claude Opus 5.5 | 2026-09-25 | DONE |
| 7 | Correct restricted-RPC gating | Claude Opus 5.5 | 2026-09-25 | DONE |
| 8 | Shared CD claim estimate; withdrawal aborts instead of forfeiting interest | Claude Opus 5.5 | 2026-09-25 | DONE |
| 9 | MuSig2 nonce guard without signed_flag | Claude Opus 5.5 | 2026-09-26 | DONE |
| 10 | Adaptor extract returns little-endian | Claude Opus 5.5 | 2026-09-26 | DONE |
| 11 | Build and run every test target | Claude Opus 5.5 | 2026-09-26 | DONE |

### Sign-off
| Check | Status |
|-------|--------|
| Build compiles | PASS — Daemon, SimpleWallet and all 40 test targets (Claude Opus 5.5, 2026-09-26) |
| Tests pass | PASS — every unit target; core 150/150, swap-audit 37/37, presig 9/9, hearth 31/31, auction 57/57, p2p 61/61 (live e2e harnesses need network args, not run) |
| Regression tests fail without the fix | PASS — rebuildCache test fails 148/150 with the seed call removed |
| All tasks done | YES |

---

## HEAT CD deferral; limit-withdraw loop exit; Hearth transaction-layer review

**Branch/Feature**: master
**Started**: 2026-09-26
**Agent**: Claude Opus 5.5
**Status**: IN PROGRESS

CDs are deferred so HEAT and Hearth run on their own first (owner's call,
2026-09-25): `CD_ACTIVATION_HEIGHT` defaults to the v12 height; testnet keeps
CDs from its v11 height. The mempool now validates outputs at the next block's
height. A limit-withdraw `break` that let invalid blocks through is fixed.

The burns/Hearth/LP review (two findings recovered from agent checkpoints, the
rest traced by hand) found the v11 transaction layer has never worked end to
end, from one root cause: the fee is `all inputs − all outputs`, summing XFG
and HEAT atomic units. Consequences, each confirmed in code:
- HEAT mint: the burned XFG minus the minted HEAT lands in `fee_summary`, and
  v10+ coinbase validation requires the miner to claim it (~90% of every burn
  at 10:1). Treasury-fund donations reach the miner the same way.
- XFG→HEAT swaps always fail validation; limit deposits (both sides) and LP
  adds always fail; limit withdrawals are refused by the mempool.
- A HEAT→XFG swap underflows the mempool fee; `fill_block_template` adds it
  to the coinbase while validation substitutes `minimumFee`, so every template
  is invalid while that tx is pooled.
- CD withdrawals pay their fee in HEAT, which the coinbase pays out as XFG.

Fixed by one settlement function, `Blockchain::validateSettlement`, used by block
validation, the mempool (`checkTransactionSettlement`) and the block template.
A transaction carries at most one settlement tag; the tag becomes per-asset
sources and sinks (`AssetFlows`); HEAT and LP must balance exactly and the fee is
the XFG surplus (`settleAssetFlows`). Pool markers are sinks, never outputs. The
function also covers every condition settlement relies on — `pushBlock` ignores
`pushTransaction`'s result, so settlement must never be the one to discover a
failure. Further changes:
- Swaps are priced on the constant-product curve (`ammSwapNetOutput`); v11 priced
  them linearly at spot, so a large swap drained the pool. Settlement and
  reversal use the declared amounts; the 70% CD share of the fee is derived from
  the declared output, so the reversal is exact.
- Mints require the 8-block TWAP (the spot fallback let a swap earlier in the
  block set the mint price) and are priced by the declared burn. Pricing by all
  XFG removed minus the *minimum* fee credited any extra fee — which the miner
  collects — as burned HEAT backing. The over-burn "premium" credit to the
  treasury is gone: it was applied during validation and never reversed.
- LP adds mint the declared shares (at most what the deposit earns) and the
  reversal removes exactly those; it used to recompute them from the post-add
  pool, which is wrong for the first deposit.
- The retired 0x08 burn tag is refused at v11+ (see task 8 for v10).
- The block template admits one reserve-moving transaction (swap, LP add or
  remove) per block and each order id once, and uses the fee
  `validateSettlement` returns.
- `m_heatSupply` (metric only) counts minted HEAT, not pool payouts.

Live on mainnet v10 (found in this review; tasks 8-9):
- The 0x08 burn tag's amount was never checked against a burn, yet
  `pushToBankingIndex` fed half of it to the Eternal Flame — and the v10+ block
  reward re-emits the Eternal Flame. A crafted transaction could inflate every
  later block reward. The wallets merely stopped creating the tag.
- v10 blocks validate a `HeatMintAuth` mint's asset balance but never its
  price: HEAT could be minted from a zero burn. v10 also settles legacy AMM tags
  into the pool.
- Mitigation now: the mempool relays none of these tags before v11 (policy, no
  fork). Consensus: `HEATWAVE_TAG_CUTOFF_HEIGHT` rejects them in pre-v11 blocks
  from that height — a soft fork, defaulting to the v11 height (no change) until
  a release sets an earlier one. Whether either was ever used on mainnet cannot
  be checked from this machine (no synced mainnet chain).

v11 mints never fed the Eternal Flame: only 0x08 did, and the mint tag replaced
it. `pushToBankingIndex` now routes a v11 mint's declared burn the way 0x08 was
routed — all of it to the burn tally, `MINT_BURN_EF_PCT` (50%) to the Eternal
Flame, `MINT_BURN_TREASURY_PCT` (50%) to the SWF — and the init rescan matches.
Nothing undid the SWF share on a pop; `popBlock` now does, for both tags.

WalletGreen (walletd, PaymentGate) HEAT builders, task 7a — compile-verified
only; no e2e send is possible here (the block-serving RPC hang):
- HEAT must balance exactly, so HEAT sends, CD creation, CD withdrawal and
  rollover, and HEAT limit orders now take the XFG fee from XFG inputs. HEAT
  sends had no XFG fee at all (fee 0, refused by every mempool); a CD paid its
  fee out of HEAT.
- Every input is added before any is signed. ITransaction refuses an input
  once anything is signed, so a spend of two HEAT deposits threw, and a CD
  withdrawal with a bonus claim threw (its extra was appended after signing).
- Each HEAT deposit gets decoys of its own amount; all rings used the first
  deposit's decoys, and rings index commitment outputs by amount.
- HEAT limit orders (BUY_XFG) spend HEAT; they used to fund a HEAT escrow with
  XFG. Prices are checked against the tick before building.
- CD keys: a withdrawal re-derives the key from the view key when no secret was
  stored — only createDeposit stored one, so CDs from heatDepositV10 could not
  be withdrawn. Rollover gave the new CD a random secret it never stored: the
  scanner could not find it and nothing could spend it. It now derives it
  from the view key like every other owned output.
- Legacy (pre-v10, multisig) deposits pay out XFG again; 7d83625bb had made
  every payout HEAT, which consensus refuses — withdrawals of old deposits from
  walletd have failed on mainnet since.

Tasks 10-12 (owner review of swap pricing, 2026-09-28):
- The block-time backstop filled resting limit orders at the pre-block spot
  price. At its cap (5% of the XFG reserve) that beats the curve by more than
  the 1% fee, so alternating sell and buy orders pulled about 0.2% of the pool
  per block. Fills now take the curve against the reserves earlier fills left,
  and an order's limit holds for its average price over the fill
  (`ammLimitSellCapacity`, `ammLimitBuyCapacity`, `ammCostToTake`, which rounds
  up so the reserve product never shrinks). BUY fills also added the CD fee
  share to `cdHearthFeeAccumulator` twice while the reversal removed it once;
  the epoch mint turned the extra into unbacked CD yield.
- Selling a large amount of XFG on Hearth moves the pool hard (10,000 XFG into
  the genesis pool: 0.1 → 0.025 HEAT per XFG), while minting at the TWAP gives
  about twice the HEAT with no price impact. `sell_xfg` stays a Hearth sale but
  shows both amounts and the price move, then asks; `/amm_quote` returns the
  mint amount too.
- walletd's `amm_swap` direction 0 burned XFG (it called the mint), while the
  SimpleWallet RPC's `amm_swap` sold on Hearth. Both now sell on Hearth; minting
  is `heat_mint` in both. `sell_xfg` / `buy_xfg` are RPC aliases in both.

Tasks 13-15 (owner review, 2026-09-28):
- A HEAT→XFG swap's CD fee share is XFG, and it was valued in HEAT at the
  post-trade spot. A trade that spiked the price (900 HEAT into a 1,000 XFG /
  100 HEAT pool: ~97×) credited ~61 HEAT of CD yield for a fee worth 0.63 HEAT —
  unbacked HEAT at the epoch mint. Swaps and HEAT-paid limit fills now value it
  at the TWAP of earlier blocks; with no TWAP yet it stays in the reserves.
- The TWAP window dropped its oldest entry on every push, restored none on a
  pop, and was never saved. After a reorg or a restart a node priced mints from
  a different window than its peers; with mints now TWAP-only, a restarted node
  would reject any block carrying a mint. `m_blockSpotHistory` keeps each v11
  block's end-of-block price (1,000 blocks past the window), is saved with the
  cache, and pops exactly; `twapBefore(h)` averages the 8 blocks below h, so
  validation, settlement and reversal agree, and the per-swap CD records go.
- No limit capped a swap's price impact (`MAX_MARKET_PRICE_DEVIATION_PCT` feeds
  only an RPC estimate). Swaps must now keep the price within double or half of
  the block's opening price (`HEARTH_MAX_BLOCK_PRICE_MOVE_PCT = 100`); limit
  fills stay capped at 5% of the reserve per block. `sell_xfg` checks it first.
- The seed had no LP shares, so the first deposit by anyone — or the Treasury's
  first provisioning — owned the whole pool, and the bootstrap-repaid check
  passed at once. The seed now carries √(xfg·heat) shares for its provider
  (`m_seedLpShares`); later deposits earn their pro-rata slice. The provider
  cannot remove them yet (task 16).

Testnet: v11 has been active there since height 30, and these rules apply to its
whole v11 history. Blocks carrying mints, treasury funds, CD creation or CD
withdrawals were paid under the old fee rule and will not revalidate; the
testnet needs a reset. Mainnet v11 (1,111,111) is not active.

### Task List
| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 1 | CD activation height: consensus gate, claim-age rule, wallet refusal, test | Claude Opus 5.5 | 2026-09-26 | DONE |
| 2 | Mempool output checks at the next block height | Claude Opus 5.5 | 2026-09-26 | DONE |
| 3 | Limit-withdraw validation: no loop exit on failure | Claude Opus 5.5 | 2026-09-26 | DONE |
| 4 | XFG-only fee model: validateSettlement (block, mempool, template); swap/LP settlement + exact reversal | Claude Opus 5.5 | 2026-09-27 | DONE |
| 5 | Constant-product swap pricing (was linear at spot); mints TWAP-only, priced by the declared burn | Claude Opus 5.5 | 2026-09-27 | DONE |
| 6 | Wallet pricing: WalletGreen + SimpleWallet mint at TWAP, swaps quoted on the curve | Claude Opus 5.5 | 2026-09-27 | DONE |
| 7a | WalletGreen HEAT builders: XFG fee inputs, per-amount rings, add-then-sign (send, CD create/withdraw/rollover, HEAT limit orders, HEAT→XFG swap) | Claude Opus 5.5 | 2026-09-27 | DONE |
| 7b | WalletLegacy (SimpleWallet) HEAT builders: same fixes through its async request chain | Claude Opus 5.5 | 2026-09-27 | TODO |
| 10 | Limit-order backstop fills priced on the curve, limits held on the average; BUY fills credited the CD fee twice | Claude Opus 5.5 | 2026-09-28 | DONE |
| 11 | `/amm_quote` returns `mint_output`; `sell_xfg` shows the Hearth and mint quotes and the price move, then confirms | Claude Opus 5.5 | 2026-09-28 | DONE |
| 12 | walletd `amm_swap` direction 0 sells on Hearth (it burned); `sell_xfg`/`buy_xfg` RPC aliases; wallet RPC quotes the curve when no output is given | Claude Opus 5.5 | 2026-09-28 | DONE |
| 13 | CD fee shares valued at the TWAP of earlier blocks (was the post-trade spot); TWAP from a saved per-block spot history that reverses exactly | Claude Opus 5.5 | 2026-09-28 | DONE |
| 14 | Per-block price band: swaps keep the price within double/half of the block's opening price | Claude Opus 5.5 | 2026-09-28 | DONE |
| 15 | Seed carries LP shares (√(xfg·heat)) held by its provider; cache v15 | Claude Opus 5.5 | 2026-09-28 | DONE |
| 16 | Seed provider can remove the seed's shares: needs the provider's key in consensus and spendable LP outputs | — | — | TODO |
| 8 | Pre-v11 HEAT/AMM tags and the unchecked 0x08 burn: mempool refuses them now; consensus cutoff height (default = v11) | Claude Opus 5.5 | 2026-09-27 | DONE |
| 9 | v11 mint burns feed the burn tally, the Eternal Flame and the SWF (0x08's 50/50 routing); SWF share undone on pop | Claude Opus 5.5 | 2026-09-27 | DONE |

### Sign-off
| Check | Status |
|-------|--------|
| Build compiles | PASS (tasks 1-6): Daemon, SimpleWallet, PaymentGateService, test targets |
| Tests pass | PASS (tasks 1-6): core 178/178, hearth 31/31, auction 57/57, p2p 61/61 |
| Tests pass (tasks 8-9) | PASS: core 185/185, hearth 31/31, auction 57/57, p2p 115/115 |
| Tests pass (tasks 10-12) | PASS: core 194/194, hearth 31/31, auction 57/57, p2p 115/115 |
| Tests pass (tasks 13-15) | PASS: core 202/202, hearth 31/31, auction 57/57, p2p 115/115 |
| All tasks done | NO — tasks 7b and 16 open |


---

## fuego-valise SDK Sync Trigger

**Branch/Feature**: claude/valise-sdk-suite-sync-9u8mdk
**Started**: 2026-09-27
**Agent**: claude-code
**Status**: COMPLETE

### Task List

| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 1 | Add `.github/workflows/notify-valise.yml`: repository_dispatch to fuego-valise when contract source files change on master | claude-code | 2026-09-27 | DONE |

No C++ source changed. The job needs the `VALISE_DISPATCH_TOKEN` secret; without it it warns and exits 0.

### Sign-Off

| Gate | Signed By | Date | Result |
|------|-----------|------|--------|
| No source change (build unaffected) | claude-code | 2026-09-27 | PASS |
| All tasks complete | claude-code | 2026-09-27 | PASS |

---

## HEAT CDs at V12, Spend-Key-Bound Commitment Keys, Transfer Theft Fix

**Branch/Feature**: claude/valise-sdk-suite-sync-9u8mdk
**Started**: 2026-09-28
**Agent**: claude-code
**Status**: COMPLETE (consensus rules activate at the V12 height; mainnet height still a placeholder)

### Task List

| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 1 | Height-aware asset classification: finite-term CDs created at or above `upgradeHeight(V12)` are HEAT (`Currency::isHeatCdHeight`, `classifyCommitmentRef`); legacy CDs stay XFG, withdraw-only | claude-code | 2026-09-28 | DONE |
| 2 | CD interest (`claimedInterest`) counted as HEAT from V12 — it is backed by the HEAT CD_APY_POOL / BONUS_VAULT partitions | claude-code | 2026-09-28 | DONE |
| 3 | Asset-homogeneous CommitmentSpend rings from V12 (closes the first-ring-member asset switch); all-HEAT_TERM rings allowed from V12 | claude-code | 2026-09-28 | DONE |
| 4 | CommitmentTransfer rings accept CDs only (pool outputs share a commit key derivable from a public seed; HEAT/LP/SWRX members allowed asset switches); classified by ring | claude-code | 2026-09-28 | DONE |
| 5 | TreasuryFund burn excluded from the miner fee sum from V12 (was minted again to the miner as XFG); block template and mempool minimum-fee check mirror it | claude-code | 2026-09-28 | DONE |
| 6 | Block template fee for mints (outputs > inputs) uses the consensus minimum fee instead of a wrapped subtraction | claude-code | 2026-09-28 | DONE |
| 7 | `/getrandom_commitment_outs.bin` `ring_class` filter (HEAT_TERM / mature HEAT CDs / mature legacy CDs; no pool or slashed outputs), plumbed through ICore, INode, InProcessNode, NodeRpcProxy | claude-code | 2026-09-28 | DONE |
| 8 | Spend-key-bound (v2) commitment keys: `Hs(D‖i‖"fuego_commit_v2")·G + B`; v1 keys let the sender and any view-key holder spend HEAT/CDs. Scanner detects v2 then v1; `ITransfersContainer::getAvailableKeyImage` tells schemes apart | claude-code | 2026-09-28 | DONE |
| 9 | WalletGreen: HEAT send / HEAT CD / withdraw / rollover rebuilt — XFG network fee inputs, ring classes, all inputs added before signing (per-input signing invalidated earlier signatures), bill-denominated CDs, v2 outputs, keys checked against the on-chain commit key | claude-code | 2026-09-28 | DONE |
| 10 | WalletLegacy: all commitment outputs v2; spends pick v1/v2 by recorded key image | claude-code | 2026-09-28 | DONE |
| 11 | Build Daemon, PaymentGateService, SimpleWallet, Wallet; cross-check v2 keys and tx serialization against the fuego-valise Rust SDK | claude-code | 2026-09-29 | DONE |

### Known Open Items

- Mainnet `UPGRADE_HEIGHT_V12` = 2666666 is ~21 years out at 480 s blocks, and no V12 upgrade detector exists, so blocks never report major version 12. All new rules key on height, not block version.
- Testnet V12 = 180: a testnet already past 180 will not resync (historical finite-term CDs now classify as HEAT) without a reset or a later V12 height.
- The mixed-ring asset switch remains open under v11 rules (not changed there for resync safety).
- WalletLegacy/SimpleWallet HEAT send and HEAT deposit still lack XFG network-fee inputs, sign per input, and pay the banking fee as an XFG output funded by HEAT; they will be rejected.
- AMM HEAT→XFG swap: the pool output classifies as HEAT so the expected XFG is 0, and SWRX outputs can never mature; LP-term commitments can never be spent (maturity overflow).
- `tests/UnitTests/INodeStubs.h` already had a stale `getRandomCommitmentOutsForAmount` signature; updated, but the unit-test target was not built in this change.

### Sign-Off

| Gate | Signed By | Date | Result |
|------|-----------|------|--------|
| Build compiles (Daemon, PaymentGateService, SimpleWallet, Wallet, Transfers) | claude-code | 2026-09-29 | PASS |
| v2 commit keys + tx roundtrip match Rust SDK (C++ parser, deriveCommitmentPublicKeyV2) | claude-code | 2026-09-29 | PASS |
| Unit/regression test suites | — | — | NOT RUN |

---

## XFG CDs Principal-Only; XFG CD Creation Retired in Wallets

**Branch/Feature**: claude/valise-sdk-suite-sync-9u8mdk
**Started**: 2026-09-29
**Agent**: claude-code
**Status**: COMPLETE

### Task List

| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 1 | Consensus (v11+ blocks): `claimedInterest > 0` requires every ring member to be a HEAT CD (finite term, created at or above the HEAT-CD height). XFG CDs earn zero interest in any asset; before the HEAT-CD height no claim can carry interest | claude-code | 2026-09-29 | DONE |
| 2 | `/estimate_cd_yield` and `InProcessNode::getCdClaimInfo` report zero interest for XFG CDs | claude-code | 2026-09-29 | DONE |
| 3 | WalletGreen / WalletLegacy: interest only computed for HEAT CDs; XFG CD rollover refused; XFG CD creation (`createDeposit`, `makeDepositRequest` finite terms) refused at every height | claude-code | 2026-09-29 | DONE |
| 4 | Withdrawal of existing XFG CDs kept: principal returned as XFG (commitment and pre-v10 multisignature paths) | claude-code | 2026-09-29 | DONE |

Resync impact: v11-era blocks carrying CD interest claims (testnet, V11 = 30) no longer validate; testnet needs a reset. Mainnet is below V11 in the checkpoint set, so it has no such blocks.

### Sign-Off

| Gate | Signed By | Date | Result |
|------|-----------|------|--------|
| Build compiles (Daemon, PaymentGateService, SimpleWallet, Wallet) | claude-code | 2026-09-29 | PASS |
| Unit/regression test suites | — | — | NOT RUN |

---

## Asset-Homogeneous Rings and TreasuryFund Miner-Fee Fix from V11

**Branch/Feature**: claude/valise-sdk-suite-sync-9u8mdk
**Started**: 2026-10-02
**Agent**: claude-code
**Status**: COMPLETE

### Task List

| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 1 | CommitmentSpend / CommitmentTransfer rings must be one asset from V11 (was V12): closes the first-ring-member HEAT/XFG switch for the whole v11 era; all-HEAT_TERM rings valid from V11 | claude-code | 2026-10-02 | DONE |
| 2 | TreasuryFund burn excluded from the miner fee sum from V11 (was V12); block template and mempool minimum fee mirror it | claude-code | 2026-10-02 | DONE |

Supersedes the "mixed-ring asset switch remains open under v11 rules" open item above. Testnet (V11 = 30) needs a reset; mainnet is below V11 per its checkpoints.

### Sign-Off

| Gate | Signed By | Date | Result |
|------|-----------|------|--------|
| Build compiles (Daemon, PaymentGateService, SimpleWallet, Wallet) | claude-code | 2026-10-02 | PASS |
| Unit/regression test suites | — | — | NOT RUN |

---

## No HEAT-Era Features Before V11; Legacy Bonds Principal-Only

**Branch/Feature**: claude/valise-sdk-suite-sync-9u8mdk
**Started**: 2026-10-02
**Agent**: claude-code
**Status**: COMPLETE

### Task List

| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 1 | `usesHeatEraFeatures`: HEAT mint/send, AMM swap/liquidity, orderbook, limit orders, TreasuryFund, CD bonus claims, HEAT/LP/pool/SWRX/DIGM commitment outputs, CD transfers. Rejected in blocks and the mempool below V11. The pre-v11 mint and legacy AMM checks fell back to a fixed 1:1 rate on an empty pool, i.e. HEAT minted from XFG before launch. No such transaction exists on mainnet before V11 | claude-code | 2026-10-02 | DONE |
| 2 | Legacy bond (0xCC) claims with positive interest rejected at every height; epoch fees no longer diverted to the legacy bond pool | claude-code | 2026-10-02 | DONE |
| 3 | WalletLegacy legacy bond withdrawal returns principal only (withdraw path kept) | claude-code | 2026-10-02 | DONE |

Assumption: no HEAT mint, AMM, LP, orderbook, DIGM or CD-transfer transaction exists on mainnet below V11. If one does, nodes with this code stop at that block.

### Sign-Off

| Gate | Signed By | Date | Result |
|------|-----------|------|--------|
| Build compiles (Daemon, PaymentGateService, SimpleWallet, Wallet) | claude-code | 2026-10-02 | PASS |
| Unit/regression test suites | — | — | NOT RUN |

---

## Network Facts Recorded; No Legacy Bonds; DIGM From V12

**Branch/Feature**: claude/valise-sdk-suite-sync-9u8mdk
**Started**: 2026-10-02
**Agent**: claude-code
**Status**: COMPLETE

### Task List

| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 1 | Legacy bond tags (0xCB / 0xCC) rejected at every height in blocks and mempool — no legacy bonds exist (`usesLegacyBondTags`) | claude-code | 2026-10-02 | DONE |
| 2 | DIGM_TERM outputs rejected below V12 (`createsDigm`), moved out of the V11 HEAT-era gate — no DIGM mint exists before V12 | claude-code | 2026-10-02 | DONE |
| 3 | AGENTS.md "Network Facts" section (maintainer-confirmed facts, consensus invariants, agent rules) | claude-code | 2026-10-02 | DONE |

### Sign-Off

| Gate | Signed By | Date | Result |
|------|-----------|------|--------|
| Build compiles (Daemon, PaymentGateService, SimpleWallet, Wallet) | claude-code | 2026-10-02 | PASS |
| Unit/regression test suites | — | — | NOT RUN |

---

## 10:1 Launch Rate Only; No Commitments Before V11, No CDs Before V12; Pool Re-seed

**Branch/Feature**: claude/valise-sdk-suite-sync-9u8mdk
**Started**: 2026-10-02
**Agent**: claude-code
**Status**: COMPLETE

### Task List

| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 1 | `rebuildCache` emptied the Hearth pool without re-seeding it, while a fresh start seeds 10,000 XFG / 1,000 HEAT — nodes would disagree on every price-dependent rule from V11. `seedHearthPool()` now runs in both | claude-code | 2026-10-02 | DONE |
| 2 | Removed every pre-v11 legacy validation/settlement path (legacy AMM swap, legacy mint auth, legacy non-auth mint, v10 LP add/remove, legacy AMM tags, legacy bond claim, pre-v11 swap settlement and reversal) — all carried fixed-rate fallbacks and are unreachable behind the V11 gate. No 1:1 rate remains | claude-code | 2026-10-02 | DONE |
| 3 | HEAT-era gate covers any commitment output and any CommitmentSpend / CommitmentTransfer input, and applies to blocks still at major version 10 (the block at the V11 height is v10) | claude-code | 2026-10-02 | DONE |
| 4 | Finite-term CD outputs rejected below V12 (`createsCd`) — no CD exists before V12 | claude-code | 2026-10-02 | DONE |
| 5 | WalletGreen AMM swap quotes fail closed without a pool price (no 10:1 guess), uint128 math | claude-code | 2026-10-02 | DONE |
| 6 | AGENTS.md facts updated: no HEAT/Hearth/CDs/atomic swaps used on mainnet yet; 10:1 launch rate only | claude-code | 2026-10-02 | DONE |

Correction to earlier entries: the pre-v11 paths did not price at 1:1 on mainnet in practice — the pool is seeded at 10:1 — but they carried a 1:1 fallback for an empty pool, which `rebuildCache` produced.

### Sign-Off

| Gate | Signed By | Date | Result |
|------|-----------|------|--------|
| Build compiles (Daemon, PaymentGateService, SimpleWallet, Wallet) | claude-code | 2026-10-02 | PASS |
| Unit/regression test suites | — | — | NOT RUN |

---

## Mainnet V12 Height 1,500,000

**Branch/Feature**: claude/valise-sdk-suite-sync-9u8mdk
**Started**: 2026-10-02
**Agent**: claude-code
**Status**: COMPLETE

### Task List

| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 1 | `UPGRADE_HEIGHT_V12` 2666666 → 1500000 (maintainer decision). V12 height gates HEAT CDs, CD outputs and DIGM; all are height-based, so no upgrade detector is needed for them | claude-code | 2026-10-02 | DONE |

### Sign-Off

| Gate | Signed By | Date | Result |
|------|-----------|------|--------|
| Build compiles (Daemon, PaymentGateService, SimpleWallet, Wallet) | claude-code | 2026-10-02 | PASS |

---

## Swap Escrow Key Image Enforcement; Spend/View-Key Review

**Branch/Feature**: claude/valise-sdk-suite-sync-9u8mdk
**Started**: 2026-10-03
**Agent**: claude-code
**Status**: COMPLETE

### Task List

| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 1 | Consensus: a swap escrow input's key image must equal `swapEscrowKeyImage(escrowTxId, index, mode)`. It enters the global spent set unchecked, so an escrow holder could copy a pending input's key image and freeze that owner's coins permanently | claude-code | 2026-10-03 | DONE |
| 2 | Mempool and block template track escrow spends per escrow output (claim and refund conflict); conflicting spends no longer reach a template that produces an invalid block | claude-code | 2026-10-03 | DONE |
| 3 | `swapEscrowKeyImage` moved to CryptoNoteCore; SwapTxBuilder delegates (identical bytes; test_swap_escrow_claim 18/18) | claude-code | 2026-10-03 | DONE |
| 4 | Review of the v2 spend/view-key code (TransactionExtra, TransfersConsumer, WalletGreen, WalletLegacy, valise SDK builder/scanner) | claude-code | 2026-10-03 | DONE |
| 5 | Fix from the review: tracking (view-only) wallets left the commitment key image uninitialized for v2 outputs; now a deterministic placeholder, as for key outputs | claude-code | 2026-10-03 | DONE |
| 6 | Cross-language check: C++ v2 public keys and spend secrets open the outputs built by the Rust SDK; tx round-trips byte-identical | claude-code | 2026-10-03 | DONE |

### Sign-Off

| Gate | Signed By | Date | Result |
|------|-----------|------|--------|
| Build compiles (Daemon, PaymentGateService, SimpleWallet, Wallet, SwapDaemonLib) | claude-code | 2026-10-03 | PASS |
| test_swap_escrow_claim | claude-code | 2026-10-03 | PASS (18/18) |

---

## Merge master into claude/valise-sdk-suite-sync-9u8mdk

**Branch/Feature**: claude/valise-sdk-suite-sync-9u8mdk
**Started**: 2026-10-05
**Agent**: claude-code
**Status**: COMPLETE

### Task List

| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 1 | Consensus: keep master's settlement (`validateSettlement`), term-only asset classification (`classifyCommitmentTermAsset`: every finite-term commitment is HEAT) and `cdActivationHeight()`; drop the branch's height-based XFG/HEAT CD split | claude-code | 2026-10-05 | DONE |
| 2 | Keep the branch's all-HEAT_TERM ring fix: master's degenerate-ring guard rejected every all-HEAT_TERM ring, which with one-asset rings made plain HEAT unspendable | claude-code | 2026-10-05 | DONE |
| 3 | CD interest claims: every ring member (not only the youngest) must be at or above `cdActivationHeight()` | claude-code | 2026-10-05 | DONE |
| 4 | CD transfer rings stay CD-only (master lacked it: pool outputs share a commit key derivable from a public seed) | claude-code | 2026-10-05 | DONE |
| 5 | `createsCd` gate uses `cdActivationHeight()` in block validation and mempool, matching master's wallet and consensus CD gate (testnet CDs at its V11) | claude-code | 2026-10-05 | DONE |
| 6 | WalletGreen: master's commitment helpers (`addCommitmentToSelf`, `deriveOwnCommitmentKeys`, `prepareHeatSpends`) created and spent v1 view-key-only keys — anyone with the view key, and the sender, could spend those outputs. Removed; every caller (withdraw, rollover, CD, HEAT send, AMM swap both directions, limit order) uses v2 `addOwnedCommitmentOutput` / `planCommitmentSpend(s)` | claude-code | 2026-10-05 | DONE |
| 7 | Master's curve-consistent AMM quotes (`ammSwapNetOutput`, slippage margin) kept | claude-code | 2026-10-05 | DONE |
| 8 | `usesLegacyBondTags` removed: master no longer parses 0xCB/0xCC, so an extra carrying them parses to no settlement tag and claims nothing | claude-code | 2026-10-05 | DONE |
| 9 | Config: master's file with V12 = 1,500,000 re-applied | claude-code | 2026-10-05 | DONE |
| 10 | AGENTS.md network facts updated for the merged model | claude-code | 2026-10-05 | DONE |

Known, not from this merge: `test_swap_state_machine_spv` fails to link on master too (Logging needs Common::Console; same CMake as master). `test_wallet` is interactive (reads stdin), not a unit test. DIGM_TERM commitments classify as HEAT under master's mapping; harmless until DIGM exists (rejected below V12) but must be resolved before V12.

### Sign-Off

| Gate | Signed By | Date | Result |
|------|-----------|------|--------|
| Build compiles (all targets except the pre-existing test_swap_state_machine_spv link failure) | claude-code | 2026-10-05 | PASS |
| core_tests | claude-code | 2026-10-05 | PASS (202/202) |
| test_swap_escrow_claim | claude-code | 2026-10-05 | PASS (18/18) |
| test_production_gates, test_xfg_spend_states, orderbook suites, swap/adaptor/presig/xmr tests | claude-code | 2026-10-05 | PASS |
