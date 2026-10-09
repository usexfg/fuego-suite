# Phases 2 & 3 Fuego Post-Quantum Roadmap — Dev Guide
## Recursive STARK UTXO Consensus & Quantum-Safe Cross-Chain Banking

---

## 1. Executive Context & Threat Transition

Phase 1 ([`HNDL_PHASE1_DEV_GUIDE.md`](file:///home/ar/fuego/docs/developer/HNDL_PHASE1_DEV_GUIDE.md)) deployed hybrid ML-KEM-768 stealth addressing (`TX_EXTRA_PQ_KEM`, `0xD7`) to permanently defeat Harvest Now, Decrypt Later against ledger recipients. 

However, Phase 1 deliberately leaves **spend authorization** on classical Ed25519 signatures. When a Cryptanalytically Relevant Quantum Computer (CRQC) becomes active:
1. **Key Image Forgery**: Classical key images $I = x \cdot H_p(P)$ fail because Shor's algorithm computes $x$ directly from public key $P$. An attacker can double-spend unspent outputs.
2. **Ring Signature Forgery**: Ed25519 MLSAG ring signatures become trivially forgeable without holding the underlying private key.
3. **Commitment Inflation**: Pedersen commitments $C = v \cdot H + m \cdot G$ lose computational binding once $\text{dlog}_G(H)$ is solved by Shor's, enabling arbitrary balance counterfeiting.
4. **Atomic Swap Failure**: MuSig2 and secp256k1 adaptor signatures collapse under discrete logarithm inversion.

Phases 2 and 3 complete the quantum transition of the Fuego protocol:
- **Phase 2 (Consensus Upgrade)**: Replaces RingCT/MLSAG with a **Recursive Hash-Based STARK UTXO Engine** (Winterfell), introducing hash-based nullifiers and infinite anonymity sets.
- **Phase 3 (Ecosystem & Cross-Chain)**: Transitions cross-chain atomic swaps to quantum-safe protocols, migrates legacy UTXOs, and binds the Hearth AMM and CD banking engine into the STARK state machine.

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                       FUEGO 3-PHASE POST-QUANTUM SUITE                      │
├─────────────────────────────────────────────────────────────────────────────┤
│ PHASE 1: LEDGER SECRECY (HNDL DEFENSE)                                      │
│ • ML-KEM-768 Hybrid Stealth Addresses (tx_extra 0xD7)                       │
│ • Immunity to retroactive harvest and recipient deanonymization             │
│ • Soft-fork compatible; live on wire                                        │
├─────────────────────────────────────────────────────────────────────────────┤
│ PHASE 2: CONSENSUS & SPEND IMMUNITY (STARK UTXO)                            │
│ • Hash-based Nullifiers replace Ed25519 Key Images                          │
│ • Recursive Winterfell STARK proofs replace MLSAG Ring Signatures           │
│ • Full UTXO set anonymity (2^32 notes) replacing ring size 8/16             │
│ • Block-level proof aggregation: ~100 KB total proof per block              │
├─────────────────────────────────────────────────────────────────────────────┤
│ PHASE 3: ATOMIC SWAPS & BANKING COMPLETION                                  │
│ • Quantum-Safe Cross-Chain Swaps (Quantum-Resistant Adaptors / Hash-Lock)   │
│ • Hearth AMM & CD Banking state transitions verified inside STARK circuit   │
│ • Legacy Ed25519 UTXO claim/sunset hardfork                                 │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Phase 2: Recursive STARK UTXO Architecture

### The Architectural Rejection of Lattice RingCT
CryptoNote protocols historically considered lattice-based ring signatures (MatRiCT / DualRing-LB). For Fuego, **Lattice RingCT is a fatal dead-end**:
- **Signature Bloat**: Lattice ring signatures require 40 KB to 100 KB *per input*. A standard 2-in 2-out transaction balloons from 2.5 KB to ~150 KB.
- **P2P Bandwidth Collapse**: A block of 100 transactions requires 15 MB of wire payload. Fuego's Levin P2P network (port 10808) would experience severe gossip delays, orphan rates, and node centralization.
- **Restricted Anonymity**: Ring size remains bounded (e.g. 16 or 32 decoys).

### The Solution: Hash-Based Recursive STARKs
Phase 2 deploys an algebraic STARK (Scalable Transparent Argument of Knowledge) based on the **Winterfell** framework (already integrated in Fuego's ecosystem via `xfg-stark`):
- **Pure Hash Security**: Relies strictly on collision-resistant hash functions (Rescue-Prime / Poseidon / Blake3). Grover's quantum search algorithm reduces 256-bit hashes to 128-bit quantum security, which remains unbreakable.
- **Global Anonymity Set**: Membership is proven via a Merkle tree of *all* historical commitment notes ($2^{32}$ leaves), rendering ring size obsolete.
- **Block Aggregation**: Miners aggregate $N$ transaction proofs into a single recursive block proof. On-chain proof overhead per block is fixed at **~100 KB regardless of transaction count**.

---

### Phase 2 State Primitives & Math

#### 1. Note Commitment
Each UTXO is a hash commitment stored in an append-only Merkle tree:
$$C = \text{Poseidon}(sk_{\text{spend}}, v, \tau, \rho)$$
- $sk_{\text{spend}}$: Private spend key (field element).
- $v$: Amount (64-bit integer, satoshis of XFG or atomic units of HEAT/CD).
- $\tau$: Asset type tag (`TERM_REGULAR = 0` for XFG, `HEAT_TERM = 0xFFFFFFFF` for HEAT, or CD epoch duration).
- $\rho$: Blinding salt derived from Phase 1's hybrid shared secret $D_{\text{hybrid}}$.

#### 2. Quantum-Safe Nullifier
Double-spend prevention without revealing which note was spent:
$$N = \text{Poseidon}(sk_{\text{spend}}, \text{leaf\_index})$$
- Deterministic: Spending the same leaf index twice yields the exact same $N$.
- Unforgeable: Deriving $N$ requires knowledge of $sk_{\text{spend}}$.
- Unlinkable: Without $sk_{\text{spend}}$, an observer cannot associate $N$ with commitment $C$ or leaf index.

#### 3. Transaction STARK Circuit Constraints
The Winterfell AIR (Algebraic Intermediate Representation) enforces five invariants:
1. **Merkle Membership**: The input commitment $C_{\text{in}}$ exists at position $\text{leaf\_index}$ in the commitment accumulator root $Root_k$.
2. **Nullifier Integrity**: $N = \text{Poseidon}(sk_{\text{spend}}, \text{leaf\_index})$.
3. **Conservation of Balance**: For each asset tag $\tau$:
   $$\sum v_{\text{in}} = \sum v_{\text{out}} + \text{fee}_\tau$$
4. **Range Integrity**: All output amounts satisfy $0 \le v_{\text{out}} < 2^{64}$.
5. **Ownership Validity**: Proof knowledge of $sk_{\text{spend}}$ matching $C_{\text{in}}$.

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                       STARK TRANSACTION PROOF AIR                           │
├─────────────────────────────────────────────────────────────────────────────┤
│ PRIVATE WITNESS (Kept by User):                                             │
│   • sk_spend, v_in, tau_in, rho_in                                          │
│   • leaf_index, Merkle authentication path to Root_k                        │
│   • v_out, tau_out, rho_out                                                 │
├─────────────────────────────────────────────────────────────────────────────┤
│ PUBLIC INPUTS (Committed on Ledger):                                        │
│   • Merkle Root (Root_k)                                                    │
│   • Nullifier (N)                                                           │
│   • Output Commitments (C_out_1, C_out_2, ...)                              │
│   • Transaction Fee (fee)                                                   │
├─────────────────────────────────────────────────────────────────────────────┤
│ VERIFICATION:                                                               │
│   STARK_Verify(AIR_Params, PublicInputs, Proof) == TRUE                     │
│   Ledger records N in spent set; inserts C_out into note tree.              │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

### Block-Level Recursive Aggregation

To eliminate transaction proof bloat:
1. **Client Phase**: Wallet generates a local STARK proof $\pi_{\text{tx}}$ (~45 KB) proving valid spending of its inputs.
2. **Mempool**: Miners receive transactions with $\pi_{\text{tx}}$ via P2P.
3. **Block Aggregation**: The miner executes a recursive STARK verifier circuit that verifies all $M$ transaction proofs in the block and outputs a single aggregated proof:
   $$\pi_{\text{block}} = \text{Aggregate}(\pi_1, \pi_2, \dots, \pi_M)$$
4. **Consensus Wire**: The mined block contains only:
   - Header with $PrevStateRoot$, $NewStateRoot$, and $NullifierRoot$.
   - List of consumed nullifiers ($M \times 32$ bytes).
   - List of new output commitments ($2M \times 32$ bytes).
   - The single aggregated proof $\pi_{\text{block}}$ (~100 KB).

#### Wire Weight Comparison per 200-Transaction Block:
- **Lattice RingCT (MatRiCT)**: $200 \times 150\,\text{KB} \approx \mathbf{30\,\text{MB}}$ (Unusable).
- **Current CryptoNote (MLSAG v10)**: $200 \times 2.5\,\text{KB} \approx \mathbf{500\,\text{KB}}$.
- **Fuego Recursive STARK**: $100\,\text{KB (proof)} + (200 \times 32\,\text{B}) + (400 \times 32\,\text{B}) \approx \mathbf{120\,\text{KB}}$ (**4x more compact than current RingCT!**).

---

## 3. Phase 3: Ecosystem Transition & Atomic Swaps

### 1. Quantum-Safe Cross-Chain Atomic Swaps

Fuego's atomic swap daemon ([`src/SwapDaemon/`](file:///home/ar/fuego/src/SwapDaemon/)) currently relies on Schnorr/secp256k1 adaptor signatures and MuSig2. Under Shor's algorithm, discrete logarithm tweak revelations fail.

#### The Phase 3 Dual Adaptation Path:
1. **Hash-Time-Locked Contracts (HTLCs)**:
   - For chains lacking post-quantum signature support (e.g. Bitcoin, EVM chains):
     - Revert to clean, constant-time SHA3-256 / Keccak-256 preimage locks:
       $$\text{Lock} = \text{Keccak-256}(S)$$
     - Quantum resistance: Inverting a 256-bit hash with Grover's algorithm requires $2^{128}$ quantum operations, which is physically impossible.
2. **Lattice-Based Adaptor Signatures**:
   - For native cross-chain interactions between post-quantum chains:
     - Implement **ML-DSA** (FIPS 204, CRYSTALS-Dilithium) adaptor signatures.
     - Hardness assumption: Module Learning With Errors (M-LWE) and Module Short Integer Solution (M-SIS).

#### Fee Pool Routing Invariance:
Swap fees remain strictly governed by Fuego's canonical tokenomics:
- Atomic swap fee: 2% (1% initiation + 1% claim).
- Protocol split: **69% CD Yield Pool / 11% Bonus Vault / 20% Treasury Reserve**.
- Routing is enforced deterministically by state transitions inside the STARK execution claim.

---

### 2. Hearth AMM & CD Banking Integration

The Hearth Exchange (v12+ on-chain AMM + limit-order overlay) transitions into the STARK state tree:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    HEARTH AMM STARK STATE ACCUMULATOR                       │
├─────────────────────────────────────────────────────────────────────────────┤
│ State Leaf:                                                                 │
│   • Reserve_XFG (8M8 canonical supply, 7 decimals)                          │
│   • Reserve_HEAT (Inflation-pegged flatcoin)                                │
│   • cdHearthFeeAccumulator (70% taker fee accumulation)                     │
│   • Treasury LP Manager paired balances                                     │
├─────────────────────────────────────────────────────────────────────────────┤
│ In-Circuit Swap Transition:                                                 │
│   • Constant-Product Invariant: (R_xfg + dx) * (R_heat - dy) >= k           │
│   • Taker Fee Deduction: 100 bps (1%)                                       │
│   • CD Accumulator Credit: 70% of fee                                       │
│   • Maker Rebate / LP Share Credit: 30% of fee                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

- **Limit Orders**: Resting limit orders (`TransactionExtraLimitDeposit`) are hashed into a commitment leaf. When spot price crosses the limit, the block proof validates order execution against pool reserves.
- **CD Epochs**: At epoch boundaries, the `cdHearthFeeAccumulator` credits the `CD_APY_POOL`, paying term-locked depositors according to active lock durations (6 to 72 epochs) with zero discrete log exposure.

---

### 3. Legacy Migration & Deprecation Gate

To ensure no quantum vulnerability remains on the network, legacy Ed25519 outputs must be sunset:

```
BlockMajorVersion 12 (Activation)
       │
       ▼
   Phase 1 Active (ML-KEM Stealth Tags, 0xD7)
   Phase 2 Active (STARK engine runs concurrently with RingCT)
       │
       ▼  [Migration Grace Window: 100,000 Blocks (~138 days)]
       │
       ├── Legacy UTXO Migration Circuit:
       │   Users prove ownership of Ed25519 one-time key P_i
       │   Old note nullified on classical chain; new STARK commitment minted
       │
       ▼
BlockMajorVersion 13 (Sunset Hardfork)
       │
       ▼
   • Ed25519 RingCT inputs permanently disabled (rejected by consensus)
   • 100% of network transactions run exclusively via STARK UTXO
   • Classical view/spend keys fully retired
```

---

## 4. Engineering & File Layout Blueprint

### New Core Subsystems

```
fuego-suite/
├── src/crypto/
│   ├── ml_kem.h / .cpp           # Phase 1: ML-KEM-768 KEM wrapper
│   ├── ml_dsa.h / .cpp           # Phase 3: ML-DSA (Dilithium) signature wrapper
│   └── stark_verifier.h / .cpp   # C++ FFI wrapper calling Rust Winterfell verifier
├── src/CryptoNoteCore/
│   ├── StarkUtxoIndex.h / .cpp   # Merkle tree accumulator for STARK notes
│   ├── StarkNullifierSet.h / .cpp# Set of spent hash nullifiers (sled/LMDB backed)
│   ├── TransactionValidatorV12.cpp# Validates block STARK proofs
│   └── Blockchain.cpp            # State root commit in block header
├── xfg-stark/                    # Rust STARK Proof Engine (Winterfell)
│   ├── src/air.rs                # Algebraic Intermediate Representation constraints
│   ├── src/prover.rs             # Client transaction prover
│   ├── src/aggregator.rs         # Block-level recursive STARK aggregator
│   └── src/ffi.rs                # C FFI bindings (exporting stark_verify_block)
└── src/SwapDaemon/
    ├── QuantumHtlc.h / .cpp      # SHA3-256 / Keccak quantum-safe HTLCs
    └── DilithiumAdaptor.h / .cpp # Post-quantum adaptor signatures
```

### C++ FFI Integration (`src/crypto/stark_verifier.h`)

```cpp
#pragma once

#include <cstdint>
#include <cstddef>

namespace Crypto {

struct StarkBlockPublicInputs {
  uint64_t block_height;
  uint8_t prev_state_root[32];
  uint8_t new_state_root[32];
  uint8_t nullifier_root[32];
  uint32_t nullifier_count;
  uint32_t output_count;
  uint64_t total_fee_xfg;
};

extern "C" {
  // Verifies an aggregated block STARK proof via Rust Winterfell
  bool winterfell_verify_block_proof(
      const uint8_t* proof_bytes,
      size_t proof_len,
      const StarkBlockPublicInputs* public_inputs);
}

class StarkVerifier {
public:
  static bool verifyBlock(
      const std::vector<uint8_t>& proof,
      const StarkBlockPublicInputs& inputs) 
  {
    return winterfell_verify_block_proof(proof.data(), proof.size(), &inputs);
  }
};

} // namespace Crypto
```

---

## 5. Performance & Resource Specifications

| Metric | Current CryptoNote (v10) | Phase 2 STARK Engine | Delta / Improvement |
|---|---|---|---|
| **Transaction Anonymity** | Ring Size 8–16 | Full UTXO Set ($2^{32}$) | **$268,000,000\times$ Anonymity Set** |
| **Transaction Client Proof** | ~2 ms (Ed25519 scalarmult) | ~1.2 s (Local Winterfell proof) | Acceptable for client desktop/mobile |
| **Block Verification Time** | 150 ms (Validating 200 MLSAGs) | 18 ms (Single STARK verify) | **8.3x Faster Block Validation** |
| **Block Wire Size (200 txs)**| ~500 KB | ~120 KB | **76% Reduction in Block Bloat** |
| **Quantum Resistance** | 0 bits (Broken by Shor's) | 128 bits post-quantum (Grover) | **Immune to Quantum Decryption & Forgery** |

---

## 6. Implementation Milestones for Tuke

1. **Milestone 2.1: Winterfell FFI Bridge**:
   - Establish Rust `xfg-stark` static library linking into Fuego C++ daemon (`libxfg_stark.a`).
   - Implement `StarkUtxoIndex` and verify Merkle root updates against test vectors.
2. **Milestone 2.2: Standalone STARK Test Harness**:
   - Write single-input, single-output transaction proof test using Goldilocks/Rescue-Prime constraints.
   - Verify balance conservation and nullifier collision rejection.
3. **Milestone 2.3: Block Aggregator**:
   - Implement miner recursive STARK aggregation for 10-transaction blocks.
4. **Milestone 3.1: Quantum Atomic Swaps**:
   - Replace secp256k1 adaptors with SHA3-256 preimages in `SwapDaemon`.
5. **Milestone 3.2: Mainnet Sunset Activation**:
   - Schedule BlockMajorVersion 13 hardfork for permanent retirement of legacy Ed25519 inputs.
