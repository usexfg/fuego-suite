# HEAT vs LP Pool ZK Proofs — Why Separate

## The Core Problem

The existing fuego-prover circuit proves **one thing**:
> "These commitment hashes really exist in these blockchain blocks"

The LP pool needs to prove **something completely different**:
> "This swap/deposit/withdrawal was calculated correctly according to AMM rules"

---

## Diagram: Two Different Worlds

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        HEAT COMMITMENT PROOF                            │
│                         (fuego-circuit)                                 │
│                                                                         │
│  INPUT:                                                                 │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐                     │
│  │ Block 1001  │  │ Block 1002  │  │ Block 1003  │  ...                │
│  │ tx_extra[]  │  │ tx_extra[]  │  │ tx_extra[]  │                     │
│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘                     │
│         │                │                │                              │
│         └────────────────┴────────────────┘                              │
│                          │                                               │
│              scan for tag 0x08                                           │
│              extract commitment hashes                                   │
│                          │                                               │
│              ┌───────────▼───────────┐                                   │
│              │   Build Merkle Tree   │                                   │
│              │   (just hash leaves)  │                                   │
│              └───────────┬───────────┘                                   │
│                          │                                               │
│  OUTPUT (public):                                                        │
│  ┌─────────────────────┬─────────────────────┬──────────────────┐       │
│  │ new_merkle_root     │ new_checkpoint_hash │ height_end       │       │
│  │ [32 bytes]          │ [32 bytes]          │ [4 bytes]        │       │
│  └─────────────────────┴─────────────────────┴──────────────────┘       │
│                                                                         │
│  WHAT IT PROVES: "I scanned real blocks and these commitments exist"    │
└─────────────────────────────────────────────────────────────────────────┘


┌─────────────────────────────────────────────────────────────────────────┐
│                        LP POOL PROOF (NEW - needed)                     │
│                       (fuego-lp-circuit)                                │
│                                                                         │
│  INPUT:                                                                 │
│  ┌───────────────────────────────────────────────────────────┐          │
│  │ Pool State BEFORE:                                        │          │
│  │   reserveA = 1,000,000 XFG                                │          │
│  │   reserveB = 500,000 BTC                                  │          │
│  │   totalLPShares = 707,106                                 │          │
│  └───────────────────────────────────────────────────────────┘          │
│                          │                                               │
│  ┌───────────────────────────────────────────────────────────┐          │
│  │ Operation: SWAP 100 XFG → BTC                             │          │
│  │   feeBps = 30 (0.3%)                                      │          │
│  └───────────────────────────────────────────────────────────┘          │
│                          │                                               │
│              ┌───────────▼───────────┐                                   │
│              │  1. Calculate fee:    │                                   │
│              │     fee = 100 * 0.003 = 0.3 XFG                          │
│              │  2. Apply AMM:        │                                   │
│              │     input = 99.7 XFG  │                                   │
│              │     output = k/(rA+input) - rB                           │
│              │  3. Update reserves   │                                   │
│              │  4. Rebuild Merkle trees                                   │
│              └───────────┬───────────┘                                   │
│                          │                                               │
│  OUTPUT (public):                                                        │
│  ┌──────────┬──────────┬───────────┬──────────┬─────────────────┐       │
│  │ lpRoot   │ feeRoot  │ reserveA  │ reserveB │ feeCollected    │       │
│  │ [32B]    │ [32B]    │ [8B]      │ [8B]     │ [8B]            │       │
│  └──────────┴──────────┴───────────┴──────────┴─────────────────┘       │
│                                                                         │
│  WHAT IT PROVES: "This swap math is correct, fees are fair"             │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Why One Circuit Can't Do Both

