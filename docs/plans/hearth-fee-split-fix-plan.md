# Hearth Fee Split — Fix Plan & Dev Guide

**Date:** 2026-10-17
**Agent:** opencode (space-bunny-free)
**Status:** PLAN ONLY — no code changed
**Scope:** `src/CryptoNoteCore/Blockchain.cpp`, `src/CryptoNoteCore/CryptoNoteConfig.h` (actually `src/CryptoNoteConfig.h`), `tests/CoreTests/`, `docs/`
**Trigger:** OKOC → `src/CryptoNoteCore/**` → fuego-guardian

> **Line references are captured against commit `ef4a725b` (master / HEAD).**
> The working tree carries uncommitted in-flight work in
> `src/CryptoNoteCore/Blockchain.cpp` (the same pool-maker region, a *different*
> fix — escrow commingling, not the fee split). That work does **not** change this
> finding: `fill.rebateHeat` is credited in neither version, and neither version
> debits `fill.cdFeeHeat` from reserves. Line numbers below may drift by ±1 once
> that work lands. Anchor on the function name and the quoted code, not the line.

---

## 1. Headline finding

The question posed was *"do LPs get the 30% maker share when the pool acts as maker?"*

**Answer: three of four paths yes, one path no — and the broken path is a HEAT supply bug, not just a missing LP credit.**

The 0xF0 pool-maker auction branch books the CD share into `cdHearthFeeAccumulator` **without ever debiting the pool reserves**. The accumulator's documented contract is that its value was already removed from reserves at settlement. In this path it was not. At the epoch boundary that claim is converted into **newly minted HEAT UTXOs**, so the pool keeps 100% of the fee *and* CD holders receive freshly minted HEAT for it.

| Path | Fee payer | CD (70%) | LP / maker (30%) | Reserve debit for CD? |
|---|---|---|---|---|
| A — direct user AMM swap | taker | accumulator | retained in reserves | ✅ yes |
| B — OOB backstop fill (pool maker) | resting order | accumulator | stays in reserves | ✅ yes |
| C — auction, **user** maker | taker | accumulator | maker's proceeds | ⚠️ escrow, not reserves — see §9 |
| D — auction, **pool** maker (`0xF0`) | taker | accumulator | **0% — vanishes** | ❌ **none** |

---

## 2. Evidence

### Path D — root cause

`src/CryptoNoteCore/Blockchain.cpp:4281-4309`

```cpp
if (fill.orderId.data[0] == 0xF0) {          // pool-generated order = pool is maker
  ...
  if (fill.side == 1) {                       // pool sells XFG, receives HEAT
    m_ammPool.reserveXfg -= xfgMoved;
    m_ammPool.reserveHeat += heatDebit;      // ← full gross, NO cdFeeHeat deduction
  } else {                                   // pool buys XFG, pays HEAT
    m_ammPool.reserveHeat -= heatDebit;      // ← full gross, NO cdFeeHeat deduction
    m_ammPool.reserveXfg += xfgMoved;
  }
  if (fill.cdFeeHeat > 0) {
    m_ammPool.cdHearthFeeAccumulator += fill.cdFeeHeat;   // ← claim booked, nothing debited
  }
```

`fill.rebateHeat` is **never read** in this branch. Repo-wide, `rebateHeat` is consumed only in the user limit-deposit branch (`:4340`, `:4341`, `:4356`) and rollback (`:5740`, `:5762`).

### The invariant it violates

`src/CryptoNoteCore/Blockchain.cpp:5476-5477`

> `V11+: m_cdYieldPool is XFG-denominated (swap fees); cdHearthFeeAccumulator is HEAT-denominated (value already debited from LP reserves at settlement).`

### Why that becomes unbacked HEAT

`src/CryptoNoteCore/Blockchain.cpp:5505-5526` — the v11 epoch boundary:

```cpp
heatMinted += m_ammPool.cdHearthFeeAccumulator;
if (m_heatSupply <= UINT64_MAX - heatMinted) {
  m_heatSupply  += heatMinted;              // supply inflation
  m_heatCdFeePool += heatMinted;
  m_feePoolBalance += heatMinted;
  m_vault.addUtxo(vI, heatMinted, HEAT, VaultPartition::CD_APY_POOL, ...);  // real UTXO
}
```

Path D books a claim whose backing was never removed → **HEAT supply inflates by 70% of the 1% fee (0.7% of notional) on every pool-maker auction fill.** This is the exact failure mode the codebase already warns about at `:110-115` ("unbacked HEAT at the next epoch mint") and `:5481` ("No phantom XFG ever enters the AMM pool").

