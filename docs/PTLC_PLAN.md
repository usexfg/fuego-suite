# Fuego PTLC Implementation Plan — Dual PTLC/HTLC with Fallback

> Version: 1.0 — 2026-08-23 — Status: APPROVED FOR EXECUTION
> Scope: Add Point Time Locked Contracts (PTLC, Schnorr adaptor) alongside existing Hashed Time Locked Contracts (HTLC) as fallback for networks not yet PTLC-compatible.
> References: Scriptless Scripts (Poelstra 2017), IACR 2018/472 (Malavolta), BIP340/341/327, One-Time VES (Fournier), Schröder 2024 Foundations of Adaptor Signatures, COMIT RFC-003, Boltz 2503.12719, rust-secp256k1-zkp

---

## 1. Executive Summary

Fuego today uses a hybrid: XFG escrow is PTLC-like (MuSig2 adaptor `T=t*G` on ed25519, no hash) while counterparty chains use HTLC hashlocks `H(t)` (`keccak256` for EVM/SOL, `SHA256` for BTC/BCH/LTC, `blake2b` for SIA). This achieves atomicity via `t` extraction but leaks hop correlation (same `H(t)` everywhere) and on-chain script footprint.

PTLC replaces `H(t)` with a point lock `T=t*G` and an adaptor signature `s'`. Claim publishes `s=s'-t`; extractor recovers `t=s'-s`. Benefits: per-hop decorrelation, scriptless cooperative Taproot spend, wormhole resistance, proof-of-payment without hash preimage sharing. Cost: requires Schnorr/Taproot (BIP340/341 + MuSig2 BIP327) or ECDSA adaptor + DLEQ proof for legacy.

**Decision: implement dual-stack with feature negotiation and fallback.**

- If both chains support PTLC → use PTLC on both legs (point lock).
- If counterparty chain is HTLC-only → use PTLC on XFG + HTLC on counterparty bridged via `H(t)` + DLEQ `log_G(T)=log_H(H(t))` (COMIT/Boltz hybrid). Existing Fuego flow is already this bridge; we formalize it and add explicit `lockType` enum.
- Keep HTLC code paths intact, behind `lockType` branch. No breaking change to existing swaps.

**Zero dummy code rule applies.** Every new file implements its real purpose. TODOs only inside phased plan with `// TODO(Phase N)` tag after user approval of this plan.

---

## 2. Research Synthesis

### 2.1 PTLC vs HTLC

| Property | HTLC | PTLC (Schnorr adaptor) |
|----------|------|------------------------|
| Lock | `H(preimage)` `OP_HASH160 <h> EQUALVERIFY` | `T=t*G`, adaptor `s'=k+e*sk+t`, `s'*G=R+e*P+T` |
| Unlock | Reveal 32B preimage | Publish completed Schnorr `s=s'-t`; extract `t=s'-s` |
| Correlation | Same `H(s)` every hop — linkable | Per-hop `R_i = L_i + y_i*G`, `R_{i+1}=L_i` — unlinkable |
| Footprint | Explicit hash script | Cooperative Taproot key-path looks like single-sig P2TR |
| Proofs | Preimage known to all | Only sender learns `z`; `z*G` + invoice for PoP |
| Reqs | Any chain with hashlock | Schnorr + Taproot or ECDSA adaptor + DLEQ |

### 2.2 Real Implementations

1. **BlockstreamResearch/scriptless-scripts** — canonical spec `multi-hop-locks.md`, `atomic-swap.md`, `musig2-adaptorsig.md` + paper `eprint.iacr.org/2018/472`.
2. **LND/dcrlnd PTLC PoC** — `update_add_ptlc` vs `update_add_htlc`, 2.5 RTT single-sig / 3.5 RTT MuSig2, `instagibbs` gist + `decred/dcrlnd` branch `v0.3.1...matheusd:ptlc-poc`.
3. **rust-lightning/LDK & secp256k1-zkp** — `secp256k1_ecdsa_adaptor_*` (162B adaptor+proof) + MuSig2, Elements/Liquid Taproot live.
4. **COMIT (Bitcoin-Ethereum)** — `RFC-003` HTLC template + planned PTLC hybrid (BTC PTLC, EVM HTLC `C=H(sa)` + oracle).
5. **Boltz** — `boltz-core` + Elements `swap_tree`/`musig` + paper `2503.12719` Taproot PTLC with pre-signed adaptor.

