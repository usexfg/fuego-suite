# Fuego Group Aliases (Multi-Sig Aliases) Implementation Plan

## Overview

This document outlines the design for **Group Aliases** — multi-signature aliases that allow groups of wallets to collectively own and control a single alias. Group aliases function as first-class network entities: they can send/receive transactions, participate in swaps, and interact with network features, all governed by configurable threshold signatures.

---

## 1. Core Concepts

### 1.1 What is a Group Alias?

A **Group Alias** is an alias (e.g., `firedao&`) owned by a group of wallet addresses rather than a single address. It operates like any regular wallet on the network, but requires M-of-N signatures from the group members to authorize operations.

### 1.2 Key Properties

| Property | Description |
|----------|-------------|
| **Alias Name** | 8-character identifier (e.g., `firedao&`) |
| **Group Key** | Aggregated public key (Musig2-style) derived from member pubkeys |
| **Threshold (M)** | Minimum signatures required to authorize operations |
| **Total Members (N)** | Total number of member wallets in the group |
| **Member List** | On-chain list of member addresses (stored as hashes for privacy) |

### 1.3 Comparison with Existing Aliases

| Feature | Regular Alias | Group Alias |
|---------|---------------|-------------|
| Owner | Single address | M-of-N group members |
| Authorization | 1 signature | M-of-N threshold signatures |
| Key Derivation | Direct address | Aggregated group key |
| Privacy | Address hash stored | Member hashes stored (on-chain) |
| Registration | 1-party registration | M-of-N member agreement |

---

## 2. Data Structures

### 2.1 New Transaction Extra Tag

```cpp
// New tag for Group Alias operations
#define TX_EXTRA_GROUP_ALIAS               0xEB  // Group alias registration/update
```

### 2.2 Group Alias Entry (On-Chain)

```cpp
// Stored in Blockchain (in-memory index + on-chain via tx_extra)
struct GroupAliasEntry {
  std::string alias;                    // 8 chars: e.g., "firedao&"
  Crypto::Hash aliasHash;               // cn_fast_hash(alias) for lookup
  
  // Group configuration
  uint8_t threshold;                   // M: required signatures (1-255)
  uint8_t memberCount;                  // N: total members (1-255)
  std::vector<Crypto::Hash> memberHashes;  // cn_fast_hash(pubkey) for each member
  
  // Aggregated group public key (Musig2-style)
  Crypto::PublicKey groupPubKey;       // The "address" of the group alias
  
  // Metadata
  uint8_t aliasType = 2;                // 2 = Group alias (new enum value)
  uint32_t registeredBlock = 0;
  std::string groupName;                // Human-readable name (optional, stored off-chain or encrypted)
  std::vector<uint8_t> metadata;        // Encrypted group config (admin rules, etc.)
};
```

### 2.3 Transaction Extra Structure

```cpp
// For registration
struct TransactionExtraGroupAliasRegistration {
  uint8_t version = 1;
  std::string alias;
  Crypto::Hash aliasHash;
  uint8_t threshold;
  std::vector<std::string> memberAddresses;      // Full addresses (encrypted in extra)
  std::vector<Crypto::PublicKey> memberPubKeys; // For key aggregation
  Crypto::PublicKey groupPubKey;                 // Aggregated pubkey
  uint8_t aliasType = 2;
  
  bool serialize(ISerializer& serializer);
  bool isValid() const;
};

// For updates (add/remove members, change threshold)
struct TransactionExtraGroupAliasUpdate {
  uint8_t version = 1;
  std::string alias;
  Crypto::Hash aliasHash;
  uint8_t operation;                     // 0=add_member, 1=remove_member, 2=change_threshold
  std::vector<std::string> affectedAddresses;
  std::vector<Crypto::PublicKey> affectedPubKeys;
  uint8_t newThreshold;
  std::vector<uint8_t> signatures;      // M-of-N signatures authorizing update
  
  bool serialize(ISerializer& serializer);
  bool isValid() const;
};
```

---

## 3. Musig2 Integration

### 3.1 Why Musig2?

Fuego already has **Musig2** infrastructure (`src/crypto/musig2.h`) for atomic swaps. This will be extended for group aliases:

