# HEAT Ring and CD Privacy Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make a HEAT spend valid and private on mainnet, and replace the CD interest claim with an exit that does not identify the CD, while yield still comes only from fees.

**Architecture:** Phase 0 proves and fixes defects that need no design choice. Phases 1 and 2 are option lists, each with one recommendation; the owner picks, and the chosen option is then written out as its own task list. Part 3 states what each option does to public supply auditing.

**Tech Stack:** C++17, CMake/Make, existing binaries `core_tests`, `test_hearth_amm`.

**Spec:** owner direction of 2026-11-13: rings limited to a CD's own amount, term and creation epoch are rejected, and so is a HEAT ring whose only ambiguous members are outputs from the spender's own block. Companion plan: `docs/plans/2026-11-12-v11-readiness-fix-plan.md`; its end-to-end gate (Task 18) depends on Phase 0 here.

**Evidence:** every statement in Part 1 is code-read on 2026-11-13. Nothing was executed. Phase 0 Task 1 turns each into a test that must fail on current code; a test that passes drops its finding and the task that fixes it.

## Global Constraints

- Yield comes only from fees paid. No inflation, and no burn-derived value, funds a CD.
- A commitment output created before v11 is never spendable as HEAT.
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

---

## Phase 0 — Defects with no design choice

### Task 1: Reproduce

**Files:** create `tests/CoreTests/HeatRingTests.cpp` (added to the `core_tests` sources in `src/CMakeLists.txt`); `CHANGELOG.agent.md`.

- [ ] Changelog section "HEAT ring and CD privacy", all tasks TODO.
- [ ] Tests, each expected to FAIL on current code:
  - `PlainHeatSpendWithAllHeatTermRingValidates` (H-1): mint at least 8 bills of one denomination, spend one through `checkTransactionInputs`.
  - `RealInputIsNotAlwaysNewestRingMember` (H-2): 100 same-bill outputs across 100 heights, the wallet's output at the median; build 200 rings; the real input is newest in fewer than half.
  - `DecoyRpcReturnsOnlyEligibleOutputs` (H-5): a bill, a pool marker, an LP output and an immature CD at one amount; only the bill comes back.
  - `NonBillHeatOutputRejectedAtV11` (H-3).
  - `ShortRingRejectedInBlockOnMainnetParams` (H-4).
  - `PreV11CommitmentOutputRejectedAsHeatInput` (H-8): a v10 burn-deposit output, then v11, then a spend naming it.
  - `EpochRateTimesLockedEqualsVaultMint` (F-2); `FeesFromEpochWithNoCdsReachLaterCds` (F-3).
- [ ] Build, run `core_tests`. Record which fail. Checkpoint.

### Task 2: A HEAT spend validates; a pre-v11 output never does (H-1, H-8)

**Files:** `src/CryptoNoteCore/Blockchain.cpp:2974-2979`, `:3234-3238`, the member loops above them.

- [ ] First, a mainnet scan: count and sum commitment outputs below the v11 height, by term. If any exist, the owner decides what they are owed (decision P-6) before this task merges.
- [ ] At block version 11 and above: the all-`HEAT_TERM` rejection no longer applies, and every ring member of a HEAT-class commitment spend or transfer must sit in a block whose major version is at least 11. Pre-v11 blocks validate as before.
- [ ] Tests from Task 1 pass; `PreV11BlocksRevalidate`. Mutation: drop the member-version rule, `PreV11CommitmentOutputRejectedAsHeatInput` fails. Checkpoint.

### Task 3: Eligible decoys, sampled across the whole set (H-2, H-5)

**Files:** `src/CryptoNoteCore/Blockchain.cpp:2357-2422`; `COMMAND_RPC_GET_RANDOM_COMMITMENT_OUTPUTS` in `src/Rpc/CoreRpcServerCommandsDefinitions.h`; `src/NodeRpcProxy/NodeRpcProxy.cpp:504`; `src/Wallet/WalletGreen.cpp:538, 832, 1049, 1409`; the equivalents in `src/WalletLegacy/WalletTransactionSender.cpp`; `src/CryptoNoteConfig.h`.

