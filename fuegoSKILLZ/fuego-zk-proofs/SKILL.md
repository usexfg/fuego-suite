---
name: fuego-zk-proofs
description: "Fuego domain expert for ZK proofs, Stark/Winterfell proving, merkle proofs, HEAT burn claiming, verifier contract architecture, and on-chain proof verification."
risk: medium
source: user-provided
---

# Fuego ZK Proofs Expert

Domain expert for Fuego ZK proof systems: Stark proofs, Winterfell, merkle verification, and Solidity verifier contracts.

## Scope

- **Stark Proofs**: Winterfell STARK system in xfg-stark-3 (prover, verifier, AIR constraints)
- **Merkle Proofs**: Fuego CommitmentIndex merkle trees, leaf proofs, root finalization
- **Burn & Mint**: HEAT burn proofs, commitment hashing, nullifier computation
- **Verifier Contracts**: Solidity verifiers (FuegoCommitmentMerkleVerifier, HEATBurnProofVerifier)
- **Bundle Architecture**: Stark + Merkle proof bundles, CompleteProofPackage schema
- **Claim Flow**: Off-chain proving → on-chain verification → HEAT minting
- **Winterfell Integration**: AIR constraints, trace generation, public inputs, proof serialization

## Trigger Set

**Should trigger on:**
- "zk proof", "stark proof", "winterfell", "burn proof"
- "merkle proof", "commitment proof", "verify burn"
- "HEAT claim", "claim HEAT", "burn and mint"
- "verifier contract", "solidity verifier", "on-chain verification"
- "nullifier", "commitment hash", "stark bundle"
- "xfg-stark-3", "proof package", "complete proof"
- "FuegoCommitmentMerkleVerifier", "HEATBurnProofVerifier"

**Should NOT trigger on:**
- Non-Fuego ZK systems (zk-SNarks, other blockchains)
- Generic Solidity questions unrelated to Fuego verifiers
- Generic Rust/Winterfell questions not related to Fuego

## Key Components

### Stark Proof System (xfg-stark-3/)

**Location:** `/Users/aejt/fuego_WS/xfg-stark-3/`

| File | Purpose |
|------|---------|
| `src/burn_mint_air.rs` | AIR constraints for burn/mint (v3 unified format) |
| `src/burn_mint_prover.rs` | STARK proof generation via Winterfell |
| `src/burn_mint_verifier.rs` | STARK proof verification |
| `src/proof_data_schema.rs` | CompleteProofPackage, StarkProof, MerkleProof structs |
| `src/bin/xfg-stark-cli.rs` | CLI tool: verify-commitment, generate, bundle commands |
| `src/fuego_rpc.rs` | Fuego daemon RPC client for commitment queries |

**Public Inputs (BurnMintPublicInputs):**
- `burn_amount` (u32) - XFG atomic units burned
- `mint_amount` (u32) - HEAT atomic units (1:1 ratio)
- `txn_hash` (u32) - First 4 bytes of on-chain tx hash
- `state` (u32) - Execution state (0=init, 1=burn, 2=mint, 3=complete)
- `network_id` (u32) - Fuego network ID (1=mainnet, 2=testnet)
- `target_chain_id` (u32) - Target chain (1=ETH, 42161=ARB)
- `commitment_version` (u32) - v3 unified format
- `deposit_term` (u32) - FOREVER for HEAT, actual blocks for COLD

**Witness (Private):**
- `secret` (32 bytes) - User's burn secret
- Used to compute commitment and nullifier inside AIR

### Commitment & Nullifier (v3 Format)

**Commitment (56 bytes preimage):**
```
secret[32] || le64(amount) || le32(network_id) || le32(chain_id) || le32(version) || le32(term)
```
- Hash: `keccak256(preimage)` → 32-byte commitment
- Stored in tx_extra as 0x08 tag (HEAT) or 0xCD tag (COLD)

**Nullifier (49 bytes preimage):**
```
secret[32] || "nullifier"[9] || le64(amount)
```
- Hash: `keccak256(preimage)` → 32-byte nullifier
- Prevents double-spending (stored in verifier contract)