### Path D is live, not dead code

| Step | Location |
|---|---|
| Pool band regenerated each block | `Blockchain.cpp:4194-4195` |
| Injected into the auction | `Blockchain.cpp:4260` |
| `0xF0` prefix stamped | `OrderbookTypes.cpp:64` |
| Auction runs post-bootstrap | `Blockchain.cpp:4233` (`!g_orderbookIsInBootstrap`) |
| Bootstrap window ended | height `1,111,111 + 144 = 1,111,255` (`:4544`, `BOOTSTRAP_BLOCKS`) |

### Reproduction

Compiled against the real `runAuction()`:

```
PATH A  direct swap           -> CD 70.0%   LPs 30.0%
PATH B  OOB backstop          -> CD 70.0%   LPs 30.0%
PATH C  auction, user maker   -> CD 70.0%   maker rebate 30.0%
PATH D  auction, pool maker   -> CD 70.0%   LPs 0%, rebate 3,000,000 uncredited
         fill.rebateHeat owed to pool : 3000000
         fill.rebateHeat credited     : 0   <-- never read in 0xF0 branch
```

### No test coverage

`tests/CoreTests/HearthAmmTests.cpp:263` asserts conservation of `AuctionResult` only. Nothing asserts the `0xF0` settlement credits the rebate or debits the CD share from reserves.

---

## 3. The gating constraint — do NOT gate on `majorVersion`

This is the non-obvious part. A naive `if (majorVersion >= BLOCK_MAJOR_VERSION_12)` fix would be **dead code**.

**There is no `UpgradeDetectorV12`.** `Blockchain.cpp:635-644` instantiates `m_upgradeDetectorV2` … `m_upgradeDetectorV11` only. `Blockchain::getBlockMajorVersionForHeight` (`:1444-1467`) has no v12 branch and tops out at V11.

`Core.cpp:639` stamps every mined block from that function:

```cpp
b.majorVersion = m_blockchain.getBlockMajorVersionForHeight(height);
```

so **blocks never carry majorVersion 12 today**. `Currency::getBlockMajorVersionForHeight` (`Currency.cpp:203-204`) does return v12 — the two disagree, and `Blockchain`'s copy is the one that matters for block production.

**House pattern to use instead — gate on height:**

```cpp
if (height >= m_currency.cdActivationHeight()) { /* corrected */ } else { /* legacy */ }
```

`cdActivationHeight()` is defined at `Currency.cpp:154-156`, already consumed inside `Blockchain.cpp` at `:2940` and `:3205`, and is the established mechanism for "v12 behaviour, activated by height". `CD_ACTIVATION_HEIGHT = UPGRADE_HEIGHT_V12 = 2,666,666` (`CryptoNoteConfig.h:478-481`). Testnet resolves it to its v11 height (`Currency.cpp:155`).

> **Side finding, out of scope but worth filing separately:** `Blockchain::getBlockMajorVersionForHeight` vs `Currency::getBlockMajorVersionForHeight` disagree on v12. Any future v12-gated consensus rule hits the same trap.

---

## 4. Blast radius (OKOC gate)

**Who uses `cdHearthFeeAccumulator`?** 51 references. Consumers:

| Consumer | Location | Impact of path D bug |
|---|---|---|
| Epoch rate numerator | `Blockchain.cpp:5209-5211` | inflated CD rate |
| Epoch mint | `Blockchain.cpp:5505-5526` | unbacked HEAT UTXO |
| `m_heatSupply` | `Blockchain.cpp:5512` | supply inflation |
| `m_feePoolBalance` | `Blockchain.cpp:5520` | fee-pool caps F-001 |
| Pop/reversal | `:5731`, `:5753`, `:6931`, `:6947`, `:7805` | must stay exactly invertible |
| Pre-epoch snapshot | `:1212`, `:3577` | reorg correctness |
| Serialization | `AmmPool.cpp:177`, `Blockchain.h:501` | state compat |

**Is it locked?** Yes — the pre-fix arithmetic is consensus history from height 1,111,255 onward. Blocks must replay bit-identically.

**What depends on it?** Every LP (pro-rata via `ammGetWithdrawalAmounts`, `AmmPool.cpp:131-132`), the Treasury LP Manager, all CD holders, and the `/heat_metrics` surface.

---

## 5. Fix design

### 5.1 Primary — debit the CD share in the 0xF0 branch

The pool-maker fee is HEAT-denominated (`fill.cdFeeHeat`, `fill.rebateHeat` are both HEAT at `OrderbookAuction.h:31-32`). The debit belongs on the HEAT leg.

