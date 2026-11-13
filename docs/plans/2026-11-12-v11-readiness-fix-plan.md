# v11 Readiness Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the six consensus blockers and the high-priority privacy, economic and completeness gaps found in the 2026-11-12 audit, so v11 (HEAT, Hearth, atomic swaps) can activate and v12 (CDs) has a sound base.

**Architecture:** Every consensus input must come from the chain, survive a restart, and reverse exactly on a pop. Phase A makes state deterministic, Phase B makes the Hearth pool solvent, Phase C binds swap escrow spends, Phase D bounds mint and removes address disclosure, Phase E finishes wallets and proves the whole on a reset testnet.

**Tech Stack:** C++17, CMake/Make, Boost 1.86+, existing test binaries `core_tests`, `test_hearth_amm`, `test_orderbook_auction`, `test_swap_escrow_claim`.

**Spec:** the audit and its adversarial review (session of 2026-11-12). Findings are referenced by id (C1–C6, H1–H4, M1–M6). Evidence is code-traced; nothing was executed. Task 0 therefore proves each defect with a failing test before any fix.

## Global Constraints

- v11 mainnet height 1,111,111 is not active: consensus rules and tx-extra wire formats may change. Testnet v11 history will not revalidate and must be reset (Task 17).
- Yield may only come from fees paid. No path may mint HEAT or credit Eternal Flame from a value nobody paid on-chain.
- No consensus value may be read from RPC, from process-local globals that are not persisted, or from a container whose size depends on node history.
- Every state mutation in push has an exact inverse in pop, and both survive `storeCache` → restart.
- `CHANGELOG.agent.md` entry exists before any source edit (Task 0). No commit or push without the owner's explicit go-ahead; "Checkpoint" steps stage only.
- Build ground truth: `make -j$(nproc)`. Test binaries run from `build/release/src/`.
- No code comments that narrate; names and extracted functions carry the meaning.
- Each fix is proven by mutation: revert the fix line, confirm the new test fails, restore, confirm it passes.

## Owner decisions (defaults chosen; confirm or change before the task starts)

| # | Decision | Default in this plan | Cost of the default |
|---|---|---|---|
| D1 | Swap protocol fee form | A consensus-enforced XFG sink (destroyed), nominal 1% refund / 2% claim, network fee paid on top | The treasury key no longer receives XFG; swap users pay the network fee in addition to the protocol fee |
| D2 | Mint bound | No volume cap. Mint is priced at the minter-worst of epoch rate, 8-block TWAP and opening spot; until the epoch rate is active the genesis ratio (1 HEAT per 8 XFG) is an additional ceiling | A minter never receives more HEAT per XFG than the pool pays, so a genuine XFG rally inside the first two epochs mints at the genesis ratio |
| D3 | Self-trade exclusion | Removed | Per-order keys make it a no-op; wash trades cannot be detected on a privacy chain |
| D4 | CD privacy (v12) | Moved to `docs/plans/2026-11-13-heat-cd-privacy-plan.md`. The bucket rule was rejected by the owner on 2026-11-13 | CDs stay deferred until that plan's choice is built |
| D5 | Reorg deeper than 1,000 blocks | Truncate to the fork point and replay from genesis | Slow on the rare path; correct instead of divergent |
| D6 | Seed LP shares | Protocol-owned; no provider key | Nobody can ever withdraw the seed. A provider key would let its holder withdraw 8,000 XFG + 1,000 HEAT that consensus created without any payment |

## Review Focus

1. A node restarted mid-epoch with resting limit orders must produce the same next block state as a node that never stopped (Task 2).
2. A block rejected at an epoch boundary after its transactions connected must leave no trace (Task 3).
3. A reorg across an epoch boundary must restore the previous mint rate (Task 3).
4. A user order that takes the whole pool band, and one that takes one atom of it, must both conserve HEAT and XFG (Tasks 6, 7).
5. A swap escrow spend whose key image equals another user's pending key image must be rejected (Task 8).

---

## Phase 0 — Record and reproduce

### Task 0: Changelog entry and failing reproductions

**Files:**
- Modify: `CHANGELOG.agent.md` (new section "v11 readiness fixes", tasks 0–18, sign-off table)
- Modify: `tests/CoreTests/TreasuryCoreTests.cpp`, `tests/CoreTests/HearthAmmTests.cpp`, `tests/CoreTests/OrderbookAuctionTest.cpp`, `src/SwapDaemon/tests/test_swap_escrow_claim.cpp`

**Interfaces:**
- Consumes: the chain fixture and `VaultCoreTestAccess` friend in `TreasuryCoreTests.cpp:1133`.
- Produces: `class HearthTestAccess` (friend of `Blockchain`, declared next to `VaultCoreTestAccess` at `Blockchain.h:804`) with `static uint64_t heatInExistence(const Blockchain&)` and `static uint64_t xfgHeldByHearth(const Blockchain&)`. Later tasks assert on these.

