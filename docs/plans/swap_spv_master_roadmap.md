# Fuego SwapDaemon: SPV + Multi-Chain Expansion — Master Phased Development Guide

> **Status:** Active — living document, updated as phases complete
> **Created:** 2026-07-17
> **Baseline:** `spv_architecture.md`, `swap_expansion_guide.md`, ADR-0001, BCH slice spec/plan

---

## Current State Summary

| Subsystem | Status | Notes |
|-----------|--------|-------|
| SwapDaemon core | **DONE** | State machine (v1 adaptor + v2 AFK), P2P, DB, price oracle, offer manager, status server |
| Adaptor signature protocol | **DONE** | MuSig2 + DLEQ, 8-step lifecycle |
| SOL chain client | **DONE** | Full `IChainClient` + Anchor HTLC program |
| ETH/ARB/BASE chain clients | **DONE** | Full `IChainClient` + Solidity HTLC (EVM reuse via chainName) |
| XMR chain client | **DONE** | Full `IChainClient` + adaptor sig (no on-chain HTLC) |
| BCH chain client | **DONE** (RPC + SPV) | Full `IChainClient` + P2SH HTLC script + SPV light client |
| SPV abstraction layer | **DONE** | `ISpvClient`, `ElectrumSpvClient`, `SpvHeaderStore` — 5 source files, 6 tests |
| BCH SPV proving slice | **DONE** | 15/15 tasks complete, all tests passing |
| BTC/LTC/DCR/KMD chain clients | **PARTIAL** | BTC, LTC, KMD done (SPV-only). DCR not started. |
| BNB chain client | **NOT STARTED** | Binance Smart Chain, EVM clone (ARB/BASE pattern) |
| TON chain client | **PARTIAL** | TonRpcClient + FunC htlc.fc + BOC send; deploy+testnet still ops |
| Neutrino/BIP-157-158 | **PARTIAL** | `NeutrinoSpvClient` present under Spv/Neutrino |
| EVM light client (Helios) | **SEAM ONLY** | `src/Storage/Evm/IEvmLightClient.h` + NullEvmLightClient (no Helios FFI yet) |
| libp2p peer discovery | **NOT STARTED** | Phase 6 — large dependency; not started |
| Polygon chain client | **DONE** | Eth thin client + registry config |
| Sia **swap** client | **PARTIAL** | `SwapDaemon/Sia/*` HTLC/memo path via siad |
| Sia **renterd storage** (DIGM infra) | **PARTIAL (7A+put/get)** | `src/Storage/Sia/*` renterd worker objects + CID + AES option; no full DIGM album flow |
| Electrum TLS | **OPTIONAL** | `ElectrumConnection::connect(host,port,useTls=true)` OpenSSL wrap |

---

## Phase Map

```
PHASE 1 ─── SPV Foundation (BCH Proving Slice) ✅ DONE
  ├─ 1A: SPV Crypto Primitives (no network)
  ├─ 1B: Electrum Client (network I/O)
  ├─ 1C: Result Type + State Machine
  ├─ 1D: BCH Integration
  └─ 1E: Config, Registration, Daemon Wiring

PHASE 2 ─── UTXO Chain Expansion (BTC, LTC, KMD) ✅ DONE
  ├─ 2A: BTC Chain Client (clone BCH, adapt for Bitcoin consensus)
  ├─ 2B: LTC Chain Client (clone BTC, MWEB/Schnorr differences)
  ├─ 2C: KMD Chain Client (already in ADR-0001 amendment)
  └─ 2D: Multi-chain SPV config + Electrum server pools

PHASE 3 ─── EVM Expansion (BNB, Polygon + future Helios seam)
  ├─ 3A: BNB Chain Client (clone ARB/BASE pattern, chainId 56)
  ├─ 3B: Polygon Chain Client (RPC path, clone ETH pattern)
  └─ 3C: Helios EVM Light Client interface (seam, not full impl)

PHASE 4 ─── Privacy Upgrade (Neutrino/BIP-157-158)
  ├─ 4A: NeutrinoSpvClient implementing ISpvClient
  └─ 4B: Per-chain compact block filter headers

PHASE 5 ─── New Protocol Chains (DCR, Zano, TON)
  ├─ 5A: DCR (dcrwallet SPV or dcrd RPC, btcsuite HTLC)
  ├─ 5B: Zano (native HTLC outputs, Monero-derived RPC)
  └─ 5C: TON (TVM smart contract HTLC, Ed25519, async msgs) [PRIORITY]

PHASE 6 ─── Network Overhaul (libp2p)
  ├─ 6A: libp2p integration for peer discovery
  └─ 6B: DHT-based Electrum server discovery

PHASE 7 ─── Sia Decentralized Storage (DIGM Audio + Content)
  ├─ 7A: Sia renterd client (S3-compatible API)
  ├─ 7B: Content upload/retrieve pipeline
  ├─ 7C: Contract renewal automation
  ├─ 7D: Content addressing + CID verification
  └─ 7E: Storage rent setup container folder
```