**AIR Computation (burn_mint_air.rs):**
- `compute_commitment(&secret)` → reg6 (trace register 6)
- `compute_nullifier(&secret)` → reg5 (trace register 5)
- Boundary constraints assert these at step 0
- Transition constraints ensure consistency across trace

### Merkle Proof System

**Fuego CommitmentIndex:**
- Maintains ordered merkle tree of all burn/deposit commitments
- Root updated when new commitments are added to blocks
- `FuegoCommitmentMerkleVerifier.sol` manages roots + EFier signatures

**MerkleProof Structure:**
```solidity
struct MerkleProof {
    bytes32 root_hash;           // Finalized merkle root
    bytes32 leaf_hash;           // Commitment hash (leaf)
    bytes32[] proof_path;        // Sibling hashes leaf→root
    uint256[] proof_indices;     // Left(0)/right(1) at each level
    uint256 leaf_index;          // Position in tree
}
```

**Verification:**
- Contract verifies merkle proof against finalized root
- Cheap: O(log N) keccak256 operations
- Root finalization requires EFier consensus (≥69% signatures)

### Verifier Contracts

**FuegoCommitmentMerkleVerifier.sol:**
- EFier Ed25519 pubkey registry
- Batch root submission with signature verification
- Merkle proof verification against finalized roots
- Shared nullifier tracking (HEAT + COLD)
- `verifyCommitment(commitment, proof, leafIndex)` → bool

**HEATBurnProofVerifier_v3.sol:**
- Current: Merkle proof + EFier consensus → mints HEAT
- NO on-chain Stark verification (trusts off-chain proof)
- L2→L1 message via ARB_SYS for minting

**Target Architecture (Merkle + Stark Bundle):**
```
User (off-chain):
  1. Generate Stark proof (xfg-stark-cli prove_burn_mint)
  2. Fetch merkle proof (get_commitment_merkle_proof RPC)
  3. Bundle: StarkProof + MerkleProof + nullifier
  4. Submit to L2 verifier contract

L2 Contract (HEATStarkMerkleVerifier):
  1. Verify Stark proof (Winterfell verifier)
  2. Verify merkle proof against on-chain root
  3. Check nullifier not used
  4. Mint HEAT to recipient
```

## Burn Claim Flow

### Current Flow (with EFier)

```
Fuego L1                    xfg-stark-cli              L2 Contract
  │                           │                        │
  ├─ Burn XFG ────────────────┤                        │
  │  (0x08 tag,               │                        │
  │   commitment in tx_extra)  │                        │
  │                           │                        │
  ├─ CommitmentIndex ─────────┤                        │
  │  (merkle root updated)    │                        │
  │                           │                        │
  │                           ├─ verify-commitment ────────┤
  │                           │  (RPC: get_commitment)  │
  │                           │                        │
  │                           ├─ generate ──────────────┤
  │                           │  (Stark proof created)    │
  │                           │                        │
  │                           ├─ bundle ────────────────┤
  │                           │  (Stark + Merkle)        │
  │                           │                        │
  │                           ├─ submit to contract ──────►
  │                           │                        │  verify merkle proof
  │                           │                        │  verify EFier sigs
  │                           │                        │  mark nullifier used
  │                           │                        │  L2→L1 mint HEAT
```

### Target Flow (Merkle + Stark Bundle)

```
Fuego L1                    xfg-stark-cli              L2 Contract
  │                           │                        │
  ├─ Burn XFG ────────────────┤                        │
  │                           │                        │
  ├─ CommitmentIndex ─────────┤                        │
  │                           │                        │
  │                           ├─ generate stark ───────┤
  │                           │  (Winterfell proof)       │
  │                           │                        │
  │                           ├─ fetch merkle ─────────┤
  │                           │  (RPC: get_merkle_proof) │
  │                           │                        │
  │                           ├─ bundle ────────────────┤
  │                           │  (Stark + Merkle + null) │
  │                           │                        │
  │                           ├─ submit ────────────────►
  │                           │                        │
  │                           │                        │  verify_stark_proof()
  │                           │                        │  verify_merkle_proof()
  │                           │                        │  check_nullifier()
  │                           │                        │  mint HEAT
```