```
┌──────────────────────────────────────────────────────────────────┐
│                        COMPARISON TABLE                          │
├─────────────────────┬────────────────────┬───────────────────────┤
│                     │ HEAT PROOF         │ LP POOL PROOF         │
├─────────────────────┼────────────────────┼───────────────────────┤
│ What's proven?      │ Commitments exist  │ Math is correct       │
│                     │ in the blockchain  │ (AMM, fees, shares)   │
├─────────────────────┼────────────────────┼───────────────────────┤
│ Input data          │ Block headers +    │ Pool state +          │
│                     │ tx_extra bytes     │ operation params      │
├─────────────────────┼────────────────────┼───────────────────────┤
│ Computation         │ Parse tag 0x08,    │ AMM formula,          │
│                     │ hash extraction    │ fee calc, share math  │
├─────────────────────┼────────────────────┼───────────────────────┤
│ Merkle tree         │ 1 tree:            │ 2 trees per pool:     │
│                     │ commitment hashes  │ LP shares + fee recs  │
├─────────────────────┼────────────────────┼───────────────────────┤
│ Public outputs      │ merkle_root,       │ lpRoot, feeRoot,      │
│                     │ checkpoint, height │ reserves, fees        │
├─────────────────────┼────────────────────┼───────────────────────┤
│ Can they merge?     │ NO — they prove    │                       │
│                     │ fundamentally      │                       │
│                     │ different things   │                       │
└─────────────────────┴────────────────────┴───────────────────────┘
```

---

## How They Connect (Fee Routing to fuego-prover)

```
┌─────────────────────────────────────────────────────────────────────┐
│                      THE FEE ROUTING FLOW                           │
│                                                                     │
│  ┌─────────────┐                                                    │
│  │  Banking    │  0.1% Elderfier fee                                │
│  │ Transaction │ ──────────────────────────────────────────┐        │
│  └─────────────┘                                           │        │
│         │                                                  │        │
│         ▼                                                  ▼        │
│  ┌─────────────────────────┐                    ┌──────────────────┐ │
│  │ LP Pool Circuit         │                    │ fuego-prover     │ │
│  │ (fuego-lp-circuit)      │                    │ (fuego-circuit)  │ │
│  │                         │                    │                  │ │
│  │ Proves: swap math OK    │                    │ Proves: HEAT     │ │
│  │ Outputs: feeCollected   │                    │ commitments OK   │ │
│  │           = 0.1% fee    │                    │                  │ │
│  └───────────┬─────────────┘                    └────────┬─────────┘ │
│              │                                           │           │
│              │  feeCollected becomes a                   │           │
│  ┌───────────▼─────────────┐                    ┌───────▼──────────┐ │
│  │  Fee Commitment         │                    │  Block           │ │
│  │  (new LP fee tree leaf) │                    │  Commitments     │ │
│  │                         │                    │  (tag 0x08)      │ │
│  └─────────────────────────┘                    └──────────────────┘ │
│              │                                           │           │
│              └───────────────────┬───────────────────────┘           │
│                                  │                                   │
│                                  ▼                                   │
│                    ┌─────────────────────────┐                       │
│                    │  Verifier / Bridge      │                       │
│                    │                         │                       │
│                    │  Checks both proofs     │                       │
│                    │  Confirms fee is valid  │                       │
│                    │  AND exists on-chain    │                       │
│                    └─────────────────────────┘                       │
└─────────────────────────────────────────────────────────────────────┘
```

---

## What CAN Be Shared (Reuse, Not Merge)

```
┌──────────────────────────────────────────────────────────────────┐
│                    SHARED BUILDING BLOCKS                        │
│              (used by BOTH circuits, but separate proofs)        │
│                                                                  │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │  SP1 zkVM runtime                                          │  │
│  │  (the "engine" that runs both circuits)                    │  │
│  └────────────────────────────────────────────────────────────┘  │
│         │                    │                    │               │
│         ▼                    ▼                    ▼               │
│  ┌─────────────┐    ┌─────────────┐    ┌─────────────────┐      │
│  │ Merkle Tree │    │ Keccak256   │    │ Checkpoint Hash │      │
│  │ Functions   │    │ Hashing     │    │ Computation     │      │
│  │ (from       │    │ (from       │    │ (from           │      │
│  │  fuego-core)│    │  fuego-core)│    │  fuego-core)    │      │
│  └─────────────┘    └─────────────┘    └─────────────────┘      │
│                                                                  │
│  These are library functions — both circuits import them         │
│  from fuego-core/src/lib.rs. They don't merge the proofs.        │
└──────────────────────────────────────────────────────────────────┘
```

