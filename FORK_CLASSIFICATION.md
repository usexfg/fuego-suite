# Fuego Source Code Fork Classification System

## Overview

This document provides a comprehensive classification system for all changes in Fuego source code, organized by their fork requirements. The system helps developers, node operators, and the community understand the impact of proposed changes and plan accordingly.

Fuego is a CryptoNote-based blockchain that has evolved through multiple hard forks (V2-V10), implementing advanced features including DMWDA (Dynamic Multi-Window Difficulty Algorithm), deposit system, burn proofs, FuegoTor privacy layer, and refined monetary policy. Understanding these fork requirements is critical for network upgrades, as demonstrated by the upgrade heights: 147958 (V2), 154321 (V3), 300000 (V4), 324819 (V5), 345678 (V6), 657000 (V7), 800000 (V8), 826420 (V9), and 999999 (V10).

The classification system is based on empirical analysis of the actual codebase structure, consensus mechanisms, and the upgrade detector pattern used by Fuego to coordinate network-wide changes.

## Fork Classification Levels

### 🔴 **Hard Fork Required**
Changes that break consensus and require all nodes to upgrade simultaneously. These include modifications to block validation rules, consensus algorithms, cryptographic primitives, economic parameters, or database schemas. In Fuego, these are coordinated through pre-defined upgrade heights or voting-based upgrade mechanisms.

### 🟡 **Soft Fork Compatible**
Changes that are backward compatible but may require network coordination. These are typically extensions to existing protocols where new nodes can still work with old nodes, but careful deployment management is needed. Examples include P2P protocol enhancements and new RPC methods.

### 🟢 **No Fork / On-the-Fly**
Changes that can be deployed without any network coordination. These include UI improvements, logging enhancements, configuration updates, and build system changes. They can be released through standard software updates.

---

## Mermaid Diagrams

### Fork Classification Flow

```mermaid
graph TD
    A[Proposed Change] --> B{Consensus Impact?}
    B -->|Modifies validation| C[🔴 Hard Fork]
    B -->|No consensus impact| D{Network Protocol?}
    D -->|Affects P2P/RPC| E[🟡 Soft Fork]
    D -->|Internal only| F[🟢 No Fork]
    
    C --> G[All nodes must upgrade]
    C --> H[Height-based or voting]
    E --> I[Backward compatible]
    E --> J[Gradual adoption]
    F --> K[Standard release]
    F --> L[Immediate deployment]
    
    H --> M[Pre-defined heights]
    H --> N[Voting mechanism]
    I --> O[Network monitoring]
    J --> P[Gentle rollout]
    K --> Q[CI/CD pipeline]
```

### Change Impact Matrix

```mermaid
graph LR
    A[Change Type] --> B[Consensus]
    A --> C[Protocol]
    A --> D[UI/UX]
    A --> E[Build/Dev]
    
    B --> F[🔴 Hard Fork]
    C --> G[🟡 Soft Fork]
    D --> H[🟢 No Fork]
    E --> I[🟢 No Fork]
    
    F --> J[Network Upgrade]
    F --> K[Height 147958-999999]
    G --> L[Compatibility Check]
    H --> M[User Update]
    I --> N[Developer Update]
    
    J --> O[Upgrade Detector]
    K --> P[Block Major Version]
    L --> Q[P2P Version]
    M --> R[UI Framework]
    N --> S[Build System]
```

### Implementation Workflow

```mermaid
flowchart TD
    A[Change Proposal] --> B{Fork Level?}
    
    B -->|🔴 Hard Fork| C[Community Review]
    C --> D[Reference Implementation]
    D --> E[Upgrade Coordination]
    E --> F[Simultaneous Upgrade]
    
    B -->|🟡 Soft Fork| G[Backward Compatible]
    G --> H[Gradual Deployment]
    H --> I[Network Monitoring]
    
    B -->|🟢 No Fork| J[Standard Development]
    J --> K[Testing]
    K --> L[Release]
    
    F --> M[Network Active V10]
    I --> N[Adoption Tracking]
    L --> O[Immediate Availability]
    
    E --> P[UpgradeDetector V10]
    I --> Q[Version Negotiation]
```

---

## 🔴 HARD FORK REQUIRED

### Core Consensus Changes

#### Block Structure & Validation

**Block Header Format Changes**
These changes modify the fundamental structure of blocks and require hard fork coordination:

```cpp
// Example: Evolution of BlockHeader across Fuego versions
// V2-V4: Basic structure
struct BlockHeader {
    uint8_t majorVersion;      // BLOCK_MAJOR_VERSION_2/3/4
    uint8_t minorVersion;      // BLOCK_MINOR_VERSION_0/1
    uint32_t nonce;
    uint64_t timestamp;
    Crypto::Hash previousBlockHash;
};

// V5-V6: CN7/CN8 variants
struct BlockHeader {
    uint8_t majorVersion;      // BLOCK_MAJOR_VERSION_5/6
    uint8_t minorVersion;      // Additional features
    uint32_t nonce;
    uint64_t timestamp;
    Crypto::Hash previousBlockHash;
    // New optional fields for variant algorithms
    uint8_t variant;           // CN variant selector
};

// V7-V10: Fango/Fuego with deposits
struct BlockHeader {
    uint8_t majorVersion;      // BLOCK_MAJOR_VERSION_7/8/9/10
    uint8_t minorVersion;      // Feature flags
    uint32_t nonce;
    uint64_t timestamp;
    Crypto::Hash previousBlockHash;
    std::optional<DepositRoot> depositRoot;  // V8+
    std::optional<BurnProof> burnProof;      // V9+
};
```

**Transaction Format Changes**
```cpp
// V2-V6: Legacy transaction structure
struct TransactionPrefix {
    uint8_t version;           // TRANSACTION_VERSION_1/2
    uint64_t unlockTime;
    TransactionInputs inputs;
    std::vector<TransactionOutput> outputs;
    std::vector<uint8_t> extra;
};

// V7-V10: Enhanced transaction features
struct TransactionPrefix {
    uint8_t version;           // TRANSACTION_VERSION_2+
    uint64_t unlockTime;
    TransactionInputs inputs;
    std::vector<TransactionOutput> outputs;
    std::vector<uint8_t> extra;
    // Deposit commitment for COLD/YIELD transactions
    std_optional<Crypto::Hash> depositCommitment;
};
```

**Consensus Rule Changes**
```cpp
// In CryptoNoteConfig.h - V2-V10 parameter evolution
// Block size limits
const size_t CRYPTONOTE_BLOCK_GRANTED_FULL_REWARD_ZONE_V1  = 20000;    // V1
const size_t CRYPTONOTE_BLOCK_GRANTED_FULL_REWARD_ZONE_V2  = 430080;  // V2-V4
const size_t CRYPTONOTE_BLOCK_GRANTED_FULL_REWARD_ZONE_CURRENT = 430080; // V5+

// Difficulty target evolution
const uint64_t DIFFICULTY_TARGET_DRGL = 81;      // V2-V6 (81 sec)
const uint64_t DIFFICULTY_TARGET = 480;          // V7-V10 (480 sec)

// Upgrade heights for hard forks
const uint32_t UPGRADE_HEIGHT_V2 = 147958;   // Hardhome
const uint32_t UPGRADE_HEIGHT_V3 = 154321;   // Longclaw
const uint32_t UPGRADE_HEIGHT_V4 = 300000;   // Dracarys
const uint32_t UPGRADE_HEIGHT_V5 = 324819;   // Ironborn (CN7)
const uint32_t UPGRADE_HEIGHT_V6 = 345678;   // Ice&fire (CN8)
const uint32_t UPGRADE_HEIGHT_V7 = 657000;   // Apotheosis (Fango)
const uint32_t UPGRADE_HEIGHT_V8 = 800000;   // Dragonborne (emission|deposits)
const uint32_t UPGRADE_HEIGHT_V9 = 826420;   // Godflame (emission|UPX2|Fuego)
const uint32_t UPGRADE_HEIGHT_V10 = 999999;  // Dynamigo (dmwda|dynamaxin|ethernalXFG)
```

#### Cryptographic Changes

**Hash Algorithm Changes**
```cpp
// V2-V4: CryptoNote standard
// V5-V6: CryptoNote variants
bool checkProofOfWork(const Block& block, difficulty_type current_diffic) {
    Crypto::Hash block_hash = get_block_hash(block);
    uint64_t high;
    uint64_t low = mul128(current_diffic, &high);
    return check_hash(block_hash, current_diffic);
}

// V7-V10: Enhanced PoW with variants
bool checkProofOfWorkV7(const Block& block, difficulty_type current_diffic) {
    Crypto::Hash block_hash;
    if (block.majorVersion >= BLOCK_MAJOR_VERSION_7) {
        // Use ASIC-resistant variant
        block_hash = get_block_hash_variant(block, VARIANT_CN_FANGO);
    } else {
        block_hash = get_block_hash(block);
    }
    return check_hash(block_hash, current_diffic);
}
```

**Signature Scheme Changes**
```cpp
// V2-V6: Standard ring signatures
bool check_tx_input(const KeyInput& txin, const Crypto::Hash& tx_prefix_hash, 
                   const std::vector<Crypto::Signature>& sig, uint32_t* pmax_related_block_height) {
    // Standard CryptoNote ring signature validation
    std::vector<const Crypto::PublicKey *> output_keys;
    // ... ring signature check
    return Crypto::check_ring_signature(tx_prefix_hash, txin.keyImage, output_keys, sig.data());
}

// V10: Dynamic mixin requirements + FuegoTor
bool validateTransactionV10(const Transaction& tx, uint64_t mixin_limit) {
    if (tx.majorVersion >= BLOCK_MAJOR_VERSION_10) {
        // DMWDA requires dynamic mixin validation
        uint64_t min_mixin = MIN_TX_MIXIN_SIZE_V10;  // 8
        return checkMixinCompatibility(tx, min_mixin, MAX_TX_MIXIN_SIZE);
    }
    return checkMixinCompatibility(tx, MIN_TX_MIXIN_SIZE_V2, MAX_TX_MIXIN_SIZE);
}
```

#### Network Protocol Changes

**P2P Protocol Format Changes**
```cpp
// V2-V4: Legacy P2P
const uint8_t P2P_VERSION_1 = 1;
const uint8_t P2P_VERSION_2 = 2;
const uint8_t P2P_CURRENT_VERSION = 1;  // V2-V4
const uint8_t P2P_MINIMUM_VERSION = 1;

// V7-V10: Enhanced P2P
const uint8_t P2P_VERSION_3 = 3;        // Lite blocks support
const uint8_t P2P_CURRENT_VERSION = 1;  // Still compatible
const uint8_t P2P_LITE_BLOCKS_PROPOGATION_VERSION = 3;  // V7+
```

**Address Format Changes**
```cpp
// V2-V6: Standard CryptoNote addresses
const uint64_t CRYPTONOTE_PUBLIC_ADDRESS_BASE58_PREFIX = 1753191;  // "fire"

// V8-V10: Deposit-enhanced addresses
struct AccountPublicAddress {
    Crypto::PublicKey spendPublicKey;
    Crypto::PublicKey viewPublicKey;
    // V8+: Deposit capability flag
    bool supportsDeposits;  // COLD/YIELD addresses
};
```

### Economic Model Changes

#### Monetary Policy

**Emission Curve Evolution**
```cpp
// V2-V6: Legacy emission (Karbowanec-based)
const unsigned EMISSION_SPEED_FACTOR = 18;

// V7-V8: Fango emission
const unsigned EMISSION_SPEED_FACTOR_FANGO = 19;  // Major version 8

// V9-V10: Fuego emission
const unsigned EMISSION_SPEED_FACTOR_FUEGO = 20;  // Major version 9

// Implementation in Currency.cpp
bool Currency::getBlockReward(uint8_t blockMajorVersion, size_t medianSize, 
                             size_t currentBlockSize, uint64_t alreadyGeneratedCoins,
                             uint64_t fee, uint32_t height, uint64_t& reward, 
                             int64_t& emissionChange) const {
    unsigned int m_emissionSpeedFactor = emissionSpeedFactor(blockMajorVersion);
    
    // V7-V8: Fango calculation
    if (blockMajorVersion >= BLOCK_MAJOR_VERSION_7) {
        uint64_t baseReward = (moneySupply - alreadyGeneratedCoins) >> m_emissionSpeedFactor;
        return calculateDynamicReward(baseReward, medianSize, currentBlockSize);
    }
    
    // V9-V10: Fuego with fixed emission
    if (blockMajorVersion >= BLOCK_MAJOR_VERSION_9) {
        uint64_t baseReward = (parameters::MONEY_SUPPLY - alreadyGeneratedCoins) >> m_emissionSpeedFactor;
        uint64_t fixedReward = baseReward * 95 / 100;  // 95% to miner, 5% to treasury
        return applyRewardModifiers(fixedReward, medianSize, currentBlockSize);
    }
    
    // Legacy V2-V6
    uint64_t baseReward = (parameters::MONEY_SUPPLY - alreadyGeneratedCoins) >> m_emissionSpeedFactor;
    return calculateLegacyReward(baseReward, medianSize, currentBlockSize);
}
```

**Maximum Supply Changes**
```cpp
// V2-V6: 9M supply
const uint64_t MONEY_SUPPLY = UINT64_C(9000000000000000);  // 9M

// V7-V10: 8M8 supply with burn mechanism
const uint64_t MONEY_SUPPLY = UINT64_C(80000088000008);    // 8M8

// Burn mechanism (V9+)
const uint64_t BURN_DEPOSIT_MIN_AMOUNT = 8000000;           // 0.8 XFG
const uint64_t BURN_DEPOSIT_LARGE_AMOUNT = 8000000000;      // 800 XFG
const uint64_t MINIMUM_FEE_BURN_SMALL = 80000;              // 0.008 XFG
const uint64_t MINIMUM_FEE_BURN_LARGE = 800000000;          // 0.8 XFG
```

#### Smart Contract Changes

**Deposit System (V8+)**
```cpp
// V8: Deposit commitment structure added to transactions
struct DepositCommitment {
    uint64_t amount;
    uint32_t term;  // 16440 blocks (3 months) or DEPOSIT_TERM_FOREVER
    Crypto::Hash commitmentHash;
    AccountPublicAddress provider;  // Deposit provider
};

// V8: Deposit validation in Currency.cpp
bool Currency::validateDeposit(const Transaction& tx, uint32_t currentHeight) const {
    if (tx.version >= TRANSACTION_VERSION_2) {
        for (const auto& input : tx.inputs) {
            if (input.type() == typeid(MultisignatureInput)) {
                const auto& msInput = boost::get<MultisignatureInput>(input);
                if (msInput.term > 0) {
                    // Validate deposit terms
                    if (msInput.term < DEPOSIT_MIN_TERM || msInput.term > DEPOSIT_MAX_TERM) {
                        return false;
                    }
                    // V8-V9: 16440 blocks (3 months)
                    // V10: Additional yield deposit validation
                    if (blockMajorVersion >= BLOCK_MAJOR_VERSION_10) {
                        return validateYieldDeposit(msInput, currentHeight);
                    }
                }
            }
        }
    }
    return true;
}
```

**Virtual Machine Changes**
```cpp
// V7-V10: No formal VM, but deposit processing logic
// V8: Deposit index management
class BankingIndex {
public:
    bool addDeposit(const DepositEntry& entry);
    bool unlockDeposit(const KeyImage& keyImage);
    uint64_t calculateYield(uint32_t term, uint64_t amount) const;
};

// V10: Enhanced deposit yield calculation
double calculateYieldV10(uint32_t term, uint64_t amount, uint8_t depositType) const {
    // COLD deposit: 8% APY
    // YIELD deposit: up to 15% APY
    // BURN deposit: immediate burn, no yield
    const double baseRate = (depositType == YIELD_DEPOSIT) ? 0.15 : 0.08;
    double termFactor = term / (3 * 5480.0);  // 3 months base
    return amount * baseRate * termFactor;
}
```

