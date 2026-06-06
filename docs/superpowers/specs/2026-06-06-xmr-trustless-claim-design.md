# XMR Atomic-Swap Leg — Trustless Claim Wiring (Design Spec)

**Status:** Draft (pending user review)
**Date:** 2026-06-06
**Scope cut:** "wire it + unit-verify" / Model C (trustless *claim*; refund deferred)
**Audit that prompted this:** the XMR leg is a partial implementation — adaptor
crypto present, protocol wiring absent and internally inconsistent.

> ⚠️ **The `A+B+T` key model referenced in §1, §3, §4 below is SUPERSEDED by the
> researched model in the Addendum (§9). The correct shared spend key is `A+B`
> (two shares), and the adaptor secret IS one party's spend share — not a third
> term. Read §9 as authoritative.**

## 1. Problem

The XMR swap leg does not work and is not safe to enable:
- `MoneroRpcClient::createSharedAddress` is a pure stub (`return false`).
- `lockAdaptor` just `transferToShared(params.ctrAddress, …)` — XMR is sent to a
  plain address with **no cryptographic binding** to the swap.
- `sweepSharedAddress` calls `sweep_all` **without waiting for wallet sync** → no
  outputs visible → claim/refund sweep nothing.
- `verifyLock` accepts **locked** (unspendable) balance; wallet filename is
  hardcoded (concurrency collision); `restore_height: 0` (full rescan).
- The helpers are **inconsistent**: `claim` assumes shared key `A+B+T`
  (`combineSpendKeys = alice+bob+adaptor`) while `refund`/`computeSharedSpendPub`
  assume `A+B`. A lock can't target both.

The adaptor crypto itself is sound and **same-curve** (XFG and XMR are both
ed25519 → no cross-curve DLEQ). So the work is orchestration wiring + Monero-RPC
correctness, not novel cryptography.

## 2. Scope

**In (this cycle):** the trustless **claim** path and the reusable mechanics,
unit-verified, no live monerod.
- Implement `createSharedAddress` = encode(spend `A+B+T`, shared view), Monero
  address encoding.
- Wire `XmrChainClient` to use **per-swap key shares** (`params.ourSwapSecKey`,
  `peerSwapPubKey`, `adaptorPoint`), not static operator keys.
- `lock` → shared address; `verifyLock` → **unlocked** balance; `claim` →
  reconstruct `a+b+t`, generate_from_keys → **sync** → `sweep_all`.
- Fix sweep: sync-before-sweep, per-swap wallet filename, restore_height from lock.
- Unit tests for all of the above (math + RPC sequence).

**Out (deferred, documented):**
- **Refund/punish** for the `A+B+T` model — needs its own protocol design + live
  verification (the hard XMR refund asymmetry). `refund` returns a clear
  "not implemented for A+B+T" error rather than sweeping with a wrong key.
- Live regtest/stagenet end-to-end.

## 3. Architecture & components

Each unit has one responsibility, testable in isolation.

| Unit | Responsibility | Notes |
|------|----------------|-------|
| `MoneroAddress` (new, pure) | `(spendPub, viewPub, networkByte) → address` | Monero **block-based base58** (8-byte→11-char blocks) + keccak checksum. NOT standard base58. Network byte configurable (mainnet 0x12 / testnet 0x35 / stagenet 0x18). **Load-bearing correctness anchor.** Reuse an existing in-tree Monero base58 util if present; else implement. |
| `AdaptorSigScheme` | `combineSpendKeys` (priv `a+b+t`, exists); add point-add for pub `A+B+T` | Same `t` binds the XFG Musig2 adaptor — claiming XMR reveals `t`. |
| `MoneroRpcClient` | `createSharedAddress` (implement); `sweepSharedAddress` (sync-before-sweep, per-swap name, restore_height); `verifyLock` (unlocked) | RPC orchestration only. |
| `XmrChainClient` | `lock`/`verifyLock`/`claim` use shared address + per-swap shares; `refund` → deferred-guard | Adapter. |
| `TestMoneroWalletRpc` (new, test) | replays canned wallet-rpc responses; asserts call sequence | Named `Test…`, not "mock". |

