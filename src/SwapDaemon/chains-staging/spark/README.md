# Spark (spark.money) Integration Plan

## Overview

Spark is Lightspark's Bitcoin L2 (statechains + FROST 2-of-3+ threshold co-signing
by Spark Operators: Lightspark, Flashnet, Breez). Bitcoin on Spark is not a
wrapped asset: users can unilaterally exit to Bitcoin L1 via pre-signed exit
trees. Mainnet is live but self-described as beta with possible limits and
breaking changes.

**Why a plan instead of a routine pair add:** Spark has no smart contracts and no
scriptable hashlock/timelock on Spark leaves. Transfers are operator-countersigned
state transitions. Every atomic-swap security invariant in `IChainClient` terms
(verify-before-fund, observable on-chain secret reveal, script-enforced refund)
has a weaker analogue on Spark, and the trust model is 1-of-n operator honesty
rather than cryptographic-only. This plan states what is possible, what is not,
and which route preserves Fuego's trustless swap guarantees.

## Technical Fit

| Parameter | Spark |
|-----------|-------|
| Model | Statechain leaves; FROST threshold co-signing by Spark Operators (SE) |
| Smart Contracts | **None** (`docs.spark.money/learn/faq`) |
| HTLC on leaves | **None** — no scriptable hashlock/timelock path |
| Timelock primitive | Conditional transfer with a time lock, held/unlocked by the SE (used for Lightning interop and offline receive) |
| Claim primitive | Lightning settlement against `H(t)` — the SE pays a Lightning HTLC and recreates the preimage; requires operator cooperation (1-of-n) |
| Secret reveal | Lightning preimage (not directly on a Spark leaf — no on-chain observable reveal on Spark itself) |
| Native asset | BTC (8 decimals), also issuer tokens (BTKN, USDB/USDC) |
| Trust model | 1-of-n operator honesty + unilateral exit to L1 |
| Interop | Native Lightning (no channels), Garden is the canonical multi-chain bridge (HTLC intent + solver, EVM/Solana/Starknet/BNB/BTC L1, bidirectional) |
| SDKs | Wallet SDK (TS/React Native), Issuer SDK (TS), Breez SDK (Rust + Swift/Kotlin/Python/Flutter/Go/C#/WASM bindings) |

## Two Routes

### Route A — Spark as a managed rail on the existing BTC pair (recommended)

No new swap pair, no escrow change, no trust regression. The atomic leg stays
the existing `SwapPair::BTC` P2WSH HTLC/adaptor escrow on Bitcoin L1
(fully trustless, `supportsPtlc()` available). Spark enters as the funding and
disbursement rail: swapd operates a Spark wallet so users can fund the BTC HTLC
from a Spark balance (Spark → L1 deposit) or receive settled BTC into Spark
(L1 → Spark deposit) after the swap completes.

- Effort: swapd gains a Spark/rail integration (Breez SDK Rust bindings behind
  FFI or a small sideload helper process; swapd is C++, there is no C++ SDK).
- Fee accounting: unchanged — the 2% BTC-pair swap fee and its 69/11/20 split
  still apply; Spark deposit/withdrawal fees (L1 mining fee, exit fee
  `250×sat/vB + 750`, Sanctuary: 0.25% to Lightning) are user-side network
  costs, not protocol earnings. **No new protocol earnings stream.**
- Limitation: not a one-click XFG↔Spark trade surface; Spark leg is manual/UX-driven.

### Route B — `SwapPair::SPARK` direct pair (only with operator trust)

Counterparty leg = Spark conditional transfer instead of a script:

- Lock: swapd (or the user's Spark wallet) makes an open transfer with an SE
  time lock. Refund path = SE unlocks the leaves at timeout — enforced by
  operators, not script.
- Claim: knowledge of `t` is lodged with the SE (preimage-conditioned release).
  Spark's Lightning interop proves the SE can settle against a hash preimage,
  but the FAQ documents no smart contracts and Spark's own cross-chain
  comparison lists operator-set trust, so this release primitive must be
  confirmed as exposed to third-party integrators before any design commit.
- Observable reveal: preimage surfaces via Lightning/SE settlement, not a Spark
  leaf script. `tryExtractClaimedSecret` can consume it, but verification is of
  an operator attestation, not chain data.
- Trust regression is explicit and must be surfaced to users in the wallet
  (danger/attestation badge), unlike every active pair.

Route B is a product decision for the maintainers, not an engineering one.

### Route C — Garden solver partnership (bridge, no in-repo rate)

Swap XFG via Garden's existing HTLC intent + solver flow as an off-repo
integration. Gains 10+ chains with the same work, but requires their team
running XFG funding/claim legs; no `SwapPair` id needed.

## Implementation Approach (Route B, if approved)

**Template**: Non-standard family, like `Solana/` — none of the EVM/UTXO
inheritance patterns apply. Requires a new `SwapChainFamily` member appended to
`SwapPairCatalog.h` (wire-serialized enum; ids never renumbered).

### Pair registration

```cpp
// SwapPairCatalog.h — APPEND ONLY, ids serialized in swap records
X(SPARK, 46, "spark", "BTC", "Spark (Bitcoin L2)", STATECHAIN, 0, 1000, 1000, 8,
  STAGED, NONE, "spark.png", "#f7931a")
```

- id 46 = next free sequential id after `SEI = 45`.
- Ships `STAGED`: id reserved, offers rejected until the release primitive is
  confirmed (same pattern SIA/ZANO already use).
- `supportsPtlc() = false`; `negotiateLockType` yields `HTLC` (or `BRIDGE` for
  XFG-side privacy via `T`/DLEQ + CTR hash).

### Files to Create

| File | Notes |
|------|-------|
| `Spark/SparkChainClient.h/.cpp` | `IChainClient` impl: conditional-transfer lock, SE-release claim, timed refund |
| `Spark/SparkRpcClient.h/.cpp` | Breez SDK (Rust) bridge — FFI or helper process; no C++ SDK exists |
| `Spark/SparkWithdrawal.h/.cpp` | Leaf supervision, unilateral-exit handling for held leaves |
| `src/SwapDaemon/Spark/tests/` | Mock-SE swap roundtrip lock→claim→refund |

### Integration Points

| File | Change |
|------|--------|
| `SwapPairCatalog.h` | New `SwapChainFamily` member (append-only), `SPARK = 46` |
| `SwapTimelock.cpp` | Wall-clock mapping (SE lock TTL; no blocks) — `refund_window_XFG ≥ refund_window_SPARK + margin` |
| `PriceOracle.cpp` | Rate seeded from BTC (Spark native asset is BTC) |
| `SwapDaemon.cpp` / `ChainRegistry` | Registration gated on the SE-release confirmation |
| `swapxfg.html` / `CHAIN_INFO` | Chain option + icon only when Route B is approved |

## Dependencies (open questions to resolve first)

1. Confirm with Spark whether conditional transfers can be released to a third
   party on knowledge of a preimage (not only time-out or Lightning payment
   proof), and under which nonces/limits — the entire Route B claim path hangs
   on this. Until confirmed: add `SPARK = 46` STAGED and stop.
2. Confirm the beta limits (swap-size caps, fee schedule table in FAQ) won't
   conflict with escrow sizes.
3. Choose the bridge: Breez SDK Rust bindings (most languages, includes
   Flutter) vs Wallet SDK (TS-only).

## Security Invariants applied to Route B

Holds differently than for active pairs:

- Verify before fund: also require the SE attestation timeout and leaf control
  confirmation, since no script exists to verify.
- Secret reveal observable: preimage observable only via SE/Lightning
  settlement — document this as attestation, not chain data.
- PTLC: Spark cannot commit to `T`; Route B is `HTLC`/`BRIDGE` (`H(t)` only,
  no `supportsPtlc()`).
- Hash family: BTC SHA-256 `H(t)` via the Lightning/SE path (matches the
  existing `SwapHashLock` SHA-256 branch, not Keccak/RIPEMD160).

## Estimated Effort

- Route A (managed rail): 5–8 days (Breez SDK wiring, funding/disbursement flows, tests)
- Route B (direct pair): 12–18 days **after** the release primitive is confirmed; potentially never if Spark declines the primitive
- Route C (Garden): design-oriented, external partnership; ~0 days in-repo

## Recommendation

Do Route A now (pure win, no new trust assumptions), reserve `SPARK = 46`
STAGED, keep Route C as a partnership track, and park Route B behind the open
primitive question #1.