### 2.3 Crypto Construction

- **Schnorr adaptor**: group `G` prime `q`, `H(P||R||m)`, sig `(R,s)` `sG=R+H*P`, adaptor `(R,s',T)` `s'G=T+R+H*P`, `adapt s=s'-t`, `extract t=s'-s`. Domain-separate `T` in challenge.
- **MuSig2 adaptor**: `keyAgg→P`, `R=R1+b*R2`, `b=hash(P,T+R1,R2,m)` includes `T` for non-interactive nonce.
- **ECDSA adaptor + DLEQ**: Fournier One-Time VES `secp256k1_ecdsa_adaptor_encrypt/verify/decrypt/recover` + DLEQ `R,T` binding; without DLEQ: GHSA `c7q2-gv3g-rgxm` forged `r'` passes verify but adapt yields invalid sig.
- **Cross-curve**: scalar `t < min(order_A,order_B)` or split scalars + ZK; Fuego ed25519 (`q≈2^252`) vs secp256k1 (`n≈2^256`) — restrict `t` via `TagHash("FuegoPtLc",secret)[0:32] mod q`.

### 2.4 Security Pitfalls

1. Nonce reuse → key leak (`x=(s1-s2)/(e1-e2)`). Enforce BIP340 `nonce=H(sk||aux_rand||msg||T)`, synthetic MuSig2 nonces.
2. ECDH leak with ECDSA adaptor — separate swap keys from wallet keys.
3. Missing DLEQ → unadaptable pre-sig (GHSA-c7q2) — enforce DLEQ proof.
4. Adaptor malleability — bind `T` in `e`, low-S.
5. Half-aggregation incompatibility — disable.
6. Leaky pre-sig double-sign — fresh nonce per `(m,Y)` session.
7. Curve field mismatch — range check `t`.
8. Refund griefing — pre-signed refund before funding, `timelockOrderingOk` with `+3600s` margin.

---

## 3. Fuego Current State

- **XFG escrow**: ed25519 MuSig2 adaptor (`src/crypto/musig2.h:42`, `adaptor.h:41`, `AdaptorSwap.h:23`). Already PTLC-like.
- **Counterparty HTLC**: EVM `HashedTimelock.sol:6`, BTC `BtcHtlcScript.h:33` P2WSH `OP_SHA256`, SOL `htlc_program.rs:23` keccak, BCH `HtlcScript.h`, TON `htlc.fc`, Sia `blake2b`.
- **Hash dispatch**: `SwapHashLock.h:22` `sol/bchHashLockHex` → `keccak256(t)`/`SHA256(t)`.
- **Peer protocol**: `SwapPeerProtocol.h:91` `MsgAdaptorExchange {T,Q,proof, H(t)}`. `SwapTypes.h:168` `hashLock`, `SwapStateMachine.h:23` state flow `KEYS_EXCH → ESCROW_FUNDED → PRESIGS_READY → CTR_LOCKED → SECRET_REVEALED → XFG_SPENT`.
- **Interface**: `IChainClient.h:10` `lock/verifyLock/claim/refund/tryExtractClaimedSecret`. `ChainRegistry.h:12` 27 pairs (`SwapTypes.h:77`).
- **Rust secp256k1 adaptor**: `fuego-swapd-adaptor/src/lib.rs:1` Schnorr `s'=k+e*sk+t` — exists but not linked into daemon.

