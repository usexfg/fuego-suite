# WAVE 1 PACKAGE — Initial Design (Phase 1, multi-agent-brainstorming)

Status: **REVISE — returned to Primary Designer after full review loop (Skeptic → Constraint Guardian → User Advocate → Arbiter); disposition and mandatory revision items in D14**
Date: 2026-08-14
Owner: Primary Designer (okoc chain)
Process: `multi-agent-brainstorming` skill, Phase 1 output. Understanding Lock: **CONFIRMED** ("Confirm, proceed").

---

## 1. Scope

Wave 1 package, one design, 8 sub-features:

| Ref | Feature | Surface |
|-----|---------|---------|
| #1  | Invoices (subaddress-bound, QR) | wallet RPC + core RPC (read-only) |
| #33 | Encrypted memos | wallet RPC (reuses existing tx-extra 0x04) |
| #35 | Invoice expiry + late-payment handling | wallet RPC |
| #21 | One-click privacy (fresh-subaddress default, ring size) | wallet RPC |
| #34 | Send-as-HEAT (single user action) | wallet RPC |
| #11 | Term-weighted CD yield | core (consensus-touching) |
| #31 | MCP payment tools | mcp/fuego-mcp-server |
| #26 | Header-verifiable price feed | core RPC + docs/examples (header field staged — A1 failed) |

Reviewed as ONE package (user decision). Waves 2–4 queued after.

---

## 2. Verified codebase facts (Phase 0)

- **Payment IDs**: `RpcServer.cpp` `/paymentid` (:136), `on_get_payment_id` (:1595), `txDetails.paymentId` (:1866-1870); `InProcessNode.cpp` `getTransactionsByPaymentId` (:974-1072); WalletLegacy; PaymentGate. → Payment IDs are a known privacy leak (user-confirmed); deprecation plan required.
- **Subaddresses**: `src/Wallet/WalletGreen.h:33` includes `crypto/subaddress.h` only. **Zero RPC/GUI surface** → subaddress lifecycle (create/list/label/derive) must be built.
- **P_clear**: `OrderbookTypes.h:93`, `Core.h:258-262` (clearingPrice + hearthPoolRatio), `PoolOrderOrchestrator.h:41-56` (30-block trailing avg), `OrderbookMatcher.h:45`. Header serialization: **only `hearthPoolRatio` is header-serialized** (`CryptoNoteSerialization.cpp:518`); P_clear + depth are NOT — **A1 RESOLVED (failed)**: #26 needs a new header field, staged at next block-version bump (D7.1, D10).
- **CD yield core**: `Blockchain.h:165 getCdYieldPool()` → `m_cdYieldPool` (:459, field :400); `CommitmentType::YIELD` (`DepositCommitment.h:89`); `TX_EXTRA_YIELD_COMMITMENT 0x07` (`TransactionExtra.h:54`); `CdOfferRelay.h` has **no yield symbols**; `HeatMintEngine.cpp/h` exist (TWAP mint path). **Real interest model (A3 RESOLVED)**: `Currency::calculateCdInterest` (`Currency.cpp:330-401`) accrues per-epoch at the pool-derived epoch fee rate (`CommitmentIndex.getEpochFeeRate(e)`), NOT fixed term APRs; loyalty tiers keyed to term-in-blocks — 72 epochs (`DEPOSIT_MAX_TERM`) → 2.5×, 36 → 2.0×, 18 → 1.5×, 6 → 1.25× — on the ORIGINAL amount, final epochs only; auto-roll compounds base interest only (loyalty never compounds; auto-roll disabled until v13, `Blockchain.cpp:2393`). `CDTermCode`/`CDAPRRate` (`TransactionExtra.h:494-506`) are **legacy/unused** (definitions only; deposits use term-in-blocks, `WalletGreen.cpp:1485`). Yield settlement (`Blockchain.cpp:5136-5241`): V12+ XFG-denominated pool → HEAT at pool rate; XFG burned EF/SWF 50/50 (`MINT_BURN_EF_PCT`/`MINT_BURN_TREASURY_PCT`); HEAT → `heatSupply`/`heatCdFeePool`/`feePoolBalance`/vault `CD_APY_POOL`; epoch split logged 80% cdShare / 20% treasuryShare (`Blockchain.cpp:5293-5294`); loyalty bonuses paid from fee pool at claim (F-001 caps, `:5227-5228`); claims capped by youngest ring member's epoch rate (`Blockchain.cpp:2465-2492`). **Mint RPC (A5 RESOLVED)**: `mintHeatV10` exposed at `WalletGreen.cpp:1213`, `WalletRpcServer.cpp:1037` (`xfg_burned, heat_minted, fee, mixin`), `WalletService.cpp:2533`, `SimpleWallet.cpp:4094`, `TestnetWallet.cpp:182`.
- **MCP**: `mcp/fuego-mcp-server/` TypeScript; `src/tools/` = `context-tools.ts`, `file-tools.ts` (pattern source for new tools).

