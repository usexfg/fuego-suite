# Hearth / HEAT — Security Review & Fix Plan

**Date:** 2026-05-14 | **Audit:** 007 + graphify + fuego-orchestrator | **Score:** 33/100 → Target: 80+

---

## Findings Summary

| # | Severity | Component | Issue | Status |
|---|----------|-----------|-------|--------|
| C1 | CRITICAL | AmmClaim | Withdraws full reserves, doesn't burn LP shares | → FIX |
| C2 | CRITICAL | AMM validation | No block-level validation for Claim/Compound | → FIX |
| C3 | CRITICAL | popBlock | m_heatSupply not reversed on reorg | → FIX |
| C4 | CRITICAL | Rebalancer | Division by zero after TWAP reset | → FIX |
| C5 | CRITICAL | Swap fees | Fees never reach blockchain (SwapDaemon→Blockchain) | Separate plan |
| C6 | CRITICAL | CD yield | Dead code — double epoch processing | Separate plan |
| H1 | HIGH | AmmCompound | Zeroes all LP fees — zero-cost griefing | → FIX |
| H2 | HIGH | popTransaction | Swap reversal uses wrong reserves | → FIX |
| H3 | HIGH | popTransaction | No reversal for AmmClaim/AmmCompound | → FIX |
| H4 | HIGH | BankingIndex | popBlock off-by-one on burned entries | → FIX |
| H5 | HIGH | BankingIndex | popBlocks erases wrong entries | → FIX |
| H6 | HIGH | Core.cpp | Key image dup check missing for commitment spends | → FIX |
| H7 | HIGH | check_outs_valid | Missing amount/term validation | → FIX |
| H8 | HIGH | popBlock | No reversal of TWAP/PI/CD yield/rebalance | → FIX |
| H9 | HIGH | popTransaction | claimerFee not reversed on rollback | → FIX |

---

## Fix Priority

### Block 1: Immediate (security-critical, before any testnet)

| # | Fix | Time |
|---|-----|------|
| C1 | AmmClaim: fee-only withdrawal, no full reserve drain | 15m |
| C2 | Block-level validation for AmmClaim/AmmCompound | 10m |
| C4 | Fix TWAP reset ordering (before rebalancer not after) | 5m |
| H4 | BankingIndex::popBlock off-by-one | 10m |
| H1 | AmmCompound: require LP shares or remove tag | 10m |
| H9 | popTransaction: reverse claimerFee | 10m |

### Block 2: State integrity (reorg safety)

| # | Fix | Time |
|---|-----|------|
| C3/H8 | Store epoch state deltas, reverse in popBlock | 30m |
| H2 | Fix popTransaction swap reversal formula | 10m |
| H3 | Add AmmClaim/AmmCompound reversal in popTransaction | 15m |
| H5 | Fix BankingIndex::popBlocks entry ordering | 10m |

### Block 3: Validation hardening

| # | Fix | Time |
|---|-----|------|
| H6 | Add commitment-spend key image dedup in Core.cpp | 10m |
| H7 | Add amount/term validation in check_outs_valid | 10m |

### Block 4: Architectural (separate sessions)

| # | Fix | Plan |
|---|-----|------|
| C5 | Swap fee routing | Bridge SwapDaemon fees to Blockchain m_currentEpochSwapFees |
| C6 | CD yield dead code | Consolidate pushBlock + addNewBlock epoch processing |

---
## Implementation Notes

### C1: AmmClaim
Change from full proportional withdrawal to fee-only distribution.
```cpp
// OLD: withdraws full proportional reserves without burning LP shares
// NEW: only withdraws fee share, principal stays in pool
```

### C4: TWAP ordering
Save avgQ64 before TWAP reset, use saved value in rebalancer.

### H4: BankingIndex off-by-one
Move `--blockCount` BEFORE the burnedXfg check, matching deposit index pattern.

### H1: AmmCompound
Remove the `accumulatedLpFees = 0` line. Compound should be a no-op (fees auto-compound in reserves).

### H9: claimerFee reversal
Add reversal of `m_feePoolBalance`, `m_currentEpochSwapFees`, `m_totalSwapFeesCollected` in popTransaction.

### C3/H8: Epoch state reversal
Add `EpochStateDelta` struct stored per-block, reversed in popBlock/removeLastBlock.

### H2: Swap reversal
Store original output amount in pushTransaction, use stored value in popTransaction.

---

## Post-Fix Expected Score

| Domain | Before | After |
|---|---|---|
| Input Validation | 35 | 75 |
| State Integrity | 25 | 80 |
| Cryptographic Correctness | 55 | 65 |
| Resiliency | 30 | 75 |
| Fee Accounting | 20 | 25 (C5/C6 pending) |
| **Overall** | **33** | **64** |

Final 80+ requires C5/C6 architectural fixes.