- **Key Aggregation**: Combine N member pubkeys into one group pubkey
- **Partial Signatures**: Each member creates a partial signature
- **Signature Aggregation**: Combine M partial signatures into one final signature
- **Non-Interactive**: No sequential signing requirement (unlike MuSig)

### 3.2 Key Derivation

```cpp
// Group pubkey derivation (Musig2 style)
bool deriveGroupPubKey(
    const std::vector<Crypto::PublicKey>& memberPubKeys,
    const std::string& alias,
    Crypto::PublicKey& groupPubKey
);

// The alias is used as a domain separator to ensure different aliases
// with same members have different group keys
```

### 3.3 Transaction Authorization

```cpp
// Sign a transaction prefix hash with M-of-N threshold
bool signGroupTransaction(
    const Crypto::Hash& txPrefixHash,
    const std::vector<Crypto::SecretKey>& memberSecKeys,
    uint8_t threshold,
    std::vector<uint8_t>& aggregatedSignature
);

// Verify threshold signature
bool verifyGroupSignature(
    const Crypto::Hash& txPrefixHash,
    const std::vector<uint8_t>& signature,
    const Crypto::PublicKey& groupPubKey,
    uint8_t threshold
);
```

---

## 4. Registration Flow

### 4.1 Creating a Group Alias

```
┌─────────────────────────────────────────────────────────────────┐
│                    GROUP ALIAS REGISTRATION                       │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  1. Members agree on:                                            │
│     - Alias name (e.g., "firedao&")                              │
│     - Threshold M (e.g., 3-of-5)                                 │
│     - Member list (wallet addresses)                            │
│                                                                  │
│  2. Each member computes their pubkey from wallet               │
│                                                                  │
│  3. Aggregate pubkeys → groupPubKey                              │
│     Musig2: groupPubKey = aggsig(..., member1, member2, ..., alias) │
│                                                                  │
│  4. Create registration transaction with TX_EXTRA_GROUP_ALIAS    │
│     - Include: alias, threshold, member hashes, groupPubKey     │
│     - Attach M-of-N signatures from M members                   │
│                                                                  │
│  5. Broadcast to network                                        │
│                                                                  │
│  6. Block validation:                                           │
│     - Verify M valid signatures from distinct members            │
│     - Verify no duplicate aliases                                │
│     - Verify threshold ≤ member count                            │
│     - Store in AliasIndex                                        │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### 4.2 Registration Transaction

```cpp
// Wallet-side registration flow
class GroupAliasRegistration {
  std::string alias;
  uint8_t threshold;
  std::vector<AccountPublicAddress> members;
  
  // Phase 1: Key Setup (off-chain)
  std::vector<Crypto::PublicKey> collectPubKeys();
  Crypto::PublicKey aggregatePubKeys();
  
  // Phase 2: Registration Signing
  // Each of M members signs the registration data
  std::vector<std::vector<uint8_t>> collectSignatures(
    const std::vector<SecretKey>& signingKeys,
    const Crypto::Hash& registrationHash
  );
  
  // Phase 3: Submit
  Transaction createRegistrationTx(
    const std::vector<std::vector<uint8_t>>& signatures
  );
};
```

---

## 5. Alias Resolution & Validation

### 5.1 Extended AliasIndex

```cpp
class AliasIndex {
public:
  // Existing single-owner aliases
  bool registerAlias(const AliasEntry& entry);
  
  // NEW: Group aliases
  bool registerGroupAlias(const GroupAliasEntry& entry);
  bool updateGroupAlias(const GroupAliasUpdate& update);
  
  // Queries - extended to handle both types
  std::optional<AliasEntry> getAliasByName(const std::string& alias) const;
  std::optional<GroupAliasEntry> getGroupAliasByName(const std::string& alias) const;
  
  // NEW: Check if address is a group member
  bool isGroupMember(const std::string& address, const std::string& alias) const;
  uint8_t getThreshold(const std::string& alias) const;
  
  // NEW: Resolve alias to group pubkey
  std::optional<Crypto::PublicKey> getGroupPubKey(const std::string& alias) const;
  
