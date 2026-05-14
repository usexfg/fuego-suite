# zkLPswap — Trustless Pool Trading (v11)

This directory contains the constant-product AMM pool implementation for trustless
pool-based trading. This code is **deferred to v11** and is not compiled into the
active SwapDaemon build.

## What's here

- `PoolTypes.*` — shared pool data types: `PoolState`, `LpShare`, `PoolParams`, fee records
- `PoolAMM.*` — constant-product AMM math (x·y=k), LP share accounting, output/input calculation
- `PoolAttestation.*` — merkle checkpoint proofs binding pool state to block height (trustless verification)
- `PoolOrganizer.*` — pool lifecycle management: deposits, withdrawals, fee accumulation, order execution
- `PoolDatabase.*` — pool state persistence (JSON files per pool + LP share registry)
- `PoolP2P.*` — P2P message types for pool operations, extending the SwapP2P message system

## Status

Not yet integrated. Requires:

- ZK proof of deposit ratio correctness
- Trustless LP fee distribution
- Integration with CommitmentIndex for on-chain accounting
- Complete test coverage for AMM math edge cases

## POOL_* states in SwapStateMachine

The `POOL_*` enum values in `SwapTypes.h` and the transition table in
`SwapStateMachine.cpp` are kept but annotated as v11-deferred. They are not
reachable from any active swap path. See those files for the comment blocks.

## Reintegration

To enable for v11:

1. Add `pool_v11/` sources back to `SwapDaemonLib` and `SwapDaemon` targets in
   `src/CMakeLists.txt` (entries are commented out with `# v11: pool trading`).
2. Re-enable POOL_* states in `SwapStateMachine.cpp` (already present, just unreachable).
3. Wire `PoolOrganizer` into `SwapDaemon.cpp`.
4. Add pool UI views to `swapxfg/` TUI.
