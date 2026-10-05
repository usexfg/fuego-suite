# Pure PTLC Plan — Multi-Agent Correctness

> Version 1.0 — 2026-08-23 — Status: DRAFT FOR REVIEW  
> Scope: Move `BRIDGE` → pure `PTLC` `T=t*G` on both legs for `BTC/LTC` Taproot, `SOL` `ClaimPtlc`, `XMR/ZANO` native, `EVM` with `EIP-6601`. No `H(t)` on pure pairs. Multi-agent verification via `fuego-guardian` + `007` + `adversarial-review` + `yes-md`.

---

## 1. Briefing (pre-task intelligence)

**Context collected**

- Existing `BRIDGE` ships: `SwapLockType` `HTLC/BRIDGE/PTLC` `SwapTypes.h:77`, `SwapPtlcLock` negotiate, `SwapPeerProtocol` `lockType/ptlcPoint` wire, `chainState |ptlc:` strip, `secp_adaptor` `TaggedHash Fuego/adaptor_challenge` `src/crypto/secp_adaptor.cpp:70`, `BtcPtlcScript` P2WSH point `src/SwapDaemon/Bitcoin/BtcPtlcScript.h:20`, `PtlcTimelock.sol:1` bridge event, `graphify` 42591 nodes, `007` quick_scan 0 findings, `test_ptlc` 7/7.

**Parallel analysis (3 specialists)**

- `crypto` — secp adaptor `s'=k+e*sk+t` `e=TaggedHash(R,P,msg)` domain separation must include `T` in `R+T` for MuSig2 `musig2.cpp:205` `b=hash(P,T+R1,R2,m)`; EVM `ecrecover` cannot verify Schnorr — need `EIP-6601` `ecMul` or `P256` precompile.
- `consensus` — `timelockOrderingOk` wall-clock `SwapTimelock.cpp:19` unchanged; `serVersion 5` `SwapStateMachine.cpp:65` already `ptlcPoint/lockType`; no chain rewind on pure.
- `swap` — `IChainClient::supportsPtlc()` true for `BTC/LTC` but `SwapDaemon.cpp:797` forces `BRIDGE` Phase1; pure needs `lockPtlc` to fund `P2TR` not `P2WSH` hash, and `tryExtractClaimedSecret` to parse `s'-s` not preimage push.

**Time / confidence**

- Phase P2 BTC/LTC Taproot pure: 6 days, 70% → +20% buffer 7 days.
- Phase P3 SOL ed25519 pure: 4 days, 75% → 5 days.
- Phase P4 EVM native Schnorr: 8 days, 50% (EIP dependent) → 10 days, feature-gated.
- Full pure rollout `BTC+LTC+SOL` = 12 days build + 3 days multi-agent verify = 15 days, 65% confidence.

**Problem mapping**

- Probable: `BtcPtlcScript` P2WSH vs P2TR confusion — resolve by keeping P2WSH for `BRIDGE` and new `BtcTaprootPtlc` for pure, both `verifyLock` strip `|ptlc:`.
- Possible: nonce reuse `x=(s1-s2)/(e1-e2)` if `presigSessionHash` missing `T` — monitor `musig2_nonce_gen` `musig2.cpp:171` synthetic.
- Critical: on-chain Schnorr verify missing on EVM before `EIP-6601` → pure would funds-lock; rollback to `BRIDGE` via `supportsPtlc` flag `false` and `requirePtlc` abort `SwapDaemon.cpp:812` `SwapPeerProtocol.cpp:3505`.

---

## 2. Goal / Non-Goal

- **Goal:** For pairs where **both** `IChainClient::supportsPtlc()==true`, negotiate `PTLC` pure: `T` everywhere, no `H(t)`. Reveal `s` on CTR, extract `t=s'-s` for XFG. Per-hop `T_i` decorrelated, scriptless key-path `P_tweak`.
- **Non-goal:** Remove `BRIDGE` — keep for `EVM` without precompile, `TON/SIA` preimage chains. Do not renumber `SwapPair`.

---

## 3. Architecture — Pure

