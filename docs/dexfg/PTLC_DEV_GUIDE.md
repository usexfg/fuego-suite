# Fuego PTLC Developer Guide — Dual PTLC/HTLC with Fallback

> Companion to `PTLC_PLAN.md`. For `fuego-suite` `src/SwapDaemon/`, `src/crypto/`, `contracts/`, `fuego-swapd-adaptor/`.
> Build: CMake 3.16+, C++17, Boost 1.86+, OpenSSL 3.x/4.x, `secp256k1` (vendored or system), Go 1.24+ for TUI, Rust for adaptor crate.
> Chain: CryptoNote (RingCT) + Hearth AMM v12 + HEAT mint. See `AGENTS.md` for bootstrap/fee context.

---

## 1. Quick Start

```bash
# fresh build with PTLC
cmake -B build -DUSE_VENDORED_SECP256K1=OFF -DFEATURE_PTLC_ENABLED=ON
make -j$(nproc)

# swap daemon (enable PTLC per pair)
./build/src/SwapDaemon/swapd --enable-ptlc btc,eth,sol --require-ptlc false

# TUI badge
./tui/swapxfg --show-ptlc-badge

# tests
ctest -R ptlc --output-on-failure
ctest -R swap_ptlc --output-on-failure
./build/tests/test_secp_adaptor --gtest_filter="*adaptor*"
```

`--require-ptlc` enforces abort if counterparty cannot do PTLC (anti-downgrade). Default `false` → fallback to `PTLC_HTLC_BRIDGE` or `HTLC`.

---

## 2. Core Concepts

### PTLC idea in 30s

HTLC lock: `hashLock = H(t)` (`keccak256` for EVM/SOL, `SHA256` for BTC). Claim reveals `t` push.

PTLC lock: `ptlcPoint = T = t*G`. Claim publishes adapted Schnorr sig `s = s' - t` where `s'*G = R + e*P + T`, `e = H(P||R+T||m)` (tagged). Extractor recovers `t = s' - s`.

Why: hop `i` sees `T_i = T + y_i*G` with blinding `y_i`, so no two hops share `H(t)`. Cooperative Taproot spends look like single-sig P2TR (no script reveal).

### Fallback model

- `PTLC`: both legs use point lock + adaptor verify (BTC Taproot, SOL ed25519, XMR/ZANO native).
- `PTLC_HTLC_BRIDGE`: XFG PTLC + counterparty HTLC `H(t)` plus off-chain DLEQ bridge `Q = t*escrowPubKey`, `proof: log_G(T)=log_{escrowPubKey}(Q)`. On claim, hash preimage `t` revealed on HTLC chain, extractor recovers `t` via `s'-s` on XFG side as well. This is today's Fuego flow formalized.
- `HTLC`: pure legacy hashlock (both sides hash). Retained for staging chains.

Negotiation per swap: `negotiateLockType(localCaps, peerCaps, requirePtlc)` → `min(caps)` or abort.

---

## 3. File Map (new / changed)

