# SPV Light-Client Foundation — BCH Proving Slice (Design Spec)

**Status:** Draft (pending user review)
**Date:** 2026-06-04
**Decision record:** [ADR-0001](../../design/2026-06-04-spv-light-client-integration-adr.md)
**Baseline:** [plans/spv_architecture.md](../../../plans/spv_architecture.md)
**Extends:** [chain-interface refactor](2026-05-25-swapxfg-chain-interface-refactor.md)

## 1. Problem & goal

Cross-chain swap verification today requires a **full node per counterparty chain**
(`BchRpcClient`, `EthRpcClient`, …). Scaling `swapXFG` to dozens of chains under
that model makes operating an LP/`SwapDaemon` feasible only for actors who can host
every chain's node — centralizing the swap network.

**Goal of this slice:** build the durable **`ISpvClient` foundation** and prove it
end-to-end on **BCH** via an Electrum SPV backend, so that adding UTXO chains
(BTC/LTC/DASH) later is config + per-chain script params rather than a new full node.
BCH is chosen because its existing `BchRpcClient` serves as a **differential-test
oracle**.

This is the thinnest vertical slice through every layer (walking skeleton). It is
**not** the full multiverse vision — EVM light clients (Helios), Neutrino privacy,
and libp2p discovery are explicitly deferred (§9), with seams left in.

## 2. Scope

**In scope:**
- UTXO-oriented `ISpvClient` interface (generic SPV plumbing, no script semantics).
- `ElectrumSpvClient` implementation (Electrum JSON-RPC over plaintext TCP).
- `SpvHeaderStore` (header-chain sync, PoW link, checkpoint anchor, reorg, persistence).
- BCH routed through SPV for two operations: `verifyLock` (differentially testable)
  and on-chain **secret extraction** (net-new; removes the cooperative-secret
  liveness dependency).
- Additive `ADAPTOR_WAITING_SPV_CONFIRMATIONS` state.
- Multi-server header cross-check (eclipse mitigation — mandatory).
- Config + registration wiring; RPC path retained as oracle.
- Known-answer + differential + multi-server tests.

**Out of scope (seams left, see §9):** Neutrino/BIP-157-158; EVM/Helios; libp2p;
removing the BCH full node from docs; first-class TLS transport; ripping out the
cooperative P2P secret path.

## 3. Architecture (ADR-0001, Approach A)

A new `ISpvClient` is **injected optionally** into existing `IChainClient`
implementations as a verification backend. When present, the chain client routes
`verifyLock` + secret extraction through SPV; when absent, it uses the full-node RPC
path. The `IChainClient` contract and the `ChainRegistry` dispatch are unchanged.

### 3.1 `ISpvClient` interface (UTXO-shaped; no chain-specific script logic)

```cpp
// src/SwapDaemon/Spv/ISpvClient.h
namespace XfgSwap {

struct SpvTxInclusion { bool included=false; uint64_t blockHeight=0; uint32_t depth=0; bool merkleVerified=false; };
struct SpvSpend       { bool spent=false; std::string spendingTxid; std::vector<uint8_t> rawSpendingTx; SpvTxInclusion inclusion; };

class ISpvClient {
public:
  virtual ~ISpvClient() = default;
  virtual std::string protocolName() const = 0;                                   // "electrum"
  virtual bool syncHeaders() = 0;                                                  // advance + cross-check to tip
  virtual bool getTipHeight(uint64_t&) = 0;
  virtual bool verifyTxInclusion(const std::string& txid, SpvTxInclusion&) = 0;    // Merkle-verified vs our header store
  // findSpend internally derives the watched scripthash from the funding output
  // (getRawTx(txid) → output[vout].scriptPubKey → scripthash → get_history).
  virtual bool findSpend(const std::string& txid, uint32_t vout, SpvSpend&) = 0;
  virtual bool getRawTx(const std::string& txid, std::vector<uint8_t>&) = 0;
};

} // namespace XfgSwap
```

> **Deferred (not in this slice):** `findFunding(scriptPubKey, minSat)` + `SpvFunding`
> — only needed when the funding txid is *unknown* upfront (e.g. AFK pre-lock discovery).
> Alice always holds `ctrLockTxId` here, so `verifyTxInclusion` covers Flow 1. Add when a
> txid-less discovery path appears.

### 3.2 Components

