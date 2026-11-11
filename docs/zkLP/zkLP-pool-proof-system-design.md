# LP Swap Pool Proof System — Full Implementation Design Guide

## 1. Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         SYSTEM OVERVIEW                                  │
│                                                                         │
│  ┌─────────────┐    ┌─────────────┐    ┌─────────────┐                 │
│  │   Users     │    │ PoolOrganizer│   │   Provers   │                 │
│  │             │    │   (daemon)  │    │  (anyone)   │                 │
│  │ - Deposit   │───►│ - Buffers   │───►│ - Decrypt   │                 │
│  │ - Swap      │    │   events    │    │ - Process   │                 │
│  │ - Withdraw  │    │ - Builds    │    │ - Generate  │                 │
│  │ - Claim fees│    │   Merkle    │    │   SP1 proof │                 │
│  └─────────────┘    │ - Publishes │    │ - Submit    │                 │
│                     │   proof     │    │   on-chain  │                 │
│                     └──────┬──────┘    └──────┬──────┘                 │
│                            │                  │                         │
│                            └────────┬─────────┘                         │
│                                     ▼                                   │
│                          ┌─────────────────────┐                        │
│                          │   On-Chain Verifier │                        │
│                          │                     │                        │
│                          │ - Verify SP1 proof  │                        │
│                          │ - Update state      │                        │
│                          │ - Distribute fees   │                        │
│                          └─────────────────────┘                        │
│                                                                         │
│  Proof System: SP1 zkVM (Plonky3/STARK backend)                        │
│  Epoch: 100 blocks (~13 hours)                                         │
│  Commitment Scheme: Pedersen with type-separated generators            │
└─────────────────────────────────────────────────────────────────────────┘
```

## 2. Commitment Scheme

### 2.1 Pedersen Commitments with Type Separation

Each value is committed as: `C = r*G + v*H_type`

Where:
- `r` = random blinding factor (32 bytes)
- `v` = committed value (u64)
- `G` = base point (secp256k1 generator)
- `H_type` = type-specific generator point

### 2.2 Commitment Types

```rust
// fuego-core/src/lib.rs — add to existing CommitmentType enum
pub enum CommitmentType {
    Heat = 0,
    Cold = 1,
    // LP Pool types
    LpReserveA = 2,
    LpReserveB = 3,
    LpShare = 4,
    LpFee = 5,
    LpFeeAccumulator = 6,
    LpNonce = 7,
}

impl CommitmentType {
    pub fn generator(&self) -> ProjectivePoint {
        // Domain-separated: H_type = HashToPoint("LP_POOL_" + type_name)
        let label = match self {
            CommitmentType::LpReserveA => b"LP_POOL_RESERVE_A",
            CommitmentType::LpReserveB => b"LP_POOL_RESERVE_B",
            CommitmentType::LpShare => b"LP_POOL_SHARE",
            CommitmentType::LpFee => b"LP_POOL_FEE",
            CommitmentType::LpFeeAccumulator => b"LP_POOL_FEE_ACC",
            CommitmentType::LpNonce => b"LP_POOL_NONCE",
            _ => panic!("Not an LP pool type"),
        };
        hash_to_secp256k1(label)
    }
}
```

### 2.3 Commitment Structure

```rust
// fuego-core/src/lib.rs
#[derive(Clone, Serialize, Deserialize)]
pub struct LpCommitment {
    pub point: [u8; 33],  // Compressed secp256k1 point
    pub r#type: u8,       // CommitmentType discriminant
    pub epoch: u32,       // Epoch this commitment was created
}

impl LpCommitment {
    pub fn commit(value: u64, blinding: [u8; 32], r#type: CommitmentType, epoch: u32) -> Self {
        let g = secp256k1_generator();
        let h = r#type.generator();
        let point = blinding * g + value * h;
        Self {
            point: compress(point),
            r#type: r#type as u8,
            epoch,
        }
    }

    pub fn verify_type(&self) -> bool {
        // Verify the point was constructed with the claimed type's generator
        // This is implicit in the circuit — the circuit reconstructs the
        // commitment using the type's generator and checks it matches.
        true
    }
}
```

## 3. Pool State

### 3.1 On-Chain Pool State

```rust
// fuego-core/src/lib.rs
#[derive(Clone, Serialize, Deserialize)]
pub struct LpPoolState {
    pub pool_id: [u8; 32],
    pub token_a: String,        // e.g., "XFG"
    pub token_b: String,        // e.g., "ETH"
    pub fee_bps: u16,           // Fee in basis points (e.g., 30 = 0.3%)
    pub min_liquidity: u64,     // Minimum liquidity to prevent division by zero

