---
name: fuego-crypto
description: "Fuego domain expert for cryptographic primitives: Ed25519 keys/signatures, MLSAG ring signatures, Pedersen commitments, MuSig2 aggregated signatures, and hashing."
risk: low
source: user-provided
---

# Fuego Crypto Expert

Domain expert for Fuego cryptographic primitives: signatures, commitments, and hashing.

## Scope

- **Ed25519**: Key generation, signing, verification
- **MLSAG**: Ring signatures for RingCT
- **Pedersen**: Commitments for amount privacy
- **MuSig2**: Aggregated signatures (atomic swaps)
- **Hashing**: CN-FastHash, tree hash
- **Additional**: ChaCha8, DLEQ proofs, Bulletproofs

## Trigger Set

**Should trigger on:**
- "ed25519", "keys", "key generation", "signing"
- "mlsag", "ring signature", "ringct"
- "pedersen", "commitment", "amount privacy"
- "musig2", "musig", "aggregated signature"
- "hash", "chacha", "hashing"
- "crypto", "cryptography", "signature"
- "key image", "DLEQ", "bulletproof"

**Should NOT trigger on:**
- Non-Fuego crypto questions
- Generic cryptography unrelated to Fuego

## Cryptographic Primitives

### Ed25519

**Source:** `src/crypto/crypto.h`, `src/crypto/crypto.cpp`

```cpp
// Generate keypair
void generate_keys(PublicKey& pub, SecretKey& priv);

// Check key validity
bool check_key(const PublicKey& pub);

// Generate signature
void generate_signature(const Hash& msg, const PublicKey& pub, 
                    const SecretKey& priv, Signature& sig);

// Generate key image (for RingCT)
void generate_key_image(const PublicKey& pub, const SecretKey& priv, 
                    KeyImage& ki);
```

### MLSAG Ring Signatures

**Source:** `src/crypto/mlsag.h`, `src/crypto/mlsag.cpp`

Used in RingCT for untraceable transactions:

```cpp
// Generate MLSAG ring signature
bool generate_ring_signature(const vector<const Hash>& ps, 
                      const vector<const PublicKey>& pub,
                      const KeyImage& image,
                      const SecretKey& priv,
                      size_t ring_size,
                      Signature& sig);

// Verify ring signature  
bool check_ring_signature(const Hash& msg,
                     const vector<const PublicKey>& pub,
                     const KeyImage& image,
                     const Signature& sig);
```

### Pedersen Commitments

**Source:** `src/crypto/pedersen.h`, `src/crypto/pedersen.cpp`

Hidden amounts in RingCT:

```cpp
// Create commitment
Commitment commit(const uint64_t amount, 
                const SecretKey& mask);

// Verify commitment
bool verify_commitment(const Commitment& cm, 
                    const SecretKey& mask);

// Verify range proof (amount is positive)
bool verify_range_proof(const Commitment& cm, 
                      const RangeProof& proof);
```

### MuSig2

**Source:** `src/crypto/musig2.h`, `src/crypto/musig2.cpp`

Aggregated signatures for atomic swaps:

```cpp
// Generate partial signature
bool musig_partial_sign(const Hash& msg,
                        const PubKey& agg_pub,
                        const SecKey& priv,
                        const vector<PubKey>& all_pubs,
                        size_t my_index,
                        PartialSig& sig);

// Aggregate signatures
bool musig_aggregate(const vector<PartialSig>& sigs,
                      const Hash& msg,
                      const vector<PubKey>& pubs,
                      Signature& final_sig);
```

### DLEQ Proofs

**Source:** `src/crypto/dleq.h`

Discrete Log Equality proofs for adaptor signatures:

```cpp
// Generate DLEQ proof
DLEQProof generate_dleq(const PublicKey& P, const PublicKey& Q,
                        const SecretKey& p, const SecretKey& q);

// Verify DLEQ proof
bool verify_dleq(const DLEQProof& proof,
                 const PublicKey& P, const PublicKey& Q);
```

### Adaptor Signatures

**Source:** `src/crypto/adaptor.h`

Used for atomic swaps (COMIT protocol):

```cpp
// Create adaptor point T = t*G
PublicKey adaptor_point(const SecretKey& t);

// Create adaptor signature (presignature)
AdaptorSig adaptor_sign(const Hash& msg, const SecretKey& priv,
                        const PublicKey& adaptor_point);

// Adapt presignature with secret t to get valid signature
Signature adapt_signature(const AdaptorSig& presig, const SecretKey& t);
```

### ChaCha8

**Source:** `src/crypto/chacha8.h`

Encryption for deposit secrets:

```cpp
void chacha8(const uint8_t* key, size_t key_len,
           const uint8_t* src, size_t src_len,
           uint64_t nonce, uint8_t* dst);
```

## Usage

```python
from references import CryptoExpert

expert = CryptoExpert(source_dir="/Users/aejt/fuego")

# Generate Ed25519 keys
pub, priv = expert.generate_keys()

# Create ring signature
sig = expert.ring_sign(message, ring_members, private_key)

# Create Pedersen commitment
commitment = expert.commit(amount, blinding)

# Verify commitment
valid = expert.verify_commitment(commitment, amount, blinding)
```

## Key Files

| File | Purpose |
|------|---------|
| `src/crypto/crypto.h` | Ed25519, key image |
| `src/crypto/crypto.cpp` | Ed25519 implementation |
| `src/crypto/mlsag.h` | MLSAG ring signatures |
| `src/crypto/mlsag.cpp` | MLSAG implementation |
| `src/crypto/pedersen.h` | Pedersen commitments |
| `src/crypto/pedersen.cpp` | Pedersen implementation |
| `src/crypto/musig2.h` | MuSig2 (atomic swaps) |
| `src/crypto/musig2.cpp` | MuSig2 implementation |
| `src/crypto/dleq.h` | DLEQ proofs (adaptor sigs) |
| `src/crypto/adaptor.h` | Adaptor signatures |
| `src/crypto/chacha8.h` | ChaCha8 encryption |
| `src/crypto/hash.h` | Hashing functions |

## Utilities

- Use `fuego-rag` for semantic code search
- Use `fuego-codebase-mapper` for file/function search