```
Bob picks t → T=t*G, Q=t*escrowPub, DLEQ proof
Alice+Bob Musig2 agg P
Bob funds XFG→P
Presigs: both Musig2 adaptor s'=k+e*sk+t for commit tx (session H(escrowTxHash||T))
Alice locks CTR point: BTC P2TR(P_tweak) / SOL PDA(ptlc_point) / EVM PtlcTimelockPure(s',R,T)
Bob claims CTR: publish s=s'-t → t extracted s'-s, verified t*G==T
Alice adapts XFG presig → XFG_SPENT
```

- `T` per hop `T_i = T + y_i*G` blinding `y_i` for decorrelation (future route).
- Challenge `e = TaggedHash(Fuego/adaptor_challenge, R, P, msg)` already `secp_adaptor.cpp:70`; for MuSig2 `b=hash(P,T+R1,R2,m)` includes `T`.
- DLEQ base `escrowPubKey` `adaptor.cpp:132`.

---

## 4. Multi-Agent Correctness — `fuego-guardian` (4 layers, 8 specialists)

```
Guardian Supervisor (strategy/routing, 50k token budget, TTL 300s)
  ├─ Layer2 Domain Verifiers (8) → checklist
  │   0 Heat/Hearth — N/A (swap fee routing unchanged 69/11/20)
  │   1 Crypto Verifier — secp adaptor, Musig2, DLEQ, constant-time, zeroize, nonce single-use, ECDH leak separation
  │   2 Consensus Verifier — serVersion 5 compat, timelock wall-clock, no fork
  │   3 P2P Verifier — MsgAdaptorExchange wire compat, |ptlc: strip, downgrade abort
  │   4 Wallet Verifier — no key log, WIF handling, chainState strip in claim/refund
  │   5 Swap Verifier — lockType BRIDGE→PTLC branch, tryExtract s'-s vs preimage, SPV/verifyLock
  │   6 Contracts Verifier — HashedTimelock stays, PtlcTimelockPure reentrancy/gas, Solana ClaimPtlc
  │   7 Quality Reviewer — clean code, SOLID, <50 lines, no magic numbers
  │   8 007 Security Auditor — 6-phase audit
  ├─ Layer3 Adversarial Validator (7 personas: malicious RPC, P2P, swap peer, supply chain, operator error, insider, economic AMM)
  └─ Layer4 Consensus Arbiter (weighted voting, tiebreak 007/security, 2-round debate)
```

**Recursion depth**

- `src/crypto/secp_adaptor.*` change → depth 2 (callers `BtcPtlcScript` `BtcChainClient`).
- `SwapTypes`/`SwapPeerProtocol` → depth 1 (direct callers).
- `PtlcTimelock.sol`/`htlc_program.rs` → depth 1 + Contracts Verifier depth 2 for reentrancy.

**File→domain map** (supervisor routing)

- `src/crypto/secp_adaptor.*` `src/crypto/musig2.*` `dleq.*` → Crypto ×3, Swap ×2, Security ×2
- `src/SwapDaemon/SwapPtlcLock.*` `SwapTypes` `SwapPeerProtocol` → Swap ×3, P2P ×2, Security ×3
- `Bitcoin/BtcPtlc*` `BtcChainClient` → Swap ×3, Security ×2, Contracts ×2
- `Solana/htlc_program.rs` → Swap ×3, Contracts ×3
- `Ethereum/PtlcTimelock*.sol` → Contracts ×3, Security ×2

---

## 5. Per-Chain Pure Spec

### BTC/TolC Taproot P2TR

- **Script:** `BtcTaprootPtlc` new `src/SwapDaemon/Bitcoin/BtcTaprootPtlc.h` (keep `BtcPtlcScript` P2WSH for BRIDGE). `internalKey P`, `tweak = TaggedHash(TapTweak, P || m_swap)` `m_swap = {T, timeout, recipient, sender}` `Boltz 2503.12719`, `output = P_tweak = P + tweak*G` `P2TR` `witnessScriptToAddress hrp bc`.
- **Presig:** `SecpAdaptorPresig R, s'` for claim tx `nVersion 2` `nLockTime timeout` `SIGHASH_ALL` `BIP143` `secp_adaptor_sign(sk,k,t,msg)` with `msg = sighash` `Bip143Sighash.cpp`.
- **Claim:** witness `[schnorrSig s, controlBlock, script]` key-path or script-path `<sig> <control> <script>`. `secp_adaptor_extract(R,s',s) → t` `secp_adaptor.cpp:180` `t = s'-s`.
- **VerifyLock:** `ElectrumSpvClient` fetch `rawTx` + `SpvHeaderStore` Taproot `P2TR` 34-byte `OP_1 PUSH32` check; `BtcRpcClient::verifyLock` address `witnessScriptToAddress` with `bc`.

