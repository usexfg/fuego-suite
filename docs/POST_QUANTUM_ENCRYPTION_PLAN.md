# Quantum-Proof Encryption Plan for Fuego

**Branch:** All implementation work will be done on the `postqntm` branch.

---

## Executive Summary

Implement post-quantum cryptography across Fuego using a **hybrid approach** that combines existing ECC with NIST-standardized post-quantum algorithms, ensuring backward compatibility while providing quantum resistance.

---

## Current Cryptographic Vulnerabilities

### Critical (Shor's Algorithm - Broken by Quantum)
| Component | Algorithm | Location |
|-----------|-----------|----------|
| Transaction signing | Ed25519 | `src/crypto/crypto.cpp` |
| Elderfier consensus | Ed25519 | `src/CryptoNoteCore/ElderfierSignatureDaemon.cpp` |
| P2P trust proofs | Ed25519 | `src/P2p/NetNode.cpp` |
| STARK signature validation | Ed25519 | `xfg-stark/src/winterfell_air.rs` |
| Key exchange (gifts) | Curve25519 ECDH | `src/CryptoNoteCore/TransactionExtra.h` |

### Moderate (Grover's Algorithm - Reduced Security)
| Component | Algorithm | Current Security | Post-Quantum Security |
|-----------|-----------|------------------|----------------------|
| Merkle trees | SHA-256 | 256-bit | 128-bit (acceptable) |
| Commitments | Keccak256 | 256-bit | 128-bit (acceptable) |
| STARK proofs | Blake3 | 256-bit | 128-bit (acceptable) |

**Note:** Hash functions remain adequately secure post-quantum; signature schemes are the priority.

---

## Why Hybrid? Security Rationale

**The hybrid approach requires BOTH signatures to verify.** This provides:

1. **Defense in depth:** If ML-DSA is later broken (new classical attack), Ed25519 still protects. If quantum computers arrive, ML-DSA protects. Attacker must break BOTH.

2. **Conservative cryptography:** Post-quantum algorithms are newer and less battle-tested than Ed25519. Keeping Ed25519 guards against undiscovered weaknesses in ML-DSA.

3. **Backward compatibility:** During transition, classical-only nodes can verify the Ed25519 component while ignoring the PQ part. Hybrid nodes verify both.

4. **No security regression:** The hybrid is strictly MORE secure than classical-only. An attacker with a quantum computer AND a classical break would need both to forge.

**Verification logic:**
```
hybrid_verify(msg, sig, pubkey):
    classical_ok = ed25519_verify(msg, sig.classical, pubkey.classical)
    pq_ok = ml_dsa_verify(msg, sig.pq, pubkey.pq)
    return classical_ok AND pq_ok  // BOTH must pass
```

**Migration timeline:**
- Phase A (now): Hybrid optional, classical accepted
- Phase B (soft fork): Hybrid required for new transactions
- Phase C (hard fork, years later): Classical component deprecated when quantum threat is imminent

---

## Algorithm Selection

### Signatures: ML-DSA (CRYSTALS-Dilithium)
- **Why:** NIST FIPS 204 standardized, fastest lattice signature, mature implementations
- **Parameters:** ML-DSA-65 (NIST Level 3, 128-bit post-quantum security)
- **Sizes:** Public key 1952 bytes, Signature 3293 bytes

### Key Encapsulation: ML-KEM (CRYSTALS-Kyber)
- **Why:** NIST FIPS 203 standardized, efficient for key exchange
- **Parameters:** ML-KEM-768 (NIST Level 3)
- **Sizes:** Public key 1184 bytes, Ciphertext 1088 bytes

### Libraries
- **C++:** liboqs (Open Quantum Safe) - production-ready, actively maintained
- **Rust:** pqcrypto crate or oqs-rs bindings

---

## Implementation Phases

### Phase 1: Foundation Layer
**Goal:** Add post-quantum crypto primitives without changing existing behavior

#### 1.1 Add liboqs Dependency
```
Files:
- CMakeLists.txt (add liboqs find/link)
- external/ (add liboqs as submodule or system dependency)
```

