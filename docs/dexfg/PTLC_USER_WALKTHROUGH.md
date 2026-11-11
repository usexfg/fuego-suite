# PTLC User Walkthrough — No Doubt What To Do

> For Fuego `xfg-swapd` + `fuego-flutter-wallet` DeXFG tab + `swapxfg` TUI.  
> Covers **PTLC/PTLC** (pure), **PTLC/HTLC bridge** (current default), **HTLC legacy** (fallback).  
> You do NOT need to pick a mode — the daemons negotiate it. This guide shows what happens, what you see, and what to do when it says `requirePtlc`.

---

## 0. TL;DR — What You Do Every Time

1. Open **DeXFG** (flutter wallet) or `swapxfg` → pick pair `XFG ↔ CTR` (BTC, ETH, SOL, BCH…).
2. Enter `XFG amount` + `CTR amount` + `CTR address` (where you want CTR). Leave `require PTLC` **off** unless you know why.
3. Press **Initiate / Accept**. Wallet calls `xfg-swapd` `initiate`.
4. Watch status: `KEYS_EXCH → ESCROW_FUNDED → PRESIGS_READY → CTR_LOCKED → SECRET_REVEALED → XFG_SPENT`.
5. Done: CTR lands at `ctrAddress`, XFG escrow claim pays counterparty. If any step says `WAITING_SPV` wait 1-6 confs. If `REFUNDED` check timeout.

That is it. The **lockType badge** tells you which mode was negotiated: `PTLC`, `BRIDGE`, `HTLC`. You do not choose it; you just read it.

---

## 1. The Three Modes (What They Mean)

| Mode | Badge | When | On-chain lock | Secret reveal | Privacy | Who supports now |
|------|-------|------|---------------|---------------|---------|------------------|
| **PTLC/PTLC** | `PTLC` | Both chains support PTLC | Point `T=t*G` off-chain + adaptor `s'` ; on-chain point commitment (Taproot P2TR / Solana `ClaimPtlc`) | Publish adapted `s=s'-t` → extract `t=s'-s` | **Per-hop `T_i` decorrelated** `T_i=T+y_i*G`, scriptless key-path, wormhole resistant | XMR/ZANO native; BTC/LTC Phase2 Taproot; SOL Phase4 `ClaimPtlc` |
| **PTLC/HTLC bridge** | `BRIDGE` | XFG always PTLC, CTR HTLC-only | XFG `T` + `Q=t*escrowPubKey` DLEQ; CTR `H(t)` hash | Reveal `t` on CTR hash lock, extract same `t` for XFG adaptor | `T` decorrelated in gossip, DLEQ proves same `t`, on-chain still hash | **ETH, ARB, BASE, BNB, POLY, BCH, TON, SIA, DOGE, DASH, ZEC** — all live as bridge |
| **HTLC legacy** | `HTLC` | Old peer pre-PTLC or staging | Hash `H(t)` both sides | Preimage `t` push | Linkable hash | Only old `xfg-swapd` < v1.5 |

**Your wallet today will almost always show `BRIDGE`.** That is correct for Phase1. `PTLC` appears only for XMR↔XFG internal sweep or future BTC Taproot.

---

## 2. What You See Per Step (DeXFG + swapxfg)

### Initiate (Alice has CTR, wants XFG)
```text
DeXFG → Amounts → [ ] Require PTLC → Initiate
→ status: KEYS_EXCHANGED (pubkeys aggregated → P)
→ Bob picks t, T, Q, proof, H(t), ptlcPoint, lockType → ADAPTOR_EXCHANGE
→ ESCROW_FUNDED (Bob funds XFG→P)
→ PRESIGS_READY (nonces + adaptor presigs)
→ CTR_LOCKED (Alice locks CTR)
  • HTLC:   IChainClient::lock → HashedTimelock.sol / BtcHtlcScript P2WSH
  • BRIDGE: same HTLC + ptlcPoint stored "|ptlc:..." off-chain
  • PTLC:   BtcPtlcScript P2WSH point commitment
→ SECRET_REVEALED (Bob claims CTR with t → Alice extracts)
→ XFG_SPENT (Alice adapted sig claims XFG)
```

Badge in DeXFG `DexScreen` pair bar: `BRIDGE` amber, `PTLC` green, `HTLC` grey. Hover shows `T=ab..` `H=cd..`.

### Accept (Bob has XFG, wants CTR)
Same states but you are Bob: you fund escrow, send `T+Q+proof+H(t)`.

