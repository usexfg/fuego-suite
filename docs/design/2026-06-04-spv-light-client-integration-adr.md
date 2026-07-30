# ADR-0001: SPV Light-Client Integration for SwapDaemon

**Status:** Accepted
**Amendment:** 2026-06-17 — KMD SPV added (SpvKmdChainClient, SwapPair::KMD_SPV). Same Electrum protocol, KMD address prefixes (0x3C P2PKH, 0x55 P2SH, WIF 0xBC).
**Amendment:** 2026-06-16 — revised to SPV-only (Option C + A safety). Full RPC fallback removed from SpvBchChainClient. The BCH proving slice ships SPV-only; the RPC-based BchChainClient remains available for side-by-side differential testing during the proving phase but is not linked into the SPV client.
**Amendment:** 2026-07-18 — chain priorities updated. Added BNB (EVM clone, ARB/BASE pattern, chainId 56). Added TON (priority, TVM smart contracts, Ed25519, async messaging). Added Zano (CryptoNote, native HTLC outputs). Added Sia (decentralized storage for DIGM). Removed DASH/ATOM/Kaspa/Aztec/Beam/DOGE from active expansion targets.
**Date:** 2026-06-04
**Deciders:** Swap maintainers (ColinRitman)
**Scope:** This ADR decides *how the SPV layer integrates* with the existing
`IChainClient`/`ChainRegistry` architecture, and the first proving slice. The
full slice design (header-chain sync, Merkle verification, the new state, config,
testing) lives in the in-flight brainstorming spec, not here.

## Context

Cross-chain atomic swaps currently verify counterparty-chain state through a
**full node per chain**: `BchRpcClient`, `EthRpcClient`, `SolRpcClient`,
`MoneroRpcClient`, each wrapped by an `IChainClient` adapter and dispatched via
`ChainRegistry` ([IChainClient.h:10](../../src/SwapDaemon/IChainClient.h)).

The chain-interface refactor
([2026-05-25-swapxfg-chain-interface-refactor.md](../superpowers/plans/2026-05-25-swapxfg-chain-interface-refactor.md))
made adding a chain "one new class + registration." Its own "Adding a New Chain"
template anticipates BTC/LTC/DCR by cloning `BchChainClient` — **but each cloned
chain still requires the operator to run that chain's full node.** Scaling
`swapXFG` to the dozens of chains in the expansion roadmap
([swap_expansion_guide.md](../../plans/swap_expansion_guide.md)) under that model
makes Liquidity-Provider and `SwapDaemon` operation feasible only for
well-funded actors who can host BTC + LTC + KMD + DCR + ETH + … nodes
simultaneously. That centralizes the swap network.

The SPV baseline ([spv_architecture.md](../../plans/spv_architecture.md)) proposes
Simplified Payment Verification — download block headers, request Merkle
inclusion proofs for specific transactions, verify against the longest
proof-of-work chain — to remove the full-node requirement.

**Forces at play:**
- **Security** — counterparty lock verification governs fund safety. An SPV
  client that can be lied to (eclipse attack) or that mis-verifies a Merkle proof
  directly enables theft.
- **Privacy** — SPV servers can observe which transactions/scripts a node queries.
- **Operational scalability** — the goal: add UTXO chains without per-chain full nodes.
- **Minimal disruption** — the `IChainClient`/`ChainRegistry` refactor just landed;
  the SPV layer should extend it, not rework it.

Two distinct SPV read-operations are required and they differ in testability:
1. **`verifyLock`** — prove the counterparty HTLC funding is included + buried N
   deep. The RPC path exists (`BchRpcClient::verifyLock`), so this is
   **differentially testable**.
2. **On-chain secret extraction** — detect the spend of the HTLC UTXO and parse
   the preimage from the claim scriptSig. This is **net-new**: today the secret is
   learned cooperatively over the P2P protocol
   ([SwapDaemon.cpp:764](../../src/SwapDaemon/SwapDaemon.cpp)), a liveness/trust
   assumption. On-chain extraction removes that assumption.