**Interfaces:**
- Produces: `const uint32_t HEAT_SPEND_MIN_AGE = 10;` — a HEAT output younger than this is neither spendable nor a decoy (consensus rule at v11+), so a real input is never fresher than every possible decoy.
- Produces: request field `asset_class`; response entries gain `term` and `height`.

- [ ] The node returns only outputs of the requested asset class that are spendable at the next block (created at v11 or later, older than `HEAT_SPEND_MIN_AGE`, not a pool marker, and matured if a CD). Sampling covers the whole eligible set; nothing is capped at the caller's output.
- [ ] The wallet stops passing its own height, checks every returned `term` and `height` itself, and refuses to build when fewer eligible outputs exist than the ring needs.
- [ ] Tests from Task 1 pass. Mutation: restore the height cap, `RealInputIsNotAlwaysNewestRingMember` fails. Checkpoint.

### Task 4: Bills in consensus (H-3)

**Files:** `src/CryptoNoteCore/Blockchain.cpp:3276-3345` (`check_tx_outputs`), the HEAT sources in `validateSettlement`; `src/Wallet/WalletGreen.cpp:1364, 507, 1679`; `src/WalletLegacy/WalletTransactionSender.cpp:1832`.

- [ ] At v11+, a HEAT output with `term` 0 or `HEAT_TERM` must have an amount in `HEAT_BILL_DENOMINATIONS`.
- [ ] HEAT leaving a protocol source (mint, Hearth swap, limit-order proceeds, LP removal, CD payout) is a sum of bills; the remainder below 0.1 HEAT stays with the source: not minted, or left in reserves, escrow or the yield pool.
- [ ] The splitter emits only bills; every builder uses it.
- [ ] Tests: `NonBillHeatOutputRejectedAtV11`, `SplitterEmitsOnlyBills` (0.1, 388.6, 500.0345678 HEAT), `MintOfNonBillSumRejected`. Mutation, checkpoint.

### Task 5: One ring size, enforced in blocks (H-4, H-6)

**Files:** `src/CryptoNoteCore/Core.cpp:297-350`, `src/CryptoNoteCore/DynamicRingSize.cpp`, `src/CryptoNoteCore/Blockchain.cpp` (block validation), `src/CryptoNoteConfig.h`.

- [ ] `const uint64_t HEAT_RING_SIZE = 16;` At v11+ a commitment spend's ring has exactly this many members, checked in block validation as well as in the mempool. Testnet uses the same rule; its chain is seeded with enough bills at reset.
- [ ] Tests: `RingOfFifteenRejected`, `RingOfSeventeenRejected`, `ShortRingRejectedInBlockOnMainnetParams`. Mutation, checkpoint.

### Task 6: Dead commitment fields (H-7)

- [ ] At v11+, `amountCommitment` and `amountProof` must be all zero. They carry no meaning until a verifier exists, and unverified bytes are not accepted.
- [ ] Test: `NonZeroAmountCommitmentRejectedAtV11`. Checkpoint.

### Task 7: One price for the epoch rate and the vault (F-2, F-3)

**Files:** `src/CryptoNoteCore/Blockchain.cpp:5306-5460, 5700-5750`.

- [ ] The swap-fee conversion, the Bonus Vault conversion and the recorded rate all use `mintPriceAt(height)` from the readiness plan (Tasks 3 and 9). The rate's numerator is exactly the HEAT minted into `CD_APY_POOL` for that epoch.
- [ ] If Task 1 confirms F-3: HEAT minted in an epoch with nothing on deposit is carried into the numerator of the next epoch that has a denominator.
- [ ] Tests from Task 1 pass; `BoundarySpotPumpDoesNotRaiseEpochRate`. Mutation, checkpoint.

