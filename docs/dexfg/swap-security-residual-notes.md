# SwapXFG security — residual INFO / LOW items (post 2026-08-09 fix pass)

Tracked after implementing audit fix order 1–7. Do **not** block production on these alone; schedule as hardening.

## Deferred (INFO / LOW)

| ID | Item | Notes |
|----|------|--------|
| L1 | ChaCha8 → ChaCha20-Poly1305 for secret encryption | MAC already present; cipher upgrade is hygiene |
| L2 | Full secret zeroization on process exit / SM dtor | Partial wipe exists after adapt |
| L3 | Timelock table coverage | BTC/LTC now explicit; keep enum complete when adding pairs |
| L4 | SOL program comment still says SHA-256 | Code uses keccak; docs only |
| I1 | `handleEscrowFunded` is log-only | Orchestration: auto-kick nonce exchange |
| I2 | Staging READMEs outdated | chains-staging vs live clients |
| I3 | Historical plan claimed RPC password gate | Fixed via `--rpc-token` / `X-Swap-Token` |

## Intentionally incomplete / removed

| Item | Status |
|------|--------|
| **Tron** | **Removed** from enum (slot 17 → SIA), client tree, UI, config |
| **CLV** | Header-only alias of EVM parachain; **not** in `SwapPair` / registration |
| **TON** | Cell/BOC + FunC `htlc.fc` + claim/refund sendBoc wired; needs deployed contract + toncenter; cell BOC is minimal (not full TL-B suite) |
| **Sia** | Client wired per staging plan (Blake2b-256, siad API); native unlock-condition HTLC still wallet/memo based until Sia HTLC grant lands |
| **EVM per-chain registries** | Gleec has `gleec_htlc_registry`; other L2s still default to `eth_htlc_registry` |

## XMR / Zano note (clarified)

“Only safe if other leg reveals `t`” means:

- Pure **Alice-locks HTLC** flow relies on `tryExtractClaimedSecret` from the CTR claim preimage.
- **XMR and Zano** use shared-address / adaptor keying, **not** HTLC preimage reveal, so that extract API returns empty.
- **XFG↔XMR and XFG↔Zano still work** if the daemon uses the **adaptor / SECRET_REVEAL / dual-adaptor** path for those pairs (Bob reveals `t` via P2P or XFG-side extract), not the “scan HTLC claim” path alone.
- Pairing XMR as the only secret-reveal leg against a chain that needs HTLC extract is the unsafe combination.

## Follow-ups for TON

1. FunC HTLC contract with `get_state` stack layout documented in `TonRpcClient::getHtlcState`
2. Cell builder for claim/refund external messages (or tonlib FFI)
3. Unit tests against toncenter testnet
