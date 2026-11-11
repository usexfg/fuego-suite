# Production Security Audit — Fuego SwapDaemon (2026-07-20)

**Scope:** `src/SwapDaemon/`, swap crypto (`adaptor`/`musig2`/`dleq`), offer relay / orderbook P2P, node swap RPC, docker exposure  
**Mode:** Full source audit (3 parallel review tracks: core, chain clients/SPV, P2P/offers)  
**Verdict:** **Not production-ready for mainnet value.** Lab/testnet only until CRITICAL blockers below are closed.

---

## Executive summary

| Subsystem | Score (end of session) | Notes |
|-----------|------:|-------|
| Crypto primitives | 6/10 | DLEQ, MuSig2 guards, encrypt-then-MAC present |
| Protocol completeness | 5/10 | Alice-locks + extract + P2P delivery wired; not e2e tested on mainnet |
| Persistence / recovery | 6/10 | ourSwapSecKey + ring nonces encrypted; escrow GI resolved after fund |
| Chain adapters | 4/10 | ETH registry path; BCH/XMR hardened; DCR improved; BTC/LTC stubs remain |
| P2P / offers | 6/10 | Signed PeerMessage transport; offer sigs; TWAP gossip dropped |
| Ops / docker | 5/10 | Loopback binds + swap-control-token; TLS to chain RPCs still missing |

**Composite (post waves 1–5): ~5 / 10 for public mainnet — testnet/lab only until e2e + external audit.**

Prior CRITICAL items already addressed in-tree before this audit (do not re-open without regression tests):
- Adaptor secret at-rest encryption (ChaCha8 + salt + HMAC-SHA256, key not in DB)
- Adaptor / DLEQ small-order point checks
- Wall-clock `timelockOrderingOk` helper
- SwapP2P default loopback + worker cap + pending queue bound
- StatusServer loopback bind
- Peer message Ed25519 signing (`SwapPeerProtocol`)
- Fuego `getTransactionOutputs` real implementation

---

## CRITICAL findings (still open or fixed this session)

| ID | Status | Title |
|----|--------|-------|
| C-CORE-1 | **FIXED** | `ourSwapSecKey` never persisted → stuck escrow after restart |
| C-CORE-2 | **FIXED** | Escrow GI now resolved after fund (not optimize local outIdx) |
| C-CORE-3 | **FIXED** | Ring order: KI → prefix hash → challenge (`writeAggregateKeyImageToTx`) |
| C-CORE-4 | **FIXED** | Round 1 material persisted (encrypted ring nonce secret) |
| C-CORE-5 | **FIXED** | Alice-locks: Bob claims with `t`; Alice `tryExtractClaimedSecret` |
| C-CORE-6 | **FIXED** | Serialize dropped secrets when enc key unset (now fail-closed) |
| C-ETH-1 | **FIXED** | Registry `lock()` + `contractId` claim (`setHtlcRegistry`) |
| C-ETH-2 | **FIXED** | Hashlock used adaptor *point* not `keccak256(secret)` |
| C-ETH-3 | **FIXED** | HTLC bytecode/registry wired at client registration |
| C-ETH-4 | **FIXED** | EIP-1559 empty access list RLP was `0x80` not `0xc0`; signed payload missing accessList |
| C-BCH-1 | **FIXED** | `lockHtlc` treated full JSON-RPC envelope as txid |
| C-XMR-1 | **FIXED** | `verifyLock` opens watch-only for shared address + view key |
| C-DCR-1 | **PARTIAL** | Fund from wallet UTXOs + pubkey/H(t); change/multi-input still weak |
| C-OFFER-1 | **FIXED** | Offer signature only over `offerId` (economic fields unsigned) |
| C-SOFT-1 | **FIXED** | Soft-order path locked **local** XFG for any maker’s offer |
| C-RPC-1 | **FIXED** | `--swap-control-token` gates control endpoints |
| C-DOCKER-1 | **FIXED** | Compose binds RPC/wallet to `127.0.0.1`; nginx `/wallet/` off |

---

## HIGH findings (selection)