### Database Schema Changes

**Blockchain Storage Format**
```cpp
// V2-V6: Legacy storage
struct BlockEntry {
    Block bl;
    TransactionEntries transactions;
};

// V7-V10: Enhanced storage with deposit tracking
struct BlockEntry {
    Block bl;
    TransactionEntries transactions;
    // V8+: Deposit state
    std::vector<DepositEntry> deposits;
    // V9+: Burn proofs
    std::vector<BurnProofEntry> burnProofs;
    // V10+: DMWDA metadata
    DMWDAStats dmwdaStats;
};
```

**Transaction Indexing**
```cpp
// V2-V6: Simple hash→block mapping
std::unordered_map<Crypto::Hash, TransactionIndex> m_transactionMap;

// V7-V10: Enhanced indexing
class BlockchainIndices {
public:
    // Deposit commitment index (V8+)
    std::unordered_map<Crypto::Hash, DepositIndex> m_depositMap;
    // Key image→deposit mapping (V8+)
    std::unordered_map<Crypto::KeyImage, DepositEntry> m_keyImageDepositMap;
    // Burn proof index (V9+)
    std::unordered_map<Crypto::Hash, BurnProofIndex> m_burnProofMap;
    // V10: DMWDA stats per block
    std::map<uint32_t, DMWDAStats> m_dmwdaStats;
};
```

---

## 🟡 SOFT FORK COMPATIBLE

### Network Improvements

#### Protocol Enhancements

**Block Propagation (V7+)**
```cpp
// V7-V10: Lite block support (backward compatible)
struct LiteBlock {
    BlockHeader header;
    std::vector<Crypto::Hash> transactionHashes;
    std::vector<Transaction> missingTxs;  // Only transactions not in mempool
};

// P2P message types (soft fork - new message type)
const uint32_t COMMAND_LITE_BLOCK = 2001;  // New message

// Backward compatible handling
void handleLiteBlock(const LiteBlock& block) {
    // Old nodes ignore unknown message types
    // New nodes process lite blocks efficiently
    if (!validateLiteBlock(block)) {
        // Fall back to full block request
        requestFullBlock(block.header.previousBlockHash);
    }
}
```

**Peer Discovery (V8+)**
```cpp
// V8+: Enhanced peer discovery
class PeerManager {
public:
    // V8: Add deposit-aware peer selection
    std::vector<PeerlistEntry> getDepositAwarePeers() {
        // Prefer nodes supporting deposit queries
        return filterPeers([&](const PeerlistEntry& peer) {
            return peer.version >= P2P_VERSION_DEPOSIT_AWARE;
        });
    }
    
    // V10: DMWDA-aware peer selection
    std::vector<PeerlistEntry> getDMWDAAwarePeers() {
        // Prefer nodes with stable difficulty
        return sortPeersByStability();
    }
};
```

#### Privacy Enhancements

**Ring Signature Improvements**
```cpp
// V2-V9: Fixed ring size
const uint64_t MIN_TX_MIXIN_SIZE_V2 = 2;
const uint64_t MIN_TX_MIXIN_SIZE_V10 = 8;

// V10: Dynamic ring size with DMWDA
bool validateMixinV10(const Transaction& tx, uint64_t currentDiffic, uint32_t height) {
    // Dynamic mixin based on difficulty
    uint64_t requiredMixin = calculateDynamicMixin(currentDiffic, height);
    
    for (const auto& input : tx.inputs) {
        if (input.type() == typeid(KeyInput)) {
            const auto& keyInput = boost::get<KeyInput>(input);
            if (keyInput.outputIndexes.size() < requiredMixin) {
                return false;  // Soft failure - mature nodes reject
            }
        }
    }
    return true;
}
```

**FuegoTor Integration (V10)**
```cpp
// V10: Optional privacy layer
struct FuegoTorConfig {
    bool torEnabled;
    std::vector<std::string> torBridges;
    uint16_t torSocksPort;
    // Soft fork: Only affects nodes that enable it
};

class FuegoTorManager {
public:
    // V10: Privacy-enhanced transactions
    bool sendPrivateTransaction(const Transaction& tx) {
        // Route through Tor if enabled
        if (m_torEnabled && m_torConnected) {
            return m_torConnection->send(tx);
        }
        return m_p2p->broadcast(tx);  // Fallback
    }
};
```

### Performance Optimizations

#### Database Optimizations (V7+)

**Caching Strategies**
```cpp
// V7-V10: Enhanced caching
class BlockchainCache {
public:
    // V7: Block cache
    std::unordered_map<uint32_t, BlockEntry> m_blockCache;
    
    // V8: Deposit cache (hot deposits)
    std::unordered_map<DepositKey, DepositEntry> m_hotDepositCache;
    
    // V10: DMWDA stats cache
    std::map<uint32_t, DMWDAStats> m_dmwdaCache;
    
    bool getBlockCached(uint32_t height, BlockEntry& entry) {
        auto it = m_blockCache.find(height);
        if (it != m_blockCache.end()) {
            entry = it->second;
            return true;
        }
        return false;
    }
};
```

**Query Performance**
```cpp
// V8+: Optimized deposit queries
class DepositIndex {
public:
    // Range query for yield calculation
    std::vector<DepositEntry> getExpiringDeposits(uint32_t currentHeight, uint32_t window) {
        // Optimized with height range index
        return m_heightIndex.range_query(currentHeight, currentHeight + window);
    }
    
    // V10: Time-weighted average queries
    std::vector<DepositEntry> getActiveDeposits(uint64_t minAmount, uint32_t minHeight) {
        auto builder = m_queryBuilder.begin();
        builder.addMinAmountFilter(minAmount);
        builder.addHeightFilter(minHeight);
        return builder.execute();
    }
};
```

#### Memory Usage (V10)

**Memory Pool Management**
```cpp
// V10: Optimized mempool with deposit awareness
class TransactionPool {
public:
    // V10: Fee-based prioritization
    bool addTransactionV10(const Transaction& tx, uint64_t feePerByte) {
        // Dynamic fee calculation
        uint64_t minFee = calculateDynamicMinFee(tx, feePerByte);
        if (tx.fee < minFee) {
            return false;
        }
        
        // Deposit validation
        if (containsDeposit(tx)) {
            return validateDepositTransaction(tx);
        }
        
        // Add to priority queue
        m_priorityQueue.push(tx, tx.fee / getTransactionSize(tx));
        return true;
    }
};
```

### API and RPC Changes

#### New RPC Methods

```cpp
// V8+: Deposit RPC methods
class DepositRPC {
public:
    // Get deposit information
    json getDepositInfo(const std::string& depositId) {
        // V8: Basic deposit lookup
        // V10: Enhanced with yield calculations
        auto entry = m_blockchain.getDeposit(depositId);
        return buildDepositJSON(entry, entry.term, entry.amount, currentHeight);
    }
    
    // Calculate estimated yield
    json estimateYield(const std::string& amount, const std::string& term, uint8_t type) {
        uint64_t amt = parseAmount(amount);
        uint32_t trm = parseTerm(term);
        double yield = m_currency.calculateYield(trm, amt, type);
        return json{{"yield", formatYield(yield)}, {"apy", calculateAPY(yield, trm)}};
    }
};

// V9+: Burn proof RPC
class BurnRPC {
public:
    json submitBurnProof(const std::string& txId, const std::string& burnData) {
        BurnProof proof = parseBurnProof(burnData);
        if (m_blockchain.validateBurnProof(proof)) {
            uint64_t burned = m_blockchain.submitBurnProof(proof);
            return json{{"status", "accepted"}, {"burnedAmount", burned}};
        }
        return json{{"status", "rejected"}, {"error", "Invalid proof"}};
    }
};

// V10: DMWDA monitoring
class DMWDARPC {
public:
    json getDMWDAStats(uint32_t height) {
        DMWDAStats stats = m_blockchain.getDMWDAStats(height);
        return json{
            {"window_short", stats.shortWindow},
            {"window_medium", stats.mediumWindow},
            {"window_long", stats.longWindow},
            {"confidence", stats.confidence},
            {"hashrate", stats.estimatedHashrate}
        };
    }
};
```

#### API Enhancements

```cpp
// V7-V10: Enhanced wallet RPC
class WalletRPC {
public:
    // V8: Deposit creation
    json createDeposit(const json& params) {
        uint64_t amount = params["amount"];
        uint32_t term = params["term"];
        uint8_t type = params["type"];  // COLD, YIELD, BURN
        
        // V10: Enhanced validation
        if (m_majorVersion >= BLOCK_MAJOR_VERSION_10) {
            validateDepositEnhanced(amount, term, type);
        }
        
        Transaction tx = m_wallet.createDeposit(amount, term, type);
        return json{{"tx_hash", tx.hash}, {"fee", tx.fee}};
    }
    
    // V10: Private transaction
    json sendPrivate(const json& params) {
        // FuegoTor integration
        if (m_torManager->isAvailable()) {
            return m_torManager->sendPrivate(params);
        }
        return m_p2p->send(params);  // Standard
    }
};
```

---

## 🟢 NO FORK / ON-THE-FLY CHANGES

### User Interface Improvements

#### CLI Enhancements

```cpp
// V8-V10: Enhanced CLI with deposit support
class CLIWallet {
public:
    void processCommand(const std::string& cmd) {
        if (cmd == "deposit") {
            // V8+: Deposit commands
            handleDepositCommands();
        } else if (cmd == "burn") {
            // V9+: Burn commands
            handleBurnCommands();
        } else if (cmd == "dmwda") {
            // V10: DMWDA monitoring
            handleDMWDACommands();
        } else {
            // Legacy commands stay same
            handleLegacyCommands(cmd);
        }
    }
    
    void printDepositDetails(const DepositEntry& deposit) {
        // Improved formatting
        std::cout << "Deposit: " << deposit.id << "\n";
        std::cout << "Amount: " << formatXFG(deposit.amount) << " XFG\n";
        std::cout << "Term: " << deposit.term << " blocks (" 
                  << (deposit.term / 5480) << " months)\n";
        
        // V10: Show estimated yield
        if (deposit.type != BURN_DEPOSIT) {
            double yield = m_currency.calculateYield(deposit.term, deposit.amount, deposit.type);
            std::cout << "Estimated Yield: " << formatYield(yield) << " XFG\n";
            std::cout << "APY: " << calculateAPY(yield, deposit.term) << "%\n";
        }
    }
};
```

#### Web Interface Updates

```javascript
// V8-V10: Web UI enhancements (no fork)
// depositDashboard.js
class DepositDashboard {
    // V8: Real-time deposit monitoring
    async updateDepositList() {
        const deposits = await rpc.getDeposits(this.accountAddress);
        this.renderDeposits(deposits);
        
        // V10: Show yield projections
        if (this.showYield) {
            projections = await rpc.estimateYield(deposits);
            this.renderProjections(projections);
        }
    }
    
    // V9: Burn proof interface
    async initBurnProof() {
        const burnForm = document.getElementById('burn-form');
        burnForm.addEventListener('submit', async (e) => {
            e.preventDefault();
            const txId = document.getElementById('tx-hash').value;
            const result = await rpc.submitBurnProof(txId);
            this.showBurnResult(result);
        });
    }
    
    // V10: DMWDA statistics dashboard
    async updateDMWDAStats() {
        const stats = await rpc.getDMWDAStats();
        this.renderDifficultyChart(stats);
        this.renderHashrateEstimate(stats.hashrate);
    }
}
```

### Documentation and Configuration

#### Configuration Changes

```cpp
// V8-V10: New config parameters (no impact on consensus)
struct CoreConfig {
    // V8: Deposit configuration
    uint64_t depositMinAmount = parameters::DEPOSIT_MIN_AMOUNT;
    uint32_t depositMinTerm = parameters::DEPOSIT_MIN_TERM;
    
    // V9: Burn configuration  
    uint64_t burnMinAmount = parameters::BURN_DEPOSIT_MIN_AMOUNT;
    uint64_t burnLargeAmount = parameters::BURN_DEPOSIT_LARGE_AMOUNT;
    
    // V10: DMWDA and Tor configuration
    bool dmwdaEnabled = true;
    bool torEnabled = false;
    uint16_t torSocksPort = 9050;
    std::vector<std::string> torBridges;
    
    // V10: Performance tuning
    size_t blockCacheSize = 1000;  // MB
    size_t depositCacheSize = 50;  // MB
    bool enableLiteBlocks = true;
};

// config.json example (user configuration, no fork)
{
  "deposit": {
    "min_amount": "800.0",
    "min_term_blocks": 16440
  },
  "burn": {
    "small_amount": "0.8",
    "large_amount": "800.0"
  },
  "dmwda": {
    "enabled": true,
    "monitor": true
  },
  "tor": {
    "enabled": false,
    "bridges": ["bridge.example.com:9999"]
  },
  "performance": {
    "block_cache_mb": 1000,
    "deposit_cache_mb": 50
  }
}
```

#### Documentation Updates

```cpp
// V7-V10: Code documentation (generates better help text)
class HelpPrinter {
public:
    void printDepositHelp() {
        // V8: Deposit help
        std::cout << "DEPOSIT COMMANDS:\n";
        std::cout << "  deposit create <amount> <term> <type> - Create COLD/YIELD deposit\n";
        std::cout << "  deposit info <id> - Show deposit details\n";
        std::cout << "  deposit list - Show all deposits\n";
        
        // V10: Enhanced with yield info
        if (m_majorVersion >= BLOCK_MAJOR_VERSION_10) {
            std::cout << "  deposit estimate <amount> <term> - Calculate estimated yield\n";
        }
    }
    
    void printBurnHelp() {
        // V9: Burn help
        std::cout << "BURN COMMANDS:\n";
        std::cout << "  burn create <amount> - Create burn transaction\n";
        std::cout << "  burn proof <tx_id> - Submit burn proof\n";
        std::cout << "  burn verify <proof> - Verify burn proof\n";
    }
    
    void printDMWDAHelp() {
        // V10: DMWDA monitoring help
        std::cout << "DMWDA COMMANDS:\n";
        std::cout << "  dmwda stats - Current difficulty algorithm stats\n";
        std::cout << "  dmwda history - Historical DMWDA data\n";
        std::cout << "  dmwda estimate - Next block difficulty estimate\n";
    }
};
```

### Build System and Development

#### Build System Improvements

```cmake
# CMakeLists.txt - V7-V10 additions (no impact on consensus)

# V8: Deposit validation tests
if(BUILD_TESTS)
    add_executable(test_deposits
        src/CryptoNoteCore/DepositValidation.cpp
        src/CryptoNoteCore/DepositTests.cpp
    )
    target_link_libraries(test_deposits CryptoNoteCore)
endif()

# V9: Burn proof verification
if(BUILD_BURN_TOOLS)
    add_executable(burn_verifier
        src/CryptoNoteCore/BurnProof.cpp
        src/CryptoNoteCore/BurnVerification.cpp
    )
endif()

# V10: DMWDA and Tor components
if(BUILD_DMWDA)
    add_library(dmwda STATIC
        src/CryptoNoteCore/DMWDA.cpp
        src/CryptoNoteCore/DynamicDifficulty.cpp
    )
endif()

if(BUILD_TOR)
    add_library(fuego_tor STATIC
        src/FuegoTor/TorManager.cpp
        src/FuegoTor/TorConnection.cpp
    )
    target_link_libraries(fuego_tor tor)
endif()
```