---

## 4. Target Architecture — Dual Stack

### 4.1 Capability Matrix

| Chain | PTLC native? | Scheme | Fallback |
|-------|--------------|--------|----------|
| Bitcoin Taproot P2TR | Yes (Schnorr BIP340/341 + MuSig2) | Schnorr adaptor `s'*G=R+e*P+T` | — |
| Bitcoin legacy P2WSH/P2SH, LTC, BCH, KMD, DOGE, DASH, ZEC, DCR | No | ECDSA adaptor `secp256k1_ecdsa_adaptor_encrypt` + DLEQ (162B) | Keep HTLC `OP_SHA256` |
| Elements/Liquid | Yes (Taproot 2021) | Schnorr adaptor | — |
| Ethereum + EVMs (ARB/BASE/BNB/POLY/GLEEC/AVAX/CRO/BOB/UNICHAIN/PLASMA/MONAD/OP/PULSEX/ROBINHOOD) | No (no Schnorr opcode; ecrecover=ECDSA) | **PTLC_HTLC_BRIDGE**: PTLC `T` on XFG + HTLC `H(t)` on EVM + DLEQ bridge `Q=t*escrowPubKey` | HashedTimelock HTLC |
| Solana SBF `xfg_htlc` PDA | Partial (ed25519 adaptor via syscall) | ed25519 adaptor `c=Hs(R+T,P,m)` | keccak HTLC |
| TON FunC `htlc.fc` | No | Bridge | SHA256 HTLC |
| SIA `blake2b` | No | Bridge | blake2b HTLC |
| XMR/ZANO CryptoNote | Yes (ed25519 adaptor-only, already point-based) | Native adaptor — no hashlock | — (currently `hashLock==ZERO`) |

### 4.2 Design Principles

1. **No breakage**: All HTLC paths remain, behind `lockType` branch.
2. **Negotiation, not config**: `supportsPtlc()` per `IChainClient`; `lockType` decided per swap in `handleKeysExchanged` based on `min(capA,capB)` + peer `SwapPeerProtocol` feature bit. Downgrade attack resistance: sender can set `requirePtlc=true` to abort if peer forces HTLC.
3. **Point/hash dual fields**: `SwapParams` carries both `ptlcPoint (PublicKey)` and `hashLock (Hash)`; verifier checks the field matching `lockType`.
4. **DLEQ bridge**: For `PTLC_HTLC_BRIDGE`, store both `T` and `H(t)` + DLEQ proof that they share `t` (ed25519 `Q=t*escrowPubKey` already exists; add `hashCommit=H(t)` binding via `keccak`/`sha256` check `H(t)==hashLock` only in bridge mode).
5. **Scriptless vs script**: PTLC cooperative spends look like single-sig; script path `OP_CHECKSIGVERIFY` with `T` tweak for non-coop. Legacy HTLC scripts retained.
6. **Secret extraction**: `tryExtractClaimedSecret` for PTLC parses `s'-s` not preimage push.

### 4.3 LockType Enum

```cpp
enum class SwapLockType : uint8_t {
  HTLC = 0,              // legacy hashlock H(t)
  PTLC = 1,              // native point lock T, adaptor verify
  PTLC_HTLC_BRIDGE = 2   // PTLC on XFG leg, HTLC H(t) on CTR leg + DLEQ bridge
};
```

`SwapTypes.h:133` additions:

```cpp
SwapLockType lockType = SwapLockType::HTLC;
Crypto::PublicKey ptlcPoint;   // T = t*G for PTLC / BRIDGE
bool requirePtlc = false;      // sender policy: abort if peer cannot do PTLC
```

Serialization in `SwapStateMachine.cpp:254` via `podToHex`/`hexToPod` for `ptlcPoint`; backward compat: missing field → `HTLC`.

### 4.4 IChainClient Extensions

`IChainClient.h:10`:

```cpp
virtual bool supportsPtlc() const { return false; } // override in PTLC-capable clients
virtual ChainClientResult lockPtlc(const SwapParams& p) { return ChainClientResult::fail("PTLC not supported"); }
virtual bool verifyPtlcAdaptor(const SwapParams& p, const std::string& adaptorSigHex) { return false; }
```

Default `lock()` checks `params.lockType`: if `PTLC` and `supportsPtlc()` call `lockPtlc()`, else HTLC path. This keeps all 15 clients shimmable with minimal change; only PTLC-capable overrides need real impl.

`tryExtractClaimedSecret` branching: PTLC → parse Schnorr sig stack ` [<sig> <R> <T>] ` or Solana `sig+presigR` and compute `t=s'-s`; HTLC → parse preimage push (existing `parseClaimPreimage`).

### 4.5 Peer Protocol Changes

`SwapPeerProtocol.h:91`:

```cpp
struct MsgAdaptorExchange {
  Crypto::PublicKey adaptorPoint;   // T
  Crypto::PublicKey adaptorDleqQ;   // Q
  Crypto::DLEQProof dleqProof;
  Crypto::Hash htlcHashLock{};      // H(t) for HTLC/BRIDGE; zero for pure PTLC
  SwapLockType lockType = SwapLockType::HTLC; // new, default HTLC for wire compat
  bool requirePtlc = false;
  Crypto::PublicKey ptlcPoint{};    // T duplicated for pure PTLC verification (same as adaptorPoint)
};
```

Wire JSON adds `lockType`, `ptlcPoint`. Old peers without field → deserialize as `HTLC` (forward compat).

`SwapDaemon.cpp:992` fill: `fillAdaptorExchange()` now sets `lockType = negotiateLockType(localCaps, peerCaps, params.requirePtlc)` where `negotiateLockType` returns `PTLC` if both `supportsPtlc()`, `PTLC_HTLC_BRIDGE` if only XFG side PTLC, else `HTLC`.

DLEQ base remains `escrowPubKey` (`AdaptorSwap.cpp:97`). For `BRIDGE`, additionally verify `H(t)==htlcHashLock` before accepting.

### 4.6 Per-Chain PTLC Scripts/Programs

#### EVM — `PtlcTimelock.sol`

Deploy alongside `HashedTimelock.sol`. Two options; pick **A** minimal wrapper:

- **Option A (bridge-friendly, keep HTLC hash)**: Extend `HashedTimelock` with `PtlcClaim` event; off-chain adaptor sig publication still reveals `t` which satisfies `keccak256(preimage)==hashLock`. No contract change needed — PTLC_HTLC_BRIDGE already works. Add `lockPtlc` alias that stores `ptlcPoint` in event but verifies hash on claim. Lowest risk.
- **Option B (native Schnorr adaptor)**: New `PtlcTimelock.sol` verifying `s'*G == R + e*P + T` via `ecrecover` trick is not directly possible for Schnorr; requires `ecMul` precompile (not available pre-Prague). Defer B to Phase 4 behind `secp256r1` EIP. So Phase 1 uses A (bridge).

Thus EVM PTLC in Phase 1 = `PTLC_HTLC_BRIDGE` with DLEQ bridge; contract unchanged except adding `ptlcPoint` emit for decorrelation indexing.

#### BTC Taproot — `BtcPtlcScript`

New `src/SwapDaemon/Bitcoin/BtcPtlcScript.h` / `.cpp`:

