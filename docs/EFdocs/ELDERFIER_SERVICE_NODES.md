# Elderfier Service Nodes - Higher Tier Eldernodes

## Overview

Elderfier service nodes represent a new **higher tier** of Eldernodes in the Fuego blockchain, offering enhanced functionality and flexible service identification options. Unlike basic Eldernodes that use public wallet addresses for service identification, Elderfier operators have multiple options for their service ID in the network registry.

## Tier System

### Eldernode Tiers

```cpp
enum class EldernodeTier : uint8_t {
    BASIC = 0,           // Basic Eldernode (no stake required)
    ELDERFIER = 1        // Elderfier service node (800 XFG stake required)
};
```

### Tier Requirements

| Tier | Minimum Stake | Service ID Options | Priority |
|------|---------------|-------------------|----------|
| **Basic** | **0 XFG** (--set-fee-address only) | Public wallet address only | Standard |
| **Elderfier** | **800 XFG** | Custom name, hashed address, or standard address | **High** |

## Service ID Options for Elderfier Nodes

Elderfier operators can choose from three different service ID types:

### 1. **Custom Name** (Exactly 8 letters, all caps)
```cpp
ElderfierServiceId serviceId = ElderfierServiceId::createCustomName("FUEGONODE", walletAddress);
```

**Features:**
- **Exactly 8 letters** (no more, no less)
- **All uppercase letters only** (A-Z)
- **Alphabetic characters only** (no numbers, underscores, or hyphens)
- Reserved name protection (ADMIN, ROOT, SYSTEM, etc.)
- **Linked to actual wallet address** for verification
- Network registry visibility

**Example:**
```cpp
ENindexEntry entry;
entry.tier = EldernodeTier::ELDERFIER;
entry.serviceId = ElderfierServiceId::createCustomName("MYNODE", "FUEGO123456789abcdef");
// Service ID: "MYNODE" (padded to 8 letters if needed)
// Linked Address: "FUEGO123456789abcdef"
```

**Validation Rules:**
- Must be exactly 8 characters
- Must be all uppercase letters (A-Z)
- Must not be a reserved name
- Must link to actual wallet address

### 2. **Hashed Public Fee Address** (Privacy Option)
```cpp
ElderfierServiceId serviceId = ElderfierServiceId::createHashedAddress("FUEGO123456789abcdef");
```

**Features:**
- SHA256 hash of the public fee address
- Privacy protection for fee address
- Masked display name (e.g., "FUEG...def")
- **Linked to actual wallet address** for verification
- Network registry shows hash, not original address

**Example:**
```cpp
ENindexEntry entry;
entry.tier = EldernodeTier::ELDERFIER;
entry.serviceId = ElderfierServiceId::createHashedAddress("FUEGO123456789abcdef");
// Service ID: "a1b2c3d4e5f6..." (64-character hash)
// Display Name: "FUEG...def"
// Linked Address: "FUEGO123456789abcdef"
```

### 3. **Standard Address** (Like Basic Eldernodes)
```cpp
ElderfierServiceId serviceId = ElderfierServiceId::createStandardAddress("FUEGO123456789abcdef");
```

**Features:**
- Same as basic Eldernodes (public wallet address)
- Full transparency
- Compatible with existing systems
- No additional privacy features

## Implementation Details

### Service ID Structure

```cpp
struct ElderfierServiceId {
    ServiceIdType type;           // STANDARD_ADDRESS, CUSTOM_NAME, HASHED_ADDRESS
    std::string identifier;       // Raw identifier (address, name, or hash)
    std::string displayName;      // Human-readable display name
    std::string linkedAddress;    // Actual wallet address (for verification)
    
    bool isValid() const;
    std::string toString() const;
    
    // Factory methods
    static ElderfierServiceId createStandardAddress(const std::string& address);
    static ElderfierServiceId createCustomName(const std::string& name, const std::string& walletAddress);
    static ElderfierServiceId createHashedAddress(const std::string& address);
};
```