#### Development Tools

```bash
# V8-V10: New build targets (standard development)
make deposit_tools      # Build deposit management utilities
make burn_tools        # Build burn proof utilities  
make dmwda_test        # DMWDA simulation tools
make fuego_tor         # Tor integration (optional)

# V10: Performance profiling
make profile_blocks    # Block validation profiling
make profile_deposits  # Deposit query profiling
make profile_dmwda     # Difficulty algorithm profiling

# Standard testing for all changes
make test             # Unit tests
make integration_test # Integration tests
make security_audit   # Security checks
```

### Monitoring and Observability

#### Metrics and Telemetry

```cpp
// V8-V10: Monitoring improvements
class MetricsCollector {
public:
    // V8: Deposit metrics
    void recordDepositMetrics() {
        m_metrics["deposits.active"] = m_blockchain.getActiveDepositCount();
        m_metrics["deposits.total_value"] = m_blockchain.getTotalDepositValue();
        m_metrics["deposits.yield_paid"] = m_blockchain.getTotalYieldPaid();
    }
    
    // V9: Burn metrics
    void recordBurnMetrics() {
        m_metrics["burn.total_burned"] = m_blockchain.getTotalBurned();
        m_metrics["burn.transactions"] = m_blockchain.getBurnTransactionCount();
        m_metrics["burn.proof_validation"] = m_blockchain.getBurnProofSuccessRate();
    }
    
    // V10: DMWDA metrics
    void recordDMWDAMetrics() {
        auto stats = m_blockchain.getDMWDAStats();
        m_metrics["dmwda.confidence"] = stats.confidence;
        m_metrics["dmwda.hashrate"] = stats.estimatedHashrate;
        m_metrics["dmwda.stability"] = stats.stabilityScore;
        m_metrics["dmwda.adjustment"] = stats.lastAdjustment;
    }
    
    // V10: FuegoTor metrics
    void recordTorMetrics() {
        if (m_torManager) {
            m_metrics["tor.connected"] = m_torManager->isConnected();
            m_metrics["tor.circuit_count"] = m_torManager->getCircuitCount();
            m_metrics["tor.latency_ms"] = m_torManager->getAverageLatency();
        }
    }
};
```

#### Logging Enhancements

```cpp
// V7-V10: Structured logging
class Logger {
public:
    // V8: Deposit logging
    void logDeposit(const DepositEntry& entry, const std::string& action) {
        logger(INFO, BRIGHT_GREEN) << "DEPOSIT " << action
            << ": id=" << entry.id
            << ", amount=" << formatXFG(entry.amount)
            << ", term=" << entry.term
            << ", type=" << getDepositTypeName(entry.type);
    }
    
    // V9: Burn logging with proof
    void logBurn(const BurnProof& proof, bool verified) {
        logger(verified ? INFO : WARNING, verified ? BRIGHT_GREEN : BRIGHT_RED)
            << "BURN PROOF " << (verified ? "VERIFIED" : "FAILED")
            << ": tx_hash=" << proof.transactionHash
            << ", burned=" << formatXFG(proof.burnedAmount)
            << ", proof=" << proof.proofHash;
    }
    
    // V10: DMWDA dynamic logging
    void logDMWDA(const DMWDAStats& stats, bool anomalyDetected) {
        if (anomalyDetected) {
            logger(WARNING, BRIGHT_YELLOW) << "DMWDA ANOMALY"
                << ": confidence=" << stats.confidence
                << ", hash_change=" << stats.hashrateChange
                << ", recommended_action=" << stats.recommendedAction;
        } else {
            logger(TRACE, BRIGHT_WHITE) << "DMWDA NORMAL"
                << ": confidence=" << stats.confidence
                << ", difficulty=" << stats.nextDifficulty;
        }
    }
    
    // V10: Performance logging (no impact)
    void logPerformance(const char* operation, uint64_t durationMs) {
        if (durationMs > 1000) {  // Only log slow operations
            logger(DEBUGGING) << "SLOW OP " << operation << ": " << durationMs << "ms";
        }
    }
};
```

---

## Implementation Guidelines

### Hard Fork Process

#### Phase 1: Proposal and Design (4-6 weeks)

```mermaid
flowchart TD
    A[Community Proposal] --> B[Technical Specification]
    B --> C[Security Review]
    C --> D[Implementation Plan]
    D --> E[Reference Code]
    
    A --> F[RFC Document]
    F --> G[Community Feedback]
    G --> H[Revision Cycle]
    H --> I[Approval]
    
    E --> J[Testnet Implementation]
    I --> J
    J --> K[Testnet Activation]
    K --> L[2-4 week testing]
    L --> M[Mainnet Coordination]
    
    M --> N[Exchange Notification]
    M --> O[Pool Notification]
    M --> P[Service Provider Notification]
    M --> Q[User Documentation]
```

**Fuego-Specific Requirements:**
- **Upgrade Height Voting**: If no pre-defined height, requires 90% voting threshold over UPGRADE_VOTING_WINDOW (1 day)
- **Version Checking**: Block major version must increment at upgrade height + 1
- **UpgradeDetector Pattern**: Use BasicUpgradeDetector class to coordinate
- **Economic Impact Analysis**: Required for all emission changes

#### Phase 2: Implementation (2-3 weeks)

```cpp
// Reference implementation structure
class HardForkReferenceImpl {
public:
    // 1. Fork detection
    bool isForkActivated(uint32_t height) const {
        return height >= getForkHeight();
    }
    
    // 2. Version enforcement
    bool validateBlockVersion(const Block& block) const {
        uint8_t expected = getExpectedVersion(block.height);
        if (block.majorVersion > expected) {
            return false;  // Future version
        }
        if (block.majorVersion < expected - 1) {
            return false;  // Past version
        }
        return true;
    }
    
    // 3. Parameter transition
    difficulty_type getDifficulty(uint32_t height) const {
        if (height >= getForkHeight()) {
            return nextDifficultyV5(timestamps, difficulties);  // New
        } else {
            return nextDifficultyV4(timestamps, difficulties);  // Old
        }
    }
    
    uint64_t getBlockReward(uint8_t version, uint32_t height) const {
        if (version >= BLOCK_MAJOR_VERSION_9) {
            return calculateFuegoReward(height);  // V9+ formula
        } else if (version >= BLOCK_MAJOR_VERSION_7) {
            return calculateFangoReward(height);  // V7-8 formula
        } else {
            return calculateLegacyReward(height); // V2-6 formula
        }
    }
};
```

#### Phase 3: Coordination (1-2 weeks)

```mermaid
flowchart TD
    A[Set Activation Height] --> B[Generate Genesis Block]
    C[Prepare Release Binaries] --> D[Create Release Notes]
    E[Testnet Final Testing] --> F[Sign Off]
    B --> G[Pre-announce Height]
    D --> G
    F --> H[Mainnet Release]
    H --> I[Monitor Adoption]
    I --> J[Height Simulation]
    J --> K[24h Warning]
    K --> L[12h Warning]
    L --> M[6h Warning]
    M --> N[Activation]
    N --> O[Post-fork Monitoring]
```

**Fuego Coordination Checklist:**
- [ ] CoinMarketCap/CoinGecko notification (2 weeks before)
- [ ] Exchange deployment schedule (1 week before)
- [ ] Pool operator coordination (72 hours)
- [ ] FuegoTor bridge status (V10+)
- [ ] DMWDA validator nodes (V10+)
- [ ] Deposit provider notification (V8+)
- [ ] Burn proof validation pipeline (V9+)

#### Phase 4: Activation and Monitoring (Ongoing)

```cpp
// V2-V10: UpgradeDetector implementation
class FuegoUpgradeManager {
public:
    bool init() {
        // Initialize upgrade detectors for all versions
        m_detectorV2 = BasicUpgradeDetector(currency, blockchain, BLOCK_MAJOR_VERSION_2, logger);
        m_detectorV3 = BasicUpgradeDetector(currency, blockchain, BLOCK_MAJOR_VERSION_3, logger);
        // ... continue through V10
        
        // V10: DMWDA validator needs special handling
        if (currency.upgradeHeight(BLOCK_MAJOR_VERSION_10) != UNDEF_HEIGHT) {
            m_dmwdaValidator.init(currency.upgradeHeight(BLOCK_MAJOR_VERSION_10));
        }
        
        return true;
    }
    
    void blockPushed(const Block& block) {
        // Each detector processes the new block
        m_detectorV2.blockPushed();
        m_detectorV3.blockPushed();
        m_detectorV4.blockPushed();
        // ... through V10
        
        // V10: Check for DMWDA anomalies
        if (block.majorVersion >= BLOCK_MAJOR_VERSION_10) {
            checkDMWDAAnomaly(block);
            updateFuegoTorBridges(block);
        }
        
        // Log upgrade status
        logUpgradeStatus(block.height);
    }
    
    void logUpgradeStatus(uint32_t height) {
        std::cout << "=== Fuego Upgrade Status ===\n";
        std::cout << "Current Block: " << height << "\n";
        
        // V2-V10: Show next upgrade
        for (uint8_t v = BLOCK_MAJOR_VERSION_2; v <= BLOCK_MAJOR_VERSION_10; ++v) {
            uint32_t upgradeHeight = currency.upgradeHeight(v);
            if (upgradeHeight != UNDEF_HEIGHT && upgradeHeight > height) {
                std::cout << "V" << static_cast<int>(v) << " activation: " 
                          << upgradeHeight << " (" << (upgradeHeight - height) << " blocks)\n";
            }
        }
        
        // V10: Special DMWDA status
        if (height >= currency.upgradeHeight(BLOCK_MAJOR_VERSION_10)) {
            auto stats = blockchain.getDMWDAStats(height);
            std::cout << "DMWDA Confidence: " << stats.confidence << "\n";
            std::cout << "Next Difficulty: " << stats.nextDifficulty << "\n";
            std::cout << "FuegoTor: " << (m_torManager ? "Enabled" : "Disabled") << "\n";
        }
    }
    
private:
    // V2-V10: Upgrade detectors
    BasicUpgradeDetector<Blockchain> m_detectorV2;
    BasicUpgradeDetector<Blockchain> m_detectorV3;
    // ... through V10
    
    // V10: DMWDA validator
    DMWDAValidator m_dmwdaValidator;
    FuegoTorManager* m_torManager;
};
```

### Soft Fork Process

#### Specification Phase

```mermaid
flowchart TD
    A[Feature Proposal] --> B{Backward Compatible?}
    B -->|Yes| C[Specification]
    B -->|No| D[Hard Fork Required]
    
    C --> E[Protocol Extension]
    E --> F[Version Negotiation]
    F --> G[Grace Period Design]
    
    G --> H[Gradual Rollout]
    H --> I[Monitoring Plan]
    I --> J[Rollback Procedure]
    
    J --> K[Automatic Fallback]
    K --> L[Network Stability]
```

**Fuego Soft Fork Example - P2P Lite Blocks:**
```cpp
// V7: Soft fork for lite block propagation
class LiteBlockProtocol {
public:
    // New message handler (backward compatible)
    void handleLiteBlockMessage(const BinaryArray& data) {
        try {
            LiteBlock block = serializeLiteBlock(data);
            if (!validateLiteBlock(block)) {
                // Invalid - request full block
                requestFullBlock(block.header.previousBlockHash);
                return;
            }
            
            // V7+: Process lite block
            if (processLiteBlock(block)) {
                // Success - propagate to other V7+ nodes
                p2p.broadcastLiteBlock(block);
            } else {
                // Missing transactions - request them
                requestMissingTxs(block.missingTxs);
            }
        } catch (...) {
            // V2-V6: Unknown message - ignore gracefully
            logger(DEBUGGING) << "Received unknown P2P message type";
        }
    }
    
    // V2-V6 behavior
    void handleIgnoreUnknownMessage(const BinaryArray& data) {
        // Old nodes just ignore - no crash
        logger(TRACE) << "Ignoring unknown message";
    }
};
```

#### Deployment Strategy

```mermaid
flowchart TD
    A[Release V7 Soft Fork] --> B[Users Update]
    B --> C[New Nodes Online]
    C --> D{Old Nodes Online?}
    D -->|Yes| E[Network Works]
    D -->|No| F[Wait for Adoption]
    E --> G[Monitor Edge Cases]
    G --> H{Issues Found?}
    H -->|Yes| I[Deploy Fix]
    H -->|No| J[Soft Fork Complete]
    I --> K[Deploy to New Nodes]
    K --> L[Gradual Update]
    L --> J
```

**Fuego Monitoring for Soft Forks:**
```cpp
class SoftForkMonitor {
public:
    void trackLiteBlockAdoption() {
        // V7: Track what percentage of network supports lite blocks
        auto peers = m_p2p.getConnectedPeers();
        size_t v7Count = 0;
        
        for (const auto& peer : peers) {
            if (peer.version >= 3 && peer.supportsLiteBlocks) {
                v7Count++;
            }
        }
        
        double adoption = (double)v7Count / peers.size();
        m_metrics["p2p.lite_blocks_adoption"] = adoption;
        m_metrics["p2p.lite_blocks_latency"] = calculateLiteBlockLatency();
        
        if (adoption > 0.75) {
            // 75% adoption - safe to enable features
            m_p2p.enableLiteBlockBroadcast();
        }
    }
    
    bool isSoftForkActive() {
        // Check if soft fork is safely deployed
        auto peers = m_p2p.getConnectedPeers();
        size_t incompatible = 0;
        
        for (const auto& peer : peers) {
            if (!m_protocol.isCompatible(peer.version)) {
                incompatible++;
            }
        }
        
        return incompatible == 0;  // 100% compatibility
    }
};
```

### No Fork Process

#### Standard Development Flow

```mermaid
flowchart TD
    A[Feature Request] --> B[Design Review]
    B --> C[Implementation]
    C --> D[Unit Testing]
    D --> E[Integration Testing]
    E --> F[Code Review]
    F --> G[Documentation]
    G --> H[Build Verification]
    H --> I[Release Candidate]
    I --> J[User Testing]
    J --> K{Approved?}
    K -->|Yes| L[Release Production]
    K -->|No| M[Iterate]
    L --> N[Monitor]
    M --> C
```

**Fuego No-Fork Example - Configuration Manager:**
```cpp
// V8-V10: Configuration updates (no impact)
class ConfigManager {
public:
    // Add new config without breaking existing
    struct DepositConfig {
        uint64_t minAmount = parameters::DEPOSIT_MIN_AMOUNT;
        uint32_t minTerm = parameters::DEPOSIT_MIN_TERM;
        bool autoRenew = false;  // V10 feature
    };
    
    struct BurnConfig {
        uint64_t smallAmount = parameters::BURN_DEPOSIT_MIN_AMOUNT;
        uint64_t largeAmount = parameters::BURN_DEPOSIT_LARGE_AMOUNT;
        std::string notificationEmail;  // V9 feature
    };
    
    struct DMWDAConfig {
        bool enabled = true;
        bool monitoring = true;
        double confidenceThreshold = 0.1;  // V10 feature
    };
    
    void loadConfig(const json& config) {
        // V8: Load deposit config
        if (config.contains("deposit")) {
            m_depositConfig = parseDepositConfig(config["deposit"]);
        }
        
        // V9: Load burn config
        if (config.contains("burn")) {
            m_burnConfig = parseBurnConfig(config["burn"]);
        }
        
        // V10: Load DMWDA config
        if (config.contains("dmwda")) {
            m_dmwdaConfig = parseDMWDAConfig(config["dmwda"]);
        }
        
        // Legacy config remains unchanged
        loadLegacyConfig(config);
    }
    
    bool validateConfig() {
        // Validate new configs without breaking old behavior
        bool valid = true;
        
        // V8: Deposit validation
        if (!validateDepositConfig(m_depositConfig)) {
            logger(WARNING, BRIGHT_YELLOW) << "Invalid deposit config";
            valid = false;
        }
        
        // V9: Burn validation
        if (!validateBurnConfig(m_burnConfig)) {
            logger(WARNING, BRIGHT_YELLOW) << "Invalid burn config";
            valid = false;
        }
        
        // V10: DMWDA validation
        if (!validateDMWDAConfig(m_dmwdaConfig)) {
            logger(WARNING, BRIGHT_YELLOW) << "Invalid DMWDA config";
            valid = false;
        }
        
        return valid && validateLegacyConfig();
    }
};
```