- `createPtlcScript(T, timeout, recipientPubKey, senderPubKey, internalKey)` → Taproot output `P2TR` with tweaked internal key `P_tweak = P + TapTweak(P||m_swap)*G` committing `m_swap = {T,timeout}` (Boltz 2503.12719). Script path: `<T> OP_CHECKSIGVERIFY <adaptedSig> OP_CHECKSIG` else `<timeout> CLTV <sender> CHECKSIG`.
- `createClaimWitness(adaptedSig)` → witness `<adaptedSig> <controlBlock> <script>`; `parseClaimAdaptorSecret(rawTx, p2trScriptPubKey, presigRHex) → t` via `t = s' - s` (requires presig `R,T` stored in `SwapParams.ptlcPoint` + `musig2.aggNonce`).
- `verifyPtlcAdaptor(P,T,presig,sig,msg)` wraps `fuego-swapd-adaptor` `adaptor_verify`.

HTLC script `BtcHtlcScript` retained for fallback.

#### Solana — `htlc_program.rs` PTLC extension

- Keep `HtlcState` but add `ptlc_point: [u8;32]` and `lock_type: u8` (0=HTLC 1=PTLC).
- New instruction `ClaimPtlc { adaptor_sig: [u8;64], presig_r: [u8;32] }` verifies `ed25519 adaptor` via `solana_program::ed25519_program` CPI or `keccak(hash(R,P,msg))` tweak inline (match `adaptor.cpp:131`). Store `ptlcPoint` via `SolChainClient::lockPtlc`.
- `SolChainClient.cpp:31` branches on `lockType`; `verifyLock` checks `ptlc_point` equality; `tryExtractClaimedSecret` for PTLC returns `adaptedSig` extraction.

#### TON / SIA

No native PTLC opcode → remain `PTLC_HTLC_BRIDGE` (no contract change). SwapDaemon `negotiateLockType` will yield `BRIDGE` for these pairs.

#### XMR/ZANO

Already PTLC/adaptor-only (`hashLock==ZERO`). Mark as `PTLC` explicitly (`isPtlcPair=true`). No change except `lockType` labeling.

### 4.7 Adaptor / MuSig2

- `src/crypto/adaptor.h` ed25519 adaptor already supports `R+T` session init (`musig2.cpp:205` `adaptor_point` nullable). No change; review constant-time and zeroization.
- `fuego-swapd-adaptor` secp256k1 Schnorr adaptor linked as optional `USE_SECP256K1_PTLC` CMake flag. C++ `BtcPtlcScript::verifyPtlcAdaptor` calls Rust via `cxx` FFI or pure C++ reimpl using `libsecp256k1` + tagged hashes `Fuego/adaptor_challenge`, `MuSig/keyagg`. Prefer C++ reimpl to avoid Rust toolchain in daemon — copy tagged hash logic from `lib.rs:105`.
- Fresh nonce per `(msg,T)` binding: `presigSessionHash(escrowTxHash || T)` as `msg` domain.

---

## 5. Phased Execution Plan

### Phase 0 — Foundation + Negotiation (3 days)

| Step | Task | File | Done check |
|------|------|------|------------|
| 0.1 | Add `SwapLockType` enum, `ptlcPoint`, `requirePtlc`, `lockType` to `SwapParams` + serialization + `swapStateToString` | `SwapTypes.h:77`, `SwapTypes.cpp`, `SwapStateMachine.cpp:254` | `SwapParams` round-trip via JSON, missing field defaults to HTLC, `make test` |
| 0.2 | Extend `IChainClient` with `supportsPtlc()`, `lockPtlc()` shim, branch `lock()` on `lockType` | `IChainClient.h:10` | All 15 clients compile with default shim |
| 0.3 | Extend `SwapPeerProtocol` `MsgAdaptorExchange` with `lockType`, `ptlcPoint`, wire JSON compat | `SwapPeerProtocol.h:91`, `SwapPeerProtocol.cpp`, `SwapDaemon.cpp:992` | Old peer deserialize → HTLC; new → PTLC/BRIDGE |
| 0.4 | `SwapHashLock` → add `SwapPtlcLock` helpers: `ptlcPointHex()`, `verifyPtlcPoint(T,t)`, `negotiateLockType()` | `SwapHashLock.h`, `SwapPtlcLock.h` new, `SwapHashLock.cpp` | Unit test `negotiateLockType` matrix 3x3 |
| 0.5 | `SwapDaemon` negotiation: `negotiateLockType()` in `handleKeysExchanged`, store, enforce `requirePtlc` downgrade abort, `timelockOrderingOk` unchanged | `SwapDaemon.cpp:793` | Swap `INITIATED→KEYS_EXCH` sets `lockType`, abort logs `downgrade blocked` |
| 0.6 | Update `SwapOfferRelay.h:145` `SwapRequestResult` + `SwapOrder` to carry `ptlcPoint`/`lockType` | `SwapOfferRelay.h` | Gossip carries PTLC flag, backward compat |

