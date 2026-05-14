Copyright (c) 2025 Fuego Developers

This file is part of Fuego.

Fuego is free software distributed in the hope that it
will be useful, but WITHOUT ANY WARRANTY; without even the
implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
PURPOSE. You can redistribute it and/or modify it under the terms
of the GNU General Public License v3 or later versions as published
by the Free Software Foundation. Fuego includes elements written
by third parties. See file labeled LICENSE for more details.
You should have received a copy of the GNU General Public License
along with Fuego. If not, see <https://www.gnu.org/licenses/>.

# Dynamic Ring Size Implementation

## Overview

Fuego now implements dynamic ring sizing to provide optimal privacy based on available outputs. This system intelligently selects the largest achievable ring size, aiming for maximum privacy while maintaining compatibility.

## How It Works

### Target Ring Sizes (in order of preference)

1. **18** - Maximum Privacy
2. **15** - Strong Privacy  
3. **12** - Better Privacy (Monero's [pre-fcmp] Ring Size)
4. **10** - Good Privacy
5. **8** - Standard Privacy (Minimum for BlockMajorVersion 10)

### Algorithm

1. **Check Block Version**: Only applies to BlockMajorVersion 10+
2. **Target Selection**: Tries ring sizes in descending order (18 → 15 → 12 → 10 → 8)
3. **Availability Check**: Verifies sufficient outputs exist for the target ring size
4. **Minimum Enforcement**: Never goes below ring size 8 for BlockMajorVersion 10+
5. **Optimizer Direction**: If ring size 8 is not achievable, directs user to run wallet optimizer

## Implementation Details

### Core Components

#### `DynamicRingSizeCalculator` Class

- `calculateOptimalRingSize()` - Main calculation function
- `getTargetRingSizes()` - Returns preferred ring sizes
- `isRingSizeAchievable()` - Checks output availability
- `getPrivacyLevelDescription()` - Human-readable privacy level

#### `Currency::calculateOptimalRingSize()` Method

- Integrated into the Currency class
- Takes amount, available outputs, and block version
- Returns optimal ring size for the transaction

### Integration Points

#### WalletTransactionSender

- Modified to use dynamic ring sizing
- Calculates optimal ring size before requesting random outputs
- Falls back gracefully if optimal size unavailable

#### SimpleWallet

- Updated to use dynamic ring sizing
- Provides better privacy for command-line transactions

#### PaymentGateService

- Enhanced RPC service with dynamic ring sizing
- Maintains compatibility with existing API

## Privacy Benefits

### Ring Size vs Privacy Level

- **Ring Size 8**: Enhanced Privacy (minimum)
- **Ring Size 10**: Good Privacy
- **Ring Size 12**: Better Privacy
- **Ring Size 15**: Strong Privacy
- **Ring Size 18**: Maximum Privacy

### Anonymity Set

- Larger ring sizes = larger anonymity set
- More decoy outputs = better transaction privacy
- Dynamic sizing ensures maximum achievable privacy

## Backwards Compatibility

### BlockMajorVersion 10+

- Dynamic ring sizing active
- Minimum ring size: 8
- Maximum ring size: 20 (configurable)

### Older Block Versions

- Static ring size: 2
- No dynamic sizing applied
- Maintains existing behavior

## Configuration

### Currency Parameters

```cpp
// Minimum ring size for V10+
const uint64_t MIN_TX_MIXIN_SIZE_V10 = 8;

// Maximum ring size
const uint64_t MAX_TX_MIXIN_SIZE = 18;

// Upgrade height for enhanced privacy
const uint32_t UPGRADE_HEIGHT_V10 = 980000;
```

### Target Ring Sizes

```cpp
std::vector<size_t> targetRingSizes = {18, 15, 12, 10, 8};
```

## Usage Examples

### Wallet Transaction

```cpp
// Automatically selects optimal ring size
uint64_t optimalRingSize = currency.calculateOptimalRingSize(
    amount, 
    availableOutputs, 
    blockMajorVersion
);
```

### Privacy Level Description

```cpp
std::string description = DynamicRingSizeCalculator::getPrivacyLevelDescription(ringSize);
// Returns: "Maximum Privacy (Ring Size 18)" or "Enhanced Privacy (Ring Size 8)"
```

## Future Enhancements

### Planned Features

1. **Real-time Output Querying**: Query daemon for actual output availability
2. **Amount-based Optimization**: Different ring sizes for different amounts
3. **Network Analysis**: Adapt ring sizes based on network conditions
4. **User Preferences**: Allow users to set privacy preferences

### Advanced Implementation

```cpp
// Future: Real-time output availability checking
size_t calculateOptimalRingSizeWithQuery(
    uint64_t amount,
    const std::vector<uint64_t>& amounts,
    INode& node
) {
    // Query daemon for available outputs
    // Calculate optimal ring size based on real data
    // Return best achievable ring size
}
```

## Error Handling

### Insufficient Outputs

When there are insufficient outputs to achieve ring size 8 (minimum for BlockMajorVersion 10+):

**Error Code**: `INSUFFICIENT_OUTPUTS_FOR_RING_SIZE`
**Error Message**: "Insufficient outputs for transaction amount. Please run wallet optimizer to consolidate outputs."

**User Action Required**:

1. Run wallet optimizer to consolidate outputs
2. Wait for optimization to complete
3. Retry transaction

### Implementation Details

- System checks available outputs before attempting transaction
- Conservative heuristic: requires multiple unique output amounts
- Never compromises on minimum ring size 8 for enhanced privacy
- Graceful error handling with clear user guidance

## Security Considerations

### Privacy Guarantees

- Minimum ring size ensures basic privacy
- Dynamic sizing maximizes privacy when possible
- Never compromises on minimum ring size 8
- Clear error handling prevents privacy degradation

### Performance Impact

- Minimal overhead in ring size calculation
- No impact on transaction validation
- Slight increase in transaction size for larger ring sizes
- Error checking adds minimal computational overhead

## Conclusion

Dynamic ring sizing provides Fuego with adaptive privacy that automatically optimizes for the best possible anonymity set while maintaining compatibility and performance. This implementation represents a significant privacy enhancement over static ring sizes.
