# Sia (SC) Integration Plan

## Overview

Sia is a decentralized cloud storage platform with a native cryptocurrency (SC).
It uses a Proof-of-Work consensus (Blake2b) and a unique storage-proof model.
Sia's address format and transaction model differ from standard UTXO chains,
requiring a custom implementation. The `Sia/` directory already exists with
placeholder files (`SiaHtlcScript.cpp`, `SiaRpcClient.cpp`) but both are empty.

## Technical Fit

- **Model**: UTXO-based (Bitcoin-derived) but with unique address format
- **HTLC Script**: Custom Sia script (different from Bitcoin's OP_SHA256)
- **Address Format**: Sia address (starts with "0" or "1", 76 chars)
- **Block Time**: ~15 seconds (fastest of all chains)
- **RPC Port**: 9980
- **Consensus**: Proof-of-Work (Blake2b)
- **Native Token**: SC (10^24 base units, 24 decimals)

## Implementation Approach

**Template**: Copy `Bitcoin/` directory and adapt for Sia's unique parameters.
The existing `Sia/` directory has empty placeholder files that need to be filled.

### Files to Create/Complete

| File | Status | Notes |
|------|--------|-------|
| `SiaChainClient.h` | New (from scratch) | Sia-specific chain client |
| `SiaChainClient.cpp` | New (from scratch) | Sia-specific logic |
| `SiaHtlcScript.h` | Exists (empty) | Fill with Sia HTLC script |
| `SiaHtlcScript.cpp` | Exists (empty) | Fill with Sia HTLC logic |
| `SiaRpcClient.h` | Exists (empty) | Fill with Sia RPC client |
| `SiaRpcClient.cpp` | Exists (empty) | Fill with Sia RPC implementation |
| `tests/` | New | Sia-specific tests |

### Key Differences from Bitcoin

| Parameter | Bitcoin | Sia |
|-----------|---------|-----|
| P2PKH prefix | 0x00 | 0x00 (same) |
| P2SH prefix | 0x05 | 0x05 (same) |
| WIF prefix | 0x80 | 0x80 (same) |
| RPC port | 8332 | 9980 |
| Block time | 10 min | 15 sec |
| Coin decimals | 8 | 24 |
| Address format | bc1.../3... | 0.../1... (76 chars) |
| Hash function | SHA-256d | Blake2b (for consensus) |
| HTLC script | OP_SHA256 | Custom (Sia-specific) |

## Integration Points

### SwapTypes.h
```cpp
SIA = 12,  // after POLYGON = 11
```

### SwapTimelock.cpp
```cpp
case SwapPair::SIA: return 15000;  // 15 seconds per block
```

### AdaptorSwap.cpp
```cpp
case SwapPair::SIA: {
    // Sia-specific hashlock (Blake2b-based, different from SHA-256)
}
```

### PriceOracle.cpp
```cpp
case SwapPair::SIA: return SEED_SIA_USD / xfgUsd;
// Add seed rate: static const double SEED_SIA_USD = 0.008;
```

### SwapDaemon.cpp
```cpp
// Register SIA chain client (RPC mode)
```

### SwapDaemon.h (ChainClientConfig)
```cpp
// Sia
std::string siaHost;
uint16_t    siaPort     = 9980;
std::string siaRpcUser;
std::string siaRpcPass;
std::string siaWif;
```

### swapxfg.html
```html
<option value="SIA">Sia (SC)</option>
```

### swapxfg.js CHAIN_INFO
```javascript
SIA: { icon: '/coin-icons/sc.png', color: '#00b8d4', ticker: 'SC', name: 'Sia' }
```

## Icon Status

- `dashboard/static/coin-icons/sc.png` — ✅ Already exists

## Estimated Effort

- **Implementation**: 5-7 days (Sia has unique address format and HTLC script)
- **Testing**: 3-4 days (Sia testnet deployment, HTLC script verification)
- **Total**: 8-11 days

## Dependencies

- `siac` daemon running with API enabled
- Sia wallet with sufficient SC for transaction fees
- Sia-specific HTLC contract design (different from Bitcoin UTXO scripts)
- Sia RPC API documentation reference