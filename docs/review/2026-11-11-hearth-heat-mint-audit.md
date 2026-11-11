# Hearth Exchange + HEAT Mint Audit — 2026-11-11

Scope: `src/CryptoNoteCore/` Hearth (AMM, call auction, depth band, backstop, limit escrow, LP, AuntPoL epoch work) and HEAT minting (`HeatMintEngine`, `validateSettlement`, TWAP). Commit audited: `a38b1f1`.

Method: source read of the consensus paths, plus one executed harness that links the production `OrderbookAuction.cpp` (`runAuction`). No daemon build, no unit-test run, no live network test. Boost 1.83 was installed for the harness; the repo requires 1.86+, so a full build was not attempted.

Evidence labels:
- **EXECUTED** — observed by running production code.
- **READ** — derived from reading the code; not run. Each lists what would falsify it.

Line numbers are `src/CryptoNoteCore/Blockchain.cpp` unless another file is named.

## Remediation status (2026-11-11)

| Finding | Status |
|---|---|
| C1 | Fixed: band tiers priced on the curve at pool spot, rebuilt every block |
| C2 | Fixed: pool is always the maker; its rebate goes to reserves |
| C3 | Fixed: orderbook state and rollback records cached (archive v18) |
| H1 | Fixed with C2 (`ammRevertPoolMakerFill`) |
| H2, H3 | Fixed: median oracle, mint cap, reserve-bounded backstop, oracle-priced epoch work |
| H4 | Fixed: min size, required expiry, per-block cap, pruning of withdrawn orders |
| H5 | Fixed: per-order one-time keys (wallet-only) |
| M3 | Fixed: supply counts only XFG-valued CD fees as new HEAT |
| M1, M2 | Fixed: 0.00001 tick; auction settles whole or not at all |
| L2 (minimum mint) | Fixed in consensus; bill denominations still wallet-only |
| M4 | Fixed: signed stop tag (0x0F) then withdraw |
| M5-M7, L1, L3-L5 | Open |

Details and test evidence: `CHANGELOG.agent.md`, section "Hearth critical fixes".

## Verdict

Not ready for v11 activation. Three findings each break the pool or consensus on their own (C1, C2, C3). The mint path's arithmetic is sound; its price input is not.

---

## Critical

### C1 — Depth band sells pool inventory at flat prices; each sweep lowers `k` (EXECUTED + READ)

`generatePoolOrders` (`OrderbookTypes.cpp:38-112`) quotes 10% of each reserve in 10 tiers inside roughly ±0.42% of `P_clear` at the 30 bps base spread. The constant-product curve moves about 10% over the same size. Auction fills against those orders move `reserveXfg`/`reserveHeat` directly (4463-4485) with no invariant check.

Harness result at the genesis seed (8,000 XFG / 1,000 HEAT), one user ask sweeping the bid band:

```
B sold 800.00 XFG: band pays 100.2771 HEAT, constant-product curve pays 90.9091 HEAT (before fee)
B k before=8.000000e+20 after=7.923696e+20 (-0.95%)
```

The seller receives 10.3% more than the curve pays. Selling into the band, then buying back on the curve or against the regenerated band, repeats every regeneration. LPs and AuntPoL fund it.

Compounding factors (READ):
- Quotes are centred on `g_orderbookLastClearingPrice` (4368), not the pool spot. Direct AMM swaps move spot up to 2× per block while `P_clear` stays, so the band then quotes a stale price against its own reserves.
- Pool orders are never decremented after a fill. The same full-size orders are re-offered each block until a regeneration trigger fires.
- `g_poolBandFilledLastBlock` is read and zeroed (4346-4347) and never written. The consumption regeneration trigger and the consumption spread widening are dead code.

Harness scope: `runAuction` is production code; the reserve updates were mirrored from 4463-4485, not executed through `processOrderbookForBlock`.
Falsifier: a `k`-floor or curve-price check on pool auction fills that I missed.

### C2 — Pool-as-taker auction fills create HEAT from nothing (EXECUTED + READ)

