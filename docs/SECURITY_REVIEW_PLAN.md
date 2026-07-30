# Hearth / HEAT — Security Review & Fix Plan

**Date:** 2026-05-14 | **Audit:** 007 + graphify + fuego-orchestrator | **Score:** 33/100 → Target: 80+
**Last Updated:** 2026-05-31 | **Verification:** Full code audit of all fixes

---

## Findings Summary

| # | Severity | Component | Issue | Status | Evidence |
|---|----------|-----------|-------|--------|----------|
| C1 | CRITICAL | AmmClaim | Withdraws full reserves, doesn't burn LP shares | ✅ FIXED | `Blockchain.cpp:3194` fee-only proportional calc from `accumulatedLpFees` |
| C2 | CRITICAL | AMM validation | No block-level validation for Claim/Compound | ✅ ADDRESSED | pushBlock validates tx-level (`Blockchain.cpp:3187-3199`) |
| C3 | CRITICAL | popBlock | m_heatSupply not reversed on reorg | ✅ FIXED | `Blockchain.cpp:4173-4189` `EpochStateSnapshot` saved + restored |
| C4 | CRITICAL | Rebalancer | Division by zero after TWAP reset | ✅ FIXED | `Blockchain.cpp:3305` epochTwapAvg before TWAP reset; TWAP in snapshot (`3930-3932`) |
| C5 | CRITICAL | Swap fees | Fees never reach blockchain (SwapDaemon→Blockchain) | ✅ FIXED | `Blockchain.cpp:4990-4994` addSwapFee() → m_currentEpochSwapFees; SwapDaemon→RPC→Core chain complete |
| C6 | CRITICAL | CD yield | Dead code — double epoch processing | ✅ FIXED | `Blockchain.cpp:3286` consolidated in pushBlock; addNewBlock delegates only |
| H1 | HIGH | AmmCompound | Zeroes all LP fees — zero-cost griefing | ✅ FIXED | `Blockchain.cpp:3185-3186` Compound is no-op; fees auto-compound in reserves |
| H2 | HIGH | popTransaction | Swap reversal uses wrong reserves | ✅ FIXED | `Blockchain.cpp:4569-4589` reverses using stored reserves + correct formula |
| H3 | HIGH | popTransaction | No reversal for AmmClaim/AmmCompound | ✅ FIXED | `Blockchain.cpp:4608-4613` Compound (no-op) + Claim (adds fees back) |
| H4 | HIGH | BankingIndex | popBlock off-by-one on burned entries | ✅ FIXED | `BankingIndex.cpp:95` --blockCount before burnedXfg check |
| H5 | HIGH | BankingIndex | popBlocks erases wrong entries | ✅ FIXED | `BankingIndex.cpp:123-154` correct ordering + burnedXfg cleanup |
| H6 | HIGH | Core.cpp | Key image dup check missing for commitment spends | ✅ FIXED | `Core.cpp:453,457,463,467` commitment spend + transfer key image dedup |
| H7 | HIGH | check_outs_valid | Missing amount/term validation | ✅ FIXED | `CryptoNoteFormatUtils.cpp:379-386` term bounds [DEPOSIT_MIN_TERM, DEPOSIT_MAX_TERM] + pool marker bypass |
| H8 | HIGH | popBlock | No reversal of TWAP/PI/CD yield/rebalance | ✅ FIXED | `Blockchain.cpp:4173-4189` full EpochStateSnapshot restore (all 15 state fields) |
| H9 | HIGH | popTransaction | claimerFee not reversed on rollback | ✅ FIXED | `Blockchain.cpp:4538-4545` explicit reversal of feePoolBalance, epochSwapFees, totalSwapFees |

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
| Input Validation | 35 | 80 |
| State Integrity | 25 | 85 |
| Cryptographic Correctness | 55 | 70 |
| Resiliency | 30 | 80 |
| Fee Accounting | 20 | 80 (C5 fee routing complete) |
| **Overall** | **33** | **79** |

All 15 findings (C1-C6, H1-H9) are now resolved. H7 closed the last gap with term bounds validation in check_outs_valid.