## Decision

**Revised 2026-06-16:** Adopt **Option C — full SPV replacement** for the production target. The BCH proving slice ships with SPV-only `SpvBchChainClient` (no RPC fallback). The full-node `BchChainClient` is retained as a standalone chain client (registered under `SwapPair::BCH`) for differential testing during the proving phase, but is not linked into the SPV path.

**Original decision (2026-06-04):** Adopt **Approach A — inject `ISpvClient` as an optional verification backend**:

- Add a new **UTXO-oriented `ISpvClient`** interface (generic SPV plumbing:
  header sync, tip, Merkle-verified inclusion, funding/spend lookup, raw-tx fetch).
  It carries **no chain-specific script semantics**.
- Implement `ElectrumSpvClient` (Electrum JSON-RPC over TCP) against it.
- Existing `IChainClient` implementations accept an **optional** `ISpvClient`.
  When present, the chain client routes `verifyLock` and on-chain secret
  extraction through SPV; when absent, it uses the existing full-node RPC path.
  `IChainClient` and the `SwapDaemon` driver loop are **unchanged**.
- **Protocol:** Electrum first. `ISpvClient` is shaped so a Neutrino/BIP-157-158
  backend (no per-query script disclosure) can implement the same interface later
  for privacy, without touching chain clients or the daemon.
- **Proving slice:** **BCH** — its existing `BchRpcClient` is retained as a
  **differential-test oracle** (run `verifyLock` through both paths against the
  same chain state; assert agreement).
- **State machine:** add an **additive** `ADAPTOR_WAITING_SPV_CONFIRMATIONS`
  sub-state used only on the SPV path; RPC-backed pairs keep their current
  `PRESIGS_READY → CTR_LOCKED` transition unchanged.

## Options Considered

### Option A: Injected verification backend (composition) — CHOSEN

`ISpvClient` injected into chain clients; SPV used for verification, RPC retained.

| Dimension | Assessment |
|-----------|------------|
| Complexity | Low–Med — one new interface + one impl; chain-client edits are localized |
| Cost | Low — `IChainClient` and daemon loop untouched |
| Scalability | High — every UTXO chain reuses one SPV backend; BTC/LTC/KMD/DCR drop in |
| Security | Best of the three — driver loop untouched; RPC retained as cross-check/oracle |
| Privacy | Electrum-equal now; clean seam to swap in Neutrino later |
| Team familiarity | High — mirrors the existing adapter/registry pattern |

**Pros:** smallest blast radius; preserves the differential oracle; honest
UTXO-only scope with a documented seam for a separate EVM light-client interface;
backward-compatible (optional injection).
**Cons:** `ISpvClient` is UTXO-shaped — EVM (Helios) needs a *different* interface
later (accepted; avoids a premature universal abstraction).

### Option B: Parallel registry sibling

Register SPV clients per pair alongside chain clients; the daemon driver chooses
RPC vs SPV per operation.

| Dimension | Assessment |
|-----------|------------|
| Complexity | Med — second registry concept; routing logic in the driver |
| Cost | Med — edits the security-critical `SwapDaemon` loop |
| Scalability | Med — verification split across two registry entries per chain |
| Security | Weaker than A — more surface in the state-machine driver |
| Privacy | Equal to A |
| Team familiarity | Med |

**Pros:** daemon-level switch between backends.
**Cons:** leaks SPV-awareness into `SwapDaemon.cpp`; verification logic for one
chain spans two registry entries.

### Option C: Full SPV replacement

`BchChainClient` runs SPV-only (Electrum broadcast included); no full node.

| Dimension | Assessment |
|-----------|------------|
| Complexity | Med–High — SPV must also build/broadcast/fee-estimate |
| Cost | High for a first slice |
| Scalability | High (end state) |
| Security | Weakest for the first slice — deletes the independent cross-check/oracle |
| Privacy | Equal to A |
| Team familiarity | Low |