- Timelock check skipped when CTR height unavailable → **FIXED** (fail closed on initiate/accept)
- `handleEscrowFunded` mutated `xfgAmount` post-fund → **FIXED** (mutation removed)
- Orderbook `pair` OOB → **FIXED** (reject `pair >= 8`)
- Order cancel ignored signature → **FIXED** (verify `cancel:<orderId>`)
- `PriceOracle::m_liveXfgUsd` uninitialized → **FIXED**
- ETH/SOL verifyLock incomplete (amount-only / no hashlock binding)
- Unsigned `COMMAND_SWAP_TRADE` manipulates TWAP
- BTC/LTC/KMD lock/claim stubs; SPV amount-only fallbacks
- DCR Neutrino peers never connected
- AFK auto-refund does not handle AFK states
- Cleartext HTTP to all chain RPCs

---

## Per-chain readiness (production) — end of session

| Chain | Production-ready? | Notes |
|-------|-------------------|-------|
| SOL | Partial | Lock with H(t); claim path real; verify still thin |
| BCH | Partial | Scripts + fee/RBF + pubkey/registry fixes; needs live testnet |
| XMR | Partial | Watch-only verify; cooperative refund only |
| ETH/ARB/BASE/BNB | Partial | Registry model wired; needs deployed contract + e2e |
| DCR | Partial | Fund path rewritten; not battle-tested |
| BTC/LTC/KMD | No | Lock/claim stubs; SPV verify fail-closed without redeem script |

**TON / Zano:** Still not implemented (intentionally deferred under plan A until fund-safety landed).

---

## Fixes applied this session

### Wave 1 (audit hardening)
1. **ETH hashlock** = `keccak256(adaptorSecret)` (`ethHashLockHex` / `solHashLockHex`)
2. **Load HTLC bytecode** from config paths for ETH/ARB/BASE/BSC
3. **EIP-1559 RLP**: `writeEmptyList()`; signed payload includes accessList
4. **BCH `sendtoaddress`**: parse JSON-RPC `result` string
5. **Timelock fail-closed** when counterparty height unavailable
6. **Remove post-fund fee mutation** of `xfgAmount`
7. **Soft-order maker gate**: only process own offers
8. **Offer canonical signature** over economic fields
9. **Order cancel signature verification**
10. **Order `pair` bounds checks**
11. **Encrypt + persist `ourSwapSecKey`**; refuse save without enc key when secrets live
12. **Initialize `m_liveXfgUsd`**

### Wave 2 (P0 fund safety)
13. **Collaborative ring order**: KI → prefix hash → challenge; never regenerate Round 1 after send
14. **Persist our Round 1 material** (encrypted ring nonce secret)
15. **Escrow global index** resolved after fund (not optimize local outIdx)
16. **SECRET_REVEAL** peer message: Bob→Alice adaptor preimage for CTR claim
17. **ETH registry model**: `setHtlcRegistry` + `lock()` / claim by `contractId` + `getContract` verify
18. **XMR verifyLock**: open watch-only for shared address + view key (not current wallet)

---

## Remaining work before mainnet (priority order)

### Wave 3 (continued audit fixes)
19. **SwapP2P transport**: `PEER_PROTOCOL` frames carry signed PeerMessage JSON; `deliverPeerMessage` / inbound callback wired; `--swap-p2p-port` / bind
20. **AFK refund**: timeout marks `AFK_REFUNDED` (time-lock unlock model)
21. **TWAP gossip**: unsigned `COMMAND_SWAP_TRADE` dropped; `recordLocalTrade` for local path only
22. **Pending request / fill-id caps** (DoS bounds)
23. **BCH verifyLock**: P2SH from redeem script `chainState`, not listunspent(txid); rawtx non-verbose
24. **DCR SPV**: refuse broken SPV mode (no Neutrino peers); RPC fallback only
25. **Docker**: RPC/wallet bind `127.0.0.1`; nginx `/wallet/` disabled

### Wave 4
26. **`tryExtractClaimedSecret`**: IChainClient API; BCH (SPV findSpend+parse); ETH (getContract preimage)
27. **Bob claim detect**: CTR_LOCKED polls extractSecret → ADAPTOR_SECRET_REVEALED
28. **`recordCompletedTrade`**: feeds PriceOracle + `recordLocalTrade` on XFG spend complete
29. **BCH lock**: requires 33-byte compressed `ctrPubKey`; fail closed on zero pubkey in RPC
30. **BTC/LTC/KMD SPV**: no amount-only any-script fallback without redeem script
31. **SPV wait path**: Alice claims with secret; Bob waits for on-chain preimage before confirm

