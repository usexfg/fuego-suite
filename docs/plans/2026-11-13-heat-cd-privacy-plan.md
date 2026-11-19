# HEAT Ring and CD Privacy Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make a HEAT spend valid and private on mainnet, and replace the CD interest claim with an exit that does not identify the CD, while yield still comes only from fees.

**Architecture:** Phase 0 implements option R1, the owner's choice for v11. Phase V11 adds the v11 network, transaction-shape and wallet privacy tasks. Phase 2A implements option A, the owner's choice for v12 CDs. Phase 3 lists the v12 privacy tasks: Tor/I2P, R2, R3, the post-quantum note pool and private HEAT transfers. Phases 1 and 2 keep the full option lists. Part 3 states what each option does to public supply auditing.

**Tech Stack:** C++17, CMake/Make, existing binaries `core_tests`, `test_hearth_amm`.

**Spec:** owner direction of 2026-11-13: rings limited to a CD's own amount, term and creation epoch are rejected, and so is a HEAT ring whose only ambiguous members are outputs from the spender's own block. Companion plan: `docs/plans/2026-11-12-v11-readiness-fix-plan.md`; its end-to-end gate (Task 18) depends on Phase 0 here.

**Evidence:** every statement in Part 1 is code-read on 2026-11-13. Nothing was executed. Phase 0 Task 1 turns each into a test that must fail on current code; a test that passes drops its finding and the task that fixes it.

## Global Constraints

- Yield comes only from fees paid. No inflation, and no burn-derived value, funds a CD.
- A commitment output created before v11 is never spendable as HEAT.
- XFG never enters a shielded pool or a hidden-amount output (owner, 2026-11-14). Any hidden-amount option applies to HEAT only.
- No consensus path admits a ring below the minimum on mainnet, in the mempool or in a block.
- v11 is not active on mainnet: consensus rules and wire formats may change. Testnet must be reset.
- `CHANGELOG.agent.md` entry before any source edit. No commit or push without the owner's go-ahead.
- Build ground truth `make -j$(nproc)`; each fix proven by mutation (revert the fix, test fails, restore, test passes).

## Review Focus

1. A bill whose denomination has fewer siblings than the ring size: the wallet reports "not spendable yet" and builds nothing; it never builds a short ring.
2. A pool marker, LP output or immature CD with the same amount as a bill never appears in a ring the wallet builds.
3. A pre-v11 commitment output with a known key is rejected as a HEAT input, in any ring.
4. A wallet restored from seed finds the bills a matured CD paid out (Phase 2, option A).
5. A reorg across a block that paid out CDs restores vault balances exactly.

## Order of work (2026-11-17)

1. Owner decisions that block v11: readiness plan D1, D3 and D5 (defaults set, unconfirmed); P-15 to P-17 and P-21 below.
2. Owner lifts the implementation hold. `CHANGELOG.agent.md` sections for the readiness plan, R1 and Phase V11.
3. Reproductions, all expected to fail on current code: readiness plan Task 0, R1 Task 1, and the reproductions in Tasks V11-2 and V11-3.
4. Task V11-1 (Dandelion++) in parallel. It changes relay behaviour only, not consensus.
5. Readiness plan Phases A–F (Phase F: DIGM, Tasks 19–20); R1 Tasks 2–10; Tasks V11-2 to V11-5; then V11-7 after readiness Tasks 5–7.
6. Gates on the reset testnet: R1 Task 11, Task V11-6, readiness plan Task 18.
7. In parallel from step 2: note-pool plan Tasks N0-2 (specification) and N0-3 (benchmark). Their results decide whether the pool and private HEAT can ship in v12.

---

## Part 1 — Current state

### HEAT rings (v11)

| # | Fact | Where |
|---|---|---|
| H-1 | Every HEAT output the wallet builds carries `term = HEAT_TERM`. Consensus rejects a commitment spend whose ring members all carry `HEAT_TERM` ("all ring members are burned outputs"). An ordinary HEAT ring is made only of such outputs, so as written it does not validate. No test reaches this check. | `WalletGreen.h:417`, `Blockchain.cpp:2976` |
| H-2 | The wallet asks for decoys created at or before its own output's block. The true input is therefore the newest ring member; only identical-amount outputs from the same block remain ambiguous. | `WalletGreen.cpp:1409` |
| H-3 | A ring is drawn from commitment outputs with the identical cleartext amount. Bills (500, 100, 50, 10, 5, 1, 0.5, 0.1 HEAT) are a wallet convention; consensus does not require them. The splitter folds any remainder into the largest bill, and several builders emit one odd-amount output. | `Blockchain.cpp:2357`, `WalletGreen.cpp:1364`, `:507`, `:1679` |
| H-4 | Mainnet mempool requires a ring of at least 8 for commitment spends; testnet allows 0. An output whose exact amount has fewer than 8 siblings does not relay. The rule is skipped for transactions arriving inside a block. | `Core.cpp:319`, `Currency.h:102` |
| H-5 | One index holds every commitment output of an amount: HEAT, pool markers, LP shares, CDs. The decoy RPC filters nothing and returns no term. A returned pool marker or immature CD makes the ring invalid. | `Blockchain.cpp:2357-2422` |
| H-6 | Ring size varies between 8 and 32, which marks transactions by wallet behaviour. | `Core.cpp:297-350` |
| H-7 | `amountCommitment` and `amountProof` are serialized on every commitment output and verified nowhere. `pedersen.cpp` and `mlsag.cpp` have no callers; the repository has no range proof. Amounts are cleartext. | `CryptoNoteSerialization.cpp:397` |
| H-8 | Hypothesis: no rule ties a ring member's creation height to v11. `classifyCommitmentTermAsset` maps every term except LP and the two XFG markers to HEAT, so a commitment output created under v10 (a burn deposit, whose secret the wallet stores) would be spent as HEAT of the same atomic amount once a ring passes H-1. | `AssetType.cpp:15`, `Blockchain.cpp:2828-2906` |
| H-9 | LP share outputs carry arbitrary share amounts, so an LP removal can never assemble 8 identical-amount ring members. On mainnet it does not relay. | `Core.cpp:319`, `Blockchain.cpp:8111-8126` |
| H-10 | A HEAT-to-XFG swap pays the XFG as a commitment output of arbitrary amount (`DEPOSIT_TERM_SWAP_RECEIVE_XFG`). It has no identical-amount siblings, so on mainnet the XFG received cannot be spent. The swap's HEAT change is one odd-amount output with the same problem. | `WalletGreen.cpp:2105-2123` |

### CD exit (v12)

- CD amounts and terms are free-form: any amount of at least 8 HEAT, any block count from 5,400 to 64,800 (`Blockchain.cpp:3293`). `CD_ALLOWED_TIERS` and `AMOUNT_TIER_*` are not enforced.
- Interest is capped by the youngest ring member's entitlement (`Blockchain.cpp:2828-3040`), so a wallet claiming in full must be the youngest member; it requests decoys accordingly (`WalletGreen.cpp:538`).
- `claimedInterest` is cleartext and is a function of amount, creation epoch and end epoch.
- An interest claim is rejected if any ring member is not a matured finite CD; with H-4 and H-5 a full claim on mainnet needs 7 older, matured CDs of the identical amount.
- The CD pays out as one output of principal plus interest (`WalletGreen.cpp:507`), an odd amount with no siblings.
- Result: one withdrawal links the CD's creation to its exit with certainty and shows amount, term, both heights and interest. Keys are one-time, so the owner's address is not shown.

### How CD interest is funded today

| Source | Path | Where |
|---|---|---|
| Atomic-swap fee, 80% (XFG) | At epoch close, converted to HEAT at the 8-block TWAP; the XFG is recorded burned (50% Eternal Flame, 50% SWF tally); the HEAT becomes a vault UTXO in `CD_APY_POOL` and raises `m_feePoolBalance` | `Blockchain.cpp:5703-5745` |
| Hearth taker fee, CD share (30% at v11, 70% at v12), already HEAT | Accumulates in `cdHearthFeeAccumulator`; joins the same vault UTXO at epoch close | same |
| Atomic-swap fee, 20% | `BONUS_VAULT`; pays only the yield-floor top-up in epochs whose rate is below `CD_YIELD_FLOOR_RATE` | `Blockchain.cpp:5306-5395`, `Currency.cpp` `calculateCdBonus` |

- A claim draws base interest from `CD_APY_POOL` and the floor top-up from `BONUS_VAULT` (`Blockchain.cpp:6546-6617`).
- AuntPoL pays nothing to CDs. It is the protocol's LP position funded by the mint premium; `m_treasuryLpYield` is never written.
- F-1: the swap-fee stream has no on-chain source today (readiness plan, finding C3 / Task 1).
- F-2: the epoch rate is priced at the boundary block's spot (`Blockchain.cpp:5417`) while the vault is funded at the TWAP (`:5707`). When spot is above the TWAP the rate promises more than the vault received; the boundary spot can be moved by one trade.
- F-3, to verify: fees closed in an epoch with no HEAT on deposit are minted into the vault at rate 0. No later rate was found that pays them out, so they would stay in the vault. All of v11 runs before CDs exist.
- F-4: `calculateCdInterest` compounds each epoch's interest into the base (`Currency.cpp`, `currentBase += epochInterest`), but the epoch rate divides fees by principal only (`m_heatOnDeposit`). Total entitlements therefore exceed the HEAT minted into the vault, and the last claimants are refused.

---

## Phase 0 — R1 for v11: implementation

R1 is the owner's choice for v11 (2026-11-13). When this phase is done, a HEAT spend validates on mainnet parameters with a ring of exactly 16 eligible bills, and nothing in the ring marks the true input. LP shares follow the same model (owner, 2026-11-14): fixed-size LP bills, spent in rings of 16, so an LP removal does not point at the deposit that created it.

### Rules R1 adds at block version 11

| # | Rule |
|---|---|
| R1-a | A HEAT output (`term` 0 or `HEAT_TERM`) has an amount in `HEAT_BILL_DENOMINATIONS`, and its `amountCommitment` and `amountProof` are all zero. |
| R1-b | An eligible HEAT ring member has `term` 0 or `HEAT_TERM`; sits in a block of major version 11 or later; is at least `HEAT_SPEND_MIN_AGE` (10) blocks old, or `minedMoneyUnlockWindow` blocks old if it is a coinbase output; and is not slashed. |
| R1-c | A HEAT-class commitment spend has exactly `HEAT_RING_SIZE` (16) distinct members, all eligible under R1-b. The all-`HEAT_TERM` rejection is removed. `claimedInterest` is 0. |
| R1-d | An LP output (`term == DEPOSIT_TERM_LP`) has an amount in `LP_BILL_DENOMINATIONS`. An LP-class commitment spend has exactly `HEAT_RING_SIZE` (16) distinct members, each an LP output of the same amount, from a block of version 11 or later, at least `HEAT_SPEND_MIN_AGE` blocks old. An LP add declares `lpShares` as a multiple of `LP_BILL_UNIT`, no more than the shares its deposit buys; its LP outputs are bills summing to `lpShares`. Value the floor leaves behind stays in the reserves and accrues to all LPs. |
| R1-e | `DEPOSIT_TERM_SWAP_RECEIVE_XFG` outputs are rejected. A swap pays XFG as ordinary key outputs. |
| R1-f | `TransactionInputCommitmentTransfer` is rejected. |
| R1-g | HEAT that a transaction takes from a consensus-computed source is a whole number of `HEAT_BILL_UNIT` (0.1 HEAT) steps. Limit orders fill only in whole steps (Task V11-7), so their refunds and proceeds have no HEAT dust. LP removal pays the HEAT below a step as XFG at the pool spot price (rule WS-f). |

