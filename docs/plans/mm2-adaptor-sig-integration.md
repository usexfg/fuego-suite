# Adaptor Signature Integration into MarketMaker2 (mm2)

**Status:** Draft Proposal  
**Date:** 2026-06-17  
**Author:** XFG Dev  
**License:** MIT / Komodo Platform Open Source  

---

## 1. Motivation

The Komodo Wallet Desktop (`swapxfgui`) — forked from AtomicDEX Desktop — expects a MarketMaker2 (mm2) daemon with:
- BIP39 HD wallet management (balances, tx history, withdraw)
- P2P orderbook gossip shared across the AtomicDEX network
- Unified coin activation via `enable`/`electrum` RPC
- Swap execution (currently HTLC-based)

`fuego_swapd` implements adaptor signatures — a superior swap protocol:
- No hash preimage on-chain (Hals of privacy)
- Adaptor secret extracted from on-chain signature (Hals atomicity)
- Cooperative refund (Hals UX)
- Musig2 key aggregation (single joint address per swap)

Rather than reimplementing mm2's wallet + orderbook in `fuego_swapd`, we add adaptor signatures as a **new swap method** in mm2 itself. This makes adaptor sigs a first-class option in the AtomicDEX ecosystem, shareable across all Komodo/Firo wallet users.

---

## 2. Design Goals

1. **Backward compatible** — existing HTLC swaps continue working unmodified
2. **Single P2P orderbook** — adaptor sig orders and HTLC orders coexist, users choose
3. **Per-order method flag** — `swap_method: "htlc" | "adaptor"` in order and swap
4. **Chain adapter reuse** — each coin's existing Electrum/UTXO logic works for both methods
5. **Gradual adoption** — coins opt into adaptor support; fall back to HTLC if unsupported
6. **Code clarity** — separate module for adaptor protocol, minimal changes to existing HTLC path

---

## 3. High-Level Architecture

```
mm2 Daemon
├── Existing HTLC Swap Engine
│   ├── P2P orderbook (unchanged)
│   ├── HTLC state machine (unchanged)
│   └── Per-coin HTLC adapters (unchanged)
│
├── New: Adaptor Swap Engine
│   ├── Adaptor state machine → 7 states
│   ├── Crypto module (Musig2, DLEQ, adaptor sigs)
│   ├── P2P protocol messages (key exchange, nonces, presigs)
│   └── Per-coin adapter hooks
│
├── Existing Wallet Layer (unchanged)
│   ├── BIP39 seed → BIP44 per-coin keys
│   ├── Electrum SPV (balances, tx history, UTXOs)
│   └── Coin configs (ticker → Electrum servers, address prefixes)
│
└── RPC Layer (extended)
    ├── Existing endpoints (unchanged: enable, my_balance, buy, sell, etc.)
    └── New: swap_method field on orderbook/order RPCs
```

---

## 4. Adaptor Signature Protocol (7-State Machine)

### 4.1 States

