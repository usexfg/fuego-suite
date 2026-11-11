# Phase 1 Fuego HNDL Defense — Dev Guide
## Post-Quantum Hybrid Stealth Addressing & Ledger Secrecy

---

## 1. Threat Model & Strategic Scope

### The HNDL (Harvest Now, Decrypt Later) Vector
Adversaries harvest and archive Fuego blockchain data today. Under classical CryptoNote cryptography, transaction outputs rely on Diffie-Hellman stealth addresses over Ed25519:
$$R = r \cdot G, \quad D = 8 \cdot r \cdot A, \quad P = H_s(D, \text{index}) \cdot G + B$$

When a Cryptanalytically Relevant Quantum Computer (CRQC) running Shor's algorithm becomes operational:
1. The adversary extracts the private view key $a = \text{dlog}_G(A)$ or ephemeral scalar $r = \text{dlog}_G(R)$ in polynomial time.
2. The shared secret $D$ is recomputed for every historical transaction on the ledger.
3. Every recipient $B$, stealth output $P$, and transaction graph is retroactively de-anonymized and linked back to real identities.
4. Any confidential amounts masked under derivations of $D$ are decrypted.

### Strategic Goal of Phase 1
Phase 1 eliminates the HNDL attack surface on the current ledger **without waiting for a consensus-shattering rewrite of RingCT**. 

By hybridizing Ed25519 Diffie-Hellman with NIST FIPS 203 **ML-KEM-768** (CRYSTALS-Kyber-768), transaction outputs recorded today remain cryptographically immune to retroactive quantum de-anonymization. Even if Ed25519 is broken in the future, the adversary cannot recover the hybrid shared secret without also breaking Module-Lattice cryptography.

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           PHASE 1 HYBRID MODEL                              │
│                                                                             │
│   Classical View Key (A) ──┐                                                │
│                            ├──► Classical Derivation (D_ec) ──┐             │
│   Ephemeral Scalar (r)   ──┘                                  │             │
│                                                               ├──► KDF ──► D_hybrid
│   PQ Public Key (pk_kem) ──┐                                  │             │
│                            ├──► Quantum Shared Secret (ss) ───┘             │
│   Encapsulation (c_kem)  ──┘                                                │
│                                                                             │
│   Result: Breaking Ed25519 via Shor's yields ONLY D_ec.                     │
│   Without ss, D_hybrid cannot be computed.                                  │
│   Output P cannot be linked to Spend Key B. Transaction graph stays sealed. │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Cryptographic Specification

### Primitives & Standards
- **Classical Elliptic Curve**: Ed25519 ($G$, group order $l \approx 2^{252}$).
- **Post-Quantum KEM**: ML-KEM-768 (NIST FIPS 203, Category 3, 128-bit post-quantum security against all known quantum lattice attacks).
  - Public Key size ($pk_{\text{kem}}$): 1,184 bytes.
  - Ciphertext size ($c_{\text{kem}}$): 1,088 bytes.
  - Shared Secret size ($ss_{\text{kem}}$): 32 bytes.
- **KDF**: Keccak-256 (canonical CryptoNote hash function, quantum pre-image resistance of 128 bits via Grover).

### Key Material & Extended Address Format

A Fuego Quantum-Safe Address (`fire_pq...`) contains three keys:
1. Public Spend Key: $B = b \cdot G$ (32 bytes)
2. Public View Key: $A = a \cdot G$ (32 bytes)
3. Public PQ KEM Key: $pk_{\text{kem}}$ (1,184 bytes)

```
Classical Address:  [Flags (1B)][SpendKey B (32B)][ViewKey A (32B)][Checksum (4B)] = ~69 bytes (Base58: "fire...")
PQ Hybrid Address:  [Flags (1B)][SpendKey B (32B)][ViewKey A (32B)][pk_kem (1184B)][Checksum (4B)] = ~1253 bytes (Base58: "fire_pq...")
```

### Derivation Algorithm

#### Sender Side (Encapsulation & Construction)
Given recipient's $(A, B, pk_{\text{kem}})$:
1. Generate ephemeral Ed25519 scalar $r \in \mathbb{Z}_l$.
2. Compute classical transaction public key:
   $$R = r \cdot G$$
3. Compute classical scalar derivation:
   $$D_{\text{ec}} = 8 \cdot r \cdot A$$
4. Execute ML-KEM-768 encapsulation:
   $$(c_{\text{kem}}, ss_{\text{kem}}) \leftarrow \text{ML-KEM-768.Encaps}(pk_{\text{kem}})$$