---

## Phase 1: SPV Foundation — BCH Proving Slice

**Goal:** Build reusable `ISpvClient` foundation, prove end-to-end on BCH via Electrum SPV, with `BchRpcClient` retained as differential-test oracle.

**Design docs:** ADR-0001 (`docs/design/2026-06-04-spv-light-client-integration-adr.md`), BCH slice spec (`docs/superpowers/specs/2026-06-04-spv-bch-slice-design.md`), implementation plan (`docs/superpowers/plans/2026-06-04-spv-bch-slice.md`).

**Tech Stack:** C++14, CryptoNote CMake, `Common::JsonValue`, POSIX sockets, OpenSSL, secp256k1. Tests: `assert()` + `int main()`.

### 1A: SPV Crypto Primitives (no network) — Tasks 1-4

| Task | Deliverable | Files | Status |
|------|------------|-------|--------|
| 1 | `ISpvClient` interface + result structs | `src/SwapDaemon/Spv/ISpvClient.h` | **TODO** |
| 2 | Merkle proof verification | `src/SwapDaemon/Spv/SpvMerkle.{h,cpp}`, `tests/test_spv_merkle.cpp` | **TODO** |
| 3 | Header parse + PoW | `src/SwapDaemon/Spv/SpvHeader.{h,cpp}`, `tests/test_spv_headers.cpp` | **TODO** |
| 4 | Header store (link/checkpoint/reorg) | `src/SwapDaemon/Spv/SpvHeaderStore.{h,cpp}` | **TODO** |

**Security notes:**
- Merkle byte-order: txid display (big-endian) → internal (little-endian) before folding. Branch order determined by `pos & 1`.
- PoW compare: 32-byte big-endian `hash <= target`. Use `long double` work() for chain selection (acceptable precision for near-tip reorgs).
- Checkpoint anchor: reject headers at/below checkpoint height that disagree. Require ancestry from checkpoint.

### 1B: Electrum Client (network I/O) — Tasks 5-9

| Task | Deliverable | Files | Status |
|------|------------|-------|--------|
| 5 | `TestElectrumServer` + `ElectrumConnection` | `Spv/ElectrumConnection.{h,cpp}`, `tests/TestElectrumServer.h` | **TODO** |
| 6 | `syncHeaders` + `getTipHeight` | `Spv/ElectrumSpvClient.{h,cpp}` | **TODO** |
| 7 | `verifyTxInclusion` | (extend ElectrumSpvClient) | **TODO** |
| 8 | `getRawTx` + `findSpend` | (extend ElectrumSpvClient) | **TODO** |
| 9 | Multi-server cross-check | (extend ElectrumSpvClient) | **TODO** |

**Security notes:**
- Electrum scripthash: `reverse(sha256(scriptPubKey))` — single SHA256, not double.
- Eclipse mitigation: require ≥ `minServers` agreement on buried headers. On disagreement → refuse to advance, log eclipse-risk.
- No TLS in this slice (plaintext TCP). First-class TLS = follow-up.
- `TestElectrumServer`: in-process, binds `127.0.0.1:0`, replays canned responses by method name. Named `Test…` per convention.

### 1C: Result Type + State Machine — Tasks 10-11

| Task | Deliverable | Files | Status |
|------|------------|-------|--------|
| 10 | `ChainClientResult` pending fields | `src/SwapDaemon/ChainClientResult.h` | **TODO** |
| 11 | `ADAPTOR_WAITING_SPV_CONFIRMATIONS` state | `SwapTypes.{h,cpp}`, `SwapStateMachine.cpp` | **TODO** |

**State transitions (SPV path only):**
- `PRESIGS_READY → WAITING_SPV_CONFIRMATIONS` (tx seen, depth < minConfs)
- `WAITING_SPV_CONFIRMATIONS → CTR_LOCKED` (depth ≥ minConfs, Merkle-verified, output matches)
- `WAITING_SPV_CONFIRMATIONS → PRESIGS_READY` (reorged out)
- `WAITING_SPV_CONFIRMATIONS → FAILED` (timeout)

### 1D: BCH Integration — Tasks 12-14