## Stark Proof Architecture

### AIR Constraints (burn_mint_air.rs)

**Execution Trace (7 registers, 64 steps):**
- reg0: burn_amount (constant)
- reg1: mint_amount (constant, = burn_amount)
- reg2: txn_hash (constant, binds to on-chain tx)
- reg3: deposit_term (constant, FOREVER for HEAT)
- reg4: state (0→1→2→3)
- reg5: nullifier (computed from secret)
- reg6: commitment (computed from secret)

**Transition Constraints:**
1. Burn amount validation (reg0 constant)
2. Mint proportionality (reg1 = reg0)
3. Txn hash consistency (reg2 constant)
4. Deposit term consistency (reg3 constant)
5. Nullifier consistency (reg5 computed correctly)
6. Commitment consistency (reg6 computed correctly)
7. State machine (init→burn→mint→complete)

**Boundary Constraints (step 0):**
- state = 0 (init)
- nullifier = compute_nullifier(secret)
- commitment = compute_commitment(secret)

### Prover (burn_mint_prover.rs)

```rust
pub fn prove_burn_mint(
    &self,
    burn_amount: u64,
    mint_amount: u64,
    txn_hash: u32,
    secret: &[u8],           // 32 bytes
    network_id: u32,
    target_chain_id: u32,
    commitment_version: u32,
    deposit_term: u32,       // FOREVER for HEAT
) -> Result<StarkProof>
```

**Process:**
1. Validate inputs (valid tiers, mint=burn)
2. Convert secret to field element
3. Build public inputs via `make_public_inputs()`
4. Create AIR with secret
5. Build execution trace (64 steps)
6. Generate STARK proof via `air.prove(trace)`

### Verifier (burn_mint_verifier.rs)

```rust
pub fn verify_burn_mint(
    &self,
    proof: &StarkProof,
    burn_amount: u64,
    mint_amount: u64,
    txn_hash: u32,
    network_id: u32,
    target_chain_id: u32,
    commitment_version: u32,
    deposit_term: u32,
) -> Result<bool>
```

**Process:**
1. Reconstruct public inputs
2. Call `verify_with_winterfell(proof, &public_inputs)`
3. Winterfell verifies AIR constraints + FRI proof
4. Returns Ok(true) if valid

## CompleteProofPackage Schema

**Location:** `src/proof_data_schema.rs`

```rust
struct CompleteProofPackage {
    stark_proof_data: StarkProofDataPackage,
    stark_proof: Option<StarkProof>,
    eldernode_verification: Option<ElderfierVerification>,
    status: PackageStatus,
    timestamps: ProofTimestamps,
}

struct StarkProof {
    proof_data: Vec<u8>,           // Winterfell proof bytes
    public_inputs: StarkPublicInputs,
    metadata: ProofMetadata,
}

struct StarkPublicInputs {
    burn_amount: u64,
    mint_amount: u64,
    txn_hash: String,           // Hex
    state: u32,
    deposit_term: u32,
    network_id: u32,
    target_chain_id: u32,
    commitment_version: u32,
    // NOTE: nullifier not in public inputs (computed in AIR)
}

struct MerkleProof {
    root_hash: String,              // Hex (64 chars)
    leaf_hash: String,              // Hex (64 chars) = commitment
    proof_path: Vec<String>,       // Sibling hashes
    proof_indices: Vec<u32>,      // Left/right flags
    leaf_index: u32,
}
```

**CLI Bundle Command:**
```bash
xfg-stark-cli bundle \
  --tx-hash <hash> \
  --secret <hex> \
  --network-id 1 \
  --target-chain 1 \
  --output proof.json
```

