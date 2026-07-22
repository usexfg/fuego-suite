# Fuego SwapDaemon: SPV + Multi-Chain Expansion — Master Phased Development Guide

> **Status:** Active — living document, updated as phases complete
> **Created:** 2026-07-17
> **Updated:** 2026-07-21
> **Baseline:** `spv_architecture.md`, `swap_expansion_guide.md`, ADR-0001, BCH slice spec/plan

---

## Current State Summary

| Subsystem | Status | Notes |
|-----------|--------|-------|
| SwapDaemon core | **DONE** | State machine (v1 adaptor + v2 AFK), P2P, DB, price oracle, offer manager, status server |
| Adaptor signature protocol | **DONE** | MuSig2 + DLEQ, 8-step lifecycle |
| SOL chain client | **DONE** | Full `IChainClient` + Anchor HTLC program |
| ETH chain client | **DONE** | Full `IChainClient` + Solidity HTLC |
| ARB chain client | **DONE** | EVM clone, chainId 42161 |
| BASE chain client | **DONE** | EVM clone, chainId 8453 |
| XMR chain client | **DONE** | Full `IChainClient` + adaptor sig (no on-chain HTLC) |
| BCH chain client | **DONE** (RPC + SPV) | Full `IChainClient` + P2SH HTLC + Electrum SPV |
| BTC chain client | **DONE** (RPC + SPV) | P2WSH HTLC, BIP-143 sighash, Electrum SPV |
| LTC chain client | **DONE** (RPC + SPV) | Clone BTC, near-identical, Electrum SPV |
| KMD chain client | **DONE** (RPC + SPV) | UTXO, standard HTLC, Electrum SPV |
| BSC (BNB) chain client | **DONE** | EVM clone, chainId 56, Solidity HTLC |
| Polygon chain client | **DONE** | EVM clone, chainId 137, Solidity HTLC |
| DCR chain client | **DONE** (RPC + SPV) | Hybrid PoW/PoS, P2SH HTLC, Neutrino SPV (BIP-157/158) |
| Sia chain client | **NOT STARTED** | UTXO + file contracts, v2 HTLC primitives, storage economy |
| SPV abstraction layer | **DONE** | `ISpvClient`, `ElectrumSpvClient`, `NeutrinoSpvClient`, `SpvHeaderStore` |
| Neutrino/BIP-157-158 | **DONE** | GCS filter construction, matching, header sync, tx verification |
| Guardian audit | **DONE** | 20 findings remediated (12 CRITICAL, 10 HIGH, 8 MEDIUM) |
| SIGPIPE hardening | **DONE** | `MSG_NOSIGNAL` on all `send()` calls across 6 RPC clients |
| TON chain client | **NOT STARTED** | TVM smart contracts, Ed25519, async message passing [PRIORITY] |
| Sia chain client | **NOT STARTED** | UTXO + file contracts, v2 HTLC primitives, storage economy [NEXT RELEASE] |
| Zano chain client | **NOT STARTED** | CryptoNote native HTLC outputs, Monero-derived RPC |
| Helios EVM light client | **NOT STARTED** | Separate interface, not `ISpvClient` |
| libp2p peer discovery | **NOT STARTED** | Phase 6 |
| Sia storage (DIGM) | **NOT STARTED** | Decentralized storage for audio albums and content |

---

## What Connecting to Another Network Gives Us (Beyond Atomic Swaps)

**The core insight:** Each chain we connect to is not just a swap pair — it's a **liquidity bridge** that multiplies the value of the entire network.

### 1. Liquidity Multiplier Effect

Every new chain we add creates **N×M swap pairs** where N = existing chains and M = new chain. With 13 chains connected, adding Sia creates 12 new swap pairs (Sia ↔ BTC, Sia ↔ ETH, Sia ↔ SOL, etc.). Each pair drives:
- **Swap fees** → 0.3% per swap → feeds CD yield pool
- **XFG demand** → every cross-chain swap goes through XFG as the bridge asset
- **Ring signature privacy** → more swap participants = larger ring pools = better privacy

### 2. Organic Demand Generation

Different chains have different economies. Connecting to them imports their demand:

