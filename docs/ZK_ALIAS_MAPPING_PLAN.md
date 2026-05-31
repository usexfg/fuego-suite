# Cross-Chain Alias ZK Mapping — Technical Plan

## 1. Problem Statement

Fuego aliases (`@alice`) provide human-readable identifiers tied to on-chain XFG addresses. For cross-chain atomic swaps, takers need to know the maker's counterparty chain address (ETH, SOL, XMR, BCH) to lock funds. Today, makers must manually share counterparty addresses during the swap handshake, which:

1. **Leaks cross-chain identity** — linking `@alice` to `0xAbC...` on Ethereum creates a public correlation between privacy-preserving XFG and transparent chains.
2. **Degrades UX** — the taker must copy/paste an address from the maker during the swap negotiation.

## 2. Goal

Allow a Fuego alias to **securely map** to counterparty chain addresses such that:

- The mapping is **private** — no on-chain observer can learn which ETH/SOL/BCH address belongs to `@alice`.
- The mapping is **verifiable** — the swap daemon can prove (to the taker) that a given counterparty address belongs to the alias holder, without revealing the mapping to anyone else.
- The mapping is **updatable** — the alias holder can rotate counterparty addresses without re-registering the alias.

## 3. Architecture Overview

```
┌──────────────────────────────────────────────────┐
│                 Fuego Blockchain                  │
│                                                   │
│  Alias Registry:                                  │
│    @alice → XFG_address + commitment(addr_map)    │
│                                                   │
│  commitment = Poseidon(ETH_addr || SOL_addr ||    │
│               XMR_addr || BCH_addr || salt)       │
└───────────────────────┬──────────────────────────┘
                        │
         ┌──────────────┴──────────────┐
         │     ZK Proof (off-chain)     │
         │                              │
         │  Statement: "I know addr_map │
         │  such that commitment ==     │
         │  Poseidon(addr_map || salt)   │
         │  AND addr_map[pair] == X"     │
         │                              │
         │  Witness: addr_map, salt      │
         │  Public: commitment, pair, X  │
         └──────────────────────────────┘
```

### Flow

1. **Registration**: Maker registers alias with a Poseidon hash commitment of their address map (one address per chain pair) plus a random salt. The commitment is stored on-chain as part of the alias extra data.

2. **Swap Handshake**: When a taker accepts a soft order, the maker's daemon generates a ZK proof that the counterparty address for the requested pair is contained in the committed address map, and sends it along with the address.

3. **Verification**: The taker's daemon verifies the proof against the on-chain commitment. If valid, it knows the address is genuinely linked to the alias without learning any other chain addresses.

4. **Rotation**: The maker submits a new alias transaction with an updated commitment. Old proofs become invalid. No linkage between old and new counterparty addresses.

## 4. ZK Circuit Design

### 4.1 Inputs

| Type | Name | Description |
|------|------|-------------|
| Public | `commitment` | On-chain Poseidon hash from alias registry |
| Public | `pair` | Chain pair index (0=SOL, 1=ETH, 2=XMR, 3=BCH) |
| Public | `revealed_addr` | The counterparty address being disclosed |
| Private | `addr_sol` | SOL address (32 bytes) |
| Private | `addr_eth` | ETH address (20 bytes, zero-padded to 32) |
| Private | `addr_xmr` | XMR address (95 bytes, split into field elements) |
| Private | `addr_bch` | BCH address (variable, padded to 32) |
| Private | `salt` | 32-byte random salt |

### 4.2 Constraints

```
1. recomputed = Poseidon(addr_sol || addr_eth || addr_xmr || addr_bch || salt)
2. commitment == recomputed
3. IF pair == 0: revealed_addr == addr_sol
   IF pair == 1: revealed_addr == addr_eth
   IF pair == 2: revealed_addr == addr_xmr
   IF pair == 3: revealed_addr == addr_bch
```

### 4.3 Field Element Encoding

- SOL addresses (base58, 32 bytes): encode directly as 1 field element (BN254 field > 2^254, fits 32 bytes).
- ETH addresses (20 bytes): zero-pad to 32 bytes, encode as 1 field element.
- XMR addresses (95 bytes): split into 3 field elements (32+32+31 bytes).
- BCH addresses (CashAddr, ~42 chars): encode as 2 field elements.

### 4.4 Hash Function

**Poseidon** — ZK-friendly algebraic hash. ~250 R1CS constraints per hash vs ~27,000 for SHA-256. Widely supported across proving systems.

- Width: 5 inputs (4 address slots + salt) → 1 output
- For XMR (3 field elements), pre-hash XMR address with Poseidon before the outer hash

### 4.5 Estimated Circuit Size

| Component | Constraints |
|-----------|------------|
| Poseidon hash (outer, 5 inputs) | ~1,250 |
| Poseidon pre-hash (XMR, 3 inputs) | ~750 |
| Selector mux (pair → address) | ~128 |
| Equality check | ~1 |
| **Total** | **~2,130** |

Proving time: <100ms on consumer hardware with Groth16.
Verification: ~1ms (3 pairings).
Proof size: 128 bytes (Groth16).

## 5. Library Selection

| Library | Language | Proving System | Pros | Cons |
|---------|----------|---------------|------|------|
| **arkworks** | Rust | Groth16, Marlin | Mature, fast, well-audited Poseidon impl | Requires Rust build integration |
| **circom + snarkjs** | JS/Wasm | Groth16, PLONK | Declarative circuit DSL, huge community | JS dependency, trusted setup per circuit |
| **halo2** | Rust | PLONK (no trusted setup) | No trusted setup, recursive | Larger proofs (~2KB), newer |
| **bellman** | Rust | Groth16 | Used by Zcash, battle-tested | Lower-level API |