**Output (proof.json):**
```json
{
  "stark_proof": {
    "proof_data": "0x...",
    "public_inputs": {
      "burn_amount": 8000000,
      "mint_amount": 8000000,
      "txn_hash": "0xdeadbeef",
      ...
    }
  },
  "merkle_proof": {
    "root_hash": "0x...",
    "leaf_hash": "0x...",
    "proof_path": ["0x...", "0x..."],
    "proof_indices": [0, 1, 0],
    "leaf_index": 42
  },
  "nullifier": "0x..."  // NEW: extracted from Stark proof
}
```

## Key Design Decisions

### 1. Nullifier in Public Inputs

**Current:** Nullifier computed inside AIR, NOT in public inputs
**Issue:** On-chain verifier can't extract nullifier without secret
**Options:**
- **A**: Add nullifier to `BurnMintPublicInputs` + update AIR boundary constraints
- **B**: Compute nullifier on-chain from public data (requires secret or different hash)
- **C**: Trust off-chain computation, only verify merkle proof on-chain

**Recommendation:** Option A - add nullifier to public inputs for clean on-chain verification.

### 2. On-Chain Stark Verification

**Winterfell Verifier in Solidity:**
- Complex: Must implement FRI, LDE, constraint evaluation
- Gas heavy: Hundreds of keccak256 + field operations
- Alternative: Precompile (Arbitrum Stylus) or separate verifier contract

**Simpler Approach (Recommended First):**
- Verify Stark proof OFF-CHAIN (user generates, CLI validates)
- ON-CHAIN: Only verify merkle proof + nullifier
- Trust model: Economic (users only claim valid burns) + cryptographic (merkle proof)

### 3. EFier Dependency

**Current:** EFier signatures required for root finalization
**Target:** Replace with trustless checkpoint system (SP1 zkVM)
- Batch verify Fuego blocks via SP1
- Update merkle root on-chain via ZK proof
- Users verify against latest checkpoint

**Transition:** Support both during migration
- Phase 1: EFier + Merkle (current)
- Phase 2: SP1 checkpoint + Merkle (new)
- Phase 3: SP1 only (fully trustless)

## RPC Endpoints (Fuego Daemon)

**Used by xfg-stark-cli:**

| Endpoint | Purpose | Returns |
|----------|---------|---------|
| `/get_height` | Chain height | Current block height |
| `/get_commitment` | Commitment details | Type, amount, block, term, tx hash, target chain, leaf index |
| `/get_commitment_stats` | Commitment stats | Total counts, merkle root, consensus % |
| `/get_commitment_merkle_proof` | Merkle proof | Root, leaf, proof path, indices, leaf index |
| `/check_commitment_exists` | Existence check | Boolean |

**Connection Strategy (FuegoRpcClient):**
1. User-specified address (`--daemon`)
2. Localhost (mainnet:18180, testnet:28280)
3. Seed node fallback (from CryptoNoteConfig.h)

## Security Considerations

### 1. Nullifier Reuse Prevention
- On-chain mapping: `mapping(bytes32 => bool) usedNullifiers`
- Check before minting: `require(!usedNullifiers[nullifier])`
- Mark after minting: `usedNullifiers[nullifier] = true`

### 2. Merkle Root Finalization
- Current: EFier signatures (≥69% of active EFiers)
- Future: SP1 ZK proof of block validity
- Contract stores: `bytes32 public latestRoot`

### 3. Commitment Binding
- Txn hash in public inputs binds proof to specific on-chain transaction
- Prevents replay with different transactions
- Merkle proof binds commitment to finalized tree

### 4. Cross-Chain Replay Prevention
- `network_id` in commitment (prevents mainnet→testnet replay)
- `target_chain_id` in public inputs (prevents cross-chain replay)
- `commitment_version` (prevents version confusion)

### 5. Privacy
- **Stark proof**: Hides secret (if verified on-chain)
- **Merkle proof**: Reveals commitment + sibling hashes
- **Claim transaction**: Reveals nullifier (not secret)
- **Current leak**: Full preimage revealed in `claimHEAT()` calldata
- **Improved**: Only nullifier revealed (if Stark verification hides secret)

## File Reference