Each rule lives in one function that both block validation and the mempool call. Blocks below version 11 validate exactly as before.

### Task 1: Changelog and failing reproductions

**Files:** `CHANGELOG.agent.md`; create `tests/CoreTests/HeatRingTests.cpp` and add it to the `core_tests` sources in `src/CMakeLists.txt`.

- [ ] Changelog section "HEAT ring R1 (v11)", Tasks 1–11 as TODO, with the sign-off table.
- [ ] Tests on mainnet parameters, each expected to FAIL on current code:
  - `PlainHeatSpendWithAllHeatTermRingValidates` (H-1).
  - `RealInputIsNotAlwaysNewestRingMember` (H-2): 100 same-bill outputs across 100 heights, the wallet's own at the median; over 200 built rings the real input is the newest member in fewer than half.
  - `NonBillHeatOutputRejectedAtV11` (H-3).
  - `ShortRingRejectedInBlock` (H-4): a block carrying a HEAT spend with ring size 1 is rejected.
  - `DecoyRpcReturnsOnlyEligibleOutputs` (H-5): a bill, a pool marker, an LP output and an immature CD at one amount; only the bill is returned.
  - `NonZeroAmountCommitmentRejectedAtV11` (H-7).
  - `PreV11CommitmentOutputRejectedAsHeatInput` (H-8).
  - `LpShareSpendValidatesWithRingOfOne` (H-9).
  - `LpRemovalPointsAtItsDeposit` (H-9): an LP removal names the LP output it spends; with R1-d it must instead carry 16 same-denomination LP bills.
  - `SwapXfgProceedsAreSpendable` (H-10): swap HEAT for XFG, then spend the XFG received.
- [ ] `make -j$(nproc)`, run `build/release/src/core_tests`. Record which fail. A test that passes drops its finding and the step that fixes it. Checkpoint.

### Task 2: Shared definitions

**Files:** create `src/CryptoNoteCore/HeatRingRules.h` and `.cpp`; modify `src/CryptoNoteConfig.h` (beside `HEAT_BILL_DENOMINATIONS`, line 423), `src/CryptoNoteCore/Blockchain.h`.

**Interfaces (produced):**
- `const uint64_t HEAT_BILL_UNIT = 1000000;` `const uint64_t HEAT_RING_SIZE = 16;` `const uint32_t HEAT_SPEND_MIN_AGE = 10;`
- `bool isHeatBill(uint64_t amount);` — member of `HEAT_BILL_DENOMINATIONS`.
- `uint64_t floorToHeatBillUnit(uint64_t amount);`
- `std::vector<uint64_t> splitIntoHeatBills(uint64_t amount);` — greedy, largest first; requires `amount % HEAT_BILL_UNIT == 0`; returns only bills, never a remainder.
- `bool Blockchain::isEligibleHeatRingMember(const CommitmentOutputRef& ref, uint32_t spendHeight) const;` — rule R1-b. A ref with `transactionIndex.transaction == 0` is a coinbase output.
- `const uint64_t LP_BILL_UNIT = 100000;` `const std::vector<uint64_t> LP_BILL_DENOMINATIONS` = {1, 5} × 10^k shares for k = 5 to 11 (14 bills). Scale: the genesis seed (8,000 XFG / 1,000 HEAT) mints `isqrt(8×10^10 × 10^10)` ≈ 2.83×10^10 shares, so the smallest bill is about 0.028 XFG + 0.0035 HEAT at launch and the largest about 17.7 genesis pools. Two bills per decade, not three, keeps each denomination's set larger (decision P-13).
- `bool isLpBill(uint64_t shares);` `uint64_t floorToLpBillUnit(uint64_t shares);` `std::vector<uint64_t> splitIntoLpBills(uint64_t shares);` — same contract as the HEAT functions.
- `bool Blockchain::isEligibleLpRingMember(const CommitmentOutputRef& ref, uint32_t spendHeight) const;` — rule R1-d.

- [ ] Unit tests in `tests/UnitTests/HeatRingRulesTests.cpp`: `SplitReturnsOnlyBills` for 0.1, 388.6 and 500.0 HEAT; `SplitSumsToInput`; `FloorDropsSubUnitRemainder` (500.0345678 HEAT gives 500.0); `EligibilityRejectsYoungPreV11PoolMarkerLpAndCdOutputs`, one case each; `LpSplitReturnsOnlyLpBills`; `LpEligibilityRejectsHeatBillOfSameAmount`.
- [ ] Implement. Build, run. Checkpoint.

### Task 3: Pre-v11 commitment outputs (H-8) — closed by the owner

The owner states (2026-11-13) that mainnet holds no commitment outputs yet. No scan is needed and nothing is owed. Rule R1-b still excludes any output created below version 11, and `PreV11CommitmentOutputRejectedAsHeatInput` in Task 4 keeps it that way.

### Task 4: Ring validation (R1-b, R1-c, R1-d, R1-f)

**Files:** `src/CryptoNoteCore/Blockchain.cpp:2828-3040` (`checkCommitmentSpendInput`), `:2672-2700` (`checkTransactionInputs`), `src/CryptoNoteCore/Core.cpp:313-338` (`check_tx_mixin`).

**Interfaces (consumed):** `isEligibleHeatRingMember`, `HEAT_RING_SIZE`.

- [ ] In `checkCommitmentSpendInput`, resolve the validating block version at the top (it is resolved only inside the interest branch today). At version 11 and later, classify the ring by its first member's term and apply:
  - HEAT class: `outputIndexes.size() == HEAT_RING_SIZE`; every relative offset after the first is non-zero; every member passes `isEligibleHeatRingMember(ref, currentHeight)`; `claimedInterest == 0`.
  - LP class: `outputIndexes.size() == HEAT_RING_SIZE`; every relative offset after the first is non-zero; every member passes `isEligibleLpRingMember(ref, currentHeight)`; `claimedInterest == 0`. The remove tag's `lpSharesBurned` equals the sum of the LP inputs' amounts.
  - XFG class: rejected.
  - The block at lines 2974-2979 ("all ring members are burned outputs") runs only below version 11.
- [ ] `checkTransactionInputs` rejects `TransactionInputCommitmentTransfer` at version 11 and later.
- [ ] `check_tx_mixin` stops judging commitment-spend ring sizes at version 11 and later; `checkCommitmentSpendInput` owns them and runs for blocks as well as the mempool.
- [ ] Tests: the Task 1 tests for H-1, H-4, H-8, H-9 pass; add `RingOfFifteenRejected`, `RingOfSeventeenRejected`, `RingWithRepeatedMemberRejected`, `RingWithNineBlockOldMemberRejected`, `CommitmentTransferRejectedAtV11`, `PreV11BlocksRevalidate`, `LpRingOfOneRejected`, `LpRingWithHeatBillMemberRejected`, `LpRemoveSharesMustEqualInputSum`.
- [ ] Mutation: drop the member-version condition from `isEligibleHeatRingMember`; `PreV11CommitmentOutputRejectedAsHeatInput` must fail. Restore. Checkpoint.

### Task 5: Output rules (R1-a, R1-e)

**Files:** `src/CryptoNoteCore/Blockchain.cpp:3276-3345` (`check_tx_outputs`).

- [ ] At version 11 and later, for a `TransactionOutputCommitment`:
  - `term` 0 or `HEAT_TERM`: `isHeatBill(out.amount)`.
  - any term: `amountCommitment` and `amountProof` are all zero.
  - `term == DEPOSIT_TERM_SWAP_RECEIVE_XFG`: rejected (remove it from the `validTerm` list at line 3293 for version 11 and later).
  - `term == DEPOSIT_TERM_LP`: `isLpBill(out.amount)`; in an LP-add transaction the LP outputs sum to the tag's `lpShares`, which is a multiple of `LP_BILL_UNIT` (`validateSettlement`, near line 8302).
- [ ] Tests: `NonBillHeatOutputRejectedAtV11`, `NonZeroAmountCommitmentRejectedAtV11`, `SwapReceiveCommitmentRejectedAtV11`, `SwapPayingXfgAsKeyOutputsValidates`, `PoolMarkerOfAnyAmountStillAccepted`, `NonBillLpOutputRejectedAtV11`, `LpAddOutputsMustSumToDeclaredShares`, `LpAddFloorLeavesExcessInReserves`.
- [ ] Mutation, checkpoint.

Cost to know: a commitment output serializes about 330 bytes, of which 288 are these two zeroed fields. A 388.6 HEAT payment is 12 bills, about 4 KB of outputs. Decision P-10 covers a compact output type later.

### Task 6: Consensus-computed HEAT sources (R1-g)

**Files:** `src/CryptoNoteCore/Blockchain.cpp` — `validateSettlement` limit-withdraw branch (8095-8110) and LP-remove branch (8116-8126); the connect paths for both in `pushTransaction` (LP remove near 6825, limit withdraw near 6950); their inverses in `popTransaction`; `Blockchain.h` (`LimitDepositInfo`).

**Interfaces (produced):**
- `void lpRemovalPayout(uint64_t shares, const AmmPoolState& pool, uint64_t& xfg, uint64_t& heat);` — `ammGetWithdrawalAmounts` with `heat` floored to whole steps and the HEAT below the step added to `xfg` at the pool spot price (rule WS-f). `validateSettlement` and the connect path both call it.

- [ ] Limit withdraw: the HEAT owed is `proceedsHeat` (sell order) or the remaining `amount` (buy order). Under Task V11-7 both are whole steps, so `flows.heatSource = owed`; a non-step amount is a consensus defect and rejects the block.
- [ ] LP remove: both sites use `lpRemovalPayout`; reserves are debited by the HEAT paid and by the XFG paid, including the sub-step HEAT paid as XFG.
- [ ] LP add: `m_lpCommitmentShares` / `m_lpCommitTxGidx` (`Blockchain.cpp:6865-6871`, inverse at `:7347-7350`) record only the first LP output of a transaction. An output's amount now is its share count, so both maps are retired and removed from the block-state serializer (`:416`). Testnet reset covers the format change.
- [ ] No rule is added for declared amounts (mint `heatMinted`, swap amounts, limit-deposit escrow, LP-add HEAT, treasury-fund HEAT). With HEAT bills on both sides, exact HEAT balance already forces each to a multiple of the unit; test this instead of coding it.
- [ ] Tests: `LimitWithdrawPaysWholeSteps`, `NonStepLimitProceedsRejectBlock`, `LpRemovePaysSubStepHeatAsXfg`, `PopRestoresLpRemovalPayout`, `MintOfNonUnitHeatFailsBalance`, `SwapWithNonUnitHeatOutputFailsBalance`, and `heatInExistence` (readiness plan Task 0) unchanged across each.
- [ ] Mutation, checkpoint.

### Task 7: Decoy RPC returns only eligible members (H-2, H-5)

**Files:** `src/CryptoNoteCore/Blockchain.cpp:2357-2422`; `src/Rpc/CoreRpcServerCommandsDefinitions.h:342-372`; `src/Rpc/RpcServer.cpp:824`; `src/CryptoNoteCore/ICore.h:87`, `Core.h:156` and its definition; `include/INode.h:85`; `src/NodeRpcProxy/NodeRpcProxy.h:69`, `.cpp:504`; `src/InProcessNode/InProcessNode.h:66, 113` and `.cpp`; `src/WalletLegacy/WalletRequest.h:74`.