**Confidence:** 90% — no crypto, only plumbing. **Risk:** serialization bloat → keep hex 64 + uint8.

### Phase 1 — EVM/SOL Bridge + Framework Hardening (5 days)

| Step | Task | File | Done check |
|------|------|------|------------|
| 1.1 | EVM: keep `HashedTimelock.sol` but add `PtlcTimelock.sol` wrapper emitting `PtlcLocked(ptlcPoint)`; `EthChainClient` override `supportsPtlc()=false` (so `negotiate` → `BRIDGE`), branch `lockPtlc()` → still deploy HTLC but store `ptlcPoint` in `chainState` + emit; `tryExtract` unchanged hash path | `Ethereum/PtlcTimelock.sol` new, `EthChainClient.h/.cpp`, `BscChainClient`, `PolygonChainClient` | EVM swap `PTLC_HTLC_BRIDGE` creates HTLC with `ptlcPoint` logged; claim extracts `t` via hash reveal |
| 1.2 | SOL: extend `htlc_program.rs` with `ClaimPtlc` instruction + `ptlc_point` field; bump program id to `Ptlc`-derived PDA; keep `claim` for HTLC fallback; `SolChainClient` `supportsPtlc()=true` (ed25519) → `negotiate` → `PTLC` for SOL-XFG if both true, else `BRIDGE` | `Solana/htlc_program.rs`, `SolChainClient.h/.cpp` | `cargo test` Anchor program, `lockPtlc` stores point, `claimPtlc` verifies adaptor `s'*G==R+e*P+T` |
| 1.3 | `SwapDaemon` `handleCtrLocked` branching: for `PTLC` call `chainClient.lockPtlc()` else `lock()`; `handleSecretRevealed` branching for PTLC extraction `t=s'-s` vs HTLC `H(t)` | `SwapDaemon.cpp:1666,1905` | State machine `CTR_LOCKED→SECRET_REVEALED` works both paths |
| 1.4 | Tests: `test_swap_ptlc_negotiation.cpp`, `test_swap_ptlc_bridge.cpp` | `tests/` | 3 states × 2 lockTypes × SPV/non-SPV |
| 1.5 | Graphify refresh `graphify update .` | — | Communities for `BtcPtlcScript` appear |

**Confidence:** 80%. **Risk:** Solana program redeploy — gate behind `cargo` build flag.

### Phase 2 — Bitcoin Taproot PTLC (6 days)

