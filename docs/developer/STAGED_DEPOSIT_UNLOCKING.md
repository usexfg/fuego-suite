# Staged Yield Deposit Unlocking System

## Overview

The Staged Yield Deposit Unlocking System implements an **optional** progressive unlock mechanism for yield deposits in the Fuego blockchain. Users can choose between traditional full-term unlock (1 transaction fee) or staged unlock (4 transaction fees) at the time of deposit creation. When staged unlock is selected, deposits are unlocked in stages over a 120-day period. Only the principal amount is unlocked in stages, while interest remains locked for the full term.

## Key Features

- **Optional staged unlock** - Users choose at deposit creation time
- **1/5 of deposit unlocks every 18 days** (5 stages)
- **Final 5/5 principal unlocks** (final stage)
- **Total unlock period**: 90 days (5 stages × 18 days)
- **Off-chain interest payments** - Interest paid via CD system at deposit time, on-chain interest always 0
- **Flexible fee structure** - 1x fee for traditional, 5x fee for staged (0.04 XFG total)
- **Automatic processing** at each block height
- **Backward compatibility** with existing deposit system

## Fee Structure

The system offers two unlock options with different fee structures:

### Traditional Unlock (Default)
- **Transaction Fee**: 0.008 XFG (1 transaction)
- **Total Fees**: 0.008 XFG
- **Unlock Method**: Full amount unlocks at term end
- **Use Case**: Standard deposits, immediate full access

### Staged Unlock (Optional)
- **Transaction Fee**: 0.008 XFG per stage (5 transactions)
- **Total Fees**: 0.040 XFG (5 × 0.008 XFG)
- **Unlock Method**: 20% every 18 days over 90 days
- **Use Case**: Risk management, gradual access, network stability

## Interest Payment System

- **On-chain Interest**: Always 0 (no supply inflation from block rewards)
- **Off-chain Interest**: Paid via CD (Certificate of Deposit) system at deposit time
- **Staged Unlock**: Only affects principal amount, not interest
- **Interest Locking**: Interest remains locked regardless of unlock method chosen

## Architecture

### Core Components

1. **StagedDepositUnlock** - Manages individual deposit unlock stages
2. **EnhancedDeposit** - Extended deposit structure with staged unlock support
3. **StagedUnlockManager** - Global manager for processing all deposits
4. **RPC API Extensions** - New endpoints for staged unlock information

### Unlock Schedule

```
Stage 1 (Day 30):  25% Principal + 0% Interest
Stage 2 (Day 60):  25% Principal + 0% Interest  
Stage 3 (Day 90):  25% Principal + 0% Interest
Stage 4 (Day 120): 25% Principal + 0% Interest
```

## Implementation Details

### StagedDepositUnlock Class

```cpp
class StagedDepositUnlock {
public:
    // Initialize with deposit details
    StagedDepositUnlock(uint64_t totalAmount, uint64_t totalInterest, uint32_t depositHeight);
    
    // Check for newly unlocked stages
    std::vector<UnlockStage> checkUnlockStages(uint32_t currentHeight);
    
    // Get unlock status
    uint64_t getTotalUnlockedAmount() const;
    uint64_t getRemainingLockedAmount() const;
    bool isFullyUnlocked() const;
    
    // Get next unlock stage
    UnlockStage getNextUnlockStage(uint32_t currentHeight) const;
};
```

### UnlockStage Structure

```cpp
struct UnlockStage {
    uint32_t stageNumber;        // 1, 2, or 3
    uint32_t unlockHeight;       // Block height when stage unlocks
    uint64_t principalAmount;    // Principal amount for this stage
    uint64_t interestAmount;     // Interest amount for this stage
    bool isUnlocked;            // Whether stage has been unlocked
    uint64_t unlockTimestamp;    // When stage was unlocked
};
```

### EnhancedDeposit Structure

```cpp
struct EnhancedDeposit {
    // Basic deposit fields
    uint64_t amount;
    uint32_t term;
    uint64_t interest;
    uint32_t height;
    uint32_t unlockHeight;
    bool locked;
    
    // Staged unlock support
    bool useStagedUnlock;
    StagedDepositUnlock stagedUnlock;
    uint64_t totalUnlockedAmount;
    uint64_t remainingLockedAmount;
};
```

## Configuration

### StagedUnlockConfig

```cpp
namespace StagedUnlockConfig {
    static const uint32_t STAGE_INTERVAL_BLOCKS = 30 * 24 * 60; // 30 days
    static const uint32_t TOTAL_STAGES = 4;
    static const uint32_t STAGE_1_UNLOCK_PERCENT = 25; // 1/4
    static const uint32_t STAGE_2_UNLOCK_PERCENT = 25; // 1/4
    static const uint32_t STAGE_3_UNLOCK_PERCENT = 25; // 1/4
    static const uint32_t STAGE_4_UNLOCK_PERCENT = 25; // 1/4 (remaining)
    static const uint32_t STAGE_1_INTEREST_PERCENT = 0; // No interest unlocks
}
```