### Wave 5
32. **Alice-locks protocol**: Alice locks H(t); Bob claims with t; Alice extracts on-chain; ADAPTOR_EXCHANGE carries H(t)
33. **BCH fee estimation** (`estimatesmartfee`) + claim nSequence RBF opt-in (`0xFFFFFFFD`)
34. **BCH getaddressinfo** pubkey resolve when wallet knows the address
35. **DCR lock**: fund from wallet UTXOs (`txid:vout`), require ctrPubKey + H(t)
36. **Swap control token**: `--swap-control-token` → `X-Swap-Token` / `Bearer` on control RPC

### Still open (post wave 5)
1. Full Neutrino TCP peer stack for DCR SPV
2. Cashaddr pure math → pubkey (impossible without wallet/key knowledge)
3. Multi-input change-aware DCR funding
4. SOL tryExtractClaimedSecret
5. End-to-end testnet matrix (SOL/BCH/ETH/XMR) with crash-restart
6. TLS to external chain RPCs
7. TON + Zano chain clients (deferred)

---

## Final session verdict (2026-07-21)

Workstreams completed under user plan **A** (audit → fix CRITICAL/HIGH → then chains):

1. Full multi-track production audit written to this file  
2. Waves 1–5 of fund-safety, protocol, P2P, ops fixes landed in source  
3. Key SwapDaemon units recompiled cleanly against existing build tree  
4. **TON/Zano not started** — correct gate until e2e on existing pairs  

**Do not put real mainnet value on this until:**
- Fresh cmake/link of `xfg-swapd` in this worktree  
- Live testnet round-trips for at least SOL + one EVM + BCH  
- External review of adaptor + ring collaborative spend  
- Operators enable `--swap-control-token` and keep RPC on loopback/firewall

### P1 — Network authenticity
6. Auth all swap control RPC; default restricted on public binds
7. Docker: loopback-only wallet/RPC; remove unauthenticated nginx `/wallet/`
8. Authenticate or remove unsigned TWAP trade gossip
9. Cap `m_pendingRequests` / fill-id sets

### P2 — Then Zano / TON
10. **Zano** — clone `XmrChainClient` + Zano RPC (CryptoNote sibling)
11. **TON** — TVM HTLC contract + TL-B/BoC client (~3–4 days)

---

## Suggested verification matrix

- [x] Crash/restart mid-swap secrets recover (`test_production_gates` + enc blob fix)
- [x] Adaptor extract unit (`test_adaptor_roundtrip`)
- [x] Collaborative ring verifies under `check_ring_signature` (`test_ring_collab` size-1 + size-9)
- [x] SOL localnet: lock → claim → extract (`test_sol_e2e` 2026-07-22)
- [x] ETH anvil: HashedTimelock lock → claim → getContract preimage (2026-07-22)
- [x] BCH offline: H(t)=SHA256 → claim tx → parseClaimPreimage recovers t
- [x] XFG spend SM: SECRET_REVEALED / SPV → XFG_SPENT (`test_xfg_spend_states`)
- [x] Dual-party XFG protocol: keys + adaptor extract + collab ring (`test_xfg_dual_protocol`)
- [x] Local smoke: `fuegod --testnet` RPC + `xfg-swapd list` (`scripts/smoke-swap-local.sh`)
- [ ] On-chain fundEscrow e2e (testnet miner currently fails block verification; MixIn sparse)
- [ ] Two swapd funded swap end-to-end
- [ ] BCH live chain node claim + extract
- [x] Offer rebroadcast with mutated rate is rejected
- [x] Soft-order for foreign makerPubKey does not lock local funds
- [x] `timelockOrderingOk` rejects inverted windows

---

## Next step

Plan A fund-safety waves are closed in source. Recommended order from here:

1. Fresh full cmake/link of `xfg-swapd` + `fuegod` (stale `build_p34` link path for `libRpc.a` still breaks Daemon)
2. Live testnet matrix: SOL + one EVM + BCH with crash-restart
3. External review of adaptor + collaborative ring
4. Then Zano (XMR sibling) / TON design

**Build note (final review):** worktree `build_p34/CMakeCache.txt` still points at `/Users/aejt/xfgo` — do not treat that tree as proof the worktree sources linked. Fresh cmake from this worktree is required before binary claims.

**TWAP residual:** gossip `handleTradeCompleted` is intentionally a no-op; only `recordLocalTrade` mutates oracle data. Handler still present for wire compat.
