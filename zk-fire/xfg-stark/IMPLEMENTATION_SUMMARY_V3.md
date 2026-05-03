# XFG-STARK v3 Implementation Summary
## Elderfier Relay Architecture (replaces MVP API backend)

**Date:** 2026-02-15
**Version:** v3.1 (Elderfier consensus relay)

---

## What Changed from MVP

### Old MVP Flow (v3.0)
```
User -> generates STARK proof -> submits to usexfg.org API -> API validates -> API calls L2 contract -> mints tokens
```

**Problem**: Centralized trust in usexfg.org API backend. Single point of failure and compromise.

### New Relay Flow (v3.1)
```
User -> xfg-stark-cli validates burn via Fuego RPC -> generates STARK proof ->
bundles with merkle proof + EFier consensus -> submits to L2 contract ->
contract verifies EFier signatures against on-chain registry -> mints tokens
```

**Key improvement**: No centralized API. The CLI queries any synced Fuego node (localhost or seed node fallback). Elderfier consensus (>=69% of staked validators signing merkle roots) replaces the trusted API backend. L2 contract verifies signatures cryptographically.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    User (Fuego L1)                          │
│                                                             │
│  1. Burns/Deposits XFG on Fuego (0x08/0xCD tx extra tags)  │
│  2. CommitmentIndex indexes -> merkle tree updated          │
│  3. Active Elderfiers sign merkle root via P2P gossip       │
└─────────────────────────────────────────────────────────────┘
                            │
                            │ Fuego daemon RPC
                            │ (localhost:18180 or seed nodes)
                            ▼
┌─────────────────────────────────────────────────────────────┐
│             xfg-stark-cli (Relay Middleman)                 │
│                                                             │
│  4. verify-commitment: checks burn exists on-chain          │
│  5. validate: confirms block height, tx hash format         │
│  6. generate: creates STARK proof locally (Winterfell)      │
│  7. bundle: fetches merkle proof + EFier consensus from     │
│     daemon, packages into CompleteProofPackage               │
└─────────────────────────────────────────────────────────────┘
                            │
                            │ Complete proof bundle (JSON)
                            ▼
