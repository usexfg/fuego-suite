# Atomic Swap Security Fix Implementation Plan

**Audit Date:** 2026-04-26  
**Target:** Fuego Atomic Swap Codebase  
**Priority:** Critical & High Severity Issues  
**Status:** Planned

---

## Executive Summary

This plan addresses 5 critical/high security vulnerabilities in the Fuego atomic swap system:

| Priority | Issue | Risk |
|----------|-------|------|
| P0 | Plaintext adaptorSecret storage | Direct fund theft |
| P0 | Unencrypted Ethereum private key | Memory dump = theft |
| P1 | No peer message authentication | MITM swap hijacking |
| P1 | Ineffective memory zeroization | Secrets persist in RAM |
| P1 | No path validation on dataDir | Path traversal |

---

## Implementation Phases

### Phase 1: Critical Fixes (Week 1-2)

#### 1.1 Encrypt adaptorSecret at Rest
**Files:** `src/SwapDaemon/SwapStateMachine.cpp`, `src/SwapDaemon/SwapTypes.h`

**Current State:**
```cpp
// Vulnerable: adaptorSecret stored plaintext
void SwapStateMachine::serialize() {
  json["adaptorSecret"] = params.adaptorSecret;
}
```

**Implementation:**
```cpp
// Secure: Encrypt with ChaCha20-Poly1305
class SecretEncryption {
  static std::vector<uint8_t> encrypt(
    const std::string& secret,
    const std::array<uint8_t, 32>& walletKey
  );
  
  static std::string decrypt(
    const std::vector<uint8_t>& ciphertext,
    const std::array<uint8_t, 32>& walletKey
  );
  
private:
  static constexpr size_t NONCE_SIZE = 12;
  static constexpr size_t TAG_SIZE = 16;
};
```

**Tasks:**
- [ ] Create `SecretEncryption` class in `src/Crypto/SecretEncryption.cpp`
- [ ] Implement ChaCha20-Poly1305 encryption using libsodium or OpenSSL
- [ ] Derive encryption key from wallet master seed using HKDF
- [ ] Modify `SwapStateMachine::serialize()` to encrypt adaptorSecret
- [ ] Modify `SwapStateMachine::deserialize()` to decrypt adaptorSecret
- [ ] Add unit tests for encryption/decryption roundtrip
- [ ] Test with existing swap database migration

**Dependencies:** libsodium or OpenSSL (already in project)  
**Risk:** Medium - requires database migration for existing swaps  
**Rollback:** Keep plaintext with "encrypted=false" flag for backward compatibility

---

#### 1.2 Secure Ethereum Private Key Storage
**Files:** `src/SwapDaemon/Ethereum/EthRpcClient.h`, `src/SwapDaemon/Ethereum/EthRpcClient.cpp`

**Current State:**
```cpp
// Vulnerable: Private key in plaintext array
class EthRpcClient {
  std::array<uint8_t, 32> m_privKey;  // PLAINTEXT!
};
```

**Implementation:**
```cpp
// Secure: Encrypted key storage with secure memory
class SecureKeyStorage {
public:
  struct EncryptedKey {
    std::vector<uint8_t> ciphertext;  // ChaCha20-Poly1305
    std::vector<uint8_t> nonce;
    std::vector<uint8_t> tag;
  };
  
  static EncryptedKey encrypt(
    const std::array<uint8_t, 32>& plaintextKey,
    const std::string& password,
    secure_string& error
  );
  
  static std::optional<std::array<uint8_t, 32>> decrypt(
    const EncryptedKey& encryptedKey,
    const std::string& password,
    secure_string& error
  );
  
  // Secure memory handling
  static void secure_zero(void* ptr, size_t len);
};
```

**Tasks:**
- [ ] Create `SecureKeyStorage` class
- [ ] Add password-based key derivation (Argon2id)
- [ ] Replace `m_privKey` with encrypted storage
- [ ] Add password prompt on startup (if key encrypted)
- [ ] Implement secure memory zeroization on key destruction
- [ ] Update `ChainClientConfig` to support encrypted key files
- [ ] Add key migration tool for existing configurations