`heatInExistence` = Σ unspent HEAT commitment outputs + `reserveHeat` + `pendingHeat` + Σ `proceedsHeat` of unwithdrawn orders + `cdHearthFeeAccumulator` + vault HEAT partitions + `m_treasuryHeatReserve`. `xfgHeldByHearth` = `reserveXfg` + `pendingXfg` + Σ `proceedsXfg` + `m_treasuryLpPendingXfg`.

- [ ] **Step 1:** Write the changelog section with all tasks as TODO.
- [ ] **Step 2:** Add one test per finding, each expected to FAIL on current code:
  - `PoolTakerFillConservesHeat` (C2): a user ask smaller than the pool bid depth crosses the band; assert `heatInExistence` is unchanged by the block.
  - `BandFillNeverBeatsCurve` (C1): set the clearing price to half of spot with two crossing user orders, advance one block, take the pool asks; assert HEAT paid ≥ `ammCostToTake(xfgTaken, reserveHeat, reserveXfg)` at pre-fill reserves.
  - `RestartKeepsHearthState` (C4): past bootstrap with one resting order, `storeCache`, reload, push one identical block on both chains; assert equal `m_ammPool`, `m_limitDeposits` and clearing price.
  - `RebuildAndLiveAgreeOnMintPrice` (C5): three epochs past v11; compare the mint price on a live chain, a cache-reloaded chain and a `rebuildCache` chain.
  - `SwapFeeIsChainDerived` (C3): assert no public method lets a test raise `m_currentEpochSwapFees` without a block.
  - `EscrowSpendRejectsForeignKeyImage` (C6): escrow claim carrying a key image other than its derivation is rejected by `checkTransactionInputs`.
- [ ] **Step 3:** Run `make -j$(nproc)` then each binary. Expected: the six tests FAIL, all earlier tests PASS. A test that passes here falsifies its finding: stop, record it in the changelog, and drop the matching task.
- [ ] **Step 4:** Checkpoint.

---

## Phase A — Determinism (C3, C4, C5)

### Task 1: Swap fees derived on-chain (C3, H4-fee, closes PR-65 task 9)

**Files:**
- Modify: `src/CryptoNoteCore/Blockchain.cpp` (`validateSettlement` ~7997, `pushTransaction` ~6629, `popTransaction` ~7252, `processBlockEpochWork` 5266/5833), `Blockchain.h`
- Modify: `src/CryptoNoteCore/CryptoNoteFormatUtils.{h,cpp}`
- Delete code: `RpcServer.cpp:209` route and `on_add_swap_fee` (3132-3141), `COMMAND_RPC_ADD_SWAP_FEE`, `core::addSwapFee`, `Blockchain::addSwapFee`, `FuegoRpcClient::addSwapFee`, the two call sites in `SwapDaemon.cpp` (~1009, ~4203)
- Modify: `src/SwapDaemon/SwapTxBuilder.cpp` (treasury outputs at 226-235 and 578-587)
- Test: `tests/CoreTests/TreasuryCoreTests.cpp`, `src/SwapDaemon/tests/test_swap_escrow_claim.cpp`

**Interfaces:**
- Produces: `uint64_t requiredSwapEscrowFee(const TransactionPrefix& tx)` in `CryptoNoteFormatUtils` — Σ over `TransactionInputSwapEscrow` inputs of `amount × SWAP_FEE_RATE_BPS × (mode == 0 ? 2 : 1) / SWAP_FEE_RATE_DIVISOR`, overflow-checked.
- Produces: `uint64_t m_blockSwapFeeAccrued` (reset when the coinbase is pushed, like `m_blockCdCreatedHeat`).

- [ ] **Step 1: Tests.** `EscrowClaimWithoutFeeSinkRejected` (outputs + miner fee consume the whole escrow → settlement error); `EscrowClaimAccruesTwoPercent` (`m_currentEpochSwapFees` rises by exactly 2% after the block); `EscrowRefundAccruesOnePercent`; `PopBlockReversesSwapFee`; `EpochBoundaryBurnEqualsFeesSunk` (burn tally increase at the boundary ≤ fees sunk in the epoch).
- [ ] **Step 2:** In `validateSettlement`, a transaction with escrow inputs may carry no settlement tag and sets `flows.xfgSink = requiredSwapEscrowFee(tx)`. `settleAssetFlows` then makes the fee a destroyed amount; the miner fee is the remaining surplus.
- [ ] **Step 3:** `pushTransaction` adds the sink to `m_currentEpochSwapFees` and `m_blockSwapFeeAccrued`; `popTransaction` subtracts both. `processBlockEpochWork` records `m_blockSwapFeeAccrued` as the block contribution in place of the `epochFeesBefore` delta.
- [ ] **Step 4:** Remove the RPC, core, client and daemon reporting path. Keep the persisted `escrow_*_fee_report_attempted` fields in `SwapParams` unread so stored swaps still load.
- [ ] **Step 5:** `SwapTxBuilder` stops emitting the treasury output; the claim and refund prefixes leave `requiredSwapEscrowFee + networkFee` unassigned. Update `test_swap_escrow_claim` amounts.
- [ ] **Step 6:** Build, run `core_tests`, `test_swap_escrow_claim`, `test_swap_audit_regressions`. Mutation: set the sink to 0, `EscrowClaimWithoutFeeSinkRejected` must fail.
- [ ] **Step 7:** Checkpoint.

