# ZK Bridge: Trustless Fuego → Ethereum HEAT Claiming 

## Context

The current HEAT claim system requires a ZK proof that trustlessly proves:

1. A chain of Fuego blocks has valid CryptoNight-UPX/2 PoW
2. Scanning those blocks produces a specific CommitmentIndex merkle root

Users can then claim HEAT by revealing their burn preimage against the on-chain merkle root — at claim time, just standard merkle proof verification in Solidity.

## Architecture

### Three components

```
FUEGO NODE (RPC)
  /get_block_range, /get_commitment_merkle_proof
        │
        ▼
BATCH PROVER (Rust CLI, user's machine)
  SP1 zkVM program:
    1. verify CryptoNight-UPX/2 PoW per block
    2. verify previousBlockHash chain linkage
    3. scan tx_extra 0x08 tags → collect HEAT commitments
    4. rebuild CommitmentIndex merkle root (keccak256-based)
    5. hash new state → new checkpoint
  → emits Groth16/Plonk proof
        │
        ▼
EVM CONTRACTS (Arbitrum / Ethereum)
  FuegoCheckpointVerifier.sol  — verifies SP1 proof, stores checkpoint
  HEATClaimer.sol              — merkle proof + preimage reveal → mint HEAT
```

### Key design facts (from codebase)

- Commitment preimage: `secret[32] || le64(amount) || le32(network_id) || le32(chain_id) || le32(version=3) || le32(term=0xFFFFFFFF)` (56 bytes)
- Commitment hash: `keccak256(preimage)`
- Nullifier: `keccak256(secret || "nullifier" || le64(amount))`
- Merkle leaf: raw commitment hash (32 bytes)
- Merkle node: `cn_fast_hash(left || right)` = `keccak256(left || right)` (64 bytes in)
- Odd leaf: `keccak256(leaf || leaf)`
- PoW: `cn_slow_hash(block_header_bytes, len, out, light=0, variant=2, prehashed=0)`
- No Cargo.toml exists yet in repo — new Rust workspace needed

## New Files to Create

```
fuego-prover/
  Cargo.toml                    # workspace
  fuego-core/
    Cargo.toml
    src/lib.rs                  # BlockHeader, CommitmentEntry, CheckpointState types
  fuego-cn/
    Cargo.toml
    src/lib.rs                  # Pure-Rust CryptoNight-UPX/2 (no C FFI; needed inside zkVM)
  fuego-circuit/
    Cargo.toml                  # SP1 program crate (sp1-zkvm dep)
    src/main.rs                 # The zkVM program: PoW verify + CommitmentIndex rebuild
  fuego-prover-cli/
    Cargo.toml                  # sp1-sdk, reqwest, clap
    src/main.rs                 # CLI: fetch blocks, run prover, submit proof

contracts/
  FuegoCheckpointVerifier.sol   # SP1Verifier wrapper + checkpoint state
  HEATClaimer.sol               # claimHEAT() — preimage + merkle proof → mint
```

## Implementation Plan

### Step 1: Rust workspace scaffold
- Create `fuego-prover/Cargo.toml` (workspace with 4 members)
- Create `fuego-core/src/lib.rs`: `BlockHeader`, `CommitmentEntry`, `CheckpointState` structs matching C++ types exactly