  // ...
  
private:
  std::map<std::string, GroupAliasEntry> m_groupAliases;
  std::map<std::string, std::set<std::string>> m_memberToGroups; // addressHash -> set of aliases
};
```

### 5.2 RPC Endpoints

```cpp
// New RPC commands for group aliases
struct COMMAND_RPC_REGISTER_GROUP_ALIAS {
  // Request
  std::string alias;
  uint8_t threshold;
  std::vector<std::string> memberAddresses;
  std::vector<std::vector<uint8_t>> partialSignatures;
  
  // Response
  std::string txHash;
  bool accepted;
  std::string status;
};

struct COMMAND_RPC_UPDATE_GROUP_ALIAS {
  // Request
  std::string alias;
  uint8_t operation; // 0=add, 1=remove, 2=change_threshold
  std::vector<std::string> affectedAddresses;
  uint8_t newThreshold;
  std::vector<std::vector<uint8_t>> signatures;
  
  // Response
  std::string txHash;
  bool accepted;
  std::string status;
};

struct COMMAND_RPC_GET_GROUP_ALIAS {
  // Request
  std::string alias;
  
  // Response
  std::string aliasName;
  Crypto::PublicKey groupPubKey;
  uint8_t threshold;
  uint8_t memberCount;
  bool isMember; // Does caller belong to this group?
  uint32_t registeredBlock;
  bool found;
  std::string status;
};
```

---

## 6. Transaction Authorization

### 6.1 Spending from Group Alias

```
┌─────────────────────────────────────────────────────────────────┐
│              SPENDING FROM GROUP ALIAS                           │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  To spend XFG from a group alias:                               │
│                                                                  │
│  1. Proposer creates unsigned transaction                        │
│     - Input: Group alias output                                  │
│     - Output: Destination address + amount                       │
│                                                                  │
│  2. Proposer creates partial signature                           │
│     - Sign tx_prefix_hash with their secret key + Musig2 session │
│                                                                  │
│  3. Proposer broadcasts partial signature to group               │
│     (via off-chain channel: encrypted message, P2P, etc.)       │
│                                                                  │
│  4. Other M-1 signers verify tx, add their partial signatures   │
│                                                                  │
│  5. Final signer aggregates signatures → full signature          │
│                                                                  │
│  6. Aggregated signature broadcast with transaction              │
│                                                                  │
│  7. Network validates:                                          │
│     - Threshold signature verifies against groupPubKey            │
│     - KeyImage prevents double-spend                            │
│     - Balance check                                             │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### 6.2 Key Image Handling

```cpp
// Each group alias has a key image derived from groupPubKey
// This allows the network to detect double-spends

struct GroupKeyImage {
  Crypto::KeyImage keyImage;
  Crypto::PublicKey groupPubKey;
  
  // Derived as: keyImage = H_p(groupPubKey) * secret_scalar
  // The secret_scalar is distributed among M members via TSS
};

// For spending, all M signers must contribute to reveal the secret_scalar
```

---

## 7. Wallet Integration

### 7.1 SimpleWallet Extensions

```cpp
// New wallet commands for group aliases
class SimpleWallet {
  // Group alias management
  bool handleCreateGroupAlias(const std::vector<std::string>& args);
  bool handleJoinGroupAlias(const std::string& alias);
  bool handleLeaveGroupAlias(const std::string& alias);
  bool handleListGroupMembers(const std::string& alias);
  
  // Signing operations
  bool handleSignGroupTx(const std::string& txHash);
  bool handleApproveGroupTx(const std::string& txHash);
  bool handleGroupPending(); // List pending transactions
  
  // Viewing
  bool handleGetGroupAlias(const std::string& alias);
};
```

### 7.2 Example CLI Commands

```bash
# Create a group alias (requires coordination with members)
create_group_alias mygroup& --threshold 3 --members addr1,addr2,addr3,addr4,addr5

# Invite a member to join existing group
group_invite mygroup& --new-member addr6

# Remove a member (requires M-of-N signatures)
group_remove mygroup& --member addr4

# Change threshold (requires M-of-N signatures)
group_update mygroup& --threshold 2

# Send from group alias
send_from_alias mygroup& --to addrX --amount 100 --signers addr1,addr2

# View pending group transactions
group_pending mygroup&

# Sign a pending transaction
group_sign mygroup& --tx-hash <hash>
```