When pool orders are the rationed side, `runAuction` marks them taker and assigns `cdFeeHeat`/`rebateHeat` to the pool fills. Settlement (4463-4490) debits the pool only `fill.heat`, still adds `cdFeeHeat` to `cdHearthFeeAccumulator`, and the user maker is credited `fill.heat + fill.rebateHeat` (4523-4526). Nobody pays the 1%.

```
A  takerIsBid=1  pool reserveHeat debit=124775000  user proceeds credit=125648425  cd accumulator credit=374325
   => HEAT created from nothing=1247750     (exactly 1% of the fill)
A2 control, user is taker: net HEAT created=-6970600 (pool maker rebate never credited=6970600)
```

Any user order smaller than the band makes the pool the rationed side, so this is the default case. The user's rebate becomes withdrawable HEAT; the CD share is minted into `CD_APY_POOL` at the epoch.

The control case shows the mirror defect: when the pool is maker, its rebate (70% of the fee at v11) is credited nowhere. That HEAT leaves the taker's escrow and is dropped from all ledgers. AGENTS.md says LPs receive it; they do not.

Falsifier: a debit of fee + rebate from pool reserves on the 0xF0 branch.

### C3 — Auction/orchestrator state is not persisted; a restarted node diverges (READ)

Consensus inputs live in file-static globals (701-711): `g_orderbookLastClearingPrice`, `g_orderbookIsInBootstrap`, `g_orderbookBootstrapBlocksRemaining`, pool orders, orchestrator price history, regeneration counters. None are in the cache serializer (345-470 region; `m_ammPool` and `m_limitDeposits` are). `init` restores them only through `rebuildCache`. `rebuildOrderbookFromUtxoSet` (4728) has no caller.

After a normal restart from cache the node holds `P_clear = 0`, bootstrap = true with 144 blocks remaining, no pool orders, empty volatility history. It skips auctions and the backstop for 144 blocks while peers run them. Reserves and limit-deposit balances then differ, and `validateSettlement` gates swap output, withdraw amounts and mint TWAP on those values, so the node forks.

`m_orderbookSnapshots` and `m_blockOrderFills` are also memory-only: after a restart no block's fills can be rolled back, and any reorg deeper than 1,000 blocks hits the same path (6081 logs an error and continues).

Falsifier: a load path that repopulates these globals from the cache or by replay.

---

## High

### H1 — Rollback of a pool sell fill adds HEAT instead of removing it (READ)

Forward (4476-4477): `reserveXfg -= x; reserveHeat += heat`. Reverse (6015-6018): `reserveXfg += x; reserveHeat += heat`. After a reorg over such a block `reserveHeat` is high by `2 × heat` relative to a node that never saw the orphan. The buy-side reversal (6054-6057) is correct.

### H2 — Mint price is an 8-block spot average on a shallow pool with no mint size bound (READ)

`twapBefore` (4218) averages the last 8 end-of-block spots. Mint size is unbounded. The seed pool is 8,000 XFG / 1,000 HEAT.

- The 2× per-block band (8196-8211) applies to direct swaps only. Pool auction fills and backstop fills move spot with no band check.
- The backstop cap is 5× auction volume (4591-4593). Two addresses crossing with each other raise auction volume at near-zero cost (exact tie = no fee; otherwise 70% of the fee returns as maker rebate at v11), which removes the cap.
- Raising HEAT-per-XFG for 8 blocks then minting yields HEAT at a discount in any size. Cost scales with pool depth; mint size does not.

Profit requires an exit deeper than the pool (resting SELL_XFG orders, CD yield, atomic swaps, later LP depth). Without one the result is supply inflation and peg damage rather than extraction.

### H3 — Epoch conversions use end-of-block spot at a known height (READ)

`processBlockEpochWork` (5253+) prices the CD yield pool, bonus vault share and the fee-rate record with `ammGetSpotPrice` at the boundary block. The boundary height is public, so one in-band swap in that block scales the HEAT minted into `CD_APY_POOL` and `BONUS_VAULT`. The AuntPoL LP add in the same block deposits at the manipulated ratio.