| Chain | Organic Demand | What We Get |
|-------|---------------|-------------|
| **Sia** | Storage providers earning SC, users paying for storage | SC miners become XFG miners via swap; storage users discover Fuego |
| **TON** | 900M+ users on Telegram, TON ecosystem | Massive user base for DIGM music distribution |
| **Zano** | Privacy-focused CryptoNote chain | Privacy-conscious users who value Fuego's hidden amounts |
| **BTC/LTC/DCR** | Established stores of value | Conservative users seeking yield via CDs |

### 3. Storage-as-a-Service (Sia Specific)

Sia is unique because it's **both a swap chain AND a storage layer**:

**From the swap (Phase 5D):**
- SC miners can earn XFG via atomic swaps
- Users can pay for Sia storage using XFG (swap SC ↔ XFG)
- File contracts create escrow patterns that map to our HTLC model

**From the storage (Phase 7):**
- DIGM albums stored on Sia (30-shard erasure coding, censorship-resistant)
- Artists upload once, store forever (3-month contracts auto-renew)
- ~$1-2/TB/month vs $23/TB/month on AWS S3
- Content-addressed (SHA-256 CID) for deduplication and verification

**The synergy:** An artist uploads a DIGM album to Sia storage. A fan discovers it, buys it with XFG (atomic swap SC ↔ XFG). The artist earns XFG. The storage provider earns SC. The CD pool earns yield. Everyone wins.

### 4. Network Effects (Metcalfe's Law)

The value of a network is proportional to the square of the number of connected users/nodes. With each chain we add:

```
Network Value ∝ (Connected Chains × Users per Chain)²

13 chains × 1M users = 13M potential connections → 169T value
14 chains × 1M users = 14M potential connections → 196T value (+16%)
```

### 5. Privacy Flywheel

More chains → more swap participants → larger ring signature pools → better privacy → more privacy-conscious users join → more chains want to connect. This is Fuego's core value proposition.

### 6. Reduced Counterparty Risk

Atomic swaps eliminate trusted intermediaries. No exchange custody, no rug pulls, no KYC. This is especially valuable for:
- Privacy chains (XMR, Zano, DCR) where exchanges often delist
- Storage chains (Sia) where users need to pay for services cross-chain
- Emerging chains (TON) where exchange listings are scarce

### 7. CD Yield Pool Growth

Every swap feeds the CD yield pool (0.3% fee). More chains = more swaps = higher yield = more XFG locked in CDs = bigger ring pools = better privacy. This is the **Fuego flywheel**:

```
More Chains → More Swaps → Higher CD Yield → More XFG Locked → Better Privacy → More Users → More Chains
```

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

PHASE 3 ─── EVM Expansion (BNB, Polygon + future Helios seam) ✅ DONE
  ├─ 3A: BSC/BNB Chain Client (clone ARB/BASE pattern, chainId 56)
  └─ 3B: Polygon Chain Client (clone ETH pattern, chainId 137)

PHASE 4 ─── Privacy Upgrade (Neutrino/BIP-157-158) ✅ DONE
  ├─ 4A: NeutrinoSpvClient implementing ISpvClient
  ├─ 4B: Per-chain compact block filter headers
  └─ 4C: DCR SPV integration via Neutrino

PHASE 5 ─── New Protocol Chains (DCR, Zano, TON, Sia)
  ├─ 5A: DCR ✅ DONE (RPC + Neutrino SPV)
  ├─ 5B: Zano (native HTLC outputs, Monero-derived RPC) [NEXT RELEASE]
  ├─ 5C: TON (TVM smart contract HTLC, Ed25519, async msgs) [PRIORITY]
  └─ 5D: Sia (UTXO + file contracts, v2 HTLC primitives) [NEXT RELEASE]

PHASE 6 ─── Network Overhaul (libp2p) [FUTURE]
  ├─ 6A: libp2p integration for peer discovery
  └─ 6B: DHT-based Electrum server discovery

PHASE 7 ─── Sia Decentralized Storage (DIGM Audio + Content) [FUTURE]
  ├─ 7A: Sia renterd client (S3-compatible API)
  ├─ 7B: Content upload/retrieve pipeline
  ├─ 7C: Contract renewal automation
  ├─ 7D: Content addressing + CID verification
  └─ 7E: Storage rent setup container folder