| Task | Deliverable | Files | Status |
|------|------------|-------|--------|
| 12 | Hashlock semantic resolution (SPIKE) | Read `BchRpcClient.cpp`, `HtlcScript.cpp` | **TODO** |
| 13 | BCH SPV `verifyLock` | `BitcoinCash/BchChainClient.{h,cpp}`, `HtlcScript.{h,cpp}`, `tests/test_spv_vs_rpc_difftest.cpp` | **TODO** |
| 14 | On-chain secret extraction | `HtlcScript.{h,cpp}`, `BchChainClient.{h,cpp}`, `tests/test_spv_extract_secret.cpp` | **TODO** |

**Critical pre-existing issue (Task 12 gates Task 14):** `BchChainClient::lock` passes `params.adaptorPoint` (T=t·G) as `hashLockSha256Hex`, while `claim` reveals `params.adaptorSecret` (t). SHA-256 HTLC requires `hashlock == SHA256(preimage)`. Resolution: parse committed hashlock from redeem script, verify `SHA256(preimage) == that`.

### 1E: Config, Registration, Daemon Wiring — Tasks 15-18

| Task | Deliverable | Files | Status |
|------|------------|-------|--------|
| 15 | Config fields + parse + validation | `SwapDaemon.h`, `ChainClientConfig.cpp` | **TODO** |
| 16 | BCH registration wiring | `SwapDaemon.cpp` (BCH constructor block) | **TODO** |
| 17 | Driver — WAITING branch + Bob extract poll | `SwapDaemon.cpp` (`handlePreSigsReady`, `handleCtrLocked`) | **TODO** |
| 18 | Full build + slice verification | All targets, all tests | **TODO** |

**Config fields:**
```json
{
  "bch_spv_enabled": true,
  "bch_electrum_servers": ["host1:50001", "host2:50001"],
  "bch_spv_min_confs": 6,
  "bch_spv_min_servers": 2,
  "bch_spv_checkpoint_height": 800000,
  "bch_spv_checkpoint_hash": "<hex>"
}
```

**Validation rule:** if `bch_spv_enabled`, require `servers.size() ≥ min_servers` and non-empty checkpoint. Otherwise fall back to RPC-only.

---

## Phase 2: UTXO Chain Expansion (BTC, LTC, KMD)

**Goal:** Add BTC, LTC, KMD chain clients reusing `ElectrumSpvClient`. Each is config + per-chain script params, not a new full node.

**Prerequisite:** Phase 1 complete (SPV foundation proven on BCH).

### 2A: BTC Chain Client ✅ DONE

| Item | Detail | Status |
|------|--------|--------|
| `BtcChainClient` | Clone `BchChainClient`, adapt for Bitcoin consensus (no `SIGHASH_FORKID`) | **DONE** |
| `BtcHtlcScript` | P2WSH HTLC script. BIP-143 sighash for SegWit. | **DONE** |
| `BtcRpcClient` | Optional RPC fallback for differential testing | **TODO** |
| `ElectrumSpvClient` reuse | Same Electrum protocol, BTC ElectrumX servers | **DONE** (reuse) |
| Config | `btc_spv_enabled`, `btc_electrum_servers[]`, `btc_spv_min_confs`, checkpoint | **TODO** |
| Tests | Known-answer Merkle, differential verifyLock (SPV vs RPC), secret extraction | **DONE** (6 tests) |

### 2B: LTC Chain Client ✅ DONE

| Item | Detail | Status |
|------|--------|--------|
| `LtcChainClient` | Clone `BtcChainClient`. Near-identical to BTC. | **DONE** |
| `LtcHtlcScript` | Same as BTC. MWEB available for future scriptless scripts. | **DONE** |
| Config | `ltc_spv_enabled`, `ltc_electrum_servers[]`, checkpoint | **TODO** |
| Tests | Differential verifyLock, secret extraction | **DONE** (3 tests) |

### 2C: KMD Chain Client ✅ DONE

| Item | Detail | Status |
|------|--------|--------|
| `KmdChainClient` | UTXO, Zcash/Bitcoin fork. Standard HTLC opcodes. | **DONE** |
| Address prefixes | 0x3C P2PKH, 0x55 P2SH, WIF 0xBC (from ADR-0001 amendment) | **DONE** |
| Config | `kmd_spv_enabled`, `kmd_electrum_servers[]`, checkpoint | **TODO** |
| Tests | Differential verifyLock, secret extraction | **DONE** (12 tests) |

### 2D: Multi-Chain SPV Config + Server Pools

| Item | Detail | Status |
|------|--------|--------|
| Per-chain Electrum server config | Each UTXO chain gets its own server list | **TODO** |
| Shared `ElectrumSpvClient` | One client instance per chain, separate TCP connections | **TODO** |
| Cross-chain header stores | Independent `SpvHeaderStore` per chain | **TODO** |

---

## Phase 3: EVM Expansion (BNB, Polygon + future Helios seam)

**Goal:** Add BNB and Polygon via existing EVM RPC pattern. Leave seam for future EVM light client.

