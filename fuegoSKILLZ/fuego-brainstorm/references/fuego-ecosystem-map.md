# Fuego Ecosystem Map

Complete reference for brainstorming. Every active component, protocol, constant, and cross-reference in the Fuego codebase.

## Architecture Overview

```
USER INTERFACES
├── Flutter Mobile Wallet (Dart + Rust FFI)
├── swapxfgui (Qt/C++ Desktop DEX)
├── swapxfg (Go/Bubbletea TUI)
├── DIGM Platform (Flutter + React + Electron)
└── simple_wallet (CLI)

BACKEND SERVICES
├── fuego-sdk (Rust/UniFFI → Kotlin/Swift/Dart)
├── SwapDaemon (C++ — adaptor sigs + HTLC + chain clients)
│   ├── Ethereum/ (Solidity HTLC)
│   ├── Solana/ (Anchor program, Rust)
│   ├── Monero/ (adaptor signatures, Ed25519)
│   └── BitcoinCash/ (Bitcoin Script HTLC)
└── MCP Server (TypeScript — AI agent integration)

CORE BLOCKCHAIN (C++ — CryptoNote fork)
├── CryptoNoteCore/ (block validation, tx pool, blockchain state)
├── CryptoNoteProtocol/ (block/tx propagation)
├── P2p/ (peer networking, Dandelion++)
├── Rpc/ (JSON-RPC: explorer, wallet, mining, AMM endpoints)
├── Wallet/ (modern wallet, RPC server, serialization)
├── WalletLegacy/ (legacy compatibility)
├── SimpleWallet/ (CLI wallet)
├── PaymentGate/ (merchant integration)
├── Miner/ (CPU miner, difficulty monitor)
├── crypto/ (Ed25519, MLSAG, Pedersen, MuSig2, ChaCha8)
├── Mnemonics/ (BIP39, 14 languages)
└── CommitmentIndex/ (merkle tree for ZK commitments)

TRANSPORT PRIVACY
├── FuegoI2P/ (I2P anonymous network)
├── FuegoTor/ (Tor integration)
└── FuegoMeshtastic/ (LoRa mesh — experimental stub)

DIGM PLATFORM
├── libfuego_core/ (10 Rust crates)
│   ├── digm-app (main logic)
│   ├── chunk-store (audio blob storage)
│   ├── fuego-audio / Paradio (audio streaming)
│   ├── parapay (PARA token payments)
│   ├── i2p-net / p2p-net (networking)
│   ├── fuego-crypto / fuego-vault / fuego-node
│   └── ffi-bridge (Flutter FFI)
├── flutter_app/ (Flutter UI)
├── frontend/ (React/TypeScript web)
└── renderer/ (Electron desktop)
```

## Core Protocols & Constants

### Currency

| Parameter | Value |
|-----------|-------|
| XFG max supply | 8,000,008,800,008 atomic units (8M8 coins) |
| Atomic units/COIN | 10^7 |
| HEAT target peg | $1.58 USD (Jan 2009 CPI) |
| HEAT mint premium | 3.33% (333 bps) |
| Overcollateralization gate | 1.5x minimum |
| Epoch length | 900 blocks (~5 days) |
| Difficulty target | 480s per block |

### CD System

| Parameter | Value |
|-----------|-------|
| XFG CD minimum | 8 XFG |
| HEAT CD minimum | 0.1 HEAT |
| XFG CD terms | 6, 18, 36, 72 epochs |
| HEAT CD term | Permanent (0xFFFFFFFF) |
| CD yield floor APY | 2% |
| XFG buyback from fees | 20% |
| Loyalty bonus | 150% (2.5× on max-term last 2 epochs) |
| Legacy bond CD share | 50% (debt cap ₲254,250) |

### Fee Distribution

```
Swap fees (1% / 100 bps):
  80% → CD yield pool
  20% → Treasury
    ├── 60% LP incentives
    └── 40% Peg defense

HEARTH swap fees (0.3% / 30 bps):
  100% → LP providers

XFG burn:
  50% → Eternal Flame (permanent burn)
  50% → Treasury
    ├── 60% LP
    └── 40% Peg defense
```

### HEAT Stability Modes

| Mode | Name | Trigger | Mechanism |
|------|------|---------|-----------|
| 0 | CPI-Adjusted | Default | Peg follows US CPI-U monthly (±10%/yr max) |
| 1 | 5:1 Self-Sovereign | XFG ≥ $5.00 | Fixed $1.50-$2.50 band, PI controller |
| 2 | 8:1 Full Float | Default/Best APY | PI-only, basin discovery |