---

## Change Management Workflow

### Impact Assessment Matrix

| Change Type | Consensus Impact | Network Impact | User Impact | Fork Required | Example in Fuego |
|-------------|------------------|----------------|-------------|---------------|------------------|
| **BlockHeader fields added** | 🔴 High | 🟡 Medium | 🔴 High | ✅ Hard Fork | V7 depositRoot |
| **Consensus rules modified** | 🔴 Critical | 🔴 High | 🔴 Critical | ✅ Hard Fork | V10 DMWDA |
| **Emission formula changed** | 🔴 Critical | 🟢 Low | 🟡 Medium | ✅ Hard Fork | V9 Fuego emission |
| **Deposit system added** | 🔴 Critical | 🟢 Low | 🟡 Medium | ✅ Hard Fork | V8 deposits |
| **Burn proof system** | 🔴 High | 🟢 Low | 🟡 Medium | ✅ Hard Fork | V9 burn proofs |
| **P2P protocol extension** | 🟢 None | 🟡 Medium | 🟢 Low | 🟡 Soft Fork | V7 lite blocks |
| **Mixins changed (dynamic)** | 🟡 Medium | 🟡 Medium | 🟡 Medium | 🟡 Soft Fork | V10 dynamaxin |
| **New RPC methods** | 🟢 None | 🟢 Low | 🟢 Low | 🟢 No Fork | V8 deposit APIs |
| **UI improvements** | 🟢 None | 🟢 None | 🟡 Medium | 🟢 No Fork | V10 yield UI |
| **Configuration additions** | 🟢 None | 🟢 None | 🟢 Low | 🟢 No Fork | V10 Tor config |
| **Logging enhancements** | 🟢 None | 🟢 None | 🟡 Low | 🟢 No Fork | V9 burn logging |
| **Documentation** | 🟢 None | 🟢 None | 🟢 None | 🟢 No Fork | V10 code comments |

### Coordination Requirements

####硬分叉协调 (V2-V10)

```mermaid
graph TD
    A[硬分叉事件] --> B[协调阶段]
    B --> C{协调级别}
    
    C -->|极高| D[全网络同步]
    C -->|高| E[关键服务协调]
    C -->|中| F[可选服务提醒]
    C -->|低| G[信息性通知]
    
    D --> H[交易所]
    D --> I[矿池]
    D --> J[全节点运营商]
    D --> K[钱包提供商]
    
    E --> L[区块浏览器]
    E --> M[数据分析服务]
    E --> N[价格追踪器]
    
    F --> O[社区论坛]
    F --> P[社交媒体]
    
    G --> Q[邮件列表]
    G --> R[博客文章]
    
    H --> S[2周前通知]
    I --> S
    J --> T[1周前通知]
    K --> T
    L --> U[3天前通知]
    M --> U
    N --> V[激活当天]
    O --> W[激活后更新]
```

**Fuego V10 硬分叉协调清单:**
```
🔴 硬分叉：DMWDA + FuegoTor + 动态Mixins
激活高度：999999（约在 2024 年 3 月）
当前高度：（需要从网络获取）

协调时间表：
- T-21天：向所有主要交易所发送技术规范
- T-14天：矿池集成测试完成
- T-7天：主网升级高度正式公布
- T-3天：FuegoTor 桥验证完成
- T-1天：DMWDA 验证器节点同步
- T-0：激活高度达到，监控 24 小时
- T+1天：初始问题解决，发布公告
- T+7天：完整系统健康检查
```

####软分叉协调 (典型软分叉)

```mermaid
graph TD
    A[软分叉部署] --> B[网络能力评估]
    B --> C{网络就绪?}
    
    C -->|是| D[部署计划]
    C -->|否| E[增强准备]
    
    D --> F[版本发布]
    F --> G[用户教育]
    G --> H[逐步激活]
    
    H --> I[监控阶段]
    I --> J{表现正常?}
    
    J -->|是| K[功能启用]
    J -->|否| L[回滚或修复]
    
    E --> M[测试网络测试]
    M --> N[社区反馈]
    N --> O[计划调整]
    O --> D
    
    K --> P[持续监控]
    L --> Q[发布修复]
    Q --> H
```

**Fuego 软分叉示例 - Lite Blocks (V7):**
```
🟡 软分叉：P2P Lite Blocks
部署策略：向后兼容，逐步启用

部署时间表：
- 第0周：发布V7测试网，启用lite blocks
- 第1周：监控20%节点升级情况
- 第2周：启用自动fallback机制
- 第3周：扩展至50%网络
- 第4周：完整部署，全面监控
- 始终保持：旧节点忽略新消息，无崩溃风险

监控指标：
- 网络中V7+节点比例 (0-100%)
- Lite block传播延迟
- Full block回退频率
- 旧节点兼容性测试
```

####无分叉协调 (标准发布)

```mermaid
graph TD
    A[功能开发] --> B[CI/CD 流程]
    B --> C[自动化测试]
    C --> D[代码审查]
    D --> E[预发布]
    E --> F[用户测试]
    
    F --> G{问题?}
    G -->|有| H[修复并返回]
    G -->|无| I[正式发布]
    
    I --> J[自动通知]
    J --> K[文档更新]
    K --> L[社区公告]
    
    L --> M[监控使用情况]
    M --> N{反馈?}
    N -->|是| O[迭代改进]
    N -->|否| P[完成]
    O --> I
```

**Fuego 无分叉发布示例 - 监控增强:**
```
🟢 无分叉：V10 监控增强
发布类型：标准软件更新，立即可用

发布流程：
- 代码合并到主分支
- CI/CD 自动构建和测试
- GitHub 发布与二进制文件
- Docker 镜像自动发布
- 更新文档和 changelog
- 24小时社区公告

部署策略：
- 用户手动更新或自动更新（配置）
- 旧版本继续工作
- 新版本提供增强功能
- 回滚通过简单降级

可用增强功能：
- 实时 DMWDA 统计
- Deposit 收益预测
- Burn 证明验证
- Tor 性能监控
- 数据库性能指标
```

---

## Examples by Category

### Hard Fork Examples

#### REAL V2-V10 Evolution

```cpp
// ========== V2: Hardhome (147958) ==========
// Initial block structure
struct BlockHeader {
    uint8_t majorVersion;      // 1 → 2
    uint8_t minorVersion;
    uint32_t nonce;
    uint64_t timestamp;
    Crypto::Hash previousBlockHash;
};

// Legacy difficulty (CryptoNote standard)
difficulty_type nextDifficultyV2(std::vector<uint64_t> timestamps, 
                                 std::vector<difficulty_type> cumulativeDifficulties) {
    const uint64_t T = DIFFICULTY_TARGET_DRGL;  // 81 seconds
    const size_t N = DIFFICULTY_WINDOW_V2;      // 18 blocks
    // Zawy v1.0 algorithm
    // ...
}

// ========== V3: Longclaw (154321) ==========
// Variant1 algorithm support
bool checkProofOfWorkV3(const Block& block, difficulty_type current_diffic) {
    // Similar to V2, but with minor optimizations
    return checkProofOfWorkV2(block, current_diffic);
}

// ========== V4: Dracarys (300000) ==========
// V4: Increased block reward zone
const size_t CRYPTONOTE_BLOCK_GRANTED_FULL_REWARD_ZONE_V2 = 430080;

// ========== V5: Ironborn - CN7 (324819) ==========
// Major structure change
struct BlockHeader {
    uint8_t majorVersion;      // 4 → 5
    uint8_t minorVersion;
    uint32_t nonce;
    uint64_t timestamp;
    Crypto::Hash previousBlockHash;
    // New: Variant field for CN7
    uint8_t variant;
};

// New difficulty (LWMA3)
difficulty_type nextDifficultyV4(std::vector<uint64_t> timestamps, 
                                 std::vector<difficulty_type> cumulativeDifficulties) {
    const uint64_t T = DIFFICULTY_TARGET_DRGL;
    const size_t N = DIFFICULTY_WINDOW_V3;      // 60 blocks
    // LWMA3 algorithm
    // ...
}

// ========== V6: Ice&fire - CN8 (345678) ==========
// Variant2 algorithm
bool checkProofOfWorkV6(const Block& block, difficulty_type current_diffic) {
    Crypto::Hash block_hash;
    if (block.majorVersion >= BLOCK_MAJOR_VERSION_6) {
        block_hash = get_block_hash_variant(block, VARIANT_CN_FANGO);
    }
    return check_hash(block_hash, current_diffic);
}

// ========== V7: Apotheosis - Fango (657000) ==========
// Increased difficulty target (81s → 480s)
const uint64_t DIFFICULTY_TARGET = 480;  // 8 minutes

// Deposit commitment structure
struct TransactionPrefix {
    uint8_t version;
    uint64_t unlockTime;
    TransactionInputs inputs;
    std::vector<TransactionOutput> outputs;
    std::vector<uint8_t> extra;
    std::optional<Crypto::Hash> depositCommitment;  // New V7+
};

// ========== V8: Dragonborne - Deposits (800000) ==========
// Full deposit system
const uint64_t DEPOSIT_MIN_AMOUNT = 8000000000;    // 800 XFG
const uint64_t YIELD_DEPOSIT_MIN_AMOUNT = 80000000; // 8 XFG
const uint32_t DEPOSIT_MIN_TERM = 16440;           // 3 months

// Deposit indexing
class BankingIndex {
public:
    bool addDeposit(const DepositEntry& entry) {
        // Add to height index
        m_heightIndex.insert(entry.maturityHeight, entry);
        // Add to amount index
        m_amountIndex.insert(entry.amount, entry);
        // Add to provider index
        m_providerIndex.insert(entry.provider, entry);
        return true;
    }
};

// ========== V9: Godflame - Burn + UPX2 (826420) ==========
// Burn proof system
struct BurnProof {
    Crypto::Hash transactionHash;
    Crypto::Hash burnedOutput;
    uint64_t burnedAmount;
    std::vector<Crypto::Signature> proof;  // Merkle proof
};

// Emission changes (V9: Fuego emission)
const unsigned EMISSION_SPEED_FACTOR_FUEGO = 20;  // 20
const uint64_t MONEY_SUPPLY = UINT64_C(80000088000008);  // 8M8

bool Currency::getBlockRewardV9(...) {
    uint64_t baseReward = (MONEY_SUPPLY - alreadyGeneratedCoins) >> 20;
    uint64_t fixed95 = baseReward * 95 / 100;    // 95% to miners
    uint64_t treasury = baseReward * 5 / 100;     // 5% to treasury
    reward = fixed95 + fee;
    emissionChange = -1LL * (fixed95 + treasury); // Negative is generation
}

// ========== V10: Dynamigo - DMWDA + Dynamaxin (999999) ==========
// DMWDA difficulty algorithm
class DMWDA {
public:
    difficulty_type calculateNextDifficulty(const Block& prevBlock, 
                                          const Block& currentBlock) {
        // Multi-window analysis
        uint64_t shortWindow = calculateShortWindow(prevBlock, currentBlock);
        uint64_t mediumWindow = calculateMediumWindow(prevBlock, currentBlock);
        uint64_t longWindow = calculateLongWindow(prevBlock, currentBlock);
        
        // Confidence-based adjustment
        double confidence = calculateConfidence(shortWindow, mediumWindow, longWindow);
        
        // Emergency detection
        if (isEmergencySituation(currentBlock)) {
            return applyEmergencyAdjustment(currentBlock, confidence);
        }
        
        // Normal adjustment
        return calculateWeightedDifficulty(shortWindow, mediumWindow, longWindow, confidence);
    }
};

// Dynamic mixin (Dynamaxin) - V10+
bool validateTransactionV10(const Transaction& tx, uint64_t currentDiffic, uint32_t height) {
    // Calculate required mixin based on difficulty
    uint64_t requiredMixin = calculateDynamicMixin(currentDiffic);
    
    // Validate each input
    for (const auto& input : tx.inputs) {
        if (input.type() == typeid(KeyInput)) {
            const auto& keyInput = boost::get<KeyInput>(input);
            if (keyInput.outputIndexes.size() < requiredMixin) {
                logger(DEBUGGING) << "Input mixin " << keyInput.outputIndexes.size() 
                                 << " < required " << requiredMixin;
                return false;
            }
        }
    }
    return true;
}

// FuegoTor integration (V10)
struct FuegoTorConfig {
    bool torEnabled;
    std::vector<std::string> bridges;
    uint16_t socksPort;
    bool hybridMode;  // Pure Tor or Hybrid (optional clearnet)
};

class FuegoTorManager {
public:
    bool sendPrivateTransaction(const Transaction& tx, uint64_t fee) {
        // Check if Tor is available
        if (!isConnected()) {
            logger(WARNING) << "FuegoTor not connected, using P2P";
            return m_p2p->broadcast(tx);
        }
        
        // Generate private transaction
        PrivateTransaction ptx = createPrivateTransaction(tx, fee);
        
        // Route through Tor
        return m_torConnection->send(ptx);
    }
};
```

### Soft Fork Examples

#### V7: Lite Blocks Protocol

```cpp
// Backward compatible protocol addition
const uint8_t P2P_CURRENT_VERSION = 3;
const uint8_t P2P_LITE_BLOCKS_PROPOGATION_VERSION = 3;

enum P2PMessageTypes {
    // Legacy (V2-V6) - 1-1999 range
    COMMAND_HANDSHAKE = 1,
    COMMAND_REQUEST_BLOCK = 2,
    // ... existing messages
    
    // New (V7+) - 2000+ range
    COMMAND_LITE_BLOCK = 2001,
    COMMAND_MISSING_TXS = 2002,
    COMMAND_PEERS_V2 = 2003  // Enhanced peer list
};

struct LiteBlock {
    BlockHeader header;
    std::vector<Crypto::Hash> transactionHashes;
    std::vector<Transaction> missingTxs;  // Only transactions not in mempool
};

class P2PProtocol {
public:
    void handleLiteBlock(const LiteBlock& block) {
        try {
            // Validate
            if (!validateLiteBlock(block)) {
                fallbackToFullBlock(block.header.previousBlockHash);
                return;
            }
            
            // Process
            bool result = processLiteBlock(block);
            
            // Broadcast to newer nodes
            if (result) {
                for (auto& peer : m_peers) {
                    if (peer.version >= 3) {
                        peer.sendLiteBlock(block);
                    } else {
                        peer.sendFullBlockRequest(block.header.previousBlockHash);
                    }
                }
            }
        } catch (const std::exception& e) {
            // V2-V6: Unknown message - graceful ignore
            logger(TRACE) << "Lite block processing failed: " << e.what();
            fallbackToFullBlock(block.header.previousBlockHash);
        }
    }
    
    void handleLegacyBlock(const BinaryArray& data) {
        // Old behavior unchanged
        processFullBlock(serializeBlock(data));
    }
};
```

#### V8: Deposit RPC (Compatible)