#### 1.2 Create Hybrid Crypto Types
```cpp
// include/CryptoTypes.h - Add new types

struct HybridPublicKey {
    PublicKey classical;      // Existing Ed25519 (32 bytes)
    uint8_t pq[1952];         // ML-DSA-65 public key
};

struct HybridSecretKey {
    SecretKey classical;      // Existing Ed25519 (32 bytes)
    uint8_t pq[4032];         // ML-DSA-65 secret key
};

struct HybridSignature {
    Signature classical;      // Existing Ed25519 (64 bytes)
    uint8_t pq[3293];         // ML-DSA-65 signature
};

struct HybridKemPublicKey {
    PublicKey classical;      // Curve25519 (32 bytes)
    uint8_t pq[1184];         // ML-KEM-768 public key
};
```

#### 1.3 Implement Hybrid Crypto Functions
```
File: src/crypto/crypto_pq.cpp (NEW)

Functions:
- generate_hybrid_keys() → HybridPublicKey, HybridSecretKey
- hybrid_sign(message, secret_key) → HybridSignature
- hybrid_verify(message, signature, public_key) → bool
- hybrid_kem_encapsulate(public_key) → ciphertext, shared_secret
- hybrid_kem_decapsulate(ciphertext, secret_key) → shared_secret
```

**Verification rule:** Both classical AND post-quantum signatures must verify for hybrid signature to be valid.

---

### Phase 2: Transaction Layer Integration
**Goal:** Enable hybrid signatures for transactions

#### 2.1 Extend Transaction Format
```
Files:
- src/CryptoNoteCore/CryptoNoteFormatUtils.cpp
- src/CryptoNoteCore/Transaction.cpp
- src/Serialization/TransactionExtra.h

Changes:
- Add TX_EXTRA_TAG_PQ_SIGNATURE (new tag, e.g., 0x05)
- Store hybrid signatures in transaction extra field
- Backward compatible: old nodes ignore unknown extra tags
```

#### 2.2 Wallet Key Generation
```
Files:
- src/Wallet/WalletGreen.cpp
- src/CryptoNoteCore/Account.cpp

Changes:
- Generate hybrid spend/view key pairs for new wallets
- Store PQ keys alongside classical keys
- Add wallet version field to distinguish key types
```

#### 2.3 Transaction Signing
```
Files:
- src/CryptoNoteCore/CryptoNoteTools.cpp
- src/crypto/crypto.cpp (extend sign_message)

Changes:
- Sign with hybrid keys when available
- Fall back to classical-only for legacy wallets
```

---

### Phase 3: Elderfier Consensus Integration
**Goal:** Quantum-proof the elderfier signature mechanism

#### 3.1 Elderfier Key Registration
```
Files:
- src/CryptoNoteCore/ElderfierSignatureDaemon.cpp
- src/CryptoNoteCore/EldernodeIndex.h

Changes:
- Register hybrid public keys for eldernodes
- Store PQ keys in eldernode registry
- Update attestation format to include hybrid signatures
```

#### 3.2 Consensus Message Signing
```
Files:
- src/CryptoNoteCore/ElderfierSignatureDaemon.cpp:119 (generate_elderfier_signature)
- src/P2p/P2pProtocolDefinitions.h

Changes:
- Sign elderfier attestations with hybrid keys
- Update gossip protocol to handle larger signatures
- Maintain backward compatibility with classical-only nodes during transition
```

---

### Phase 4: STARK Layer (Rust)
**Goal:** Quantum-proof the STARK proof system's signature validation

#### 4.1 Add Rust PQ Dependencies
```
File: xfg-stark/Cargo.toml

Add:
pqcrypto-dilithium = "0.5"
pqcrypto-kyber = "0.8"
# OR
oqs = "0.9"
```

#### 4.2 Update Signature Validation
```
Files:
- xfg-stark/src/winterfell_air.rs (validate_signature function)
- xfg-stark/src/proof_data_schema.rs (EldernodeSignature struct)

Changes:
- Extend EldernodeSignature to include PQ signature bytes
- Update validate_signature() to verify hybrid signatures
- AIR constraints for PQ signature verification (if needed in-circuit)
```

---

### Phase 5: P2P and Network Layer
**Goal:** Secure node-to-node communication