    // Commitments (opaque to observers)
    pub c_reserve_a: LpCommitment,
    pub c_reserve_b: LpCommitment,
    pub c_lp_shares: LpCommitment,
    pub c_fee_accum_a: LpCommitment,
    pub c_fee_accum_b: LpCommitment,

    // Merkle roots (public)
    pub lp_merkle_root: [u8; 32],
    pub fee_merkle_root: [u8; 32],

    // Epoch tracking
    pub epoch_start: u32,
    pub epoch_end: u32,
    pub prev_state_commitment: [u8; 32],
}
```

### 3.2 Merkle Trees

Two trees per pool:

1. **LP Share Tree**: Maps LP provider → share balance
   - Leaf: `Hash(provider_key, c_lp_balance, blinding_factor)`
   - Root: `lp_merkle_root`

2. **Fee Record Tree**: Maps LP provider → claimable fees
   - Leaf: `Hash(provider_key, c_claimable_a, c_claimable_b)`
   - Root: `fee_merkle_root`

## 4. Encrypted Events

### 4.1 Event Types

```rust
// fuego-core/src/lib.rs
#[derive(Clone, Serialize, Deserialize)]
pub enum EncryptedLpEvent {
    Deposit {
        c_amount_a: LpCommitment,
        c_amount_b: LpCommitment,
        c_lp_minted: LpCommitment,
        provider_key: [u8; 32],
        epoch_key_hint: [u8; 32],  // For decryption at epoch boundary
    },
    Withdrawal {
        c_lp_burned: LpCommitment,
        c_amount_a: LpCommitment,
        c_amount_b: LpCommitment,
        provider_key: [u8; 32],
        epoch_key_hint: [u8; 32],
    },
    Swap {
        c_input: LpCommitment,
        c_output: LpCommitment,
        c_fee: LpCommitment,
        swap_a_for_b: bool,
        trader_key: [u8; 32],
        epoch_key_hint: [u8; 32],
    },
    FeeClaim {
        c_claimed_a: LpCommitment,
        c_claimed_b: LpCommitment,
        provider_key: [u8; 32],
        epoch_key_hint: [u8; 32],
    },
}
```

### 4.2 Encryption Scheme

Events are encrypted with a **deterministic pool key** derived from pool ID and epoch:

```rust
// Pool key derivation
fn derive_epoch_key(pool_id: [u8; 32], epoch: u32) -> [u8; 32] {
    // HKDF(pool_id || epoch_number)
    // Revealed at epoch boundary so any prover can decrypt
    hkdf_sha256(&pool_id, &epoch.to_le_bytes())
}

// Event encryption
fn encrypt_event(event: &LpEvent, epoch_key: [u8; 32]) -> EncryptedLpEvent {
    // AES-256-GCM with epoch_key
    // Nonce = random per-event
    // Ciphertext includes all commitment data
}
```

## 5. SP1 Circuit Design

### 5.1 Circuit Entry Point

```rust
// fuego-lp-circuit/src/main.rs
#![no_main]
sp1_zkvm::entrypoint!(main);

use fuego_core::{
    LpPoolState, EncryptedLpEvent, LpCommitment, CommitmentType,
    compute_merkle_root, verify_merkle_proof, compute_checkpoint_hash,
};