```cpp
if (fill.side == 1) {                        // pool sells XFG, receives HEAT
  ...
  m_ammPool.reserveHeat += (heatDebit - fill.cdFeeHeat);   // net of the CD share
} else {                                     // pool buys XFG, pays HEAT
  ...
  m_ammPool.reserveHeat -= (heatDebit + fill.cdFeeHeat);
}
```

Guard with a saturating subtraction, matching the established pattern at `:4290` / `:4299`:

```cpp
if (fill.side == 1) {
  if (m_ammPool.reserveHeat <= UINT64_MAX - (heatDebit - fill.cdFeeHeat)) { ... }
}
```

**Why the rebate needs no explicit credit here:** `rebateHeat` was never debited from the pool in the first place — only `cdFeeHeat` ever leaves. Leaving it in `reserveHeat` makes the 30% accrue to LPs automatically through the existing pro-rata withdrawal. This is the same implicit-residual mechanism paths A and B already rely on (`:4447`, `:4497`, `:6416`). No new accumulator, no new state field, no serialization change.

### 5.2 Reversal must be the exact inverse

`popBlock` reverses auction fills at `:5704-5766`. The `rec.feeHeat` subtraction at `:5732-5734` / `:5754-5756` is already correct for the accumulator; the **new reserve debit needs its own inverse** added alongside it. Add a `rec` flag or reuse `rec.isAuction && rec.orderId.data[0] == 0xF0` to distinguish.

### 5.3 Gate

Wrap the corrected arithmetic in `height >= m_currency.cdActivationHeight()`, keeping the current code as the `else` branch verbatim. Pre-activation replay stays bit-identical.

### 5.4 Rejected alternatives

| Option | Why rejected |
|---|---|
| Route the 30% to `m_treasuryLpYield` | That accumulator is **never incremented** — only initialised (`:1075`) and restored on pop (`:5870`). It is a dead field. |
| Burn the unbacked HEAT instead of minting | Changes CD yield economics. Separate decision, needs its own fork. |
| Fix in place without a height gate | Breaks replay of v11 history. Hard fork of existing state. |
| Gate on `majorVersion >= 12` | Dead code — no UpgradeDetectorV12 (§3). |

---

## 6. Task list

| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 1 | Confirm v11 activation height on the live chain (1,111,111) and current height | opencode | 2026-10-17 | DONE |
| 2 | Prove path D defect with harness against real `runAuction()` | opencode | 2026-10-17 | DONE |
| 3 | Trace `cdHearthFeeAccumulator` → epoch mint → HEAT supply | opencode | 2026-10-17 | DONE |
| 4 | Establish gating mechanism (no UpgradeDetectorV12 → height gate) | opencode | 2026-10-17 | DONE |
| 5 | Blast-radius map of the accumulator (51 refs) | opencode | 2026-10-17 | DONE |
| 6 | Write this plan | opencode | 2026-10-17 | DONE |
| 7 | Implement §5.1 reserve debit behind the height gate | — | — | **TODO** |
| 8 | Implement §5.2 exact-inverse reversal | — | — | **TODO** |
| 9 | Add `test_hearth_amm` case: pool-maker fill debits reserves + credits rebate to LPs | — | — | **TODO** |
| 10 | Add apply/undo round-trip test (`popBlock` inverts the new debit exactly) | — | — | **TODO** |
| 11 | Add conservation test: accumulator claim ≤ value actually removed from reserves | — | — | **TODO** |
| 12 | Name the 30% constant — make `HEARTH_MAKER_REBATE_BPS` live or delete it | — | — | **TODO** |
| 13 | Delete unreachable legacy 50/50 branches (`:6365-6405`, `:6919-6937`) | — | — | **TODO** |
| 14 | Reconcile the 10 conflicting docs pages (§7) | — | — | **TODO** |
| 15 | Add `CHANGELOG.agent.md` entry (§8) | — | — | **TODO** |
| 16 | fuego-guardian verification pass on the diff | — | — | **TODO** |
| 17 | 007 review — is unbacked-mint reachable pre-activation? Quantify live exposure | — | — | **TODO** |

---

## 7. Documentation reconciliation

**Wrong — claim 100% CD / 0% LP:**

| File | Lines |
|---|---|
| `docs/features/hearth/providing-liquidity.mdx` | 11 |
| `docs/introduction.mdx` | 63 |
| `docs/api-reference/fee-pool.mdx` | 12 |
| `docs/security/risk-warnings.mdx` | 39 |
| `docs/developer/architecture.mdx` | 36 |
| `docs/heat/fuego-dual-asset-architecture.mdx` | 38, 53 |
| `docs/tutorials/hearth-amm-cd-real-yield.mdx` | 40 |
| `docs/changelog.mdx` | 23 (superseded 50/50→100% entry) |