**Dependencies:** libsodium (Argon2id), secure memory allocation  
**Risk:** High - affects Ethereum integration  
**Rollback:** Support both encrypted and plaintext during migration period

---

### Phase 2: Authentication & MITM Protection (Week 2-3)

#### 2.1 SwapPeerProtocol HMAC Authentication
**Files:** `src/SwapDaemon/SwapPeerProtocol.h`, `src/SwapDaemon/SwapPeerProtocol.cpp`

**Current State:**
```cpp
// Vulnerable: No authentication
void PeerProtocol::handleMessage(const PeerMessage& msg) {
  // No verification!
  processMessage(msg);
}
```

**Implementation:**
```cpp
class PeerMessageAuth {
public:
  static constexpr size_t HMAC_SIZE = 32;
  
  struct AuthenticatedMessage {
    MessageType type;
    std::vector<uint8_t> payload;
    std::array<uint8_t, HMAC_SIZE> hmac;
    uint64_t timestamp;
    uint64_t nonce;
  };
  
  static AuthenticatedMessage sign(
    const std::vector<uint8_t>& payload,
    const std::array<uint8_t, 32>& sharedSecret,
    uint64_t nonce
  );
  
  static bool verify(
    const AuthenticatedMessage& msg,
    const std::array<uint8_t, 32>& sharedSecret
  );
  
private:
  static constexpr uint64_t MAX_AGE_SECONDS = 300;  // 5 min
  static constexpr uint64_t MAX_NONCE_AGE = 1000;
};
```

**Tasks:**
- [ ] Create `PeerMessageAuth` class
- [ ] Derive shared secret during key exchange (DH)
- [ ] Add HMAC-SHA256 to all peer messages
- [ ] Add timestamp and nonce for replay protection
- [ ] Implement message replay cache (bloom filter)
- [ ] Update all message handlers to require authentication
- [ ] Add backward compatibility mode (optional HMAC)
- [ ] Integration test with two swap daemons

**Dependencies:** Crypto++ or existing crypto library  
**Risk:** Medium - protocol change requires coordination  
**Rollback:** Feature flag for HMAC enforcement

---

#### 2.2 Path Validation for dataDir
**Files:** `src/SwapDaemon/SwapDatabase.cpp`

**Current State:**
```cpp
// Vulnerable: No path validation
void SwapDatabase::open(const std::string& dataDir) {
  std::filesystem::create_directory(dataDir);  // Traversal possible!
}
```

**Implementation:**
```cpp
class PathValidator {
public:
  static constexpr std::string_view ALLOWED_ROOT = "/var/lib/fuego";
  
  static bool isValidDataDir(const std::string& path, std::string& error) {
    // Must be absolute
    if (!std::filesystem::path(path).is_absolute()) {
      error = "dataDir must be absolute path";
      return false;
    }
    
    // Must be within allowed root
    auto canonical = std::filesystem::canonical(path);
    if (!canonical.string().rfind(ALLOWED_ROOT, 0) == 0) {
      error = "dataDir must be within " + std::string(ALLOWED_ROOT);
      return false;
    }
    
    // No parent references
    if (path.find("..") != std::string::npos) {
      error = "path traversal not allowed";
      return false;
    }
    
    return true;
  }
};
```

**Tasks:**
- [ ] Create `PathValidator` utility
- [ ] Add validation to `SwapDatabase::open()`
- [ ] Add validation to `SwapDaemon` constructor
- [ ] Add config file validation on startup
- [ ] Add integration test for path traversal attempts

**Risk:** Low - defensive addition  
**Rollback:** None needed

---

### Phase 3: Memory Security (Week 3)

#### 3.1 Secure Memory Zeroization
**Files:** `src/SwapDaemon/AdaptorSwap.cpp`, `src/SwapDaemon/Crypto/`

