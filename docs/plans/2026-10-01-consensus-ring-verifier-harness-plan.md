# Plan: consensus-level verification harness for commitment rings

**Date:** 2026-10-01
**Status:** plan only. Nothing here is implemented. Follows
`v11-p0-commitment-keys-and-asset-accounting-guide.md` Section B.

> **Update 2026-10-03 (audit merge of `keyderiv` into master).** This plan was written
> against the branch's own Section B implementation, which was **not merged**. Master
> already contained a different fix (`054cae35e`), and the audit chose it over the
> branch's version. Read the plan with these corrections:
>
> - The single asset table is `Currency::classifyCommitmentTermAsset(term)`. It is total:
>   every finite term, including **term 0** and `DIGM_TERM`, is HEAT; there is no
>   "unrecognised term" outcome. Rows 5 and 6 below no longer apply.
> - A mixed-asset ring is **rejected inside `checkCommitmentSpendInput` and
>   `checkCommitmentTransferInput`** ("ring mixes asset classes"). It is not credited as
>   XFG and not left to the conservation check. Row 3 below now expects a rejection at the
>   ring check. `classifyInputAsset` classifies by member 0, which is exact *because*
>   mixed rings are rejected first. The branch's version returned XFG for an unresolvable
>   ring, which let a poison decoy choose the asset an input was credited as.
> - `TransactionInputCommitmentTransfer` is now classified by its ring the same way, so it
>   is no longer in the "not covered" list.
> - `resolveCommitmentRingAsset` and the free `classifyCommitmentTermAsset(term, min, max)`
>   do not exist on master; step 1 under Sequencing has no counterpart.
>
> The harness itself is still needed: nothing exercises the ring-mixing rejection through a
> real `Blockchain` today. `tests/UnitTests/AssetClassificationTest.cpp` covers only the table.

## Why a harness rather than a unit test

The defect fixed in Section B (part 1) was `classifyInputAsset` reading the asset
from ring member 0. That was provable with a pure function test, because the logic
was already isolated in the term table.

The remaining risk is not the table — it is **where the table is consulted and what
happens when it says no**. Specifically:

- `checkCommitmentSpendInput` accepts a ring whose members disagree on asset or
  carry an unrecognised term, because it never classifies members at all.
- `getTransactionInputAssetAmounts` credits an input using `classifyInputAsset`,
  and its result feeds the per-asset conservation check in `Core::check_tx_inputs`.
- `popBlock` must reverse the same deltas, or state diverges after a reorg.

Those are call-path properties. A unit test cannot establish them; only a test that
drives the real verifier can.

## What the harness must be able to do

1. Construct a `Blockchain` over a `Currency` (mainnet and testnet parameters) with
   a `tx_memory_pool` and logger, and let it build genesis.
2. Inject `CommitmentOutputRef` entries into `IndexManager::commitmentOutputs()` at
   chosen global indices, with chosen `term`, `commitKey`, `commitment_ec`,
   `transactionIndex` and `isSlashed`.
3. Build a `TransactionInputCommitmentSpend` whose `outputIndexes` are relative
   offsets encoding a chosen absolute ring, at a chosen `amount`.
4. Produce a real MLSAG signature over that ring, using a real key image that
   satisfies the `L*I == I` subgroup check.
5. Assert `checkCommitmentSpendInput` accepts or rejects.
6. Push the transaction through the full block path
   (`pushBlock` -> `check_tx_inputs` -> connect) and read the resulting per-asset
   balances.
7. Pop the block and assert the balances return exactly to their pre-block values.
8. Replay the same chain from genesis and assert identical state (determinism).

## Blocking seam

`IndexManager m_indexManager` is private (`Blockchain.h:521`, under `private:` at
`:362`) with no public accessor and no test friend. Step 2 has no route today.

Options, in the order I would choose them:

1. **A narrow public accessor** for `commitmentOutputs()` on `Blockchain`, documented
   as read-only/test-support. Smallest conceptual change; does widen the API.
2. **`friend` the test fixture.** No production API change, but couples a test to a
   7000-line consensus class and needs the fixture declared in the header.
3. **A `BlockchainIndicesSerializer` test path** — that class is already a friend
   (`:479`) and already owns index serialization. Extend it rather than adding a
   second seam.

Option 3 is the most consistent with the existing design and avoids both a public
mutation path and a test-only friend. I would revisit it if the serializer turns out
to be tied to on-disk state in a way that makes read-only injection awkward.