### Configuration

```cpp
struct ElderfierServiceConfig {
    uint64_t minimumStakeAmount = 800000000;      // 800 XFG minimum (800 * 1,000,000)
    uint64_t customNameLength = 8;                 // Exactly 8 letters
    bool allowHashedAddresses = true;              // Enable privacy option
    std::vector<std::string> reservedNames;        // Protected names
};
```

### Reserved Names Protection

The following names are reserved and cannot be used as custom names:
- `ADMIN`, `ROOT`, `SYSTEM`, `FUEGO`, `ELDER`, `NODE`
- `TEST`, `DEV`, `MAIN`, `PROD`, `SERVER`, `CLIENT`
- `MASTER`, `SLAVE`, `BACKUP`, `CACHE`, `DB`, `API`, `WEB`, `APP`

## Enhanced Features

### 1. **Tier Prioritization**
Elderfier nodes are automatically prioritized in consensus operations:

```cpp
// Elderfier nodes appear first in sorted lists
std::sort(activeParticipants.begin(), activeParticipants.end());
// ELDERFIER > BASIC (regardless of stake amount)
```

### 2. **Service ID Conflict Detection**
Prevents duplicate service IDs across Elderfier nodes:

```cpp
bool hasServiceIdConflict(const ElderfierServiceId& serviceId, const Crypto::PublicKey& excludeKey) const;
```

### 3. **Enhanced Validation**
Tier-specific validation rules:

```cpp
bool validateEldernodeEntry(const ENindexEntry& entry) const {
    if (entry.tier == EldernodeTier::ELDERFIER) {
        if (entry.stakeAmount < m_elderfierConfig.minimumStakeAmount) {
            return false; // Insufficient stake for Elderfier (less than 800 XFG)
        }
        if (!entry.serviceId.isValid()) {
            return false; // Invalid service ID
        }
    } else if (entry.tier == EldernodeTier::BASIC) {
        if (entry.stakeAmount != 0) {
            return false; // Basic Eldernodes should have no stake
        }
    }
    return true;
}
```

### 4. **Service ID Lookup**
Find Elderfier nodes by their service ID:

```cpp
std::optional<ENindexEntry> getEldernodeByServiceId(const ElderfierServiceId& serviceId) const;
```

### 5. **Address Linking Verification**
Ensure custom/hashed service IDs link to actual wallet addresses:

```cpp
// Verify that linked address matches fee address for custom/hashed types
if (entry.serviceId.type != ServiceIdType::STANDARD_ADDRESS) {
    if (entry.serviceId.linkedAddress != entry.feeAddress) {
        return false; // Linked address mismatch
    }
}
```

## Usage Examples

### Creating an Elderfier Node with Custom Name

```cpp
EldernodeIndexManager manager;

ENindexEntry elderfierEntry;
Crypto::generate_keys(elderfierEntry.eldernodePublicKey, elderfierEntry.eldernodeSecretKey);
elderfierEntry.feeAddress = "FUEGO987654321fedcba";
elderfierEntry.stakeAmount = 800000000; // 800 XFG minimum
elderfierEntry.tier = EldernodeTier::ELDERFIER;
elderfierEntry.serviceId = ElderfierServiceId::createCustomName("FUEGONODE", "FUEGO987654321fedcba");
elderfierEntry.isActive = true;

bool success = manager.addEldernode(elderfierEntry);
```

### Creating an Elderfier Node with Hashed Address

```cpp
ENindexEntry privacyEntry;
Crypto::generate_keys(privacyEntry.eldernodePublicKey, privacyEntry.eldernodeSecretKey);
privacyEntry.feeAddress = "FUEGO555666777888999";
privacyEntry.stakeAmount = 1000000000; // 1000 XFG
privacyEntry.tier = EldernodeTier::ELDERFIER;
privacyEntry.serviceId = ElderfierServiceId::createHashedAddress("FUEGO555666777888999");
privacyEntry.isActive = true;

bool success = manager.addEldernode(privacyEntry);
```

