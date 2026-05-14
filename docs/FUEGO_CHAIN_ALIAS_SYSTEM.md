# Fuego Chain Alias System (On-Blockchain)

## Clarification: On-Fuego-Chain Aliases for Elderfiers

**Goal:** Elderfiers have 8-character aliases directly on Fuego blockchain (not L1/L2)

**Example:**
```
Elderfier address: XFG1a2b3c4d5e6f...
Alias: FIRENODE
Both tracked on Fuego chain
```

**This is MUCH simpler than L1/L2 alias registry!**

---

## Part 1: Why This is Better

### On-Fuego Approach (What You're Asking)
- **Storage:** Fuego blockchain
- **Lookup:** Query Fuego RPC directly
- **Scope:** Single blockchain (no cross-chain complexity)
- **Implementation:** Add to transaction extra or RPC
- **Cost:** Negligible (just RPC overhead)
- **Effort:** 3-4 days

### L1/L2 Approach (What I Was Overcomplicating)
- **Storage:** Solidity smart contract
- **Lookup:** Call Ethereum/Arbitrum contract
- **Scope:** Cross-chain coordination needed
- **Implementation:** Deploy contract + ABI
- **Cost:** Contract calls, gas
- **Effort:** 1-2 weeks

**Your idea is better!** Simpler, cheaper, faster.

---

## Part 2: Implementation on Fuego Chain

### Option A: Add to TransactionExtra

```cpp
// In TransactionExtra.h - NEW tag
static constexpr uint8_t TRANSACTION_EXTRA_TAG_ELDERFIER_ALIAS = 0xEA;

struct TransactionExtraElderfierAlias {
    uint8_t version = 1;
    std::string alias;  // Exactly 8 characters [A-Z0-9] UPPERCASE
    bytes32 aliasHash;  // keccak256(alias) for lookups
    bytes32 addressHash; // keccak256(elderfier_address) for privacy
};

// IMPORTANT: Alias Format Convention
// - Elderfiers (Phase 1): ALLCAPS [A-Z0-9] exactly 8 chars (e.g., FIRENODE)
// - Network-wide (Phase 3+): lowercase [a-z0-9] variable length (e.g., firenode)
// This makes elderfier vs regular user aliases visually distinct
```

**Validation (TransactionValidator.cpp):**
```cpp
bool validateElderfierAlias(const Transaction& tx) {
    auto alias = tx.extra.getElderfierAlias();

    // 1. Length check: exactly 8 characters (elderfier only)
    if (alias.alias.length() != 8) return false;

    // 2. Character check: [A-Z0-9] UPPERCASE ONLY (elderfier requirement)
    for (char c : alias.alias) {
        bool isUpper = (c >= 'A' && c <= 'Z');
        bool isDigit = (c >= '0' && c <= '9');
        if (!isUpper && !isDigit) return false;

        // Explicitly reject lowercase (reserved for Phase 3+ network-wide aliases)
        if (c >= 'a' && c <= 'z') return false;
    }

    // 3. No duplicates (query blockchain for existing alias)
    if (m_commitmentIndex.aliasExists(alias.aliasHash)) {
        return false;  // Alias already registered
    }

    return true;
}
```

**Format Convention:**
- **Elderfier aliases (Phase 1):** `FIRENODE` (ALLCAPS, 8 chars, [A-Z0-9])
- **Network aliases (Phase 3+):** `firenode` (lowercase, variable length, [a-z0-9])
- **Visual distinction:** Immediately clear if alias belongs to elderfier or regular user

### Option B: Dedicated RPC Endpoint

```cpp
// In CoreRpcServerCommandsDefinitions.h

struct RegisterElderfierAliasRequest {
    std::string elderfierAddress;
    std::string alias;  // 8 characters [A-Z0-9]
};

struct RegisterElderfierAliasResponse {
    bool registered;
    std::string message;
};
```

**But Option A is better** (immutable on-chain record).