```
src/SwapDaemon/SwapTypes.h:133          SwapParams {lockType, ptlcPoint, requirePtlc}
src/SwapDaemon/SwapTypes.cpp            swapLockTypeToString()
src/SwapDaemon/SwapPtlcLock.h           NEW: SwapPtlcLock helper + negotiateLockType()
src/SwapDaemon/SwapPtlcLock.cpp         NEW: ptlcPointHex, verifyPtlcPoint, negotiate matrix
src/SwapDaemon/IChainClient.h:10        supportsPtlc(), lockPtlc() shim
src/SwapDaemon/SwapHashLock.h/.cpp      keep sol/bchHashLockHex (HTLC path)
src/SwapDaemon/SwapPeerProtocol.h:91    MsgAdaptorExchange {lockType, ptlcPoint, requirePtlc}
src/SwapDaemon/SwapPeerProtocol.cpp     JSON encode/decode PTLC fields
src/SwapDaemon/ChainRegistry.h/.cpp     registerChain still 27 pairs; no change
src/SwapDaemon/AdaptorSwap.h/.cpp       adaptor_generate_adaptor still produces T+Q+DLEQ+H(t); add adaptorVerifyPtlc()
src/SwapDaemon/SwapDaemon.h/.cpp        negotiateLockType() in handleKeysExchanged; handleCtrLocked branching; handleSecretRevealed PTLC extract t=s'-s
src/SwapDaemon/SwapStateMachine.*       serialize ptlcPoint, lockType (backward compat missing→HTLC)
src/SwapDaemon/Ethereum/PtlcTimelock.sol NEW wrapper (Phase 4 native verifier deferred)
src/SwapDaemon/Ethereum/EthChainClient.h/.cpp  supportsPtlc()=false → BRIDGE; lockPtlc() stores ptlcPoint
src/SwapDaemon/Bitcoin/BtcPtlcScript.h/.cpp NEW Taproot P2TR PTLC script + adaptor extract
src/SwapDaemon/Bitcoin/BtcChainClient.h/.cpp  supportsPtlc()=true, lockPtlc()/tryExtract PTLC branch
src/SwapDaemon/Litecoin/LtcPtlcScript.* or reuse      mirrors BTC hrp ltc
src/SwapDaemon/Solana/htlc_program.rs   add ClaimPtlc instruction + ptlc_point field
src/SwapDaemon/Solana/SolChainClient.*  supportsPtlc()=true, lockPtlc() branch
src/crypto/secp_adaptor.h/.cpp          NEW Schnorr adaptor for secp256k1 (C++ port of lib.rs tagged hashes)
src/crypto/adaptor.h/.cpp               ed25519 adaptor already PTLC-capable; add tweak helper
fuego-swapd-adaptor/src/lib.rs          unchanged, optional C ABI feature
src/CryptoNoteCore/SwapOfferRelay.h     SwapRequestResult {ptlcPoint, lockType}
tests/test_swap_ptlc_*.cpp              new suites
```

---

## 4. Adding a New PTLC-Compatible Chain

1. Implement `IChainClient` subclass in `src/SwapDaemon/<Chain>/`.

```cpp
class FooChainClient : public IChainClient {
  std::string chainName() const override { return "FOO"; }
  bool supportsPtlc() const override { return true; } // or false → BRIDGE

  ChainClientResult lock(const SwapParams& p) override {
    if (p.lockType == SwapLockType::PTLC)
      return lockPtlc(p);
    return lockHtlc(p); // fallback
  }
  ChainClientResult lockPtlc(const SwapParams& p) override {
    // create Foo PTLC script/contract with p.ptlcPoint
    // store hex in params.chainState via FooRpcClient
  }
  ChainClientResult verifyLock(const SwapParams& p) override {
    // fetch on-chain object, check ptlc_point == p.ptlcPoint if PTLC else hashLock
  }
  ChainClientResult claim(const SwapParams& p) override {
    // for PTLC: publish adaptedSig = adaptor_aggregate(presig + t)
    // for HTLC/BRIDGE: publish preimage t
  }
  std::string tryExtractClaimedSecret(const SwapParams& p) override {
    // PTLC: fetch adapted sig on-chain, return t = s' - s (hex)
    // HTLC: fetch preimage push (existing parseClaimPreimage)
  }
};
```

2. Add contract/script `FooPtlcScript.h` (like `BtcPtlcScript`) or extend existing `FooHtlcScript`.
3. Register in `SwapDaemon.cpp:213-433` `registerChain(SwapPair::FOO, FooChainClient)` and add `SwapPair::FOO` in `SwapTypes.h:77` if new pair.
4. Add `supportsPtlc` to capability test `tests/test_production_gates.cpp`.
5. Wire negotiator: `SwapPtlcLock.cpp:negotiateLockType` reads `getClient(pair)->supportsPtlc()` for both sides. No registry change needed — `ChainRegistry::getClient(pair)` already maps.

---

## 5. Negotiation Flow (code)