**Current State:**
```cpp
// Vulnerable: Compiler may optimize away
volatile uint8_t* p = adaptorSecret.data();
while (len--) *p++ = 0;
```

**Implementation:**
```cpp
// Secure: Using OpenSSL or platform-specific
#include <openssl/crypto.h>

void secure_zero(void* ptr, size_t len) {
  OPENSSL_cleanse(ptr, len);
  // Or platform-specific:
  // memset_s(ptr, len, 0, len);
  // Or portable:
  // volatile unsigned char* p = (volatile unsigned char*)ptr;
  // while (len--) *p++ = 0;
}

template<typename T>
void secure_clear(T& obj) {
  if constexpr (std::is_trivially_destructible<T>) {
    secure_zero(&obj, sizeof(T));
  } else {
    obj.~T();
    secure_zero(&obj, sizeof(T));
  }
}
```

**Tasks:**
- [ ] Create `SecureMem.h` with platform-aware implementations
- [ ] Replace all secret memory clearing in AdaptorSwap
- [ ] Replace all secret memory clearing in EthRpcClient
- [ ] Replace all secret memory clearing in Secp256k1Signer
- [ ] Add compile-time verification (volatile audit)
- [ ] Add runtime verification (test with valgrind/drd)

**Risk:** Low - improvement to existing code  
**Rollback:** None needed

---

### Phase 4: Testing & Validation (Week 4)

#### 4.1 Security Test Suite

**Tasks:**
- [ ] Unit tests for SecretEncryption
- [ ] Unit tests for SecureKeyStorage
- [ ] Unit tests for PeerMessageAuth (HMAC, replay protection)
- [ ] Unit tests for PathValidator
- [ ] Unit tests for secure_zero

**Integration Tests:**
- [ ] End-to-end swap with encrypted secrets
- [ ] MITM attack simulation (should fail)
- [ ] Memory dump recovery attempt (should find nothing)
- [ ] Path traversal attempts (should be blocked)
- [ ] Replay attack simulation (should be blocked)

**Automated Security Scans:**
- [ ] Add Clang Static Analyzer to CI
- [ ] Add Coverity scan
- [ ] Add memory sanitizers (ASan, MSan) to tests
- [ ] Add valgrind memcheck to CI

---

## Timeline

| Week | Phase | Deliverables |
|------|-------|--------------|
| 1 | Phase 1.1 | SecretEncryption class, encrypted adaptorSecret |
| 2 | Phase 1.2 | SecureKeyStorage, encrypted ETH key |
| 2 | Phase 2.2 | Path validation |
| 3 | Phase 2.1 | HMAC authentication in P2P |
| 4 | Phase 3 | Secure memory zeroization |
| 4 | Phase 4 | Test suite, security scans |

---

## Risk Assessment

| Fix | Implementation Risk | Rollback Risk | Priority |
|-----|---------------------|---------------|----------|
| adaptorSecret encryption | Medium (DB migration) | Low | P0 |
| ETH key encryption | High (affects config) | Medium | P0 |
| HMAC auth | Medium (protocol) | Low (feature flag) | P1 |
| Path validation | Low | None | P1 |
| Memory zeroization | Low | None | P1 |

---

## Success Criteria

- [ ] All critical vulnerabilities remediated
- [ ] 90%+ test coverage on security components
- [ ] Zero findings in automated security scans
- [ ] No regression in swap functionality
- [ ] Security score improved to 80+

---

## Open Questions

1. **Key Storage:** Should we use hardware security module (HSM) integration for production, or is password-protected encryption sufficient for initial release?

2. **Migration:** How handle existing swaps with plaintext adaptorSecret? Options:
   - Force refund and restart
   - One-time migration on startup
   - Keep plaintext with warning (not recommended)

3. **P2P Protocol:** Should HMAC be mandatory or optional (backward compatible) during rollout?

4. **Testing:** What simulation scenarios should be included in red team exercise?