### H4 — Unbounded limit-deposit set scanned three times per block (READ)

Consensus sets no minimum order size, no cap on open orders and no bound on `expiration` (0 = never) (8212-8237). Withdrawn entries are erased only on `popTransaction` (7369). Each block iterates all of `m_limitDeposits` for expiry (4391), auction build (4416) and backstop (4599), then sorts for the auction. Cost to the attacker is one tx fee per 1-atomic order; cost to every node is permanent.

### H5 — Limit orders link to the wallet address (READ)

- `addressHash = H(spendPublic ‖ viewPublic)` of the funding wallet (`WalletGreen.cpp:1686-1692`). Every order from one wallet carries the same value, and anyone holding an address can test it against the order set.
- `TransactionExtraLimitWithdraw` publishes `spendPublicKey` and `viewPublicKey` on chain (8239-8250). That is the full public address.
- Side, amount, limit price, expiry and all fills are public per order.

Consensus only requires that the withdraw key pair hashes to `addressHash`; a per-order one-time key pair would satisfy it. The linkage is a wallet choice.

---

## Medium

| # | Finding | Location | Label |
|---|---------|----------|-------|
| M1 | Price tick is 0.01 HEAT/XFG. Launch price is 0.125, so user limit prices sit on an 8% grid; below 0.01 no order is valid. | `CryptoNoteConfig.h:448`, 8219 | READ |
| M2 | Auction settlement guards `continue` on one leg (4470, 4482, 4510, 4520, 4535) while the counter-leg still applies: one-sided value transfer if a guard ever fires. | 4463-4541 | READ |
| M3 | `m_heatSupply` adds `cdHearthFeeAccumulator` at the epoch. For XFG→HEAT swaps and auction taker fees that HEAT already existed, so the supply metric and audit snapshot overstate. | epoch work, 6778-6780 | READ |
| M4 | Withdraw must balance HEAT exactly against `amount`/`proceeds` at inclusion (`AssetType.h:79`). A fill between signing and inclusion invalidates the tx, so a live order near the market cannot be cancelled reliably. | 8069-8084 | READ |
| M5 | No peg mechanism in pricing. `hearthstoneCents` has no consumer outside FCI state and RPC; `FCI_PAYOUTS_ENABLED = false`. Mint follows the pool price and there is no redemption, so nothing pulls HEAT toward the Hearthstone reference. | `FuegoCostIndex.cpp`, `CryptoNoteConfig.h:280` | READ |
| M6 | `orderbookClearingPrice`, `hearthPoolRatio`, depth fields are not in the block hashing blob and each node overwrites them (4710-4724). They are not PoW-committed; light clients cannot rely on them. | `CryptoNoteFormatUtils.cpp:608-613` | READ |
| M7 | Self-trade exclusion keys on a user-supplied `addressHash`; a second hash bypasses it. | `OrderbookAuction.cpp` | READ |

## Low

- `orderId` is chosen by the sender. A mempool observer can submit the same id first and invalidate the victim's deposit (grief only).
- `HEAT_MINT_MIN_HEAT` and bill denominations are enforced in wallets only. Commitment rings are keyed by exact amount (`classifyInputAsset`, 7952), so a non-wallet mint with an odd amount has an anonymity set of one.
- `scanMintTx` counts every `CommitmentSpend` as XFG. Safe today only because HEAT must balance exactly; documented at `AssetType.h:64-78`.
- Saturating adds on the fee accumulator and reserves (4487, 6016) hide invariant breaks instead of rejecting the block.
- Mint and swap amounts are public in tx extra. Expected for this design; users should not assume RingCT-style amount privacy for HEAT.

## Checked and found sound (READ)