### Task 2: Persist Hearth book state (C4)

**Files:**
- Modify: `src/CryptoNoteCore/Blockchain.cpp` (globals 701-711, serializer 380-507, `processOrderbookForBlock` 4313-4331, `popBlock` 6229-6248), `Blockchain.h:681-760`, `PoolOrderOrchestrator.h`
- Test: `tests/CoreTests/HearthAmmTests.cpp`

**Interfaces:**
- Produces: `OrderbookRollbackSnapshot Blockchain::captureHearthBook() const` and `void Blockchain::restoreHearthBook(const OrderbookRollbackSnapshot&)`. The snapshot gains `lastRegenXfgReserve`, `lastRegenHeatReserve`. `serialize(ISerializer&)` for `OrderbookRollbackSnapshot` and `OrderFillRecord`.

- [ ] **Step 1: Tests.** `RestartKeepsHearthState` from Task 0; `PopAfterRestartReversesFills` (fill a resting order, store, reload, `popBlock`, assert `m_limitDeposits` and reserves equal the pre-block values).
- [ ] **Step 2:** Extract the capture block (4314-4327) and the restore block (6231-6247) into the two functions; both existing sites call them.
- [ ] **Step 3:** Serialize `"hearth_book"` (from `captureHearthBook()`), `"orderbook_snapshots"` and `"block_order_fills"` beside `vault_spent_by_tx` (498), using the same deque-through-vector pattern. On load call `restoreHearthBook`.
- [ ] **Step 4:** `CURRENT_BLOCKCACHE_STORAGE_ARCHIVE_VER` 17 → 18.
- [ ] **Step 5:** Build, run `test_hearth_amm`, `core_tests`. Mutation: skip `restoreHearthBook` on load, `RestartKeepsHearthState` must fail.
- [ ] **Step 6:** Checkpoint.

Note: `rebuildOrderbookFromUtxoSet` (4738) is dead code with no callers; left in place, not part of this change.

### Task 3: Deterministic mint price (C5)

**Files:**
- Modify: `src/CryptoNoteCore/Blockchain.cpp` (3706-3716, `accumulateTwap` 5899-5929, serializer 417, `popBlock` 6208, `validateSettlement` 8167-8168, `pushTransaction` 6646), `Blockchain.h:508` (`EpochStateSnapshot`), `Blockchain.h:607`
- Test: `tests/CoreTests/TreasuryCoreTests.cpp`

**Interfaces:**
- Produces: `uint64_t Blockchain::mintPriceAt(uint32_t height) const` — the only mint price source for validation, settlement, RPC and wallets. Task 9 extends it.
- Produces: `EpochStateSnapshot::epochMintRate`.

Rule: `mintPriceAt(h)` returns `m_epochMintRate` when `h > upgradeHeight(v11) + 2 × epochDuration` and the rate is non-zero, else `twapBefore(h)`. The rate for an epoch is fixed when its predecessor's last block commits, so the chain tip alone determines the price of the next block and mempool and block validation agree.

- [ ] **Step 1: Tests.** `RebuildAndLiveAgreeOnMintPrice` from Task 0; `PopAcrossEpochBoundaryRestoresMintRate`; `RejectedBoundaryBlockLeavesNoSnapshot` (push a boundary block with an invalid transaction; assert `m_epochSnapshots.size()` and the rate are unchanged); `PremiumPricedAtMintPrice` (treasury HEAT credit equals `premiumHeatFor(burn, mintPriceAt(h))` in an epoch where the rate differs from the TWAP).
- [ ] **Step 2:** Move the rate assignment out of `pushBlock` (3713-3715) into `accumulateTwap`, immediately before the accumulator reset at 5926. `accumulateTwap` already runs in the rebuild replay.
- [ ] **Step 3:** Add `epochMintRate` to `EpochStateSnapshot` (both capture sites, 1252 and 3698), restore it in `popBlock` at 6208, serialize `"epoch_mint_rate"`.
- [ ] **Step 4:** In `pushBlock`, a scope guard pops the snapshot pushed at 3706 on every failure return after it.
- [ ] **Step 5:** Replace the price expression at 8167-8168 and the `twapBefore(block.height)` at 6646 with `mintPriceAt(height)`. Delete the `m_epochSnapshots.size() >= 2` gate.
- [ ] **Step 6:** Point `/heat_metrics`, `/amm_quote` `mint_output`, and the WalletGreen / SimpleWallet mint builders at `mintPriceAt(tip + 1)` (find callers with `grep -rn getRollingTwap src`).
- [ ] **Step 7:** Build, run `core_tests`. Mutation: drop the snapshot field restore, `PopAcrossEpochBoundaryRestoresMintRate` must fail.
- [ ] **Step 8:** Checkpoint.

### Task 4: Reorg deeper than rollback history (M4)