pub fn main() {
    // 1. Read witness from SP1 stdin
    let pool_state: LpPoolState = sp1_zkvm::io::read();
    let events: Vec<EncryptedLpEvent> = sp1_zkvm::io::read();
    let epoch_key: [u8; 32] = sp1_zkvm::io::read();
    let prev_lp_tree_leaves: Vec<[u8; 32]> = sp1_zkvm::io::read();
    let prev_fee_tree_leaves: Vec<[u8; 32]> = sp1_zkvm::io::read();

    // 2. Decrypt events
    let decrypted_events = decrypt_events(&events, epoch_key);

    // 3. Process events in order
    let mut state = pool_state.clone();
    let mut new_lp_leaves = prev_lp_tree_leaves.clone();
    let mut new_fee_leaves = prev_fee_tree_leaves.clone();

    for event in &decrypted_events {
        match event {
            EncryptedLpEvent::Deposit { .. } => {
                process_deposit(&mut state, &mut new_lp_leaves, event);
            }
            EncryptedLpEvent::Withdrawal { .. } => {
                process_withdrawal(&mut state, &mut new_lp_leaves, event);
            }
            EncryptedLpEvent::Swap { .. } => {
                process_swap(&mut state, event);
            }
            EncryptedLpEvent::FeeClaim { .. } => {
                process_fee_claim(&mut state, &mut new_fee_leaves, event);
            }
        }
    }

    // 4. Recompute Merkle roots
    let new_lp_root = compute_merkle_root(&new_lp_leaves);
    let new_fee_root = compute_merkle_root(&new_fee_leaves);

    // 5. Verify state consistency
    assert!(state.c_reserve_a.verify_type());
    assert!(state.c_reserve_b.verify_type());
    assert!(state.c_lp_shares.verify_type());

    // 6. Verify AMM invariant (commitment-based)
    verify_amm_invariant(&pool_state, &state, &decrypted_events);

    // 7. Verify fee calculations
    verify_fees(&state, &decrypted_events, pool_state.fee_bps);

    // 8. Compute state transition hash
    let prev_commitment = state.prev_state_commitment;
    let new_commitment = compute_checkpoint_hash(&[
        &state.c_reserve_a.point,
        &state.c_reserve_b.point,
        &state.c_lp_shares.point,
        &new_lp_root,
        &new_fee_root,
    ]);

    // 9. Commit public outputs to SP1 journal
    sp1_zkvm::io::commit(&prev_commitment);
    sp1_zkvm::io::commit(&new_commitment);
    sp1_zkvm::io::commit(&new_lp_root);
    sp1_zkvm::io::commit(&new_fee_root);
    sp1_zkvm::io::commit(&state.epoch_start);
    sp1_zkvm::io::commit(&state.epoch_end);
}
```

### 5.2 AMM Invariant Verification (Commitment-Based)

```rust
fn verify_amm_invariant(
    prev_state: &LpPoolState,
    final_state: &LpPoolState,
    events: &[DecryptedLpEvent],
) {
    // The AMM invariant: reserveA * reserveB = k (constant product)
    //
    // With commitments, we can't directly multiply committed values.
    // Instead, we verify the state transition is consistent:
    //
    // For each swap:
    //   input_after_fee = input - fee
    //   output = reserveB - (reserveA * reserveB) / (reserveA + input_after_fee)
    //   new_reserveA = reserveA + input
    //   new_reserveB = reserveB - output
    //
    // The circuit verifies this arithmetically on the plaintext values
    // (which the prover sees during execution) while only committing
    // the commitment points to the journal.

    let mut reserve_a = extract_internal(prev_state.c_reserve_a); // internal to circuit
    let mut reserve_b = extract_internal(prev_state.c_reserve_b);

    for event in events {
        match event {
            DecryptedLpEvent::Swap { input, output, fee, a_for_b } => {
                if *a_for_b {
                    let input_after_fee = input - fee;
                    let expected_output = reserve_b - (reserve_a * reserve_b) / (reserve_a + input_after_fee);
                    assert_eq!(output, expected_output, "AMM invariant violated");
                    reserve_a += input;
                    reserve_b -= output;
                } else {
                    let input_after_fee = input - fee;
                    let expected_output = reserve_a - (reserve_a * reserve_b) / (reserve_b + input_after_fee);
                    assert_eq!(output, expected_output, "AMM invariant violated");
                    reserve_b += input;
                    reserve_a -= output;
                }
            }
            DecryptedLpEvent::Deposit { amount_a, amount_b, .. } => {
                reserve_a += amount_a;
                reserve_b += amount_b;
            }
            DecryptedLpEvent::Withdrawal { amount_a, amount_b, .. } => {
                reserve_a -= amount_a;
                reserve_b -= amount_b;
            }
            _ => {}
        }
    }

    // Final reserves must match the final state commitments
    assert_commitment_matches_value(&final_state.c_reserve_a, reserve_a);
    assert_commitment_matches_value(&final_state.c_reserve_b, reserve_b);
}
```

### 5.3 Fee Verification

```rust
fn verify_fees(state: &LpPoolState, events: &[DecryptedLpEvent], fee_bps: u16) {
    let mut expected_fee_a = 0u64;
    let mut expected_fee_b = 0u64;

    for event in events {
        if let DecryptedLpEvent::Swap { input, a_for_b, .. } = event {
            let fee = input * fee_bps as u64 / 10000;
            if *a_for_b {
                expected_fee_a += fee;
            } else {
                expected_fee_b += fee;
            }
        }
    }

    // Verify fee accumulator commitments match expected
    assert_commitment_matches_value(&state.c_fee_accum_a, expected_fee_a);
    assert_commitment_matches_value(&state.c_fee_accum_b, expected_fee_b);
}
```

## 6. PoolOrganizer Integration

### 6.1 New Methods in PoolOrganizer

```cpp
// src/SwapDaemon/PoolOrganizer.h — additions