**Recommendation**: **arkworks** with Groth16. Reasons:
- Fuego already has Rust in its toolchain (Winterfell ZK prover for HEAT burn proofs)
- Groth16 produces the smallest proofs (128 bytes) — fits in a P2P swap message
- arkworks has a production-quality Poseidon implementation (`ark-crypto-primitives`)
- Trusted setup is acceptable since the circuit is small and fixed

## 6. Integration Points

### 6.1 Alias Registry (C++)

**File**: `src/CryptoNoteCore/AliasManager.cpp`

Current alias registration stores `{name, address, signature}`. Extend the extra data field to include an optional `zk_commitment` (32 bytes, Poseidon hash). Backward compatible — aliases without commitments continue to work.

```cpp
struct AliasRegistration {
  std::string name;
  std::string address;
  Crypto::Signature signature;
  Crypto::Hash zkCommitment;  // optional: Poseidon(addr_map || salt)
  bool hasZkCommitment = false;
};
```

### 6.2 Swap Peer Protocol (C++)

**File**: `src/SwapDaemon/SwapPeerProtocol.h`

Add a new peer message type:

```cpp
ZK_ALIAS_PROOF = 20,  // alias holder proves counterparty address
```

Message payload:
```
{
  "alias": "@alice",
  "pair": 1,
  "ctr_address": "0xAbC...",
  "proof": "<128 bytes hex>",
  "public_inputs": ["<commitment>", "<pair>", "<addr_hash>"]
}
```

### 6.3 SwapDaemon (C++)

**File**: `src/SwapDaemon/SwapDaemon.cpp`

In the key exchange phase (`ADAPTOR_KEYS_EXCHANGED`):
1. Maker sends `ZK_ALIAS_PROOF` with their counterparty address + proof
2. Taker verifies:
   a. Fetch alias commitment from blockchain
   b. Run Groth16 verifier (C++ binding to arkworks via FFI or precompiled verifier)
   c. If valid, store `ctrAddress` in `SwapParams`

### 6.4 ZK Verifier (Rust → C FFI)

Create a standalone Rust library that exposes a C-compatible API:

```rust
// lib.rs
#[no_mangle]
pub extern "C" fn verify_alias_proof(
    commitment: *const u8,     // 32 bytes
    pair: u8,
    revealed_addr: *const u8,  // 32 bytes
    proof: *const u8,          // 128 bytes
    vk: *const u8,             // verification key
    vk_len: usize,
) -> bool { ... }
```

Build as a static library (`libzk_alias_verifier.a`) and link into fuegod.

### 6.5 Wallet RPC

**File**: `src/Wallet/WalletRpcServer.cpp`

New endpoints:
- `set_alias_addresses` — set counterparty addresses + generate commitment
- `prove_alias_address` — generate ZK proof for a specific pair
- `get_alias_commitment` — return the current commitment

### 6.6 TUI (Go)

**File**: `swapxfg/app/tui.go`

New commands:
- `alias set-addresses <sol_addr> <eth_addr> <xmr_addr> <bch_addr>` — calls wallet RPC to set addresses + register commitment
- `alias show` — display current alias and commitment status

The proof generation/verification is handled by the daemon; the TUI only triggers it.

## 7. Security Analysis

### 7.1 Threat Model

| Threat | Mitigation |
|--------|-----------|
| Attacker replays old proof with outdated address | Commitment changes on rotation; old proofs fail verification |
| Attacker brute-forces address from commitment | Poseidon with 256-bit salt; 2^256 search space |
| Verifier learns other chain addresses | ZK proof reveals only the selected pair's address |
| Malicious prover uses wrong commitment | Verifier fetches commitment from blockchain (trustless) |
| Trusted setup compromise (Groth16) | Circuit is small + fixed; can run a public ceremony or migrate to PLONK later |

### 7.2 XMR Privacy Considerations

XMR uses stealth addresses; revealing a public address for swap purposes does not compromise transaction privacy since XMR transactions use one-time addresses derived from the public address. The ZK mapping does not create any on-chain XMR linkage.

## 8. Implementation Phases

### Phase A: Foundation (estimated: 2-3 weeks)

1. Define Poseidon circuit in arkworks (Rust)
2. Build C FFI wrapper for verifier
3. Integrate static library into CMake build
4. Add `zkCommitment` field to alias registration

### Phase B: Wallet Integration (estimated: 1-2 weeks)

1. Implement `set_alias_addresses` wallet RPC
2. Implement `prove_alias_address` wallet RPC (prover side)
3. Store address map + salt encrypted in wallet file

### Phase C: Swap Protocol Integration (estimated: 2-3 weeks)

1. Add `ZK_ALIAS_PROOF` peer message type
2. Integrate proof verification in SwapDaemon key exchange phase
3. Fallback: if no ZK commitment, manual address exchange (backward compat)

### Phase D: TUI + Testing (estimated: 1 week)

1. Add `alias set-addresses` and `alias show` TUI commands
2. Integration tests: register alias with commitment, run swap, verify proof
3. Edge cases: rotation, expired aliases, missing commitments

**Total estimated effort: 6-9 weeks**

## 9. Future Extensions

- **Recursive proofs**: If migrating to PLONK/Halo2, proofs can be recursively composed to prove membership in a batch of aliases (for market makers with multiple aliases).
- **Selective disclosure**: Extend the circuit to prove properties about the address (e.g., "my ETH address has >X balance") without revealing the address itself, using bridge oracles.
- **Cross-chain alias resolution**: Once ZK mapping is established, a taker could resolve `@alice` → counterparty address entirely through the daemon, eliminating manual address exchange from the UX entirely.