| Step | Task | File | Done check |
|------|------|------|------------|
| 2.1 | C++ Schnorr adaptor for secp256k1 in daemon (no Rust): port `fuego-swapd-adaptor/lib.rs:105` `compute_challenge` tagged hash + `adaptor_verify` `s'*G==R+e*P+T` using `crypto/cryptoOps` or `libsecp256k1` | `src/crypto/secp_adaptor.h/.cpp` new | `test_secp_adaptor.cpp` roundtrip `adaptor_sign→verify→extract` passes |
| 2.2 | `BtcPtlcScript.h/.cpp` Taproot `P2TR` creation (`witnessScriptHash` → `TapTweak`), witness stacks `createClaimWitness(adaptedSig)` / `parseClaimAdaptorSecret(rawTx)` extraction `t=s'-s`, `sha256` vs `TaggedHash` separation | `Bitcoin/BtcPtlcScript.*` | `test_btc_ptlc_script.cpp` buildTx→parse→extract |
| 2.3 | `BtcChainClient` override `supportsPtlc()=true`, implement `lockPtlc()`, `verifyLock()`, `claimPtlc()`, `tryExtractClaimedSecret()` PTLC path (parse Schnorr sig not preimage), keep HTLC methods for fallback | `Bitcoin/BtcChainClient.h/.cpp` | `BtcChainClient lockPtlc→verifyLock→claim→extract` e2e on regtest |
| 2.4 | `LtcChainClient` mirrors BTC (hrp `ltc`) | `Litecoin/LtcPtlcScript.*` or reuse | LTC PTLC on same script, new hrp |
| 2.5 | `SwapTimelock.h` add `ptlcTimelockOrderingOk()` same as `timelockOrderingOk` (timeout semantics identical) | `SwapTimelock.h` | Timeout ordering PTLC vs HTLC equal |

**Confidence:** 70% (secp256k1 math + Taproot). **Risk:** libsecp256k1 submodule missing → CMake `USE_VENDORED_SECP256K1=OFF` system fallback.

### Phase 3 — Hardening + XMR/ZANO Label + Privacy Decorrelation (4 days)

| Step | Task | Done check |
|------|------|------------|
| 3.1 | XMR/ZANO `supportsPtlc()=true` + `lockType=PTLC` labeling (no script change) — `hashLock==ZERO` path renamed `isPtlcPair` helper | `XmrChainClient.h/.cpp`, `ZanoChainClient` |
| 3.2 | Per-hop decorrelation for multi-hop (future Lightning-style): `adaptorGenerateTweak(t, hopScalar y_i)` `T_i = T + y_i*G`, `R_{i+1}=R_i` preserved. Add `adaptorTweak(t,y)→T'` helper in `adaptor.h` | Unit test `T'` sum law |
| 3.3 | `fuego-swapd-adaptor` Rust crate: expose `adaptor_sign`/`verify`/`extract` via C ABI for daemon optionally (`extern "C"`). Feature gate `ENABLE_RUST_ADAPTOR`. | `rust-bindgen` CI |
| 3.4 | Wallet RPC `fire_wallet` TUI badge `PTLC` vs `HTLC` + `requirePtlc` toggle in `SwapDaemonConfig` | `Wallet/WalletRpcServer.cpp:371` |
| 3.5 | PriceOracle unchanged; `SwapOfferRelay` `PtlcPrice` gossip for PTLC market stats | `SwapOfferRelay.cpp:238` |

### Phase 4 — EVM Native Schnorr (deferred, 8 days, behind flag)

- Implement `PtlcTimelock.sol` native verifier using `secp256k1` `ecMul` precompile post-EIP-1962 Prague + `isValidSchnorr` inline. Requires `e(P,R,T,m)` tagged hash in Solidity. Deploy to staging `chains-staging/evm_ptlc/`. Not blocking main PTLC release.

**Total Phases 0-3: ~18 days, confidence 75%. Buffer 20% → 22 days.**

---

## 6. Test Plan