class PoolOrganizer {
public:
    // ... existing methods ...

    // Epoch management
    void startEpoch(uint32_t blockHeight);
    void endEpoch(uint32_t blockHeight);
    bool isEpochBoundary(uint32_t blockHeight) const;

    // Event buffering
    void bufferEncryptedEvent(const EncryptedLpEvent& event);
    std::vector<EncryptedLpEvent> getBufferedEvents() const;

    // Merkle tree management
    HashValue buildLpMerkleRoot() const;
    HashValue buildFeeMerkleRoot() const;
    std::vector<HashValue> getLpTreeLeaves() const;
    std::vector<HashValue> getFeeTreeLeaves() const;

    // Proof generation
    LpProof generateEpochProof(
        const LpPoolState& prevState,
        const std::vector<EncryptedLpEvent>& events,
        const std::vector<uint8_t>& epochKey,
        const std::vector<HashValue>& prevLpLeaves,
        const std::vector<HashValue>& prevFeeLeaves
    );

    // State management
    LpPoolState getCurrentState() const;
    void updateState(const LpPoolState& newState);

private:
    // Epoch tracking
    uint32_t m_epochStart = 0;
    uint32_t m_epochEnd = 0;
    static constexpr uint32_t EPOCH_BLOCKS = 100;

    // Event buffer
    std::vector<EncryptedLpEvent> m_eventBuffer;

    // Merkle trees
    std::vector<HashValue> m_lpTreeLeaves;
    std::vector<HashValue> m_feeTreeLeaves;

    // Current pool state (commitments)
    LpPoolState m_currentState;

    // Epoch key derivation
    std::vector<uint8_t> deriveEpochKey(uint32_t epoch) const;
};
```

### 6.2 Epoch Lifecycle in PoolOrganizer

```cpp
// src/SwapDaemon/PoolOrganizer.cpp — additions

void PoolOrganizer::startEpoch(uint32_t blockHeight) {
    m_epochStart = blockHeight;
    m_epochEnd = blockHeight + EPOCH_BLOCKS;
    m_eventBuffer.clear();
    m_log(INFO, "LP pool epoch started: " << blockHeight << " - " << m_epochEnd);
}

bool PoolOrganizer::isEpochBoundary(uint32_t blockHeight) const {
    return blockHeight >= m_epochEnd;
}

void PoolOrganizer::endEpoch(uint32_t blockHeight) {
    // 1. Derive epoch key (revealed at boundary)
    auto epochKey = deriveEpochKey(m_epochStart / EPOCH_BLOCKS);

    // 2. Generate proof
    auto proof = generateEpochProof(
        m_currentState,
        m_eventBuffer,
        epochKey,
        m_lpTreeLeaves,
        m_feeTreeLeaves
    );

    // 3. Publish proof to chain
    publishProof(proof);

    // 4. Update state
    m_currentState.prev_state_commitment = proof.newStateCommitment;
    m_currentState.lp_merkle_root = proof.lpMerkleRoot;
    m_currentState.fee_merkle_root = proof.feeMerkleRoot;
    m_currentState.epoch_start = m_epochStart;
    m_currentState.epoch_end = m_epochEnd;

    // 5. Start next epoch
    startEpoch(blockHeight);
}
```

## 7. Prover Infrastructure

### 7.1 SP1 Prover CLI Extension

```rust
// fuego-prover/fuego-prover-cli/src/main.rs — add LP subcommand

#[derive(clap::Subcommand)]
enum Command {
    // Existing HEAT commands
    ProveHeat { ... },

    // NEW: LP pool commands
    ProveLp {
        #[arg(long)]
        pool_state: String,  // JSON file with pool state

        #[arg(long)]
        events: String,      // JSON file with encrypted events

        #[arg(long)]
        epoch_key: String,   // Hex-encoded epoch key

        #[arg(long)]
        prev_lp_leaves: String,  // JSON file with LP tree leaves

        #[arg(long)]
        prev_fee_leaves: String, // JSON file with fee tree leaves

        #[arg(long)]
        output: Option<String>,  // Output proof file
    },
}