---

## 3. Foundation — Payment Addressing Layer (PAL)

Required by #1/#21/#33/#35. No payment IDs in new flows.

### 3.1 New wallet RPC family (core `Wallet` service)

- `subaddress_create(label)` → subaddress + derivation index
- `subaddress_list()` → all subaddresses, labels, total_received
- `subaddress_label(index, label)` → rename
- `subaddress_derive(index)` → refresh balance for that subaddress (idempotent)
- `subaddress_get_payments(index, since_height)` → payment txs bound to subaddress

### 3.2 Payment-ID deprecation (3-phase, D2)

1. **Phase 1 (this wave)**: stop issuing new payment IDs in wallet flows; log deprecation warning on `/paymentid` use.
2. **Phase 2**: soft-deprecate `/paymentid`; add daemon flag `--reject-legacy-payment-ids` (opt-in).
3. **Phase 3 (future wave)**: remove `/paymentid`, `getTransactionsByPaymentId`, PaymentGate; payment IDs rejected at tx-extra parse.

Invoice binding is **not** payment IDs — binding is the subaddress itself (D1).

---

## 4. Feature designs

### 4.1 #1 — Invoices (wallet-side state, D1)

- State: off-chain JSON in wallet cache (`invoices.json`): id, subaddress, amount, currency, label, created, expires, status.
- RPC:
  - `invoice_create(amount, currency=xfg|heat, label?, expires_in_hours?)` → creates subaddress (PAL), returns invoice + QR payload
  - `invoice_status(invoice_id)` → pending / partial / paid / expired / late; paid amount = subaddress `total_received` (partial payments work naturally)
  - `invoice_list(status?)`
- QR URI: `xfg:<subaddress>?amount=<atomic>&invoice=<id>` (D1.1). Wallet import accepts URI.
- Receipt: on-chain tx + subaddress binding; no memo required.

### 4.2 #33 — Encrypted memos (encrypted-only, D3; wallet-layer only — D9)

- **Reuses existing `TX_EXTRA_MESSAGE_TAG 0x04`** (`TransactionExtra.h:28-66` tag map; ECIES path already present at `CryptoNoteFormatUtils.cpp:219-220`) — **no new tx-extra tag** (0x08 is taken by HEAT_COMMITMENT; any new tag is consensus-affecting per `TransactionExtra.cpp:449` `default: return false`).
- Wallet-defined sub-marker inside the 0x04 payload distinguishes memos from other messages (wallet-layer only, no consensus change).
- Payload: ECIES to recipient's subaddress spend key. Max 512 B.
- **Plaintext memos rejected** (metadata leak; D3.1). Unencrypted memo attempts error at RPC.
- Wallet RPC: `memo_set(subaddress, text)` / `memo_get(subaddress)` — decrypt only by recipient wallet.
- Consensus note: **none** — no new tag, additive payload within existing tag; no hard fork required (D9).

### 4.3 #35 — Expiry + late payments (D4)