| Unit | Responsibility | Depends on |
|------|----------------|-----------|
| `ISpvClient` | Generic SPV query contract | SwapTypes |
| `ElectrumSpvClient` | Electrum JSON-RPC; multi-server cross-check; orchestrates header store + Merkle verify | TCP socket; `SpvHeaderStore`; `BchHtlcScript::doubleSha256` |
| `SpvHeaderStore` | Persist/validate header chain (link + PoW + checkpoint + cumulative work); reorg | — |
| `BchChainClient` (modify) | Route `verifyLock` + new `extractSecret` through SPV when injected, else RPC | optional `ISpvClient`; `BchHtlcScript` (P2SH reconstruct, scriptSig parse) |
| `SwapStateMachine` (modify) | Add `ADAPTOR_WAITING_SPV_CONFIRMATIONS` + transitions + (de)serialize | — |
| `ChainClientResult` (modify) | Add `pending`/`confirmations`/`requiredConfirmations` + `pendingConfs()` | — |

**Separation:** `ISpvClient` returns *raw* spending-tx bytes; preimage parsing and the
`SHA256(preimage)==hashlock` check live in `BchChainClient`/`BchHtlcScript`. SPV
plumbing stays chain-agnostic → reused by BTC/LTC/DASH unchanged.

## 4. SPV verification core

### 4.1 Header chain
Electrum: `blockchain.headers.subscribe`, `blockchain.block.headers(start,count)`,
`blockchain.block.header(height, cp_height)`. BCH header = 80 bytes
(`version|prevHash|merkleRoot|time|bits|nonce`); hash = `doubleSHA256(header)`.
Per-header validation:
1. **Link** — `prevHash == doubleSHA256(prevHeader)`.
2. **PoW** — `doubleSHA256(header) ≤ target` (target from compact `bits`).
3. **Anchor + selection** — validate forward from a **hardcoded recent checkpoint**
   (config `bch_spv_checkpoint_{height,hash}`); among competing tips choose **most
   cumulative work** (Σ difficulty).

**Security boundary (explicit):** this slice does **not** recompute BCH's ASERT
difficulty per header. Checkpoint-anchor + PoW≤target + most-work + multi-server
agreement bounds an attacker to forging real-difficulty PoW from a recent checkpoint
or being caught by server disagreement. Full DAA recomputation is a hardening
follow-up (§9), not implied here.

### 4.2 Merkle inclusion
`blockchain.transaction.get_merkle(txid)` → `{block_height, merkle[], pos}`. Fold
`txid` (internal byte order) with each branch hash (order by `pos` bit, `doubleSHA256`
per level); result **must equal our validated header store's `merkleRoot` at
`block_height`** — never a server-supplied inline root. `depth = tip − block_height + 1`.

### 4.3 Multi-server cross-check (eclipse mitigation, mandatory)
Connect to ≥ `bch_spv_min_servers` (default 2). Require agreement on the header hash
at `tip − k` (small jitter buffer `k`); on buried-header disagreement, **refuse to
advance and never emit a false "verified."** Honest limit: reduces, does not
eliminate, eclipse risk (attacker controlling all configured servers still wins);
strength scales with server diversity.

### 4.4 Reorg
On `prevHash` mismatch at `tip−1`, walk back to the fork, compare cumulative work,
switch to the heavier chain, recompute depths. A lock tx reorged below `minConfs`
reverts the swap `WAITING → PRESIGS_READY`.

### 4.5 Binding inclusion to *our* HTLC (correctness)
Inclusion alone is insufficient. `verifyLock`(SPV) also calls `getRawTx(ctrLockTxId)`
and confirms **some output's `scriptPubKey == P2SH(redeemScript)`** (redeem script
reconstructed from `params`/`chainState`) **and `value ≥ ctrAmount`**.

## 5. State machine

- **Enum** (`SwapTypes.h`): add `ADAPTOR_WAITING_SPV_CONFIRMATIONS = 17` (after
  `ADAPTOR_REFUNDED = 16`); add `swapStateToString` case; permit in `isValidTransition`
  and (de)serialization.
- **Transitions (SPV verifier path only):**
  - `PRESIGS_READY → WAITING_SPV_CONFIRMATIONS` (tx seen, depth < minConfs)
  - `WAITING_SPV_CONFIRMATIONS → CTR_LOCKED` (depth ≥ minConfs, Merkle-verified, output matches)
  - `WAITING_SPV_CONFIRMATIONS → PRESIGS_READY` (reorged out) | `→ FAILED` (timeout)