**PI Controller (removed from code, design preserved):**
- PI_KP = 0.08, PI_KI = 0.015
- Base rate: ±50%/yr, Absolute max: ±1000%/yr
- Hill damping: M=200%, N=4
- Basin discovery: 3 epoch bootstrap → 7 epoch observation → 4 stable epochs to lock

### HEARTH AMM

| Parameter | Value |
|-----------|-------|
| Activation height | UPGRADE_HEIGHT_V11 = 1111111 |
| Formula | xfg_reserve × heat_reserve = k |
| Fee | 0.3% (30 bps) → LP |
| Seed | 10,000 XFG / 1,000 HEAT (10:1) |
| Min depth target | 5,000 XFG / 5,000 HEAT |

### Atomic Swaps

**Swap chains:**

| ID | Chain | Protocol |
|----|-------|----------|
| 0 | SOL | Adaptor sigs (Anchor HTLC) |
| 1 | ETH | Adaptor sigs (Solidity HTLC) |
| 2 | XMR | Adaptor sigs (Ed25519) |
| 3 | BCH | HTLC (Bitcoin Script) |
| 4 | ARB | Adaptor sigs |
| 5 | BASE | Adaptor sigs |

**Adaptor Protocol (v1) state machine:**
```
ADAPTOR_KEYS_EXCHANGED (10) → ADAPTOR_ESCROW_FUNDED (11)
→ ADAPTOR_PRESIGS_READY (12) → ADAPTOR_CTR_LOCKED (13)
→ ADAPTOR_SECRET_REVEALED (14) → ADAPTOR_XFG_SPENT (15)
→ ADAPTOR_REFUNDED (16)
```

**AFK Protocol (v2, non-interactive):**
```
AFK_OFFER_LOCKED (100) → AFK_OFFER_ACCEPTED (101)
→ AFK_CLAIMED (102) → AFK_REFUNDED (103)
```

**Fees:** 1% (100 bps) → 80% CD pool, 20% treasury

### Cryptographic Primitives

| Primitive | Use |
|-----------|-----|
| Ed25519 | Key generation, signing, verification, key images |
| MLSAG | Ring signatures (RingCT), mixin 8-32 |
| Pedersen Commitments | Hide tx amounts, range proofs |
| MuSig2 | Aggregated signatures for atomic swaps |
| Adaptor Signatures | DLEQ proofs, COMIT cross-chain protocol |
| ChaCha8 | Deposit secret encryption (ECDH + chacha8) |
| CN-FastHash | PoW hash (memory-hard, 2MB scratchpad) |
| Bulletproofs | Referenced as available |

### Network

| Parameter | Value |
|-----------|-------|
| Target connections | 8 |
| Max incoming | 250 |
| Max inbound/IP | 8 |
| Max packet | 20 MB |
| Handshake interval | 60s |
| Dandelion++ stem stay | 90% (max 10 hops) |
| Swap offer stem stay | 80% (max 5 hops) |
| Embargo timeout | 30s |
| Epoch rotation | 90s |

**P2P commands:** HANDSHAKE(1001), TIMED_SYNC(1002), PING(1003), REQUEST_PEER_ID(1006), SWAP_OFFER(1013), SWAP_CANCEL(1014), SWAP_TRADE(1015)

### Transaction Types & Tags

**Types:** REGULAR(0), COINBASE(1), DEPOSIT(2), SWAP(3)

**Extra tags:**

| Tag | Purpose |
|-----|---------|
| 0x08 | HEAT commitment |
| 0xCD | COLD commitment |
| 0xD5 | Deposit secret (encrypted) |
| 0xF5 | HEAT mint auth |
| 0xF6 | AMM swap auth |
| 0xF7 | LP add auth |
| 0xF8 | LP remove auth |
| 0xF9 | HEAT send auth |
| 0xFA | Order place |
| 0x0F | Order cancel |
| 0xFC | Market buy auth |
| 0xFD | Market sell auth |

**Deposit term markers:**