- Expiry is **quoting expiry**, not auto-refund: RingCT hides sender, so refund-to-sender is impossible (D4.1).
- `invoice_create` accepts optional `refund_address` (payer's address).
- Late payment (after expiry): auto-returned to `refund_address` if set; else credited at current rate and marked `late` (D4.2).
- No burning, no confiscation. CD-grade honesty: funds are never lost.

### 4.4 #21 — One-click privacy (address hygiene, not churn — D8)

- `wallet_shield()` RPC: creates a fresh subaddress and sets it as default receive target; optional ring-size override (OSP_EAD default 11; user-selectable 3/7/11/15).
- Fresh-subaddress-by-default: **ON** for new wallets; existing wallets get a one-time `shield` prompt (D8.1).
- Explicitly **NOT** churn (user correction): no moving of funds, no self-spends.

### 4.5 #34 — Send-as-HEAT (two-step internals, one user action — D5)

- Single RPC `send_heat(to_address, heat_amount)`.
- Internals:
  1. Quote: TWAP price from `mintHeatV10` path (**A5 RESOLVED** — already exposed at `WalletRpcServer.cpp:1037`, `xfg_burned/heat_minted/fee/mixin`; wraps existing RPC, no new mint path).
  2. Mint: burn XFG → mint HEAT (0% premium).
  3. Transfer: send HEAT to `to_address`.
- Failure handling: mint fails → nothing sent; transfer fails → HEAT stays in sender wallet (recoverable, no loss).
- UI disclosure: quote shown pre-confirm; both fees disclosed (burn+XFG transfer fee; HEAT transfer fee).

### 4.6 #11 — Term-weighted CD yield (consensus-touching; D6 rebased — D12)

- **Rebased on the real interest model (A3 RESOLVED)**: interest accrues per-epoch at the pool-derived epoch fee rate (`Currency::calculateCdInterest`, `Currency.cpp:330-401`) — there is NO fixed term APR schedule (`CDTermCode`/`CDAPRRate` enums at `TransactionExtra.h:494-506` are legacy/unused). The original D6 premise was wrong on both counts.
- Term weights: 1× / 3× / 6× / 12× for terms 6 / 18 / 36 / 72 epochs, applied as a weight on the epoch-rate accrual in `calculateCdInterest` (single formula change).
- Redistribution **within the existing CD yield pool only** — no new earnings source (AGENTS.md protocol constraint; observed epoch split 80% cdShare / 20% treasuryShare, `Blockchain.cpp:5293-5294`). Loyalty bonus tiers (2.5×/2.0×/1.5×/1.25× on original amount, final epochs only) are **untouched**.
- Settlement: weight applied inside `calculateCdInterest` per CD term; no change to yield-pool settlement (`Blockchain.cpp:5136-5241`).
- RPC: `getTermWeightedApy(term)` (read-only; projected APY display, #27 later).
- Independent of 11% bonus vault (multipliers stay as-is).
- **Consensus flag**: changes effective balances → requires block-version bump / staged activation (D12; same staging as D7.1/D10).

### 4.7 #31 — MCP payment tools

- New tool group in `mcp/fuego-mcp-server` mirroring `file-tools.ts`/`context-tools.ts` patterns (registration, schema, error handling):
  - `invoice_create` / `invoice_status`
  - `wallet_balance`
  - `wallet_send` (XFG or HEAT via #34 path)
  - `cd_deposit_status`
  - `shield`
- Auth: MCP server already gated (per existing architecture); payment tools additionally require wallet password/keystore access confirmation per call (agent-usable, human-approved).

### 4.8 #26 — Header-verifiable price feed (D7)

- `price_proof(height)` RPC → P_clear, hearthPoolRatio, depth stats + merkle/serialized header field for verification.
- Reference verifier: `docs/examples/price_feed_verify.py` — client-side header verification (independent of RPC trust).
- **A1 RESOLVED (failed)**: only `hearthPoolRatio` is header-serialized (`CryptoNoteSerialization.cpp:518`); P_clear + depth are NOT. → new header field required, staged at next block-version bump (D7.1; staged, not hot-patched — D10).

---

## 5. Open verification risks

| ID | Risk | Gate |
|----|------|------|
| A1 | P_clear + depth stats in serialized header | **RESOLVED (failed)** — only `hearthPoolRatio` in header (`CryptoNoteSerialization.cpp:518`); #26 needs new header field, staged (D10) |
| A5 | `mintHeatV10` TWAP path callable from wallet automation | **RESOLVED** — already exposed (`WalletRpcServer.cpp:1037`); #34 wraps existing RPC |
| A3 | CD yield pool epoch-settled, term-weighting = single formula change | **RESOLVED** — real model: per-epoch fee-rate accrual + loyalty tiers (`Currency.cpp:330-401`); #11 = single formula change, D6 rebased (D12) |

---

## 6. Decision Log (D1–D14)

| ID | Decision | Alternatives considered | Objections | Resolution / rationale |
|----|----------|------------------------|------------|------------------------|
| D1 | Invoice state wallet-side (off-chain JSON) | On-chain invoice registry | On-chain = permanent metadata, fees, no privacy | Off-chain state + subaddress binding; receipt = tx. Resolves Q1 |
| D2 | Payment-ID deprecation in 3 phases | Immediate removal | Breaking change for existing integrations | Staged: stop issuing → soft-deprecate → remove. Privacy fix without breakage |
| D3 | Memos encrypted-only | Plaintext + encrypted mixed | Plaintext leaks metadata | Encrypted-only; plaintext rejected at RPC. Resolves Q4 |
| D4 | #35 expiry = quoting expiry + optional refund_address | Auto-refund to sender | RingCT hides sender | Refund_address opt-in; late = credit-at-current-rate or return. No fund loss |
| D5 | #34 two-step (mint → transfer) under one RPC | Single atomic consensus tx | Complexity | Single user action; internal two-step; failure keeps HEAT in sender wallet. Resolves Q5 |
| D6 | #11 term weights 1×/3×/6×/12× (6/18/36/72) | Flat per-epoch, exponentiated | Simplicity vs incentive alignment | Linear weights within existing 69% pool; no new earnings. Resolves Q2 |
| D7 | #26 price proof RPC-first, header field only at next bump | Hot-patch header now | Consensus risk | RPC + reference verifier now; header field staged with block-version bump if A1 fails |
| D8 | #21 = fresh-subaddress default + ring size; NOT churn | Wallet churn for privacy | User explicitly ruled out churn | Address hygiene; no fund movement. One-time shield prompt for existing wallets |
| D9 | #33 reuses existing 0x04 (wallet sub-marker), no new tag | New `TX_EXTRA_MEMO` tag | 0x08 taken (HEAT_COMMITMENT); new tag = consensus change (`TransactionExtra.cpp:449`) | Wallet-layer marker inside existing ECIES tag; no hard fork. Kills Skeptic objections 1–3 |
| D10 | #26 header field confirmed necessary, staged | RPC-only price proof | Consensus risk of hot-patching header | A1 RESOLVED (failed): only `hearthPoolRatio` serialized (`CryptoNoteSerialization.cpp:518`); add P_clear+depth at next block-version bump |
| D11 | #34 wraps existing `mintHeatV10` RPC | New consensus mint path | A5 gate | A5 RESOLVED: exposed at `WalletRpcServer.cpp:1037`; no new mint path, no consensus change |
| D12 | #11 rebased: term weight on epoch-rate accrual in `calculateCdInterest`; weights 1×/3×/6×/12×; no APR table | Fixed term APR schedule (D6 original) | `CDTermCode`/`CDAPRRate` legacy/unused; real interest = per-epoch fee-pool rate + loyalty tiers (A3) | Single formula change in `Currency.cpp:330-401`; loyalty tiers untouched; within existing CD yield pool; staged at block-version bump |
| D13 | Skeptic objection list truncated in transcript (17 raised, 4 visible) | — | — | Noted in doc; remaining objections reviewed via documented facts + subsequent reviewers |
| D14 | **Arbiter disposition: REVISE** | APPROVED / REJECT | F-001: #11 as specified is not pool-conserving (claims on weighted exposure vs unweighted settlement `Blockchain.cpp:4939`) — Σ claims > inflow; F-101: #1 has no URI import surface in codebase or design — invoice loop unclosed for users; F-004: #26 header field and #11 activation must share ONE block-version bump with deterministic validator recomputation | Full loop run (Skeptic → CG F-001…F-009 → UA F-101…F-109); REVISE with 3 mandatory revision items + carried conditions (see Arbiter report); verification re-review of revised sections after designer revision |

---

## 7. Review status

- [x] Understanding Lock confirmed
- [x] Initial design produced (this doc)
- [x] Decision Log started
- [x] Skeptic / Challenger review (17 objections; 1–4 applied — tag collision, hard-fork, 0x04 reinvention, core-crypto; rest truncated in transcript — D13)
- [x] Constraint Guardian review (F-001…F-009, Verdict: CONDITIONS)
- [x] User Advocate review (F-101…F-109, Verdict: CONDITIONS)
- [x] Integrator / Arbiter disposition — **REVISE** (D14): 3 mandatory revision items + carried conditions; re-verify revised sections after designer revision

Pending: designer revision per D14 → verification re-review (Arbiter) → wave approval.