#### 5.1 P2P Handshake
```
Files:
- src/P2p/NetNode.cpp
- src/P2p/P2pProtocolDefinitions.h

Changes:
- Use ML-KEM for session key establishment
- Hybrid key exchange: ECDH + ML-KEM, combine shared secrets
- Update COMMAND_HANDSHAKE payload
```

#### 5.2 Message Authentication
```
Files:
- src/P2p/LevinProtocol.cpp

Changes:
- Optional hybrid signatures on critical P2P messages
- Configurable via node settings
```

---

### Phase 6: Migration and Deprecation
**Goal:** Transition network to quantum-proof mode

#### 6.1 Soft Fork Activation
```
- Block height trigger for mandatory hybrid signatures
- Grace period: accept both classical and hybrid
- After activation: require hybrid for new transactions
```

#### 6.2 Wallet Migration Tool
```
File: src/Wallet/WalletMigration.cpp (NEW)

Functions:
- migrate_to_hybrid_keys() - generate PQ keys for existing wallet
- export_hybrid_keys() - backup new key material
```

---

## File Modification Summary

| File | Phase | Changes |
|------|-------|---------|
| `CMakeLists.txt` | 1 | Add liboqs dependency |
| `include/CryptoTypes.h` | 1 | Add hybrid key/signature types |
| `src/crypto/crypto_pq.cpp` (NEW) | 1 | Hybrid crypto implementation |
| `src/crypto/crypto_pq.h` (NEW) | 1 | Hybrid crypto API |
| `src/CryptoNoteCore/Transaction.cpp` | 2 | Hybrid transaction signing |
| `src/CryptoNoteCore/Account.cpp` | 2 | Hybrid key generation |
| `src/Wallet/WalletGreen.cpp` | 2 | Wallet hybrid key support |
| `src/CryptoNoteCore/ElderfierSignatureDaemon.cpp` | 3 | Hybrid elderfier signatures |
| `src/CryptoNoteCore/EldernodeIndex.h` | 3 | PQ key registry |
| `xfg-stark/Cargo.toml` | 4 | Add pqcrypto dependencies |
| `xfg-stark/src/winterfell_air.rs` | 4 | Hybrid signature validation |
| `xfg-stark/src/proof_data_schema.rs` | 4 | Extended signature struct |
| `src/P2p/NetNode.cpp` | 5 | Hybrid key exchange |
| `src/P2p/P2pProtocolDefinitions.h` | 5 | Updated protocol messages |

---

## Testing Strategy

### Unit Tests
```
tests/crypto/test_pq_crypto.cpp (NEW)
- Test hybrid key generation
- Test hybrid sign/verify
- Test ML-KEM encapsulation/decapsulation
- Test backward compatibility with classical-only

xfg-stark/tests/test_pq_signatures.rs (NEW)
- Test Rust PQ signature verification
- Test hybrid signature in STARK proofs
```

### Integration Tests
```
tests/integration/test_hybrid_transactions.cpp
- Create and verify hybrid-signed transactions
- Test classical node accepting hybrid transactions (ignore PQ)
- Test hybrid node rejecting invalid PQ signatures

tests/integration/test_elderfier_pq.cpp
- Test hybrid elderfier attestation flow
- Test consensus with mixed classical/hybrid nodes
```

### Network Tests
```
- Testnet deployment with hybrid-enabled nodes
- Measure signature verification performance
- Test bandwidth impact of larger signatures
- Migration simulation with mixed node versions
```

---

## Performance Considerations

| Operation | Classical | Hybrid | Overhead |
|-----------|-----------|--------|----------|
| Key generation | ~0.1ms | ~0.5ms | 5x |
| Sign | ~0.1ms | ~0.8ms | 8x |
| Verify | ~0.2ms | ~1.0ms | 5x |
| Signature size | 64 bytes | 3357 bytes | 52x |
| Public key size | 32 bytes | 1984 bytes | 62x |

**Mitigations:**
- Batch verification where possible
- Compress signatures in storage (they compress well)
- Consider FALCON for size-critical paths (smaller sigs, slower)

### Mitigation Implementation Details