### Creating a Basic Eldernode (No Stake)

```cpp
ENindexEntry basicEntry;
Crypto::generate_keys(basicEntry.eldernodePublicKey, basicEntry.eldernodeSecretKey);
basicEntry.feeAddress = "FUEGO123456789abcdef";
basicEntry.stakeAmount = 0; // No stake required for basic Eldernodes
basicEntry.tier = EldernodeTier::BASIC;
basicEntry.isActive = true;

bool success = manager.addEldernode(basicEntry);
```

### Looking Up Elderfier Nodes

```cpp
// Get all Elderfier nodes
auto elderfierNodes = manager.getElderfierNodes();

// Get specific Elderfier by service ID
auto serviceId = ElderfierServiceId::createCustomName("FUEGONODE", "FUEGO987654321fedcba");
auto node = manager.getEldernodeByServiceId(serviceId);

// Get statistics
uint32_t elderfierCount = manager.getElderfierNodeCount();
```

## Network Registry Integration

Elderfier service IDs are automatically added to the network registry with the following information:

### Registry Entry Structure
```json
{
  "tier": "ELDERFIER",
  "serviceId": {
    "type": "CUSTOM_NAME",
    "identifier": "FUEGONODE",
    "displayName": "FUEGONODE",
    "linkedAddress": "FUEGO987654321fedcba"
  },
  "publicKey": "a1b2c3d4...",
  "stakeAmount": 800000000,
  "feeAddress": "FUEGO987654321fedcba",
  "isActive": true
}
```

### Privacy Considerations

1. **Custom Names**: Fully public, human-readable identifiers (8 letters, all caps)
2. **Hashed Addresses**: Privacy-preserving, original address not visible but linked internally
3. **Standard Addresses**: Same transparency as basic Eldernodes

## Benefits

### For Elderfier Operators
- **Flexibility**: Choose service ID type based on privacy needs
- **Branding**: Use custom names for recognition (8-letter limit)
- **Privacy**: Option to hide fee addresses
- **Priority**: Higher priority in consensus operations
- **Verification**: Service IDs linked to actual wallet addresses

### For Network
- **Enhanced Security**: Higher stake requirements (800 XFG)
- **Better Organization**: Human-readable service identifiers
- **Privacy Options**: Support for privacy-conscious operators
- **Scalability**: Tiered system for future expansion
- **Integrity**: Service ID verification and conflict prevention

## Testing

Comprehensive test suite covers:

1. **Basic Eldernode Operations**: No stake requirement validation
2. **Elderfier Service Node Creation**: All three service ID types
3. **Service ID Validation**: Exactly 8 letters, all caps, reserved names, hashed addresses
4. **Stake Requirements**: 800 XFG minimum for Elderfier, 0 for Basic
5. **Tier Prioritization**: Elderfier nodes prioritized in consensus
6. **Conflict Detection**: Duplicate service ID prevention
7. **Address Linking**: Verification that service IDs link to actual wallet addresses

## Future Enhancements

1. **Service ID Transfer**: Allow service ID transfer between operators
2. **Custom Name Auctions**: Auction system for premium custom names
3. **Service ID Expiration**: Time-limited service IDs
4. **Enhanced Privacy**: Additional privacy features for hashed addresses
5. **Service ID Categories**: Categorize service IDs by type or region

## Conclusion

Elderfier service nodes provide a powerful enhancement to the Fuego Eldernode system, offering:

- **Higher tier** with increased stake requirements (800 XFG)
- **Flexible service identification** with three distinct options
- **Exact custom name requirements** (8 letters, all caps)
- **Privacy support** through hashed addresses
- **Enhanced prioritization** in network operations
- **Comprehensive validation** and conflict prevention
- **Address linking verification** for integrity

This implementation successfully addresses the requirement for Elderfier operators to have additional service ID options beyond public wallet addresses, while maintaining compatibility with the existing basic Eldernode system and ensuring proper stake requirements and validation.