### 3A: BNB Chain Client

| Item | Detail | Status |
|------|--------|--------|
| `BnbChainClient` | Clone `EthChainClient` with `chainName="BNB"`, chainId 56 | **TODO** |
| HTLC contract | Deploy same `HashedTimelock.sol` to BNB Chain | **TODO** |
| Gas economics | Adjust fee estimation for BNB's lower gas prices | **TODO** |
| Config | `bnb_rpc_url`, `bnb_chain_id` | **TODO** |
| Tests | Verify existing ETH HTLC works unmodified on BNB | **TODO** |

**Note:** BNB rides the existing EVM RPC path. Largest NFT marketplace volume in Asia, 1M+ daily active users.

### 3B: Polygon Chain Client

| Item | Detail | Status |
|------|--------|--------|
| `PolygonChainClient` | Reuse `EthChainClient` with `chainName="POLYGON"`, chainId 137 | **TODO** |
| HTLC contract | Deploy same `HashedTimelock.sol` to Polygon | **TODO** |
| Gas economics | Adjust fee estimation for Polygon's lower gas prices | **TODO** |
| Config | `polygon_rpc_url`, `polygon_chain_id` | **TODO** |
| Tests | Verify existing ETH HTLC works unmodified on Polygon | **TODO** |

**Note:** Polygon rides the existing EVM RPC path. Low-cost NFT minting, Zora deploys there. Trustless verification (Helios sync-committee) is a separate interface, not `ISpvClient`. Deferred to Phase 3C.

### 3C: Helios EVM Light Client Interface (Seam)

| Item | Detail | Status |
|------|--------|--------|
| `IEvmLightClient` interface | Separate from `ISpvClient`. Sync-committee based. | **DONE** (`src/Storage/Evm/IEvmLightClient.h`) |
| `NullEvmLightClient` | Fail-closed placeholder | **DONE** |
| Helios integration | Rust lib compiled to C-bindings. Verifies Ethereum sync-committee signatures. | **TODO** (research spike / FFI) |

**Scope:** Interface definition + research spike only. Full implementation deferred.

---

## Phase 4: Privacy Upgrade (Neutrino/BIP-157-158)

**Goal:** Replace Electrum's per-query script disclosure with compact block filters.

| Item | Detail | Status |
|------|--------|--------|
| `NeutrinoSpvClient` | Implements `ISpvClient`. Downloads deterministic GCS filters per block. | **TODO** |
| BIP-157 filter headers | Download filter header chain, verify against block headers | **TODO** |
| BIP-158 filter matching | Local filter matching (no server-side script disclosure) | **TODO** |
| Per-chain filter seeds | Chain-specific filter parameters | **TODO** |

**Seam:** `ISpvClient` is shaped so `NeutrinoSpvClient` implements the same interface. No consumer changes needed. This is a drop-in privacy upgrade for all UTXO chains.

---

## Phase 5: New Protocol Chains (DCR, Zano, TON)

### 5A: Decred (DCR)

**Difficulty:** Low-Medium. btcsuite fork, same Bitcoin Script HTLC.

| Item | Detail | Status |
|------|--------|--------|
| `DcrRpcClient` | JSON-RPC to dcrd or dcrwallet (btcsuite interface) | **TODO** |
| `DcrHtlcScript` | Standard OP_SHA256 + OP_CHECKLOCKTIMEVERIFY (same as BCH/DOGE) | **TODO** |
| `DcrChainClient` | Implements `IChainClient` | **TODO** |
| Address prefixes | P2PKH `D` (0x16), P2SH `43`/`41`, WIF `P` (0x22) | **TODO** |
| SPV option | dcrwallet `--spv` mode (discovers peers from seeders, verifies headers) | **TODO** |
| Tests | HTLC lock/claim/refund, differential RPC vs SPV | **TODO** |