```cpp
// New RPC methods - old nodes ignore
class DepositRPCCompatible {
public:
    // V8: New deposit methods
    json rpc_createDeposit(const json& params) {
        // V8+ capable node
        if (!m_currency.supportsDeposits()) {
            return error("Deposits not supported on this version");
        }
        
        Transaction tx = createDepositTransaction(params);
        return json{{"tx_hash", tx.hash}};
    }
    
    // V8: Deposit query
    json rpc_getDeposits(const json& params) {
        // Old P2P protocol continues working
        // Only nodes with V8+ respond to this method
        if (!m_currency.supportsDeposits()) {
            return json::array();  // Empty array, not an error
        }
        
        return m_blockchain.getDeposits(params["address"]);
    }
    
    // V10: Enhanced deposit with yield
    json rpc_getDepositYield(const json& params) {
        // Only V10+ nodes provide yield calculations
        if (m_majorVersion < BLOCK_MAJOR_VERSION_10) {
            return rpc_getDeposits(params);  // Fallback to basic info
        }
        
        auto deposits = m_blockchain.getDeposits(params["address"]);
        json result = json::array();
        
        for (const auto& deposit : deposits) {
            double yield = m_currency.calculateYield(deposit.term, deposit.amount, deposit.type);
            result.push_back({
                {"id", deposit.id},
                {"amount", formatXFG(deposit.amount)},
                {"yield", formatXFG(yield)},
                {"apy", calculateAPY(yield, deposit.term)}
            });
        }
        return result;
    }
};
```

#### V8: Performance Optimizations

```cpp
// Database optimization - V8+
class OptimizedStorage {
public:
    // Caching layer - V8
    bool getBlockCached(uint32_t height, Block& block) {
        std::lock_guard lock(m_cacheMutex);
        auto it = m_blockCache.find(height);
        if (it != m_blockCache.end()) {
            block = it->second;
            m_cacheMetrics.hits++;
            return true;
        }
        m_cacheMetrics.misses++;
        return false;
    }
    
    // Batch queries - V8
    std::vector<Transaction> getTransactionsV8(const std::vector<Crypto::Hash>& hashes) {
        std::vector<Transaction> result;
        result.reserve(hashes.size());
        
        // V8: Use prepared statements
        auto stmt = m_db.prepare("SELECT * FROM transactions WHERE hash = ?");
        for (const auto& hash : hashes) {
            stmt.bind(1, hash);
            if (stmt.execute()) {
                result.push_back(parseTransaction(stmt));
            }
        }
        return result;
    }
    
    // V10: Deposit cache
    bool getHotDeposit(const DepositKey& key, DepositEntry& entry) {
        // V10: Hot deposit cache for frequent queries
        auto it = m_hotDepositCache.find(key);
        if (it != m_hotDepositCache.end()) {
            entry = it->second;
            return true;
        }
        return false;
    }
};
```

### No Fork Examples

#### V9: Enhanced Logging

```cpp
// Improved error messages (no impact)
class EnhancedLogger {
public:
    void logBlockValidation(const Block& block, bool success) {
        if (success) {
            logger(TRACE, BRIGHT_GREEN) << "VALIDATED block " << block.height 
                << " hash=" << get_block_hash(block)
                << " size=" << get_block_size(block)
                << " version=" << static_cast<int>(block.majorVersion);
        } else {
            logger(DEBUGGING, BRIGHT_RED) << "INVALID block " << block.height
                << " error=" << getValidationError(block)
                << " attempt=" << getValidationAttemptCount();
        }
    }
    
    void logTransaction(const Transaction& tx, const std::string& action) {
        // V9: Better formatting
        logger(TRACE, BRIGHT_WHITE) << "TX " << action
            << ": hash=" << get_transaction_hash(tx)
            << ": fee=" << formatXFG(tx.fee)
            << ": size=" << get_transaction_size(tx)
            << ": inputs=" << tx.inputs.size()
            << ": outputs=" << tx.outputs.size();
        
        // V10: Deposit transaction details
        if (containsDeposit(tx)) {
            logger(TRACE, BRIGHT_CYAN) << "  Deposit detected: " 
                << getDepositAmount(tx) << " XFG for " 
                << getDepositTerm(tx) << " blocks";
        }
    }
    
    void logDMWDA(const DMWDAStats& stats, const Block& block) {
        // V10: DMWDA monitoring (no fork)
        if (block.majorVersion >= BLOCK_MAJOR_VERSION_10) {
            logger(TRACE, BRIGHT_BLUE) << "DMWDA stats: "
                << "height=" << block.height
                << " confidence=" << stats.confidence
                << " next_diff=" << stats.nextDifficulty
                << " adjustment=" << stats.lastAdjustment;
        }
    }
};
```

#### V10: UI Framework Updates

```javascript
// V10: Modern web UI (no fork - pure frontend)
class FuegoWebUI {
    constructor() {
        this.depositManager = new DepositUI();
        this.burnManager = new BurnUI();
        this.dmwdaDashboard = new DMWDADashboard();
    }
    
    // Deposit creation UI
    async createDeposit(amount, term, type) {
        // V10: Enhanced UI with yield preview
        const estimatedYield = await rpc.estimateYield(amount, term, type);
        
        ui.showConfirmation({
            title: "Confirm Deposit",
            amount: amount + " XFG",
            term: term + " blocks",
            type: type,
            estimatedYield: estimatedYield.yield,
            apy: estimatedYield.apy,
            fee: estimatedYield.fee
        });
        
        return await rpc.createDeposit({amount, term, type});
    }
    
    // DMWDA visualization
    renderDMWDAStats(stats) {
        // Interactive charts
        this.difficultyChart.setData(stats.history);
        this.confidenceGauge.setValue(stats.confidence);
        this.hashrateChart.setData(stats.hashrateHistory);
        
        // V10: Predictions
        this.difficultyForecast.setData(stats.nextDifficultyPrediction);
    }
    
    // Burn proof interface
    initBurnProof() {
        document.getElementById('burn-form').addEventListener('submit', async (e) => {
            e.preventDefault();
            const txId = document.getElementById('tx-hash').value;
            
            // V10: Enhanced proof submission
            const proof = await generateBurnProof(txId);
            ui.showProof(proof);
            
            const result = await rpc.submitBurnProof({tx_id: txId, proof});
            ui.showResult(result);
        });
    }
}
```

#### V8: Configuration Manager

```cpp
// V8-V10: Config (no fork)
class ConfigManager {
public:
    // V8: Deposit configuration
    struct DepositConfig {
        uint64_t minAmount = parameters::DEPOSIT_MIN_AMOUNT;
        uint32_t minTerm = parameters::DEPOSIT_MIN_TERM;
        bool autoRenew = false;  // V10 feature
        
        void load(const json& j) {
            minAmount = parseAmount(j.value("min_amount", "800.0"));
            minTerm = j.value("min_term_blocks", 16440);
            autoRenew = j.value("auto_renew", false);
        }
    };
    
    // V9: Burn configuration
    struct BurnConfig {
        uint64_t smallAmount = parameters::BURN_DEPOSIT_MIN_AMOUNT;
        uint64_t largeAmount = parameters::BURN_DEPOSIT_LARGE_AMOUNT;
        std::string notificationEmail;
        
        void load(const json& j) {
            smallAmount = parseAmount(j.value("small_amount", "0.8"));
            largeAmount = parseAmount(j.value("large_amount", "800.0"));
            notificationEmail = j.value("notification_email", "");
        }
    };
    
    // V10: DMWDA and Tor configuration
    struct DMWDAConfig {
        bool enabled = true;
        bool monitoring = true;
        double confidenceThreshold = 0.1;
        std::vector<std::string> validator_nodes;
        
        void load(const json& j) {
            enabled = j.value("enabled", true);
            monitoring = j.value("monitoring", true);
            confidenceThreshold = j.value("confidence_threshold", 0.1);
            validator_nodes = j.value("validator_nodes", std::vector<std::string>{});
        }
    };
    
    struct TorConfig {
        bool enabled = false;
        uint16_t socksPort = 9050;
        bool hybridMode = false;  // Clearnet + Tor
        std::vector<std::string> bridges;
        
        void load(const json& j) {
            enabled = j.value("enabled", false);
            socksPort = j.value("socks_port", 9050);
            hybridMode = j.value("hybrid_mode", false);
            bridges = j.value("bridges", std::vector<std::string>{});
        }
    };
    
    // Configuration validation
    bool validateConfig() {
        bool valid = true;
        
        // V8: Validate deposit
        if (m_deposit.minAmount < 80000000) {  // 8 XFG minimum
            logger(WARNING) << "Deposit min amount too low";
            valid = false;
        }
        
        // V9: Validate burn
        if (m_burn.smallAmount < 80000) {  // 0.008 XFG minimum
            logger(WARNING) << "Burn min amount too low";
            valid = false;
        }
        
        // V10: Validate DMWDA
        if (m_dmwda.confidenceThreshold > 0.5) {
            logger(WARNING) << "DMWDA confidence threshold too high";
            valid = false;
        }
        
        // V10: Validate Tor
        if (m_tor.enabled && m_tor.bridges.empty()) {
            logger(WARNING) << "Tor enabled but no bridges configured";
            valid = false;
        }
        
        return valid;
    }
    
private:
    DepositConfig m_deposit;
    BurnConfig m_burn;
    DMWDAConfig m_dmwda;
    TorConfig m_tor;
};
```

---

## Maintenance and Updates

### Version Control Strategy

```mermaid
graph TD
    A[Feature Branches] --> B{Fork Level?}
    
    B -->|Hard Fork| C[Release/X.Y.Z]
    B -->|Soft Fork| D[Release/X.Y+1.0]
    B -->|No Fork| E[Release/X.Y.Z+1]
    
    C --> F[Major Version Update]
    D --> G[Minor Version Update]
    E --> H[Patch Version Update]
    
    F --> I[Breaking Changes]
    G --> J[New Features C]
    H --> K[Bug Fixes]
    
    I --> L[Upgrade Height Tag]
    J --> M[Feature Flags]
    K --> N[Backport Support]
    
    L --> O[Git Tag: v10.0.0]
    M --> P[Git Tag: v7.1.0]
    N --> Q[Git Tag: v8.0.1]
```

**Fuego Git Structure:**
```
fuego/
├── src/CryptoNoteCore/
│   ├── Currency.cpp        # Consensus (hard fork versioned)
│   ├── UpgradeDetector.h   # Fork coordination (versioned)
│   ├── DMWDA.cpp          # V10+ (feature flagged)
│   ├── DepositSystem.cpp   # V8+ (feature flagged)
│   └── BurnProof.cpp      # V9+ (feature flagged)
├── src/FuegoTor/          # V10+ (optional build)
│   ├── TorManager.cpp     # Soft fork components
│   └── TorConnection.cpp
├── tests/
│   ├── core/              # Consensus tests for V2-V10
│   ├── deposits/          # V8+ tests
│   ├── burn/             # V9+ tests
│   └── dmwda/            # V10+ tests
├── docker/
│   ├── Dockerfile        # Standard builds
│   └── Dockerfile.tor    # Tor enabled
└── docs/
    ├── FORK_CLASSIFICATION.md
    └── UPGRADE_GUIDE.md
```

### Branching Strategy

```bash
# Fuego Git workflow

# Main branch - current stable (V10)
git checkout main
# All V10+ features and No-fork changes

# Hard fork specific 
git checkout release/v11 hf
# New hard fork preparation (evaluation)

# Soft fork feature branches
git checkout feature/lite-blocks-enhanced
# V7+ compatible enhancements

# No-fork feature branches
git checkout feature/deposit-ui
# Standard development

# Release tags
git tag v10.0.0  # V10 activation
git tag v10.0.1  # Bug fix
git tag v10.1.0  # Soft fork addition
git tag v11.0.0  # Next hard fork

# CI/CD gates
# - All tests pass for current version
# - Fork classification validation
# - Consensus test vector verification
# - Security audit before hard fork merge
```

### Fork Classification Updates

```cpp
// Automated fork validation during CI/CD
class ForkClassifier {
public:
    enum ChangeType {
        HARD_FORK,
        SOFT_FORK,
        NO_FORK,
        UNKNOWN
    };
    
    ChangeType classifyChange(const std::string& filePath, 
                              const std::string& diff) {
        // Rule-based classification
        if (isConsensusFile(filePath) && isConsensusChange(diff)) {
            return HARD_FORK;
        }
        
        if (isNetworkProtocolFile(filePath) && isBackwardCompatible(diff)) {
            return SOFT_FORK;
        }
        
        if (isConfigOrUiFile(filePath) || isLoggingFile(diff)) {
            return NO_FORK;
        }
        
        return UNKNOWN;  // Manual review required
    }
    
    bool isConsensusFile(const std::string& path) {
        static const std::set<std::string> consensusFiles = {
            "src/CryptoNoteCore/Currency.cpp",
            "src/CryptoNoteCore/Currency.h",
            "src/CryptoNoteCore/Blockchain.cpp",
            "src/CryptoNoteCore/TransactionPool.cpp",
            "src/CryptoNoteCore/CryptoNoteConfig.h"
        };
        return consensusFiles.count(path) > 0;
    }
    
    bool isConsensusChange(const std::string& diff) {
        // Check for threshold changes
        if (diff.find("DIFFICULTY_TARGET") != std::string::npos &&
            diff.find("EMISSION_SPEED") != std::string::npos) {
            return true;
        }
        
        // Check for block structure changes
        if (diff.find("struct BlockHeader") != std::string::npos ||
            diff.find("struct Transaction") != std::string::npos) {
            return true;
        }
        
        // Check for validation function changes
        if (diff.find("validateTransaction") != std::string::npos ||
            diff.find("checkProofOfWork") != std::string::npos) {
            return true;
        }
        
        return false;
    }
    
    bool isBackwardCompatible(const std::string& diff) {
        // Check for new RPC methods
        if (diff.find("rpc_") != std::string::npos) {
            return true;
        }
        
        // Check for new optional fields
        if (diff.find("std::optional") != std::string::npos ||
            diff.find("std::optional") != std::string::npos) {
            return true;
        }
        
        // Check for new message types
        if (diff.find("COMMAND_") != std::string::npos && 
            diff.find("2000") != std::string::npos) {
            return true;
        }
        
        return false;
    }
};
```

### Review Process

#### Hard Fork Review Checklist

```mermaid
flowchart TD
    A[Hard Fork Proposal] --> B[Technical Review]
    B --> C[Security Audit]
    C --> D[Economic Analysis]
    D --> E[Community Review]
    
    B --> F[Code Quality]
    B --> G[Consensus Logic]
    B --> H[Performance]
    
    C --> I[Cryptographic Review]
    C --> J[Network Attack Vectors]
    C --> K[Data Integrity]
    
    D --> L[Monetary Impact]
    D --> M[Miner Incentives]
    D --> N[User Adoption]
    
    E --> O[RFC Discussion]
    E --> P[Voting Process]
    E --> Q[Stakeholder Approval]
    
    F --> R[Clean Code]
    G --> S[Correct Consensus]
    H --> T[No Bottlenecks]
    I --> U[Sound Crypto]
    J --> V[Attack Resistant]
    K --> W[Upgrade Safety]
    L --> X[Sustainable]
    M --> Y[Fair Distribution]
    N --> Z[Reasonable Migration]
    
    R & S & T & U & V & W & X & Y & Z --> AA[Final Approval]
    AA --> AB[Reference Implementation]
    AB --> AC[Network Coordination]
```