## Signature generation

`checkCommitmentSpendInput` calls `Crypto::check_ring_signature` over the ring's
commit keys. The harness needs to sign with the real member's secret. The owner-bound
form gives exactly that:

- `deriveOwnerBoundCommitKey(D, i, B)` produces the output key for a recipient spend
  key `B`.
- `deriveOwnerBoundKeyImage(D, i, P, b)` produces the scalar `x = b + t` and key image
  `I` that `checkCommitmentSpendInput` will accept.

So the harness can generate a fully valid owner-bound input without any new
cryptography, and the existing `commitment_owner_bound_tests` vectors already prove
the two sides agree byte-for-byte.

## Test matrix

Every case must state which layer rejects it. This is the point of the harness:
distinguishing "rejected at mempool" from "rejected at block" from "accepted at
historical height".

| # | Case | Setup | Expected |
|---|------|-------|----------|
| 1 | Single-asset HEAT ring | All members `HEAT_TERM` | Accepted |
| 2 | Single-asset XFG ring | All members `TERM_REGULAR`-equivalent or `POOL_XFG` | Accepted |
| 3 | **Mixed ring, real member last** | Member 0 `HEAT_TERM`, real member `POOL_XFG` at a later slot | **`checkCommitmentSpendInput` rejects** ("ring mixes asset classes") before any signature or conservation check |
| 4 | Mixed ring, real member first | Reverse of #3 | Same rejection — proves order-independence |
| 5 | ~~Unrecognised term in ring~~ | *Obsolete on master:* term 0 is plain HEAT, so a ring of `term = 0` and `HEAT_TERM` members is a single-asset ring | Accepted (pin this, it documents the decision) |
| 6 | ~~DIGM_TERM in ring~~ | *Obsolete on master:* `DIGM_TERM` classifies as HEAT | Accepted as HEAT; DIGM mint accounting is a separate concern |
| 7 | Out-of-range index | Relative offset beyond the amount's outputs | Rejected |
| 8 | Slashed member | `isSlashed = true` on a decoy | Rejected (existing guard, regression coverage) |
| 9 | Pool-term member | `POOL_XFG`/`POOL_HEAT` as a decoy | Rejected by the unspendability guard (existing) |
| 10 | Interest claim with non-CD ring | `claimedInterest > 0`, ring contains `HEAT_TERM` | Rejected by the existing finite-CD ring rule |
| 11 | **Rollback exactness** | Case #1 accepted, then popBlock | Pre-block per-asset balances restored exactly |
| 12 | **Replay determinism** | Push #1, pop, re-push | Identical state |
| 13 | Mixed-era heights | A case-3 transaction at a pre-v11 height | Rejected/accepted per historical rules, not the new rule |

Case #13 is the resync-safety check and is the one most likely to be forgotten. The
new classification must not retroactively reject history.

## Sequencing

1. ~~Extraction + focused test~~ — not applicable on master (see the update above).
2. **Injection seam.** Choose option 1, 2 or 3 above. This unblocks everything else
   and is independently reviewable.
3. **Harness fixture**: `Currency` builder, genesis construction, ref injection
   helper. Reuse `commitment_owner_bound_tests`' key-generation approach for
   determinism — fixed seeds, no RNG.
4. **MLSAG signing helper** using the owner-bound derivation. Verify case #1 accepts
   before writing any negative case; a harness that rejects everything passes no
   test.
5. **Cases 1–2** (baseline accept) then 3–4 (the defect), then 5–10 (regression),
   then 11–13 (state and resync safety).

## Definition of done

- Every case in the matrix runs, and each names the layer that rejected it.
- The mixed-ring cases fail against the pre-fix `classifyInputAsset` and pass after.
  This must be *demonstrated*, not asserted.
- `fuegod` replays an existing chain to the same head hash with these changes
  compiled in.
- A multi-node reorg over a chain containing cases #1 and #3 converges.

## Not covered by this harness

- `TransactionInputCommitmentTransfer` no longer always classifies XFG (master resolves
  its ring like a spend), but it still does not bind `newTerm` to a replacement output.
  That part is open and needs its own cases.
- `uint64_t` wrap in `out + fee + burn`. Separate work; needs its own adversarial
  cases at the `Core.cpp` conservation check.
- DIGM mint accounting in `DigmMintEngine`, which sums commitment inputs as HEAT
  without knowing the real ring member's asset.