## Usage Examples

### Creating a Deposit with Optional Staged Unlock

#### Traditional Deposit (Default)
```bash
curl -X POST http://localhost:8070/createDeposit \
  -H "Content-Type: application/json" \
  -d '{
    "amount": 1000000000000,
    "term": 100000,
    "sourceAddress": "fuego1...",
    "useStagedUnlock": false
  }'
```

**Response:**
```json
{
  "transactionHash": "abc123...",
  "isBurnDeposit": false,
  "useStagedUnlock": false,
  "transactionFee": 800000,
  "totalFees": 800000
}
```

#### Staged Unlock Deposit (Optional)
```bash
curl -X POST http://localhost:8070/createDeposit \
  -H "Content-Type: application/json" \
  -d '{
    "amount": 1000000000000,
    "term": 100000,
    "sourceAddress": "fuego1...",
    "useStagedUnlock": true
  }'
```

**Response:**
```json
{
  "transactionHash": "def456...",
  "isBurnDeposit": false,
  "useStagedUnlock": true,
  "transactionFee": 800000,
  "totalFees": 4000000
}
```

### Querying Deposit Information

```bash
curl -X POST http://localhost:8070/getDeposit \
  -H "Content-Type: application/json" \
  -d '{"depositId": 123}'
```

**Response (Traditional Deposit):**
```json
{
  "amount": 1000000000000,
  "term": 100000,
  "interest": 100000000000,
  "height": 100000,
  "unlockHeight": 200000,
  "locked": true,
  "useStagedUnlock": false,
  "transactionFee": 800000,
  "totalFees": 800000,
  "creatingTransactionHash": "abc123...",
  "spendingTransactionHash": "",
  "address": "fuego1..."
}
```

**Response (Staged Unlock Deposit):**
```json
{
  "amount": 1000000000000,
  "term": 100000,
  "interest": 100000000000,
  "height": 100000,
  "unlockHeight": 200000,
  "locked": true,
  "useStagedUnlock": true,
  "transactionFee": 800000,
  "totalFees": 4000000,
  "creatingTransactionHash": "def456...",
  "spendingTransactionHash": "",
  "address": "fuego1..."
}
```

### Programmatic Usage

```cpp
// Create staged unlock for a deposit
uint64_t amount = 1000000000; // 1000 XFG
uint64_t interest = 100000000; // 100 XFG
uint32_t depositHeight = 1000;

StagedDepositUnlock stagedUnlock(amount, interest, depositHeight);
```

### Processing Unlocks

```cpp
// Process unlocks at current height
uint32_t currentHeight = 1000 + 30 * 24 * 60; // 30 days later
auto newlyUnlocked = stagedUnlock.checkUnlockStages(currentHeight);

for (const auto& stage : newlyUnlocked) {
    std::cout << "Stage " << stage.stageNumber << " unlocked: "
              << stage.principalAmount << " XFG principal" << std::endl;
}
```

### Enhanced Deposit Management

```cpp
// Convert basic deposits to enhanced deposits
std::vector<Deposit> basicDeposits = getDeposits();
auto enhancedDeposits = EnhancedDepositManager::convertDeposits(basicDeposits);

// Process all unlocks
uint32_t currentHeight = getCurrentHeight();
auto newlyUnlocked = EnhancedDepositManager::processAllUnlocks(currentHeight, enhancedDeposits);
```

## RPC API Extensions

### Create Deposit (Updated)

**Request:**
```bash
curl -X POST http://localhost:8070/createDeposit \
  -H "Content-Type: application/json" \
  -d '{
    "amount": 1000000000000,
    "term": 100000,
    "sourceAddress": "fuego1...",
    "useStagedUnlock": true
  }'
```

**Response:**
```json
{
  "transactionHash": "def456...",
  "isBurnDeposit": false,
  "useStagedUnlock": true,
  "transactionFee": 800000,
  "totalFees": 4000000
}
```

### Get Deposit (Updated)

**Request:**
```bash
curl -X POST http://localhost:8070/getDeposit \
  -H "Content-Type: application/json" \
  -d '{"depositId": 123}'
```

**Response:**
```json
{
  "amount": 1000000000000,
  "term": 100000,
  "interest": 100000000000,
  "height": 100000,
  "unlockHeight": 200000,
  "locked": true,
  "useStagedUnlock": true,
  "transactionFee": 800000,
  "totalFees": 4000000,
  "creatingTransactionHash": "def456...",
  "spendingTransactionHash": "",
  "address": "fuego1..."
}
```

### Get Staged Unlock Schedule

```bash
curl -X POST http://localhost:28180/getStagedUnlockSchedule \
  -H "Content-Type: application/json" \
  -d '{"depositId": 123}'
```

