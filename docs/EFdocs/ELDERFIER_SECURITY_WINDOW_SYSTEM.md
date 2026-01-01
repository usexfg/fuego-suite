# Elderfier Security Window System

## Overview

The Elderfier Security Window System provides a **8-hour buffer period** after each Elderfier signature to prevent false proof signing and immediate unlocking to avoid slashing. This system ensures that burn deposit verification can be completed before Elderfiers can unlock their stakes.

## Security Window Mechanics

### Configuration
```cpp
namespace SecurityWindow {
    static const uint64_t DEFAULT_DURATION_SECONDS = 28800;      // 8 hours
    static const uint64_t MINIMUM_SIGNATURE_INTERVAL = 3600;     // 1 hour minimum between signatures
    static const uint64_t GRACE_PERIOD_SECONDS = 300;            // 5 minute grace period
    static const uint64_t MAX_OFFLINE_TIME = 86400;              // 24 hours max offline
}
```

### Data Structure
```cpp
struct ElderfierDepositData {
    // Security window fields
    uint64_t lastSignatureTimestamp; // Last signature timestamp
    uint64_t securityWindowEnd;      // When security window ends
    uint64_t securityWindowDuration; // Duration of security window
    bool isInSecurityWindow;         // Currently in security window
    bool unlockRequested;            // Elderfier requested to unlock
    uint64_t unlockRequestTimestamp; // When unlock was requested
    
    // Methods
    bool canUnlock() const;          // Check if deposit can be unlocked
    uint64_t getSecurityWindowRemaining() const; // Get remaining time
    void updateLastSignature(uint64_t timestamp); // Update signature timestamp
    void requestUnlock(uint64_t timestamp);       // Request to unlock deposit
};
```

## Core Functions

### 1. Signature Update
```cpp
bool EldernodeIndexManager::updateElderfierSignature(const Crypto::PublicKey& publicKey, uint64_t timestamp) {
    // Updates signature timestamp and triggers 8-hour security window
    // Prevents signature spam with minimum interval validation
    // Updates security window status automatically
}
```

### 2. Unlock Request
```cpp
bool EldernodeIndexManager::requestElderfierUnlock(const Crypto::PublicKey& publicKey, uint64_t timestamp) {
    // Requests unlock only if not in security window
    // Validates unlock request conditions
    // Sets unlock request flag
}
```

### 3. Unlock Processing
```cpp
bool EldernodeIndexManager::processElderfierUnlock(const Crypto::PublicKey& publicKey) {
    // Processes unlock after security window expires
    // Removes Elderfier from ENindex
    // Marks deposit as unlocked
}
```

## Security Features

### 1. **Anti-Spam Protection**
- **Minimum Interval**: 1 hour between signatures
- **Grace Period**: 5 minute grace period for timestamp validation
- **Future Timestamp Prevention**: Prevents future timestamps

### 2. **Security Window Enforcement**
- **8-Hour Buffer**: 8-hour security window after each signature
- **Unlock Prevention**: Cannot unlock during security window
- **Automatic Updates**: Security window status updated automatically

### 3. **Burn Deposit Verification**
- **Review Period**: 8 hours for burn deposit verification
- **Slashing Prevention**: Prevents immediate unlocking to avoid slashing
- **Proof Validation**: Time for proof validation and review

## Usage Examples

### Signature Update
```cpp
bool updateSignature(const Crypto::PublicKey& publicKey) {
    uint64_t timestamp = getCurrentTimestamp();
    return eldernodeIndexManager.updateElderfierSignature(publicKey, timestamp);
}
```

### Unlock Request
```cpp
bool requestUnlock(const Crypto::PublicKey& publicKey) {
    uint64_t timestamp = getCurrentTimestamp();
    return eldernodeIndexManager.requestElderfierUnlock(publicKey, timestamp);
}
```

### Security Window Status
```cpp
void checkSecurityWindow(const Crypto::PublicKey& publicKey) {
    if (eldernodeIndexManager.isElderfierInSecurityWindow(publicKey)) {
        uint64_t remaining = eldernodeIndexManager.getSecurityWindowRemaining(publicKey);
        logger(INFO) << "Security window active, remaining: " << remaining << " seconds";
    }
}
```

## API Reference

### Core Methods
```cpp
// Signature management
bool updateElderfierSignature(const Crypto::PublicKey& publicKey, uint64_t timestamp);

// Security window status
bool isElderfierInSecurityWindow(const Crypto::PublicKey& publicKey) const;
uint64_t getSecurityWindowRemaining(const Crypto::PublicKey& publicKey) const;
bool canElderfierUnlock(const Crypto::PublicKey& publicKey) const;

// Unlock process
bool requestElderfierUnlock(const Crypto::PublicKey& publicKey, uint64_t timestamp);
bool processElderfierUnlock(const Crypto::PublicKey& publicKey);
```

## Conclusion

The Elderfier Security Window System provides essential security features:

### **Key Benefits**:
1. **Burn Deposit Verification**: 8-hour buffer for proof validation
2. **Slashing Prevention**: Prevents immediate unlocking to avoid slashing
3. **Anti-Spam Protection**: Minimum intervals between signatures
4. **Secure Unlock Process**: Controlled unlock with validation
5. **Automatic Management**: Automatic security window updates

### **Security Features**:
1. **8-Hour Security Window**: Buffer period after each signature
2. **Signature Validation**: Prevents spam and future timestamps
3. **Unlock Control**: Controlled unlock process with validation
4. **ENindex Management**: Automatic removal after unlock
5. **Thread Safety**: Thread-safe operations with mutex protection

This system ensures that **Elderfiers cannot sign false proofs and immediately unlock** to avoid slashing, providing the security needed for burn deposit verification! 🚀