5. Derive the hybrid shared secret using Keccak-256 domain-separated mixing:
   $$D_{\text{hybrid}} = \text{Keccak-256}(D_{\text{ec}} \mathbin{\Vert} ss_{\text{kem}} \mathbin{\Vert} \text{"FUEGO_PQ_HYBRID_STEALTH_V1"})$$
6. Compute view tag (1-byte scanning acceleration filter):
   $$\text{view\_tag} = \text{Keccak-256}(D_{\text{hybrid}} \mathbin{\Vert} \text{"VIEW_TAG"})[0]$$
7. Derive output scalar for output index $i$:
   $$h_i = H_s(D_{\text{hybrid}} \mathbin{\Vert} \text{varint}(i))$$
8. Derive the one-time stealth destination address:
   $$P_i = h_i \cdot G + B$$
9. Publish in transaction:
   - Output locked to $P_i$.
   - $R$ stored in `tx_extra` tag `TX_EXTRA_TAG_PUBKEY` (`0x01`).
   - $c_{\text{kem}}$ and `view_tag` stored in `tx_extra` tag `TX_EXTRA_PQ_KEM` (`0xD7`).

#### Recipient Side (Decapsulation & Scanning)
Given recipient's private keys $(a, b, sk_{\text{kem}})$:
1. Parse $R$ from `TX_EXTRA_TAG_PUBKEY`.
2. Parse $c_{\text{kem}}$ and `view_tag` from `TX_EXTRA_PQ_KEM`.
3. Compute classical derivation:
   $$D_{\text{ec}} = 8 \cdot a \cdot R$$
4. Decapsulate the quantum shared secret:
   $$ss_{\text{kem}} = \text{ML-KEM-768.Decaps}(c_{\text{kem}}, sk_{\text{kem}})$$
5. Reconstruct hybrid secret:
   $$D_{\text{hybrid}} = \text{Keccak-256}(D_{\text{ec}} \mathbin{\Vert} ss_{\text{kem}} \mathbin{\Vert} \text{"FUEGO_PQ_HYBRID_STEALTH_V1"})$$
6. Fast filter: check if computed view tag matches published view tag. If mismatch, skip.
7. Compute scalar:
   $$h_i = H_s(D_{\text{hybrid}} \mathbin{\Vert} \text{varint}(i))$$
8. Test ownership:
   $$P_i \stackrel{?}{=} h_i \cdot G + B$$
9. If equal, compute one-time private spend key:
   $$x_i = h_i + b \pmod l$$
   Notice that $x_i \cdot G = (h_i + b) \cdot G = h_i \cdot G + B = P_i$.

---

## 3. Wire Format & Transaction Extra Architecture

