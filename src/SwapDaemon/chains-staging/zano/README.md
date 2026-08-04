# Zano (ZANO) Integration Plan

## Overview

Zano is a privacy-focused UTXO cryptocurrency built on the CryptoNote protocol
(same base as Monero/XMR). It features optional shielded transactions with
ring signatures and stealth addresses. Zano has a smaller market cap but a
dedicated community focused on privacy and decentralization.

## Technical Fit

- **Model**: UTXO with CryptoNote protocol (same base as XMR)
- **HTLC Script**: CryptoNote-specific (different from Bitcoin UTXO scripts)
- **Address Format**: Zano address (starts with "za")
- **Block Time**: ~2 minutes
- **RPC Port**: 11998
- **SPV Support**: Requires custom SPV implementation (CryptoNote protocol differs from Bitcoin SPV)

## Implementation Approach

**Template**: Copy `Monero/` directory and adapt network parameters.
Zano uses the same CryptoNote protocol as Monero, so the core cryptographic
primitives (ring signatures, stealth addresses, Pedersen commitments) are shared.

### Files to Create

| File | Source Template | Notes |
|------|----------------|-------|
| `ZanoChainClient.h` | `Monero/MoneroAddress.h` (adapted) | CryptoNote-based, new address format |
| `ZanoChainClient.cpp` | `Monero/AdaptorSignature.cpp` (adapted) | CryptoNote-specific HTLC |
| `ZanoRpcClient.h` | New file | Zano-specific RPC client |
| `ZanoRpcClient.cpp` | New file | Zano JSON-RPC implementation |
| `ZanoHtlcScript.h` | New file | CryptoNote HTLC script |
| `ZanoHtlcScript.cpp` | New file | CryptoNote HTLC script logic |
| `tests/` | `Monero/tests/` (adapted) | Adapt test parameters |

### Key Differences from Monero

| Parameter | Monero | Zano |
|-----------|--------|------|
| Address prefix | 18 (0x12) | 13 (0x0D) |
| Block time | 2 min | 2 min |
| RPC port | 18081 | 11998 |
| Coin decimals | 12 | 12 |
| Address format | 40-char base58 | 95-char base58 (za...) |
| Ring size | 16 | 16 (default) |
| Bulletproofs | Yes | Yes |

## Integration Points

### SwapTypes.h
```cpp
ZANO = 12,  // after POLYGON = 11
```

### SwapTimelock.cpp
```cpp
case SwapPair::ZANO: return 120000;  // 2 min per block
```

### AdaptorSwap.cpp
```cpp
case SwapPair::ZANO:
case SwapPair::XMR: {
    // CryptoNote hashlock (Keccak-based, different from SHA-256)
}
```

### PriceOracle.cpp
```cpp
case SwapPair::ZANO: return SEED_ZANO_USD / xfgUsd;
// Add seed rate: static const double SEED_ZANO_USD = 0.85;
```

### SwapDaemon.cpp
```cpp
// Register ZANO chain client (RPC mode)
```

### SwapDaemon.h (ChainClientConfig)
```cpp
// Zano
std::string zanoHost;
uint16_t    zanoPort     = 11998;
std::string zanoRpcUser;
std::string zanoRpcPass;
std::string zanoViewKey;   // CryptoNote view key for scanning
std::string zanoSpendKey;  // CryptoNote spend key for signing
```

### swapxfg.html
```html
<option value="ZANO">Zano (ZANO)</option>
```

### swapxfg.js CHAIN_INFO
```javascript
ZANO: { icon: '/coin-icons/zano.png', color: '#4a90d9', ticker: 'ZANO', name: 'Zano' }
```

## Icon Required

- `dashboard/static/coin-icons/zano.png` — needs to be added

## Estimated Effort

- **Implementation**: 5-7 days (CryptoNote protocol requires custom HTLC logic)
- **Testing**: 3-4 days (CryptoNote-specific testing, ring signature verification)
- **Total**: 8-11 days

## Dependencies

- `zano-wallet-rpc` daemon running with RPC enabled
- CryptoNote protocol knowledge (shared with Monero implementation)
- Custom SPV implementation or full-node RPC required
- Zano-specific HTLC contract design needed (different from Bitcoin UTXO scripts)