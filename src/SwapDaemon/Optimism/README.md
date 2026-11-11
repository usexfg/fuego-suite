# Optimism Integration Plan

## Overview
Optimism (OP Mainnet) is an EVM L2 built on the OP Stack. Uses ETH, chain ID 10, ~2s block time.

## Chain Parameters
- **Chain ID**: 10
- **RPC Port**: 8545
- **Block Time**: ~2s
- **Native Token**: ETH
- **Decimals**: 18
- **Gas Model**: EIP-1559 (type 2 transactions)

## Implementation
**Header**: `src/SwapDaemon/Optimism/OptimismChainClient.h` — ✅ Created
Pattern: Header-only inheritance from `EthChainClient` (same as Polygon/BSC)

## Integration Steps
1. Add `OPTIMISM = 13` to `SwapTypes.h`
2. Add config fields to `ChainClientConfig` in `SwapDaemon.h`
3. Add registration in `SwapDaemon.cpp`
4. Add seed rate in `PriceOracle.cpp`
5. Add block time in `SwapTimelock.cpp`
6. Add string mapping in `SwapTypes.cpp`
7. Add `<option>` in `swapxfg.html`
8. Add `CHAIN_INFO.OPTIMISM` in `swapxfg.js`
9. Add `dashboard/static/coin-icons/op.png` icon