---

## Part 3: Storage in CommitmentIndex

```cpp
// In CommitmentIndex.h

struct ElderfierAlias {
    std::string alias;           // "FIRENODE"
    bytes32 aliasHash;          // keccak256(alias)
    bytes32 addressHash;        // keccak256(address) for privacy
    std::string elderfierAddress; // Public address
    uint64_t registeredBlock;    // Block when registered
    uint64_t blockHeight;        // Current height
};

// In CommitmentIndex class
std::map<bytes32, ElderfierAlias> m_elderfierAliases;  // Hash → Alias
std::map<std::string, bytes32> m_aliasToHash;           // String → Hash (for lookup)
std::map<bytes32, std::string> m_addressToAlias;        // Address hash → Alias

// Methods
bool addElderfierAlias(const ElderfierAlias& alias);
std::optional<ElderfierAlias> getAliasByName(const std::string& alias) const;
std::optional<ElderfierAlias> getAliasByAddress(const bytes32& addressHash) const;
std::vector<ElderfierAlias> getAllAliases() const;
```

---

## Part 4: RPC Endpoints

### 1. Get Alias by Name

```bash
POST http://localhost:18180/json_rpc

Request:
{
  "jsonrpc": "2.0",
  "id": "0",
  "method": "get_elderfier_alias",
  "params": {
    "alias": "FIRENODE"
  }
}

Response:
{
  "jsonrpc": "2.0",
  "id": "0",
  "result": {
    "alias": "FIRENODE",
    "address_hash": "0x1f3e5d7c",
    "registered_block": 1000000,
    "is_valid": true
  }
}
```

### 2. Get Alias by Address

```bash
Request:
{
  "jsonrpc": "2.0",
  "id": "0",
  "method": "get_elderfier_alias_by_address",
  "params": {
    "address": "XFG1a2b3c4d5e6f..."
  }
}

Response:
{
  "jsonrpc": "2.0",
  "id": "0",
  "result": {
    "alias": "FIRENODE",
    "address": "XFG1a2b3c4d5e6f...",
    "registered_block": 1000000
  }
}
```

### 3. List All Aliases

```bash
Request:
{
  "jsonrpc": "2.0",
  "id": "0",
  "method": "get_all_elderfier_aliases",
  "params": {}
}

Response:
{
  "jsonrpc": "2.0",
  "id": "0",
  "result": {
    "aliases": [
      {
        "alias": "FIRENODE",
        "address": "XFG1a2b3c...",
        "registered_block": 1000000
      },
      {
        "alias": "XFG4LIFE",
        "address": "XFG2b3c4d...",
        "registered_block": 1000001
      },
      // ... more
    ],
    "total": 5
  }
}
```

---

## Part 5: CLI Integration

### Register Alias Command

```bash
$ xfg-stark-cli elderfier-stake set-alias \
  --alias FIRENODE \
  --address XFG1a2b3c4d5e6f...

Output:
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Registering alias on Fuego chain...

Creating transaction:
  - Alias: FIRENODE
  - Address: XFG1a2b3c4d5e6f...
  - Transaction tag: 0xEA (ELDERFIER_ALIAS)
  - Fee: 0.001 XFG

Submitting to network...

✓ Alias registered!
  - Transaction ID: 0x1234...
  - Confirmation: Block 1000100
  - Status: ACTIVE
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
```

### Query Alias Command

```bash
$ xfg-stark-cli elderfier-stake get-alias --address XFG1a2b3c4d5e6f...

Output:
Elderfier Alias Information
───────────────────────────
Alias: FIRENODE
Address: XFG1a2b3c4d5e6f...
Registered: Block 1000100
Status: ACTIVE
Days since registration: 25
```

### List All Aliases