### Waiting
- `WAITING_SPV` → SPV mode, need 1-6 confs on CTR (BCH/BSV/DCR Neutrino). Do not close wallet.
- `SECRET_CONFIRMED_SPV` → confs done, proceeding to claim.

---

## 3. Walkthroughs

### A. BRIDGE (XFG ↔ ETH) — What Most Users Do Today

**Alice (CTR=ETH, XFG-wanter):**
1. DeXFG → `XFG/ETH` → `XFG amount: 10` `ETH amount: 0.05` `ETH address: 0xYourEth…` `Require PTLC: off` → **Initiate**.
2. Log: `PTLC negotiation: CTR ETH HTLC-only → PTLC_HTLC_BRIDGE` `Adaptor point T: 02ab…` `DLEQ ok`.
3. Wait `ESCROW_FUNDED` (Bob's XFG tx). Dashboard shows `escrowTxHash`.
4. Auto `PRESIGS_READY`. Then you fund ETH: `EthChainClient::lock` deploys `HashedTimelock` with `hashLock=keccak256(t)` + event `PtlcLocked(ptlcPoint)`. TxId appears `ctrLockTxId`.
5. `CTR_LOCKED`. Bob's daemon will `claim` ETH by sending `t`.
6. Your daemon polls `tryExtractClaimedSecret` → `t*G==T` ok, `keccak256(t)==hashLock` ok → `SECRET_REVEALED`.
7. Auto `finalizeEscrowSpend` → `XFG_SPENT`. ETH claimed, XFG paid.

**Bob (XFG-holder):**
- Same but you see `Delivered ADAPTOR_EXCHANGE (T + H(t) + ptlcPoint)` then you fund XFG then wait for Alice's ETH lock then claim ETH.

**If `Require PTLC` on:** Alice had `Require PTLC: on` but ETH is HTLC-only. At initiate `SwapDaemon.cpp:812` aborts `requirePtlc=true but CTR chain does not support PTLC — aborting`. You see `INITIATE failed: requirePtlc abort`. Turn it **off** or pick a PTLC chain (XMR).

### B. PTLC (XFG ↔ XMR) — Native Adaptor

1. Pair `XFG/XMR` → amounts → `XMR address` → Initiate.
2. Badge `PTLC` green. No `hashLock` at all (`hashLock==0`). `htlcHashLock` zero in `MsgAdaptorExchange`.
3. Bob funds XFG escrow → Alice watches XFG claim → reveals `xmrSpendShare` via `XMR_SHARE_REVEAL` after `XFG_SPENT`.
4. No HTLC contract, just shared CryptoNote address `xmrSpendPub+peer`. You will see `XMR flow: XFG claim broadcast (t revealed on-chain)` then `waiting for Alice's spend-share`.

### C. PTLC (future XFG ↔ BTC Taproot)

1. Pair `XFG/BTC` with updated node (both `supportsPtlc=true`). Initiate shows `PTLC negotiation: CTR BTC supports PTLC → BRIDGE (Phase1)` today, `PTLC` after Phase2.
2. Phase2 pure: lock creates `BtcPtlcScript::createPtlcScript` P2WSH with `T` x-only, witness `[sig, t, 0x01, redeem]`. Claim extracts `t` from witness[1] and verifies `t*G==T` off-chain. No hash. UI still `BRIDGE` until you upgrade.

---

## 4. The `Require PTLC` Toggle — When to Use

- **Off (default):** Allow `BRIDGE` fallback. Works with every peer and every chain. Use this **unless you have a privacy reason**.
- **On:** Abort if peer or chain cannot do PTLC. You get a clean error **before any funds lock** (`INITIATE failed` or `ADAPTOR_EXCHANGE downgrade blocked`). Use when:
  - You need per-hop decorrelation (you are routing or you care about hash linkability).
  - You are testing PTLC.
  - You are an auditor.

**Do not turn it on for EVM swaps today** — they are bridge, not pure, so you will abort every time. The warning `requirePtlc=true but CTR chain does not support PTLC` is intentional.

Downgrade attack resistance: If you have `requirePtlc on`, a malicious peer cannot force you to `HTLC`. The daemon checks `localRequire && peer lockType==HTLC → reject` `SwapDaemon.cpp:3505`.

---

## 5. Fallback — What Happens If Peer Is Old

- You `BRIDGE`, peer `HTLC` (old, no `lockType` field). On wire `deserialize` missing `lockType` → defaults `HTLC` `SwapPeerProtocol.cpp:138`. Your daemon sees `peer lockType HTLC` but your `localRequire` false → accept, but `ptlcPoint` zero. You will still proceed as `BRIDGE` (since you have `T` locally). Old peer will ignore `ptlcPoint` and just use `hashLock`. Cross-compatible.
- If you `requirePtlc on` and peer old → `downgrade blocked` abort. Tell peer to upgrade.

---

## 6. Troubleshooting — What To Do

| Symptom | Cause | What to do |
|---------|-------|------------|
| `INITIATE failed: requirePtlc abort` | You asked PTLC but chain is HTLC-only | Turn **off** `Require PTLC` or pick XMR/BTC Taproot after Phase2 |
| `DLEQ proof verification failed!` | Peer sent bad `T`/`Q` (bad t or corrupted) | Swap aborts, no funds moved. Retry. If persistent, peer is malicious — `FAILED`. |
| `Extracted preimage does not match adaptorPoint T — rejecting` | Claim tx reveals wrong `t` or malleated `s'` | Daemon rejects, stays `CTR_LOCKED`, waits for correct claim or timeout refund. Do not manual claim. |
| `H(t) does not match hashLock — rejecting` | HTLC hash mismatch (wrong secret) | Same — rejected, wait. |
| Stuck `CTR_LOCKED` 10m | Bob hasn't claimed CTR yet | Normal — Bob must claim before `ctrTimeoutBlock`. If past timeout, you can `refund` CTR via `swap_daemon_client refund` or wait auto-refund. XFG escrow refunds after `xfgTimeoutHeight` via `broadcastEscrowRefundDirect`. |
| Stuck `WAITING_SPV` | Need more confirmations on BCH/DCR | Open `xfg-swapd status` `curl localhost:18900/status`, check `confirmations`. Wait. |
| `chainState invalid redeem` | `|ptlc:` suffix not stripped (old build) | Update `xfg-swapd` to >= PTLC build. |
| `PTLC downgrade blocked` in logs | Peer tried to force HTLC | Good — your `requirePtlc` protected you. If you did not want that, turn `Require PTLC` off. |

**Timeout safety:** `timelockOrderingOk` ensures `xfgTimeoutHeight` outlasts `ctrTimeoutBlock` by `SAFETY_MARGIN 3600s` wall-clock (`SwapTimelock.h:15`, `SwapDaemon.cpp:797`). Never set `ctrTimeoutBlock` manually unless you understand block times (`SwapTimelock.cpp:19` `msPerBlock`: XFG 480s, BTC 600s, ETH 12s, SOL 400ms). Auto-derive is safe.

---

## 7. How to Add a New Chain (for power users, one paragraph)

Pick family in `src/SwapDaemon/fuego-add-swap-chain:1`: EVM → header-only `EthChainClient` subclass; UTXO → copy `BtcHtlcScript` + `BtcPtlcScript` style; non-standard → custom `IChainClient`. Add `SwapPair::FOO` `SwapTypes.h:77`, `supportsPtlc()` true iff you implement `lockPtlc()` with point commitment, register in `ChainRegistry.cpp:5` and `SwapDaemon.cpp:213`, `SwapTimelock.cpp` block time, `PriceOracle.cpp` seed rate, `PtlcTimelock.sol` event if EVM. See `PTLC_DEV_GUIDE.md:4`.

---

## 8. Fees (unchanged by PTLC)

Swap fee 2% total (1% initiation + 1% claim) `CryptoNoteConfig.h:148` `SWAP_FEE_RATE_BPS=100` split `69 CD Yield / 11 Bonus Vault / 20 Treasury`. PTLC does not change fee. `HEAT_MINT_PREMIUM_BPS=0`. Hearth 1% taker fee separate.

---

## 9. Quick CLI (`swapxfg` TUI)

```bash
# Check negotiated mode before funding
xfg-swapd status | jq .swaps[].lockType
# BRIDGE, PTLC, HTLC

# Force PTLC (will abort if not possible)
swapxfg initiate --pair BTC --xfg 1 --ctr 0.001 --require-ptlc

# Normal (allow bridge)
swapxfg initiate --pair ETH --xfg 1 --ctr 0.02

# Inspect point
xfg-swapd getswap <swapId> | jq .ptlcPoint
```

---

> If in doubt: **leave `Require PTLC` off**. You get bridge privacy (DLEQ + per-swap `T`) with HTLC safety on every chain. Turn it on only to enforce pure PTLC.