**Interfaces (produced):**
- `COMMAND_RPC_GET_RANDOM_COMMITMENT_OUTPUTS_out_entry` gains `uint32_t term; uint32_t height;`.
- `max_height` / `maxHeight` is removed from the request and from every signature listed above. The parameter is the defect; leaving it in place invites its return.
- `bool Blockchain::getRandomCommitmentOutputsForAmount(uint64_t amount, uint64_t count, std::vector<...out_entry>& result);`

- [ ] The request gains `uint8_t ring_class` (0 = HEAT, 1 = LP). The picker builds its candidate list with `isEligibleHeatRingMember` or `isEligibleLpRingMember` at `tipHeight + 1` and samples from that list with the existing triangular bias toward recent outputs. If the list holds no more than `count` entries it returns all of them.
- [ ] Tests: `DecoyRpcReturnsOnlyEligibleOutputs`; `DecoyRpcLpClassReturnsOnlyLpBills`; `DecoyRpcCoversWholeEligibleRange` (over 1,000 calls, entries both older and newer than the median output appear).
- [ ] Mutation: sample from all outputs of the amount; `DecoyRpcReturnsOnlyEligibleOutputs` must fail. Checkpoint.

Superseded in part by Task V11-5: decoys are picked by the wallet, and this task's eligibility filter moves into `/get_output_distribution`. Build Task 7's eligibility rules; do not build the node-side picker.

Limit to know: the triangular bias is a guess. The right distribution is the chain's own spend-age distribution, which cannot be measured before HEAT has traffic. Re-fit it after launch. A remote node also learns which bill denomination a wallet is about to spend.

### Task 8: Wallets build valid, unmarked rings

**Files:** `src/Wallet/WalletGreen.cpp` — `:82` (ring size), `:1364` (splitter), `:1396-1432` (`prepareHeatSpends`), `:507`, `:1679`, `:2118-2123` (single odd-amount outputs), `:2105-2112` (swap-receive commitment), the LP-remove builder; `src/Transfers/TransfersContainer.cpp:1009` (availability); `src/WalletLegacy/WalletTransactionSender.cpp:547, 731, 1832`; `src/Rpc/RpcServer.cpp` and `CoreRpcServerCommandsDefinitions.h` (new read-only endpoint).

**Interfaces (produced):**
- Daemon endpoint `/get_heat_bill_counts` (open, read-only): for each denomination, the number of eligible ring members at the tip.
- `std::vector<uint64_t> chooseHeatBills(uint64_t amount, const std::map<uint64_t, uint64_t>& eligibleCounts);` in the wallet: walk denominations largest first; use a denomination when its count is at least `HEAT_RING_SIZE`, or when the amount still to split is at least `HEAT_RING_SIZE` times the denomination (the transaction then seeds that set itself); the 0.1 bill is always allowed. At most 64 bills per transaction; a larger amount is split across transactions.

- [ ] Every HEAT output any builder creates comes from `chooseHeatBills`. Both private splitters are deleted in favour of `splitIntoHeatBills`.
- [ ] `prepareHeatSpends` requests `HEAT_RING_SIZE + 4` decoys, drops any whose `term` or `height` fails R1-b by the wallet's own check, drops its own output, and takes 15. With fewer than 15 it throws, with the message "this bill cannot be spent yet: N of 16 ring members exist".
- [ ] A HEAT output becomes available `HEAT_SPEND_MIN_AGE` blocks after it is mined.
- [ ] HEAT-to-XFG swaps pay the XFG as key outputs to the wallet's own address, decomposed the way limit-withdraw proceeds already are (`WalletGreen.cpp:1855`).
- [ ] LP add requests `floorToLpBillUnit(shares)` and receives `splitIntoLpBills` outputs. LP removal spends whole LP bills, each in a ring of 16 drawn through the LP class of the decoy RPC, with the same refusal message when fewer than 15 eligible decoys exist.
- [ ] LP removal defaults to one transaction per denomination-sized piece, not one transaction for the whole position, so the remove tag's `lpSharesBurned` is a single bill and cannot be matched against the add tag's total. The user may opt to remove in one transaction, with a warning that the total links it to the deposit.
- [ ] The three CD builders (`:538`, `:832`, `:1049`) are not touched here; Phase 2A removes them.
- [ ] Tests: `RealInputIsNotAlwaysNewestRingMember`; `WalletNeverEmitsNonBill` across send, mint, swap, limit deposit, limit withdraw, LP add and LP remove; `ChooseHeatBillsSeedsAnEmptyDenomination`; `ChooseHeatBillsAvoidsThinDenomination`; `SpendWithFourteenDecoysRefused`; `SwapXfgProceedsAreSpendable`; `LpRemoveBuildsRingOfSixteen`; `LpRemoveDefaultsToOneBillPerTransaction`.
- [ ] Mutation: pass the wallet's own height back as a cap in the picker call; `RealInputIsNotAlwaysNewestRingMember` must fail. Checkpoint.

Limit to know: until 16 eligible bills of a denomination exist, nobody can spend one. The first users of a denomination wait, or seed it themselves; a ring whose other 15 members are the spender's own bills hides nothing from anyone. Rings gain meaning only as other holders arrive.

Limits for LP, stated plainly:
- An LP cannot remove a denomination until 16 eligible LP bills of it exist. At launch, with few LPs, liquidity can be locked until others add. Decoys known to be unspendable (protocol-owned bills) do not help: an observer excludes them.
- Bills created by one LP add are siblings. If the other 15 members of a ring all came from one add, removing one reveals that add. Rings hide the link only once several independent LPs hold the denomination.
- The add tag's amounts and share total stay public. Hidden: which add a removal draws from. Not hidden: that someone added a given size at a given height.
- Removing several bills in one transaction links them to each other, as with HEAT bills.

### Task 9: Vault conversions use the mint price (F-2)

**Files:** `src/CryptoNoteCore/Blockchain.cpp:5306-5460` (Bonus Vault conversion and rate numerator), `:5700-5750` (vault mint).

- [ ] The Bonus Vault conversions (lines 5314 and 5350), the rate numerator (line 5417) and the vault mint (line 5717) all price XFG with `mintPriceAt(height)` from the readiness plan (its Tasks 3 and 9). The recorded numerator is the HEAT minted for that epoch.
- [ ] The other conversions in this function (lines 5489, 5495, 5593, 5630, 5636, 5684) were not read for this plan. Before editing, classify each: pairing the two legs of an LP position keeps the live reserve ratio; converting value from XFG to HEAT uses `mintPriceAt`.
- [ ] Tests: `EpochRateNumeratorEqualsVaultMint`, `BoundarySpotPumpDoesNotRaiseVaultMint`. Mutation, checkpoint.

### Task 10: Documents match the code

- [x] AGENTS.md: tier multipliers removed and the Bonus Vault described as the yield floor (owner, 2026-11-14).
- [ ] AGENTS.md: remove "RingCT" and "Pedersen (RingCT)" from the protocol description; add the R1 rules; record H-1 to H-10 as fixed.
- [ ] `DEPOSIT_ARCHITECTURE.md:415-448`: replace the term-privacy section with Part 1's statement of what a ring hides and what it does not.

### Task 11: R1 gate on the reset testnet

Feeds the readiness plan's Task 18. Each line is an observation on live nodes using mainnet ring rules:

- [ ] A first mint seeds a denomination with at least 16 bills; a second wallet's mint of the same denomination becomes spendable 10 blocks later.
- [ ] Send, receive and re-spend HEAT between two wallets; every transaction relays and confirms.
- [ ] Across 100 HEAT spends, the true input is the newest ring member in fewer than half.
- [ ] Swap both directions, limit deposit and withdraw, LP add and remove: all confirm; every HEAT output on chain is a bill; every LP output is an LP bill; every LP removal carries a ring of 16; XFG proceeds re-spend.
- [ ] `heatInExistence` equals minted HEAT minus burned HEAT on every node.
- [ ] Sign off the changelog table.

---

## Phase V11 — v11 privacy tasks (owner, 2026-11-17)

These tasks close the leaks used most in practice against chains with public amounts. None needs new cryptography. V11-1 changes relay behaviour only. V11-2 and V11-3 add consensus rules at version 11. V11-4 and V11-5 are wallet and RPC work.

Facts read on 2026-11-17 (code-read, not executed):
- No code path that originates a transaction, swap offer or order sets `dandelion_stem`. Every node floods its own messages to all peers on the first hop (`CHANGELOG.agent.md`, section "Investigate order deanonymization"; `docs/V11_LAUNCH_READINESS.md` item 1.10).
- XFG rings: minimum 8 members from version 10 (`MIN_TX_MIXIN_SIZE_V10`), maximum 33. The minimum is checked only when `!keeped_by_block` (`Core.cpp:307`), so a block may carry an XFG spend with a ring of one. This is the XFG form of H-4.
- XFG amounts are split 1–9 × 10^k by wallets only. The coinbase uses powers of ten down to 0.0001 XFG. Consensus enforces no denomination.
- `MINIMUM_FEE` is 0.0008 XFG (`CryptoNoteConfig.h:64`).
- Wallet RPC lets a sender set any `unlockTime` (`WalletGreen.cpp:4138`). Payment IDs in `TX_EXTRA_NONCE` are built by `WalletRpcServer.cpp` and `PoolRpcServer.cpp`. Message (0x04) and TTL (0x05) tags exist.
- XFG decoys are chosen by the node (`Blockchain::getRandomOutsByAmount`, `Blockchain.cpp:2269-2310`), not by the wallet.

### Task V11-1: Dandelion++ origination

**Files:** `src/Rpc/RpcServer.cpp` (transaction submission, near 1697); `src/InProcessNode/InProcessNode.cpp` (near 385); `src/CryptoNoteCore/SwapOfferRelay.cpp` (offers 350-381, orders 911-996); `src/CryptoNoteProtocol/CryptoNoteProtocolHandler.cpp` (402-426, 944-960, 1204-1217); `src/P2p/P2pProtocolDefinitions.h` (332, 495); existing `DandelionStemTest.cpp`.

- [ ] Every `dandelion_stem` field gets a default initializer of `false` or `0`. `COMMAND_ORDER_OPEN::request` has none today, so a default-constructed message would serialize stack bytes.
- [ ] Locally originated transactions and swap offers set the stem flag and go to `externalRelayNotifyToStem`. No message struct gains a field: Levin parsing is positional, and a new field breaks peers that have not upgraded.
- [ ] Order messages: the gossiped order book is retired (AGENTS.md). If no live path still sends these messages, delete their relay code. Otherwise route them the same way.
- [ ] The embargo (`DANDELION_EMBARGO_SECONDS = 30`) fluffs any stem message the node has not seen fluffed in time. A dead or hostile successor then delays a message by at most the embargo and never drops it.
- [ ] Tests: `LocalTransactionStartsInStem`, `LocalSwapOfferStartsInStem`, `StemMessageFluffsAfterEmbargoWhenSuccessorDrops`, `DefaultConstructedMessagesSerializeZeroStem`.
- [ ] Gate on the reset testnet: at least 12 nodes and one spy node connected to all of them. Over 200 transactions, record how often the spy's first-seen peer is the originator. Target below 20% (decision P-17).

Limit: a wallet that uses a remote node hands its transaction straight to that node. Dandelion hides the node's IP, not the wallet's. Wallets need their own node or Task V12-1.

### Task V11-2: XFG bills and fixed rings

Rules at version 11:

| # | Rule |
|---|---|
| X-a | A non-coinbase key output has an amount in `XFG_BILL_DENOMINATIONS`: 1 and 5 × 10^k atomic units, from 0.0001 XFG to 1,000,000 XFG. These are the 21 sizes of the note pool, so v11 bills later cross the bridge one for one. |
| X-b | Every `KeyInput` has exactly `XFG_RING_SIZE` (16) distinct members. Each member is an unlocked key output of the input's amount and at least `HEAT_SPEND_MIN_AGE` blocks old (coinbase outputs: `minedMoneyUnlockWindow`). When fewer than 16 eligible outputs of that amount exist, the ring holds all of them. Checked in blocks as well as the mempool. |
| X-c | The coinbase pays the largest bill sum not above reward plus `m_rewardDustCarry`. The remainder becomes the new carry (block cache and undo record). This moves note-pool Task N6-1 forward to v11. |
| X-d | The fee is at least `MINIMUM_FEE` (0.0008 XFG, unchanged) and a multiple of 0.0001 XFG. |
| X-e | XFG a transaction takes from a consensus-computed source (limit proceeds, LP removal, swap output) is floored to 0.0001 XFG, and the dust stays in `reserveXfg`. Consensus-computed charges paid in XFG (CD creation fee, alias fees) are rounded up to 0.0001 XFG. |

Legacy outputs of any amount stay spendable as inputs under X-b.

**Files:** `src/CryptoNoteCore/Core.cpp:297-350` (the `KeyInput` rule moves into `Blockchain::checkTransactionInputs` so blocks and mempool share it); `Blockchain.cpp` (`check_tx_outputs`, the `KeyInput` branch near 2771, `getRandomOutsByAmount`, `validate_miner_transaction`, block-cache serializer, `popBlock`); `Currency.cpp` (`constructMinerTx`); `CryptoNoteFormatUtils.h:119-175`; every wallet builder that creates key outputs.

**Interfaces (produced),** in the Task 2 file, renamed `RingRules.{h,cpp}`:
- `const uint64_t XFG_BILL_UNIT = 1000;` `const uint64_t XFG_RING_SIZE = 16;` `const std::vector<uint64_t> XFG_BILL_DENOMINATIONS;`
- `bool isXfgBill(uint64_t amount);` `uint64_t floorToXfgBillUnit(uint64_t amount);` `std::vector<uint64_t> splitIntoXfgBills(uint64_t amount);`
- `bool Blockchain::isEligibleXfgRingMember(uint64_t amount, uint32_t globalIndex, uint32_t spendHeight) const;`
- `size_t Blockchain::eligibleXfgCount(uint64_t amount, uint32_t spendHeight) const;`

- [ ] Reproductions first, expected to fail: `XfgRingOfOneInBlockAccepted`, `NonBillXfgOutputAcceptedAtV11`.
- [ ] Tests: `XfgRingOfOneInBlockRejected`, `XfgRingOfFifteenRejectedWhenSixteenExist`, `XfgRareAmountRingMustHoldAllEligible`, `NonBillXfgOutputRejectedAtV11`, `LegacyDigitOutputStillSpendable`, `CoinbasePaysBillsAndCarriesDust`, `PopBlockRestoresRewardDustCarry`, `FeeBelowOrOffUnitRejected`, `LimitWithdrawXfgDustStaysInReserves`.
- [ ] Mutation: restore the `!keeped_by_block` exemption; `XfgRingOfOneInBlockRejected` must fail. Checkpoint.

Costs to accept:
- Two more sizes (0.0001 and 0.0005 XFG) than a 0.001 XFG floor would give, each with its own thin ring set at first (decision P-14). The minimum fee stays 0.0008 XFG.
- Payments need more outputs: 7 XFG becomes 5 + 1 + 1.
- A legacy amount with few outputs on chain gives a small ring. The wallet should spend such outputs early, into bills.
- A rare large bill (1,000,000 XFG) has small rings until many exist. XFG takes the small ring rather than refusing the spend, because refusing would lock legacy funds.

### Task V11-3: One transaction shape

Rules at version 11 (consensus, unless marked otherwise):

| # | Rule |
|---|---|
| T-a | A non-coinbase transaction has `unlockTime == 0`. Before adding the rule, list every builder and reader of `unlockTime` and confirm none is a protocol lock: CD terms, limit expiry and swap timeouts use their own fields. |
| T-b | `extra` holds exactly one transaction public key, then only the tags allowed for the transaction's action, each at most once, in ascending tag order, with no padding. `TX_EXTRA_NONCE` (payment IDs) is rejected (decision P-15). |
| T-c | Message (0x04) and TTL (0x05) tags: decision P-16. |
| T-d | Wallet: at least two outputs. When there is no change, the wallet splits a recipient bill so the count is still two or more. Outputs are shuffled, and every wallet uses one transaction version. |
| T-e | Relay: the fee is the minimum fee per started KB, rounded up to 0.0001 XFG, times a priority level of 1, 4 or 20. The mempool does not relay other fees. |

**Files:** `src/CryptoNoteCore/Blockchain.cpp` (a new `checkTransactionShape`, called from block validation and the mempool); `TransactionExtra.cpp` (`parseTransactionExtra`); `TransactionPool.cpp` (fee); `src/Wallet/WalletGreen.cpp:4138`; `WalletRpcServer.cpp`; `PoolRpcServer.cpp`; `src/CryptoNoteCore/Transaction.cpp`; SimpleWallet.

- [ ] Reproductions first, expected to fail: `NonZeroUnlockTimeAcceptedAtV11`, `PaymentIdNonceAcceptedAtV11`.
- [ ] Tests: `NonZeroUnlockTimeRejectedAtV11`, `ExtraWithNonceRejectedAtV11`, `ExtraOutOfOrderRejected`, `ExtraDuplicateTagRejected`, `ExtraPaddingRejected`, `WalletNeverBuildsSingleOutput`, `ChangePositionIsUniform`, `NonStandardFeeNotRelayed`.
- [ ] Mutation: for each rule, remove it; its test must fail. Checkpoint.

Cost to accept: rejecting payment IDs breaks integrated addresses and the exchange deposit flows that use them. Exchanges must move to subaddresses before the fork, and the change must be announced ahead of the fork height.

### Task V11-4: Wallet input handling

**Files:** `src/Wallet/WalletGreen.cpp` (`selectTransfers` and its callers at 1216, 1484, 1588, 1709, 1829, 3906); `src/WalletLegacy/WalletTransactionSender.cpp`; TUI and SimpleWallet prompts.

- [ ] Input selection takes inputs from the fewest receiving transactions and never combines outputs received on different subaddress accounts.
- [ ] When a payment needs inputs from several sources, the wallet offers to pay in several transactions at random intervals, and asks for confirmation before merging.
- [ ] The wallet shows a linkability warning when a spend uses protocol proceeds (mint, withdraw, swap, LP removal) within `PROTOCOL_FOLLOWUP_WARN_BLOCKS` (proposed 20) of receiving them.
- [ ] Tests: `SelectionPrefersSingleSource`, `SubaddressAccountsNeverMerged`, `MergeNeedsConfirmation`, `QuickFollowUpWarns`. Checkpoint.

Limit: these are wallet defaults. Nothing stops another wallet from merging inputs.

### Task V11-5: Decoys chosen by the wallet

**Files:** `src/CryptoNoteCore/Blockchain.cpp:2233-2422`; `src/Rpc/RpcServer.cpp`, `CoreRpcServerCommandsDefinitions.h`; `include/INode.h`, `NodeRpcProxy`, `InProcessNode`; `WalletGreen.cpp` (`requestMixinOuts`, `prepareHeatSpends`); WalletLegacy.