```bash
$ xfg-stark-cli elderfier-alias list

Output:
Registered Elderfier Aliases
────────────────────────────────────────────
#  Alias       | Address            | Registered
───────────────────────────────────────────────
1  FIRENODE    | XFG1a2b3c4d...     | Block 1000100
2  XFG4LIFE    | XFG2b3c4d5e...     | Block 1000101
3  HODLER42    | XFG3c4d5e6f...     | Block 1000102
4  MOONBOUND   | XFG4d5e6f7g...     | Block 1000103
5  KEYSTROKE   | XFG5e6f7g8h...     | Block 1000104

Total: 5 registered aliases
```

---

## Part 6: Integration with Dynamigo

### Alias Registration Flow

```
User Creates Elderfier Stake (0xEF)
    ↓
User Registers Alias (0xEA transaction)
    ↓
Block includes both transactions
    ↓
CommitmentIndex tracks:
    - Stake (0xEF): Amount, blocks, unlock window
    - Alias (0xEA): Name, address, registration block
    ↓
RPC queries return complete profile:
    - Alias: FIRENODE
    - Stake: 1600 XFG
    - Status: Locked
    - Registered: Block 1000100
```

### Status Display After Alias Registration

```bash
$ xfg-stark-cli elderfier-stake status

Output:
Your Elderfier Registration Status
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Account: XFG1a2b3c4d...
Alias: FIRENODE ← NEW!
Total Staked: 1600 XFG
Deposit Count: 2/4

Deposits:
1. Amount: 800 XFG | Status: LOCKED | Unlock: 8d 2h
2. Amount: 800 XFG | Status: LOCKED | Unlock: 8d 2h

Registration Status: PENDING (awaiting governance vote)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
```

---

## Part 7: Privacy Considerations

### What's Public (On-Chain)

```
Alias: FIRENODE (public, anyone can see)
Registered Block: 1000100 (public, anyone can see)
Address Hash: 0x1f3e5d7c (semi-public, keccak256 of address)
Real Address: XFG1a2b3c4d... (can be public or private)
```

### Options for Address Privacy

**Option 1: Publish Real Address (Simple)**
- Alias → Real Address directly visible
- Privacy: 1/5 (no privacy)
- Simplicity: 5/5 (easiest)

**Option 2: Publish Hash Only (Recommended)**
- Alias → Hash (keccak256(address))
- Real address private (only owner knows)
- Privacy: 3/5 (good)
- Simplicity: 4/5 (still simple)

**Option 3: Zk-Proof (Future)**
- Alias registered with zk-proof
- Real address completely hidden
- Privacy: 5/5 (perfect)
- Simplicity: 1/5 (complex)
- **Defer to Phase 2 with circom**

**Recommendation:** Option 2 (Hash-based) for MVP

---

## Part 8: Implementation Checklist

### Fuego Blockchain Layer (C++)

- [ ] Add 0xEA tag to TransactionExtra.h
- [ ] Create TransactionExtraElderfierAlias struct
- [ ] Implement alias validation (8 chars, [A-Z0-9])
- [ ] Check for duplicate aliases on-chain
- [ ] Add alias processing to Blockchain::processCommitments()
- [ ] Implement CommitmentIndex::addElderfierAlias()
- [ ] Implement CommitmentIndex::getAliasByName()
- [ ] Implement CommitmentIndex::getAliasByAddress()
- [ ] Implement CommitmentIndex::getAllAliases()

### RPC Layer (C++)

- [ ] Add get_elderfier_alias() endpoint
- [ ] Add get_elderfier_alias_by_address() endpoint
- [ ] Add get_all_elderfier_aliases() endpoint
- [ ] Add to CoreRpcServerCommandsDefinitions.h

### CLI Layer (Rust)

- [ ] Implement `elderfier-stake set-alias` command
- [ ] Implement `elderfier-stake get-alias` command
- [ ] Implement `elderfier-alias list` command
- [ ] Validate alias format before submission
- [ ] Generate 0xEA transaction

### Testing

