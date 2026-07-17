# DIGM Implementation Plan

## Current State Summary

| Subsystem | Status | Notes |
|-----------|--------|-------|
| `Blockchain.h/cpp` | ✅ Committed | Pool logic, swap/sell, bootstrap, supply tracking |
| `Core.h/cpp` | ⚡ Written, uncommitted | `DigmPoolInfo`, `executeDigmSwap`, `executeDigmSell` |
| `DigmMintEngine.h/cpp` | ✅ Committed | Mint validation logic |
| RPC commands/CoreRpcServerCommandsDefinitions.h | ❌ Missing | No `COMMAND_RPC_DIGM_*` structs |
| RPC handlers/RpcServer.h/cpp | ❌ Missing | No `on_digm_*` endpoints |
| SimpleWallet.h/cpp | ❌ Missing | No `digm_buy/sell/info` commands |
| Build | ❌ Broken | `OrderbookMatcher.cpp` + `Currency.cpp` errors |

---

## Phase A: Finish DIGM Pool Code (infrastructure)

### A1 — RPC Command Definitions
**File:** `src/Rpc/CoreRpcServerCommandsDefinitions.h`
- Add `COMMAND_RPC_DIGM_POOL_INFO` (request/response: reserves, peg)
- Add `COMMAND_RPC_DIGM_SWAP` (request: heatIn, expectedDigmOut; response: success, digmOut, fee)
- Add `COMMAND_RPC_DIGM_SWAP_SELL` (request: digmIn, expectedHeatOut; response: success, heatOut, fee)

### A2 — RPC Handlers
**File:** `src/Rpc/RpcServer.h`
- Declare `on_digm_pool_info`, `on_digm_swap`, `on_digm_swap_sell`

**File:** `src/Rpc/RpcServer.cpp`
- Wire handlers into `processJsonRpcRequest()` switch
- Implement each handler calling through `m_core`

### A3 — SimpleWallet CLI
**File:** `src/SimpleWallet.h`
- Declare `digm_info()`, `digm_buy()`, `digm_sell()`

**File:** `src/SimpleWallet.cpp`
- Add `DIGM` menu category
- `digm_info`: call RPC, display pool reserves
- `digm_buy`: prompt HEAT amount, call RPC swap, confirm
- `digm_sell`: prompt DIGM amount (atomic with human-readable helper), call RPC sell

### A4 — Fix Build
- **A4a:** `OrderbookMatcher.cpp` — resolve `PublicKey` comparison operator (added in new Hearthbook code)
- **A4b:** `Currency.cpp` — define missing `TESTNET_BOOTSTRAP_BLOCKS` / `BOOTSTRAP_BLOCKS` constants
- **A4c:** `NodeRpcProxy` — uncomment `getOutputsHeights` or decide whether to remove it
- **A4d:** Run CMake + `make -j$(nproc)` to verify clean compile
- **A4e:** Commit Phase A with a clean build

**Estimated effort:** 4–6 edits across 5 files, then build verification.

---

## Phase B: DIGM CD Real Yield (strategy implementation)

### B0 — Prerequisites
- **B0a:** Fix build (A4 above) — everything depends on a working binary
- **B0b:** Write comprehensive L1 unit tests for DIGM pool (swap, sell, edge cases, pool exhaustion, fee accrual)
- **B0c:** Clarify & implement DIGM burn mechanism — how does CD redemption work? Burn DIGM → return HEAT? Need on-chain burn RPC/transaction type

### B1 — Fuego L1 Fee Collection
- Route accumulated swap fees (`accumulatedLpFees`) into the HEAT CD fee pool for CD holders
- Add RPC endpoint to query DIGM fee pool balance
- Implement per-block or per-epoch fee distribution to active CDs (weighted by deposit amount × term)

### B2 — DIGM Music Platform Integration
- Album sales → DIGM-denominated royalties → platform collects fee → fee pool
- Subscription revenue share (15–20% of gross to CD holders, 80% to artist)
- Artist service fees (minting, distribution, analytics) → fee pool
- Advertisement revenue share → fee pool

### B3 — Cross-Chain DeFi (future)
- STARK bridge settlement fees (0.1% on bridging DIGM to Base) → fee pool
- DIGM lending and borrowing interest spread (overcollateralized loans) → fee pool
- POL (Protocol-Owned Liquidity) swap fee sharing → fee pool

---

## Milestones

| Milestone | Components | Builds? | Tested? |
|-----------|------------|---------|---------|
| M1: Pool complete | A1–A4 | ✅ | Manual |
| M2: Clean build | A4 | ✅ | ✅ `make` |
| M3: L1 validation | B0b | ✅ | ✅ Unit test pass |
| M4: Fee distribution live | B1 | ✅ | ✅ Integration test pass |
| M5: Platform integrated | B2 | ✅ | ✅ Staging test pass |
