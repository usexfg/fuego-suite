# Dash (DASH) Integration Plan

## Overview

Dash is a UTXO-based cryptocurrency (~$3B market cap) with a different consensus mechanism (X11 PoW, transitioning to PoS) than Litecoin. However, for atomic swap purposes, the UTXO model, address format (P2PKH/P2SH/WIF), and HTLC script semantics are identical to Litecoin — making the implementation effort comparable. The consensus mechanism does not affect HTLC lock/claim/refund logic.

## Technical Fit

- **Model**: UTXO (same as BTC, LTC, BCH, DCR, KMD)
- **HTLC Script**: Identical to LTC — `OP_SHA256` hashlock + `OP_CHECKLOCKTIMEVERIFY` refund
- **Address Prefixes**: P2PKH=0x4C, P2SH=0x10, WIF=0xcc
- **Block Time**: ~2.5 minutes
- **RPC Port**: 9998
- **SPV Support**: Compatible with ElectrumSpvClient (same protocol as LTC)

## Implementation Approach

**Template**: Copy `Litecoin/` directory and adapt network parameters.

### Files to Create

| File | Source Template | Notes |
|------|----------------|-------|
| `DashChainClient.h` | `LtcChainClient.h` | Change class name, chain name string |
| `DashChainClient.cpp` | `LtcChainClient.cpp` | Change address version bytes |
| `DashHtlcScript.h` | `LtcHtlcScript.h` | Identical logic, different constants |
| `DashHtlcScript.cpp` | `LtcHtlcScript.cpp` | Change P2PKH/P2SH/WIF prefixes |
| `DashRpcClient.h` | `LtcRpcClient.h` | Change class name, RPC port |
| `DashRpcClient.cpp` | `LtcRpcClient.cpp` | Change RPC port (9998) |
| `tests/` | `LtcChainClient/tests/` | Adapt test parameters |

### Key Differences from Litecoin

| Parameter | Litecoin | Dash |
|-----------|----------|------|
| Consensus | Scrypt PoW | X11 PoW (transitioning to PoS) |
| P2PKH prefix | 0x30 | 0x4C |
| P2SH prefix | 0x32 | 0x10 |
| WIF prefix | 0xB0 | 0xCC |
| RPC port | 9332 | 9998 |
| Block time | 2.5 min | 2.5 min |
| Coin decimals | 8 | 8 |
| Address format | LTC... | D... |
| Masternode network | No | Yes (out of scope for swaps) |

## Integration Points

### SwapTypes.h
```cpp
DASH = 12,  // after POLYGON = 11
```

### SwapTimelock.cpp
```cpp
case SwapPair::DASH: return 150000;  // 2.5 min per block
```

### AdaptorSwap.cpp
```cpp
case SwapPair::DASH:
case SwapPair::LTC:
case SwapPair::BCH:
case SwapPair::DCR:
case SwapPair::KMD_SPV: {
    // SHA-256 hashlock (UTXO pattern)
}
```

### PriceOracle.cpp
```cpp
case SwapPair::DASH: return SEED_DASH_USD / xfgUsd;
// Add seed rate: static const double SEED_DASH_USD = 28.0;
```

### SwapDaemon.cpp
```cpp
// Register DASH chain client (RPC + SPV modes)
```

### SwapDaemon.h (ChainClientConfig)
```cpp
// Dash
std::string dashHost;
uint16_t    dashPort     = 9998;
std::string dashRpcUser;
std::string dashRpcPass;
std::string dashWif;

std::string dashMode;
std::vector<std::string> dashSpvServers;
size_t    dashSpvMinServers  = 1;
uint64_t  dashSpvCheckpointHeight = 0;
std::string dashSpvCheckpointHash;
```

### swapxfg.html
```html
<option value="DASH">Dash (DASH)</option>
```

### swapxfg.js CHAIN_INFO
```javascript
DASH: { icon: '/coin-icons/dash.png', color: '#0088cc', ticker: 'DASH', name: 'Dash' }
```

## Icon Required

- `dashboard/static/coin-icons/dash.png` — needs to be added

## Estimated Effort

- **Implementation**: 2-3 days (copy LTC template, change constants)
- **Testing**: 1-2 days (unit tests, integration tests)
- **Total**: 3-5 days

## Dependencies

- `dashd` daemon running with RPC enabled
- Electrum server for SPV mode (optional)
- Masternode network is not required for atomic swaps