**Correct already:** `docs/features/hearth/fee-earning.mdx`, `how-hearth-works.mdx`, `README.mdx`, `AGENTS.md`.

**Also stale:** `docs/CD-apy/cd_apy_models.md:9` claims `m_treasuryLpYield` "only accumulates" — it is never incremented at all.

Two doc claims need softening after the fix even though they state the 70/30 intent correctly: `how-hearth-works.mdx:45` and `fee-earning.mdx:17` both say the 30% "stays with LPs" when the pool is the counterparty. That is true post-fix and false on-chain today.

---

## 8. Ready-to-paste `CHANGELOG.agent.md` entry

```markdown
## Hearth fee split: pool-maker auction leg booked CD yield without debiting reserves

**Started**: 2026-10-17
**Agent**: opencode (space-bunny-free)
**Status**: TODO

### Headline finding

The 0xF0 pool-generated-order branch of the call auction
(`Blockchain.cpp:4281-4309`) settled the pool's maker leg at full gross and then
booked `fill.cdFeeHeat` into `m_ammPool.cdHearthFeeAccumulator` without removing
that value from the pool reserves. It also never credited `fill.rebateHeat`.

`cdHearthFeeAccumulator` is contractually HEAT-denominated value "already debited
from LP reserves at settlement" (`:5476-5477`). The v11 epoch boundary converts it
into newly minted HEAT UTXOs (`:5505-5526`). Booking a claim with no backing
therefore inflated `m_heatSupply` by 70% of the 1% taker fee on every pool-maker
auction fill, while the pool retained 100% of the fee and the 30% maker rebate
went unallocated.

Paths A (direct swap), B (OOB backstop) and C (user maker) were unaffected: each
debits the CD share from reserves at settlement.

Live since the bootstrap window closed at height 1,111,255.

### Fix

Debit `fill.cdFeeHeat` on the pool's HEAT leg so the accumulator's backing
invariant holds; the 30% rebate stays in reserves and accrues to LPs through the
existing pro-rata withdrawal. Gated on `height >= m_currency.cdActivationHeight()`
— **not** `majorVersion`, because no `UpgradeDetectorV12` exists and mined blocks
never carry majorVersion 12.

### Task List

| # | Task | Owner | Date | Status |
|---|------|-------|------|--------|
| 1 | Implement reserve debit behind height gate | | | TODO |
| 2 | Exact-inverse reversal in popBlock | | | TODO |
| 3 | Regression tests (rebate to LPs, round-trip, conservation) | | | TODO |
| 4 | Reconcile 10 conflicting docs pages | | | TODO |

### Sign-off

| Gate | Result | Agent | Date |
|------|--------|-------|------|
| Build compiles | | | |
| Tests pass | | | |
| Replay bit-identical below activation | | | |
| fuego-guardian pass | | | |
| All tasks done | | | |
```

---

## 9. Open questions — resolve before implementing

1. **Path C backing.** In the user-maker auction branch the fee moves between user escrows (`pendingXfg`/`pendingHeat`), not AMM reserves. Whether the CD claim there is properly backed needs a separate audit. **Do not assume it is fine — it was not examined.**
2. **Live exposure.** How many pool-maker auction fills have actually occurred, and how much HEAT was minted unbacked? Needs a chain scan; the block-serving RPC hang documented in `AGENTS.md` may block this.
3. **Remediation policy.** If exposure is material, correcting future behaviour alone leaves the historical inflation in place. A clawback or a treasury offset is a governance decision, not an engineering one.
4. **Versioning gap.** Should `UpgradeDetectorV12` be added as part of this work or tracked separately? Without it, every future v12-gated rule repeats this trap.

---

## 10. Sign-off

| Gate | Result | Agent | Date |
|------|--------|-------|------|
| Investigation complete, all paths traced | PASS | opencode (space-bunny-free) | 2026-10-17 |
| Defect reproduced against real `runAuction()` | PASS | opencode (space-bunny-free) | 2026-10-17 |
| Blast radius mapped (51 refs, 8 consumers) | PASS | opencode (space-bunny-free) | 2026-10-17 |
| Gating mechanism established | PASS | opencode (space-bunny-free) | 2026-10-17 |
| Build compiles | N/A — no code changed | — | — |
| Tests pass | N/A — no code changed | — | — |
| All tasks done | **NO — 11 of 17 TODO** | — | — |

**Verdict: plan ready for review. Implementation not started.**