┌─────────────────────────────────────────────────────────────┐
│        Target L2 Contract (Arbitrum)                        │
│        FuegoCommitmentMerkleVerifier                        │
│                                                             │
│  HEAT: HEATBurnProofVerifier                               │
│  COLD: COLDProofVerifier                                   │
│                                                             │
│  8. Verifies merkle proof against stored root               │
│  9. Verifies EFier signatures on merkle root using          │
│     on-chain Elderfier Index registry                       │
│  10. Checks nullifier not used (prevent replay)             │
│  11. Sends L2->L1 message via ARB_SYS                       │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│           Ethereum L1 Token Contract                        │
│                                                             │
│  HEAT: EmbersTokenHEAT (mintFromL2)                        │
│  COLD: FuegoCOLDAOToken (mintFromL2)                       │
│                                                             │
│  12. Receives message from Arbitrum Outbox                  │
│  13. Mints tokens to recipient                              │
└─────────────────────────────────────────────────────────────┘
```

---

## xfg-stark-cli Relay Details

### Connection Strategy
The CLI connects to a Fuego daemon via `FuegoRpcClient` (in `src/fuego_rpc.rs`):

1. **User-specified address** (`--daemon host:port`) if provided
2. **Localhost** (`127.0.0.1:18180` mainnet / `127.0.0.1:28280` testnet)
3. **Seed node fallback** (IPs from CryptoNoteConfig.h, probed sequentially)

Commitment data is network-wide state. Any fully-synced node serves it.

### RPC Endpoints Called

| Endpoint                       |          Used By            |                         Data Returned                                                       |
|--------------------------------|-----------------------------|---------------------------------------------------------------------------------------------|
| `/get_height`                  | `daemon-status`, `validate` | Chain height                                                                                |
| `/get_commitment`              | `verify-commitment`         | Full commitment entry (type, amount, block, term, tx hash, target-chain, leaf index)        |
| `/get_commitment_stats`        | `daemon-status`, `bundle`   | Total/heat/cold counts, merkle root, consensus %, signed/pending EFier IDs                  |
| `/get_commitment_merkle_proof` | `fetch-proof`, `bundle`     | Merkle root, leaf hash, proof path (sibling hashes), proof indices, leaf index, consensus % |
| `/check_commitment_exists`     | internal validation         | Boolean existence check                                                                     |

### Bundle Output Structure

The `bundle` command produces a `CompleteProofPackage` JSON containing:

```json
{
  "stark_proof_data": { /* burn tx, recipient, secret, metadata */ },
  "stark_proof": { /* Winterfell proof bytes, public inputs */ },
  "eldernode_verification": {
    "merkle_proof": {
      "root_hash": "...",
      "leaf_hash": "...",
      "proof_path": ["sibling1", "sibling2", ...],
      "proof_indices": [0, 1, 0, ...],
      "leaf_index": 42
    },
    "eldernode_signatures": [
      { "elderfier_id": 0, "signing_pubkey": "ed25519_pubkey_hex_64chars", "signature": "ed25519_sig_hex_128chars", "block_height": 12345, "timestamp": 1739577600 },
      { "elderfier_id": 3, "signing_pubkey": "...", "signature": "...", "block_height": 12345, "timestamp": 1739577601 }
    ],
    "consensus": {
      "eldernode_count": 5,
      "threshold_met": true,
      "consensus_type": "5/8"
    },
    "metadata": {
      "verified_at": "2026-02-15T...",
      "network": "fuego-mainnet",
      "version": "3.0.0"
    }
  },
  "status": "Complete",
  "timestamps": { ... }
}
```

---

## Elderfier Consensus Layer

### How Merkle Root Signing Works

1. **CommitmentIndex** on Fuego L1 maintains an ordered merkle tree of all burn/deposit commitments
2. Each block that contains commitments triggers a merkle root update
3. Active Elderfiers (registered via `elderking_ceremony` with 5x 800 XFG stakes) run the `ElderfierSignatureDaemon`
4. The daemon signs the current merkle root with the EFier's Ed25519 key and broadcasts via P2P gossip
5. Peer nodes cache these signatures in `CommitmentIndex::m_signatures`
6. When >=69% of active EFiers have signed, the root is considered consensus-confirmed

### L2 Contract Verification (Two-Phase)

The L2 `FuegoCommitmentMerkleVerifier` contract maintains:
- A registry of active Elderfier Ed25519 public keys (registered at deployment, updated by owner)
- The latest consensus-finalized merkle root (genesis root seeded in constructor)
- A nullifier map to prevent double-spending (shared across HEAT + COLD)
- An `IEd25519Verifier` reference for signature verification

**Phase 1 — Root finalization (once per root update):**
Anyone calls `submitRoot(root, commitmentCount, highestBlock, efids[], sigs[])`.
Contract verifies each Ed25519 signature against registered pubkeys via `IEd25519Verifier`.
When threshold is met (e.g., 5 of 8 EFiers), root is finalized and stored.
This only happens when new commitments are added to the Fuego merkle tree.

**Phase 2 — Claim (per user, cheap):**
User calls `claimHEAT()` or `claimCD()` on the respective verifier contract.
Contract calls `merkleVerifier.verifyCommitment(commitment, proof, leafIndex)`.
Only checks merkle proof against the already-finalized root — no signature re-verification.
Nullifier is marked used, L2→L1 message mints tokens.

### PQ Hybrid Signatures

Elderfier signatures are forward-compatible with post-quantum cryptography:
- Current: Ed25519 (64-byte signatures)
- Future: ML-DSA-65 (3293-byte signatures)
- Detection: Length-based (backward compatible)
- Compilation: PQ code paths behind `#ifdef FUEGO_PQ_ENABLED`

---

## Tier Conversions Reference

### HEAT Burn Tiers (4 tiers, amount-based, term = FOREVER)

| Tier | XFG Burned | HEAT Minted |
|------|-----------|-------------|
| 0    | 0.8 XFG   | 8M HEAT     |
| 1    | 8 XFG     | 80M HEAT    |
| 2    | 80 XFG    | 800M HEAT   |
| 3    | 800 XFG   | 8B HEAT     |

### COLD Deposit Tiers (8 tiers = 4 amounts × 2 terms, v3 canonical)

Encoding: `tier = (amountIndex * 2) + termIndex`

| Tier | XFG Locked | Lock | APY | CD Interest (atomic, 12 dec) |
|------|-----------|------|-----|------------------------------|
| 0    | 0.8 XFG   | 3mo  | 8%  | 640,000                      |
| 1    | 0.8 XFG   | 12mo | 27% | 2,160,000                    |
| 2    | 8 XFG     | 3mo  | 18% | 14,400,000                   |
| 3    | 8 XFG     | 12mo | 33% | 26,400,000                   |
| 4    | 80 XFG    | 3mo  | 27% | 216,000,000                  |
| 5    | 80 XFG    | 12mo | 42% | 336,000,000                  |
| 6    | 800 XFG   | 3mo  | 33% | 2,640,000,000                |
| 7    | 800 XFG   | 12mo | 69% | 5,520,000,000                |

Legacy (tiers 6-7 only, deposited before 2026-01-01): 80% APY → 6,400,000,000 atomic

---

## Security Model

### Trust Comparison: MVP vs Relay