| Suite | Cases |
|-------|-------|
| `test_swap_ptlc_negotiation` | 3×3 matrix (cap local × cap remote × requirePtlc) → lockType HTLC/PTLC/BRIDGE/abort |
| `test_swap_ptlc_bridge` | XFG PTLC + EVM HTLC with DLEQ → claim → extract `t=s'-s` vs `H(t)` |
| `test_btc_ptlc_script` | P2TR create → fund → claim witness `adaptedSig` → parse extract |
| `test_secp_adaptor` | sign→verify→extract, wrong T fails, nonce-reuse key-leak test, DLEQ binding test, GHSA c7q2 forged r' test |
| `test_sol_ptlc` | Anchor `lockPtlc` → `claimPtlc` ed25519 adaptor verify, fallback `claim` HTLC |
| `test_spv_ptlc` | `ADAPTOR_WAITING_SPV→SECRET_CONFIRMED_SPV` PTLC branch |
| `test_downgrade_attack` | MITM forces HTLC when `requirePtlc=true` → swap abort, log `downgrade blocked` |
| `test_musig2_adaptor` | `musig2_nonce_agg` includes `T` bound in `b`, half-agg disabled, second `pSign(m,Y)` leak test |
| `integration_swap_e2e` | Full Alice-locks `XFG↔ETH` bridge + `XFG↔BTC` PTLC + `XFG↔SOL` PTLC on regtest/devnet |

---

## 7. Security & 007 Alignment (6-phase)

| Phase | Check |
|-------|-------|
| 1 Attack Surface | P2P `MsgAdaptorExchange` new fields, RPC `requirePtlc`, on-chain scripts (EVM/SOL/BTC P2TR), FFI secp adaptor |
| 2 STRIDE | Spoofing peer T → DLEQ proof mandatory; Tampering wire JSON → Ed25519 `verifyPeerMessage`; InfoDisc `t` never logged; DoS `lockPtlc` size limits; EoP `supportsPtlc` not bypassable |
| 3 Checklist | No hardcoded secrets, `explicit_bzero` adaptorSecret after `adaptor_aggregate` zeros `t:261`, `verify_adaptor_signature` constant-time, `aux_rand` per BIP340, DLEQ `check_dleq_proof` enforced, low-S, `timelockOrderingOk` +3600s |
| 4 Red Team | Malicious RPC downgrade, malicious P2P forged `r'`, compromised swap counterparty abandon → refund path `broadcastEscrowRefundDirect`, supply chain `libsecp256k1`, economic AMM not in scope |
| 5 Blue Team | Architecture: dual code paths isolated, `lockType` enum whitelisted; Guardrails: `supportsPtlc` virtual not config; Sandboxing: adaptor verify before funding; Monitoring: `ptlcPoint` mismatch alert; Audit trail: all `lockType` decisions logged |
| 6 Veredito | Target score 85+ (APPROVED_WITH_RESERVATIONS). Blocking: missing DLEQ, nonce reuse, HTLC downgrade without `requirePtlc` log |

---

## 8. Integration Points

- `graphify update .` after each phase; `fuego-guardian` auto-triggers on `src/crypto/*`, `src/SwapDaemon/*`, `contracts/*`, `fuego-swapd-adaptor/*`.
- `yes-md` 6-layer gates: safety gates (DLEQ, zeroize), evidence-based (parseClaim tests), anti-slack (no `unimplemented!()`), machine hooks (graphify, `quick_scan.py`).
- `adversarial-review`: 7 personas + named persons (Torvalds, Thompson, Carmack, Kent Beck, Cagan, Jobs).

---

## 9. Rollback Plan

- Feature flag `FEATURE_PTLC_ENABLED` CMake `option` default `ON`; runtime `SwapDaemonConfig::enablePtlc` per pair (default `false` staging, `true` prod after audit). If PTLC breaks, flip to `false` → `negotiate` yields `HTLC`/`BRIDGE` only, HTLC path untouched.
- Contract rollback: `PtlcTimelock` not replacing `HashedTimelock`; both deployed, client selects by `lockType`.
- Data rollback: `SwapParams` missing `ptlcPoint` deserializes to `HTLC` (backward compat), no chain rewind.

---

## 10. Open Questions

- EVM native Schnorr opcode availability — deferred to Phase 4.
- `ed25519` adaptor for TON/SIA — similar bridge solves without contract change.
- Billing: keep `SWAP_FEE_RATE_BPS=100` (1%+1% = 2% cross-chain swap) `CryptoNoteConfig.h:148` — PTLC does not change fee split `69/11/20`.