### SOL ed25519 ClaimPtlc

- `htlc_program.rs` `declare_id!("J4H9…")` keep, add `HtlcState {ptlc_point: [u8;32], lock_type: u8}` `lock_type 0 HTLC 1 PTLC`.
- Instruction `ClaimPtlc { adaptor_sig: [u8;64], presig_r: [u8;32] }` CPI `ed25519_program` verify `s'*G == R + e*P + T` `e = keccak(R,P,T,msg)` matching `adaptor.cpp:131` tagged hash, store `ptlc_point`.
- Client `SolChainClient::lockPtlc` stores `ptlc_point` not `hash_lock`; `verifyLock` checks `ptlc_point` equality; `tryExtractClaimedSecret` for PTLC returns `t = s'-s` via `adaptor_extract_secret` `src/crypto/adaptor.cpp:215`.

### EVM Native (gated `EIP-6601`)

- `PtlcTimelockPure.sol` verify `s'*G == R + e*P + T` via `ecMul` precompile `0x07` + `ecRecover` fallback. `lockWithPoint` stores `ptlcPoint` `hashLock=0`, `claimPtlc(s, R, T, e)` checks `ecMul`. Until activation keep `PtlcTimelock.sol:1` bridge `HashedTimelock + PtlcLocked` event.

---

## 6. Wire / State

- `SwapTypes.h:133` `lockType PTLC` already; pure adds `ptlcPoint` x-only 32 `T`; `SwapStateMachine.cpp:254` `lockType` `ptlcPoint` already `serVersion 5`.
- `SwapPeerProtocol.h:91` `MsgAdaptorExchange` `lockType/ptlcPoint/requirePtlc` already; pure sets `lockType=PTLC` `ptlcPoint=T` `htlcHashLock=0` for XMR/ZANO style; old peers default `HTLC` `SwapPeerProtocol.cpp:138`.
- `chainState` `|ptlc:` suffix already stripped in `BtcChainClient.cpp:132` `claim:160` `refund:220` and `LtcChainClient`.

---

## 7. Phases (pure)

| Phase | What | Files | Done check |
|-------|------|-------|------------|
| P2.1 | `secp_adaptor` EVP finalize (done `secp_adaptor.cpp:70`) + `BtcTaprootPtlc` create tap tweak `TaggedHash TapTweak` | `src/crypto/secp_adaptor.*` `src/SwapDaemon/Bitcoin/BtcTaprootPtlc.*` | `test_secp_adaptor` `verifyPtlcPoint` roundtrip `t*G==T` |
| P2.2 | `BtcChainClient` `lockPtlc` pure `P2TR` `P_tweak`, `verifyLock` `P2TR`, `claim` Schnorr adapted, `tryExtract` `s'-s` | `Bitcoin/BtcChainClient.*` `BtcRpcClient` `Bip143Sighash` | `test_btc_ptlc_p2tr` `buildRawSegWitTx` → parse → extract |
| P2.3 | `LtcChainClient` mirror `hrp ltc` | `Litecoin/LtcTaprootPtlc.*` | same |
| P3.1 | `htlc_program.rs` `ClaimPtlc` ed25519 adaptor verify `ptlc_point` | `Solana/htlc_program.rs` | `cargo test` `lockPtlc`→`claimPtlc` |
| P3.2 | `SolChainClient` `lockPtlc` point, `verifyLock` point | `Solana/SolChainClient.*` | `test_sol_ptlc` |
| P4.1 | `PtlcTimelockPure.sol` `ecMul` verify (gated) | `Ethereum/PtlcTimelockPure.sol` | `forge test` `claimPtlc` |
| P4.2 | Flip `SwapDaemon.cpp:797` `negotiate` `localPtlc true → PTLC` (today `BRIDGE`) behind `FEATURE_PURE_PTLC` flag | `SwapDaemon.cpp` | `test_swap_ptlc` `negotiate` PTLC |

Buffer per phase +20%.

---

## 8. Tests (must pass before merge)

