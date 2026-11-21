# Plan: seed Hearth by XFG donation to AuntPoL

**Status:** proposed, not implemented
**Date:** 2026-11-21
**Task:** #15 (session task list)
**Builds on:** seedboot from 9d89d50 (empty pool at v11; a user's first deposit must sit within 1% of 8:1 and locks 1,000 shares). That stays. This plan adds a second way to open the pool, owned by the protocol.

## Summary

Anyone sends XFG to AuntPoL with one transaction. In that transaction's block, consensus:
- burns part of the donation at the mint price and mints the matching HEAT, through the normal mint accounting;
- adds the rest of the XFG plus the minted HEAT to the pool as **protocol-owned** LP.

If the pool is empty, this opens it at the launch ratio. A 16,000 XFG donation opens it at 8,000 XFG / 1,000 HEAT.

- **No mint premium.** The premium funds AuntPoL, and AuntPoL is already the recipient.
- **No mint transaction.** The donation pays only its own network fee (0.0008 XFG).
- **Better privacy for the donor:** one XFG transaction with decoys. The donor never holds HEAT and never owns LP shares, so no HEAT input links anything.

## Facts this plan relies on (read from code, 2026-11-21)

- `TransactionExtraTreasuryFund {asset, amount}`, settled in `validateSettlement` (~8356) and applied in `pushTransaction` (~7159):
  - **asset 0 (XFG):** treated as destroyed. It credits `m_swfBurnedXfgPendingHeat` and the burn tally. It never reaches AuntPoL.
  - **asset 1 (HEAT):** credits `m_treasuryHeatReserve`, the AuntPoL HEAT leg.
  - **anything above 1:** refused.
- **Finding:** the wallet's `donateToTreasury` / wallet RPC `donate` sends asset 0, and its code comment says the XFG goes to the "Treasury LP Manager". It does not: it burns into the SWF ledger. Users of `donate` today are burning, not funding AuntPoL.
- **AuntPoL pairing** (epoch work, ~5800): pairs `m_treasuryLpPendingXfg` with `m_treasuryHeatReserve` at the pool ratio, and only when the pool is non-empty. Unpaired excess carries forward.
- **Mint burn accounting** (`bankedBurn` path, ~5140): 50% of a burn goes to the Eternal Flame (permanent), 100% to the total-burn tally, and 50% to `m_swfBurnedXfgPendingHeat`. HEAT supply rises by the HEAT minted.
- **Mempool template:** `getPoolTouch` lets at most one reserve-moving tx (swap, LP add, LP remove) into each block template.
- **Mainnet epoch:** 900 blocks (5 days). Testnet: 10 blocks.

## Design

### Tag
- Add `TreasuryFund` asset **2 = XFG donation to AuntPoL**, accepted from v11. Assets 0 and 1 are unchanged.
- Settlement: the whole `amount` is an XFG sink. There are no outputs except the donor's change. The tx fee is the normal XFG surplus.
- Minimum donation: `HEARTH_POL_DONATION_MIN` (a decision below), so dust can't open the pool.

### Applying a donation of `D` XFG (in `pushTransaction`)
Let `p = mintPriceBefore(height)`, in HEAT per XFG × COIN. It is the launch ratio while the pool has no price history, and the 8-block TWAP after.

**Pool empty:**
- burn `b = D / 2` and mint `h = b × p / COIN` HEAT;
- keep `x = D − b` as the XFG leg;
- open the pool with reserves `x` / `h` and `totalLpShares = isqrt(x × h)`, all of them added to `m_protocolLpShares`. Protocol shares can never be removed, so the pool can never empty again and no extra lock is needed.

**Pool non-empty:**
- **Price guard (G1).** If the spot price differs from the mint TWAP `p` by more than `HEARTH_POL_DONATION_MAX_DEVIATION_BPS` (recommended 200), nothing is paired in this block: the whole `D` goes to `m_treasuryLpPendingXfg` and waits for the epoch pairing. Without the guard, someone can move the spot one block ahead (or a miner can sandwich the donation) and make the protocol add liquidity at a bad price, then arbitrage it back.
- Otherwise, choose the burn so the pair matches the pool ratio `rX / rH`:
  `b = D × (COIN × rH) / (COIN × rH + p × rX)`;
  `h = b × p / COIN`; `x = D − b`.
- **Checked arithmetic (G2).** Every product above uses a checked 128-bit multiply-divide. `D × COIN × rH` can exceed `uint128_t` on extreme reserves. An overflow refuses the donation in `validateSettlement`; it is never truncated.
- **Pairing (G3).** The pairing is one function shared with the AuntPoL epoch work, extracted from the existing code (~5800), not a second copy. It pairs `x` / `h` at the pool ratio and mints protocol shares with `ammMintLpShares`. The unpaired remainder of **each** leg carries forward: XFG to `m_treasuryLpPendingXfg`, HEAT to `m_treasuryHeatReserve`.
- When the TWAP equals the spot price, `b = D / 2`.

**Burn accounting (G4):** the `b` burned is recorded exactly like a mint burn:
- 50% Eternal Flame, 100% total-burn tally, 50% `m_swfBurnedXfgPendingHeat`;
- `m_heatSupply += h`;
- only `b` counts as burned; `x` is not;
- `bankedBurn` is a pure per-tag function used by the block-level burn loop (~5140), but `b` depends on pool state. So the donation's burn is recorded once, at connect, through one helper that the user-mint path also uses, so the two cannot drift. `bankedBurn` must not also count asset 2, which would record it twice.

**Not revenue:** a donation is not a fee and never funds CD yield. The burned share follows the burn-derived rules in AGENTS.md.

**Price:** the burn uses the same mint price a user would pay in that block. No path is cheaper than a user mint except the skipped premium, and that premium would go to AuntPoL itself.

### Mempool and template
- An asset-2 donation moves reserves. `getPoolTouch` marks it like an LP add, so at most one of these goes in each block template.
- A donation has no outputs whose amounts depend on pool state; it is only an XFG sink. So a change in pool state between relay and block can't invalidate it. Only the minimums, the price guard (which defers it, never refuses it) and overflow can.
- `validateSettlement` and `pushTransaction` compute `b`, `h`, `x` and the shares from the same state through the same function. Block validation is sequential per tx, so a miner-built block with several reserve-moving txs still validates deterministically.
- **Below the opening minimum on an empty pool:** the donation is refused. Pending XFG can't open a pool, so accepting it would strand the donation until someone else opens the pool.

### Interaction with the user first deposit
- **Donation and first deposit in the same block:** block order decides. The first one opens the pool and the second pairs at the new ratio. Both are valid in either order.
- **The first deposit is already in:** the donation pairs at the pool ratio.

### Reorg and cache
- Per-tx journal `m_polDonationByTx[txHash] = {burned b, minted h, xfg x, shares, pendingRemainder}`. `popTransaction` reverses it exactly: reserves, shares, protocol shares, HEAT supply, burn tally, Eternal Flame and SWF ledger, pending XFG. It is modelled on `m_mintPremiumHeatByTx`.
- The journal is stored in the block cache. Cache archive version 19 → 20.
- Cache rebuild (`rebuildCache`) replays the same function.

### Wallet and RPC
- **New** CLI `donate_pol <xfg amount>` and wallet RPC `donate_pol`. They send asset 2 with key inputs, decoys and change.
- **Existing** `donate` / `donateToTreasury` (asset 0) are kept, but their description and the misleading comment are corrected: they burn into the SWF ledger and do not fund AuntPoL. (Maintainer decision: rename it, or remove it from the RPC.)
- **Node RPC:** `/amm_pool_info` adds `protocol_lp_shares` and `user_lp_shares`.

## Decisions for the maintainer
1. **`HEARTH_POL_DONATION_MIN`:** a minimum per donation. Recommended: 100 XFG. **`HEARTH_POL_DONATION_MAX_DEVIATION_BPS`:** recommended 200 (2%).
2. **Minimum size for opening the pool, for donations and user first deposits alike:** a tiny opening pool lets anyone move the TWAP that prices mints for very little. Recommended: an opening needs at least 1,000 XFG on the XFG leg. Today a user first deposit has no minimum.
3. **Existing `donate` (asset 0):** decided 2026-11-21: keep it, with corrected wording. Its code comment, RPC definition and handler comment now say it destroys XFG into the SWF ledger and does not fund AuntPoL.
4. **Testnet:** no reset is expected. Asset 2 is refused today, so no existing testnet block carries it and replay produces the same state. The archive bump still forces one full replay. The only change to existing behaviour is the opening minimum, if decision 2 adds one: a past tiny first deposit would then fail on replay, and testnet would need a reset.

## Guardian review (2026-11-21)

Verdict: **approved with reservations**. G1–G4 are blocking; each is written into the design above.

| ID | Severity | Finding | Resolution |
|---|---|---|---|
| G1 | High (economic) | Pairing at a manipulated spot price lets an attacker or miner extract value from the donation | Spot/TWAP deviation guard; defer to epoch pairing |
| G2 | High (correctness) | The split formula can overflow `uint128_t` | Checked multiply-divide; refuse on overflow |
| G3 | Medium | Rounding left one leg unaccounted; a second pairing copy could drift | Shared pairing function; both remainders carried |
| G4 | Medium | `bankedBurn` is per-tag and stateless; recording the burn there and at connect would double-count | One connect-time helper shared with mints |
| G5 | Medium (latent, out of scope) | 50% of every burn is credited to `m_swfBurnedXfgPendingHeat` as a future HEAT entitlement. The conversion is disabled (`if (false && …)` ~5879). Re-enabling it would mint SWF HEAT against XFG that already backs minted HEAT: a second claim on one burn, which the AGENTS.md burn rule forbids | Donations inherit mint accounting unchanged; logged for the maintainer; never re-enable as written |
| G6 | Medium (existing) | The existing AuntPoL epoch pairing also pairs at the spot price with no deviation guard | The guard function from G1 is applied there too |
| G7 | Medium (existing) | The wallet `donate` (asset 0) burns into the SWF ledger while its comment says it funds AuntPoL | Wording corrected (decision 3) |
| G8 | Info | Coinbase can't carry the tag (`isHeatwaveTag`); it is refused before v11 (`hasTreasuryFund` gate) | Verified in code |

Other attacker paths checked:
- dust spam and dust opening: per-donation and opening minimums, plus the fee;
- reorg double count: per-tx journal;
- miner reordering: sequential validation;
- refused-after-relay griefing: the tag has no state-dependent outputs.

## Tests
- **core_tests:**
  - the split arithmetic: empty pool exact 8:1; non-empty pool ratio match; rounding remainder to pending; `uint128_t` bounds;
  - asset 2 refused before v11; minimum enforced;
  - burn accounting equals a user mint of `b` without premium: Eternal Flame, tally, SWF, HEAT supply;
  - pop restores every field;
  - price guard: deviation above the limit defers the whole donation to pending; at the limit it pairs;
  - overflow inputs are refused, never truncated;
  - both rounding remainders are carried; the epoch pairing and the donation pairing give identical results for identical inputs;
  - the burn is recorded once: the tally equals `b`, not `2b` and not `D`;
  - mutations: drop the burn accounting, drop the journal pop, and drop the price guard; each must fail a test.
- **Real-chain harness, new `poldonate` command:**
  - at v11 the pool is empty;
  - the wallet `donate_pol 16000` opens the pool at 8,000 / 1,000 in the donation's block, with all shares protocol-owned;
  - HEAT supply rises by 1,000 and the burn tally by 8,000;
  - a later 160 XFG donation pairs at the pool ratio;
  - a user LP add and removal work around protocol liquidity;
  - a user swap succeeds;
  - no user can remove protocol shares.
- **Restart and reorg checks:** the donation in the restarted and reorged ranges (F relays it at the same height); C1/C2a/C2b must pass.
- **Full run:** full `make`, all four suites, seedboot, `poldonate` and the chain checks.

## Docs
- `AGENTS.md` (AuntPoL funding: donations), `docs/features/hearth/how-hearth-works.mdx`, `docs/features/hearth/README.mdx`, `docs/releasenotes/RELEASE_NOTES_V11.md`, `docs/changelog.mdx`, the review doc.
- The local guide `SEEDBOOT_DEPOSIT_GUIDE.local.md`, rewritten as: fresh wallet, Tor, `donate_pol 16000`.

## Changelog
A new `CHANGELOG.agent.md` entry with one task per section above, written before code, with sign-off after the full run.

## Out of scope
- The mint premium's HEAT leg with no burn behind it (task #14).
- Withdrawing protocol liquidity.
- HEAT donations opening the pool (asset 1 keeps waiting for epoch pairing).