#### Batch Verification
```
File: src/crypto/crypto_pq.cpp

// Verify multiple hybrid signatures in one call
bool hybrid_verify_batch(
    const std::vector<std::tuple<Hash, HybridSignature, HybridPublicKey>>& items
);

// ML-DSA supports batching natively - amortizes some costs
// Classical Ed25519 batch verify already exists in crypto.cpp
```

#### Signature Compression
```
Files:
- src/CryptoNoteCore/CryptoNoteFormatUtils.cpp
- src/Serialization/BinaryOutputStreamSerializer.cpp

// ML-DSA signatures compress ~40% with zstd
// Store compressed in DB, decompress for verification
struct CompressedHybridSignature {
    Signature classical;           // 64 bytes (don't compress)
    std::vector<uint8_t> pq_zstd;  // ~2000 bytes compressed
};

// Add to serialization
void serialize(CompressedHybridSignature& sig, ISerializer& s) {
    s(sig.classical, "classical");
    s(sig.pq_zstd, "pq");  // Already compressed blob
}
```

#### FALCON for Size-Critical Paths
```
File: src/crypto/crypto_pq.cpp

// FALCON-512 signatures: 690 bytes (vs 3293 for ML-DSA-65)
// Use for: P2P messages, frequent attestations
// Keep ML-DSA for: Transactions (security > size)

enum PQAlgorithm {
    ML_DSA_65,      // Default, highest security
    FALCON_512,     // Size-optimized, still NIST Level 1
};

bool hybrid_sign(message, secret_key, algo = ML_DSA_65) → HybridSignature;
```

#### Lazy Verification
```
Files:
- src/CryptoNoteCore/Blockchain.cpp
- src/CryptoNoteCore/TransactionPool.cpp

// Don't verify PQ signatures until block confirmation
// Classical signature provides immediate security
// PQ verification runs async in background

struct PendingPQVerification {
    Hash tx_hash;
    HybridSignature sig;
    HybridPublicKey pubkey;
    uint64_t block_height;
};

// Background thread verifies PQ sigs for recent blocks
void verify_pq_signatures_async(std::vector<PendingPQVerification>& pending);
```

#### Memory Pool Optimization
```
File: src/CryptoNoteCore/TransactionPool.cpp

// Cache verified PQ signatures to avoid re-verification
std::unordered_map<Hash, bool> pq_verification_cache;

// Evict when transaction leaves mempool
void on_transaction_removed(const Hash& tx_hash) {
    pq_verification_cache.erase(tx_hash);
}
```

#### Network Bandwidth Optimization
```
Files:
- src/P2p/P2pProtocolDefinitions.h
- src/P2p/LevinProtocol.cpp

// Compact sync mode: only send classical sigs during IBD
// Full sync: send hybrid sigs for recent blocks only
// Nodes can request PQ sigs for specific txs if needed

struct NOTIFY_NEW_BLOCK_COMPACT {
    Block block;
    std::vector<Signature> classical_sigs;  // Classical only
    // PQ sigs requested separately via COMMAND_REQUEST_PQ_SIGS
};

struct COMMAND_REQUEST_PQ_SIGS {
    std::vector<Hash> tx_hashes;
};

struct COMMAND_RESPONSE_PQ_SIGS {
    std::vector<std::pair<Hash, std::vector<uint8_t>>> pq_signatures;
};
```

---

## Risk Assessment

| Risk | Impact | Mitigation |
|------|--------|------------|
| liboqs vulnerability | High | Pin versions, monitor advisories, have fallback |
| Performance degradation | Medium | Benchmark before deployment, optimize hot paths |
| Network partition (version mismatch) | High | Long grace period, clear upgrade path |
| Key size bloat | Medium | Storage optimization, pruning strategy |
| Implementation bugs | High | Extensive testing, security audit before mainnet |

---

## Verification Checklist

- [ ] liboqs compiles and links correctly
- [ ] Hybrid key generation produces valid keys
- [ ] Hybrid signatures verify correctly
- [ ] Classical-only verification still works (backward compat)
- [ ] Transactions with hybrid signatures validate
- [ ] Elderfier attestations work with hybrid keys
- [ ] STARK proofs verify hybrid signatures
- [ ] P2P handshake completes with ML-KEM
- [ ] Wallet migration preserves funds access
- [ ] Performance benchmarks acceptable
- [ ] All existing tests still pass