- [ ] Unit tests: Alias validation (length, characters)
- [ ] Unit tests: Duplicate detection
- [ ] Integration tests: Create alias → query RPC
- [ ] Integration tests: List all aliases
- [ ] Stress tests: 1000 aliases
- [ ] Edge cases: Reserved names, special chars

---

## Part 9: Reserved Aliases

**Option:** Prevent certain aliases from being registered

```cpp
std::set<std::string> RESERVED_ALIASES = {
    "FUEGO000",   // Reserved for foundation
    "ELDERFIRE",  // Reserved for team
    "COLD0000",   // Reserved
    "HEAT0000",   // Reserved
};

bool isReserved(const std::string& alias) {
    return RESERVED_ALIASES.count(alias) > 0;
}
```

**Or:** Allow governance to expand reserved list later.

---

## Part 10: Total Effort

### Time Estimate (Single Developer)

| Task | Hours |
|------|-------|
| TransactionExtra + validation | 4 |
| CommitmentIndex integration | 4 |
| RPC endpoints | 4 |
| CLI commands | 3 |
| Testing | 4 |
| Documentation | 2 |
| **TOTAL** | **21 hours** |

**Timeline: 2-3 days** (with full implementation + testing)

### Parallel with Dynamigo

Since Dynamigo is ~22-28 days, alias registration can be done:
- **Week 1, Days 1-2:** Implement alias system
- **Rest of Dynamigo:** Continue with fees, privacy, etc.

**Critical Path:** Alias system is FAST, doesn't block anything.

---

## Part 11: User Experience

### Complete Elderfier Registration Journey

```
Day 1: User creates 0xEF stake deposit
       → Transaction confirmed, stake locked

Day 2: User registers 0xEA alias (FIRENODE)
       → Transaction confirmed, alias visible in blockchain

Day 3: User checks status
       → Alias: FIRENODE ✓
       → Stake: 1600 XFG ✓
       → Status: Pending governance vote

Day 4-8: COLDAO governance votes on elderfier candidates
         → Passes vote, elderfier activated

Day 9: Elderfier starts running relay daemon
       → Merkle roots submitted to L1 contract
       → Fee accumulation begins
```

---

## Part 12: Comparison: On-Fuego vs. L1/L2

| Aspect | Fuego Chain | L1 Solidity | L2 Solidity |
|--------|------------|-----------|-----------|
| **Storage** | Fuego blockchain | Ethereum contract | Arbitrum contract |
| **Query** | Fuego RPC | Ethereum RPC | Arbitrum RPC |
| **Implementation** | Add to transaction tag | Deploy contract | Deploy contract |
| **Effort** | 21 hours | 2-3 weeks | 2-3 weeks |
| **Cost (registration)** | 0.001 XFG | ~50 GWEI (~$0.10) | ~0.01 GWEI (~$0.00001) |
| **Cost (lookup)** | Free (RPC) | Gas (contract call) | Gas (contract call) |
| **Privacy** | Hash-based (3/5) | Hash-based (3/5) | Hash-based (3/5) |
| **Uptime** | Depends on Fuego | Ethereum uptime | Arbitrum uptime |
| **Censorship resistance** | High | High | High |

**Winner:** Fuego Chain (simplest, cheapest, fastest)

---

## Summary

**On-Fuego-Chain Aliases:**
- ✅ 8-character aliases stored on Fuego blockchain
- ✅ Simple to implement (21 hours = 2-3 days)
- ✅ No L1/L2 contract needed
- ✅ Free to query (RPC only)
- ✅ Immutable (cannot be changed once registered)
- ✅ Can implement during Dynamigo (doesn't block anything)

**Implementation:**
1. Add 0xEA tag to TransactionExtra
2. Add CommitmentIndex methods for aliases
3. Create 3 RPC endpoints
4. Create 3 CLI commands
5. Test thoroughly

**Timeline:** 2-3 days (very fast!)

**Recommended Privacy:** Use hashed address on-chain, publish real address optionally.

This is perfect for Dynamigo phase!