| Marker | Hex | Purpose |
|--------|-----|---------|
| TERM_REGULAR | 0 | Non-locked |
| HEAT_TERM | 0xFFFFFFFF | HEAT CD (permanent) |
| DEPOSIT_TERM_LP | 0xFFFFFFFD | Hearth LP share |
| DEPOSIT_TERM_POOL_XFG | 0x504F4C58 | AMM pool XFG |
| DEPOSIT_TERM_POOL_HEAT | 0x504F4C48 | AMM pool HEAT |
| DEPOSIT_TERM_SWAP_RECEIVE_XFG | 0x53575258 | HEAT→XFG swap output |

### Wallet

- Address prefix: `fire` (1753191 base58)
- Format: `fire<base58(spend_key + view_key + checksum)>`
- Dual-key: spend key (signing) + view key (auditing)
- Balance: Spendable = Total - Locked (unlock_time > current_height)
- Binaries: SimpleWallet (CLI), fuego-wallet (GUI), IntegratedWallet

**Wallet commands (v11+):** mint_heat, hearth_add, hearth_exit, hearth_xfg, hearth_heat, hearth_info

### Mining

- Algorithm: CN-FastHash (CryptoNight variant, ASIC-resistant)
- Difficulty: Zawy-LWMA1 (v4), window 45 blocks
- Reward: (reward_base - already_generated) / 2^(height/2^20)
- Emission speed factor: 20 (v9)
- Block reward size: 430,080 bytes (420KB)
- Max block size: 8,000,000 bytes

### ZK Proof System

**Stark (xfg-stark-3):**
- Proving: Winterfell STARK
- Trace: 7 registers, 64 steps
- Registers: burn_amount, mint_amount, txn_hash, deposit_term, state, nullifier, commitment

**Commitment (56 bytes preimage):**
```
secret[32] || le64(amount) || le32(network_id) || le32(chain_id) || le32(version) || le32(term)
→ keccak256 → 32-byte commitment
```

**Nullifier (49 bytes preimage):**
```
secret[32] || "nullifier"[9] || le64(amount)
→ keccak256 → 32-byte nullifier
```

**Migration plan:**
- Phase 1 (current): EFier + Merkle verification
- Phase 2: SP1 zkVM checkpoint + Merkle (trustless block verification)
- Phase 3: SP1 only (fully trustless)

**Solidity contracts:** FuegoCommitmentMerkleVerifier.sol, HEATBurnProofVerifier_v3.sol, COLDProofVerifier_v3.sol, TierConversions.sol

### DIGM Platform

Decentralized Independent Groove Marketplace — music platform built on Fuego.

**Rust crates (libfuego_core):**

| Crate | Purpose |
|-------|---------|
| digm-app | Main application logic |
| chunk-store | Audio blob storage |
| fuego-audio / Paradio | Audio streaming |
| parapay | PARA token payments |
| i2p-net | I2P network layer |
| p2p-net | P2P networking |
| fuego-crypto | Cryptographic primitives |
| fuego-vault | Secure vault/key storage |
| fuego-node | Fuego node client |
| ffi-bridge | Flutter FFI bridge |

**Tokens:** PARA (publishing rights), CURA (curation), DIGM (stablecoin — 0.1 HEAT + audio publishing rights)

**UI:** Flutter app, React web frontend, Electron desktop renderer

### SDK

- fuego-sdk-core: BIP39, Ed25519, ChaCha20-Poly1305 keyring, base58, daemon client, wallet, keystore
- fuego-sdk-rpc-client: RPC client
- fuego-sdk: UniFFI bindings (cdylib + staticlib) for Kotlin/Swift/Dart

### MCP Server

TypeScript-based Model Context Protocol server for AI-agent integration. Enables swap/mint automation via AI agents.

### Fork Heights

| Height | Fork |
|--------|------|
| UPGRADE_HEIGHT_V11 = 1111111 | HEATWAVE (HEAT + HEARTH AMM) |
| V12 | Auth tags (0xF5-0xF9) |

### Codebase Stats

- 20,885 graph nodes, 33,869 edges, 2,138 communities
- 1,082 indexed files, 329,578 lines, 4,840 functions
- 53 MCP tools across 16 domains
- Languages: C++, Rust, Go, Dart, TypeScript, Solidity, Python

### Key Documentation Paths

- xfgo/docs/heat/ — HEAT stablecoin economics (8 docs)
- xfgo/docs/zkLP/ — ZK proof for private LP pools (6 design docs)
- xfgo/docs/starkproof-CDs/ — STARK proofs for CDs
- xfgo/docs/digm/ — DIGM platform docs
- xfgo/docs/orderbook/ — Orderbook design
- xfgo/docs/AFKswapXFG/ — AFK swap strategy
