# Dogecoin (DOGE) Integration Plan

## Overview

Dogecoin is a UTXO-based cryptocurrency and top-10 by market cap (~$35B).
It is a fork of Litecoin with the same script semantics, making it the
lowest-effort chain to integrate into Fuego SwapXFG.

## Technical Fit

- **Model**: UTXO (same as BTC, LTC, BCH, DCR, KMD)
- **HTLC Script**: Identical to LTC — `OP_SHA256` hashlock + `OP_CHECKLOCKTIMEVERIFY` refund
- **Address Prefixes**: P2PKH=0x1E, P2SH=0x16, WIF=0x9E
- **Block Time**: ~1 minute (60 seconds)
- **RPC Port**: 22556
- **SPV Support**: Compatible with ElectrumSpvClient (same protocol as LTC)

## Implementation Approach

**Template**: Copy `Litecoin/` directory and adapt network parameters.

### Files to Create

| File | Source Template | Notes |
|------|----------------|-------|
| `DogeChainClient.h` | `LtcChainClient.h` | Change class name, chain name string |
| `DogeChainClient.cpp` | `LtcChainClient.cpp` | Change address version bytes |
| `DogeHtlcScript.h` | `LtcHtlcScript.h` | Identical logic, different constants |
| `DogeHtlcScript.cpp` | `LtcHtlcScript.cpp` | Change P2PKH/P2SH/WIF prefixes |
| `DogeRpcClient.h` | `LtcRpcClient.h` | Change class name, RPC port |
| `DogeRpcClient.cpp` | `LtcRpcClient.cpp` | Change RPC port (22556) |
| `tests/` | `LtcChainClient/tests/` | Adapt test parameters |

### Key Differences from Litecoin

| Parameter | Litecoin | Dogecoin |
|-----------|----------|----------|
| P2PKH prefix | 0x30 | 0x1E |
| P2SH prefix | 0x32 | 0x16 |
| WIF prefix | 0xB0 | 0x9E |
| RPC port | 9332 | 22556 |
| Block time | 2.5 min | 1 min |
| Coin decimals | 8 | 8 |
| Address format | LTC... | D... |

## Integration Points

### SwapTypes.h
```cpp
DOGE = 12,  // after POLYGON = 11
```

### SwapTimelock.cpp
```cpp
case SwapPair::DOGE: return 60000;  // 1 minute per block
```

### AdaptorSwap.cpp
```cpp
case SwapPair::DOGE:
case SwapPair::LTC:
case SwapPair::BCH:
case SwapPair::DCR:
case SwapPair::KMD_SPV: {
    // SHA-256 hashlock (UTXO pattern)
}
```

### PriceOracle.cpp
```cpp
case SwapPair::DOGE: return SEED_DOGE_USD / xfgUsd;
```

### SwapDaemon.cpp
```cpp
// Register DOGE chain client (RPC + SPV modes)
```

### SwapDaemon.h (ChainClientConfig)
```cpp
// Dogecoin
std::string dogeHost;
uint16_t    dogePort     = 22556;
std::string dogeRpcUser;
std::string dogeRpcPass;
std::string dogeWif;

std::string dogeMode;
std::vector<std::string> dogeSpvServers;
size_t    dogeSpvMinServers  = 1;
uint64_t  dogeSpvCheckpointHeight = 0;
std::string dogeSpvCheckpointHash;
```

### swapxfg.html
```html
<option value="DOGE">Dogecoin (DOGE)</option>
```

### swapxfg.js CHAIN_INFO
```javascript
DOGE: { icon: '/coin-icons/doge.png', color: '#c2a633', ticker: 'DOGE', name: 'Dogecoin' }
```

## Icon Required

- `dashboard/static/coin-icons/doge.png` — needs to be added

## Estimated Effort

- **Implementation**: 2-3 days (copy LTC template, change constants)
- **Testing**: 1-2 days (unit tests, integration tests)
- **Total**: 3-5 days

## Dependencies

- None (standalone UTXO chain, no cross-chain dependencies)
- Requires `dogecoin` daemon running with RPC enabled
- Requires Electrum server for SPV mode (optional)