# Avalanche (AVAX) Chain Integration

## Status: Ready for Integration

### Chain Parameters
| Parameter | Value |
|-----------|-------|
| Chain ID | 43114 |
| RPC Port | 8545 |
| Block Time | ~2s |
| Native Token | AVAX |
| Decimals | 18 |
| Chain Type | EVM (Avalanche C-Chain) |
| RPC URL | https://api.avax.network/ext/bc/C/rpc |

### Files
- `src/SwapDaemon/Avalanche/AvalancheChainClient.h` — ✅ Created

### Implementation Steps
1. Add `AVAX = 13` to `SwapTypes.h` (after GLEEC = 12)
2. Add config fields to `ChainClientConfig` in `SwapDaemon.h`
3. Add registration in `SwapDaemon.cpp`
4. Add seed rate `SEED_AVAX_USD = 35.0` in `PriceOracle.cpp`
5. Add `case SwapPair::AVAX` in `PriceOracle.cpp` (seed rate + ctrDivisor)
6. Add block time in `SwapTimelock.cpp`
7. Add string mapping in `SwapTypes.cpp`
8. Add `<option>` in `swapxfg.html`
9. Add `CHAIN_INFO.AVAX` in `swapxfg.js`
10. Add `dashboard/static/coin-icons/avax.png` icon