```cpp
// SwapDaemon.cpp:handleKeysExchanged
SwapLockType negotiated = SwapPtlcLock::negotiateLockType(
    registry.getClient(params.pair)->supportsPtlc(), // local CTR cap
    peerSupportsPtlc,                                 // from MsgAdaptorExchange.lockType / feature bit
    params.requirePtlc
);
if (negotiated == SwapLockType::ABORT) { fail("downgrade blocked"); return; }
params.lockType = negotiated;
params.ptlcPoint = T; // from adaptor_generate_adaptor
// For BRIDGE, also keep hashLock = H(t) for HTLC leg
if (negotiated == SwapLockType::PTLC_HTLC_BRIDGE)
  params.hashLock = hashFromSecret(adaptorSecret); // keccak or sha256 per chain
persist(params);
```

`SwapPtlcLock::negotiateLockType`:

```cpp
SwapLockType negotiate(bool localPtlc, bool peerPtlc, bool requirePtlc) {
  if (localPtlc && peerPtlc) return PTLC;
  if (localPtlc || peerPtlc) return PTLC_HTLC_BRIDGE; // at least XFG side is PTLC
  if (requirePtlc) return ABORT;
  return HTLC;
}
```

XMR/ZANO `supportsPtlc()=true` + `isPtlcPair(pair)` helper forces `PTLC` without hash.

---

## 6. Per-Chain Cookbook

### EVM (ETH/ARB/BASE/BNB/POLY/...)

Phase 1: no new on-chain code. `EthChainClient::lockPtlc` delegates to `lock` (HashedTimelock HTLC) but also stores `ptlcPoint` hex in `chainState` (`ptlcPoint:hashLock:contractId`) and emits `PtlcLocked(ptlcPoint)` via `PtlcTimelock` wrapper (optional). `verifyLock` checks both; `tryExtract` fetches `preimage` via `getContract`/`Claimed` event (hash path). Privacy gain is per-hop `ptlcPoint` in gossip not on-chain.

Phase 4 (deferred): `PtlcTimelock.sol` with `claimPtlc(sig, presigR, T, e)` `require(ecrecover(hash(R,P,T,m), sig) == P_tweaked)`. Not required for PTLC_BRIDGE.

### BTC/LTC Taproot

```cpp
// BtcPtlcScript.cpp
auto redeem = BtcPtlcScript::createPtlcScript(T, timeout, recipientPub, senderPub, internalKey);
auto p2tr = BtcPtlcScript::redeemScriptToP2tr(redeem, internalKey);
auto witness = BtcPtlcScript::createClaimWitness(adaptedSig, controlBlock, redeem);
auto t = BtcPtlcScript::parseClaimAdaptorSecret(rawTx, p2trScriptPubKey, presigHex); // t = s' - s
```

`internalKey` = MuSig2 aggregated `escrowPubKey` or single-sig per design (Boltz `P_tweak = P + TapTweak(P||m_swap)*G`).

### Solana

Anchor `htlc_program.rs`:

```rust
pub struct HtlcState {
  pub ptlc_point: [u8; 32],
  pub lock_type: u8, // 0 HTLC 1 PTLC
  // ... existing fields
}
pub fn claim_ptlc(ctx: Context<ClaimPtlc>, sig: [u8;64], presig_r: [u8;32]) -> Result<()> {
  // verify ed25519 adaptor: s'*G == R + e*P + T
  // e = keccak(R||P||T||msg) truncated or Hs per adaptor.cpp
}
```

`SolChainClient::lockPtlc` stores `ptlc_point`; `tryExtractClaimedSecret` for PTLC reads `claimPtlc` event and returns `t`.

---

## 7. Crypto Details (reuse, don't reinvent)