**Pros:** realizes "no full node" immediately; smallest long-term footprint.
**Cons:** removes the RPC oracle BCH was chosen for; highest first-slice risk.

## Trade-off Analysis

Decision rule applied (in priority order): **security → privacy → speed.**

- **Security.** A, B, C share identical `ElectrumSpvClient` correctness, so the
  differentiator is *coupling to the swap state-machine driver* and *retention of
  an independent verification path*. A leaves the driver and `IChainClient`
  untouched and keeps the full-node RPC path as defense-in-depth cross-check. B
  edits the driver's routing (more surface for a state-machine bug). C deletes the
  independent path entirely. **A > B > C.** This tier alone is decisive.
- **Privacy (tiebreak).** Identical at runtime (all three use Electrum, same
  scripthash exposure). A's clean `ISpvClient` seam best preserves the *path to*
  Neutrino-grade privacy without touching chain clients or the daemon.
- **Speed (tiebreak).** Identical runtime; A is also the least code to land.

The same security-first rule resolves the operation-scope sub-decision:
**on-chain secret extraction is in scope**, because it removes the cooperative-
secret liveness/trust assumption — a real security improvement over the status quo.

## Consequences

**Easier:**
- Adding UTXO chains (BTC, LTC, KMD, DCR) — they reuse `ElectrumSpvClient`; only
  per-chain address/script params and an Electrum server list differ.
- Operating an LP/`SwapDaemon` without hosting a full node per UTXO chain.
- Future privacy upgrade — a `NeutrinoSpvClient` implements `ISpvClient` with no
  consumer changes.
- Trustless secret recovery — Bob no longer depends on Alice's cooperation.

**Harder / new burden:**
- SPV brings an eclipse-attack surface absent from a trusted local full node.
  Mitigation (multi-server header cross-reference, confirmation depth) is
  mandatory, not optional, and adds code and config.
- Header-chain state must be persisted and reorg-handled.

**To revisit later:**
- A separate light-client interface for EVM (Helios sync-committees) — **not**
  `ISpvClient`; this ADR deliberately does not generalize across UTXO and EVM.
- Neutrino/BIP-157-158 backend for privacy.
- libp2p-based Electrum/peer discovery (servers are config-supplied for now).
- Retiring the BCH full node from LP docs once SPV is proven (kept now as oracle).

**Pre-existing item flagged (not introduced here):** `BchChainClient::lock` passes
`params.adaptorPoint` (T = t·G) as the HTLC `hashLockSha256Hex`
([BchChainClient.cpp:15](../../src/SwapDaemon/BitcoinCash/BchChainClient.cpp)),
while `claim` reveals `params.adaptorSecret` (t). A SHA-256 HTLC requires the
committed hashlock to equal `SHA256(revealed_preimage)`. SPV extraction reuses
whatever value `lock()` commits (so it stays internally consistent), but this
lock/claim hashlock semantic must be confirmed during implementation — it governs
the extraction equality check.

## Expansion Targets (post-slice)

Recorded targets, grouped by **light-client family** — only the first group reuses
`ElectrumSpvClient` directly. The others are deliberately *not* `ISpvClient` and
need separate work; lumping them together would misjudge the effort.

