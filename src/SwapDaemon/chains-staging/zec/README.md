# Zcash (ZEC) Integration Plan

## Overview

Zcash is a privacy-focused UTXO cryptocurrency (~$1.5B market cap).
It uses the same Bitcoin script model for transparent addresses (t-addrs)
and adds shielded addresses (z-addrs) using zero-knowledge proofs (zk-SNARKs).
For atomic swaps, only t-addrs are used (same as BTC), making integration
straightforward.

## Technical Fit

- **Model**: UTXO (same as BTC, LTC, BCH, DCR, KMD)
- **HTLC Script**: Identical to BTC — `OP_SHA256` hashlock + `OP_CHECKLOCKTIMEVERIFY` refund
- **Address Prefixes (t-addr)**: P2PKH=0x1C, P2SH=0x3C, WIF=0x80
- **Block Time**: ~2.5 minutes
- **RPC Port**: 8232
- **SPV Support**: Compatible with ElectrumSpvClient (same protocol as BTC)

## Implementation Approach

**Template**: Copy `Bitcoin/` directory and adapt network parameters.

### Files to Create

| File | Source Template | Notes |
|------|----------------|-------|
| `ZecChainClient.h` | `BtcChainClient.h` | Change class name, chain name string |
| `ZecChainClient.cpp` | `BtcChainClient.cpp` | Change address version bytes |
| `ZecHtlcScript.h` | `BtcHtlcScript.h` | Identical logic (same script model) |
| `ZecHtlcScript.cpp` | `BtcHtlcScript.cpp` | Change P2PKH/P2SH/WIF prefixes |
| `ZecRpcClient.h` | `BtcRpcClient.h` | Change class name, RPC port |
| `ZecRpcClient.cpp` | `BtcRpcClient.cpp` | Change RPC port (8232) |
| `tests/` | `BtcChainClient/tests/` | Adapt test parameters |

### Key Differences from Bitcoin

| Parameter | Bitcoin | Zcash (t-addr) |
|-----------|---------|----------------|
| P2PKH prefix | 0x00 | 0x1C |
| P2SH prefix | 0x05 | 0x3C |
| WIF prefix | 0x80 | 0x80 |
| RPC port | 8332 | 8232 |
| Block time | 10 min | 2.5 min |
| Coin decimals | 8 | 8 |
| Address format | bc1.../3... | t1.../3... |
| Shielded (z-addr) | N/A | z... (out of scope for v1) |

## Integration Points

### SwapTypes.h
```cpp
ZEC = 12,  // after POLYGON = 11
```

### SwapTimelock.cpp
```cpp
case SwapPair::ZEC: return 150000;  // 2.5 min per block
```

### AdaptorSwap.cpp
```cpp
case SwapPair::ZEC:
case SwapPair::BTC:
case SwapPair::BCH:
case SwapPair::LTC:
case SwapPair::DCR:
case SwapPair::KMD_SPV: {
    // SHA-256 hashlock (UTXO pattern)
}
```

### PriceOracle.cpp
```cpp
case SwapPair::ZEC: return SEED_ZEC_USD / xfgUsd;
// Add seed rate: static const double SEED_ZEC_USD = 25.0;
```

### SwapDaemon.cpp
```cpp
// Register ZEC chain client (RPC + SPV modes)
```

### SwapDaemon.h (ChainClientConfig)
```cpp
// Zcash
std::string zecHost;
uint16_t   zecPort     = 8232;
std::string zecRpcUser;
std::string zecRpcPass;
std::string zecWif;

std::string zecMode;
std::vector<std::string> zecSpvServers;
size_t   zecSpvMinServers  = 1;
uint64_t zecSpvCheckpointHeight = 0;
std::string zecSpvCheckpointHash;
```

### swapxfg.html
```html
<option value="ZEC">Zcash (ZEC)</option>
```

### swapxfg.js CHAIN_INFO
```javascript
ZEC: { icon: '/coin-icons/zec.png', color: '#8cb53e', ticker: 'ZEC', name: 'Zcash' }
```

## Icon Required

- `dashboard/static/coin-icons/zec.png` — needs to be added

## Estimated Effort

- **Implementation**: 3-4 days (copy BTC template, change constants, test z-addr exclusion)
- **Testing**: 2 days (unit tests, integration tests, t-addr HTLC verification)
- **Total**: 5-6 days

## Dependencies

- `zcashd` daemon running with RPC enabled
- Electrum server for SPV mode (optional)
- z-address support is out of scope for v1 (t-addr only)