- [ ] New open RPC `/get_output_distribution`: for an XFG amount, or for a bill and ring class of commitment outputs, the cumulative count of eligible outputs by height.
- [ ] New open RPC `/get_outs_by_index`: the outputs at a list of global indexes for one amount and class.
- [ ] The wallet picks decoys itself. It samples an age from a gamma distribution over log output age (the method of Möser et al., "An Empirical Analysis of Traceability in the Monero Blockchain", PoPETs 2018), maps the age to an index through the distribution, drops ineligible picks, and repeats until it has 15. It then requests all 16 indexes in shuffled order. The node never chooses decoys and never learns which index is real.
- [ ] R1 Task 7's node-side picker is replaced by this. Its eligibility filter moves into `/get_output_distribution`.
- [ ] `getrandom_outs.bin` and `getrandom_outs_json` are removed at v11.
- [ ] Re-fit the parameters to Fuego's own spend ages once a year of v11 traffic exists.
- [ ] Tests: `WalletDecoyAgesFollowDistribution`, `NodeRequestDoesNotRevealRealIndex` (request sets are identically distributed whatever the real member's position), `IneligibleIndexesNeverChosen`, `DistributionRpcMatchesChainState`. Checkpoint.

Limit: the gamma parameters were fitted to Monero's spend behaviour, not Fuego's.

### Task V11-7: Whole-step HEAT fills

The smallest HEAT bill stays 0.1 HEAT, and DIGM is not minted until a later release (owner, 2026-11-18 and 2026-11-19). So nothing smaller than 0.1 HEAT can be paid at v11. Rather than forfeit a remainder, Hearth fills users only in whole 0.1 HEAT steps and leaves the rest of an order unfilled and refundable. Depends on readiness plan Tasks 5–7 (auction application, pool as maker, band pricing), which change the same code.

Rules at version 11:

| # | Rule |
|---|---|
| WS-a | After the call auction finds its clearing price `P*` and matched volume, the matched HEAT volume is floored to whole steps of `HEAT_BILL_UNIT` and allocated to user orders in whole steps, in the existing priority (price, then order id; user orders before pool orders at the same price). Pool orders (`0xF0`) take any atomic remainder. A user selling XFG gives `ceil(steps × HEAT_BILL_UNIT × COIN / P*)` atomic XFG; a user buying XFG receives `floor(...)`. The difference, at most one atomic unit per fill, goes to `reserveXfg`. |
| WS-b | A taker that receives HEAT pays its fee in XFG. A taker that pays HEAT pays its fee in HEAT, inside its whole steps. The CD share of an XFG fee is exchanged for HEAT against the pool at `P*` and credited to `cdHearthFeeAccumulator`. The maker share is paid in the fee's asset. |
| WS-c | The AMM backstop fills user leftovers against the curve in whole HEAT steps. Its XFG leg comes from `ammCostToTake`, rounded in the pool's favour by at most one atomic unit. |
| WS-d | An order whose remaining escrow is worth less than one step at `P*` does not fill in that block. It rests until it fills, expires or is withdrawn, and is then refunded in full. |
| WS-e | HEAT escrow refunds and HEAT proceeds are whole steps by WS-a, so no HEAT dust exists. XFG refunds and proceeds are paid as XFG bills; the part below 0.0001 XFG stays in `reserveXfg` (rule X-e). |
| WS-f | LP removal pays the HEAT leg in whole steps. The HEAT below the step is paid as XFG at the pool spot price; XFG below 0.0001 XFG stays in the reserves. |

Example at the launch price (8 XFG per HEAT): a sell of 0.5 XFG cannot fill, because one step costs 0.8 XFG; it expires and refunds 0.5 XFG. A sell of 2 XFG fills 2 steps (0.2 HEAT) for 1.6 XFG plus a 0.016 XFG fee, and 0.384 XFG stays in the order.

Before coding: read how `OrderbookAuction.cpp` and `validateSettlement` charge the taker fee today, and record it in the changelog. WS-b changes the fee's asset for takers that receive HEAT.

**Files:** `src/CryptoNoteCore/OrderbookAuction.{h,cpp}` (allocation); `src/CryptoNoteCore/Blockchain.cpp` (auction application, backstop, fee routing, `validateSettlement` limit-withdraw and LP-remove branches, their connect and pop paths); `src/CryptoNoteCore/AmmPool.cpp` (`ammCostToTake` rounding); `src/Wallet/WalletGreen.cpp`; `src/SimpleWallet/SimpleWallet.cpp` (`place_order`); TUI swap and order entry.

**Interfaces (produced):**
- `uint64_t wholeHeatSteps(uint64_t heatAtomic);` — the number of `HEAT_BILL_UNIT` steps in an amount.
- `std::vector<UserFill> allocateWholeSteps(const AuctionResult& result, const std::vector<Order>& userOrders);` — WS-a.
- `uint64_t minimumFillableXfg(uint64_t spotPrice);` — one step of HEAT at spot, plus the fee, rounded up to 0.0001 XFG. Used by wallets.

- [ ] Reproduction first, expected to fail: `SmallSellOrderRefundsInFull` (a 0.5 XFG sell at the launch price ends with 0.5 XFG returned, nothing forfeited).
- [ ] Wallet: shows the minimum fillable order and warns below it. Direct AMM swaps quote a whole-step HEAT output and an XFG input rounded up to 0.0001 XFG, and refuse a swap below one step.
- [ ] Tests: `SmallSellOrderRefundsInFull`, `UserFillsAreWholeHeatSteps`, `AuctionConservesHeatAndXfgUnderStepAllocation`, `PoolOrdersAbsorbAtomicRemainder`, `TakerReceivingHeatPaysFeeInXfg`, `XfgFeeCdShareReachesAccumulatorAsHeat`, `BackstopFillsWholeSteps`, `BuyOrderRefundIsWholeSteps`, `LpRemovalPaysSubStepHeatAsXfg`, `PopRestoresStepAllocatedFills`, and `heatInExistence` unchanged across each.
- [ ] Mutation: allocate user fills in atomic HEAT; `UserFillsAreWholeHeatSteps` must fail. Checkpoint.

Costs to accept:
- Nothing worth less than 0.1 HEAT trades at v11: about 0.8 XFG at launch. The minimum falls in XFG terms as XFG gains value against HEAT (0.001 XFG at 100 HEAT per XFG).
- The fee's asset differs by side (WS-b), which adds an exchange step for the CD share of XFG fees.
- Every rounding favours the protocol by at most one atomic unit per fill, or 0.0001 XFG per refund.
- From the v12 pool on, hidden HEAT holds any atomic amount, and Hearth payouts into hidden notes need no whole steps.

### Task V11-6: v11 privacy gate on the reset testnet

- [ ] The Task V11-1 measurement is recorded and meets decision P-17.
- [ ] Every v11 transaction on chain has rings of 16 (or every eligible output, for XFG), only bill outputs, `unlockTime` 0, canonical `extra` and a standard fee.
- [ ] A hostile corpus is rejected by every node: a ring of one inside a mined block, a non-bill output, a payment-ID nonce, a non-zero unlock time, a DIGM output.
- [ ] A 0.5 XFG limit sell expires and refunds 0.5 XFG; a 2 XFG sell fills two whole steps and refunds the rest.
- [ ] Feeds readiness plan Task 18, beside Task 11. Sign off the changelog table.

---

## Phase 1 — HEAT ring options

Phase 0 is option R1. A ring cannot be larger than the number of eligible outputs that exist: at launch every option has thin sets, and no design changes that.

| | Option | Ambiguous set per input | Amounts | New cryptography |
|---|---|---|---|---|
| R1 | Phase 0: wallet-chosen decoys from all eligible same-bill outputs | 16, drawn from every same-bill output | Public | None |
| R2 | Fixed batches: consensus assigns each bill output to a batch; the ring is the whole batch | The batch (16–32), identical for every spend from it | Public | None |
| R3 | Log-size linkable ring signature (Triptych class) over large batches | 1,024–4,096 | Public | New signature, audit |
| R4 | Confidential amounts (Pedersen commitments, range proofs); rings across all HEAT, no bills | 16–32, drawn from every HEAT output | Hidden | Range proofs, MLSAG/CLSAG wiring |
| R5 | Full-set membership proof (Curve Trees class) | Every HEAT output | Hidden, or public with bills | Large |
| R6 | Shielded note pool, HEAT only (the 2026-09-29 STARK design minus XFG) | Every HEAT note | Hidden | Largest; nothing implemented |

Notes:
- **R1** leaves the classic weak point: which decoys a wallet picks. Age-distribution mismatches and ring intersection have traced a large share of spends on other ring-signature chains.
- **R2** removes decoy choice. Every spend from a batch shows the same ring, so there is nothing to compare. Costs: an output is unspendable until its batch closes; a batch that stays short for one epoch merges into the batch before it. Risk: an attacker who creates the other members of a victim's batch knows the victim's spend. Mitigation: batches are formed per 20-block window by a permutation seeded with the window's closing block hash, so membership cannot be chosen at creation. A miner can still grind that hash at the cost of block rewards.
- **R3** is R2 with a signature whose size grows with the logarithm of the batch. It keeps every amount public and reaches thousands. Verification time grows linearly with the batch. Monero studied this class and did not deploy it.
- **R4–R6** hide amounts; see Part 3.
- Outside all of these: the node that first relays a transaction is a separate leak. The changelog records the Dandelion stem phase as disabled.

**Chosen for v11:** R1, implemented by Phase 0. R2 and R3 are targeted at v12 (Phase 3), subject to decision P-18. Hidden HEAT amounts are built inside the post-quantum pool (Phase 3, Task V12-5); R4 is the classical alternative under decision P-19.

---

## Phase 2 — CD exit options

Constraint on all of them: the epoch rate divides fees by total HEAT on deposit, so that total must be public. Under A–E the amount entering a CD is public at creation. B and C also need CD principal split into bills; under A a CD is one public output and is never in a ring.

| | Option | Ambiguous set at exit | Who pays, and when | New cryptography |
|---|---|---|---|---|
| A | Consensus pays the CD out as HEAT bills at maturity; no claim transaction | That of any HEAT bill (Phase 1) | Consensus draws `CD_APY_POOL` and `BONUS_VAULT` at maturity, in output-index order | None |
| B | Share-priced pool: deposits buy shares, redemption pays shares × current price | Every matured CD share bill, all creation epochs and terms | The vault backs the share price; redemption burns shares | None |
| C | Per-epoch coupons with epoch-tagged key images; principal exits separately | Every CD bill locked in that epoch | Vault, per coupon claim | Tagged linkable ring signature |
| D | Confidential HEAT; ring over any matured CD against its public payout value | 16–32 from all matured CDs; amounts hidden afterwards | Vault debit fixed publicly at maturity | Range proofs |
| E | One-of-many proof on top of D | Thousands | As D | Groth–Kohlweiss class |
| F | Shielded note pool with CD terms inside the note | Every note | Vault debit inside the proof; total on deposit needs public bills or shares at the pool boundary | STARK prover and verifier |

Notes:
- **A** deletes the claim path: no `claimedInterest`, no ring over CDs, no over-claim. The CD's existence, size, yield and payout bills stay public; what is hidden is who spends each bill. Costs: no choice of claim time; interest is final at maturity, so either the last partial epoch is not credited or the payout waits for that epoch to close; spending several payout bills in one transaction links them; the wallet must scan for payout keys derived from the CD key.
- **B** makes the payout independent of when the CD was created. Cost: a matured CD keeps earning until redeemed. Stopping accrual at maturity makes the value per-CD again, which is the leak; fixing that needs A's maturity step.
- **C** costs up to 72 claims per bill, each with an XFG fee. Batching claims in one transaction links them and outlines the CD's lifetime. No compounding.
- **D** needs confidential outputs across all of HEAT. The payout value of each CD is public, so only which CD was redeemed is hidden.
- Fixed terms, fixed amounts or monthly cohorts are not listed: they change the bucket size, not the leak.

**Chosen for v12:** A, implemented by Phase 2A, built so D can later replace bills. F remains the long-run direction and needs decision P-5 first.

---

## Phase 2A — Option A for v12: implementation

Option A is the owner's choice for v12 (2026-11-13). A CD is a public, locked output. At maturity consensus pays it out as ordinary HEAT bills in the block's coinbase. There is no claim transaction, no ring over CDs and no `claimedInterest`. Phase 0 must be complete first: the payout is only as private as a HEAT bill spend.

### Rules option A adds at block version 12

`D` is the epoch length in blocks. A CD created at height `h` with term `t` has creation epoch `E = h / D`, maturity epoch `M = (h + t) / D`, and creation-epoch weight `f = ((E + 1) × D − h) / D`: the fraction of epoch `E` it is locked for (1 when created in the epoch's first block).

| # | Rule |
|---|---|
| A-a | A CD output is a commitment output with `term` between `depositMinTerm` and `depositMaxTerm`; amount a multiple of `HEAT_BILL_UNIT`, at least `DEPOSIT_MIN_AMOUNT` (8 HEAT), at most `CD_MAX_OUTPUT_AMOUNT` (5,000 HEAT); zero commitment fields. It is never a ring member and never an input. |
| A-b | Accrual uses one index per epoch, `CD_INDEX_ONE = 10^12` at the v12 activation epoch. A CD earns full-epoch growth from epoch `E + 1`. Its value in epoch `e > E` is `entryValue × index[e] / index[E + 1]`, with `entryValue` from A-d. |
| A-c | When an epoch closes, `index` rises by `inflow × CD_INDEX_ONE / totalShares`. `inflow` is the HEAT minted into `CD_APY_POOL` for that epoch, plus the carry release, plus the yield-floor top-up. |
| A-d | Pro rata in the creation epoch: epoch `E`'s inflow is shared over `m_cdTotalShares` plus each new CD's weight `amount × f × CD_INDEX_ONE / index[E]`. At `E`'s close the CD is credited `firstInterest = floor(amount × f × delta[E] / index[E])` and enters with `entryValue = amount + firstInterest`. It then earns epochs `E + 1` through `M − 1` in full. A CD earns from the block it is created in; nothing is gained by depositing late in an epoch, since its weight shrinks with the blocks left. |
| A-e | Payout value is `floor(entryValue × index[M] / index[E + 1])`, floored again to `HEAT_BILL_UNIT`. The dust joins the carry. |
| A-f | A CD is due at height `h + t + 1` and joins a first-in-first-out queue. A block pays CDs from the head of the queue until the next one would take the block past `CD_PAYOUT_MAX_OUTPUTS_PER_BLOCK` (128) bills. The head CD is always paid, whatever its size. |
| A-g | Payout bills are coinbase outputs with `term = HEAT_TERM`, in queue order, each CD's bills largest first. Bill `k` of a CD with key `P` pays to `P + Hs("fuego-cd-payout" ‖ P ‖ k)·G`. A coinbase's commitment outputs must equal this list exactly. |
| A-h | The XFG reward check sums the coinbase's key outputs only. The bytes of payout outputs do not count toward the block size used for the reward and the size limit. |
| A-i | Interest is drawn from `CD_APY_POOL`. The yield-floor top-up moves from `BONUS_VAULT` to `CD_APY_POOL` when the epoch closes, limited by the Bonus Vault's balance, and reaches every CD through the index. |
| A-j | `TransactionExtraCdBonusClaim` extras are rejected. With R1-b, R1-c and R1-f this retires the whole claim path. |

Consequences to accept:
- The lock runs to the payout block, at least one block past the term, longer if the queue is backed up. Payout bills then follow the coinbase unlock window (60 blocks on mainnet).
- The final partial epoch earns nothing. Today's code credits it, and credits `T + 1` epochs for a `T`-epoch term.
- A CD earns from its creation block, pro rata by blocks locked in that epoch. Today's code pays the creation epoch in full, so a deposit in its last block takes a full share of fees it was not locked for; the weight `f` removes that.
- The tail is not pro rata: blocks between the last boundary and `h + t` earn nothing, because the payout is due at `h + t + 1`, before that epoch's fees are known. A mid-epoch CD with a term of `T` whole epochs earns `T − 1 + f` epochs. Crediting the tail means paying at the next boundary instead (decision P-9).
- Fees collected before v12 reach CDs over about a year, starting 6 epochs after v12. While few CDs exist, each release is shared among few; nothing caps the rate.
- A CD's existence, size, term, yield and payout bills are public. What is hidden is who spends each bill.
- Funds from one CD arrive as sibling bills. Spending several in one transaction links them.

### State added (all in the block cache; all reversed by `popBlock`)

| Member | Meaning |
|---|---|
| `uint64_t m_cdIndexFirstEpoch`, `std::vector<uint64_t> m_cdIndexByEpoch` | `m_cdIndexByEpoch[i]` is the index in force during epoch `m_cdIndexFirstEpoch + i` |
| `uint64_t m_cdTotalShares` | Sum over CDs in full earning of `ceil(entryValue × CD_INDEX_ONE / index[E + 1])` |
| `std::map<uint64_t, std::vector<CdRef>> m_cdCreatedByEpoch` | CDs in their creation epoch, keyed by `E`; entries kept for replay |
| `unsigned __int128 m_cdPartialWeight` | Sum of `amount × ((E + 1) × D − h) × CD_INDEX_ONE / (D × index[E])` over CDs created in the open epoch |
| `std::map<uint64_t, uint64_t> m_cdSharesByMaturityEpoch` | Shares that stop earning when epoch `M − 1` closes; entries are never erased |
| `uint64_t m_cdCarryHeat` | HEAT in `CD_APY_POOL` not yet attributed to any CD |
| `uint64_t m_cdCarryScheduled` | The per-epoch release, fixed when the carry is first set: one seventy-second of it |
| `std::map<uint32_t, std::vector<CdRef>> m_cdPayoutSchedule` | CDs by due height |
| `std::deque<CdRef> m_cdPayoutQueue` | CDs due and not yet paid |
| `std::vector<CdRef> m_blockCdCreated` | CDs created by the block being connected; cleared when the coinbase is pushed |
| `std::deque<std::pair<uint32_t, CdBlockUndo>> m_cdBlockUndo` | One undo record per block, kept for `MAX_ROLLBACK_HISTORY` blocks |
| `CommitmentOutputRef::paidOut` | Set when the CD is paid |

```cpp
struct CdRef {
  uint64_t amount;
  uint32_t globalIndex;      // position in commitmentOutputs()[amount]
  uint32_t creationHeight;
  uint32_t term;
  uint64_t shares;
  Crypto::PublicKey commitKey;
};
struct CdPayout {
  CdRef cd;
  uint64_t paid;             // principal + interest, a multiple of HEAT_BILL_UNIT
  uint64_t interest;
  uint64_t dust;
  std::vector<uint64_t> bills;
};
struct CdBlockUndo {
  uint64_t totalSharesBefore;
  uint64_t carryBefore;
  uint64_t carryScheduledBefore;
  uint64_t feePoolBefore;
  size_t indexCountBefore;
  size_t queueSizeBefore;          // before this block's due CDs were appended
  std::vector<CdRef> created;      // admitted by this block
  unsigned __int128 partialWeightBefore;
  std::vector<CdRef> started;      // moved to full earning at this block's epoch close
  std::vector<CdPayout> paid;      // paid by this block, in order
};
```

Rounding is one-directional: shares round up when added, payouts round down. `CD_APY_POOL` can hold slightly more than it owes, never less.

### Task A-1: Changelog, test access, reproductions

**Files:** `CHANGELOG.agent.md`; create `tests/CoreTests/CdPayoutTests.cpp` (added to `core_tests`); `src/CryptoNoteCore/Blockchain.h` (friend `CdTestAccess`).

**Interfaces (produced):** `class CdTestAccess` with `static uint64_t owedInterest(const Blockchain&)` — the sum, over earning and queued CDs, of payout value minus principal — and `static uint64_t apyPoolBalance(const Blockchain&)`.

- [ ] Changelog section "CD payout at maturity (v12)", Tasks A-1 to A-12 as TODO.
- [ ] Tests against current code, expected to FAIL:
  - `ClaimsNeverExceedEpochInflow` (F-4): two CDs held across three epochs with fees in each; the sum of `calculateCdInterest` over both must not exceed the HEAT minted into the vault. Today it does, because interest compounds on a base the rate's denominator leaves out.
  - `FeesFromEpochWithNoCdsReachLaterCds` (F-3).
- [ ] Build, run, record. Checkpoint.

### Task A-2: Payout key derivation

**Files:** `src/crypto/crypto.h`, `src/crypto/crypto.cpp`; tests in `tests/UnitTests/CdPayoutKeyTests.cpp`.

**Interfaces (produced):**
- `bool derive_cd_payout_key(const Crypto::PublicKey& cdKey, uint32_t billIndex, Crypto::PublicKey& payoutKey);` — false if `cdKey` is not a valid point.
- `void derive_cd_payout_secret(const Crypto::SecretKey& cdSecret, const Crypto::PublicKey& cdKey, uint32_t billIndex, Crypto::SecretKey& payoutSecret);`
- Both use the scalar `hash_to_scalar("fuego-cd-payout" ‖ cdKey ‖ varint(billIndex))`.

- [ ] Tests: `PayoutSecretMatchesPayoutKey` for 1,000 random keys and indexes 0–63; `DifferentBillsGiveDifferentKeys`; `InvalidCdKeyRejected`; `RingSignatureWithPayoutSecretVerifies`.
- [ ] Implement. Mutation: omit `billIndex` from the hash; `DifferentBillsGiveDifferentKeys` must fail. Checkpoint.

### Task A-3: State, persistence, undo

**Files:** `src/CryptoNoteCore/Blockchain.h`, `Blockchain.cpp` (serializer 380-507, reset block 1100-1160, `popBlock`), `src/CryptoNoteCore/IndexManager.h:77-90` (`paidOut`), `src/CryptoNoteConfig.h`.

**Interfaces (produced):**
- Constants: `CD_INDEX_ONE = 1000000000000`, `CD_MAX_OUTPUT_AMOUNT = 5000 × COIN`, `CD_PAYOUT_MAX_OUTPUTS_PER_BLOCK = 128`, `CD_CARRY_RELEASE_EPOCHS = 72`, `CD_CARRY_WARMUP_EPOCHS = 6`.
- `uint64_t Blockchain::cdIndexAt(uint64_t epoch) const;` — `CD_INDEX_ONE` while the series is empty and for any epoch before `m_cdIndexFirstEpoch`; otherwise the stored value.
- `CdBlockUndo& Blockchain::cdUndoFor(uint32_t height);` — creates the block's record on first use.
- `void Blockchain::undoCdBlock(uint32_t height);` — called by `popBlock` before transactions are popped.

- [ ] Add the members, `serialize` for `CdRef`, `CdPayout`, `CdBlockUndo`, and cache entries for every member in the table. Bump the cache version. Reset all of them in the rebuild reset block.
- [ ] `undoCdBlock` restores the scalars, truncates `m_cdIndexByEpoch`, removes each `created` CD from its schedule entry and removes it from `m_cdCreatedByEpoch`, restores `m_cdPartialWeight`, subtracts each `started` CD's shares from `m_cdTotalShares` and `m_cdSharesByMaturityEpoch`, clears `paidOut` and re-queues each `paid` CD at the front in reverse order, and cuts the queue back to `queueSizeBefore`.
- [ ] Tests: `RestartKeepsCdState` (store, reload, compare every member); `UndoOfEmptyBlockChangesNothing`. Checkpoint.

### Task A-4: Creation rules and admission (A-a)

**Files:** `src/CryptoNoteCore/Blockchain.cpp:3276-3345` (`check_tx_outputs`), `:6712-6726` (commitment-output branch of `pushTransaction`), `processBlockEpochWork` (5263, last step), `src/CryptoNoteCore/Currency.cpp:354` (`cdLastCreditedEpoch`).

**Interfaces (produced):** `void Blockchain::admitCreatedCds(uint32_t height);`

- [ ] `check_tx_outputs` at version 12: a CD-term output obeys rule A-a.
- [ ] `pushTransaction` appends a `CdRef` (shares 0) to `m_blockCdCreated` for each CD output. It does no share arithmetic.
- [ ] `admitCreatedCds` runs as the last step of `processBlockEpochWork`, so a CD created in a boundary block is priced at the new epoch's index. For each created CD: append to `m_cdPayoutSchedule[h + t + 1]` and to `m_cdCreatedByEpoch[E]`, add its weight to `m_cdPartialWeight`, and record it in the block's undo. A CD in a boundary block belongs to the new epoch with `f = 1`.
- [ ] `cdLastCreditedEpoch` returns `M − 1` for a creation height at version 12 or later, so the existing epoch report drops a CD from `m_heatOnDeposit` at the same boundary.
- [ ] Tests: `CdAmountNotMultipleOfUnitRejected`, `CdAboveMaxOutputAmountRejected`, `CdBelowMinimumRejected`, `CdCreatedInBoundaryBlockPricedAtNewIndex`, `CdCreatedInBoundaryBlockEarnsFullEpoch`, `CdCreatedMidEpochEarnsBlockFraction`, `LastBlockDepositEarnsOneBlockOfEpoch`, `PopBlockRemovesCreatedCdAndWeight`.
- [ ] Mutation: price a boundary-block CD at the old index; `CdCreatedInBoundaryBlockPricedAtNewIndex` must fail. Checkpoint.

### Task A-5: Epoch close (A-b, A-c, A-i)

**Files:** `src/CryptoNoteCore/Blockchain.cpp` — `processBlockEpochWork`, directly after the v11+ vault mint (near 5730-5745).

**Interfaces (produced):** `void Blockchain::closeCdEpoch(uint32_t height, uint64_t mintedHeat, const Crypto::Hash& blockHash, const Crypto::Hash& coinbaseHash);`

Steps of `closeCdEpoch`, in this order, with `e` the epoch being closed:
1. At the first boundary at version 12: `m_cdIndexFirstEpoch = e`, `m_cdIndexByEpoch = {CD_INDEX_ONE}`, and `m_cdCarryHeat` is the `CD_APY_POOL` partition balance minus `mintedHeat`. Everything collected before CDs existed is carry. `m_cdCarryScheduled = ceil(m_cdCarryHeat / CD_CARRY_RELEASE_EPOCHS)`.
2. `weight = m_cdTotalShares + m_cdPartialWeight`. `totalValue = weight × cdIndexAt(e) / CD_INDEX_ONE`.
3. If `weight == 0`: `m_cdCarryHeat += mintedHeat`, append `cdIndexAt(e)` unchanged, go to step 8.
4. `carryRelease` is zero for the first `CD_CARRY_WARMUP_EPOCHS` epochs of the series. After that it is `min(m_cdCarryHeat, max(m_cdCarryScheduled, m_cdCarryHeat / CD_CARRY_RELEASE_EPOCHS))`. Nothing caps the rate this produces; yield is never limited.
5. `floorTarget = totalValue × CD_YIELD_FLOOR_RATE / FEE_POOL_RATE_PRECISION`. `topUp` is zero when `mintedHeat + carryRelease ≥ floorTarget`; otherwise the shortfall, limited by `m_bonusVaultBalance` and by the `BONUS_VAULT` partition balance. Move `topUp`: spend it from `BONUS_VAULT`, add a `CD_APY_POOL` UTXO, record the spent indices under `m_vaultSpentByTx[coinbaseHash]`, and adjust both counters.
6. `inflow = mintedHeat + carryRelease + topUp`. `delta = inflow × CD_INDEX_ONE / weight`. Append `cdIndexAt(e) + delta`.
7. `m_cdCarryHeat = m_cdCarryHeat − carryRelease + (inflow − Σ interest credited this epoch)`, the sum taken over full shares and every `firstInterest`, each floored.
8. `m_cdTotalShares −= m_cdSharesByMaturityEpoch[e + 1]`.
9. For each CD in `m_cdCreatedByEpoch[e]`: `firstInterest = floor(amount × f × delta / cdIndexAt(e))`, `shares = ceil((amount + firstInterest) × CD_INDEX_ONE / cdIndexAt(e + 1))`, added to `m_cdTotalShares` and `m_cdSharesByMaturityEpoch[M]`; recorded in the block's undo as `started`. Then `m_cdPartialWeight = 0`. The entry is kept, so a replacement block re-starts the same CDs.

- [ ] All 128-bit intermediates use `uint128_t`. The append saturates at `UINT64_MAX`; that needs more than a 10^7-fold rise and is not reachable.
- [ ] Tests: `IndexRisesByInflowPerShare`, `BoundaryCdWithSixEpochTermEarnsSixIncrements`, `HalfEpochCdEarnsHalfOfCreationEpoch`, `PartialWeightDilutesFullSharesExactly`, `MaturedCdStopsEarning`, `EpochWithNoCdsCarriesItsFees`, `CarryReleaseStartsAfterWarmup`, `StartingCarryIsReleasedOverSeventyTwoEpochs`, `CarryReleaseIsNotCappedByDepositSize`, `V11FeesBecomeCarryAtActivation`, `YieldFloorTopUpComesFromBonusVaultAndIsCapped`, `ApyPoolNeverBelowOwedInterest` (property test over random deposits, fees and maturities), `PopAcrossBoundaryRestoresIndexAndShares`, `PopAcrossBoundaryRestoresPartialWeightAndStartedCds`.
- [ ] Mutation: round shares down in Task A-4; `ApyPoolNeverBelowOwedInterest` must fail. Mutation: set `f = 1` for every CD; `LastBlockDepositEarnsOneBlockOfEpoch` must fail. Checkpoint.

### Task A-6: Payout list and coinbase validation (A-e, A-f, A-g, A-h)

**Files:** `src/CryptoNoteCore/Blockchain.cpp` — new functions; `pushBlock` (after the `prevalidate_miner_transaction` call, and the coinbase size at 3718); `validate_miner_transaction` (1858); `src/CryptoNoteCore/CryptoNoteFormatUtils.{h,cpp}` (`check_outs_valid`, sums).

**Interfaces (produced):**
- `std::vector<CdPayout> Blockchain::cdPayoutsForHeight(uint32_t height) const;` — candidates are the queue followed by `m_cdPayoutSchedule[height]`; rule A-f selects; rule A-e values each. It reads only state from before the block.
- `std::vector<TransactionOutput> Blockchain::cdPayoutOutputsForHeight(uint32_t height) const;` — rule A-g.
- `uint64_t getXfgOutputAmount(const Transaction& tx);` — sum of outputs that are not commitment outputs.
- `size_t coinbaseSizeForLimits(const Transaction& baseTransaction);` — serialized size minus the serialized size of its commitment outputs.
- `bool Blockchain::validateCdPayoutOutputs(const Block& block, uint32_t height) const;`

- [ ] If `CD_APY_POOL` holds less than a payout's interest, the payout is `amount + floorToHeatBillUnit(available)` and an error is logged. This cannot happen while the solvency property holds; the rule exists so that nodes still agree if it ever does.
- [ ] `validateCdPayoutOutputs` at version 12: the coinbase's commitment outputs, in order, equal `cdPayoutOutputsForHeight(height)` in amount, key and term, with zero commitment fields. `pushBlock` calls it right after `prevalidate_miner_transaction` and rejects the block on a mismatch. It is not placed inside `prevalidate_miner_transaction`: that function also runs for alternative blocks (line 2090), whose expected payouts depend on a chain state the node does not hold until it switches. `check_outs_valid` accepts `HEAT_TERM` commitment outputs in a base transaction. Below version 12 nothing changes.
- [ ] `validate_miner_transaction` uses `getXfgOutputAmount` for `coinbaseTotal` in every branch, the checkpoint-zone branch included. Review every other caller of `get_outs_money_amount` and `getOutputAmount` that is handed a base transaction and switch those that mean XFG.
- [ ] `pushBlock` and the rebuild replay use `coinbaseSizeForLimits`.
- [ ] Tests: `CoinbaseMissingPayoutRejected`, `CoinbaseExtraHeatOutputRejected`, `CoinbasePayoutWrongKeyRejected`, `CoinbasePayoutWrongOrderRejected`, `XfgEmissionExcludesPayoutBills`, `PayoutQueueCarriesOverAtCapacity`, `OversizedHeadCdPaidAlone`, `PayoutEqualsPrincipalTimesIndexRatioFlooredToUnit`, `PayoutBytesDoNotReduceBlockReward`, `AlternativeBlockWithPayoutsPassesPrevalidation`.
- [ ] Mutation: sum all coinbase outputs in `validate_miner_transaction`; `XfgEmissionExcludesPayoutBills` must fail. Checkpoint.

### Task A-7: Settlement, reversal, replay

**Files:** `src/CryptoNoteCore/Blockchain.cpp` — `processBlockEpochWork` (first step), `popBlock`, the rebuild replay loop (1245-1290).

**Interfaces (produced):** `void Blockchain::settleCdPayouts(uint32_t height, const Crypto::Hash& coinbaseHash);`

- [ ] `settleCdPayouts` runs as the first step of `processBlockEpochWork`, before the epoch closes. It first creates the block's undo record with the scalar values and `queueSizeBefore`, and computes `cdPayoutsForHeight(height)`. It then appends `m_cdPayoutSchedule[height]` to the queue (the schedule entry is kept, so a replacement block at this height appends the same CDs), and for each payout: sets `paidOut` on the CD's output ref; spends `interest` from `CD_APY_POOL` with `spendUtxos`, returns the overshoot with `mintVaultChangeUtxo`, and records the spent indices under `m_vaultSpentByTx[coinbaseHash]`; subtracts `interest` from `m_feePoolBalance`; adds `dust` to `m_cdCarryHeat`; pops the queue head; records the payout in the block's undo.
- [ ] Schedule entries, `m_cdCreatedByEpoch` entries and `m_cdSharesByMaturityEpoch` entries more than `MAX_ROLLBACK_HISTORY` blocks below the tip are erased.
- [ ] `popBlock` calls `undoCdBlock` before popping transactions. The vault un-spend already runs when the coinbase is popped (`popTransaction`, line 7252), and `m_vault.removeAboveIndex` already removes the change UTXOs.
- [ ] Because all three steps (`settleCdPayouts`, `closeCdEpoch`, `admitCreatedCds`) sit inside `processBlockEpochWork`, the rebuild replay runs them with no further change. Confirm by test.
- [ ] Tests: `PaidCdIsMarkedAndLeavesQueue`, `InterestLeavesApyPoolExactly`, `PopBlockRestoresCdPayoutState`, `RebuildMatchesLiveCdState`, `ReorgAcrossPayoutBlockConverges`, `HeatInExistenceUnchangedByPayout`.
- [ ] Mutation: skip the `m_feePoolBalance` restore in `undoCdBlock`; `PopBlockRestoresCdPayoutState` must fail. Checkpoint.

### Task A-8: Block template

**Files:** `src/CryptoNoteCore/Currency.cpp:715` (`constructMinerTx`), `src/CryptoNoteCore/Core.cpp:625-770` (`get_block_template`), `src/CryptoNoteCore/ICore.h`, `Core.h`.

- [ ] `constructMinerTx` takes `const std::vector<TransactionOutput>& protocolOutputs` and appends them after the reward outputs. `get_block_template` passes `cdPayoutOutputsForHeight(height)` and computes sizes with `coinbaseSizeForLimits`.
- [ ] Tests: `TemplateCoinbaseValidates` on a block with three due CDs; `TemplateWithNoDueCdsIsUnchanged`. Checkpoint.

### Task A-9: Retire the claim path (A-j)

**Files:** `src/CryptoNoteCore/Blockchain.cpp` (bonus-claim checks near 3998-4027 and 6423-6434); `src/Wallet/WalletGreen.cpp:466-600` (withdrawal), `:719-924` (rollover), `:1049` region, `capInterestByPool`; the equivalents in `src/WalletLegacy/WalletTransactionSender.cpp`; `INode::getCdClaimInfo` and its implementers; `src/Rpc/RpcServer.cpp` (`estimateCdClaim` handler near 2841); `src/PaymentGate/` and `src/Wallet/WalletRpcServer.cpp` methods that withdraw or roll a CD.

- [ ] Consensus at version 12: a transaction carrying `TransactionExtraCdBonusClaim` is rejected. The interest and bonus validation code stays for blocks below version 12.
- [ ] Wallets: remove the CD withdrawal and rollover builders and the claim-estimate plumbing. The wallet methods for withdrawing a CD return "this CD pays out automatically at block N". A rollover is a new deposit made with the bills received.
- [ ] Tests: `BonusClaimExtraRejectedAtV12`, `CdOutputNeverAcceptedAsRingMember`, `WalletWithdrawCdReportsPayoutHeight`. Checkpoint.

### Task A-10: Wallet finds and spends payout bills

**Files:** `src/Transfers/TransfersConsumer.cpp:102, 507-521`; `src/Transfers/` subscription interfaces; `src/Wallet/WalletGreen.cpp` (`resolveCommitmentSpendKey`, CD creation near 1961, deposit tracking); `src/CryptoNoteCore/TransactionExtra.cpp` (`resolveCommitmentSpendKeys`); the WalletLegacy equivalents.

**Interfaces (produced):**
- `void addExpectedCommitmentKey(const Crypto::PublicKey& key, const Crypto::PublicKey& cdCommitKey, uint32_t billIndex);` on the transfers consumer. A commitment output in a base transaction whose key matches is reported as owned, tagged with the CD key and bill index.

- [ ] When the wallet records an owned CD it registers payout keys for bill indexes 0–31. Each time a match arrives within 8 of the highest registered index it registers 32 more. Bills are ordered largest first, so indexes are dense from 0.
- [ ] `resolveCommitmentSpendKey` gains the payout case: `derive_cd_payout_secret(cdSecret, cdCommitKey, billIndex)`.
- [ ] CD creation builds outputs obeying rule A-a; a deposit above 5,000 HEAT becomes several CD outputs.
- [ ] The wallet shows each CD's due height and, once paid, the bills received.
- [ ] Tests: `WalletDetectsPayoutBills`, `WalletSpendsPayoutBill` (the spend validates under R1), `WalletFindsPayoutBillsAfterRestoreFromSeed`, `PayoutWithFortyBillsFullyDetected`, `ForeignCoinbaseCommitmentIgnored`.
- [ ] Mutation: register only index 0; `PayoutWithFortyBillsFullyDetected` must fail. Checkpoint.

### Task A-11: RPC and documents

**Files:** `src/Rpc/RpcServer.cpp`, `src/Rpc/CoreRpcServerCommandsDefinitions.h`; `AGENTS.md`; `DEPOSIT_ARCHITECTURE.md`; `docs/api-reference/`.

- [ ] `/get_cd_status` (open): by CD key, returns state (earning, queued, paid), amount, creation height, term, current value, due height, and for a paid CD its payout height and bills.
- [ ] `/get_cd_index` (open): the index series, total shares, carry, queue length and `CD_APY_POOL` balance, so anyone can recompute every payout.
- [ ] AGENTS.md and `DEPOSIT_ARCHITECTURE.md`: replace the claim model with rules A-a to A-j; state that CD interest is paid by `CD_APY_POOL`, the floor by `BONUS_VAULT`, and nothing by AuntPoL.
- [ ] Checkpoint.

### Task A-12: v12 gate on the reset testnet

- [ ] Create CDs of 6 and 18 epochs from two wallets; one of them in an epoch-boundary block.
- [ ] At each due height the coinbase carries the expected bills; both wallets see them; a wallet restored from seed sees the same bills.
- [ ] `CD_APY_POOL` balance is never below `owedInterest` on any node, and equals the carry once every CD is paid.
- [ ] Restart one node inside an epoch and reorg three blocks across a payout block; all nodes converge.
- [ ] Spend payout bills under R1; over 100 such spends the true input is the newest ring member in fewer than half.
- [ ] XFG emission per block equals the reward schedule in blocks that carry payouts.
- [ ] Sign off the changelog table.

---

## Phase 3 — v12 privacy tasks (owner, 2026-11-17)

Scope set by the owner: Tor/I2P transport, R2 batch rings, R3 log-size rings, the post-quantum note pool, and private HEAT amounts between wallets, all targeted at v12 alongside option A for CDs.

Stated plainly:
- This makes v12 the largest release in these plans by far. The pool alone is a new proof system, a new crate, a new transaction family, a new wallet key scheme and a bridge. Its benchmark gate (pool plan Task N0-3) and its audits set its date, not the version number. If its gates are not met, v12 ships without the pool and without private HEAT. The gates are never lowered to hold the date.
- R2 and R3 are classical ring designs for legacy-format outputs. Once the pool is active, no legacy output can be created, so R2 and R3 protect only spends into the bridge. If the pool makes v12, most of the R2 work and all of the R3 work goes into a format that ends at migration. Recommended: build R2 only if the pool misses v12, and R3 only if the pool is abandoned (decision P-18).
- Private HEAT amounts (Task V12-5) are built inside the pool and cannot ship without it, unless decision P-19 picks the classical route.

### Task V12-1: Tor and I2P transport

**Files:** `src/P2p/NetNode.{h,cpp}`, `NetNodeConfig.{h,cpp}`, `NetworkAddressTypes.{h,cpp}`; daemon options; `src/NodeRpcProxy/` (wallet-to-daemon connection).

- [ ] Daemon option: send the node's own transactions and swap offers only through a SOCKS5 proxy to Tor or I2P peers. Option: accept inbound peers on an onion or I2P address.
- [ ] Wallet option: reach its daemon through the proxy.
- [ ] When the proxy fails, the node holds its own messages. It never falls back to clearnet.
- [ ] Tests: `OwnTransactionsLeaveOnlyThroughProxy`, `ProxyFailureDoesNotFallBackToClearnet`, `OnionPeerAddressRoundTrips`. Checkpoint.

Limit: opt-in. It protects only users who turn it on, which is why Task V11-1 ranks above it.

### Task V12-2: R2 fixed batch rings (subject to P-18)

- [ ] Rules as in Phase 1, note R2: bill outputs (XFG, HEAT, LP) are assigned to batches per 20-block window by a permutation seeded with the window's closing block hash. A batch holds 16 to 32 outputs, a short batch merges into the one before it, and the ring is the whole batch. An output is spendable once its batch closes.
- [ ] Tests: `BatchMembershipFixedByClosingHash`, `ShortBatchMergesIntoPrevious`, `RingMustEqualBatch`, `OutputUnspendableBeforeBatchCloses`, `PopBlockRestoresBatches`. Mutation, checkpoint.

Risk: a miner can grind the closing hash at the cost of block rewards.

### Task V12-3: R3 log-size rings (subject to P-18)

- [ ] A linkable ring signature of Triptych class over R2 batches enlarged to 1,024–4,096 members. Specification and independent audit before any consensus code.
- [ ] Tests: soundness and linkability vectors; `KeyImageFromBatchMemberDetectsDoubleSpend`; verification-time bound per batch size.

Cost: a new signature scheme to audit; verification time grows linearly with the batch. Monero studied this class and did not deploy it.

### Task V12-4: Post-quantum note pool

- [ ] Implemented by `docs/plans/2026-09-29-v11-pq-shielded-note-pool.md`, Phases N0–N10, targeted at v12. Tasks N0-2 and N0-3 start as soon as the hold is lifted.

### Task V12-5: Private HEAT between wallets

- [ ] Implemented by pool plan section 2.8: one hidden-amount HEAT class inside the pool. Amounts are hidden for transfers between wallets. HEAT entering or leaving a protocol action (mint, burn, Hearth, LP, CD) is public at that boundary. A turnstile counter keeps the hidden float from going negative.
- [ ] A CD then pays out as one HEAT note of its exact value, with no sibling bills (pool plan Task N7-5).

Why not classical hidden amounts (Pedersen commitments with Bulletproofs): their binding rests on discrete log. A quantum computer can open a commitment to a different amount and mint HEAT that no one can see. That conflicts with the post-quantum goal. They are faster to build and do not need the pool (decision P-19).

What it costs: the integrity of hidden HEAT rests on the proof. The turnstile bounds a forgery to the hidden float, which may be most of the HEAT in existence, and reveals it only when exits would exceed the float.

---

## Part 3 — Effect on public supply auditing

| Options | Amounts | What "anyone can count the supply" becomes |
|---|---|---|
| Phase 0, R1–R3, CD A–C | Public | Unchanged. Every output, burn, mint and vault balance is countable |
| R4, CD D–E | Hidden for HEAT | HEAT supply is public mints minus public burns, correct only if the proofs and their implementation are correct. A flaw mints HEAT that no observer can see |
| R5, R6, CD F | Hidden | The same, with a larger proof system. A STARK needs no trusted setup and rests on hash security, which removes the two worst supply risks of other proof systems |

- This codebase's unbacked-HEAT paths (readiness plan C2 and C3, and those recorded in the changelog) were found by reading public accounting. Under hidden amounts the same class of defect leaves no trace on chain.
- Precedent: the Zcash Sprout counterfeiting flaw was found in 2018, fixed in Sapling and disclosed in 2019. Zcash later added a consensus rule that a shielded pool's total balance may never go negative.
- If hidden amounts are adopted, the conservative shape is: XFG stays publicly countable; only HEAT is hidden; and a turnstile counter bounds every public sink (pool reserves, CD deposits, burns) so that HEAT arriving from hidden outputs can never exceed HEAT that entered them. A flaw is then limited to the hidden float and is detected when the counter would go negative.
- XFG stays out of every shielded pool and every hidden-amount output (owner, 2026-11-14). The 2026-09-29 STARK design (`docs/plans/2026-09-29-v11-pq-shielded-note-pool.md`) puts XFG and HEAT in one pool; that part is rejected. That file was rewritten on 2026-11-15 as a post-quantum note pool with public bill amounts for XFG, HEAT and LP shares (XFG bills 1 and 5 × 10^k, 0.0001 to 1,000,000 XFG). On 2026-11-17 the owner targeted it at v12 and added hidden HEAT amounts between wallets inside it (Phase 3). R6 and CD F as hidden-amount designs, if ever built, hold HEAT only.

---

## Decisions for the owner

| # | Decision | Status or default |
|---|---|---|
| P-1 | HEAT ring for v11 | Chosen 2026-11-13: R1 (Phase 0). R2 and R3 targeted at v12 (owner, 2026-11-17), subject to P-18 |
| P-2 | Ring size | 16 |
| P-3 | Spend-age floor | 10 blocks; coinbase outputs follow the 60-block mined-money window |
| P-4 | CD exit for v12 | Chosen 2026-11-13: A (Phase 2A) |
| P-5 | Hidden amounts | Never for XFG (owner, 2026-11-14; confirmed 2026-11-19 after review of hiding XFG everywhere except at public pools, which would make the XFG supply count depend on the proof). HEAT transfers between wallets hidden at v12, inside the post-quantum pool, with a turnstile (owner, 2026-11-17) |
| P-6 | Pre-v11 commitment outputs | Closed 2026-11-13: the owner states mainnet holds none. R1-b keeps any pre-v11 output out of HEAT rings regardless |
| P-7 | CD principal | One public output per CD, a multiple of 0.1 HEAT, at most 5,000 HEAT. Not a bill: under A it is never in a ring |
| P-8 | Accrual | Per-epoch index: compounding, and solvent by construction. Alternative: keep per-epoch rates with simple interest on principal |
| P-9 | Credited epochs and due height | Creation epoch pro rata by blocks, then `E + 1` to `M − 1` in full (owner, 2026-11-14); due at `h + t + 1`. Open: credit the tail too by paying at the first boundary after `h + t` |
| P-10 | The 288 unused bytes on every commitment output | Required to be zero now; a compact HEAT output type later |
| P-11 | Payout capacity | 128 bills per block; the head CD is always paid |
| P-12 | Fees collected before any CD exists | Carry. Nothing is released for the first 6 epochs after v12; then one seventy-second of the starting carry per epoch. No cap on the resulting rate (owner, 2026-11-13). Alternative: all of it to the first epoch's depositors |
| P-13 | LP share spends | Chosen 2026-11-14: LP bills, {1, 5} × 10^k shares, rings of 16 (R1-d). Hides the add-to-remove link; position size at add stays public |
| P-14 | Smallest XFG bill at v11 | Chosen 2026-11-18: 0.0001 XFG, matching `DEFAULT_DUST_THRESHOLD` and the smallest coinbase piece. The minimum fee stays 0.0008 XFG |
| P-15 | Payment IDs at v11 | Default: rejected. Exchanges move to subaddresses before the fork |
| P-16 | Message (0x04) and TTL (0x05) tags | Open. Keeping them marks every transaction that uses them. Rejecting them removes the feature. A fixed-size message slot in every transaction hides use and costs bytes on every transaction |
| P-17 | Dandelion gate | Default: the spy node identifies the originator in under 20% of 200 testnet transactions |
| P-18 | R2 and R3 when the pool targets v12 | Recommended: R2 only if the pool misses v12; R3 only if the pool is abandoned |
| P-20 | Amounts below 0.1 HEAT at v11 | Chosen 2026-11-19: whole-step fills (Task V11-7). Smallest HEAT bill stays 0.1 HEAT. DIGM is not minted at v11 and is not used for small change |
| P-21 | Fee asset under whole-step fills | Default: a taker receiving HEAT pays in XFG, a taker paying HEAT pays in HEAT (rule WS-b) |
| P-19 | How private HEAT is built | Default: inside the post-quantum pool (needs the pool). Alternative: classical Pedersen commitments and Bulletproofs, faster to build, but a quantum computer can inflate HEAT unseen |
