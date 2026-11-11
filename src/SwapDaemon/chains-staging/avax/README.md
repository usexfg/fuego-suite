# Avalanche (AVAX) Integration Plan

## Overview

Avalanche is an EVM-compatible Layer 1 blockchain with high throughput
(~4,500 TPS) and a growing DeFi ecosystem (~$8B market cap).
As an EVM chain, it follows the same header-only inheritance pattern
as Polygon, BNB, Arbitrum, and Base.

## Technical Fit

- **Model**: EVM account-based (same as ETH, BNB, POLYGON, ARB, BASE)
- **HTLC Script**: Solidity-based HashedTimelock contract (same as ETH)
- **Chain ID**: 43114
- **RPC Port**: 8545
- **Consensus**: Avalanche consensus (Snowman++), not PoW
- **Native Token**: AVAX (18 decimals)

## Implementation Approach

**Template**: Single header file inheriting from `EthChainClient` (same as Polygon).

### Files to Create

| File | Source Template | Notes |
|------|----------------|-------|
| `AvalancheChainClient.h` | `PolygonChainClient.h` | Change chain name to "AVAX", chainId to 43114 |

That is it — one file, ~14 lines.

### Key Differences from Polygon

| Parameter | Polygon | Avalanche |
|-----------|---------|-----------|
| Chain ID | 137 | 43114 |
| RPC Port | 8545 | 8545 |
| Chain Name | "POLYGON" | "AVAX" |
| Block Time | ~2s | ~2s |
| Coin Decimals | 18 | 18 |
| Address Format | 0x... | 0x... |

## Integration Points

### SwapTypes.h
```cpp
AVAX = 12,  // after POLYGON = 11
```

### SwapDaemon.h (ChainClientConfig)
```cpp
// Avalanche
std::string avaxHost;
uint16_t    avaxPort     = 8545;
std::string avaxPrivKeyHex;
std::string avaxAddress;
uint64_t    avaxChainId  = 43114;
std::string avaxHtlcBinPath;
```

### SwapDaemon.cpp
```cpp
// Register AVAX chain client (EVM inheritance)
if (!chainCfg.avaxHost.empty()) {
    auto rpc = std::make_unique<EthRpcClient>(
        chainCfg.avaxHost, chainCfg.avaxPort,
        chainCfg.avaxPrivKeyHex, chainCfg.avaxAddress,
        chainCfg.avaxChainId);
    m_chainRegistry.registerChain(SwapPair::AVAX,
        std::make_unique<AvalancheChainClient>(std::move(rpc),
        chainCfg.avaxAddress, "AVAX"));
}
```

### PriceOracle.cpp
```cpp
case SwapPair::AVAX: return SEED_AVAX_USD / xfgUsd;
// Add seed rate: static const double SEED_AVAX_USD = 35.0;
```

### swapxfg.html
```html
<option value="AVAX">Avalanche (AVAX)</option>
```

### swapxfg.js CHAIN_INFO
```javascript
AVAX: { icon: '/coin-icons/avax.png', color: '#e84142', ticker: 'AVAX', name: 'Avalanche' }
```

## Icon Required

- `dashboard/static/coin-icons/avax.png` — needs to be added

## Estimated Effort

- **Implementation**: 1 day (single header file)
- **Testing**: 1 day (unit tests, integration tests)
- **Total**: 2 days

## Dependencies

- None (standalone EVM chain)
- Requires `avalanchego` daemon or RPC endpoint running
- HTLC contract must be deployed on AVAX C-Chain (same Solidity contract as ETH/BSC/Polygon)