| Aspect | MVP (usexfg.org API) | Relay (xfg-stark-cli) |
|--------|---------------------|----------------------|
| Proof validation | API validates off-chain | STARK proof verified locally + on L2 contract |
| Commitment check | API queries Fuego RPC | CLI queries any synced node directly |
| Authorization | API is sole authorized caller | EFier signatures provide distributed auth |
| Single point of failure | API server | None (any node, any EFier subset >=69%) |
| Key compromise impact | Full system compromise | Need >=69% of EFier keys (distributed) |
| Censorship resistance | API can censor requests | Any node can serve data, user submits to L2 directly |

### On-Chain Guarantees
- **Nullifier uniqueness**: Prevents double-spending
- **Merkle proof**: Cryptographic proof commitment exists in Fuego tree
- **EFier signatures**: Distributed attestation of merkle root
- **Tier validation**: Only valid tiers accepted

---

## File Summary

### Rust (xfg-stark)

| File | Purpose |
|------|---------|
| `src/fuego_rpc.rs` | Fuego daemon RPC client (auto-connect, seed fallback) |
| `src/bin/xfg-stark-cli.rs` | Interactive + subcommand CLI relay |
| `src/proof_data_schema.rs` | Data package + complete bundle + EFier verification schemas |
| `src/burn_mint_prover.rs` | STARK proof generation (Winterfell) |
| `src/burn_mint_verifier.rs` | STARK proof verification |
| `src/burn_mint_air.rs` | AIR constraint system for burn-to-mint |
| `src/winterfell_integration.rs` | Winterfell framework integration |

### C++ (Fuego daemon)

| File | Purpose |
|------|---------|
| `src/CryptoNoteCore/CommitmentIndex.h/cpp` | Commitment merkle tree, EFier signature cache, consensus tracking |
| `src/CryptoNoteCore/AliasIndex.h/cpp` | On-chain alias registry (EFier + regular aliases) |
| `src/CryptoNoteCore/Blockchain.cpp` | Block processing: indexes commitments, processes aliases |
| `src/Rpc/RpcServer.cpp` | Commitment RPC endpoints queried by CLI |
| `src/CryptoNoteCore/ElderfierSignatureDaemon.cpp` | Signs merkle roots, broadcasts via P2P |
| `src/CryptoNoteCore/ElderfierSignatureBroadcaster.cpp` | P2P relay + signature validation |

### Solidity (L2 contracts)

| File | Purpose |
|------|---------|
| `FuegoCommitmentMerkleVerifier.sol` | EFier Ed25519 pubkey registry, batch root finalization, merkle proof verification, shared nullifier tracking |
| `HEATBurnProofVerifier_v3.sol` | L2 HEAT claim: merkle proof → L2→L1 mint (no API) |
| `COLDProofVerifier_v3.sol` | L2 COLD claim: merkle proof → L2→L1 mint with legacy rate detection |
| `interfaces/IEd25519Verifier.sol` | IEd25519Verifier interface (modular sig verification) |
| `TierConversions.sol` | Shared tier constants for HEAT (4) and COLD (8) tiers |

---

## Deployment Checklist

### Fuego L1 (Already Running)
- [x] CommitmentIndex with merkle tree
- [x] EFier signature caching + consensus tracking
- [x] Commitment RPC endpoints (5 endpoints)
- [x] ElderfierSignatureDaemon
- [x] AliasIndex for Elderfiers + regular aliases
- [ ] P2P signature relay fully tested on mainnet

### xfg-stark-cli (Complete)
- [x] FuegoRpcClient with seed node fallback
- [x] Interactive + subcommand modes
- [x] daemon-status, verify-commitment, fetch-proof, bundle commands
- [x] STARK proof generation via Winterfell (secret derived from 0xD5 tag; displayed by burn_info/cold_info as "STARK Secret:")
- [x] CompleteProofPackage bundling

### L2 Contracts (Architecture Complete)
- [x] FuegoCommitmentMerkleVerifier: Ed25519 pubkey registry, batch root submission, merkle verification
- [x] HEATBurnProofVerifier: merkle proof claim (no API), L2→L1 mint via ARB_SYS
- [x] COLDProofVerifier: merkle proof claim with legacy rate detection, L2→L1 mint via ARB_SYS
- [x] IEd25519Verifier interface (modular — swap Solidity/Stylus/precompile implementation)
- [x] Nullifier tracking shared via FuegoCommitmentMerkleVerifier (authorized verifiers only)
- [ ] Deploy Ed25519Verifier implementation (Solidity lib or Arbitrum Stylus)
- [ ] Deploy to Arbitrum Sepolia (testnet)
- [ ] End-to-end testing
- [ ] Deploy to Arbitrum One (mainnet)

---

**2026 Elderfire Privacy Group**