**Notes:**
- DCR uses Keccak-256 for address hashing (different from BTC's double-SHA256) — verify P2SH derivation
- DCR has official `atomicswap` CLI tool and DCRDEX (built-in atomic swap support)
- First ever on-chain atomic swap was LTC ↔ DCR (September 2017)

### 5B: Zano

**Difficulty:** Medium. CryptoNote chain with native HTLC outputs.

| Item | Detail | Status |
|------|--------|--------|
| `ZanoRpcClient` | JSON-RPC to zanod/simplewallet (Monero-derived API) | **TODO** |
| `ZanoChainClient` | Implements `IChainClient` using native HTLC outputs | **TODO** |
| Native HTLC | Zano introduces `output to HTLC` type — refund address, redeem address, timelock, hash in protocol | **TODO** |
| Auto-refund | HTLC not redeemed within timelock → coins auto-returned to sender | **TODO** |
| Atomic units | 1 coin = 10^12 (same as Monero) | **TODO** |
| Tests | HTLC lock/claim/auto-refund, cross-chain with BTC | **TODO** |

**Notes:**
- Zano's native HTLC uses same SHA256 hash as Bitcoin P2SH HTLCs — cross-chain compatible
- Wallet: `simplewallet` (not `monero-wallet-rpc`), port 12233
- Daemon: `zanod`, port 11211
- d/v-CLSAG ring signatures, Bulletproofs+ for commitments
- "Ionic Swaps" — Zano's branded atomic swap UX, integrated in desktop wallet

### 5C: TON (The Open Network) — PRIORITY

**Difficulty:** High. Completely different VM, async messaging, Ed25519.

| Item | Detail | Status |
|------|--------|--------|
| `TonRpcClient` | JSON-RPC to TON Center (`toncenter.com/api/v2/jsonRPC`) or self-hosted `ton-http-api` | **TODO** |
| `TonChainClient` | Implements `IChainClient` using TVM smart contract HTLC | **TODO** |
| HTLC contract | Deploy Tact/Tolk HTLC contract on TON (hashlock + timelock in TVM) | **TODO** |
| Address format | User-friendly `EQ...`/`UQ...` (base64-encoded), Ed25519 keys | **TODO** |
| Signing | Ed25519 (not secp256k1) — need separate signing path | **TODO** |
| Async model | Actor-based message passing — lock/claim are contract messages, not RPC calls | **TODO** |
| Tests | HTLC deploy, lock, claim, refund on TON testnet | **TODO** |

**TON HTLC contract (Tact):**
```tact
// SPDX: MIT
contract TonHtlc {
    receive("lock", hash: Int<256>, timeout: Int<64>) { ... }
    receive("claim", preimage: Int<256>) { ... }
    // TVM enforces: SHA256(preimage) == hash, timeout for refund
}
```

**Key differences from UTXO chains:**
- No Bitcoin Script — TVM smart contracts handle HTLC logic
- No UTXO model — account-based, balances stored on contract
- Ed25519 signing (TON native) — Fuego's secp256k1 path won't work directly
- Asynchronous — lock/claim are contract messages with delivery guarantees, not synchronous RPC
- Liteserver config: `https://ton.org/global.config.json`
- SDK: `ton-core` (TypeScript), `tonutils` (Python)

**Reference implementations:**
- `github.com/broxus/ton-atomic-swap` — HTLC contracts for TON ↔ ETH ↔ BTC
- TeleSwap — Bitcoin SPV light client as TON smart contract (on-chain SPV verification)

---

## Phase 6: Network Overhaul (libp2p)

**Goal:** Replace config-supplied Electrum servers with dynamic peer discovery.

**Full plan + developer guide:** [`docs/plans/libp2p-swap-network-rewrite.md`](libp2p-swap-network-rewrite.md)
(phased L0–L6: `ISwapTransport` → dual TCP/libp2p → mDNS/DHT → Electrum hints → CBF relay).

| Item | Detail | Status |
|------|--------|--------|
| Implementation plan / dev guide | `docs/plans/libp2p-swap-network-rewrite.md` | **DONE (plan)** |
| libp2p integration | Peer discovery, DHT for Electrum servers + swap peers | **TODO** (code) |
| Compact block filter relay | Relay BIP-158 filters over libp2p | **TODO** |
| Cross-chain swap coordination | P2P orderbook/discovery without centralized matchmaker | **TODO** |

---

## Dependency Graph

```
Phase 1 (SPV Foundation)
  ├─ 1A (primitives) ────────── no deps
  ├─ 1B (electrum) ─────────── depends on 1A
  ├─ 1C (result + state) ───── independent of 1A-1B (can parallel)
  ├─ 1D (BCH integration) ──── depends on 1A, 1B, 1C
  └─ 1E (wiring) ───────────── depends on 1D

Phase 2 (UTXO Expansion) ───── depends on Phase 1
  ├─ 2A (BTC) ──────────────── depends on 1E
  ├─ 2B (LTC) ──────────────── depends on 2A (clone)
  ├─ 2C (KMD) ──────────────── depends on 1E
  └─ 2D (multi-chain config) ─ depends on 2A, 2B, 2C

Phase 3 (EVM Expansion) ────── independent of Phase 2 (can parallel)
  ├─ 3A (BNB) ──────────────── depends on Phase 1 (EVM pattern exists)
  ├─ 3B (Polygon) ──────────── depends on Phase 1 (EVM pattern exists)
  └─ 3C (Helios seam) ──────── independent research spike

Phase 4 (Neutrino) ─────────── depends on Phase 1 (ISpvClient exists)
Phase 5 (New Protocol) ─────── each chain independent
Phase 6 (libp2p) ───────────── depends on Phase 4 (filter relay)
Phase 7 (Sia Storage) ──────── independent of all swap phases (can parallel anytime)
  ├─ 7A (renterd client) ───── no deps
  ├─ 7B (upload/retrieve) ──── depends on 7A
  ├─ 7C (renewal) ──────────── depends on 7A
  └─ 7D (CID verification) ─── depends on 7B
```

---

## Risk Register

| Risk | Severity | Mitigation |
|------|----------|------------|
| Eclipse attack on SPV | HIGH | Multi-server cross-check (mandatory), configurable min servers |
| Merkle proof forgery | CRITICAL | Verify against our header store root, never server-supplied root |
| Hashlock semantic mismatch (Task 12) | HIGH | Parse committed hashlock from redeem script, verify SHA256 gate |
| DAA recomputation deferred | MEDIUM | Checkpoint anchor + PoW + multi-server bounds attacker. Full DAA = hardening follow-up |
| Header persistence | LOW | In-memory + append-only file. Reload on start. |
| TLS deferred | LOW | Plaintext TCP (local Fulcrum or TLS-terminating proxy) |
| DCR Electrum-incompatibility | MEDIUM | Evaluate dcrwallet SPV or Electrum-compat server before committing |
| TON async delivery guarantees | HIGH | Toncenter API handles delivery; self-hosted ton-http-api for reliability |
| TON Ed25519 signing | MEDIUM | Separate signing path (secp256k1 won't work), use ed25519-dalek |
| TON HTLC contract deployment | MEDIUM | Use proven broxus/ton-atomic-swap patterns, testnet first |
| Zano daemon compatibility | LOW | Monero-derived API, same family as Fuego |
| Sia contract renewal gap | MEDIUM | Auto-renewal watcher with budget cap, expiry notifications |
| Sia host churn | LOW | 30-shard erasure coding tolerates host drops, renterd auto-repairs |

---

## Phase 7: Sia Decentralized Storage — DIGM Audio + Content

**Goal:** Add Sia as the decentralized storage backend for DIGM audio albums, cover art, and other content files. Sia provides censorship-resistant, encrypted, erasure-coded storage with native HTLC support (v2 hardfork June 2025).

**Why Sia:**
- UTXO model with file contracts maps to swap escrow patterns
- v2 hardfork added `OP_SHA256` + `OP_CHECKLOCKTIMEVERIFY` (same primitives as BCH/BTC HTLCs)
- Adaptor signatures proven on Sia (roadie project: Sia ↔ ETH atomic swaps)
- Renter-host contracts with 30-shard erasure coding (10 of 30 to reconstruct)
- S3-compatible API via Renterd — drop-in S3 client, no custom protocol
- ~$1-2/TB/month, bandwidth bundled into contracts

### 7A: Sia renterd client (S3-compatible API)

| Item | Detail | Status |
|------|--------|--------|
| `SiaStorageClient` | renterd worker objects API (`/api/worker/objects/...`) | **DONE** (infra) |
| Auth | renterd API password (Basic) | **DONE** |
| Config | `SiaConfig` JSON: `sia_renterd_url`, `sia_renterd_password`, `sia_bucket`, AES key | **DONE** |
| Encryption | Optional client-side AES-256-CTR when key set | **DONE** |
| Full AWS S3 SigV4 gateway | Optional alternate path | **TODO** |

**Implementation notes:**
- Use renterd's S3-compatible endpoint (`s3.daemon.sia.tech` or self-hosted)
- Standard S3 PutObject/GetObject — no custom protocol needed
- renterd handles file contracts, erasure coding, host selection, repairs

### 7B: Content upload/retrieve pipeline

| Item | Detail | Status |
|------|--------|--------|
| Upload | `putObject` → renterd worker (renterd handles shards/hosts) | **DONE** (infra API) |
| Retrieve | `getObject` + optional CID check | **DONE** (infra API) |
| Metadata | Album art, track listings, DIGM product flow | **TODO** (out of scope here) |
| Verification | SHA-256 CID on put/get | **DONE** |

**Data flow:**
```
User uploads DIGM album
  → Client-side encrypt (AES-256, key held by user)
  → Compute content hash (SHA-256)
  → S3 PutObject to renterd
  → renterd splits into 30 shards, distributes to hosts
  → File contract created on Sia chain
  → Content hash + Sia transaction ID returned to caller
```

### 7C: Contract renewal automation

| Item | Detail | Status |
|------|--------|--------|
| Renewal watcher | Monitor contract expiry, auto-renew before lapse | **TODO** |
| Budget cap | Max SC spent per renewal cycle | **TODO** |
| Host selection | Prefer hosts with good uptime/proof history | **TODO** |
| Expiry notification | Alert before contracts expire | **TODO** |

**Notes:**
- Sia file contracts default to 3-month terms
- renterd handles renewals automatically if funded
- For long-lived DIGM content, set renewal to perpetuity with sufficient SC balance
- Host collateral ensures hosts are incentivized to keep data

### 7D: Content addressing + CID verification

| Item | Detail | Status |
|------|--------|--------|
| CID scheme | SHA-256 content hash as the content identifier | **TODO** |
| Verification | On retrieve, recompute hash and compare to stored CID | **TODO** |
| Immutability | Same CID always resolves to same content | **TODO** |
| Deduplication | Multiple references to same CID = single storage | **TODO** |

### 7E: Storage rent setup container folder

| Item | Detail | Status |
|------|--------|--------|
| Renter setup container | Initial SC funding, contract formation, host selection | **TODO** |
| Onboarding flow | User → SC purchase → form contracts → ready to upload | **TODO** |
| Minimum SC balance | Threshold check before upload, error if insufficient | **TODO** |
| Host selection | Prefer hosts with >99% uptime, good collateral ratio | **TODO** |

**Notes:**
- renterd handles contract formation automatically when SC balance is funded
- Container folder documents the setup flow for new deployments
- Minimum SC balance: ~400 SC (~$4 at current prices) for initial contract formation
- After setup, DIGM upload pipeline (7B) takes over

---

## Open Questions (carried from spec §9)

1. **Hashlock semantics** — resolved procedurally in Task 12: parse committed hashlock from redeem script; `SHA256(preimage) == that`.
2. **DAA recomputation** — deferred; security boundary documented (checkpoint + PoW + multi-server).
3. **TLS** — deferred; plaintext TCP.
4. **`vout=0` assumption** — assumed in Tasks 13/14; confirm against `lockHtlc` output ordering.
5. **Header-store persistence** — in-memory + flat append-only file under daemon data dir.
6. **Test framework** — plain `assert()` + `int main()`, declared in `src/CMakeLists.txt`.

---

## File Map (Phase 1 — complete)

| Action | Path | Responsibility |
|--------|------|----------------|
| Create | `src/SwapDaemon/Spv/ISpvClient.h` | Interface + result structs |
| Create | `src/SwapDaemon/Spv/SpvMerkle.{h,cpp}` | Merkle branch fold + verify |
| Create | `src/SwapDaemon/Spv/SpvHeader.{h,cpp}` | 80-byte header parse, hash, PoW |
| Create | `src/SwapDaemon/Spv/SpvHeaderStore.{h,cpp}` | Header chain validate/persist/reorg |
| Create | `src/SwapDaemon/Spv/ElectrumConnection.{h,cpp}` | TCP + newline-delimited JSON-RPC |
| Create | `src/SwapDaemon/Spv/ElectrumSpvClient.{h,cpp}` | ISpvClient impl; multi-server; orchestration |
| Modify | `src/SwapDaemon/ChainClientResult.h` | `pending`/`confirmations`/`requiredConfirmations` |
| Modify | `src/SwapDaemon/BitcoinCash/BchChainClient.{h,cpp}` | Optional ISpvClient; SPV verifyLock; extractSecret |
| Modify | `src/SwapDaemon/BitcoinCash/HtlcScript.{h,cpp}` | `parseClaimPreimage()`, `redeemScriptToP2shScriptPubKey()` |
| Modify | `src/SwapDaemon/SwapTypes.{h,cpp}` | `ADAPTOR_WAITING_SPV_CONFIRMATIONS` |
| Modify | `src/SwapDaemon/SwapStateMachine.cpp` | isValidTransition for new state |
| Modify | `src/SwapDaemon/SwapDaemon.{h,cpp}` | Config, registration, WAITING branch, Bob extract poll |
| Modify | `src/SwapDaemon/ChainClientConfig.cpp` | `bch_spv_*` config parsing |
| Modify | `src/CMakeLists.txt` | Add Spv/*.cpp + test targets |
| Create | `src/SwapDaemon/tests/test_spv_merkle.cpp` | Known-answer Merkle |
| Create | `src/SwapDaemon/tests/test_spv_headers.cpp` | Header link/PoW/reorg/checkpoint |
| Create | `src/SwapDaemon/tests/test_spv_extract_secret.cpp` | Preimage parse + SHA256 gate |
| Create | `src/SwapDaemon/tests/test_spv_electrum.cpp` | Electrum connection + sync + verify |
| Create | `src/SwapDaemon/tests/test_spv_vs_rpc_difftest.cpp` | Differential SPV vs RPC |
| Create | `src/SwapDaemon/tests/TestElectrumServer.h` | In-process canned Electrum server |
| Create | `src/SwapDaemon/tests/TestBchRpcServer.h` | In-process canned BCH RPC server |

---

## File Map (Phase 7 — Sia Storage)

| Action | Path | Responsibility |
|--------|------|----------------|
| Create | `src/Storage/Sia/SiaStorageClient.h` | S3-compatible client interface for renterd |
| Create | `src/Storage/Sia/SiaStorageClient.cpp` | renterd S3 API implementation (PutObject, GetObject, DeleteObject) |
| Create | `src/Storage/Sia/SiaContractWatcher.h` | File contract renewal monitoring |
| Create | `src/Storage/Sia/SiaContractWatcher.cpp` | Renewal automation with budget cap |
| Create | `src/Storage/Sia/SiaContentHash.h` | SHA-256 content addressing (CID scheme) |
| Create | `src/Storage/Sia/SiaContentHash.cpp` | Hash computation + verification |
| Create | `src/Storage/Sia/SiaConfig.h` | Config struct: renterd URL, API key, contract settings |
| Create | `src/Storage/Sia/SiaConfig.cpp` | JSON config parser for Sia storage |
| Create | `src/Storage/ISiaStorage.h` | Abstract storage interface (testable, swappable) |
| Create | `src/Storage/Sia/SiaRenterSetup.h` | Renter setup container (SC funding, contract formation) |
| Create | `src/Storage/Sia/SiaRenterSetup.cpp` | Onboarding flow: SC purchase → host selection → ready to upload |
| Modify | `src/CMakeLists.txt` | Add Storage/Sia/*.cpp targets |

---

## File Map (Phase 2 — UTXO Expansion)

| Action | Path | Responsibility |
|--------|------|----------------|
| Create | `src/SwapDaemon/Bitcoin/BtcChainClient.{h,cpp}` | Clone BCH, remove SIGHASH_FORKID |
| Create | `src/SwapDaemon/Bitcoin/BtcHtlcScript.{h,cpp}` | P2WSH/P2TR HTLC, BIP-143 sighash |
| Create | `src/SwapDaemon/Litecoin/LtcChainClient.{h,cpp}` | Clone BTC, near-identical |
| Create | `src/SwapDaemon/Litecoin/LtcHtlcScript.{h,cpp}` | Same as BTC, MWEB available |
| Create | `src/SwapDaemon/Komodo/KmdChainClient.{h,cpp}` | UTXO, standard HTLC |
| Create | `src/SwapDaemon/tests/test_spv_btc_difftest.cpp` | Differential SPV vs RPC for BTC |
| Create | `src/SwapDaemon/tests/test_spv_ltc_difftest.cpp` | Differential SPV vs RPC for LTC |

---

## File Map (Phase 3 — EVM Expansion)

| Action | Path | Responsibility |
|--------|------|----------------|
| Create | `src/SwapDaemon/Binance/BnbChainClient.{h,cpp}` | Clone ARB/BASE pattern, chainId 56 |
| Create | `src/SwapDaemon/Polygon/PolygonChainClient.{h,cpp}` | Clone ETH pattern, chainId 137 |
| Create | `src/SwapDaemon/tests/test_htlc_bnb.cpp` | Verify HashedTimelock.sol on BNB |
| Create | `src/SwapDaemon/tests/test_htlc_polygon.cpp` | Verify HashedTimelock.sol on Polygon |

---

## File Map (Phase 5 — New Protocol Chains)

| Action | Path | Responsibility |
|--------|------|----------------|
| Create | `src/SwapDaemon/Decred/DcrChainClient.{h,cpp}` | dcrd/dcrwallet RPC, standard HTLC |
| Create | `src/SwapDaemon/Decred/DcrHtlcScript.{h,cpp}` | OP_SHA256 + CLTV, Keccak-256 addresses |
| Create | `src/SwapDaemon/Zano/ZanoChainClient.{h,cpp}` | Monero-derived RPC, native HTLC outputs |
| Create | `src/SwapDaemon/Ton/TonChainClient.{h,cpp}` | TVM smart contract HTLC, Ed25519 |
| Create | `src/SwapDaemon/Ton/TonHtlcContract.{h,cpp}` | Tact HTLC contract deployment + interaction |
| Create | `src/SwapDaemon/Ton/TonRpcClient.{h,cpp}` | Toncenter JSON-RPC client |
| Create | `src/SwapDaemon/tests/test_spv_dcr_difftest.cpp` | Differential verifyLock for DCR |
| Create | `src/SwapDaemon/tests/test_zano_htlc.cpp` | HTLC lock/claim/auto-refund for Zano |
| Create | `src/SwapDaemon/tests/test_ton_htlc.cpp` | HTLC deploy/lock/claim on TON testnet |