- Per-asset balance with one settlement tag per tx; retired 0x08 and legacy LP tags rejected; coinbase tag guard.
- Mint arithmetic: checked sums, premium as a haircut inside the burn, premium excluded from burn totals (`bankedBurn`, 194-205), TWAP excludes the current block.
- Direct swap: constant-product pricing, declared output capped at net curve output, `minOutput`, reserve underflow checks.
- 0xF0 prefix rejected on user deposits (8228).
- Withdraw authorisation binds `orderId`, owner and outputs hash; double-withdraw blocked in-block and across blocks.
- LP add: ratio tolerance and `min(sharesA, sharesB)`; seed carries LP shares; LP remove has minimum-out.
- Backstop fills are on-curve with the fee retained; `k` does not fall on that path.
- Mempool re-validates settlement when filling a block template (`TransactionPool.cpp:629`).

## Test coverage gaps

`tests/CoreTests/` covers `runAuction`, AMM math and `HeatMintEngine` in isolation. No test drives `processOrderbookForBlock` → `popBlock` round-trips, pool-as-taker settlement, a cache store/load between blocks, or reorg after restart. C2, C3 and H1 each fail a state-equality test of that shape.

---

## Recommendations

Ordered by dependency. Each is a consensus change and needs its own plan and `CHANGELOG.agent.md` entry before any source edit.

1. **Replace flat band quotes with curve-derived quotes (C1).** Price tier *i* at the marginal curve price after tiers 0..i-1 plus spread, centred on pool spot. Reject any pool auction fill that lowers `reserveXfg × reserveHeat`. Decrement pool orders on fill, or regenerate every block. Remove the dead consumption path or wire `g_poolBandFilledLastBlock`.
2. **Make the pool a maker only (C2).** If pool volume is the rationed side, charge no fee on that block's pool fills, or debit fee + rebate from reserves. Credit pool-maker rebates to reserves so LPs receive them. Assert per-block HEAT conservation: Δescrow + Δreserves + Δproceeds + Δaccumulator = 0.
3. **Move orderbook state into `Blockchain` members and serialize it (C3).** Include `P_clear`, bootstrap counters, pool orders, orchestrator history, regeneration counters, `m_orderbookSnapshots`, `m_blockOrderFills`. Derive bootstrap from height. On missing rollback records, refuse the pop and trigger `rebuildCache` instead of logging.
4. **Fix the sign at 6018 (H1)** and add a push/pop state-equality test over every fill type.
5. **Harden the mint oracle (H2, H3).** Price mints at `min(short TWAP, long TWAP)` with the long window at least one epoch; use a median, not a mean. Apply the per-block band to end-of-block spot regardless of which path moved it. Cap mint volume per block as a fraction of pool HEAT depth. Bound the backstop by reserves in all cases. Use the same TWAP for epoch conversions and the AuntPoL add.
6. **Bound the order set (H4).** Consensus minimum order size, maximum TTL, required non-zero expiry, per-block deposit cap, and erase withdrawn/expired-and-claimed entries with an undo record.
7. **Per-order one-time keys (H5).** Wallet derives a fresh key pair per order from the spend secret and `orderId`; `addressHash` commits to that pair. No consensus change. Self-trade exclusion then needs a different basis or removal (M7).
8. **Tick as a ratio (M1).** Use a relative tick (for example 10 bps of price) or a much smaller absolute tick.
9. **Two-step cancel (M4).** A cancel tag freezes the order at inclusion; the claim tx follows against frozen balances.
10. **Decide the peg mechanism (M5).** Either wire Hearthstone into mint pricing or redemption, or state in the docs that HEAT is a free-floating pool-priced asset with an informational index.
11. **Derive `orderId` in consensus** as a hash of the tx prefix; enforce mint minimum and bill denominations in `check_tx_outputs`.
12. **Stateful test harness** that connects blocks through the production path, stores and reloads the cache at random heights, reorgs at random depths, and asserts full state equality against a fresh replay.

## Not verified

- No full build or existing test suite run in this environment.
- C1 round-trip profitability was computed from one executed sweep plus arithmetic; a multi-block attack was not run through block connection.
- CD claim paths, vault UTXO spending, FCI voting, SwapDaemon and wallet RPC were outside the read set except where cited.