---

## 8. Implementation Phases

### Phase 1: Core Infrastructure (Foundation)
**Estimated: 2-3 weeks**

1. **Data Structures**
   - Define `GroupAliasEntry` in `AliasIndex.h`
   - Add `TransactionExtraGroupAliasRegistration` to `TransactionExtra.h`
   - Add `TX_EXTRA_GROUP_ALIAS = 0xEB` tag

2. **Musig2 Extensions**
   - Extend `musig2.h/cpp` with key aggregation utilities
   - Add `deriveGroupPubKey()` function
   - Add partial signature aggregation

3. **AliasIndex Integration**
   - Add `m_groupAliases` map
   - Implement `registerGroupAlias()`, `updateGroupAlias()`
   - Add group-specific query methods

### Phase 2: Registration & On-Chain (Core)
**Estimated: 2 weeks**

1. **Blockchain Processing**
   - Process `TX_EXTRA_GROUP_ALIAS` in `Blockchain.cpp`
   - Validate M-of-N signatures during registration
   - Store group aliases in persistent state

2. **Transaction Builder**
   - Create `GroupAliasTransactionBuilder`
   - Support registration transactions
   - Support update transactions (add/remove/change threshold)

3. **Serialization**
   - Implement `serialize()` for new structures
   - Add binary parsing in `TransactionExtra.cpp`

### Phase 3: RPC & Network Layer
**Estimated: 1-2 weeks**

1. **RPC Endpoints**
   - `/register_group_alias`
   - `/update_group_alias`
   - `/get_group_alias`
   - `/get_group_members`

2. **Address Resolution**
   - Extend alias resolution to handle group pubkeys
   - Group aliases resolve to their aggregated pubkey as "address"

### Phase 4: Wallet Integration
**Estimated: 2-3 weeks**

1. **SimpleWallet Commands**
   - `create_group_alias`
   - `group_invite` / `group_remove`
   - `group_update`
   - `send_from_group`
   - `group_pending`
   - `group_sign`

2. **Transaction Signing Flow**
   - Multi-round signing session management
   - Partial signature collection and aggregation
   - Pending transaction queue

3. **TUI Integration** (if applicable)
   - Group alias management interface
   - Signing workflow UI

### Phase 5: Testing & Hardening
**Estimated: 1-2 weeks**

1. **Unit Tests**
   - Musig2 group key derivation
   - Threshold signature verification
   - Registration validation

2. **Integration Tests**
   - Full registration flow
   - Multi-party signing
   - Update operations

3. **Security Audit**
   - Verify threshold signature security
   - Check for double-spend vectors
   - Review privacy implications

---

## 9. Privacy Considerations

### 9.1 On-Chain Privacy

- **Member hashes**: Store `cn_fast_hash(memberAddress)` on-chain, not raw addresses
- **Group pubkey**: Non-linkable to individual members
- **Transaction amounts**: Ring signatures still apply for transaction amounts

### 9.2 Off-Chain Privacy

- **Member list sharing**: Only shared among group members via encrypted channel
- **Signing coordination**: Use encrypted P2P messaging or external secure channel

### 9.3 Comparison with Regular Aliases

| Aspect | Regular Alias | Group Alias |
|--------|---------------|-------------|
| Owner visibility | Address hash only | Member hashes only |
| Linkability | Single owner | Distributed across M members |
| Transaction privacy | Standard ring sigs | Standard ring sigs + threshold |

---

## 10. Backward Compatibility

### 10.1 Existing Aliases

- Regular aliases (type 0, 1) continue to work unchanged
- Existing alias RPC commands remain functional
- No migration required for existing aliases

### 10.2 Version Handling

```cpp
// Version field in TransactionExtraGroupAliasRegistration
uint8_t version = 1;  // Initial version

// Future upgrades can increment version for new features
// Network can reject old versions after hard fork
```

---

## 11. Fee Structure

### 11.1 Registration Fee