### Rust (xfg-stark-3/src/)
- `lib.rs` - Crate root, module declarations
- `burn_mint_air.rs` - AIR constraints, commitment/nullifier computation
- `burn_mint_prover.rs` - Proof generation
- `burn_mint_verifier.rs` - Proof verification
- `proof_data_schema.rs` - Data structures (CompleteProofPackage, etc.)
- `fuego_rpc.rs` - Fuego daemon RPC client
- `bin/xfg-stark-cli.rs` - CLI tool with bundle command
- `types/stark.rs` - Stark proof types
- `proof/mod.rs` - Proof utilities
- `air/security.rs` - Security parameters

### Solidity (xfg-stark-3/)
- `FuegoCommitmentMerkleVerifier.sol` - Merkle verification + EFier registry
- `HEATBurnProofVerifier_v3.sol` - Current HEAT claim (merkle + EFier)
- `COLDProofVerifier_v3.sol` - COLD claim (merkle + EFier)
- `TierConversions.sol` - Shared tier constants
- `interfaces/IEd25519Verifier.sol` - Ed25519 verification interface

### Fuego C++ (src/)
- `CryptoNoteCore/CommitmentIndex.h/cpp` - Merkle tree, commitment indexing
- `CryptoNoteCore/StarkCommitmentGenerator.h/cpp` - Commitment/nullifier computation
- `Rpc/RpcServer.cpp` - Commitment RPC endpoints
- `CryptoNoteCore/ElderfierSignatureDaemon.cpp` - EFier signing
- `Wallet/SimpleWallet.cpp` - CLI: `burn_info`, `gen_proof`, `migrate_legacy_deposit`

## Common Tasks

### Generate Burn Proof
```bash
cd /Users/aejt/fuego_WS/xfg-stark-3
cargo run --bin xfg-stark-cli -- prove \
  --tx-hash <hash> \
  --secret <hex> \
  --amount 8000000 \
  --network 1 \
  --chain 1 \
  --output proof.bin
```

### Verify Proof (Off-Chain)
```bash
cargo run --bin xfg-stark-cli -- verify \
  --proof proof.bin \
  --public-inputs '{"burn_amount":8000000,...}'
```

### Bundle for Claim
```bash
cargo run --bin xfg-stark-cli -- bundle \
  --tx-hash <hash> \
  --secret <hex> \
  --output bundle.json
```

### Check Commitment on Fuego
```bash
cargo run --bin xfg-stark-cli -- verify-commitment \
  --tx-hash <hash> \
  --daemon localhost:18180
```

## Open Questions

1. **Should nullifier be added to Stark public inputs?**
   - Enables clean on-chain verification
   - Increases proof size slightly
   - Requires AIR modifications

2. **Winterfell verifier in Solidity - feasible?**
   - Complex FRI verification in EVM
   - High gas costs
   - Alternative: Stylus (Arbitrum) or external precompile

3. **EFier replacement timeline?**
   - SP1 zkVM for block verification
   - Trustless checkpoint system
   - Migration strategy (parallel support vs. hard switch)

4. **Privacy target: hide secret or just minimize leakage?**
   - Current: Full preimage revealed on-chain
   - Better: Only nullifier revealed (Stark proof verified on-chain)
   - Best: Full ZK (secret never leaves user)

## Related Skills

- `fuego-crypto` - Ed25519, MLSAG, Pedersen (cryptographic primitives)
- `fuego-currency` - CD interest, deposits, fee pool (economic layer)
- `fuego-orchestrator` - Routes to this skill for ZK/burn queries

## References

- **xfg-stark-3 repo:** `/Users/aejt/fuego_WS/xfg-stark-3/`
- **Implementation Summary:** `/Users/aejt/fuego_WS/xfg-stark-3/IMPLEMENTATION_SUMMARY_V3.md`
- **COLD vs HEAT:** `/Users/aejt/fuego_WS/xfg-stark-3/COLD_VS_HEAT_COMPARISON.md`
- **Winterfell docs:** https:///winterfell.rs/
- **Fuego repo:** `/Users/aejt/fuego_WS/`