**Files:**
- Modify: `src/CryptoNoteCore/Blockchain.cpp` (`switch_to_alternative_blockchain` 1569, `rollback_blockchain_switching` 1527)
- Test: `tests/CoreTests/TreasuryCoreTests.cpp`

- [ ] **Step 1: Test.** `DeepReorgMatchesFreshSync` with `MAX_ROLLBACK_HISTORY` lowered through the test access class: reorg deeper than the history, assert state equals a fresh sync of the winning chain.
- [ ] **Step 2:** When the fork point is at or above the v11 height and deeper than the oldest retained rollback record, truncate `m_blocks` to the fork point, call `rebuildCache()`, then push the alternative blocks. The error log at 6217 stays as a guard.
- [ ] **Step 3:** Build, run, mutation (skip the replay branch → test fails), checkpoint.

---

## Phase B — Hearth solvency (C1, C2)

### Task 5: All-or-nothing auction application

**Files:**
- Modify: `src/CryptoNoteCore/Blockchain.cpp:4466-4573`
- Test: `tests/CoreTests/HearthAmmTests.cpp`

**Interfaces:**
- Produces: `bool Blockchain::auctionSettlesExactly(const AuctionResult&) const` — true when every fill can be applied in full: each order exists, is live, has escrow ≥ its debit, `pending*` and `reserve*` cover every debit in fill order.

The current loop skips a fill with `continue` when a guard trips while its counterparty's fill is still applied, and clamps with `std::min`. Either breaks conservation.