```
Group Alias Registration Fee = (N + M) * BASE_FEE
                                (1 XFG * (members + threshold))
```

- More members = higher fee (more storage)
- Higher threshold = slightly higher fee (more validation work)

### 11.2 Update Fees

```
Member Add/Remove = BASE_FEE * 2
Threshold Change = BASE_FEE * 3
```

---

## 12. Open Questions & Design Decisions

### 12.1 For Discussion

1. **Key Image Distribution**: How should we distribute the key image secret among M signers?
   - Option A: TSS (Threshold Signature Scheme) - most secure, complex
   - Option B: Each signer holds partial, all M needed to reconstruct
   - Option C: Stealth addresses with rotating key images per transaction

2. **Member Removal**: Should removed members retain a "revocation key" to invalidate old signatures?

3. **Offline Signing**: How do members sign transactions when offline?
   - Option A: Watch-only wallet with pending queue
   - Option B: Hierarchical deterministic keys for offline signing

4. **Maximum Group Size**: Should there be a hard limit on N?
   - Suggested: N ≤ 15 for practical signature collection

5. **Minimum Threshold**: Should M ≥ 2 always? (Prevent single-point-of-failure)

6. **Group Alias Naming**: Should group aliases use a special naming convention?
   - Option A: Require `&` suffix (e.g., `firedao&`)
   - Option B: Special type byte distinguishes them

---

## 13. Future Enhancements

### 13.1 Potential Features

1. **Hierarchical Groups**: Groups within groups (DAOs, sub-committees)
2. **Time-Locked Spending**: Require M signatures + timelock for large amounts
3. **Role-Based Access**: Different thresholds for different operations
4. **Recovery Mechanisms**: Time-delayed recovery with guardian members
5. **Group Alias Auctions**: Market for desirable group alias names
6. **Cross-Chain Group Aliases**: Extend Musig2 groups to bridges

### 13.2 Integration Points

- **Atomic Swaps**: Group aliases can participate as swap counterparties
- **DIGM**: Groups can collectively own albums and receive listener payments
- **Elderfier Staking**: Groups can stake together (shared Elderfier deposit)
- **Governance**: Groups can participate in on-chain governance

---

## 14. File Modifications Summary

| File | Changes |
|------|---------|
| `src/CryptoNoteCore/AliasIndex.h/cpp` | Add GroupAliasEntry, group alias methods |
| `src/CryptoNoteCore/TransactionExtra.h/cpp` | Add GroupAliasRegistration, GroupAliasUpdate |
| `src/CryptoNoteCore/Blockchain.cpp` | Process 0xEB tag in block validation |
| `src/crypto/musig2.h/cpp` | Extend with key aggregation |
| `src/Rpc/CoreRpcServerCommandsDefinitions.h` | New RPC structures |
| `src/Rpc/RpcServer.cpp` | New RPC handlers |
| `src/SimpleWallet/SimpleWallet.cpp` | New CLI commands |
| `src/CryptoNoteConfig.h` | Fees, constants |
| `tui/tview_main.go` | TUI integration (optional) |
| `swapxfg/app/rpc.go` | Swap integration for group aliases |

---

## 15. Success Criteria

A group alias implementation is successful if:

1. **Functionality**
   - [ ] Groups can register aliases with M-of-N configuration
   - [ ] Groups can send transactions (threshold signature verification works)
   - [ ] Groups can receive transactions (funds accessible to group)
   - [ ] Groups can update membership and threshold

2. **Security**
   - [ ] No single member can unilaterally spend (unless M=1 explicitly chosen)
   - [ ] Double-spend protection works for group outputs
   - [ ] Signature verification is quantum-resistant (based on discrete log)

3. **Usability**
   - [ ] Clear CLI commands for all operations
   - [ ] Reasonable signing coordination overhead
   - [ ] No additional fee burden compared to equivalent multi-transaction flow

4. **Compatibility**
   - [ ] Existing aliases continue working
   - [ ] Existing RPC and wallet functionality preserved
   - [ ] No breaking changes to transaction format

---

*Document Version: 1.0*  
*Last Updated: April 2026*  
*Authors: Fuego Core Team*