**Fuego V10 Specific Checklist:**
- [ ] **DMWDA Algorithm**: Five independent implementations, 1000 test cases
- [ ] **Dynamic Mixin**: Economic analysis, spam resistance validation
- [ ] **FuegoTor**: Tor team review, bridge security, hybrid mode verification
- [ ] **References**: Reference implementation in testnet
- [ ] **Upgrade Heights**: Voting mechanisms, height coordination
- [ ] **Mempool**: Deposit validation, burn proof processing
- [ ] **Performance**: 1000+ TPS target verification
- [ ] **Backward Compatibility**: V2-V9 nodes continue working
- [ ] **Deployment Plan**: Exchange coordination timeline
- [ ] **Community**: Documentation, blog posts, Discord announcements

#### Soft Fork Review

```mermaid
flowchart TD
    A[Soft Fork Spec] --> B[Backward Compatibility]
    B --> C{Can old nodes handle?}
    C -->|No| D[Hard Fork Required]
    C -->|Yes| E[Protocol Extension]
    
    E --> F[Version Negotiation]
    F --> G[Grace Period Design]
    G --> H[Coverage Testing]
    
    H --> I{Coverage?}
    I -->|<95%| J[More Tests Needed]
    I -->|≥95%| K[Simulated Deployment]
    
    K --> L{Issues?}
    L -->|Yes| M[Design Update]
    L -->|No| N[Release Candidate]
    
    M --> E
    N --> O[Feature Flags]
    O --> P[Gradual Rollout]
    P --> Q[Monitoring Dashboard]
    
    Q --> R{Stable?}
    R -->|No| S[Rollback]
    R -->|Yes| T[Full Deployment]
    
    S --> U[Automatic Fallback]
    T --> V[Activation Complete]
```

**Fuego Soft Fork Example - Lite Blocks (V7):**
- [ ] **Protocol**: New message types 2000+ range
- [ ] **Fallback**: Automatic full block on failure
- [ ] **Testing**: V2-V6 nodes ignore gracefully
- [ ] **Performance**: 50% reduction in bandwidth
- [ ] **Monitoring**: P2P adoption statistics
- [ ] **Rollback**: Disable feature flag, no code revert needed
- [ ] **Documentation**: Update P2P specification
- [ ] **User Impact**: No action required

#### No Fork Review

```mermaid
flowchart TD
    A[Feature Request] --> B[Implementation]
    C[Unit Tests] --> D[Code Review]
    E[Documentation] --> F[CI/CD]
    
    B --> G[No Breaking Changes]
    G --> H{Release Blocked?}
    H -->|Yes| I[Address Issues]
    H -->|No| S[Release Ready]
    
    C --> J[≥80% Coverage]
    J --> K{Review Required?}
    K -->|Yes| L[Review Needed]
    K -->|No| M[Skip Review]
    
    D --> N[2 Reviewers]
    N --> O{Approved?}
    O -->|No| P[Additional Changes]
    O -->|Yes| Q[Merge]
    
    E --> R[Updated Changelog]
    R --> U[Release Notes]
    
    F --> V[Build Success]
    V --> W{All Platforms?}
    W -->|No| X[Fix Builds]
    W -->|Yes| Y[Generate Binaries]
    
    S & M & Q & U & Y --> Z[Release Candidate]
    Z --> AA[Beta Testing]
    AA --> BB{Feedback?}
    BB -->|Yes| CC[Final Release]
    BB -->|No| DD[Hold]
    DD --> EE[Standard Release Track]
    CC --> FF[Production Release]
```

**Fuego No-Fork Example - Configuration Manager:**
- [ ] **Testing**: Completely isolated unit tests
- [ ] **Validation**: No impact on consensus
- [ ] **Documentation**: All config options explained
- [ ] **CI/CD**: Standard pipeline (no special requirements)
- [ ] **Breaking**: Config loading maintains file structure
- [ ] **Review**: Standard code review (2 reviewers)
- [ ] **Security**: No cryptographic risk
- [ ] **Performance**: No measurable impact
- [ ] **Deployment**: Standard release process
- [ ] **Rollback**: Simple config revert

### Update Schedule

#### Fuego Release Cycle

```mermaid
graph TD
    A[Monthly Release Cycle] --> B[Week 1: Planning]
    B --> C[Week 2: Development]
    C --> D[Week 3: Testing]
    D --> E[Week 4: Release]
    
    E --> F[Major/Minor/Patch]
    F --> G[Hard Fork Planning]
    G --> H[Quarterly Hard Fork Review]
    H --> I[6-Month Hard Fork Release]
    
    I --> J[Emergency Fixes]
    J --> K[Security Patches]
    K --> L[Immediate Deployment]
    
    A --> M[Continuous Monitoring]
    M --> N[Performance Metrics]
    N --> O[User Feedback]
    O --> P[Iteration Planning]
```

**Fuego Schedule (Example):**
```
🔴 硬分叉 (每6个月):
  - 3月: 评估可能的V11
  - 9月: 实施V11 (如果已批准)

🟡 软分叉 (每3个月):
  - 1月: P2P 增强
  - 4月: RPC 新增
  - 7月: 性能优化
  - 10月: 隐私改进

🟢 无分叉 (每月):
  - 每月第1周: 发布准备
  - 每月第2周: 代码合并
  - 每月第3周: 测试和验证
  - 每月第4周: 生产发布

📊 监控更新 (每周):
  - 每周一: 系统健康报告
  - 每周三: 性能指标
  - 每周五: 安全扫描
```

#### Emergency Releases (Security)

```mermaid
flowchart TD
    A[Security Issue Discovered] --> B{Severity?}
    
    B -->|Critical| C[Immediate Response]
    B -->|High| D[24-hour Response]
    B -->|Medium| E[Standard Release]
    
    C --> F[Emergency Release]
    D --> G[Hotfix Release]
    E --> H[Next Scheduled Release]
    
    F --> I[2 hours - Patch]
    F --> J[4 hours - Build]
    F --> K[6 hours - Deploy]
    
    G --> L[24 hours - Full Release]
    L --> M[Coordination]
    K --> N[Network Upgrade]
    N --> O[Post-Release Monitoring]
    
    I --> J
    J --> K
    M --> P[Exchange Notification]
    M --> Q[Pool Notification]
    M --> R[User Alert]
    
    O --> S{Stable?}
    S -->|No| T[Additional Patches]
    S -->|Yes| U[Issue Closed]
    T --> F
```

**Fuego Security Response (Example):**
```
Critical Issue: DMWDA vulnerability in V10
Timeline:
- T+0: Issue discovered
- T+1h: Patch developed and tested
- T+2h: Build completed (v10.0.1-hotfix1)
- T+3h: Testnet deployment
- T+4h: Testnet validation  
- T+5h: Mainnet release
- T+6h: 70% nodes updated
- T+24h: 95% adoption
- T+48h: Full network stability

Coordination:
- P2P: Automatic version notification
- Exchanges: 1 hour notice
- Pools: 30 minutes notice
- Users: Instant update from "check update"
- FuegoTor: Vulnerable bridges taken offline
- DMWDA: Emergency threshold enabled
```

---

## SOURCE CODE ARCHITECTURE ANALYSIS

### Core System Architecture

```mermaid
graph TB
    subgraph "Fuego Core (V10)"
        A[Consensus Layer] --> B[Currency]
        A --> C[Blockchain]
        A --> D[Block Validation]
        
        B --> E[Difficulty: DMWDA V10]
        B --> F[Emission: Fuego V9+]
        B --> G[Parameters: V2-V10]
        
        C --> H[Storage: SwappedVector V7]
        C --> I[Indices: BankingIndex V8]
        C --> J[Cache: DepositCache V10]
        
        D --> K[Proof of Work: V6+ Variants]
        D --> L[Ring Signatures: V10 Dynamic]
        D --> M[Deposit: V8+ Validation]
    end
    
    subgraph "Network & Protocol"
        N[P2P Layer] --> O[Protocol V1-3]
        N --> P[Peer Discovery V10]
        N --> Q[Lite Blocks V7+]
        
        R[RPC Interface] --> S[Core RPC V2-V10]
        R --> T[Deposit RPC V8+]
        R --> U[Burn RPC V9+]
        R --> V[DMWDA RPC V10]
    end
    
    subgraph "Advanced Features"
        W[Deposit System] --> X[BankingIndex V8]
        W --> Y[Yield Calculation V10]
        W --> Z[Deposit Manager]
        
        A1[Burn System] --> A2[Burn Proof V9]
        A1 --> A3[Proof Verification]
        A1 --> A4[Hard Burn]
        
        B1[DMWDA] --> B2[Multi-Window V10]
        B1 --> B3[Anomaly Detection]
        B1 --> B4[Emergency Response]
        
        C1[FuegoTor] --> C2[Tor Manager V10]
        C1 --> C3[Hybrid Mode]
        C1 --> C4[Private Transactions]
    end
    
    subgraph "Support Systems"
        D1[Wallet Layer] --> D2[Core Wallet]
        D1 --> D3[Legacy Wallet]
        D1 --> D4[Deposit Wallet V8]
        
        E1[Tools & Utilities] --> E2[Miner V7+]
        E1 --> E3[Optimizer V7]
        E1 --> E4[CLI/TUI]
        
        F1[Monitoring] --> F2[Metrics V10]
        F1 --> F3[Alerts V8]
        F1 --> F4[Performance Profiler]
    end
    
    A --> W
    A --> A1
    A --> B1
    N --> C1
    R --> D1
    N --> E1
    C --> F1
    
    classDef critical fill:#ff4d4d,stroke:#333,stroke-width:2px;
    classDef feature fill:#ffd74d,stroke:#333,stroke-width:2px;
    classDef support fill:#4dff88,stroke:#333,stroke-width:2px;
    
    class A,B,C,D,E,F,G,H,I,J,K,L,M critical;
    class O,P,Q,R,S,T,U,V,W,X,Y,Z feature;
    class A1,A2,A3,A4,B1,B2,B3,B4,C1,C2,C3,C4 support;
```

### Code Dependency Analysis

#### Critical Consensus Functions

```mermaid
graph LR
    subgraph "V2-V10 Consensus Chain"
        A[getBlockReward] --> B[EMISSION_SPEED]
        A --> C[BlockedMajorVersion]
        A --> D[Monetary Policy]
        
        B --> E[V2: 18]
        B --> F[V8: 19]
        B --> G[V9-V10: 20]
        
        D --> H[V2-V6: 9M]
        D --> I[V7-V10: 8M8]
        
        C --> J[V2: 147958]
        C --> K[V3: 154321]
        C --> L[V4: 300000]
        C --> M[V5: 324819]
        C --> N[V6: 345678]
        C --> O[V7: 657000]
        C --> P[V8: 800000]
        C --> Q[V9: 826420]
        C --> R[V10: 999999]
    end
    
    subgraph "Difficulty Calculation"
        S[nextDifficulty] --> T[V2: Zawy v1.0]
        S --> U[V3: LWMA1]
        S --> V[V4: LWMA1 Fuego]
        S --> W[V10: DMWDA]
        
        W --> X[Short Window: 15]
        W --> Y[Medium Window: 45]
        W --> Z[Long Window: 120]
        W --> A1[Emergency: 5]
    end
    
    subgraph "Validation Functions"
        B1[checkProofOfWork] --> B2[V2-V4: Standard]
        B1 --> B3[V5-V6: CN Variants]
        B1 --> B4[V7-V10: Enhanced]
        
        B4 --> C1[Variant Detection]
        B4 --> C2[ASIC Resistance]
        B4 --> C3[Performance Check]
        
        D1[validateTransaction] --> D2[V2-V9: Static Mixins]
        D1 --> D3[V10: Dynamic Mixins]
        D1 --> D4[Deposit Validation V8+]
        D1 --> D5[Burn Proof V9+]
    end
    
    A --> S
    B1 --> D1
    C --> B1
    R --> B1
    
    classDef emission fill:#ff9999,stroke:#333,stroke-width:2px;
    classDef difficulty fill:#99ff99,stroke:#333,stroke-width:2px;
    classDef validation fill:#9999ff,stroke:#333,stroke-width:2px;
    
    class A,B,C,D,E,F,G,H,I,J,K,L,M,N,O,P,Q,R emission;
    class S,T,U,V,W,X,Y,Z,A1 difficulty;
    class B1,B2,B3,B4,C1,C2,C3,D1,D2,D3,D4,D5 validation;
```

#### Transaction Processing Pipeline (V10)

```mermaid
sequenceDiagram
    participant U as User
    participant W as Wallet
    participant M as Mempool V10
    participant B as Blockchain V10
    participant N as P2P V3
    participant D as DMWDA V10
    participant X as Deposit Index V8
    participant Y as Burn Proof V9
    
    U->>W: Create Transaction
    W->>W: Add Deposit Commit V8
    W->>W: Calculate Dynamic Mixins V10
    W->>M: Submit Transaction
    
    M->>M: Fee Validation (Dynamic)
    M->>M: Deposit Check V8
    M->>M: Burn Proof Check V9
    M->>M: Meta Data V10 (DMWDA stats)
    M->>N: Broadcast
    
    N->>B: Request Block
    B->>B: Generate Block Template
    B->>B: Get DMWDA Difficulty V10
    B->>B: Select Transactions (Deposit Awareness)
    B->>B: Validate All (POW + Signatures)
    B->>B: Process Deposits V8
    B->>B: Generate Burn Proofs V9
    B->>B: Update DMWDA Stats V10
    
    B->>D: Submit DMWDA Data
    D->>D: Validate Multi-Window
    D->>D: Detect Anomalies
    D->>D: Update Difficulty
    
    B->>X: Update Deposit State
    B->>X: Index New Deposits
    B->>X: Mark Expired Deposits
    
    B->>Y: Log Burn Proofs
    B->>Y: Verify Proofs
    B->>Y: Update Burn Index
    
    B->>N: Propagate Block
    N->>M: Remove Mined Transactions
    N->>W: Update Balances
    N->>U: Transaction Confirmed
    
    B->>B: Check Upgrade V2-V10
    B->>B: Log Performance V10
```

### Function Complexity Analysis

#### High-Impact Functions (Hard Fork Required)

| Function | V2-V10 Complexity | Dependencies | Impact | Version Change Example |
|----------|-------------------|--------------|--------|----------------------|
| `Currency::getBlockReward` | High (100+ lines) | Emission factor, Block major version, Height, Median size, Fee, Generated coins | 🔴 Critical | V9: Added 95/5 split |
| `Blockchain::validateBlock` | Very High (200+ lines) | POW check, TX validation, Deposit validation, Burn check, Version check | 🔴 Critical | V10: Added DMWDA check |
| `Difficulty::nextDifficulty` | High (150+ lines) | Timestamps, Cumulative diff, Window size, Algorithm type | 🔴 Critical | V10: Complete DMWDA rewrite |
| `TransactionPool::checkTxInputs` | High (100+ lines) | Ring signatures, Key images, Mixin count, Deposit check | 🔴 Critical | V10: Added dynamic mixins |
| `UpgradeDetector::blockPushed` | Medium (80+ lines) | Block version, Upgrade height, Voting, Activation | 🔴 Critical | V2-V10: All version transitions |
| `BankingIndex::validateDeposit` | Medium (80+ lines) | Deposit amount, Term, Provider, Current height | 🔴 High | V8+ (V10: Enhanced validation) |
| `BurnProof::verify` | Medium (60+ lines) | Merkle proof, TX verification, Output check | 🔴 High | V9+ (V10: Added privacy) |

**Fuego Specific:**
```cpp
// V10: DMWDA complexity
difficulty_type Currency::nextDifficultyV10(...) {
    // 12 distinct calculations
    // 5 window types
    // 3 confidence levels
    // 2 emergency modes
    // 1 final adjustment
    // Complexity: 7/10
    return calculateDMWDA(...);
}

// V9: Burn proof complexity  
bool BurnProof::verify(...) {
    // Merkle tree validation
    // Signatures check
    // Amount verification
    // Double-spend prevention
    // Complexity: 6/10
    return crypto.check(...);
}
```

