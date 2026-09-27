# Swap platform executable development guide

Reconstructed 2026-09-27 after the earlier temporary guide disappeared. This
is the execution ledger for the canonical `xfgo` swap daemon and dashboard;
wallet metadata and order-book relay capacity are not evidence of an executable
swap. No historical swaps were reported by the operator, but new-swap safety
checks and recovery paths remain separate by design.

## Non-negotiable boundaries

- `fuegod` owns the XFG-side escrow and order-book relay. `xfg-swapd` owns
  counterparty-chain lock, verification, claim, and refund. They have separate
  databases. Do not mark a pair active because the Flutter wallet displays it.
- Admission for a **new** offer, initiation, acceptance, or funding needs a
  fresh chain-client readiness check. Once a swap exists, claim/refund and
  record inspection must remain available even if admission is closed.
- EVM native amounts use canonical decimal uint256 atomic strings end to end.
  The 18.446744073709551615-unit `uint64` ceiling is not an EVM limit.
  Non-EVM adapters that use `uint64` reject overflow before creating a record.
  This does **not** add ERC-20 token swaps; those require a separately audited
  token HTLC and allowance/transfer model.
- Version-2 KEY_EXCHANGE signs pair and exact amounts. A peer-supplied public
  key is an expected identity, not proof that the exchange already completed.
- Keep the old SwapXFG TUI until the live dashboard has feature and recovery
  parity. Keep the fake-data Hearth design sandbox isolated from the operator
  dashboard; never let design fixtures impersonate live market state.

## Packet 0 — integrate the reviewed core into `xfgo`

1. Start from the current `xfgo` HEAD; preserve unrelated dirty HEAT, wallet,
   and Starkproof work. Import the recovered swap branch and the reviewed
   uint256/fee-preflight delta by scoped commit, not by copying a temp tree.
2. Reconcile the current main crypto semantics: `secp_adaptor_extract` returns
   CryptoNote little-endian `t`, and consumed `Musig2SecNonce` has no
   `signed_flag`. Keep the main-side regression expectations.
3. Build `SwapDaemon`/`Daemon` and focused swap tests. Run ETH protocol,
   state-machine/SPV, presignature, adaptor/PTLC, catalog, config, dashboard,
   and `git diff --check` checks. Record exact pass/fail counts in the changelog.
4. Confirm `src/SwapDaemon/SwapTypes.h` in the canonical checkout has
   `AtomicAmount ctrAmount`; a passing temp build is not delivery.

Acceptance: one scoped, reproducible main-checkout commit with no unrelated
files staged and no review/test failures. No funded mainnet assertion follows
from offline tests.

## Packet 1 — full-width amount sign-off

Implementation checklist:

- Parse only canonical unsigned decimal atomics; reject uint256 overflow.
- Persist v2 amounts as strings; reject old or mismatched key exchange before
  any state advance or funding. Exercise restart/replay/downgrade tests.
- Encode the full 32-byte value in EVM ABI, contract ID, RLP legacy/type-2
  transactions, and RPC balance handling. Test `2^64-1`, `2^64`, 100 ETH,
  `2^256-1`, and overflow.
- Estimate gas with the configured signer as `from`, check the buffered gas
  estimate against the selected limit, and require balance >= value + maximum
  fee without arithmetic wrap. Reject EIP-1559 quotes above the 500 gwei
  safety ceiling used by legacy transactions. Validate against a real RPC on
  a funded testnet before claiming production readiness.

Status: offline implementation and focused tests complete in the integration
work; funded testnet execution remains open.

## Packet 2 — generic EVM batch chains

The catalog has IDs 29–45 as **ADAPTER** candidates (Linea, ZKsync Era,
HyperEVM, Ink, Rootstock, Gnosis, Flare, Kaia, Scroll, Abstract, Plume,
Soneium, Doma, Beam, Moonriver, peaq, Sei). `ACTIVE` is not granted by catalog
entry or RPC reachability alone. Tempo, DOT, Sia, TON, and Zano require their
own reviews and are not generic EVM drop-ins. "Bloom" remains unresolved
until the intended network and chain ID are confirmed.

For each candidate, in small batches:

1. Verify chain ID, native currency decimals, transaction envelope, RPC
   behavior, finality/reorg policy, and block-time bounds against that chain.
2. Configure an operator-owned signer and a chain-specific deployed HTLC
   registry. Check runtime bytecode and chain ID, not just address syntax.
3. Execute funded testnet lock, independent verification, claim, refund,
   restart/recovery, insufficient-funds, fee-ceiling, wrong-chain, and reorg
   scenarios. Keep new admission closed if any required path is missing.
4. Publish per-chain evidence and only then promote support/readiness. The
   dashboard consumes daemon catalog/readiness, not a duplicate allowlist.

## Packet 3 — CTR oracle policy

For each counterparty asset, document the canonical symbol, atomic precision,
USD price source(s), freshness window, deviation bound, and failure behavior.
Use an exact atomic amount for funding and a clearly bounded conversion only
for price checks. Do not let a seed price or stale quote silently authorize a
money-moving trade; mark bootstrap/no-data behavior visibly and require
operator policy. Record which new EVM assets have no trusted oracle feed.

## Packet 4 — live dashboard parity

- SwapXFG: daemon-backed catalog/status, balances, quote, signed offer
  placement/cancel, own orders, taker fill, swap timeline, claim/refund,
  recovery and error states. Every write uses the allowlisted local
  `xfg-swapd` RPC bridge; tokens stay server-side.
- Hearth Floor: live pool/order-book/TWAP and transaction state, quote-first
  execution with slippage/min-output, expiry and order management. Remove
  random or mock production fallbacks. Retain a separate fake-data design copy
  for visual iteration.
- Show `unavailable`, `stale`, and `not ready for new swaps` separately from
  recoverable existing swaps. Test desktop/mobile layouts and failure modes.

## Packet 5 — TUI retirement and release gate

Delete the SwapXFG TUI only after a feature-by-feature dashboard parity matrix
and recovery drill pass, funded testnet swap paths pass for each promoted pair,
and the operator explicitly accepts the replacement. Preserve CLI access via
the dashboard's documented daemon controls rather than silently dropping
operations. Mainnet validation remains read-only until the funded testnet and
security gates are signed off.
