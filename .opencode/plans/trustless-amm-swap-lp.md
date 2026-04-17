# Trustless AMM Swap LP - Implementation Plan

## Overview

Add LP pool trading as an **additional option** alongside existing P2P atomic swaps in the Fuego DEX. LP providers deposit both assets of a swap pair into a pool governed by constant product (x*y=k) AMM formula. Swaps execute against pool reserves with fees accrued proportionally to LP share holders.

Pool state is tracked via **merkle checkpoint attestations** (mirroring fuego-prover patterns) so no trust in the pool organizer is required.
Furthermore, we are exploring the feasibility of pederson-type commitment/proofs which LP providers can generate in order to obscure all pool data from pool organizer (similar to the model of a rollup's sequencer) while proving legitimacy (also) using Fuego Prover, for zero-knowledge privacy in all LP swap pool trades, instead of plaintext.

## Architecture

```
SwapDaemon (extended)
├── Atomic Swap (existing, untouched)
│   └── P2P ↔ P2P direct swaps via adaptor signatures
│
├── Pool Organizer (NEW)
│   ├── LP share registry (merkle tree)
│   ├── Fee accumulator (merkle tree)
│   ├── Pool orderbook
│   └── Checkpoint attestations
│
└── Price Oracle (extended)
    ├── TWAP from atomic swaps (existing)
    └── Pool spot prices (NEW)
```

## Files to Create

1. `src/SwapDaemon/PoolTypes.h` - Pool data structures (PoolId, PoolState, LPShare, PoolCheckpoint, etc.)
2. `src/SwapDaemon/PoolTypes.cpp` - String conversions, serialization helpers
3. `src/SwapDaemon/PoolOrganizer.h` - Pool organizer class declaration
4. `src/SwapDaemon/PoolOrganizer.cpp` - Pool state management, LP share registry, fee distribution
5. `src/SwapDaemon/PoolAMM.h` - Constant product math declarations
6. `src/SwapDaemon/PoolAMM.cpp` - AMM calculations (swap output, LP shares, fees)
7. `src/SwapDaemon/PoolAttestation.h` - Merkle checkpoint generation declarations
8. `src/SwapDaemon/PoolAttestation.cpp` - Checkpoint proof generation (mirrors fuego-prover)
9. `src/SwapDaemon/PoolP2P.h` - Pool P2P message types
10. `src/SwapDaemon/PoolP2P.cpp` - Pool P2P message handling
11. `src/SwapDaemon/PoolDatabase.h` - Pool persistence declarations
12. `src/SwapDaemon/PoolDatabase.cpp` - Pool state persistence (JSON files)
13. `fuego-pool-circuit/` - Rust workspace for ZK pool proofs (Phase 5)

## Files to Modify

1. `src/SwapDaemon/SwapTypes.h` - Add pool state enums (20-43)
2. `src/SwapDaemon/SwapStateMachine.h/.cpp` - Add pool state transitions
3. `src/SwapDaemon/SwapDatabase.h/.cpp` - Add pool persistence integration
4. `src/SwapDaemon/SwapP2P.h/.cpp` - Add pool message types
5. `src/SwapDaemon/PriceOracle.h/.cpp` - Add pool price sources
6. `src/SwapDaemon/SwapDaemon.h/.cpp` - Integrate PoolOrganizer
7. `src/SwapDaemon/CMakeLists.txt` - Add new source files

## Implementation Phases

### Phase 1: Core Types (PoolTypes.h/.cpp)
- PoolId, PoolState, LPShare, PoolSwapOrder, PoolEvent, PoolCheckpoint
- PoolFeeRecord, LPDepositParams, LPWithdrawalParams
- String conversions, serialization helpers

### Phase 2: AMM Math (PoolAMM.h/.cpp)
- Constant product swap output calculation
- LP share minting/burning
- Fee calculation and distribution
- Slippage protection

### Phase 3: Pool Organizer (PoolOrganizer.h/.cpp)
- Pool state management
- LP share registry (merkle tree)
- Fee accumulator
- Event processing (deposit, withdrawal, swap)
- Checkpoint generation

### Phase 4: Attestations (PoolAttestation.h/.cpp)
- Merkle tree for LP shares
- Merkle tree for fee records
- Checkpoint hash computation
- Merkle proof verification

### Phase 5: P2P Messaging (PoolP2P.h/.cpp)
- Pool message types (deposit, withdrawal, swap, checkpoint)
- Message serialization/deserialization
- Integration with existing SwapP2P

### Phase 6: Persistence (PoolDatabase.h/.cpp)
- Pool state persistence (JSON files)
- LP share persistence
- Event history persistence

### Phase 7: Integration
- Add pool state enums to SwapTypes.h
- Add pool state transitions to SwapStateMachine
- Integrate PoolOrganizer into SwapDaemon
- Add pool price sources to PriceOracle

### Phase 8: ZK Circuit (fuego-pool-circuit/)
- Rust workspace for ZK pool proofs
- SP1 zkVM program for pool state verification
- Mirrors fuego-prover architecture

## Pool State Machine

```
POOL_DEPOSIT_INITIATED  = 20   // LP provider requests deposit
POOL_DEPOSIT_LOCKED_A   = 21   // Asset A locked in escrow
POOL_DEPOSIT_LOCKED_B   = 22   // Asset B locked in escrow
POOL_DEPOSIT_CONFIRMED  = 23   // Both locked, LP shares minted
POOL_DEPOSIT_COMPLETE   = 24   // Escrow spent, reserves updated
POOL_DEPOSIT_REFUNDED   = 25   // Deposit cancelled, assets returned

POOL_WITHDRAW_INITIATED = 30   // LP provider requests withdrawal
POOL_WITHDRAW_LOCKED    = 31   // Shares nullified, assets prepared
POOL_WITHDRAW_COMPLETE  = 32   // Assets returned to LP provider
POOL_WITHDRAW_REFUNDED  = 33   // Withdrawal cancelled

POOL_SWAP_INITIATED     = 40   // Swap order submitted
POOL_SWAP_EXECUTED      = 41   // Swap processed, reserves updated
POOL_SWAP_COMPLETE      = 42   // Atomic swap finished
POOL_SWAP_REFUNDED      = 43   // Swap cancelled
```

## Key Design Decisions

- **AMM Model**: Constant Product (x*y=k) - simple, well-understood
- **Fee Rate**: 0.3% per swap (30 basis points) - industry standard
- **Attestation**: Merkle checkpoint + ZK proof - trustless verification
- **LP Share Format**: Merkle leaf with nullifier - prevents double-spend
- **Trust Model**: ZK-verified state transitions - no trust in organizer needed
- **P2P Protocol**: Extend SwapP2P + adaptor signatures - reuse battle-tested code