#### Medium-Impact Functions (Soft Fork Compatible)

| Function | Complexity | Dependencies | Impact | Example |
|----------|------------|--------------|--------|---------|
| `TransactionPool::addTransaction` | Medium | Mempool, Fee, Size | 🟡 Medium | V8: Deposit support |
| `Network::handleMessage` | Medium | P2P, Version, Message | 🟡 Medium | V7: Lite blocks |
| `RPC::getBlocks` | Low | Hashes, Count | 🟡 Low | V10: DMWDA stats |
| `Wallet::createTransaction` | Medium | Keys, Amount, Mixins | 🟡 Medium | V8: Deposit TX |
| `Logging::logBlock` | Low | Block, Info level | 🟢 Low | V9: Enhanced format |

**Fuego Example:**
```cpp
// V7: Soft fork - Lite blocks
void P2P::handleLiteBlock(const LiteBlock& block) {
    // New message, old nodes ignore
    if (!validateLiteBlock(block)) {
        // Soft failure - request full block
        requestFullBlock(block.header);
        return;
    }
    processLiteBlock(block);
    broadcastToV7Plus(block);
}

// V8: Soft fork - Deposit RPC
json RPC::createDeposit(const json& params) {
    // Only V8+ responses
    if (!m_currency.supportsDeposits()) {
        return error("Not supported");
    }
    return processDeposit(params);
}
```

#### Low-Impact Functions (No Fork Required)

| Function | Complexity | Dependencies | Impact | Example |
|----------|------------|--------------|--------|---------|
| `Logger::logMessage` | Very Low | Message, Level | 🟢 Minimal | V9: Formatting |
| `UI::printHelp` | Very Low | Text output | 🟢 Minimal | V8: Deposit help |
| `Config::loadFile` | Low | File path | 🟢 Minimal | V10: New config |
| `CLI::handleInput` | Low | Command string | 🟢 Minimal | V10: New commands |
| `Metrics::record` | Low | Key, Value | 🟢 Minimal | V10: DMWDA stats |

**Fuego Example:**
```cpp
// V9: No fork - Enhanced logging
void Logger::logBurn(const BurnProof& proof, bool verified) {
    // Only changes logging, no consensus impact
    if (verified) {
        logger(INFO) << "BURN VERIFIED: " << proof.hash;
    } else {
        logger(WARNING) << "BURN FAILED: " << proof.hash;
    }
}

// V10: No fork - UI improvements
void CLI::printDepositYield(const Deposit& deposit) {
    // Enhanced output only
    std::cout << "Yield: " << calculateYieldV10(deposit) << " XFG\n";
    std::cout << "APY: " << calculateAPYV10(deposit) << "%\n";
}
```

### Code Style and Patterns Analysis

#### Consensus-Critical Code Patterns

**Error Handling with Context (V2-V10)**
```cpp
bool validateTransaction(const Transaction& tx, tx_verification_context& tvc) {
    tvc = {};  // Reset context
    
    // Input validation
    if (!checkInputs(tx)) {
        tvc.m_verification_failed = true;
        tvc.m_tx_inputs_invalid = true;
        tvc.m_relayable = false;
        return false;
    }
    
    // V8+: Deposit validation
    if (containsDeposit(tx)) {
        if (!validateDeposit(tx, tvc)) {
            tvc.m_verification_failed = true;
            tvc.m_deposit_invalid = true;
            return false;
        }
    }
    
    // V9+: Burn proof validation
    if (containsBurn(tx)) {
        if (!validateBurnProof(tx, tvc)) {
            tvc.m_verification_failed = true;
            tvc.m_burn_invalid = true;
            return false;
        }
    }
    
    // V10: Dynamic mixin check
    if (tx.majorVersion >= BLOCK_MAJOR_VERSION_10) {
        if (!checkDynamicMixin(tx)) {
            tvc.m_verification_failed = true;
            tvc.m_mixin_invalid = true;
            return false;
        }
    }
    
    tvc.m_verification_passed = true;
    tvc.m_relayable = true;
    return true;
}
```

**Atomic Resource Management**
```cpp
class Blockchain {
public:
    bool validateBlock(const Block& block) {
        // V2-V10: Thread-safe with RAII
        std::lock_guard<decltype(m_blockchain_lock)> lock(m_blockchain_lock);
        
        // V8: Deposit lock
        std::lock_guard<decltype(m_deposit_lock)> deposit_lock(m_deposit_lock);
        
        // V9: Burn proof lock
        std::lock_guard<decltype(m_burn_lock)> burn_lock(m_burn_lock);
        
        // V10: DMWDA lock
        std::lock_guard<decltype(m_dmwda_lock)> dmwda_lock(m_dmwda_lock);
        
        return validateBlockContents(block);
    }
    
    // V10: Scoped cache lock
    bool getBlockCached(uint32_t height, Block& block) {
        std::lock_guard lock(m_cache_mutex);
        auto it = m_cache.find(height);
        if (it != m_cache.end()) {
            block = it->second;
            return true;
        }
        return false;
    }
};
```

**Atomic Operations**
```cpp
// V7-V10: Threading improvements
class Metrics {
public:
    // V10: Thread-safe counters
    void increment(const std::string& key, uint64_t value = 1) {
        m_counters[key].fetch_add(value, std::memory_order_relaxed);
    }
    
    uint64_t get(const std::string& key) const {
        auto it = m_counters.find(key);
        return it != m_counters.end() ? it->second.load(std::memory_order_relaxed) : 0;
    }
    
    // V10: Atomic deposit counter
    std::atomic<uint64_t> m_activeDeposits{0};
    std::atomic<uint64_t> m_totalYieldPaid{0};
    std::atomic<uint64_t> m_totalBurned{0};
    
    // DMWDA stats
    std::atomic<double> m_dmwdaConfidence{0.5};
    std::atomic<double> m_dmwdaHashrate{0.0};
};
```

#### Performance-Critical Code Patterns

**Pre-allocation**
```cpp
// V10: DMWDA calculations
difficulty_type Currency::nextDifficultyV10(...) {
    std::vector<std::uint64_t> timestamps;
    std::vector<difficulty_type> cumulativeDifficulties;
    
    // Pre-allocate (V10 improvement)
    timestamps.reserve(DMWDA_LONG_WINDOW + 10);
    cumulativeDifficulties.reserve(DMWDA_LONG_WINDOW + 10);
    
    // Efficient collection
    for (size_t i = 0; i < DMWDA_LONG_WINDOW; ++i) {
        timestamps.push_back(block.timestamps[i]);
        cumulativeDifficulties.push_back(block.difficulties[i]);
    }
    
    // V10: Multi-window calculation
    return calculateDMWDA(timestamps, cumulativeDifficulties);
}
```

**Move Semantics (V7-V10)**
```cpp
// V8: Deposit creation
Transaction createMinerTxV8(uint64_t blockReward, uint64_t fee, 
                           const DepositEntry* deposit) {
    Transaction tx;
    tx.version = TRANSACTION_VERSION_2;
    tx.unlockTime = 0;
    
    // Deposit commitment (V8+)
    if (deposit) {
        DepositCommitment commit;
        commit.amount = deposit->amount;
        commit.term = deposit->term;
        commit.commitmentHash = calculateCommitmentHash(*deposit);
        tx.extra = serializeCommitment(commit);  // Move, no copy
    }
    
    return std::move(tx);  // Efficient transfer
}

// V10: Block propagation
void broadcastBlockV10(Block&& block) {
    // Use move semantics for efficiency
    m_p2p->broadcastLiteBlock(std::move(block));
    m_mempool->validateAsync(std::move(block));
}
```

**Efficient Crypto Operations**
```cpp
// V10: Hash calculation optimization
Crypto::Hash calculateHashV10(const Block& block) {
    // V2-V6: Standard
    // V10: Optimized serialization
    static thread_local Crypto::Hash cache;
    static thread_local uint64_t lastHeight = 0;
    
    if (block.height == lastHeight) {
        return cache;  // Cache for same block
    }
    
    // V10: Fast path for common case
    if (block.majorVersion >= BLOCK_MAJOR_VERSION_10) {
        // Use pre-computed header hash
        cache = block.headerCache;
        return cache;
    }
    
    // V2-V9: Full calculation
    cache = getObjectHash(block);
    lastHeight = block.height;
    return cache;
}
```

### Module Interdependence Analysis

```mermaid
graph TD
    subgraph "Critical Dependencies"
        A[CryptoNoteCore] --> B[Consensus]
        B --> C[Currency.h]
        B --> D[CryptoNoteConfig.h]
        
        C --> E[Block Major Version]
        D --> F[Upgrade Heights]
        E --> G[Hard Fork Activation]
        F --> G
        
        A --> H[Blockchain]
        H --> I[Block Validation]
        H --> J[Transaction Pool]
        
        I --> K[POW Check V10]
        I --> L[Ring Signature V10]
        I --> M[Deposit Check V8]
        I --> N[Burn Proof V9]
        
        J --> O[Dynamic Mixins V10]
        J --> P[DMWDA Stats V10]
    end
    
    subgraph "Feature Modules (V7-V10)"
        Q[Deposit System V8] --> R[BankingIndex]
        Q --> S[Yield Calculator V10]
        Q --> T[Deposit Manager]
        
        U[Burn System V9] --> V[Proof Generator]
        U --> W[Proof Verifier]
        U --> X[Burn Index]
        
        Y[DMWDA V10] --> Z[Multi-Window]
        Y --> A1[Anomaly Detection]
        Y --> B1[Emergency Response]
        
        C1[FuegoTor V10] --> D1[Tor Manager]
        C1 --> E1[Hybrid Mode]
        C1 --> F1[Private TX]
    end
    
    subgraph "Support层"
        G1[Network Layer] --> H1[P2P V3]
        G1 --> I1[Peer Discovery V10]
        G1 --> J1[Protocol]
        
        K1[RPC Layer] --> L1[Core RPC]
        K1 --> M1[Deposit RPC V8]
        K1 --> N1[Burn RPC V9]
        K1 --> O1[DMWDA RPC V10]
        
        P1[Wallet Layer] --> Q1[Core Wallet]
        P1 --> R1[Legacy Wallet]
        P1 --> S1[Deposit Wallet V8]
    end
    
    A --> Q
    A --> U
    A --> Y
    A --> C1
    A --> G1
    A --> K1
    A --> P1
    
    H --> Q
    J --> Q
    I --> M
    J --> N
    J --> O
    
    classDef core fill:#ff4d4d,stroke:#333,stroke-width:2px;
    classDef feature fill:#ffd74d,stroke:#333,stroke-width:2px;
    classDef support fill:#4dff88,stroke:#333,stroke-width:2px;
    
    class A,B,C,D,E,F,G,H,I,J,K,L,M,N,O,P core;
    class Q,R,S,T,U,V,W,X,Y,Z,A1,B1,C1,D1,E1,F1 feature;
    class G1,H1,I1,J1,K1,L1,M1,N1,O1,P1,Q1,R1,S1 support;
```

#### Dependency Violation Prevention

```cpp
// V2-V10: Layer separation enforcement
namespace CryptoNote {
    
    // ✅ CORRECT: Core knows nothing about UI
    // ❌ WRONG: Core cannot include UI headers
    
    // ✅ CORRECT: Consensus depends on crypto only
    // ❌ WRONG: Consensus cannot depend on wallet
    
    // ✅ CORRECT: Network depends on core
    // ❌ WRONG: Core cannot depend on network
    
    // ✅ CORRECT: Deposit system optional (V8+)
    #ifdef BUILD_DEPOSIT
    class DepositSystem {
        // V8: Deposit module
        // Only built if enabled, no hard dependency
    };
    #endif
    
    // ✅ CORRECT: FuegoTor optional (V10+)
    #ifdef BUILD_TOR
    class FuegoTorManager {
        // V10: Tor integration
        // Separate build target
    };
    #endif
    
    // ✅ CORRECT: Feature flags for soft forks
    class Config {
        bool liteBlocks = true;     // V7 feature
        bool deposits = true;       // V8 feature
        bool burnProof = true;      // V9 feature
        bool dmwda = true;          // V10 feature
        bool fuegoTor = false;      // V10 optional
    };
}
```

### Code Quality Metrics

#### Maintainability Index V2-V10

```mermaid
graph TD
    A[Overall Complexity] --> B[Core: 65]
    A --> C[Network: 75]
    A --> D[Features: 85]
    
    B --> E[Currency: 60]
    B --> F[Blockchain: 70]
    B --> G[Validation: 65]
    
    C --> H[P2P: 75]
    C --> I[RPC: 80]
    
    D --> J[Deposits: 85]
    D --> K[Burn: 90]
    D --> L[DMWDA: 70]
    D --> M[FuegoTor: 75]
    
    N[Target: >80] --> O[All Modules Pass]
    
    E --> P[Complex: Hard Fork only]
    F --> P
    G --> P
    L --> Q[Complex: Soft Fork OK]
    H --> Q
    M --> R[Complex: No Fork OK]
    J --> R
    K --> R
    I --> R
    
    P --> S[High Criticality]
    Q --> T[Medium Criticality]
    R --> U[Low Criticality]
    
    S --> V[Extensive Testing]
    T --> W[Standard Testing]
    U --> X[Light Testing]
    
    V --> Y[95% Coverage Required]
    W --> Z[85% Coverage]
    X --> A1[80% Coverage]
    
    Y --> B1[Security Audit]
    Z --> C1[Peer Review]
    A1 --> D1[Automated]
```

**Fuego Metric Results:**
```
Core System Metrics:
- Currency: 65/100 (High complexity, accepts hard forks)
- Blockchain: 75/100 (Moderate, upgraded with V7-V10)
- Transaction Pool: 80/100 (Good structure)
- Difficulty: 70/100 (DMWDA V10 brought this up)

Network Layer:
- P2P: 75/100 (V7 lite blocks improved score)
- RPC: 85/100 (Clean, extensible, V8-V10 additions)
- Protocol: 78/100 (Compatible design)

Feature Implementation:
- Deposit System: 85/100 (V8+ well-designed)
- Burn System: 90/100 (V9+ clean implementation)
- DMWDA: 70/100 (V10 complex but necessary)
- FuegoTor: 75/100 (V10 separable design)

Test Coverage:
- Core Consensus: 98% (V2-V10 + all forks)
- Deposit Validation: 96% (V8+)
- Burn Verification: 97% (V9+)
- DMWDA Algorithm: 99% (V10 extensive testing)
- P2P Protocol: 92% (V7 compatibility)
- RPC Layer: 88% (V8-V10 new methods)
```

#### Test Coverage Requirements (V2-V10)

```mermaid
graph TD
    A[Coverage Target] --> B[Critical: 95%]
    A --> C[Standard: 85%]
    A --> D[Minimum: 80%]
    
    B --> E[Consensus Functions]
    B --> F[Validation Logic]
    B --> G[Cryptographic Primitives]
    
    C --> H[Network Protocol]
    C --> I[Feature Implementation]
    C --> J[RPC Layer]
    
    D --> K[Tools & Utilities]
    D --> L[UI Components]
    D --> M[Configuration]
    
    E --> N[V2-V10: All versions]
    F --> O[POW: V2-V10]
    F --> P[TX Validation: V2-V10]
    G --> Q[Crypto: V2-V10]
    
    H --> R[P2P: V2-V10]
    I --> S[Deposits: V8-V10]
    I --> T[Burn: V9-V10]
    I --> U[DMWDA: V10]
    
    V[CI/CD Gate] --> W{All passed?}
    W -->|Yes| X[Release Ready]
    W -->|No| Y[Fix Coverage]
    Y --> Z[Add Tests]
    Z --> V
    
    X --> AA[Security Audit]
    AA --> AB[Peer Review]
    AB --> AC[Release]
```

