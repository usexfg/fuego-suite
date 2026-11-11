# Cronos Integration Plan

## Overview
Cronos is an EVM-compatible L1 built by Crypto.com using the Cosmos SDK + Ethermint.
Chain ID 25, ~6-7s block time. Has both EVM and Cosmos IBC.

## Chain Parameters
- **Chain ID**: 25
- **RPC Port**: 8545
- **Block Time**: ~6-7s
- **Native Token**: CRO
- **Decimals**: 18
- **RPC URL**: https://evm.cronos.org

## Implementation
**Header**: `src/SwapDaemon/Cronos/CronosChainClient.h` — ✅ Created
Pattern: Header-only inheritance from `EthChainClient` (same as Polygon/BSC)