- **Challenge**: `e = TaggedHash("Fuego/adaptor_challenge", R || P || msg)` (`lib.rs:105`). Must match on both sides; wire `R,P,msg` as compressed 33B + 32B digest.
- **Verify**: `s'*G == R + e*P + T` (`lib.rs:150`). Fail → abort before funding.
- **Extract**: `t = s' - s` `scalar_add(s', negate(s))` (`lib.rs:180`).
- **DLEQ**: `generate_dleq_proof(t, G, escrowPubKey)` → `Q=t*escrowPubKey`; `check_dleq_proof(Q,T)` (`src/crypto/dleq.cpp:76`).
- **Nonce domain**: `presigSessionHash(escrowTxHash || T)` (`AdaptorSwap.h:38`) — include `T` so re-locking same escrow with different `T` uses fresh nonce (anti-reuse).
- **Zeroization**: `explicit_bzero(adaptorSecret)` after `adaptor_aggregate`, `memset_s` for `ourSwapSecKey`. See `adaptor.cpp:215`.

---

## 8. Testing

```bash
# unit
./build/tests/test_swap_ptlc_negotiation
./build/tests/test_secp_adaptor --gtest_filter="*GHSA*"
./build/tests/test_btc_ptlc_script

# e2e bridge (regtest)
python3 tests/swap_e2e_ptlc.py --pair BTC --locktype bridge --regtest
python3 tests/swap_e2e_ptlc.py --pair ETH --locktype bridge
python3 tests/swap_e2e_ptlc.py --pair SOL --locktype ptlc

# downgrade attack
./build/tests/test_downgrade_attack --require-ptlc

# graphify
graphify update .
graphify query "ptlcPoint data flow" --budget 20
```

Add `tests/test_production_gates.cpp` entry: `EXPECT_TRUE(registry.getClient(SwapPair::BTC)->supportsPtlc())`.

---

## 9. Security Checklist (per change)

- [ ] DLEQ proof verified before `ESCROW_FUNDED`
- [ ] `adaptor_verify` constant-time `sc_*`, low-S
- [ ] Nonce never reused across `(msg,T)`
- [ ] `t` range check `t < q_ed25519` and `t < n_secp` (split)
- [ ] `t*G==T && (lockType==BRIDGE ? H(t)==hashLock : true)`
- [ ] `timelockOrderingOk` holds for PTLC same as HTLC (`+3600s`)
- [ ] `requirePtlc` downgrade log + abort
- [ ] `explicit_bzero` after adaptor aggregate
- [ ] `verifyPeerMessage` on all `MsgAdaptorExchange` with new fields
- [ ] No `eval`, `exec`, `pickle` with peer data

---

## 10. Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| `downgrade blocked` abort | Peer `supportsPtlc()=false` but `requirePtlc=true` | Set `--require-ptlc false` or upgrade peer |
| `DLEQ verify failed` | `Q` mismatch or truncated `T` | Re-derive `Q=t*escrowPubKey` with same base `escrowPubKey` not `G` |
| `adaptor verify false` | `e` domain wrong (missing `T` in hash) | Use tagged hash `Fuego/adaptor_challenge` with `R||P||msg`, not `H(P,R,m)` |
| HTLC claim succeeds but PTLC extract returns empty | `parseClaimPreimage` called on PTLC tx (sig stack not preimage) | Branch `tryExtractClaimedSecret` on `lockType` → `parseClaimAdaptorSecret` |
| `P2TR address mismatch` | InternakKey tweak missing `m_swap` | Include `TapTweak(P||serialize(T,timeout))` |
| `nonce reuse key leak` test fails | `presigSessionHash` without `T` | Change to `hash(escrowTxHash || ptlcPoint)` |

---

## 11. References

- `src/crypto/adaptor.h:41` ed25519 adaptor, `musig2.h:42`, `dleq.h:37`
- `src/SwapDaemon/AdaptorSwap.h:23` swap wiring, `IChainClient.h:10` interface
- `src/SwapDaemon/SwapHashLock.h:22` HTLC dispatch, new `SwapPtlcLock.h`
- `contracts/` / `HashedTimelock.sol:6` HTLC, `PtlcTimelock.sol` future native
- `fuego-swapd-adaptor/src/lib.rs:1` secp256k1 Schnorr adaptor (tagged hashes)
- Papers: Poelstra 2017, Malavolta 2018/472, Fournier One-Time VES, Schröder 2024