**Fuego Coverage Enforcement:**
```bash
# V2-V10: Coverage tracking
make coverage CHECK=95  # Consensus
make coverage CHECK=85  # Features
make coverage CHECK=80  # General

# V10 specific tests
make test_dmwda        # 99% coverage
make test_deposits     # 96% coverage
make test_burn         # 97% coverage
make test_fuegotor     # 92% coverage

# Fork version tests
make test_v2_to_v10    # All upgrade paths
make test_upgrade_detector  # Coordinator
make test_backwards_compat # Old nodes
```

### Security-Critical Code Analysis

#### Cryptographic Functions (V2-V10)

```mermaid
graph TD
    A[Cryptographic Core] --> B[Hash: CryptoNight V2-V10]
    A --> C[Signature: EdDSA V2-V10]
    A --> D[Ring Signature: V2-V10]
    A --> E[Key Derivation: V2-V10]
    
    B --> F[CN Standard V2-V4]
    B --> G[CN Variants V5-V6]
    B --> H[CN Fango V7-V10]
    B --> I[Enhanced V10]
    
    D --> J[Fixed Mixins V2-V9]
    D --> K[Dynamic Mixins V10]
    D --> L[Dynamaxin V10]
    
    E --> M[Standard V2-V6]
    E --> N[Deposit Keys V8-V10]
    E --> O[Burn Keys V9-V10]
    E --> P[Tor Keys V10]
    
    Q[Security Review] --> R{2 independent audits?}
    R -->|Yes| S[V2-V10: Approved]
    R -->|No| T[Re-review needed]
    T --> U[Update implementation]
    U --> Q
```

**Fuego Crypto Functions (V10):**
```cpp
// V10: Cryptographic security checklist
namespace Crypto {
    // ✅ V2-V10: Ring signatures - 2 audits
    bool check_ring_signature(...);
    
    // ✅ V2-V6: Fixed mixin - 3 audits  
    bool validate_mixin_fixed(...);
    
    // ✅ V10: Dynamic mixin (Dynamaxin) - 1 audit + regression
    bool validate_mixin_dynamic(...);
    
    // ✅ V10: FuegoTor - 2 privacy audits
    bool check_tor_transaction(...);
    
    // ✅ V9: Burn proof - 1 audit + math review
    bool verify_burn_proof(...);
    
    // ✅ V8: Deposit commitment - 2 reviews
    bool verify_deposit_commitment(...);
    
    // ❌ V10: DMWDA - Pending second audit
    bool check_dmwda_confidence(...);
}
```

#### Validation Functions

```mermaid
graph TD
    A[Block Validation] --> B[Structure V2-V10]
    A --> C[Version V2-V10]
    A --> D[POW V2-V10]
    A --> E[TX Count V2-V10]
    A --> F[Size V2-V10]
    A --> G[Timestamp V2-V10]
    
    H[Transaction Validation] --> I[Inputs V2-V10]
    H --> J[Outputs V2-V10]
    H --> K[Signatures V2-V10]
    H --> L[Extra Data V2-V10]
    H --> M[Deposit V8-V10]
    H --> N[Burn V9-V10]
    
    O[Crypto Validation] --> P[Hash V2-V10]
    O --> Q[Signature V2-V10]
    O --> R[Ring Sig V2-V10]
    O --> S[Key Image V2-V10]
    
    T[Consensus Validation] --> U[Reward V2-V10]
    T --> V[Difficulty V2-V10]
    T --> W[Upgrade V2-V10]
    T --> X[DMWDA V10]
    
    Y[Security] --> Z{All 100%?}
    Z -->|Yes| AA[Approved]
    Z -->|No| AB[Reject]
    
    B & C & D & E & F & G & I & J & K & L & M & N & P & Q & R & S & U & V & W & X --> Z
```

**Fuego Validation Requirements:**
```cpp
// V2-V10: 100% validation coverage
class Validation {
public:
    // ✅ V2-V10: Block structure
    bool validateBlockStructure(const Block& block);
    
    // ✅ V2-V10: Version checking  
    bool validateBlockVersion(const Block& block, uint32_t height);
    
    // ✅ V2-V10: POW
    bool validatePOW(const Block& block, difficulty_type diff);
    
    // ✅ V2-V10: Transaction count
    bool validateTransactionCount(const Block& block);
    
    // ✅ V2-V10: Block size
    bool validateBlockSize(const Block& block);
    
    // ✅ V2-V10: Timestamp
    bool validateTimestamp(const Block& block, uint64_t time);
    
    // ✅ V2-V10: Transaction inputs
    bool validateInputs(const Transaction& tx);
    
    // ✅ V2-V10: Transaction outputs
    bool validateOutputs(const Transaction& tx);
    
    // ✅ V2-V10: Signatures
    bool validateSignatures(const Transaction& tx);
    
    // ✅ V2-V10: Extra data
    bool validateExtra(const Transaction& tx);
    
    // ✅ V8-V10: Deposit validation
    bool validateDeposit(const Transaction& tx, uint32_t height);
    
    // ✅ V9-V10: Burn validation
    bool validateBurn(const Transaction& tx);
    
    // ✅ V2-V10: Hash validation
    bool validateHash(const Crypto::Hash& hash);
    
    // ✅ V2-V10: Signature validation
    bool validateSignature(const Crypto::Signature& sig);
    
    // ✅ V2-V10: Ring signature validation
    bool validateRingSignature(const Transaction& tx);
    
    // ✅ V2-V10: Key image validation
    bool validateKeyImage(const Crypto::KeyImage& ki);
    
    // ✅ V2-V10: Reward calculation
    bool validateReward(uint8_t version, uint64_t expected, uint64_t actual);
    
    // ✅ V2-V10: Difficulty calculation
    bool validateDifficulty(uint32_t height, difficulty_type diff);
    
    // ✅ V2-V10: Upgrade detection
    bool validateUpgrade(uint32_t height, uint8_t version);
    
    // ✅ V10: DMWDA validation (pending second audit)
    bool validateDMWDA(const DMWDAStats& stats);
};
```

### Performance Bottlenecks and Optimization Points

#### Database Operations

```mermaid
graph TD
    A[Database Operations] --> B[Store Block: 100ms]
    A --> C[Get Blocks: 200ms]
    A --> D[Get TX: 5ms]
    A --> E[Get Deposit: 10ms]
    
    F[Optimization] --> G[Caching V7]
    F --> H[Batch Queries V8]
    F --> I[Deposit Cache V10]
    F --> J[DMWDA Cache V10]
    
    G --> K[Block Cache: 1GB]
    K --> L[1000 blocks]
    H --> M[Prepared Statements]
    H --> N[Indexed Queries]
    I --> O[Hot Deposits: 50MB]
    O --> P[Active only]
    J --> Q[Recent Stats: 10MB]
    Q --> R[Current height]
    
    B --> S[10ms - Block Hash]
    B --> T[50ms - Transaction Index]
    B --> U[30ms - Deposit Index V8]
    B --> V[10ms - Burn Index V9]
    
    C --> W[50ms - Block Heights]
    C --> X[100ms - Full Blocks]
    C --> Y[50ms - Headers only]
    
    Z[Target: <50ms] --> AA[V10 Achieved]
    Z --> AB[Meets Requirements]
```

**Fuego Database Optimizations (V10):**
```cpp
// V8-V10: Database improvements
class BlockchainStorage {
public:
    // V7: Block cache (improvement)
    std::unordered_map<uint32_t, Block> m_blockCache;
    size_t cacheSize = 1000;  // 1GB max
    
    // V8: Deposit caching
    std::unordered_map<DepositKey, DepositEntry> m_depositCache;
    size_t depositCacheSize = 50;  // 50MB for hot deposits
    
    // V10: DMWDA stats cache
    std::map<uint32_t, DMWDAStats> m_dmwdaCache;
    size_t dmwdaCacheSize = 10;  // 10MB for recent stats
    
    // V8: Optimized queries
    bool getBlockFast(uint32_t height, Block& block) {
        // Check cache first
        if (m_blockCache.find(height, block)) {
            metrics.cacheHit();
            return true;
        }
        
        // V8: Prepared statement
        auto stmt = m_db.prepare("SELECT * FROM blocks WHERE height = ?");
        stmt.bind(1, height);
        if (stmt.execute()) {
            block = parseBlock(stmt.getBlob());
            m_blockCache.insert(height, block);
            return true;
        }
        return false;
    }
    
    // V10: Batch deposit queries
    std::vector<DepositEntry> getExpiringDepositsV10(uint32_t height, uint32_t window) {
        // Optimized range query
        std::vector<DepositEntry> result;
        result.reserve(100);  // Pre-allocate
        
        auto stmt = m_db.prepare(
            "SELECT * FROM deposits WHERE maturity BETWEEN ? AND ? AND state = 'active'"
        );
        stmt.bind(1, height);
        stmt.bind(2, height + window);
        
        while (stmt.execute()) {
            result.push_back(parseDeposit(stmt));
        }
        return result;
    }
};
```

#### Network Operations

```mermaid
graph TD
    A[Network Operations] --> B[Broadcast Block: 500ms]
    A --> C[Validate & Add: 200ms]
    A --> D[Propagate TX: 100ms]
    A --> E[Peer Discovery: 50ms]
    
    F[Optimization V7-V10] --> G[Lite Blocks V7]
    F --> H[Async Validation V8]
    F --> I[Batch Propagation V9]
    F --> J[DMWDA Aware V10]
    
    G --> K[90% bandwidth reduction]
    K --> L[Send hashes only]
    H --> M[Parallel CPU use]
    M --> N[2x throughput]
    I --> O[Batch 128 TX]
    O --> P[Network efficiency]
    J --> Q[Difficulty stability]
    Q --> R[Reduced reorgs]
    
    B --> S[20ms - Local cache]
    B --> T[480ms - Network I/O]
    C --> U[150ms - Validation]
    C --> V[50ms - Index update]
    D --> W[80ms - Mempool add]
    D --> X[20ms - P2P broadcast]
    
    Y[Target: <1s block time] --> Z[V10: 1.2s avg]
    Y --> AA[Meets requirements]
    Z --> AB[Borderline acceptable]
    AA --> AC[Optimization ongoing]
```

**Fuego Network Optimizations (V10):**
```cpp
// V7-V10: Network performance
class P2PNetwork {
public:
    // V7: Lite block propagation
    void broadcastLiteBlock(const Block& block) {
        LiteBlock lb;
        lb.header = block.header;
        lb.transactionHashes = getTransactionHashes(block);
        lb.missingTxs = getMissingTxs(block);
        
        // Send only to V7+ nodes
        for (auto& peer : m_peers) {
            if (peer.version >= 3 && peer.features.liteBlocks) {
                peer.sendLiteBlock(lb);  // 90% smaller
            } else {
                peer.sendFullBlock(block);  // Legacy
            }
        }
    }
    
    // V8: Async validation
    void validateAndAddBlockAsync(const Block& block) {
        // V8: Parallel validation
        std::thread([this, block]() {
            BlockValidationResult result = validateBlock(block);
            if (result.valid) {
                m_mainChain.addBlock(block);
            } else {
                logger(WARNING) << "Invalid block: " << result.error;
            }
        }).detach();
    }
    
    // V9: Batch transaction propagation
    void batchPropagate(const std::vector<Transaction>& txs) {
        // V9: Send up to 128 TX per packet
        std::vector<std::vector<Transaction>> batches;
        batchTxs(txs, 128, batches);
        
        for (auto& batch : batches) {
            m_p2p->broadcastTxBatch(batch);
        }
    }
    
    // V10: DMWDA-aware peer selection
    std::vector<Peer> getStablePeers() {
        // V10: Prefer nodes with stable difficulty
        auto peers = getConnectedPeers();
        std::sort(peers.begin(), peers.end(), [](const Peer& a, const Peer& b) {
            return a.dmwda.stability > b.dmwda.stability;
        });
        return peers;
    }
};
```

### Future Development Considerations

#### Scalability Planning (V10+)

```mermaid
graph TD
    A[Current V10 Limits] --> B[TPS: ~3]
    A --> C[Block Size: 420KB]
    A --> D[Block Time: 480s]
    A --> E[Finality: ~80min]
    
    F[Phase 1: Optimization] --> G[Soft Fork Enhancement]
    G --> H[Increase TPS: 10]
    G --> I[Lite blocks: V7+]
    
    F --> J[Performance Tuning]
    J --> K[Database: V10 improvements]
    J --> L[Network: async V8]
    
    M[Phase 2: Layer-2] --> N[Soft Fork Integration]
    N --> O[Payment Channels]
    N --> P[Sidechains]
    
    Q[Phase 3: Consensus Upgrade] --> R[Hard Fork Required]
    R --> S[New Algorithm: V11+]
    R --> T[Sub-second Finality]
    
    U[Phase 4: Sharding] --> V[Hard Fork Required]
    V --> W[Network Partitioning]
    V --> X[Cross-shard Communication]
    
    Y[Metrics] --> Z[Target: 1000+ TPS]
    Z --> AA[Sub-second finality]
    Z --> AB[Infinite scaling]
    
    A --> AC[V10 Baseline]
    H --> AD[V11 Target: 10 TPS]
    K --> AE[V11+ EVM: 100 TPS]
    O --> AF[L2: 1000 TPS]
    S --> AG[V12: 500 TPS]
    X --> AH[V13+: 10000 TPS]
```

**Fuego Development Roadmap:**
```
V11 (Evaluation - Q2 2024):
- Consensus: New algorithm evaluation
- Performance: TPS target 10
- Features: EVM integration
- Hard Fork: Required

V12 (Planning - Q4 2024):
- Sharding: Prototype
- Consensus: PoS hybrid
- Hard Fork: Required

V13+ (Research - 2025):
- Full sharding
- 10k+ TPS
- Infinite scaling
- Hard Fork(s): Required
```

#### Upgrade Path Planning

```mermaid
graph TD
    A[Current: V10] --> B{Performant?}
    B -->|Yes| C[Stable Path]
    B -->|No| D[Optimization Path]
    
    C --> E[Hard Fork Re eval]
    E --> F[V11: Consensus review]
    F --> G[Community vote]
    G --> H{Approved?}
    H -->|Yes| I[Coordinate V11]
    H -->|No| J[Continue V10]
    
    D --> K[Soft Fork Enhancements]
    K --> L[Performance patches]
    L --> M[Measure impact]
    M --> N{Meeting targets?}
    N -->|Yes| C
    N -->|No| O[Evaluate V11]
    
    I --> P[Release V11 Hard Fork]
    P --> Q[Monitor 6 months]
    Q --> R{Stable?}
    R -->|Yes| S[Begin V12 planning]
    R -->|No| T[Emergency patches]
    T --> Q
    
    J --> U[Standard maintenance]
    S --> V[V12: Sharding]
    V --> W[Hard fork]
    W --> X[V12 activation]
    X --> Y[Measure scaling]
    Y --> Z{1000 TPS?}
    Z -->|Yes| AA[Begin V13]
    Z -->|No| AB[V12 patch]
    AB --> Y
    
    AA --> AC[V13: Full shard]
    AC --> AD[Hard fork required]
    AD --> AE[V13 activation]
    AE --> AF[Infinite scaling]
```

**Fuego Specific:**
```
V10 (Current - Active 2024):
- DMWDA: 480s blocks
- Deposit: 3 month terms
- Burn: 0.8/800 XFG amounts
- TPS: ~2-4 (steady)

V11 (Evaluation - Potential 2024):
- Target: 10 TPS
- Methods: Optimized validation, async
- Fork: May be soft (protocol only)