- **`ChainClientResult` additions** (additive, backward-compatible):
  `bool pending=false; uint32_t confirmations=0; uint32_t requiredConfirmations=0;`
  plus `static ChainClientResult pendingConfs(txId, conf, required)`.
- **Driver edit (one additive branch)** in `handlePreSigsReady` Alice path
  ([SwapDaemon.cpp:697](../../../src/SwapDaemon/SwapDaemon.cpp)): `result.pending` →
  record WAITING; `result.success` → CTR_LOCKED (as today); else retry. **RPC pairs
  never set `pending`** → SOL/ETH/XMR/ARB/BASE flows unchanged. The registry dispatch
  itself is unchanged; only this state-mapping branch is added.
- Rationale: primarily observability + restart-robustness; correctness is provided by
  the existing per-tick retry. Included because the baseline asks for it and it aids
  status/TUI visibility.

## 6. Data flows

**Flow 1 — Alice verifies Bob's lock (differentially testable).**
`handlePreSigsReady`(Alice) → `client->verifyLock` → SPV: `syncHeaders` (cross-checked)
→ `verifyTxInclusion(ctrLockTxId)` → `getRawTx` output/amount check (§4.5) →
return `ok` (depth≥N) / `pendingConfs` (seen, shallow) / `fail` (unseen). Driver maps
to CTR_LOCKED / WAITING / retry. **Oracle:** `BchRpcClient::verifyLock` must agree on
confirmed-vs-not for the same chain state.

**Flow 2 — Bob extracts the secret on-chain (net-new; the trust reduction).**
After CTR_LOCKED, `handleCtrLocked`(Bob) polls new `BchChainClient::extractSecret` →
SPV `findSpend(ctrLockTxId, vout=0)` (vout 0 matches existing claim/refund calls at
[BchChainClient.cpp:34](../../../src/SwapDaemon/BitcoinCash/BchChainClient.cpp)) → if
spent + Merkle-verified, parse the spending input's claim scriptSig
(`<sig> <preimage> OP_TRUE <redeemScript>`), extract `preimage`, check
`SHA256(preimage)==committed hashlock` (subject to §9 hashlock item) → return the
adaptor secret. **Wiring (scoped):** SPV extraction is a **fallback secret source**
into the existing `handleSecretRevealed` escrow-spend path — Bob still tries
cooperative P2P first; SPV removes the *dependency* on it. The cooperative path is
**not** removed this slice. **Test:** no RPC oracle → known-answer test (build a claim
scriptSig via `BchHtlcScript`, run extraction, assert preimage + `SHA256` gate).

## 7. Config & registration

`fuego_swapd.json`:
```json
"bch_spv_enabled":           true,
"bch_electrum_servers":      ["electrum1.host:50001", "electrum2.host:50001"],
"bch_spv_min_confs":         6,
"bch_spv_min_servers":       2,
"bch_spv_checkpoint_height": 800000,
"bch_spv_checkpoint_hash":   "<header hash hex>"
```
- **Backward compatible:** absent/`false` → `BchChainClient` is RPC-only (today).
- **Registration** (BCH constructor block, `SwapDaemon.cpp`): build `BchRpcClient` as
  today; if SPV enabled, build `ElectrumSpvClient(servers, minServers, checkpoint)` and
  pass it (+`minConfs`) to the `BchChainClient` ctor:
  `BchChainClient(std::unique_ptr<BchRpcClient> rpc, const std::string& wif, std::unique_ptr<ISpvClient> spv = nullptr, uint32_t spvMinConfs = 6);`
- **Config validation:** if `bch_spv_enabled`, require
  `bch_electrum_servers.size() ≥ bch_spv_min_servers` and a non-empty checkpoint;
  otherwise log an error and fall back to RPC-only — never run SPV with an
  unsatisfiable quorum (which would silently never verify).
- **Transport:** plaintext TCP (local Fulcrum or TLS-terminating proxy), mirroring
  `EthRpcClient`. First-class TLS = follow-up.

## 8. Error handling

**Rule: SPV uncertainty must never produce a false "verified." Every ambiguity →
retry/WAITING, never advance.**

| Condition | Handling |
|---|---|
| Quorum unmet (< `minServers`) | `fail("spv quorum unavailable")` → retry; never advance |
| Servers disagree on buried header | refuse to advance; eclipse-risk warn; stay WAITING/PRESIGS_READY |
| Merkle proof fails / root mismatch | treat as not-included; retry; warn if persistent |
| Seen but depth < minConfs | `pendingConfs` → WAITING; keep polling |
| Reorg drops lock below inclusion | revert WAITING → PRESIGS_READY; re-verify |
| `getRawTx` output/amount mismatch | `fail` (wrong/insufficient lock); do not advance |
| Spend seen, scriptSig not claim-path/malformed | extraction false → cooperative fallback; no crash |
| `SHA256(preimage) ≠ hashlock` | reject extracted secret; log; keep watching |
| Swap timeout while WAITING | existing timeout/refund path |

