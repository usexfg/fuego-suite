# CLV Parachain (Robinhood Chain) Integration Plan

## Overview
CLV Parachain is a Substrate-based Polkadot parachain with an EVM module.
Chain ID 1024 (EVM), SS58 prefix 73.

## Chain Parameters
- **EVM Chain ID**: 1024
- **SS58 Prefix**: 73
- **RPC Port**: 8545 (EVM module)
- **Block Time**: ~6s (Polkadot)
- **Native Token**: CLV
- **Decimals**: 18

## Implementation
**Header**: `src/SwapDaemon/CLV/CLVChainClient.h` — ✅ Created
Pattern: Header-only inheritance from `EthChainClient` (EVM module)