## 4. Data flow (claim)

1. **Setup:** parties exchange swap pubkeys `A`,`B` + view-key shares; the
   XFG-side already carries `T = t·G` + DLEQ (`params.adaptorPoint`,
   `adaptorDleqProof`). Shared spend pub = `A + B + T`; shared view =
   `a_view + b_view`. Monero address = `MoneroAddress::encode(...)`.
2. **Lock** (XMR holder): `transferToShared(sharedAddr, ctrAmount)`.
3. **verifyLock:** `checkAddressBalance` → **unlocked** ≥ `ctrAmount`.
4. **Claim:** `combineSpendKeys(ourShare, peerShare, adaptorSecret) → a+b+t`;
   `generate_from_keys` (per-swap wallet, restore_height) → poll `get_height`
   until synced → `sweep_all` → dest. The on-chain spend reveals `t` to the
   counterparty (who completes the XFG side).

## 5. Error handling

- `createSharedAddress`: validate point/scalar decode; reject zero/identity result.
- `combineSpendKeys`: already rejects zero (would brick the sweep).
- `sweepSharedAddress`: poll sync with a bounded timeout; **never** `sweep_all`
  before synced; surface RPC errors verbatim.
- `verifyLock`: unlocked balance only.
- `refund`: return `"XMR refund not implemented for A+B+T model (deferred)"` —
  no sweep with a mismatched key.

## 6. Testing (unit, deterministic, no live monerod)

1. **`MoneroAddress` known-answer** — a published `(spend,view)`→address vector
   for one network byte. The anchor (like the keccak vectors that caught the SOL
   hashlock bug). Also: round-trip / checksum-tamper rejection.
2. **`combineSpendKeys`** — known-answer scalar add mod ℓ; zero-result rejected.
3. **Shared spend pub `A+B+T`** — point-add known-answer.
4. **`TestMoneroWalletRpc` sequence** — `createSharedAddress`/claim issue
   `generate_from_keys` → poll `get_height` → `sweep_all`, and **sweep is never
   issued before sync** (proves the G2 fix).

## 7. File map (drives the plan)

| Action | Path | Responsibility |
|--------|------|----------------|
| Create | `src/SwapDaemon/Monero/MoneroAddress.{h,cpp}` | Monero base58 + address encode |
| Modify | `src/SwapDaemon/Monero/AdaptorSignature.{h,cpp}` | add `sharedSpendPub(A,B,T)` point-add |
| Modify | `src/SwapDaemon/Monero/MoneroRpcClient.{h,cpp}` | `createSharedAddress`; sweep sync/name/height; `verifyLock` unlocked |
| Modify | `src/SwapDaemon/Monero/XmrChainClient.{h,cpp}` | per-swap shares; lock/verify/claim wiring; refund deferred-guard |
| Create | `src/SwapDaemon/tests/test_monero_address.cpp` | address known-answer |
| Create | `src/SwapDaemon/tests/test_xmr_shared_key.cpp` | combineSpendKeys + A+B+T point-add |
| Create | `src/SwapDaemon/tests/test_xmr_sweep_sequence.cpp` | `TestMoneroWalletRpc` call-order |
| Create | `src/SwapDaemon/tests/TestMoneroWalletRpc.h` | canned wallet-rpc double (`Test…`) |
| Modify | `src/CMakeLists.txt` | add new sources + test targets |

## 8. Open questions / flagged

1. **Per-swap XMR view-key shares:** the design needs each party's view-key
   *secret* share to form the shared view key. Confirm where these come from in
   the swap negotiation (may need a field in `SwapParams`/peer protocol). If not
   present, this cycle derives the shared view from the swap key material and
   flags it for the negotiation layer.
2. **Network byte:** default to the swap config's network (mainnet vs test/stage).
3. **Existing Monero base58 util:** check `src/Common`/`src/crypto` before writing
   a new one.
4. **Refund/punish** — deferred to a dedicated cycle (see §2).

