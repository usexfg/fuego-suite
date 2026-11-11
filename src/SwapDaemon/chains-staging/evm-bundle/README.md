# EVM Bundle Integration Plan

This directory tracks EVM-compatible chains not yet in SwapPair, ready for integration.

## Chains in this Bundle

| Tier | Chain | Chain ID | Status | Notes |
|------|-------|----------|--------|-------|
| **Tier 1** | **Cronos** | 25 | Active | Cosmos SDK + EVM |
| **Tier 2** | **Sanko** | 18439 | Live | Gaming-focused, Dynamic Sharding |
| **Tier 3** | **Polygon** | 137 | Active | ❌ ALREADY IMPLEMENTED |

## Implementation Status

- ✅ **GLEEC** — Header created at `src/SwapDaemon/Gleec/GleecChainClient.h`
- ✅ **AVAX** — Header created in staging (needs `src/SwapDaemon/Avalanche/`)
- ✅ **CRONOS** — Header created at `src/SwapDaemon/Cronos/CronosChainClient.h`
- ✅ **SANKO** — Header created at `src/SwapDaemon/Sanko/SankoChainClient.h`
- ❌ **POLYGON** — Already in `src/SwapDaemon/Polygon/PolygonChainClient.h`

## Implementation Steps

For each chain:
1. ✅ Create `<Chain>ChainClient.h` (header-only inheritance from `EthChainClient`)
2. Add `SwapPair::<CHAIN>` enum value in `SwapTypes.h`
3. Add config fields to `ChainClientConfig` in `SwapDaemon.h`
4. Add `registerChain` call in `SwapDaemon.cpp`
5. Add seed rate `SEED_<CHAIN>_USD` in `PriceOracle.cpp`
6. Add `case SwapPair::<CHAIN>` in `PriceOracle.cpp` (2 switch statements)
7. Add block time in `SwapTimelock.cpp`
8. Add `<CHAIN>` case in `AdaptorSwap.cpp` (if needed for hashlock)
9. Add string mapping in `SwapTypes.cpp` (`swapPairToString`)
10. Add `<option>` in `swapxfg.html`
11. Add `CHAIN_INFO` entry in `swapxfg.js`
12. Add coin icon in `dashboard/static/coin-icons/`

## Chain Parameters

| Chain | Chain ID | RPC Port | Block Time | HTLC Hash | Type |
|-------|----------|----------|------------|-----------|------|
| GLEEC | 11169 | 8545 | ~5s (Evmos) | keccak256 | EVM (Evmos fork) |
| AVAX | 43114 | 8545 | ~2s | keccak256 | EVM |
| CRONOS | 25 | 8545 | ~6-7s | keccak256 | EVM (Cosmos SDK) |
| SANKO | 18439 | 8545 | ~1s | keccak256 | EVM (custom) |

All use keccak256 hashlock (EVM pattern) — no changes needed in `AdaptorSwap.cpp`.