### Step 2: Pure-Rust CryptoNight-UPX/2 (`fuego-cn`)
- Port or wrap `cn_slow_hash` variant=2 into pure Rust (no C FFI — zkVMs can't call C)
- Reference: `src/crypto/slow-hash.c` and `slow-hash-xmrig.inl`
- Key UPX2 additions over base CN: integer div/sqrt seeded from `state.hs.w[12..13]`, memory shuffles at offsets 0x10/0x20/0x30
- Existing Rust CryptoNight crate (`cryptonight` on crates.io) can be used as starting point; verify it supports variant 2 with correct UPX2 tweaks
- Add test: hash a known Fuego block header, compare against `cn_slow_hash(..., 2, ...)` C output

### Step 3: SP1 zkVM circuit (`fuego-circuit`)
Program receives as **public inputs**:
```
prev_checkpoint_hash: [u8; 32]
new_checkpoint_hash:  [u8; 32]
new_merkle_root:      [u8; 32]
height_start:         u32
height_end:           u32
difficulty_target:    u32
```
**Private inputs (witness, stdin)**:
```
blocks: Vec<(BlockHeader, Vec<Transaction>)>
prev_commitment_leaves: Vec<[u8; 32]>   // all prior merkle leaves
```

Circuit logic:
1. **Chain linkage**: For each block i, assert `block[i].previousBlockHash == hash(block[i-1])`
2. **PoW**: For each block header, call `fuego_cn::cn_slow_hash(header_bytes, variant=2)`, assert result meets `difficulty_target`
3. **Commitment scan**: For each tx, parse tx_extra, extract 0x08 tags, collect new `commitment` hashes
4. **CommitmentIndex rebuild**: Append new leaves to `prev_commitment_leaves`, compute new merkle root using keccak256 (matching C++ `computeMerkleRoot()` exactly)
5. **Checkpoint hash**: `new_checkpoint_hash = keccak256(new_merkle_root || height_end || encoded_leaves_hash)`
6. Assert `new_checkpoint_hash` matches public input

### Step 4: Prover CLI (`fuego-prover-cli`)
```
fuego-prover prove \
  --rpc http://localhost:11211 \
  --from-height <last_checkpoint_height> \
  --to-height <current_height> \
  --checkpoint <prev_checkpoint_hash> \
  --out proof.bin

fuego-prover claim \
  --rpc http://localhost:11211 \
  --commitment <hex> \
  --preimage <hex> \
  --recipient <eth_addr> \
  --out claim.json
```

`prove` subcommand:
- Fetches blocks via Fuego RPC (new endpoint needed: `/get_block_range`)
- Fetches all prior commitment leaves via `/get_commitment_merkle_proof` and `/get_commitment_stats`
- Runs SP1 prover with `sp1_sdk::ProverClient`
- Writes proof bundle (proof bytes + public inputs) to file

`claim` subcommand:
- Fetches merkle proof for commitment via `/get_commitment_merkle_proof`
- Encodes preimage + proof into calldata for `HEATClaimer.claimHEAT()`
- Outputs JSON ready for `cast send` or ethers.js

### Step 5: Solidity contracts

**`FuegoCheckpointVerifier.sol`**:
```solidity
struct Checkpoint {
    bytes32 checkpointHash;
    bytes32 merkleRoot;
    uint32  height;
}
Checkpoint public latest;
ISP1Verifier public immutable verifier;  // SP1Verifier.sol from Succinct

function updateCheckpoint(
    bytes calldata proof,
    bytes32 prevCheckpointHash,
    bytes32 newCheckpointHash,
    bytes32 newMerkleRoot,
    uint32  heightStart,
    uint32  heightEnd,
    uint32  difficultyTarget
) external {
    require(prevCheckpointHash == latest.checkpointHash);
    require(heightStart == latest.height + 1);
    bytes memory publicValues = abi.encode(...);
    verifier.verifyProof(PROGRAM_VKEY, publicValues, proof);
    latest = Checkpoint(newCheckpointHash, newMerkleRoot, heightEnd);
    emit CheckpointUpdated(newMerkleRoot, heightEnd);
}
```

**`HEATClaimer.sol`**:
```solidity
function claimHEAT(
    bytes  calldata preimage,       // 56 bytes: secret||amount||networkId||chainId||version||term
    bytes32[] calldata merkleProof,
    uint256 leafIndex,
    address recipient
) external {
    bytes32 commitment = keccak256(preimage);
    bytes32 nullifier = keccak256(abi.encodePacked(
        preimage[:32], "nullifier", preimage[32:40]  // secret || "nullifier" || amount
    ));
    require(!usedNullifiers[nullifier], "already claimed");
    require(verifyMerkleProof(commitment, merkleProof, leafIndex, checkpointVerifier.latest().merkleRoot));
    usedNullifiers[nullifier] = true;
    uint64 amount = uint64(bytes8(preimage[32:40]));  // le64 decode
    uint256 heatAmount = deriveHEATAmount(amount);
    IHEATToken(heatToken).mint(recipient, heatAmount);
    emit HEATClaimed(commitment, nullifier, recipient, heatAmount);
}
```

### Step 6: New Fuego RPC endpoint
Add `/get_block_range` to `src/Rpc/RpcServer.cpp` and `CoreRpcServerCommandsDefinitions.h`:
- Input: `{ "start_height": N, "end_height": M }`
- Output: array of serialized block bytes (headers + all transactions with tx_extra)
- This is read-only; just iterates existing `Blockchain` data

Critical files to modify:
- `src/Rpc/RpcServer.cpp` — add `on_get_block_range()` handler
- `src/Rpc/RpcServer.h` — declare handler
- `src/Rpc/CoreRpcServerCommandsDefinitions.h` — add request/response structs

## Checkpoint State Hash Definition

```
checkpoint_hash = keccak256(
    merkle_root (32 bytes)
  || height_end (4 bytes LE)
  || keccak256(all_commitment_hashes_concatenated) (32 bytes)
)
```

This makes the checkpoint bind to both the tree root and the full ordered leaf set — prevents a prover from presenting a valid root computed from a different leaf ordering.

## Verification / Testing

1. **Unit test CryptoNight port**: Hash a known Fuego block header in Rust, compare against C `cn_slow_hash(..., 2)` output
2. **Unit test merkle rebuild**: Feed known CommitmentIndex state into circuit logic, assert root matches `/get_commitment_merkle_root` RPC output
3. **End-to-end local test**:
   - Run `fuego-prover prove` against a local regtest node with a few burn deposits
   - Verify proof with SP1 verifier locally (`ProverClient::verify()`)
   - Deploy contracts to Anvil/Hardhat fork, call `updateCheckpoint()` with proof
   - Call `claimHEAT()` with preimage + merkle proof, verify HEAT minted
4. **Gas measurement**: `updateCheckpoint()` target < 400k gas; `claimHEAT()` target < 100k gas

## What Gets Removed (EFier dependencies)

Once this is deployed, the following can be stripped:
- `ElderfierSignatureDaemon.cpp/h`
- 0xEF tag handling in tx_extra parsing
- `CachedElderfierSignature` in CommitmentIndex
- `/get_ef_consensus_status` RPC endpoint
- EFier deposit validation in Currency.cpp (0xEF tier checks)
- EFier P2P gossip protocol messages

The CommitmentIndex itself stays — it's still needed for merkle proof generation.
