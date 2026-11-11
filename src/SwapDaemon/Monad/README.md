# Monad Integration Plan

## Overview
Monad is a new EVM L1 built on the OP Stack architecture.
Features ~0.5s block times and high throughput.

## Chain Parameters
- **Chain ID**: 185
- **RPC Port**: 8545
- **Block Time**: ~0.5-1s
- **Native Token**: MON
- **Decimals**: 18

## Implementation
**Header**: `src/SwapDaemon/Monad/MonadChainClient.h` — ✅ Created
Pattern: Header-only inheritance from `EthChainClient` (same as Polygon/BSC)

## Integration Steps
1. Add `MONAD = 14` to `SwapTypes.h`
2. Add config fields to `ChainClientConfig` in `SwapDaemon.h`
3. Add registration in `SwapDaemon.cpp`
4. Add seed rate in `PriceOracle.cpp`
5. Add block time in `SwapTimelock.cpp`
6. Add string mapping in `SwapTypes.cpp`
7. Add `<option>` in `swapxfg.html`
8. Add `CHAIN_INFO.MONAD` in `swapxfg.js`
9. Add `dashboard/static/coin-icons/monad.png` icon