```

---

## Phase 1: SPV Foundation — BCH Proving Slice ✅ DONE

**Goal:** Build reusable `ISpvClient` foundation, prove end-to-end on BCH via Electrum SPV, with `BchRpcClient` retained as differential-test oracle.

**Design docs:** ADR-0001 (`docs/design/2026-06-04-spv-light-client-integration-adr.md`), BCH slice spec (`docs/superpowers/specs/2026-06-04-spv-bch-slice-design.md`), implementation plan (`docs/superpowers/plans/2026-06-04-spv-bch-slice.md`).

**Tech Stack:** C++14, CryptoNote CMake, `Common::JsonValue`, POSIX sockets, OpenSSL, secp256k1. Tests: `assert()` + `int main()`.

### 1A: SPV Crypto Primitives (no network) — Tasks 1-4 ✅ DONE

| Task | Deliverable | Files | Status |
|------|------------|-------|--------|
| 1 | `ISpvClient` interface + result structs | `src/SwapDaemon/Spv/ISpvClient.h` | **DONE** |
| 2 | Merkle proof verification | `src/SwapDaemon/Spv/SpvMerkle.{h,cpp}`, `tests/test_spv_merkle.cpp` | **DONE** |
| 3 | Header parse + PoW | `src/SwapDaemon/Spv/SpvHeader.{h,cpp}`, `tests/test_spv_headers.cpp` | **DONE** |
| 4 | Header store (link/checkpoint/reorg) | `src/SwapDaemon/Spv/SpvHeaderStore.{h,cpp}` | **DONE** |

### 1B: Electrum Client (network I/O) — Tasks 5-9 ✅ DONE

| Task | Deliverable | Files | Status |
|------|------------|-------|--------|
| 5 | `TestElectrumServer` + `ElectrumConnection` | `Spv/ElectrumConnection.{h,cpp}`, `tests/TestElectrumServer.h` | **DONE** |
| 6 | `syncHeaders` + `getTipHeight` | `Spv/ElectrumSpvClient.{h,cpp}` | **DONE** |
| 7 | `verifyTxInclusion` | (extend ElectrumSpvClient) | **DONE** |
| 8 | `getRawTx` + `findSpend` | (extend ElectrumSpvClient) | **DONE** |
| 9 | Multi-server cross-check | (extend ElectrumSpvClient) | **DONE** |

### 1C: Result Type + State Machine — Tasks 10-11 ✅ DONE

| Task | Deliverable | Files | Status |
|------|------------|-------|--------|
| 10 | `ChainClientResult` pending fields | `src/SwapDaemon/ChainClientResult.h` | **DONE** |
| 11 | `ADAPTOR_WAITING_SPV_CONFIRMATIONS` state | `SwapTypes.{h,cpp}`, `SwapStateMachine.cpp` | **DONE** |

### 1D: BCH Integration — Tasks 12-14 ✅ DONE

| Task | Deliverable | Files | Status |
|------|------------|-------|--------|
| 12 | Hashlock semantic resolution (SPIKE) | Read `BchRpcClient.cpp`, `HtlcScript.cpp` | **DONE** |
| 13 | BCH SPV `verifyLock` | `BitcoinCash/BchChainClient.{h,cpp}`, `HtlcScript.{h,cpp}` | **DONE** |
| 14 | On-chain secret extraction | `HtlcScript.{h,cpp}`, `BchChainClient.{h,cpp}` | **DONE** |

### 1E: Config, Registration, Daemon Wiring — Tasks 15-18 ✅ DONE

| Task | Deliverable | Files | Status |
|------|------------|-------|--------|
| 15 | Config fields + parse + validation | `SwapDaemon.h`, `ChainClientConfig.cpp` | **DONE** |
| 16 | BCH registration wiring | `SwapDaemon.cpp` (BCH constructor block) | **DONE** |
| 17 | Driver — WAITING branch + Bob extract poll | `SwapDaemon.cpp` (`handlePreSigsReady`, `handleCtrLocked`) | **DONE** |
| 18 | Full build + slice verification | All targets, all tests | **DONE** |

---

## Phase 2: UTXO Chain Expansion (BTC, LTC, KMD) ✅ DONE

**Goal:** Add BTC, LTC, KMD chain clients reusing `ElectrumSpvClient`. Each is config + per-chain script params, not a new full node.

**Prerequisite:** Phase 1 complete (SPV foundation proven on BCH).

### 2A: BTC Chain Client ✅ DONE

| Item | Detail | Status |
|------|--------|--------|
| `BtcChainClient` | Clone `BchChainClient`, adapt for Bitcoin consensus (no `SIGHASH_FORKID`) | **DONE** |
| `BtcHtlcScript` | P2WSH HTLC script. BIP-143 sighash for SegWit. | **DONE** |
| `BtcRpcClient` | Optional RPC fallback for differential testing | **DONE** |
| `ElectrumSpvClient` reuse | Same Electrum protocol, BTC ElectrumX servers | **DONE** (reuse) |
| Config | `btc_spv_enabled`, `btc_electrum_servers[]`, `btc_spv_min_confs`, checkpoint | **DONE** |
| Tests | Known-answer Merkle, differential verifyLock (SPV vs RPC), secret extraction | **DONE** (6 tests) |

### 2B: LTC Chain Client ✅ DONE

| Item | Detail | Status |
|------|--------|--------|
| `LtcChainClient` | Clone `BtcChainClient`. Near-identical to BTC. | **DONE** |
| `LtcHtlcScript` | Same as BTC. MWEB available for future scriptless scripts. | **DONE** |
| Config | `ltc_spv_enabled`, `ltc_electrum_servers[]`, checkpoint | **DONE** |
| Tests | Differential verifyLock, secret extraction | **DONE** (3 tests) |

### 2C: KMD Chain Client ✅ DONE

| Item | Detail | Status |
|------|--------|--------|
| `KmdChainClient` | UTXO, Zcash/Bitcoin fork. Standard HTLC opcodes. | **DONE** |
| Address prefixes | 0x3C P2PKH, 0x55 P2SH, WIF 0xBC (from ADR-0001 amendment) | **DONE** |
| Config | `kmd_spv_enabled`, `kmd_electrum_servers[]`, checkpoint | **DONE** |
| Tests | Differential verifyLock, secret extraction | **DONE** (12 tests) |

### 2D: Multi-Chain SPV Config + Server Pools ✅ DONE

| Item | Detail | Status |
|------|--------|--------|
| Per-chain Electrum server config | Each UTXO chain gets its own server list | **DONE** |
| Shared `ElectrumSpvClient` | One client instance per chain, separate TCP connections | **DONE** |
| Cross-chain header stores | Independent `SpvHeaderStore` per chain | **DONE** |

---

## Phase 3: EVM Expansion (BNB, Polygon + future Helios seam) ✅ DONE

**Goal:** Add BNB and Polygon via existing EVM RPC pattern. Leave seam for future EVM light client.

### 3A: BSC/BNB Chain Client ✅ DONE

| Item | Detail | Status |
|------|--------|--------|
| `BscChainClient` | Clone `EthChainClient` with `chainName="BNB"`, chainId 56 | **DONE** |
| HTLC contract | Deploy same `HashedTimelock.sol` to BNB Chain | **DONE** |
| Config | `bsc_rpc_host`, `bsc_rpc_port`, `bsc_chain_id` | **DONE** |
| Tests | `test_bsc_chain_client` | **DONE** |

**Note:** BSC rides the existing EVM RPC path. Largest NFT marketplace volume in Asia, 1M+ daily active users.

### 3B: Polygon Chain Client ✅ DONE

| Item | Detail | Status |
|------|--------|--------|
| `PolygonChainClient` | Reuse `EthChainClient` with `chainName="POLYGON"`, chainId 137 | **DONE** |
| HTLC contract | Deploy same `HashedTimelock.sol` to Polygon | **DONE** |
| Config | `polygon_rpc_host`, `polygon_rpc_port`, `polygon_chain_id` | **DONE** |
| Tests | `test_polygon_chain_client` | **DONE** |

### 3C: Helios EVM Light Client Interface (Seam) [FUTURE]

| Item | Detail | Status |
|------|--------|--------|
| `IEvmLightClient` interface | Separate from `ISpvClient`. Sync-committee based. | **TODO** (interface only) |
| Helios integration | Rust lib compiled to C-bindings. Verifies Ethereum sync-committee signatures. | **TODO** (research spike) |

---

## Phase 4: Privacy Upgrade (Neutrino/BIP-157-158) ✅ DONE

**Goal:** Replace Electrum's per-query script disclosure with compact block filters.

| Item | Detail | Status |
|------|--------|--------|
| `NeutrinoSpvClient` | Implements `ISpvClient`. Downloads deterministic GCS filters per block. | **DONE** |
| BIP-157 filter headers | Download filter header chain, verify against block headers | **DONE** |
| BIP-158 filter matching | Local filter matching (no server-side script disclosure) | **DONE** |
| Per-chain filter seeds | Chain-specific filter parameters | **DONE** |

**Seam:** `ISpvClient` is shaped so `NeutrinoSpvClient` implements the same interface. No consumer changes needed. This is a drop-in privacy upgrade for all UTXO chains.

---

## Phase 5: New Protocol Chains (DCR, Zano, TON, Sia)

### 5A: Decred (DCR) ✅ DONE

**Difficulty:** Low-Medium. btcsuite fork, same Bitcoin Script HTLC.

| Item | Detail | Status |
|------|--------|--------|
| `DcrChainClient` | RPC + Neutrino SPV implementation | **DONE** |
| `DcrHtlcScript` | Standard OP_SHA256 + OP_CHECKLOCKTIMEVERIFY | **DONE** |
| Address prefixes | P2PKH `D` (0x16), P2SH `43`/`41`, WIF `P` (0x22) | **DONE** |
| SPV via Neutrino | BIP-157/158 compact block filters | **DONE** |
| Tests | Differential verifyLock (RPC vs SPV), secret extraction | **DONE** (5 tests) |

**Notes:**
- DCR uses Keccak-256 for address hashing (different from BTC's double-SHA256)
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

### 5D: Sia (SC) — Atomic Swap Chain

**Difficulty:** Low-Medium. UTXO model, v2 hardfork added `OP_SHA256` + `OP_CHECKLOCKTIMEVERIFY` — same primitives as BCH/BTC HTLCs.

| Item | Detail | Status |
|------|--------|--------|
| `SiaRpcClient` | JSON-RPC to Sia daemon (`siad`) or renterd | **TODO** |
| `SiaChainClient` | Implements `IChainClient` using UTXO + file contracts | **TODO** |
| `SiaHtlcScript` | `OP_SHA256` + `OP_CHECKLOCKTIMEVERIFY` (v2 hardfork, same as BCH/BTC) | **TODO** |
| Address prefixes | Sia addresses (ed25519.pubkey → 32-byte unlock hash) | **TODO** |
| SPV option | Sia has built-in consensus verification via renter contracts | **TODO** |
| Tests | HTLC lock/claim/refund, differential RPC vs SPV | **TODO** |

**Why Sia as a swap chain (beyond storage):**
- v2 hardfork (June 2025) added `OP_SHA256` + `OP_CHECKLOCKTIMEVERIFY` — exact same HTLC primitives as BCH/BTC
- UTXO model maps directly to our adaptor signature protocol
- Adaptor signatures already proven on Sia (roadie project: Sia ↔ ETH atomic swaps)
- SC is the native currency — miners earn SC, file contracts use SC, HTLCs use SC
- Sia's storage economy creates organic demand: pay for storage in SC, earn SC from hosting

**Key differences from UTXO chains:**
- Sia addresses are ed25519 pubkey → 32-byte unlock hash (not Base58Check)
- File contracts are Sia-specific — `SiaChainClient` needs to handle both UTXO and contract state
- Sia daemon (`siad`) exposes REST API, not JSON-RPC — `SiaRpcClient` wraps REST endpoints
- Block rewards include both miner fees and storage fees — fee structure is unique

**Reference implementations:**
- `github.com/broxus/ton-atomic-swap` — HTLC contracts for TON ↔ ETH ↔ BTC
- Sia atomic swap research: `https://sia.com/blog/atomic-swaps`
- Roadie project: Sia ↔ ETH atomic swaps via adaptor signatures