Plus: per-server connect/read timeouts; bounded header-batch sizes; backoff on the
existing tick cadence.

## 9. Open questions / flagged items

1. **Hashlock semantics (pre-existing).** `BchChainClient::lock` passes
   `params.adaptorPoint` (T=t·G) as the HTLC `hashLockSha256Hex`
   ([BchChainClient.cpp:15](../../../src/SwapDaemon/BitcoinCash/BchChainClient.cpp))
   while `claim` reveals `params.adaptorSecret`. A SHA-256 HTLC needs
   `hashlock == SHA256(preimage)`. Confirm before wiring the extraction equality check.
2. **DAA recomputation** deferred (§4.1) — note the security boundary.
3. **First-class TLS** for Electrum deferred (§7).
4. **`vout=0` assumption** for the HTLC output (matches existing claim/refund) — confirm
   the lock tx always places the HTLC at output 0.
5. **Test harness location/framework** — align with existing `src/SwapDaemon/tests/`
   (verify framework during planning).
6. **Header-store persistence backend** — file vs reuse `SwapDatabase`; decide in plan.

## 10. File map (basis for the implementation plan)

| Action | Path | Responsibility |
|--------|------|----------------|
| Create | `src/SwapDaemon/Spv/ISpvClient.h` | Interface + result structs |
| Create | `src/SwapDaemon/Spv/SpvHeaderStore.{h,cpp}` | Header chain validate/persist/reorg |
| Create | `src/SwapDaemon/Spv/ElectrumSpvClient.{h,cpp}` | Electrum JSON-RPC; multi-server; Merkle verify |
| Create | `src/SwapDaemon/Spv/ElectrumConnection.{h,cpp}` | TCP socket + JSON-RPC framing per server |
| Modify | `src/SwapDaemon/ChainClientResult.h` | `pending`/`confirmations`/`requiredConfirmations` + `pendingConfs()` |
| Modify | `src/SwapDaemon/BitcoinCash/BchChainClient.{h,cpp}` | Optional `ISpvClient`; SPV `verifyLock`; new `extractSecret` |
| Modify | `src/SwapDaemon/BitcoinCash/HtlcScript.{h,cpp}` | Parse claim scriptSig → preimage; build P2SH spk from redeem script |
| Modify | `src/SwapDaemon/SwapTypes.{h,cpp}` | `ADAPTOR_WAITING_SPV_CONFIRMATIONS` + string + (de)serialize |
| Modify | `src/SwapDaemon/SwapStateMachine.cpp` | `isValidTransition` for the new state |
| Modify | `src/SwapDaemon/SwapDaemon.cpp` | additive WAITING branch in `handlePreSigsReady`; Bob `extractSecret` poll; BCH registration |
| Modify | `src/SwapDaemon/SwapDaemon.h` + `ChainClientConfig.cpp` | `bch_spv_*` config fields + parsing |
| Create | `src/SwapDaemon/tests/test_spv_merkle.cpp` | Known-answer Merkle proof |
| Create | `src/SwapDaemon/tests/test_spv_headers.cpp` | Header link/PoW/reorg/checkpoint |
| Create | `src/SwapDaemon/tests/test_spv_extract_secret.cpp` | Preimage extraction + SHA256 gate |
| Create | `src/SwapDaemon/tests/test_spv_vs_rpc_difftest.cpp` | Differential `verifyLock` (SPV vs RPC) on fixtures |
| Create | `src/SwapDaemon/tests/TestElectrumServer.h` | Replays recorded/synthetic Electrum responses (named `Test…`, not "mock") |

## 11. Expansion targets

Post-slice targets recorded with light-client-family grouping in
[ADR-0001 §Expansion Targets](../../design/2026-06-04-spv-light-client-integration-adr.md):
**BTC/LTC/DASH** (direct `ElectrumSpvClient` reuse), **DCR** (infra spike — dcrwallet
SPV, not ElectrumX), **POLYGON** (EVM RPC now / Helios later — not `ISpvClient`),
**ATOM** (Tendermint light client — separate family).