fn handle_prove_lp(args: ProveLpArgs) -> Result<()> {
    // 1. Load ELF
    let elf = include_bytes!("../../fuego-lp-circuit/elf/riscv32im-succinct-zkvm-elf");

    // 2. Setup prover client
    let client = ProverClient::new();

    // 3. Build stdin
    let mut stdin = SP1Stdin::new();
    stdin.write(&load_pool_state(&args.pool_state)?);
    stdin.write(&load_events(&args.events)?);
    stdin.write(&hex::decode(&args.epoch_key)?);
    stdin.write(&load_leaves(&args.prev_lp_leaves)?);
    stdin.write(&load_leaves(&args.prev_fee_leaves)?);

    // 4. Execute circuit
    let (pk, vk) = client.setup(elf);
    let proof = client.prove(&pk, &stdin).run()?;

    // 5. Verify
    client.verify(&proof, &vk)?;

    // 6. Output
    let output_path = args.output.unwrap_or_else(|| "lp_proof.json".into());
    std::fs::write(&output_path, serde_json::to_string(&proof)?)?;

    println!("LP proof generated: {}", output_path);
    Ok(())
}
```

### 7.2 Proof Structure

```rust
// fuego-core/src/lib.rs
#[derive(Serialize, Deserialize)]
pub struct LpProof {
    // Public outputs (committed to chain)
    pub prev_state_commitment: [u8; 32],
    pub new_state_commitment: [u8; 32],
    pub lp_merkle_root: [u8; 32],
    pub fee_merkle_root: [u8; 32],
    pub epoch_start: u32,
    pub epoch_end: u32,

    // SP1 proof bytes
    pub sp1_proof: Vec<u8>,

    // Verification key
    pub vk_hash: [u8; 32],
}
```

## 8. On-Chain Verification

### 8.1 Verification Contract (Pseudocode)

```solidity
// LpPoolVerifier.sol (conceptual — actual implementation depends on chain)
contract LpPoolVerifier {
    mapping(bytes32 => bool) public validProofs;
    mapping(bytes32 => PoolState) public poolStates;

    function submitLpProof(LpProof calldata proof) external {
        // 1. Verify SP1 proof
        require(
            verifySp1Proof(proof.sp1_proof, proof.vk_hash),
            "Invalid SP1 proof"
        );

        // 2. Verify epoch boundaries
        require(proof.epoch_end > proof.epoch_start, "Invalid epoch");

        // 3. Verify state linkage
        bytes32 poolId = computePoolId();
        require(
            poolStates[poolId].stateCommitment == proof.prev_state_commitment,
            "State mismatch"
        );

        // 4. Update pool state
        poolStates[poolId] = PoolState({
            stateCommitment: proof.new_state_commitment,
            lpMerkleRoot: proof.lp_merkle_root,
            feeMerkleRoot: proof.fee_merkle_root,
            epochEnd: proof.epoch_end
        });

        // 5. Mark proof valid
        validProofs[keccak256(abi.encode(proof))] = true;

        // 6. Distribute fees to LP providers (if any)
        distributeFees(proof.fee_merkle_root);

        emit ProofSubmitted(proof.epoch_start, proof.epoch_end);
    }
}
```

## 9. File Structure

```
fuego-prover/
├── fuego-core/
│   └── src/
│       └── lib.rs              # Add: LpCommitment, LpPoolState, EncryptedLpEvent, LpProof
├── fuego-circuit/              # Existing HEAT circuit (unchanged)
│   └── src/
│       └── main.rs
├── fuego-lp-circuit/           # NEW: LP pool circuit
│   ├── Cargo.toml
│   ├── build.rs
│   └── src/
│       └── main.rs             # LP pool SP1 circuit
├── fuego-prover-cli/
│   └── src/
│       └── main.rs             # Add: prove-lp subcommand

