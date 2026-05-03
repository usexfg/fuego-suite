# XFG Winterfell Integration (XFGWIN) Documentation

## Overview
XFGWIN is a complete STARK proof system implementation for cross-chain operations between the Fuego blockchain (XFG tokens) and target blockchains (HEAT tokens). This system enables users to burn XFG tokens on Fuego and mint equivalent HEAT tokens on other blockchains through zero-knowledge proofs, verified by the Elderfier consensus layer.

## Project Status
- **Core STARK System: COMPLETE**
- **Fuego Daemon RPC Integration: COMPLETE**
- **xfg-stark-cli Relay: COMPLETE**
- **Elderfier Merkle Consensus: COMPLETE**
- **L2 Contract Verification: IN PROGRESS**

## Architecture

The system uses `xfg-stark-cli` as a middleman relay between the Fuego L1 chain and the target L2 chain. The CLI validates burn transactions by querying Fuego daemon RPC (locally or via seed node fallback), fetches commitment merkle proofs, bundles them with STARK proofs, and produces a complete package for L2 contract submission. On the L2 side, the Elderfier Index of active registered Elderfiers verifies the merkle root signatures.

```
                        FUEGO L1 CHAIN
 ┌───────────────────────────────────────────────────────────┐
 │                                                           │
 │  User burns XFG (0x08 tag) or deposits COLD (0xCD tag)   │
 │          |                                                │
 │          v                                                │
 │  CommitmentIndex indexes burn → merkle tree updated       │
 │          |                                                │
 │          v                                                │
 │  Elderfiers sign merkle root (Ed25519 / ML-DSA hybrid)   │
 │  Consensus: >=69% of active EFiers must sign              │
 │                                                           │
 └──────────────────────┬────────────────────────────────────┘
                        │ RPC (localhost:18180 or seed nodes)
                        │
            ┌───────────v───────────┐
            │    xfg-stark-cli      │
            │    (Relay Middleman)  │
            │                       │
            │  1. Verify commitment │
            │     exists on-chain   │
            │     via daemon RPC    │
            │                       │
            │  2. Fetch merkle      │
            │     proof + consensus │
            │     from daemon       │
            │                       │
            │  3. Generate STARK    │
            │     proof locally     │
            │     (Winterfell)      │
            │                       │
            │  4. Bundle:           │
            │     STARK proof       │
            │     + merkle proof    │
            │     + EFier sigs      │
            │     + consensus data  │
            └───────────┬───────────┘
                        │ Complete proof bundle (JSON)
                        │
            ┌───────────v───────────────────────────────────┐
            │           TARGET L2 CHAIN (Arbitrum)          │
            │                                               │
            │  FuegoCommitmentMerkleVerifier contract:      │
            │                                               │
            │  1. Verify merkle proof against stored root   │
            │  2. Verify EFier signatures on merkle root    │
            │     using on-chain Elderfier Index registry    │
            │  3. Check nullifier not used (no double-spend)│
            │  4. Mint HEAT/COLD tokens to recipient        │
            │                                               │
            └───────────────────────────────────────────────┘
```

### Key Design Decisions

**No centralized API backend.** The old MVP relied on `usexfg.org` as a trusted intermediary to validate proofs and call contracts. The new architecture eliminates this single point of trust:

- **xfg-stark-cli** queries any fully-synced Fuego node directly (commitment data is network-wide state)
- **Elderfier consensus** provides distributed validation (>=69% of active staked validators must sign each merkle root)
- **L2 contract** verifies signatures against the on-chain Elderfier registry (no API trust required)

**Seed node fallback.** Users without a local daemon can still generate valid proof bundles. The CLI auto-connects: `localhost` -> seed nodes (IPs from CryptoNoteConfig.h).

## Documentation

### 1. End-to-End User Guide
**File**: `docs/XFG_STARK_PROOF_USER_GUIDE.md`

Covers the full burn-to-mint flow using xfg-stark-cli.

### 2. Architecture Summary
**File**: `IMPLEMENTATION_SUMMARY_V3.md`

Detailed architecture, tier conversions, and deployment checklist.

### 3. Ed25519 Signature Verification
**File**: `ED25519_SIGNATURE_VERIFICATION.md`

Details on Elderfier signature verification (Ed25519 + future ML-DSA hybrid).

## Quick Start

### Prerequisites
- Rust 1.70+ installed
- Cargo package manager
- A running Fuego daemon (optional; CLI falls back to seed nodes)

### Interactive CLI Flow
```bash
# Build the CLI
cargo build --bin xfg-stark-cli

# Launch interactive mode (auto-connects to daemon)
./target/release/xfg-stark-cli interactive

# Or with explicit daemon address
./target/release/xfg-stark-cli --daemon 192.168.1.5:18180 interactive

# Or testnet mode
./target/release/xfg-stark-cli --testnet interactive
```

### Step-by-Step Proof Generation
```bash
# 1. Check daemon connection + commitment index stats
daemon-status

# 2. Create a data package from your burn transaction
create-package <txn_hash_64hex> <0x_eth_address> package.json

# 3. Validate the package (checks format + live blockchain verification)
validate package.json

# 4. Verify your commitment exists on-chain
verify-commitment <commitment_hash>

# 5. Generate STARK proof
generate package.json proof.json

# 6. Bundle everything (fetches merkle proof + consensus from daemon)
bundle package.json proof.json <commitment_hash> bundle.json

# 7. Submit bundle.json to FuegoCommitmentMerkleVerifier on target chain
```