- `test_swap_ptlc` `negotiate` 3×3 matrix `HTLC/BRIDGE/PTLC/abort` `SwapPtlcLock.cpp:22`
- `test_secp_adaptor` `sign→verify→extract` `wrong T` fail `nonce reuse` leak `GHSA-c7q2` forged `r'` without DLEQ → reject
- `test_btc_ptlc_p2tr` `createPtlcScript` `P_tweak` `witnessScriptToAddress` `bc1p…` `claim` `parseClaimAdaptorSecret` `t recovered`
- `test_sol_ptlc` `Anchor` `lockPtlc` `claimPtlc` `ed25519` `T` bound
- `test_spv_ptlc` `WAITING_SPV→CONFIRMED` `PTLC` branch
- `test_downgrade` `requirePtlc true` + `HTLC` peer → abort `downgrade blocked` `SwapDaemon.cpp:3505`
- `integration e2e` `XFG↔BTC P2TR` `XFG↔SOL PTLC` regtest `listSwaps` `lockTypeName PTLC` `flutter test swap_locktype_test.dart` already 6/6

---

## 9. 007 6-Phase Gate (per fuego-guardian)

- **F1 Surface:** P2P `MsgAdaptorExchange` +3 fields, `PtlcTimelockPure` external `ecMul`, `htlc_program` `ClaimPtlc`.
- **F2 STRIDE:** Spoofing `T`→DLEQ mandatory `adaptor_verify_adaptor`; Tampering wire JSON→`verifyPeerMessage` `SwapPeerProtocol.cpp:138`; Info Disclosure `t` never log; DoS `P2TR` size limit; EoP `supportsPtlc` virtual not config.
- **F3 Checklist:** `explicit_bzero` `adaptorSecret` after `adaptor_aggregate` `AdaptorSwap.cpp:261`, `aux_rand` per `BIP340`, low-S, `timelockOrderingOk` `+3600s`.
- **F4 Red Team:** 7 personas — malicious `R`/`s'` malleability `Dai SFExt`, P2P forged `r'` `GHSA`, swap abandon refund `broadcastEscrowRefundDirect` `refund` path, supply chain `secp256k1` `OpenSSL 3.6.3`.
- **F5 Blue:** guard `chainState |ptlc:` strip `BtcChainClient.cpp:132`, `supportsPtlc` not bypassable, monitor `ptlcPoint mismatch` alert.
- **F6 Verdict:** target `90/100` `Approved`. Blocking: missing DLEQ, nonce reuse, `HTLC` downgrade without log.

---

## 10. Deployment / Rollback

- Feature flag `FEATURE_PURE_PTLC` CMake `OFF` default, runtime `SwapDaemonConfig::enablePurePtlc` per pair `false` staging → `true` prod after audit. `negotiate` respects flag: if off, `localPtlc true → BRIDGE` (today), if on `→ PTLC`.
- Contracts not replacing: `HashedTimelock` + `PtlcTimelock` + `PtlcTimelockPure` co-deploy; `EthRpcClient` `chainState` selects.
- Data compat: missing `ptlcPoint` → `HTLC` `SwapStateMachine.cpp:65` `serVersion 5` tolerant.
- Rollback: flip flag `false` → `BRIDGE` only, no chain rewind. `graphify update .` after flip.

---

## 11. Multi-Agent Runbook

```bash
# 1. Guardian supervisor decompose
opencode run fuego-guardian verify --files "$(git diff --name-only HEAD~1)" --depth 2 --domains crypto,swap,contracts

# 2. 007 full audit
python /Users/aejt/.opencode/skills/007/scripts/full_audit.py --target src/SwapDaemon --framework both

# 3. Adversarial (Saboteur/New Hire/Security)
opencode run adversarial-review --diff HEAD~1

# 4. yes-md gates: backup, blast radius, deploy safety, conclusion integrity
# 5. Consensus Arbiter: weighted vote, 007 tiebreak, 2-round debate

# 6. Graph refresh
graphify update . && graphify query "ptlcPoint data flow" --budget 20
```

---

> Pure `PTLC` removes `BRIDGE` `H(t)` for `BTC/LTC/SOL/XMR` pairs, keeping `BRIDGE` only for `EVM` without precompile. Privacy: hop `T_i = T + y_i*G` decorrelated; on-chain `P_tweak` looks like single-sig `P2TR`.