- [ ] **Step 1: Test.** `PartialAuctionNeverApplied`: force one fill to fail (test access zeroes an order's escrow after the auction inputs are built); assert no state changed and the clearing price is unchanged.
- [ ] **Step 2:** Call `auctionSettlesExactly` before the loop; when false, treat the auction as not crossed. Remove the per-fill `continue` guards and `std::min` clamps.
- [ ] **Step 3:** Build, run `test_hearth_amm`, mutation, checkpoint.

### Task 6: Pool is always the maker (C2)

**Files:**
- Modify: `src/CryptoNoteCore/OrderbookAuction.cpp` (taker selection and fee block), `OrderbookAuction.h`
- Modify: `src/CryptoNoteCore/Blockchain.cpp:4473-4516` (pool fill branch), `popBlock` pool-fill reversal (~6020-6060)
- Test: `tests/CoreTests/OrderbookAuctionTest.cpp`, `tests/CoreTests/HearthAmmTests.cpp`

**Interfaces:**
- Consumes: `HearthTestAccess::heatInExistence`.
- Produces: `AuctionResult::poolFilledSide` (`0` bid, `1` ask, `255` none).

Rule: if any 0xF0 order is filled, the taker side is the opposite side, whatever the rationing; otherwise the existing rationed-side rule holds. Fee accrues on taker-side fills only; the rebate pool is shared pro-rata by fill value across maker-side fills, pool included. The pool's rebate share returns to `reserveHeat`.

- [ ] **Step 1: Tests.** `PoolTakerFillConservesHeat` from Task 0; `UserCrossingBandPaysOnePercent` (proceeds = price value − 1%); `PoolMakerRebateReachesReserves` (`reserveHeat` delta = price value + rebate share); `BandRoundTripLosesMoney` (buy then sell the band in consecutive blocks; assert the trader ends with less HEAT-equivalent than they started); `PopBlockRestoresReservesAfterPoolFill`.
- [ ] **Step 2:** In `runAuction`, set `poolFilledSide` from the fills and choose the taker side by the rule above.
- [ ] **Step 3:** In the pool branch, apply `fill.rebateHeat`: pool ask filled → `reserveHeat += fill.heat + fill.rebateHeat`; pool bid filled → `reserveHeat -= fill.heat − fill.rebateHeat`. Store `rebateHeat` in the fill record and invert it in `popBlock`.
- [ ] **Step 4:** Build, run `test_orderbook_auction`, `test_hearth_amm`. Mutation: revert the taker rule, `BandRoundTripLosesMoney` must fail.
- [ ] **Step 5:** Checkpoint.

### Task 7: Band priced on the curve from spot, rebuilt every block (C1)

**Files:**
- Modify: `src/CryptoNoteCore/OrderbookTypes.{h,cpp}` (`PoolOrderParams`, `generatePoolOrders`), `PoolOrderOrchestrator.{h,cpp}`, `Blockchain.cpp:4351-4392`, `Blockchain.h` (`OrderbookRollbackSnapshot`)
- Test: `tests/CoreTests/HearthAmmTests.cpp`

**Interfaces:**
- Produces: `PoolOrderParams::midPrice` (replaces `P_clear`), always `ammGetSpotPrice(reserveXfg, reserveHeat)`.

Rules:
1. Orders are generated every block after bootstrap, from the reserves as the block's transactions left them. `shouldRegenerate`, `g_blocksSinceLastPoolRegen`, `g_priorPool*` and `g_poolBandFilledLastBlock` are removed, with their snapshot fields.
2. Ask tier `k` covering cumulative XFG `q_k`: price = max(`midPrice + spread/2 + k·spread/10`, ceil(`(ammCostToTake(q_k) − ammCostToTake(q_{k−1})) × COIN / tierAmount`)), rounded up to `ORDER_PRICE_TICK`.
3. Bid tier `k`: price = min(`midPrice − spread/2 − k·spread/10`, floor(`(ammGetOutputAmount(q_k) − ammGetOutputAmount(q_{k−1})) × COIN / tierAmount`)), rounded down to the tick.
4. `computeSpreadBps` takes spot, not the clearing price.

Because the auction clears at one price no lower than the highest filled ask tier, the pool's total HEAT received is at least the curve cost of the XFG it gave; symmetric for bids.

- [ ] **Step 1: Tests.** `BandFillNeverBeatsCurve` from Task 0; `ClearingPriceDoesNotMoveBand` (manipulate the clearing price, assert next block's pool order prices depend only on reserves); `BandFollowsSwapInSameBlock` (a swap in block N moves the band used by block N's auction); `ConstantProductNeverShrinksAcrossAuction` (`reserveXfg × reserveHeat` after ≥ before for any pool fill).
- [ ] **Step 2:** Implement rules 1–4. Bid depth in XFG is derived from the HEAT budget with 128-bit arithmetic; a tier whose volume exceeds `UINT64_MAX` is dropped.
- [ ] **Step 3:** Extend `auctionSettlesExactly` (Task 5) with the aggregate check: pool HEAT received ≥ `ammCostToTake` for pool asks, pool HEAT paid ≤ `ammGetOutputAmount` for pool bids, at pre-auction reserves.
- [ ] **Step 4:** Build, run `test_hearth_amm`, `test_orderbook_auction`, `core_tests`. Mutation: centre on the clearing price again, `ClearingPriceDoesNotMoveBand` must fail.
- [ ] **Step 5:** Update AGENTS.md "Data Flow Per Block" step 1–2 to say the band is rebuilt each block from spot. Checkpoint.

---

## Phase C — Swap escrow (C6)

### Task 8: Bind the escrow key image in consensus

**Files:**
- Modify: `src/CryptoNoteCore/CryptoNoteFormatUtils.{h,cpp}`, `src/CryptoNoteCore/Blockchain.cpp:7439` (`validateSwapEscrowInput`), `src/SwapDaemon/SwapTxBuilder.cpp:510-530`
- Test: `tests/CoreTests/TreasuryCoreTests.cpp`, `src/SwapDaemon/tests/test_swap_escrow_claim.cpp`

**Interfaces:**
- Produces: `Crypto::KeyImage deriveSwapEscrowKeyImage(const Crypto::Hash& escrowTxId, uint16_t outputIndex, uint8_t mode)` in `CryptoNoteFormatUtils`. `SwapTxBuilder::swapEscrowKeyImage` forwards to it; the bytes it returns must not change.

- [ ] **Step 1: Tests.** `EscrowSpendRejectsForeignKeyImage` from Task 0; `EscrowSpendAcceptsDerivedKeyImage`; `DerivedKeyImageInPrimeOrderSubgroup` (passes the `scalarmultKey(ki, L) == I` check for 1,000 random inputs); `BuilderAndConsensusDeriveSameImage`.
- [ ] **Step 2:** Move the derivation body into the core function unchanged. If `DerivedKeyImageInPrimeOrderSubgroup` fails, multiply by the cofactor in the derivation and update both tests of fixed bytes — this would be a wire change for pending testnet swaps only.
- [ ] **Step 3:** `validateSwapEscrowInput` rejects `input.keyImage != deriveSwapEscrowKeyImage(input.escrowTxId, input.escrowOutputIndex, input.mode)`.
- [ ] **Step 4:** Build, run `core_tests`, `test_swap_escrow_claim`. Mutation: remove the equality check, the Task 0 test must fail.
- [ ] **Step 5:** Checkpoint.

---

## Phase D — Economic bounds and privacy (H1, H2, M3, H3)

### Task 9: Mint priced minter-worst, no volume cap (H1)

Minting is the system's core function and is not throttled. A mint only harms holders when it issues more HEAT per XFG than the pool pays, because the excess can be sold into the pool for more XFG than was burned. Pricing at the lowest available rate removes that case without limiting volume: a mint at or below the pool rate is a burn the protocol always wants.

**Files:**
- Modify: `src/CryptoNoteConfig.h` (near 416), `src/CryptoNoteCore/Blockchain.cpp` (`mintPriceAt`, `validateSettlement` mint branch)
- Test: `tests/CoreTests/TreasuryCoreTests.cpp`

**Interfaces:**
- Consumes: `mintPriceAt` from Task 3.
- Produces: `const uint64_t HEAT_MINT_GENESIS_RATE = COIN / 8;` (HEAT per XFG × COIN, the launch ratio).

- [ ] **Step 1: Tests.** `MintUsesLowestOfRateTwapAndOpeningSpot`; `PumpedPoolDoesNotRaiseMintOutput` (double the spot for 8 blocks inside an epoch; mint output stays at the epoch rate); `BootstrapMintNeverExceedsGenesisRatio` (before the epoch rate is active, pump the pool; mint output stays at 1 HEAT per 8 XFG); `MintThenSellToPoolNeverReturnsMoreXfgThanBurned` (for burns of 1, 1,000 and 100,000 XFG at the genesis pool); `LargeMintInOneBlockAccepted` (a burn of 1,000,000 XFG validates).
- [ ] **Step 2:** `mintPriceAt(h)` returns the minimum HEAT-per-XFG of the epoch rate (when active), `twapBefore(h)` and the block's opening spot; while the epoch rate is not active, `HEAT_MINT_GENESIS_RATE` joins the minimum. Zero from any required source still means "no price".
- [ ] **Step 3:** Build, run. Mutation: return the TWAP alone, `PumpedPoolDoesNotRaiseMintOutput` must fail.
- [ ] **Step 4:** Checkpoint.

Limits stated plainly:
- A mint transaction built at one price is rejected, not repriced, if the minimum falls before it is mined; the wallet rebuilds it.
- After the epoch rate is active a pool pump can still lower the price for minters (they receive less HEAT), never raise it.
- The genesis ceiling ends when the epoch rate activates, two epochs after v11.

### Task 10: Per-order ownership key and bound order id (H2, M3, D3)

**Files:**
- Modify: `src/CryptoNoteCore/TransactionExtra.{h,cpp}` (structs at 343-360, their (de)serializers, `addLimitDepositToExtra`, `addLimitWithdrawToExtra`, `getLimitWithdrawAuthHash`), `Blockchain.cpp:8243-8294`, `OrderbookAuction.cpp:60-106`, `Blockchain.h` (`LimitDepositInfo`)
- Modify: `src/Wallet/WalletGreen.cpp:1676-1742, 1846-1880`, `src/WalletLegacy/WalletTransactionSender.cpp:3181, 3395-3401`, `src/Rpc/RpcServer.cpp:3112-3120` and its RPC struct, `tui/rpc_builders.go`
- Test: `tests/CoreTests/HearthAmmTests.cpp`, `tests/CoreTests/OrderbookAuctionTest.cpp`, `tui/rpc_builders_test.go`

**Interfaces:**
- Produces:
  - `TransactionExtraLimitDeposit::ownerKeyHash` (replaces `addressHash`) = `cn_fast_hash(ownerKey)`.
  - `TransactionExtraLimitWithdraw { Crypto::Hash orderId; Crypto::PublicKey ownerKey; Crypto::Hash outputsHash; Crypto::Signature proof; }` — `viewPublicKey` removed.
  - `Crypto::SecretKey deriveLimitOrderSecret(const Crypto::SecretKey& spendSecret, const Crypto::KeyImage& firstInputKeyImage)` = `hash_to_scalar("fuego-limit-order-owner" ‖ spendSecret ‖ firstInputKeyImage)`, in the wallet layer.
  - `Crypto::Hash limitOrderId(const Crypto::KeyImage& firstInputKeyImage, const Crypto::Hash& ownerKeyHash)` in `TransactionExtra`.

- [ ] **Step 1: Tests.** `WithdrawRevealsNoWalletKey` (the withdraw extra contains neither the wallet spend nor view public key bytes); `TwoOrdersFromOneWalletShareNoIdentifier`; `DepositWithForeignOrderIdRejected` (`orderId != limitOrderId(first key image, ownerKeyHash)`); `CopiedOrderIdCannotPreemptVictim`; `WalletRecoversOrderKeyFromSeed` (derive, drop wallet cache, re-derive, withdraw succeeds).
- [ ] **Step 2:** Change the two structs and serializers. `validateSettlement` requires the bound order id on deposit, and on withdraw checks `cn_fast_hash(ownerKey) == ownerKeyHash` and the signature under `ownerKey`.
- [ ] **Step 3:** Remove the self-trade exclusion block and `AuctionOrder::addressHash`; delete the tests that assert it.
- [ ] **Step 4:** Wallets derive the order key from the deposit's first input key image, which is on-chain, so a wallet restored from seed re-derives it with no stored secret and no extra field. `/getlimitorders` returns `owner_key_hash`.
- [ ] **Step 5:** Build, run `test_hearth_amm`, `test_orderbook_auction`, `go test ./tui/...`. Mutation: sign the withdraw with the wallet spend key, consensus must reject.
- [ ] **Step 6:** Tick the "no plaintext spend/view in `tx_extra`" line in `docs/security/PRE_PRODUCTION_CHECKLIST.md`. Checkpoint.

### Task 11: CD privacy redesign (H3, v12) — moved

The bucket rule (a ring limited to one amount, one term and one creation epoch) was rejected by the owner on 2026-11-13. The CD exit, and the HEAT ring defects found while tracing it, are planned in `docs/plans/2026-11-13-heat-cd-privacy-plan.md`.

- CDs stay deferred until that plan's Phase 2 choice is built. The claim path as written must not activate: it over-pays (interest is taken from the youngest ring member), it identifies the CD, and on mainnet it mostly cannot relay.
- That plan's Phase 0 is a v11 dependency, not a v12 one: as written an ordinary HEAT spend does not validate (its finding H-1).

---

## Phase E — Completeness and release gates (H4, M1, M2, M5, M6)

### Task 12: RPC hygiene (M1, M6)

**Files:** `src/Rpc/RpcServer.h:199`, `src/Rpc/RpcServer.cpp:382-403`, `src/Daemon/Daemon.cpp:410-411`

- [ ] `bool m_restricted_rpc = true;`; call `restrictRPC` before `start`.
- [ ] Compare tokens with a length-independent constant-time function (`CRYPTO_memcmp` after a length check) in `checkSwapControlAuth` and the swap daemon's `authorize()`.
- [ ] Test: `RestrictedByDefaultBeforeConfigured` in the RPC unit tests. Build, run, checkpoint.

### Task 13: v11 block fields carry spot and are validated (M5)

**Files:** `src/CryptoNoteCore/Blockchain.cpp:4719-4735`, `pushBlock` before `processOrderbookForBlock`

- [ ] `hearthPoolRatio` stores pool spot; `orderbookClearingPrice` stores the clearing price.
- [ ] After `processOrderbookForBlock`, a received block whose five v11 fields differ from the computed values is rejected. Miners fill them from the template (`Core.cpp:1727` already reads spot).
- [ ] Tests: `BlockWithWrongHearthFieldsRejected`, `TemplateFieldsMatchValidation`. Build, run, checkpoint.

Hard truth: the fields are still outside the block hash, so they authenticate nothing to a light client. Committing Hearth state into the hash is a separate hard fork and is not in this plan.

### Task 14: Reserve-moving transactions per block (M2)

**Files:** block template loop (`grep -n "reserve-moving\|reserveMoving" src/CryptoNoteCore/*.cpp`)

- [ ] **Step 1:** Read the template loop and record why it admits one swap/LP transaction per block.
- [ ] **Step 2:** If the reason is only that the mempool validates each transaction against tip reserves, validate candidates sequentially with one shared `SettlementScratch` and a scratch copy of `m_ammPool`, admitting each that still settles. If block validation itself assumes one, keep the limit and document it in AGENTS.md as a throughput bound of one pool trade per block.
- [ ] **Step 3:** Test `TemplateAdmitsEverySwapThatSettlesInOrder` or the documentation change. Build, run, checkpoint.

### Task 15: SimpleWallet HEAT builders (changelog task 7b)

**Files:** `src/WalletLegacy/WalletTransactionSender.cpp`, `WalletSendTransactionContext.h`

- [ ] Port the five WalletGreen fixes listed under "WalletGreen (walletd, PaymentGate) HEAT builders, task 7a" in `CHANGELOG.agent.md`: XFG fee from XFG inputs, all inputs added before signing, per-amount decoys, HEAT-funded BUY escrow, CD key re-derivation.
- [ ] Unit tests mirror the WalletGreen ones per builder. Build `SimpleWallet`, run, checkpoint.

### Task 16: Seed LP shares are protocol-owned (changelog task 16)

**Files:** `src/CryptoNoteConfig.h`, `src/CryptoNoteCore/Blockchain.cpp` (`seedHearthPool` 685, LP-remove branch 8116)

The seed (8,000 XFG / 1,000 HEAT) is written into the reserves by `seedHearthPool` (`Blockchain.cpp:685`); no transaction pays for it. Its LP shares therefore have no owner key. A provider key would only be needed to let a person withdraw them, and that withdrawal would hand out value consensus created for free. Default (D6): no key.

- [ ] `m_seedLpShares` are protocol-owned for the life of the chain: `LpRemoveAuth` can never burn them, and `withdrawTreasuryLp` (5932) excludes them from the withdrawable amount.
- [ ] The bootstrap-repaid check (5778-5781) stays as the measure of when AuntPoL's own reserves match the seed; it moves no funds.
- [ ] Tests: `SeedSharesNeverRemovable` (LP remove of `totalLpShares` with only the seed outstanding is rejected), `TreasuryWithdrawExcludesSeed`, `BootstrapRepaidCheckUnaffected`. Build, run, checkpoint.
- [ ] Close changelog task 16 with this decision recorded.

### Task 17: Block-serving RPC hang and testnet reset (H4)

The cause is not known. Static reading of the request path on 2026-11-13 found no wait, no lock inversion and no unbounded loop: `binMethod` (`RpcServer.cpp:58`) → `on_query_blocks_lite` (736) → `core::queryBlocksLite` → `findStartAndFullOffsets` → `Blockchain::getLowerBound` (7648) → `getBlocks` (2174). The steps below find it by observation and fix what is observed. This container has no Boost headers, so the work runs on a machine that builds the daemon.

**Files:**
- Create: `tests/IntegrationTests/BlockServingRpcTest.cpp` (or a script under `scripts/` if `tests/IntegrationTests` stays unbuilt), wired into `src/CMakeLists.txt` as `test_block_serving_rpc`
- Modify: whichever of `src/Rpc/RpcServer.cpp`, `src/HTTP/HttpServer.cpp`, `src/CryptoNoteCore/Core.cpp`, `src/CryptoNoteCore/Blockchain.cpp`, `src/CryptoNoteCore/SwappedVector.h` Step 3 implicates

- [ ] **Step 1: Reproduce on the smallest chain.** Start a fresh `--testnet` daemon, mine 20 blocks, then call `queryblockslite.bin` with a well-formed request (genesis hash as the only block id, timestamp 0) using the repo's own `NodeRpcProxy`, with a 10-second client timeout. Record: does it hang on 20 blocks, or only on a long chain? This splits "request path is broken" from "cost grows with height".
- [ ] **Step 2: Capture the hung state.** `gdb -p <pid> -batch -ex "thread apply all bt"` three times, five seconds apart. Record which thread holds `m_blockchain_lock`, whether the RPC fiber is in `TcpStreambuf` read, in a handler, or in `TcpStreambuf` write, and whether the stacks change between captures (busy loop) or not (blocked).
- [ ] **Step 3: Match the capture to one cause and apply its fix.**

| Capture shows | Cause | Fix |
|---|---|---|
| RPC fiber blocked in `HttpParser::receiveRequest` / `TcpStreambuf::underflow` | Body never fully read: missing or wrong `Content-Length` handling for binary bodies | Read exactly `Content-Length` bytes in `HttpParser`; reject a binary request without it |
| Handler returned, fiber blocked in `stream.peek()` (`HttpServer.cpp` connection loop), client still waiting | Response sent without a usable `Content-Length`, or a `false` handler result produced an empty 200 the client keeps waiting on | `binMethod` sets status 500 and a body on every failure path; `HttpResponse` always emits `Content-Length` |
| Thread inside `getLowerBound` → `SwappedVector::operator[]`, stacks changing | `std::lower_bound` over disk-backed `m_blocks` deserializes whole `BlockEntry` records and evicts the block cache on each probe; note `timestamp - blockFutureTimeLimit()` underflows for small timestamps | Search `Blockchain::m_timestampIndex` when block indexes are enabled, otherwise keep a height-ordered timestamp vector in memory; clamp the subtraction at 0 |
| Thread inside `getBlocks` / `getTransactions`, stacks changing | Per-block transaction fetch for up to `BLOCKS_SYNCHRONIZING_DEFAULT_COUNT` blocks is too slow at mainnet height | Bound the response by serialized size and return a partial batch |
| RPC fiber waiting on `m_blockchain_lock`, another thread holds it and does not progress | Lock held across a long operation (cache rebuild, save, or a second dispatcher thread) | Release the lock around the long operation, or serve these three endpoints from a snapshot of the block index taken under the lock |
| Dispatcher thread blocked on an OS mutex while fibers are queued | A blocking `std::mutex` taken on the cooperative dispatcher thread while its holder waits for that dispatcher | Move the blocking call to `System::RemoteContext` (`src/System/RemoteContext.h`, used by the miner) |

  A capture that matches no row is recorded verbatim in the changelog and the task stays open; no speculative fix is applied.
- [ ] **Step 4: Regression test.** `test_block_serving_rpc` starts a testnet daemon, mines 200 blocks, and asserts each of the three endpoints answers a full-sync request within 5 seconds and that the returned block ids equal the chain's. It must fail on the unfixed binary and pass on the fixed one.
- [ ] **Step 5: Mainnet-height check.** Run the same three requests against a synced mainnet daemon; record wall time per request in the changelog.
- [ ] **Step 6:** Remove the "Block-serving RPC hang" entry from AGENTS.md Known Issues and `docs/plans/backlog.md:14`; own changelog entry with its task list and sign-off.
- [ ] **Step 7:** Reset the testnet with the Phase A–D rules from its v11 height.

### Task 18: End-to-end gate on the reset testnet

Depends on Phase 0 of `docs/plans/2026-11-13-heat-cd-privacy-plan.md`: until it lands, a HEAT spend does not validate on mainnet parameters.

Each line is a pass/fail observation on live nodes, two of which are restarted mid-run:

- [ ] Mint, HEAT send, swap both directions, limit deposit both sides, partial fill, expiry, withdraw, LP add/remove.
- [ ] A user order crossing the band; `heatInExistence` (exposed read-only via `/heat_metrics`) equal on all nodes and equal to minted minus burned.
- [ ] Atomic swap claim and refund; `m_currentEpochSwapFees` equal on all nodes with no RPC call.
- [ ] Restart a node inside an epoch with resting orders; it stays on chain for 200 blocks.
- [ ] A 3-block reorg across an epoch boundary; all nodes converge on one state hash.
- [ ] Two epochs past v11, mints validate identically on a live, a restarted and a resynced node.
- [ ] Record results in `docs/RELEASE_READINESS_CHECK.md`; sign off the changelog table.

## Not in this plan

- Open items in `docs/review/2026-09-09-swap-security-audit.md` that were not re-verified (SPV Merkle proof, state-machine edges, contract `contractId` salt). They need their own verification pass before swaps are enabled on mainnet.
- Committing Hearth state into the block hash.
- HEAT ring privacy and the CD exit: `docs/plans/2026-11-13-heat-cd-privacy-plan.md`.