### Task 8: Documents match the code

- [ ] AGENTS.md: remove the 2.5× / 2× / 1.5× / 1.25× tier multipliers (the code has a flat yield floor); remove "RingCT" and "Pedersen (RingCT)" from the protocol description until amounts are hidden; record H-1 to H-7 as fixed or open.
- [ ] `DEPOSIT_ARCHITECTURE.md:415-448`: replace "terms do not hurt privacy" with what Part 1 states.

---

## Phase 1 — HEAT ring options

Phase 0 alone gives option R1. A ring cannot be larger than the number of eligible outputs that exist: at launch every option has thin sets, and no design changes that.

| | Option | Ambiguous set per input | Amounts | New cryptography |
|---|---|---|---|---|
| R1 | Phase 0: wallet-chosen decoys from all eligible same-bill outputs | 16, drawn from every same-bill output | Public | None |
| R2 | Fixed batches: consensus assigns each bill output to a batch; the ring is the whole batch | The batch (16–32), identical for every spend from it | Public | None |
| R3 | Log-size linkable ring signature (Triptych class) over large batches | 1,024–4,096 | Public | New signature, audit |
| R4 | Confidential amounts (Pedersen commitments, range proofs); rings across all HEAT, no bills | 16–32, drawn from every HEAT output | Hidden | Range proofs, MLSAG/CLSAG wiring |
| R5 | Full-set membership proof (Curve Trees class) | Every HEAT output | Hidden, or public with bills | Large |
| R6 | Shielded note pool (the 2026-09-29 STARK design) | Every note | Hidden | Largest; nothing implemented |

Notes:
- **R1** leaves the classic weak point: which decoys a wallet picks. Age-distribution mismatches and ring intersection have traced a large share of spends on other ring-signature chains.
- **R2** removes decoy choice. Every spend from a batch shows the same ring, so there is nothing to compare. Costs: an output is unspendable until its batch closes; a batch that stays short for one epoch merges into the batch before it. Risk: an attacker who creates the other members of a victim's batch knows the victim's spend. Mitigation: batches are formed per 20-block window by a permutation seeded with the window's closing block hash, so membership cannot be chosen at creation. A miner can still grind that hash at the cost of block rewards.
- **R3** is R2 with a signature whose size grows with the logarithm of the batch. It keeps every amount public and reaches thousands. Verification time grows linearly with the batch. Monero studied this class and did not deploy it.
- **R4–R6** hide amounts; see Part 3.
- Outside all of these: the node that first relays a transaction is a separate leak. The changelog records the Dandelion stem phase as disabled.

**Recommendation:** Phase 0 for v11 (it is required for HEAT to work at all). R2 next, as the consensus groundwork for R3. R4–R6 only after decision P-5.

---

## Phase 2 — CD exit options

Constraint on all of them: the epoch rate divides fees by total HEAT on deposit, so that total must be public. Under A–E the amount entering a CD is public at creation. A–C also need CD principal split into bills.

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

**Recommendation:** A for v12, built so D can later replace bills. F remains the long-run direction and needs decision P-5 first.

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
- The 2026-09-29 STARK design puts XFG and HEAT in one pool. That places the base money's supply behind proof soundness and is the decision with the largest effect on the sound-money claim.

---

## Decisions for the owner

| # | Decision | Default |
|---|---|---|
| P-1 | HEAT ring after Phase 0 | R2, then R3 |
| P-2 | Ring size | 16 |
| P-3 | Spend-age floor | 10 blocks |
| P-4 | CD exit | A |
| P-5 | Hidden amounts | Not for XFG. For HEAT only after R3 and A are live, with a turnstile |
| P-6 | Pre-v11 commitment outputs, if the mainnet scan finds any | Not spendable as HEAT; what they are owed is undecided |
| P-7 | CD principal as bills | Yes (required by A–C) |