---

## ⚠️ Critical: Hash Function Compatibility

### The Problem

There are **two different hash functions** in Fuego that affect HEAT proofs:

| Component | Hash Function | Used By | Compatible with Solidity? |
|-----------|---------------|---------|--------------------------|
| `CommitmentIndex` | `cn_fast_hash` (CryptoNote) | Original HEAT scanning | ❌ NO |
| `PoolMerkleTree` | `keccak256` | LP proofs, HEATClaimer | ✅ YES |

### Why This Matters

The `HEATClaimer.sol` contract uses Solidity's `keccak256()` for merkle proof verification:

```solidity
function _verifyMerkleProof(bytes32 leaf, bytes32[] calldata proof, uint256 index, bytes32 root) internal pure returns (bool) {
    bytes32 current = leaf;
    for (uint256 i = 0; i < proof.length; i++) {
        if (index % 2 == 0) {
            current = keccak256(abi.encodePacked(current, proof[i]));  // keccak256!
        } else {
            current = keccak256(abi.encodePacked(proof[i], current));
        }
        index /= 2;
    }
    return current == root;
}
```

If you generate merkle proofs using `CommitmentIndex` (which uses `cn_fast_hash`), the Solidity contract will **reject them** because the hash function doesn't match.

### ✅ Solution: Use PoolMerkleTree

For any HEAT proofs that need to be verified by the `HEATClaimer.sol` contract, use `PoolMerkleTree`:

```cpp
// CORRECT - Use PoolMerkleTree with keccak256
#include "SwapDaemon/PoolAttestation.h"

XfgSwap::PoolMerkleTree tree;
tree.addLeaf(commitmentHash);  // Hash must be keccak256 of preimage
Crypto::Hash root = tree.computeRoot();
std::vector<Crypto::Hash> proof = tree.getProof(leafIndex);

// Verify (matches HEATClaimer.sol)
bool valid = XfgSwap::PoolMerkleTree::verifyProof(leaf, proof, leafIndex, root);
```

### Migration from CommitmentIndex

If you have existing code using `CommitmentIndex`:

```cpp
// ❌ OLD - Incompatible with HEATClaimer.sol
CommitmentIndex index;
index.pushBlock(block);
auto root = index.getRoot();  // Uses cn_fast_hash!
```

```cpp
// ✅ NEW - Compatible with HEATClaimer.sol
PoolMerkleTree tree;
// Add leaves with keccak256 hashing
for (const auto& commitment : commitments) {
    Crypto::Hash leaf = keccak256(commitment.preimage, 56);  // Must match Solidity
    tree.addLeaf(leaf);
}
auto root = tree.computeRoot();  // Uses keccak256 ✓
```

### Integration Test

See `tests/IntegrationTests/perform7_merkle_test.cpp` for a complete test that verifies:
- Merkle root computation
- Proof generation and verification
- **Solidity compatibility** (simulated in C++)

---

## Summary

```
HEAT proof = "These commitments are in real blockchain blocks"
LP pool proof = "This swap/deposit/withdrawal math is correct"

They are as different as:
  - A receipt proving you bought something (HEAT proof)
  - A calculator proving the math on the receipt is right (LP proof)

Both are needed. Both use keccak256 hash functions for Solidity compatibility.
But they cannot be the same proof.

⚠️ IMPORTANT: Always use PoolMerkleTree (keccak256) for HEAT proofs,
   NOT CommitmentIndex (cn_fast_hash)!
```
HEAT proof  = "These commitments are in real blockchain blocks"
LP pool proof = "This swap/deposit/withdrawal math is correct"

They are as different as:
  - A receipt proving you bought something (HEAT proof)
  - A calculator proving the math on the receipt is right (LP proof)

Both are needed. Both use the same hash functions.
But they cannot be the same proof.
```