---

## Phase 6: Network Overhaul (libp2p)

**Goal:** Replace config-supplied Electrum servers with dynamic peer discovery.

| Item | Detail | Status |
|------|--------|--------|
| libp2p integration | Peer discovery, DHT for Electrum servers + swap peers | **TODO** |
| Compact block filter relay | Relay BIP-158 filters over libp2p | **TODO** |
| Cross-chain swap coordination | P2P orderbook/discovery without centralized matchmaker | **TODO** |

---

## Dependency Graph

```
Phase 1 (SPV Foundation) ✅ DONE
  ├─ 1A (primitives) ✅ DONE
  ├─ 1B (electrum) ✅ DONE
  ├─ 1C (result + state) ✅ DONE
  ├─ 1D (BCH integration) ✅ DONE
  └─ 1E (wiring) ✅ DONE

Phase 2 (UTXO Expansion) ✅ DONE
  ├─ 2A (BTC) ✅ DONE
  ├─ 2B (LTC) ✅ DONE
  ├─ 2C (KMD) ✅ DONE
  └─ 2D (multi-chain config) ✅ DONE

Phase 3 (EVM Expansion) ✅ DONE
  ├─ 3A (BNB) ✅ DONE
  ├─ 3B (Polygon) ✅ DONE
  └─ 3C (Helios seam) ──────── independent research spike [FUTURE]

Phase 4 (Neutrino) ✅ DONE ─────── depends on Phase 1 (ISpvClient exists)
Phase 5 (New Protocol)
  ├─ 5A (DCR) ✅ DONE
  ├─ 5B (Zano) ────────────── independent, Monero-derived RPC [NEXT RELEASE]
  ├─ 5C (TON) ─────────────── independent, TVM smart contract [PRIORITY]
  └─ 5D (Sia swap chain) ──── independent, UTXO + file contracts [NEXT RELEASE]
Phase 6 (libp2p) ───────────── depends on Phase 4 (filter relay) [FUTURE]
Phase 7 (Sia Storage) ──────── depends on Phase 5D (Sia chain client exists)
  ├─ 7A (renterd client) ───── depends on 5D
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
| DCR Electrum-incompatibility | RESOLVED | Used Neutrino (BIP-157/158) instead — native SPV, no Electrum dependency |
| TON async delivery guarantees | HIGH | Toncenter API handles delivery; self-hosted ton-http-api for reliability |
| TON Ed25519 signing | MEDIUM | Separate signing path (secp256k1 won't work), use ed25519-dalek |
| TON HTLC contract deployment | MEDIUM | Use proven broxus/ton-atomic-swap patterns, testnet first |
| Zano daemon compatibility | LOW | Monero-derived API, same family as Fuego |
| Sia contract renewal gap | MEDIUM | Auto-renewal watcher with budget cap, expiry notifications |
| Sia host churn | LOW | 30-shard erasure coding tolerates host drops, renterd auto-repairs |
| Sia scripting limitations | MEDIUM | v2 hardfork added OP_SHA256 + OP_CLTV — verify HTLC support before committing |
| Sia REST API (not JSON-RPC) | LOW | SiaRpcClient wraps REST endpoints, standard HTTP client |
| Sia address format (ed25519) | LOW | 32-byte unlock hash, not Base58Check — need separate address handling |

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
| `SiaStorageClient` | S3-compatible client talking to local/remote renterd | **TODO** |
| Auth | Web3 identity auth or renterd API key | **TODO** |
| Config | `sia_renterd_url`, `sia_api_key`, `sia_renter_contracts` | **TODO** |
| Encryption | Client-side AES-256 before upload (Sia encrypts at rest too) | **TODO** |

**Implementation notes:**
- Use renterd's S3-compatible endpoint (`s3.daemon.sia.tech` or self-hosted)
- Standard S3 PutObject/GetObject — no custom protocol needed
- renterd handles file contracts, erasure coding, host selection, repairs

### 7B: Content upload/retrieve pipeline

| Item | Detail | Status |
|------|--------|--------|
| Upload | Audio files → renterd S3 API → 30-shard erasure coding → hosts | **TODO** |
| Retrieve | CID/content hash → renterd → reassemble from shards | **TODO** |
| Metadata | Album art, track listings, DIGM metadata stored alongside audio | **TODO** |
| Verification | Content hash check on retrieve (SHA-256 of file) | **TODO** |

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

1. **Hashlock semantics** — RESOLVED. Parsed committed hashlock from redeem script; `SHA256(preimage) == that`. Full implementation in `HtlcScript.{h,cpp}` and `BchChainClient.{h,cpp}`.
2. **DAA recomputation** — DEFERRED; security boundary documented (checkpoint + PoW + multi-server).
3. **TLS** — DEFERRED; plaintext TCP.
4. **`vout=0` assumption** — assumed in Tasks 13/14; confirm against `lockHtlc` output ordering. NOTED.
5. **Header-store persistence** — RESOLVED. In-memory + flat append-only file under daemon data dir. Implemented in `SpvHeaderStore.{h,cpp}`.
6. **Test framework** — RESOLVED. Plain `assert()` + `int main()`, declared in `src/CMakeLists.txt`.
7. **DCR Electrum compatibility** — RESOLVED. Used Neutrino (BIP-157/158) instead — native SPV, no Electrum dependency.
8. **Sia scripting limitations** — INVESTIGATION REQUIRED. v2 hardfork added `OP_SHA256` + `OP_CHECKLOCKTIMEVERIFY`, but verify that Sia's scripting supports the minimum HTLC requirements (hash lock + time lock) before committing to Phase 5D.

---

## File Map (Phase 1 — SPV Foundation) ✅ DONE

| Action | Path | Responsibility |
|--------|------|----------------|
| **DONE** | `src/SwapDaemon/Spv/ISpvClient.h` | Interface + result structs |
| **DONE** | `src/SwapDaemon/Spv/SpvMerkle.{h,cpp}` | Merkle branch fold + verify |
| **DONE** | `src/SwapDaemon/Spv/SpvHeader.{h,cpp}` | 80-byte header parse, hash, PoW |
| **DONE** | `src/SwapDaemon/Spv/SpvHeaderStore.{h,cpp}` | Header chain validate/persist/reorg |
| **DONE** | `src/SwapDaemon/Spv/ElectrumConnection.{h,cpp}` | TCP + newline-delimited JSON-RPC |
| **DONE** | `src/SwapDaemon/Spv/ElectrumSpvClient.{h,cpp}` | ISpvClient impl; multi-server; orchestration |
| **DONE** | `src/SwapDaemon/ChainClientResult.h` | `pending`/`confirmations`/`requiredConfirmations` |
| **DONE** | `src/SwapDaemon/BitcoinCash/BchChainClient.{h,cpp}` | Optional ISpvClient; SPV verifyLock; extractSecret |
| **DONE** | `src/SwapDaemon/BitcoinCash/HtlcScript.{h,cpp}` | `parseClaimPreimage()`, `redeemScriptToP2shScriptPubKey()` |
| **DONE** | `src/SwapDaemon/SwapTypes.{h,cpp}` | `ADAPTOR_WAITING_SPV_CONFIRMATIONS` |
| **DONE** | `src/SwapDaemon/SwapStateMachine.cpp` | isValidTransition for new state |
| **DONE** | `src/SwapDaemon/SwapDaemon.{h,cpp}` | Config, registration, WAITING branch, Bob extract poll |
| **DONE** | `src/SwapDaemon/ChainClientConfig.cpp` | `bch_spv_*` config parsing |
| **DONE** | `src/CMakeLists.txt` | Add Spv/*.cpp + test targets |
| **DONE** | `src/SwapDaemon/tests/test_spv_merkle.cpp` | Known-answer Merkle |
| **DONE** | `src/SwapDaemon/tests/test_spv_headers.cpp` | Header link/PoW/reorg/checkpoint |
| **DONE** | `src/SwapDaemon/tests/test_spv_extract_secret.cpp` | Preimage parse + SHA256 gate |
| **DONE** | `src/SwapDaemon/tests/test_spv_electrum.cpp` | Electrum connection + sync + verify |
| **DONE** | `src/SwapDaemon/tests/test_spv_vs_rpc_difftest.cpp` | Differential SPV vs RPC |
| **DONE** | `src/SwapDaemon/tests/TestElectrumServer.h` | In-process canned Electrum server |
| **DONE** | `src/SwapDaemon/tests/TestBchRpcServer.h` | In-process canned BCH RPC server |

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

## File Map (Phase 2 — UTXO Expansion) ✅ DONE

| Action | Path | Responsibility |
|--------|------|----------------|
| **DONE** | `src/SwapDaemon/Bitcoin/BtcChainClient.{h,cpp}` | Clone BCH, remove SIGHASH_FORKID |
| **DONE** | `src/SwapDaemon/Bitcoin/BtcHtlcScript.{h,cpp}` | P2WSH/P2TR HTLC, BIP-143 sighash |
| **DONE** | `src/SwapDaemon/Litecoin/LtcChainClient.{h,cpp}` | Clone BTC, near-identical |
| **DONE** | `src/SwapDaemon/Litecoin/LtcHtlcScript.{h,cpp}` | Same as BTC, MWEB available |
| **DONE** | `src/SwapDaemon/Komodo/KmdChainClient.{h,cpp}` | UTXO, standard HTLC |
| **DONE** | `src/SwapDaemon/tests/test_spv_btc_difftest.cpp` | Differential SPV vs RPC for BTC |
| **DONE** | `src/SwapDaemon/tests/test_spv_ltc_difftest.cpp` | Differential SPV vs RPC for LTC |

---

## File Map (Phase 3 — EVM Expansion) ✅ DONE

| Action | Path | Responsibility |
|--------|------|----------------|
| **DONE** | `src/SwapDaemon/Binance/BnbChainClient.{h,cpp}` | Clone ARB/BASE pattern, chainId 56 |
| **DONE** | `src/SwapDaemon/Polygon/PolygonChainClient.{h,cpp}` | Clone ETH pattern, chainId 137 |
| **DONE** | `src/SwapDaemon/tests/test_htlc_bnb.cpp` | Verify HashedTimelock.sol on BNB |
| **DONE** | `src/SwapDaemon/tests/test_htlc_polygon.cpp` | Verify HashedTimelock.sol on Polygon |

---

## File Map (Phase 5 — New Protocol Chains)

| Action | Path | Responsibility |
|--------|------|----------------|
| **DONE** | `src/SwapDaemon/Decred/DcrChainClient.{h,cpp}` | RPC + Neutrino SPV, standard HTLC |
| **DONE** | `src/SwapDaemon/Decred/DcrHtlcScript.{h,cpp}` | OP_SHA256 + CLTV, Keccak-256 addresses |
| Create | `src/SwapDaemon/Zano/ZanoChainClient.{h,cpp}` | Monero-derived RPC, native HTLC outputs |
| Create | `src/SwapDaemon/Ton/TonChainClient.{h,cpp}` | TVM smart contract HTLC, Ed25519 |
| Create | `src/SwapDaemon/Ton/TonHtlcContract.{h,cpp}` | Tact HTLC contract deployment + interaction |
| Create | `src/SwapDaemon/Ton/TonRpcClient.{h,cpp}` | Toncenter JSON-RPC client |
| **DONE** | `src/SwapDaemon/tests/test_spv_dcr_difftest.cpp` | Differential verifyLock for DCR |
| Create | `src/SwapDaemon/tests/test_zano_htlc.cpp` | HTLC lock/claim/auto-refund for Zano |
| Create | `src/SwapDaemon/tests/test_ton_htlc.cpp` | HTLC deploy/lock/claim on TON testnet |
| Create | `src/SwapDaemon/Sia/SiaChainClient.{h,cpp}` | UTXO + file contracts, v2 HTLC primitives |
| Create | `src/SwapDaemon/Sia/SiaHtlcScript.{h,cpp}` | OP_SHA256 + CLTV, ed25519 addresses |
| Create | `src/SwapDaemon/Sia/SiaRpcClient.{h,cpp}` | REST API wrapper (siad/renterd) |
| Create | `src/SwapDaemon/tests/test_sia_htlc.cpp` | HTLC lock/claim/refund for Sia |
| Create | `src/SwapDaemon/tests/test_sia_difftest.cpp` | Differential verifyLock for Sia |