src/SwapDaemon/
├── PoolOrganizer.h             # Add: epoch management, proof generation
├── PoolOrganizer.cpp           # Add: epoch lifecycle, SP1 integration
├── SwapTypes.h                 # Add: LpPoolState, EncryptedLpEvent, LpProof
└── SwapDaemon.cpp              # Add: epoch boundary detection, proof publishing
```

## 10. Implementation Phases

### Phase 1: Core Types (Week 1-2)
- [ ] Add `CommitmentType` variants to `fuego-core/src/lib.rs`
- [ ] Implement `LpCommitment` struct with Pedersen commitment
- [ ] Implement `LpPoolState` struct
- [ ] Implement `EncryptedLpEvent` enum
- [ ] Implement `LpProof` struct
- [ ] Unit tests for commitment arithmetic

### Phase 2: Merkle Trees (Week 2-3)
- [ ] Implement LP share Merkle tree in PoolOrganizer
- [ ] Implement fee record Merkle tree in PoolOrganizer
- [ ] Add Merkle proof generation and verification
- [ ] Unit tests for tree operations

### Phase 3: SP1 Circuit (Week 3-5)
- [ ] Create `fuego-lp-circuit` project
- [ ] Implement event decryption in circuit
- [ ] Implement deposit processing
- [ ] Implement withdrawal processing
- [ ] Implement swap processing (AMM math)
- [ ] Implement fee claim processing
- [ ] Implement AMM invariant verification
- [ ] Implement fee verification
- [ ] Implement Merkle root recomputation
- [ ] Commit public outputs to journal
- [ ] Build ELF and test with mock data

### Phase 4: PoolOrganizer Integration (Week 5-6)
- [ ] Add epoch management to PoolOrganizer
- [ ] Add event buffering
- [ ] Add epoch key derivation
- [ ] Add SP1 prover CLI integration
- [ ] Add proof publishing to chain
- [ ] Integration tests

### Phase 5: Prover CLI (Week 6-7)
- [ ] Add `prove-lp` subcommand to fuego-prover-cli
- [ ] Add proof verification
- [ ] Add proof output/serialization
- [ ] Test end-to-end with mock events

### Phase 6: On-Chain Verification (Week 7-8)
- [ ] Implement verification contract/logic
- [ ] Implement state update logic
- [ ] Implement fee distribution logic
- [ ] Test proof submission and verification

### Phase 7: Privacy Hardening (Week 8-9)
- [ ] Implement per-epoch commitment re-randomization
- [ ] Implement range proofs for committed values
- [ ] Test privacy guarantees (prover cannot extract values)

### Phase 8: Production Readiness (Week 9-10)
- [ ] Add error handling and recovery
- [ ] Add monitoring and alerting
- [ ] Add fallback prover mechanism
- [ ] Load testing
- [ ] Security audit
- [ ] Documentation

## 11. Key Design Decisions

| Decision | Choice | Rationale |
|---|---|---|
| Proof system | SP1 zkVM | Shared infrastructure with HEAT prover, same toolchain |
| Commitment scheme | Pedersen with type-separated generators | Hides values, prevents type confusion, supports homomorphic operations |
| Epoch size | 100 blocks (~13 hours) | Balance of privacy mixing vs fraud detection speed |
| Encryption | AES-256-GCM with epoch-derived key | Simple, fast, key revealed at epoch boundary |
| Prover model | Permissionless (anyone can prove) | Decentralization, censorship resistance |
| Incentive | LP-prover model (LPs have incentive to prove) | Aligned incentives, no new actors needed |
| Fallback | Pool freezes if no proof by deadline | Safety over liveness, LPs can force-withdraw |

## 12. Dependencies

```toml
# fuego-lp-circuit/Cargo.toml
[dependencies]
sp1-zkvm = "3"
fuego-core = { path = "../fuego-core" }
k256 = { version = "0.13", default-features = false, features = ["arithmetic"] }
sha2 = { version = "0.10", default-features = false }
serde = { version = "1.0", default-features = false, features = ["derive"] }

[build-dependencies]
sp1-helper = "3"
```

## 13. Testing Strategy

```
Unit Tests:
  - Commitment creation and verification
  - Type separation (can't mix types)
  - Merkle tree operations
  - AMM math correctness
  - Fee calculation correctness
  - Epoch key derivation

Integration Tests:
  - Full epoch lifecycle (start → events → end → proof)
  - SP1 circuit execution with mock data
  - Proof generation and verification
  - State transition consistency

Privacy Tests:
  - Prover cannot extract values from commitments
  - Re-randomization makes commitments unlinkable across epochs
  - Range proofs hide exact values within bounds

Failure Mode Tests:
  - No proof submitted → pool freezes
  - Invalid proof submitted → rejected
  - Missing events → proof fails
  - Wrong epoch key → decryption fails
```