**Response:**
```json
{
  "useStagedUnlock": true,
  "stages": [
    {
      "stageNumber": 1,
      "unlockHeight": 143200,
      "principalAmount": 250000000,
      "interestAmount": 0,
      "isUnlocked": true,
      "unlockTimestamp": 1640995200
    },
    {
      "stageNumber": 2,
      "unlockHeight": 186400,
      "principalAmount": 250000000,
      "interestAmount": 0,
      "isUnlocked": false,
      "unlockTimestamp": 0
    },
    {
      "stageNumber": 3,
      "unlockHeight": 229600,
      "principalAmount": 250000000,
      "interestAmount": 0,
      "isUnlocked": false,
      "unlockTimestamp": 0
    },
    {
      "stageNumber": 4,
      "unlockHeight": 272800,
      "principalAmount": 250000000,
      "interestAmount": 0,
      "isUnlocked": false,
      "unlockTimestamp": 0
    }
  ],
  "status": "Stage 2 in 43200 blocks",
  "totalUnlockedAmount": 250000000,
  "remainingLockedAmount": 750000000
}
```

### Get All Staged Deposits

```bash
curl -X POST http://localhost:28180/getStagedDeposits \
  -H "Content-Type: application/json" \
  -d '{}'
```

### Process Staged Unlocks

```bash
curl -X POST http://localhost:28180/processStagedUnlocks \
  -H "Content-Type: application/json" \
  -d '{}'
```

## Integration Points

### Wallet Integration

The staged unlock system integrates with the existing wallet system:

1. **Deposit Creation** - Automatically determines if staged unlocking should be used
2. **Balance Calculation** - Includes unlocked amounts in available balance
3. **Transaction Creation** - Allows spending of unlocked amounts
4. **UI Updates** - Shows unlock progress and next unlock dates

### Blockchain Integration

1. **Block Processing** - Automatically processes unlocks at each block
2. **State Updates** - Updates deposit states and balances
3. **Event Notifications** - Notifies wallets of newly unlocked amounts

## Benefits

### For Users

- **Gradual Principal Access** - Reduces risk of large withdrawals with 4 equal stages
- **Predictable Schedule** - Clear timeline for fund availability over 120 days
- **Flexibility** - Can spend unlocked amounts as they become available
- **Interest Preservation** - Interest remains locked for the full term

### For Network

- **Reduced Volatility** - Gradual unlocks prevent large withdrawals
- **Improved Stability** - Maintains deposit base over time
- **Better Liquidity Management** - Predictable unlock patterns
- **Enhanced Security** - Reduces risk of mass withdrawals

## Migration Strategy

### Backward Compatibility

- **Existing Deposits** - Continue to use traditional unlock mechanism
- **New Deposits** - Automatically use staged unlocking for yield deposits
- **FOREVER Deposits** - Always use traditional unlock (no staging)

### Gradual Rollout

1. **Phase 1** - Deploy staged unlock system alongside existing system
2. **Phase 2** - Enable staged unlocking for new yield deposits
3. **Phase 3** - Monitor and optimize based on usage patterns
4. **Phase 4** - Consider extending to other deposit types

## Testing

### Unit Tests

Comprehensive test suite covering:
- Basic staged unlock functionality
- Stage progression and timing
- Edge cases and error conditions
- Serialization and persistence
- Integration with existing systems

### Integration Tests

- End-to-end deposit creation and unlocking
- RPC API functionality
- Wallet integration
- Performance under load

## Monitoring and Analytics

### Key Metrics

- **Unlock Success Rate** - Percentage of successful unlocks
- **Average Unlock Time** - Time between deposit creation and full unlock
- **User Adoption** - Percentage of users using staged unlocking
- **Network Impact** - Effect on network stability and liquidity

### Logging

- **Unlock Events** - Log all stage unlocks
- **Error Conditions** - Log any unlock failures
- **Performance Metrics** - Track processing times
- **User Actions** - Log user interactions with staged deposits

## Future Enhancements

### Potential Improvements

1. **Customizable Schedules** - Allow users to choose unlock patterns
2. **Partial Withdrawals** - Allow partial withdrawals of unlocked amounts
3. **Accelerated Unlocks** - Options for faster unlocking with fees
4. **Lock Extensions** - Ability to extend lock periods
5. **Multi-Currency Support** - Extend to other supported currencies

### Advanced Features

1. **Smart Contracts** - Programmable unlock conditions
2. **Delegation** - Allow others to manage staged unlocks
3. **Pools** - Combine multiple deposits for better yields
4. **Insurance** - Optional insurance for locked funds

## Conclusion

The Staged Yield Deposit Unlocking System provides a balanced approach to deposit management, offering users immediate access to earned interest while maintaining network stability through gradual principal unlocks. The system is designed for easy integration, backward compatibility, and future extensibility.

For technical implementation details, see the source code in:
- `src/CryptoNoteCore/StagedDepositUnlock.h`
- `src/CryptoNoteCore/StagedDepositUnlock.cpp`
- `src/CryptoNoteCore/EnhancedDeposit.h`
- `src/CryptoNoteCore/EnhancedDeposit.cpp`
- `src/PaymentGate/StagedDepositRpc.h`
- `src/PaymentGate/StagedDepositRpc.cpp`