## 9. Addendum: correct protocol model (researched — authoritative)

Reference: Gugger, *Atomic Swaps between Bitcoin and Monero* (arXiv:2101.12332);
COMIT `xmr-btc-swap`. This supersedes the `A+B+T` model above.

### Roles
- **M** — holds XMR, wants XFG (Monero seller).
- **F** — holds XFG, wants XMR (Monero buyer).

### Keys
- ed25519 spend-key shares: M has `s_m` (`S_m=s_m·G`), F has `s_f` (`S_f=s_f·G`).
- **Shared XMR spend key = `s_m + s_f`; shared spend pub = `S_m + S_f`** — two
  terms, **no adaptor term**.
- Shared view key = `v_m + v_f` (view secrets exchanged in the clear; both parties
  scan the shared address).
- XMR lock address = `MoneroAddress(S_m+S_f, (v_m+v_f)·G)`.

### Binding the two legs (same curve ⇒ no DLEQ)
- The XFG escrow's adaptor point is set to **`T = S_m`** (M's XMR spend pub share).
  XFG and XMR are both ed25519, so `S_m` is directly valid on both chains — **no
  cross-curve DLEQ needed** (the BTC↔XMR complication does not apply here).
- Hence the XFG-side adaptor secret **is `s_m`**.

### Happy-path flow
1. F locks XFG in the Musig2 escrow, adaptor-bound to `T = S_m`.
2. M locks XMR to the shared address `(S_m+S_f, v_m+v_f)`.
3. F verifies the XMR lock (unlocked balance ≥ amount; F scans via the shared view key).
4. M claims XFG by broadcasting the adapted signature → **reveals `s_m`** on XFG.
5. F **extracts `s_m`** from M's XFG claim sig, computes `s_m + s_f`, **sweeps the XMR**.

The secret that unlocks XMR for F is `s_m`, learned from M's XFG claim. The sweep
key is the 2-term `s_m + s_f`. There is no separate `t`.

### Refund symmetry (the hard part — DEFERRED)
If M never claims XFG, F refunds XFG and M must reclaim XMR — but M needs `s_f`.
Standard fix: **F's XFG refund tx is adaptor-bound under `S_f`**, so refunding
reveals `s_f` to M (who then reclaims XMR); a punish path covers F refunding after
M already claimed. Needs a second adaptor secret (`s_f`), careful timelock
ordering, and a punish tx → its own cycle + live verification.

### Corrections this model mandates in the existing code
- `AdaptorSigScheme::combineSpendKeys(a,b,adaptor)` (3-term) is **wrong** for this
  protocol — **deprecate/remove**. Use 2-term `computeFullSpendKey(own, extracted)`
  and `computeSharedSpendPub(A,B)=A+B` (both already exist and are correct).
- `XmrChainClient::claim` must reconstruct `ownShare + extractedPeerShare`, where
  the peer share is **extracted from the XFG claim adaptor signature**
  (`AdaptorSigScheme::extractSecret`) — not a standalone `adaptorSecret` arg.
- The XFG escrow adaptor point for an XMR swap must be set to the counterparty's
  XMR spend pub share `S_m`.
- Per-swap shares come from the swap negotiation; **view-secret shares must be
  exchanged** (new negotiation field) or the shared address can't be formed/scanned.

### Revised scope for THIS cycle (model now pinned to A+B)
Fund-safe, model-correct, unit-verifiable now:
1. `MoneroAddress` encoder (+ known-answer test) — correct under any model.
2. `sweepSharedAddress` sync-before-sweep (G2) + per-swap wallet name (G4) +
   restore_height (G5); `verifyLock` unlocked-only (G3) — model-independent.
3. `computeSharedSpendPub(A,B)=A+B` + 2-term `computeFullSpendKey` with
   known-answer tests; **deprecate the 3-term `combineSpendKeys`** so no caller can
   build a wrong sweep key.

Still deferred (needs the negotiation view-share field + live verification):
full `createSharedAddress` wiring into the live flow, the `T=S_m` XFG binding, the
claim-extraction path, and refund/punish.
