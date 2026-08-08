# Polkadot Relay Chain Integration Plan

## Overview
Polkadot relay chain uses Substrate consensus (NPoS), no native EVM module.
Requires full Substrate/Scale client.

## Chain Parameters
- **SS58 Prefix**: 0
- **Block Time**: ~6s
- **Consensus**: Nominated Proof of Stake (NPoS)
- **Native Token**: DOT
- **Decimals**: 10

## Implementation
- `src/SwapDaemon/Polkadot/PolkadotChainClient.h` — ✅ Created (IChainClient)
- `src/SwapDaemon/Polkadot/SubstrateRpcClient.h` — ✅ Created (SCALE RPC)
- `src/SwapDaemon/Polkadot/PolkadotChainClient.cpp` — ❌ TODO (full implementation)
- `src/SwapDaemon/Polkadot/Ss58Address.h/.cpp` — ❌ TODO
- `src/SwapDaemon/Polkadot/ScaleCodec.h/.cpp` — ❌ TODO

## Approach
Full Substrate client with SCALE encoding, XCM messaging, and pallet-hashTimeLock.