### Tag Assignment
In [`src/CryptoNoteCore/TransactionExtra.h`](file:///home/ar/fuego/src/CryptoNoteCore/TransactionExtra.h):
```cpp
#define TX_EXTRA_PQ_KEM                     0xD7  // v12+ Post-Quantum Hybrid Stealth Address Tag
```

### Binary Serialization Layout
The `TX_EXTRA_PQ_KEM` payload contains:
- `version` (1 byte, default `0x01`)
- `output_index` (varint)
- `view_tag` (1 byte)
- `ciphertext` (1,088 bytes fixed-size ML-KEM-768 ciphertext)

```
┌──────────────┬──────────────────┬──────────────┬───────────────────────────────┐
│ Tag (1B)     │ Length (varint)  │ Version (1B) │ View Tag (1B)                 │
│ 0xD7         │ 1090             │ 0x01         │ 0x4A                          │
├──────────────┴──────────────────┴──────────────┴───────────────────────────────┤
│ ML-KEM-768 Ciphertext (1,088 bytes)                                            │
│ [c_kem bytes 0 .. 1087]                                                        │
└────────────────────────────────────────────────────────────────────────────────┘
```

Total size per PQ output: **1,092 bytes**.

### Wire Budget & Transaction Weight
- A standard classical 2-in 2-out Fuego transaction: ~2.5 KB.
- A hybrid PQ 2-in 2-out Fuego transaction: ~2.5 KB + 2,184 B $\approx$ **4.7 KB**.
- **Bandwidth overhead**: ~1.8x. This is exceptionally efficient compared to Lattice RingCT (which balloons transactions to 80–200 KB).
- **Block size impact**: At standard block limits (100 KB–300 KB dynamic), a 4.7 KB transaction fits comfortably in standard Levin P2P propagation without causing network fragmentation.

---

## 4. C++ Implementation Architecture

### File Modifications & Layout

```
fuego-suite/
├── src/crypto/
│   ├── ml_kem.h                # ML-KEM-768 wrapper (NIST FIPS 203)
│   ├── ml_kem.cpp              # C++ implementation / liboqs or clean reference
│   ├── crypto.h                # Export generate_hybrid_derivation()
│   └── crypto.cpp              # Implementation of D_hybrid and view tag
├── src/CryptoNoteCore/
│   ├── TransactionExtra.h      # Add TX_EXTRA_PQ_KEM (0xD7) and TransactionExtraPqKem struct
│   ├── TransactionExtra.cpp    # Parser and serializer for 0xD7 tag
│   └── CryptoNoteFormatUtils.cpp # Construct transaction outputs with PQ KEM
└── src/Wallet/
    └── WalletGreen.cpp         # Wallet scanner with ML-KEM decapsulation
```

### C++ Struct Definitions

#### 1. `TransactionExtraPqKem` ([`src/CryptoNoteCore/TransactionExtra.h`](file:///home/ar/fuego/src/CryptoNoteCore/TransactionExtra.h))
```cpp
namespace CryptoNote {

struct TransactionExtraPqKem {
  static constexpr uint8_t CURRENT_VERSION = 1;
  static constexpr size_t CIPHERTEXT_SIZE = 1088;

  uint8_t version = CURRENT_VERSION;
  uint8_t view_tag = 0;
  uint32_t output_index = 0;
  std::vector<uint8_t> ciphertext; // Exactly 1088 bytes

  template <template <bool> class Archive>
  bool serialize(Archive<true>& ar) {
    ar(version, "version");
    ar(view_tag, "view_tag");
    ar(output_index, "output_index");
    uint64_t sz = ciphertext.size();
    ar(sz, "sz");
    if (sz != CIPHERTEXT_SIZE) return false;
    ar(Common::asBinaryArray(ciphertext), "ciphertext");
    return true;
  }

  template <template <bool> class Archive>
  bool serialize(Archive<false>& ar) {
    ar(version, "version");
    ar(view_tag, "view_tag");
    ar(output_index, "output_index");
    uint64_t sz = 0;
    ar(sz, "sz");
    if (sz != CIPHERTEXT_SIZE) return false;
    ciphertext.resize(sz);
    ar(Common::asBinaryArray(ciphertext), "ciphertext");
    return true;
  }
};

} // namespace CryptoNote
```

#### 2. Crypto Header Additions ([`src/crypto/crypto.h`](file:///home/ar/fuego/src/crypto/crypto.h))
```cpp
namespace Crypto {

struct PqKemCiphertext {
  uint8_t data[1088];
};

struct PqKemSharedSecret {
  uint8_t data[32];
};

struct PqKemPublicKey {
  uint8_t data[1184];
};

struct PqKemSecretKey {
  uint8_t data[2400];
};

// Generates hybrid derivation mixing Ed25519 ECDH and ML-KEM-768 secret
bool generate_hybrid_key_derivation(
    const PublicKey& classical_pub,
    const SecretKey& classical_sec,
    const PqKemSharedSecret& kem_secret,
    KeyDerivation& hybrid_derivation);

// Derives fast 1-byte view tag
uint8_t derive_pq_view_tag(const KeyDerivation& hybrid_derivation);

} // namespace Crypto
```

#### 3. Core Crypto Implementation ([`src/crypto/crypto.cpp`](file:///home/ar/fuego/src/crypto/crypto.cpp))
```cpp
bool generate_hybrid_key_derivation(
    const PublicKey& classical_pub,
    const SecretKey& classical_sec,
    const PqKemSharedSecret& kem_secret,
    KeyDerivation& hybrid_derivation) 
{
  KeyDerivation classical_derivation;
  if (!crypto_ops::generate_key_derivation(classical_pub, classical_sec, classical_derivation)) {
    return false;
  }

  // Domain-separated Keccak mixing
  // H(D_ec || ss_kem || domain_tag)
  static const char domain[] = "FUEGO_PQ_HYBRID_STEALTH_V1";
  std::vector<uint8_t> buffer;
  buffer.reserve(sizeof(classical_derivation) + sizeof(kem_secret) + sizeof(domain) - 1);
  
  buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(&classical_derivation),
                reinterpret_cast<const uint8_t*>(&classical_derivation) + sizeof(classical_derivation));
  buffer.insert(buffer.end(), kem_secret.data, kem_secret.data + sizeof(kem_secret.data));
  buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(domain),
                reinterpret_cast<const uint8_t*>(domain) + sizeof(domain) - 1);

  keccak(buffer.data(), buffer.size(), reinterpret_cast<uint8_t*>(&hybrid_derivation), sizeof(hybrid_derivation));
  return true;
}

uint8_t derive_pq_view_tag(const KeyDerivation& hybrid_derivation) {
  static const char tag_domain[] = "VIEW_TAG";
  uint8_t h[32];
  std::vector<uint8_t> buf(sizeof(hybrid_derivation) + sizeof(tag_domain) - 1);
  memcpy(buf.data(), &hybrid_derivation, sizeof(hybrid_derivation));
  memcpy(buf.data() + sizeof(hybrid_derivation), tag_domain, sizeof(tag_domain) - 1);
  keccak(buf.data(), buf.size(), h, sizeof(h));
  return h[0];
}
```

---

## 5. Wallet Scanning Performance & Optimization

### The Decapsulation Bottleneck
ML-KEM-768 decapsulation takes ~25 microseconds on modern x86/ARM CPUs. While fast, running 1,000 decapsulations per block during wallet sync would create noticeable sync latency:
$$1000 \times 25\,\mu\text{s} = 25\,\text{ms/block}$$

### The 2-Stage Filter Pipeline
To keep wallet sync instant, Fuego implements a two-stage filter:

1. **Stage 1 (Classical ECDH + View Tag)**:
   - For incoming blocks, first perform classical scalar multiplication $8 \cdot a \cdot R$ (~300 ns).
   - If the transaction extra does not contain tag `0xD7`, it is a legacy classical transaction. Process normally.
   - If tag `0xD7` is present, execute ML-KEM decapsulation to recover $ss_{\text{kem}}$.
   - Compute candidate `view_tag` in 1 Keccak round.
   - Compare candidate `view_tag` with published byte:
     - 255 out of 256 transactions (99.6%) that do not belong to this wallet are discarded before deriving keys or performing public key matching.
2. **Stage 2 (Output Match)**:
   - For the 0.4% that pass view tag filtering, compute $P_i = h_i \cdot G + B$ and verify output match.

---

## 6. Backward Compatibility & Soft-Fork Path

### Wire Format Compatibility
- **Old Nodes**: Old nodes parse `tx_extra` sequentially. Any unknown tag (including `0xD7`) is treated as unparsed blob and skipped. Existing consensus rules for transaction structure and block size limits accept `0xD7` without hard-forking.
- **Legacy Wallets**: Wallets sending to classical `fire...` addresses produce transactions with standard `TX_EXTRA_TAG_PUBKEY` (no `0xD7` tag).
- **PQ-Enabled Wallets**: Wallets sending to `fire_pq...` addresses attach `TX_EXTRA_PQ_KEM` (`0xD7`). Both legacy and PQ outputs can coexist within the same transaction.

### Migration Timeline
1. **v12.0 Testnet**: Activate `TX_EXTRA_PQ_KEM` parser and `fire_pq` address support in CLI wallet.
2. **v12.1 Mainnet**: Standardize `fire_pq` addresses for default receive addresses in `fire_wallet` and TUI.
3. **v13.0 Hardfork (Phase 2 Prep)**: Make `TX_EXTRA_PQ_KEM` mandatory for all new on-chain commitment outputs, permanently ending the creation of non-PQ harvestable transactions.

---

## 7. Security & Cryptographic Invariants

> [!IMPORTANT]
> **RNG Entropy Requirement**: ML-KEM-768 encapsulation requires 32 bytes of cryptographically secure random entropy. Never reuse random coins across encapsulations. Fuego must source randomness directly via OpenSSL `RAND_bytes()` or Linux `getrandom()`.

> [!CAUTION]
> **Constant-Time Decapsulation**: ML-KEM decapsulation must execute in constant time to prevent cache-timing attacks that could leak the private key $sk_{\text{kem}}$. Fuego's integration must reject variable-time reference implementations and link against verified constant-time implementations (such as PQClean or liboqs).

> [!NOTE]
> **Ciphertext Malleability**: ML-KEM is an IND-CCA2 secure KEM. An attacker cannot tamper with $c_{\text{kem}}$ in `tx_extra` without causing decapsulation to fail (it returns an implicit pseudo-random rejection secret). This prevents ciphertext manipulation attacks.