## Key Components

### 1. Fuego RPC Client (`src/fuego_rpc.rs`)
- Blocking HTTP client for Fuego daemon JSON-RPC
- Auto-connect: localhost -> seed nodes fallback
- Endpoints: `/get_commitment`, `/get_commitment_stats`, `/get_commitment_merkle_root`, `/get_commitment_merkle_proof`, `/check_commitment_exists`
- Configurable for mainnet (port 18180) or testnet (port 28280)

### 2. STARK Proof System (`src/types/stark.rs`)
- Core proof structure and generation via Winterfell
- Merkle tree commitments
- FRI proof implementation

### 3. Burn-Mint AIR (`src/burn_mint_air.rs`)
- Algebraic Intermediate Representation for burn-to-mint proofs
- Constraint system: proves burn amount, recipient, and commitment knowledge

### 4. CLI Relay (`src/bin/xfg-stark-cli.rs`)
- Interactive and subcommand modes
- Daemon RPC commands: `daemon-status`, `verify-commitment`, `fetch-proof`, `bundle`
- Proof commands: `create-package`, `validate`, `generate`
- Bundles STARK proof + merkle proof + Elderfier consensus into complete package

### 5. Proof Data Schema (`src/proof_data_schema.rs`)
- `StarkProofDataPackage`: burn transaction data + recipient + secret
- `CompleteProofPackage`: full bundle with STARK proof + merkle proof + Elderfier verification
- `EldernodeVerification`: merkle proof + EFier signatures + consensus info

## Elderfier Consensus Integration

The Fuego CommitmentIndex maintains a merkle tree of all burn/deposit commitments. Active Elderfiers (staked validators with 5x 800 XFG deposits) sign each merkle root update:

1. **On Fuego L1**: Elderfiers run `ElderfierSignatureDaemon`, which signs the current merkle root and broadcasts via P2P
2. **CommitmentIndex** caches signatures and tracks consensus percentage
3. **xfg-stark-cli** queries this data via RPC (`/get_commitment_stats`, `/get_commitment_merkle_proof`)
4. **L2 contract** verifies EFier signatures against the on-chain Elderfier registry

Consensus threshold: **>=69%** of active Elderfiers must have signed the merkle root for the proof bundle to be accepted.

### Elderfier Registration + Alias System

Elderfiers register via the `elderking_ceremony` in `fire_wallet`:
- Stakes 5 deposits of 800 XFG each (0xEF tag)
- Chooses an 8-character alias `[A-Z0-9&]` during ceremony
- Alias is auto-registered on-chain (AliasIndex) when the 5th deposit confirms
- Alias is voided if the Elderfier unstakes

## RPC Endpoints Used by CLI

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/get_height` | POST | Current blockchain height |
| `/get_commitment` | POST | Look up commitment by hash |
| `/get_commitment_stats` | POST | Index stats: total, heat, cold, merkle root, consensus %, signed/pending EFiers |
| `/get_commitment_merkle_root` | POST | Current merkle root + metadata |
| `/get_commitment_merkle_proof` | POST | Merkle proof path for a commitment |
| `/check_commitment_exists` | POST | Boolean existence check |

## Tier Conversions

| Tier | XFG Amount | HEAT Amount | COLD Base |
|------|------------|-------------|-----------|
| 0    | 0.8 XFG    | 8M HEAT     | 0.000008  |
| 1    | 8 XFG      | 80M HEAT    | 0.00008   |
| 2    | 80 XFG     | 800M HEAT   | 0.0008    |
| 3    | 800 XFG    | 8B HEAT     | 0.008     |

## Security Model

### Trust Assumptions
- **Fuego L1**: Commitment merkle tree is maintained by the blockchain consensus
- **Elderfier consensus**: >=69% of staked validators must sign each merkle root
- **xfg-stark-cli**: Stateless relay; fetches data from any synced node (no trust required in the CLI itself)
- **L2 contract**: Verifies EFier signatures cryptographically against on-chain registry

### On-Chain Guarantees
- **Nullifier uniqueness**: Prevents double-spending on L2
- **Merkle proof verification**: Proves commitment exists in the Fuego merkle tree
- **Elderfier signature verification**: Proves the merkle root was attested by staked validators
- **Tier validation**: Only valid burn amounts accepted

### PQ Readiness
- Elderfier signatures support hybrid Ed25519 + ML-DSA-65 (post-quantum)
- Length-based detection: 64 bytes = Ed25519, 3293 bytes = ML-DSA-65
- PQ paths compiled behind `#ifdef FUEGO_PQ_ENABLED`

## Development

### Building
```bash
cargo build
cargo test
cargo bench
```

### Running Tests
```bash
# All tests
cargo test

# Specific modules
cargo test burn_mint_air
cargo test proof
cargo test winterfell_integration
cargo test fuego_rpc
```

## Support and Resources

### Documentation
- [User Guide](XFG_STARK_PROOF_USER_GUIDE.md)
- [Tier Reference](../COLD_TIER_REFERENCE.md)
- [Winterfell Framework](https://github.com/facebook/winterfell)

### Testing
- [Integration Tests](../tests/)

---

**2026 Elderfire Privacy Group**