| Target | Family | Reuses | Notes |
|--------|--------|--------|-------|
| **BTC** | UTXO / Electrum SPV | `ElectrumSpvClient` directly | Native ElectrumX ecosystem; P2WSH HTLC; ~90% of BCH script code reusable (BIP-143 sighash, no `SIGHASH_FORKID`) |
| **LTC** | UTXO / Electrum SPV | `ElectrumSpvClient` directly | Near-identical to BTC; ElectrumX servers exist |
| **KMD** | UTXO / Electrum SPV | `ElectrumSpvClient` directly | Same as BCH/BTC tx model, different address prefixes (0x3C P2PKH, 0x55 P2SH, WIF 0xBC) |
| **BNB** | EVM (account-based) | `EthChainClient` pattern | **Not `ISpvClient`.** Clone ARB/BASE pattern, chainId 56. Largest NFT marketplace volume in Asia, 1M+ daily active users. |
| **DCR** | UTXO (Bitcoin-derived) | partial | Scripting supports `OP_SHA256`/`CLTV` (DCRDEX relies on it), but Decred's light-client infra is dcrwallet SPV, **not** ElectrumX — needs an Electrum-compatible server or a Decred-specific SPV backend. Evaluate before committing. |
| **POLYGON** | EVM (account-based) | `EthChainClient` pattern | **Not `ISpvClient`.** Near-term: same RPC path as ARB/BASE (chainId 137). Trustless path = future EVM light client (Helios sync-committee), a separate interface. |
| **TON** | TVM (smart contract) | none — new protocol | **Not `ISpvClient`.** TVM smart contracts handle HTLC. Ed25519 signing, async message passing, account-based. Needs `TonChainClient` with Toncenter RPC + Tact HTLC contract. |
| **Zano** | CryptoNote | XMR adaptor pattern | CryptoNote derivative like Fuego. Native HTLC outputs with auto-refund. Monero-derived RPC API (simplewallet). Reuse XMR adaptor signature patterns. |
| **Sia** | Storage (UTXO) | none — storage layer | Decentralized storage for DIGM audio. renterd S3-compatible API. UTXO model with file contracts. Not a swap chain — storage backend. |

Sequencing implication: **BTC → LTC** are the cheap wins off this slice
(direct `ElectrumSpvClient` reuse). **KMD** is the second SPV chain,
shipping with identical tx model (P2PKH/P2SH, BIP143) and different address
prefixes. **BNB** and **POLYGON** ride the existing EVM RPC path (clone ARB/BASE pattern). **DCR** needs an infra spike first. **TON**
is priority work (new protocol chain, TVM + Ed25519). **Zano** reuses XMR patterns.
**Sia** is the storage layer for DIGM content.

## Action Items

1. [x] Define `ISpvClient` interface + result structs (UTXO-shaped, no script semantics).
2. [x] Implement `ElectrumSpvClient`: Electrum JSON-RPC, header-chain sync + PoW link,
       Merkle-proof verification, multi-server tip cross-reference.
3. [x] Add optional `ISpvClient` injection to `BchChainClient`; route `verifyLock`
       + secret extraction through SPV with RPC fallback.
4. [x] Add additive `ADAPTOR_WAITING_SPV_CONFIRMATIONS` state + transitions +
       (de)serialization; SPV-only path.
5. [x] Config (`fuego_swapd.json`): `bch_spv_enabled`, `bch_electrum_servers[]`,
       `bch_spv_min_confs`; registration wiring in `SwapDaemon`.
6. [x] Tests: known-answer Merkle/header/preimage unit tests; differential
       `verifyLock` (SPV vs RPC) on recorded fixtures; opt-in live-testnet integration.
7. [x] Record final ratification (flip Status → Accepted) after the spec is approved.
8. [x] KMD SPV: SpvKmdChainClient + SwapPair::KMD_SPV + config + tests (4/4 passing).

## Related
- Baseline: [plans/spv_architecture.md](../../plans/spv_architecture.md)
- Extends: [chain-interface refactor](../superpowers/plans/2026-05-25-swapxfg-chain-interface-refactor.md)
- Roadmap: [swap_expansion_guide.md](../../plans/swap_expansion_guide.md)
- Interacts with: [atomic-swap security fixes](../superpowers/plans/2026-05-31-atomic-swap-security-fixes.md) (reserve-proof gap applies across chains)
- Full slice design: in-flight brainstorming spec → `docs/superpowers/specs/2026-06-04-spv-bch-slice-design.md` (pending)