| State | Code | Description |
|---|---|---|
| `ADAPTOR_IDLE` | 0 | Initial, no swap active |
| `ADAPTOR_KEYS_EXCHANGED` | 1 | Both peers have exchanged swap keys; Musig2 aggregate key computed |
| `ADAPTOR_ESCROW_FUNDED` | 2 | XFG (or counterparty's native coin) sent to Musig2 joint address |
| `ADAPTOR_PRESIGS_READY` | 3 | Nonces exchanged, both sides hold partial signatures |
| `ADAPTOR_CTR_LOCKED` | 4 | Counterparty lock tx broadcast and confirmed |
| `ADAPTOR_SECRET_REVEALED` | 5 | Adaptor secret extracted from on-chain claim tx |
| `ADAPTOR_XFG_SPENT` | 6 | Adapted signature broadcast, escrow claimed |
| `ADAPTOR_REFUNDED` | 7 | Cooperative refund after timeout (both sign to return funds) |

### 4.2 Flow (Bob has XFG, Alice has CTR)

```
Bob                              Alice
 │                                 │
 │─── ORDER (swap_method=adaptor)──│  (via P2P orderbook gossip)
 │                                 │
 │←──── MATCH + KEY_EXCH ──────────│  (direct P2P: swap pubkeys)
 │                                 │
 │─── KEY_EXCH_ACK ───────────────→│  (Musig2 key agg computed both sides)
 │                                 │
 │─── ADAPTOR_EXCH ───────────────→│  (Bob: T = t*G, DLEQ proof T=Q/escrowPub)
 │                                 │
 │←─── ADAPTOR_EXCH_ACK ──────────│  (Alice verifies DLEQ proof)
 │                                 │
 │←─── NONCE_EXCH ────────────────│  (Alice sends pub nonce R_A)
 │                                 │
 │─── NONCE_EXCH_ACK ─────────────→│  (Bob sends pub nonce R_B)
 │                                 │
 │─── PRESIG (Bob) ───────────────→│  (Bob signs escrow-spend tx, adapted with T)
 │                                 │
 │←─── PRESIG (Alice) ────────────│  (Alice signs ctr-lock tx, adapted with T)
 │                                 │
 │─── LOCK_CTR (on-chain) ────────│  (Alice broadcasts lock tx on CTR chain)
 │                                 │
 │─── CLAIM_CTR (on-chain) ───────│  (Bob claims CTR funds → reveals t on-chain)
 │                                 │
 │─── ADAPT_PRESIG ───────────────│  (Bob: sig' = presig + t → valid XFG sig)
 │─── CLAIM_XFG (on-chain) ───────│  (Bob broadcasts adapted sig, claims escrow)
```

### 4.3 State Machine Transitions

```
ADAPTOR_IDLE ────[match + key_exchange]────→ ADAPTOR_KEYS_EXCHANGED
ADAPTOR_KEYS_EXCHANGED ──[adaptor_exchange]──→ ADAPTOR_KEYS_EXCHANGED
ADAPTOR_KEYS_EXCHANGED ──[nonce_exchange]────→ ADAPTOR_PRESIGS_READY
ADAPTOR_PRESIGS_READY ──[presigs complete]───→ ADAPTOR_PRESIGS_READY
ADAPTOR_PRESIGS_READY ──[ctr_lock_tx]────────→ ADAPTOR_CTR_LOCKED
ADAPTOR_CTR_LOCKED ──[secret_revealed]───────→ ADAPTOR_SECRET_REVEALED
ADAPTOR_SECRET_REVEALED ──[xfg_spent]────────→ ADAPTOR_XFG_SPENT
                        ──[timeout]──────────→ ADAPTOR_REFUNDED
```

---

## 5. Crypto Module (`mm2/src/adaptor/`)

### 5.1 Components

| Component | Function | Source |
|---|---|---|
| `Musig2KeyAgg` | Compute aggregate pubkey Q from Q_A, Q_B | Komodo `mm2` already has MuSig for KMD consensus; add BIP327-compatible |
| `AdaptorPoint` | Compute T = t * G, Q' = t * escrowPubKey | New — port from `fuego_swapd` |
| `DLEQProof` | Prove log_G(T) == log_escrowPub(Q') | New — port from `fuego_swapd` |
| `AdaptorSign` | Sign message with adaptor: sig' = s + t | New — simple EC addition |
| `AdaptorExtract` | Given sig' and valid sig, extract t = sig' - s | New |
| `AdaptorVerify` | Verify adapted sig without knowing t | New |

### 5.2 Key Derivation (BIP32 + per-swap)

Each swap generates ephemeral keys:
```
swap_seed = H(swap_id || master_seed)
swap_sk   = H(swap_seed || "adaptor-swap-key")  mod n
swap_pk   = swap_sk * G
```

This ensures swap keys are unlinkable across swaps.

---

## 6. P2P Protocol Extensions

### 6.1 New P2P Messages (over existing mm2 P2P or direct TCP)

| Message | Direction | Payload |
|---|---|---|
| `ADAPTOR_KEY_EXCHANGE` | bidirectional | `swap_method`, `swap_id`, `swap_pubkey` |
| `ADAPTOR_KEY_EXCHANGE_ACK` | bidirectional | `swap_method`, `swap_id`, `swap_pubkey`, `aggregate_pubkey` |
| `ADAPTOR_POINT_EXCHANGE` | Bob→Alice | `swap_id`, `adaptor_point`, `dleq_proof` |
| `ADAPTOR_POINT_VERIFIED` | Alice→Bob | `swap_id`, `result` (ok/fail) |
| `ADAPTOR_NONCE_EXCHANGE` | bidirectional | `swap_id`, `pub_nonce` |
| `ADAPTOR_PARTIAL_SIG` | bidirectional | `swap_id`, `partial_sig` |
| `ADAPTOR_SECRET_REVEAL` | Bob→Alice | `swap_id`, `t` (adaptor secret, after on-chain claim) |
| `ADAPTOR_ABORT` | bidirectional | `swap_id`, `reason` |

### 6.2 Transport Options

1. **Direct TCP** (preferred) — mm2 already opens random ports for swap coordination. Add adaptor protocol on the same channel, distinguished by `swap_method` field.
2. **Tor onion** (future) — for IP privacy, route adaptor protocol through Tor hidden services.

---

## 7. Chain Adapter Interface

### 7.1 Existing HTLC Interface (unchanged)

Each coin provides:
```rust
fn htlc_lock(params: LockParams) -> Result<LockTx>;
fn htlc_claim(params: ClaimParams) -> Result<ClaimTx>;
fn htlc_refund(params: RefundParams) -> Result<RefundTx>;
fn htlc_verify_lock(params: VerifyLockParams) -> Result<LockStatus>;
```

### 7.2 New Adaptor Interface

For coins that support adaptor sigs, add:
```rust
fn adaptor_lock(params: AdaptorLockParams) -> Result<LockTx>;
fn adaptor_claim(params: AdaptorClaimParams) -> Result<ClaimTx>;
fn adaptor_refund(params: AdaptorRefundParams) -> Result<RefundTx>;
fn adaptor_verify_lock(params: VerifyLockParams) -> Result<LockStatus>;
fn supports_adaptor() -> bool;
```

For most UTXO coins (BTC, BCH, KMD, LTC, DOGE), the difference is:
- **HTLC**: creates P2SH with OP_IF/HASH160/OP_ELSE/OP_CLTV/OP_ENDIF/OP_CHECKSIG
- **Adaptor**: creates P2PKH (or P2WPKH) to Musig2 aggregate key with nLockTime for timeout

Simpler script, lower fees, better privacy. EVM chains use a contract call either way.

### 7.3 Fallback Logic

When a maker posts an adaptor-sig order, the taker can still fill it only if both parties' coins support adaptor. Otherwise, the order shows as incompatible:

```rust
fn can_do_adaptor(coin_a: &CoinConfig, coin_b: &CoinConfig) -> bool {
    coin_a.adaptor_adapter.is_some() && coin_b.adaptor_adapter.is_some()
}
```

On the orderbook, orders with mismatched methods are hidden from the counterparty (or shown greyed out).

---

## 8. RPC Extensions

### 8.1 `orderbook` V2 — new field `swap_method`

```json
// Request (existing, extended)
{ "base": "XFG", "rel": "KMD", "swap_method": "adaptor" }

// Response (existing, extended)
{
  "asks": [...],
  "bids": [...],
  "base": "XFG",
  "rel": "KMD",
  "swap_method": "adaptor"
}
```

If `swap_method` is omitted, returns both types (all orders).
If specified, returns only matching method.

### 8.2 `buy` / `sell` — new field `swap_method`

```json
{
  "base": "XFG",
  "rel": "KMD",
  "price": "0.001",
  "volume": "100",
  "swap_method": "adaptor"
}
```

mm2 selects the protocol based on the field and verifies both coins support it.

### 8.3 `my_orders` — each order shows `swap_method`

Orders include `swap_method` so the GUI can display which protocol is used.

### 8.4 `my_recent_swaps` — each swap shows `swap_method`

Swaps include the method for display and debugging.

---

## 9. Adding XFG to mm2 (New Coin Config)

XFG would be added like any other UTXO coin in mm2:

```json
{
  "coin": "XFG",
  "name": "fuego",
  "fname": "Fuego",
  "rpcport": 28280,
  "pubtype": 183,
  "p2shtype": 5,
  "wiftype": 248,
  "txversion": 4,
  "overwintered": 1,
  "mm2": 1,
  "required_confirmations": 10,
  "protocol": {
    "type": "UTXO"
  }
}
```

The coin config includes:
- Address prefixes (pubtype = 0xB7 for XFG, p2shtype, wiftype)
- ElectrumX servers (for SPV wallet mode)
- Block explorer URL
- Confirmation requirements

---

## 10. Migration Path

### Phase 0 — Prerequisites (2-3 weeks)
- Fork mm2 source from `KomodoPlatform/mm2` to `xfg-community/mm2`
- Add Musig2 + adaptor crypto primitives as a standalone Rust/C module
- Write unit tests for adaptor sign/extract/verify

### Phase 1 — Core Protocol (3-4 weeks)
- Implement 7-state adaptor swap state machine
- Add P2P messages for adaptor exchange rounds
- Wire adaptor state machine into mm2 swap lifecycle
- Add `swap_method` field to orderbook/order/settle RPCs

### Phase 2 — Chain Adapters (2-3 weeks)
- Implement `adaptor_lock`/`claim`/`refund` for P2PKH UTXO coins (BCH, KMD first)
- Implement `adaptor_lock`/`claim`/`refund` for EVM contract (ETH, ARB, BASE)
- Fallback to HTLC for coins without adaptor adapter

### Phase 3 — XFG Coin Support (1-2 weeks)
- Add XFG coin config to mm2
- Add XFG Electrum server endpoints
- Test XFG ↔ KMD adaptor swap end-to-end

### Phase 4 — GUI Integration (1-2 weeks)
- Update `swapxfgui` to spawn custom mm2 binary with XFG support
- Add `swap_method` display in orderbook widget
- Add `swap_method` selector in trade form (optional, default adaptor)
- Test full workflow: order → match → adaptor swap → settle

### Phase 5 — Upstream Contribution (ongoing)
- Propose adaptor sigs as an optional swap method upstream
- Submit draft as RFC to Komodo Platform
- Gradually onboard more coins (LTC, DOGE, DASH)

---

## 11. Security Considerations

| Risk | Mitigation |
|---|---|
| **Bad DLEQ proof** (Bob lies about adaptor point) | Alice verifies DLEQ proof before proceeding. If invalid, abort. |
| **Peer goes offline mid-swap** | Timeout-based cooperative refund. Both sign refund tx at `presigs_ready` state. |
| **Replay attack** (adaptor sig reused) | Each swap has unique `swap_id` → unique nonces → sigs non-reusable. |
| **Musig2 rogue key attack** | BIP327 key aggregation with proof of possession. |
| **Private key extraction from DLEQ** | Fiat-Shamir transform (non-interactive), validated before use. |
| **Chain reorganization** | `required_confirmations` applies to lock tx, same as HTLC. |
| **No preimage on-chain** | Adaptor secret is encoded in the ECDSA signature `s` value. Observer sees a normal P2PKH spend. |

---

## 12. Open Questions

1. **Direct TCP vs P2P gossip for adaptor exchange?** — Direct TCP is lower latency but requires port opening. P2P gossip is NAT-friendly but adds latency. Recommend direct TCP with UPnP/port mapping fallback.

2. **Should we support both methods in a single swap** (e.g., Alice HTLC → Bob adaptor)? — No. Both sides must agree on the protocol. Mixed swaps add complexity without benefit.

3. **What about chain-requiring privacy** (Monero, Zcash)? — Adaptor sigs work with any ECDSA/Schnorr chain. For RingCT chains, we'd need a RingCT-specific adaptor variant (portable from `fuego_swapd`'s XMR support).

4. **Tor integration for exchange messages?** — Adds latency but hides IPs. Recommend v1 with optional Tor, required v2.

---

## 13. Appendices

### A. Comparison: HTLC vs Adaptor Signature

| Property | HTLC | Adaptor Signature |
|---|---|---|
| On-chain footprint | P2SH with ~25-byte script + hash preimage | P2PKH (or P2WPKH) — standard spend |
| Privacy | Hash preimage visible on-chain. Observer sees "atomic swap" pattern. | Standard P2PKH spend — indistinguishable from normal payment. |
| Atomicity | Both claim or both refund (hashlock + timelock) | Both claim or cooperative refund (adaptor secret embedded in sig) |
| Refund | Timeout-based only. Must wait for timelock. | Cooperative: both sign refund at setup. Immediate if both agree. |
| Fee efficiency | Higher (P2SH scriptsig costs extra bytes) | Same as normal transaction |
| Implementation complexity | Simple (OP_IF/OP_HASH160/OP_CLTV) | Complex (Musig2, DLEQ proofs, adaptor point math) |
| Maturity | 10+ years, billions in volume | Newer protocol, ~3 years production |

### B. Referenced Code

| Source | Component |
|---|---|
| `fuego_swapd` `SwapStateMachine.cpp` | Adaptor state machine reference |
| `fuego_swapd` `SwapPeerProtocol.h` | Adaptor exchange message formats |
| `fuego_swapd` `Musig2Types.h` | Musig2 key aggregation + signing |
| `fuego_swapd` `CryptoNote/crypto/crypto-ops.h` | DLEQ proof generation/verification |
| `KomodoPlatform/mm2` | Base mm2 daemon codebase |
| BIP-327 | MuSig2 key aggregation standard |
| BIP-340-342 | Schnorr signatures (for future Taproot adaptor support) |
| Adaptor Signatures (Poelstra 2017) | Original academic paper |

---

## 14. Changelog

| Date | Change |
|---|---|
| 2026-06-17 | Initial draft after comparing `fuego_swapd` adaptor sigs with mm2 HTLC |
