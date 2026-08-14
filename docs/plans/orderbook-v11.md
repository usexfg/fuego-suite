# Orderbook v11 — Call-Auction CLOB + Microstructure Upgrades

**Status**: Plan / awaiting approval of the v12→v11 re-gate before implementation.

## 1. Goal

Replace pool-intermediated pricing with real two-sided price discovery on-chain, without reintroducing the unbacked-order attack class. The mechanism is a **per-block call auction** (exchange opening-auction rules) over **tx-extra-backed limit orders**, upgraded with exchange-grade microstructure rules, with the constant-product AMM retained as a volume-capped backstop.

## 2. Architecture

```
Order = TransactionExtraLimitDeposit (XFG or HEAT into escrow buckets)   [exists]
                 │
                 ▼
         Per-block call auction (NEW)
   ┌──────────────────────────────────┐
   │ 1. Resting orders (deposits from │
   │    earlier blocks)               │
   │ 2. Build cumulative bid/ask      │
   │ 3. Clearing price = crossing     │
   │    point maximizing executed     │
   │    volume (not VWAP)             │
   │ 4. Self-trade rejection          │
   │    (same addressHash never       │
   │    crosses)                      │
   │ 5. Price-time priority           │
   │ 6. Settle: proceeds credited to  │
   │    each side's buckets (no pool  │
   │    involvement)                  │
   └──────────────────────────────────┘
                 │
                 ▼
   P_clear = auction clearing price (when volume > 0)
   Mint TWAP = P_clear (fallback: pool spot)
                 │
                 ▼
   Unfilled remainder → AMM backstop band at adaptive spread (volume-capped)
   Taker pays 1% only on backstop fills; auction fills are fee-free.
```

## 3. Clearing-price algorithm (deterministic, integer math)

Canonical price scale: HEAT atomics per XFG atomic × COIN (1e7). Orders sorted per side:

- Bids: descending price, time priority within a level (older deposit first).
- Asks: ascending price, time priority within a level.

For each candidate price level p (the set of distinct bid/ask prices):

```
matchedVol(p)  = min(Σ bid.amount over bids ≥ p, Σ ask.amount over asks ≤ p)
imbalance(p)   = |Σbids − Σasks| at p
```

Choose p* that maximizes `matchedVol(p)`; ties broken by minimizing `imbalance(p)`; further ties broken in favor of the previous block's P_clear (continuity). If no crossing exists (best bid < best ask), no auction, P_clear unchanged.

Settlement: all orders participating at p* fill fully up to `matchedVol`, pro-rata-by-time on the larger side (the last price-time level in the larger aggregate is rationed by time priority). Credits flow to `proceedsXfg` / `proceedsHeat` on both sides — pool reserves untouched.

This is O(n log n) over distinct price levels with prefix sums — trivially within block budget.

## 4. Microstructure rules

| Rule | Behavior |
|---|---|
| Price-time priority | Within a price level, earlier deposits fill first |
| Min price tick | Orders must price at multiples of `ORDER_PRICE_TICK` (e.g., COIN / 100); prevents sub-tick queue-jumping |
| Post-only flag (optional v11.1) | Deposit with `postOnly=1` never crosses; the auction skips it if it would take |
| Self-trade rejection | Same `addressHash` on both sides of a crossing → the later deposit is skipped (or matched amounts are excluded) |
| Sybil mitigation | Wash-trading across fresh keypairs costs per-block tx fees; P_clear volume-weighting makes manipulation cost scale with attempted move. Documented as the privacy-chain floor |
| AMM backstop cap | Backstop fills capped at `HEARTH_BACKSTOP_MAX_BPS` (e.g., 500 bps) of the block's auction volume; if auction volume is zero the backstop serves alone (bootstrap behavior) |

## 5. Attack model

| Vector | Mitigation |
|---|---|
| Unbacked orders | Impossible — order requires a confirmed deposit tx |
| Unsigned orders / hijacked cancels | Orders are signed txs; cancel = withdraw of own deposit |
| Fake fills inflating reserves | Auction fills never touch pool reserves; backstop leg bounded by real reserves |
| Wash trading | Same-key rejection + fee-gated sybil + volume-weighted P_clear |
| Front-running | Batch auction: one clearing price per block, no ordering race |
| Fill invisibility | Fills are deterministic chain state; proceeds claimable on-chain |
| Empty-book gappiness | Accepted — honest discovery; AMM backstop covers bootstrap |

## 6. What stays from the current (v12) overlay

`TransactionExtraLimitDeposit` escrow buckets (`pendingXfg/pendingHeat`), `LimitDepositInfo` (amount, targetPrice, expiration, addressHash, proceeds, depositedAmount), `TransactionExtraLimitWithdraw` (claim/cancel), height-based expiry, per-block deterministic executor in `processOrderbookForBlock`, exact push/pop reversal via `m_blockOrderFills`.

## 7. Dev guide (phases)

### Phase A — Auction matcher (core)
1. `OrderbookAuction` (new file `src/CryptoNoteCore/OrderbookAuction.{h,cpp}`): pure, testable — takes two sorted books + prev P_clear, returns {p*, matched volume, participating orders}. Integer math only, no RNG.
2. Wire into `processOrderbookForBlock`: build books from `m_limitDeposits` (skip withdrawn/expired), run auction, settle into proceeds buckets, record fills in `m_blockOrderFills` for popBlock reversal.
3. `P_clear` = p* when matchedVol > 0; header write unchanged.
4. Mint TWAP: `getRollingTwap()` window fed from `block.hearthPoolRatio` — switch the v11 write to `P_clear` (auction price) with pool-spot fallback when no auction volume.

### Phase B — Microstructure
5. Price-tick validation in the LimitDeposit validation branch (v11-gated).
6. Same-`addressHash` self-trade rejection in the auction book build.
7. Maker/taker fee asymmetry: auction fills fee-free (both legs), backstop fills keep the 1% taker fee. (Maker rebates deferred — no fee token; rebates would need a fee-credit mechanism, scope as v11.1.)

### Phase C — Backstop cap + tests
8. `HEARTH_BACKSTOP_MAX_BPS` cap on backstop volume per block.
9. `tests/CoreTests/OrderbookAuctionTest.cpp`: crossing price, tie-breaks, time priority, self-trade rejection, pro-rata rationing, zero-crossing, rollback symmetry (mirror the existing `test_orderbook` harness style).
10. Guardian verification pass at depth 2.

## 8. Re-gate: v12 → v11 — EXECUTED

Chain height at decision time was **1,015,027**; `UPGRADE_HEIGHT_V11 = 1,111,111` (~96k blocks out). No v11 blocks exist yet, so re-gating is retroactively safe: the new consensus rules activate cleanly when the chain crosses v11. No checkpoint needed beyond the normal upgrade — all nodes must run the new binary before height 1,111,111.

All 32 gates in `Blockchain.cpp` + 1 in `WalletGreen.cpp` were changed `BLOCK_MAJOR_VERSION_12 → BLOCK_MAJOR_VERSION_11` (comments updated v12+ → v11+). Legacy (`< v11`) paths retained for pre-v11 re-validation. Coverage matrix verified (25/25 checks) and full build + `core_tests` + `test_orderbook` green.

Original inventory (all now re-gated):

| Region | Lines | What it gates |
|---|---|---|
| CD interest cap | 2475–2487 | Pre-v12 deposit zero-claim + youngest-ring cap vs legacy max-cap |
| Term whitelist | 2642 | Commitment-output term allowlist |
| Coinbase rejection | 3175 | Settlement tags in coinbase |
| Treasury fund validation | 3195, 3207 | TreasuryFund tag checks + settlement class |
| AMM swap validation | 3262 | Fee-adjusted taker pricing vs legacy Q64.64 |
| LP validation | 3409, 3438 | v12 auth LP add/remove branches |
| Mint validation | 3557, 3641 | Canonical-price mint path vs legacy |
| TWAP window clear | 3883–3885 | Scale transition clearing + raw accumulator |
| Header price write | 4097 | Canonical hearthPoolRatio write |
| OOB executor | 4127, 4163, 4297 | Fill executor, expiry pass, volatility feed (spot) |
| Epoch: CD APY denom | 4853 | `m_heatOnDeposit` vs `m_totalCdLocked` |
| Epoch: bonus vault | 4864, 4951 | HEAT conversion + pending counter |
| Epoch: conversions/burns | 4998, 5048, 5132 | 50/50 burns, CD-share conversion, LP Manager |
| Epoch: CD yield split | 5179 | Legacy vs v12 mint branches |
| Epoch: bootstrap | 5277 | Two-leg owned-reserves check |
| Swap settlement | 5779, 5954, 5982 | Actual-delta settle, auth-only LP settle/rollback |
| Swap rollback | 6251, 6344, 6362 | Legacy vs actual-delta pop, LP pop gates |
| Wallet CD fee | WalletGreen:1536 | TreasuryFund burn vs dev-fund output |

Approval gate: after you confirm the re-gate (and the coordinated-release/checkpoint decision), the change is a mechanical sed of `BLOCK_MAJOR_VERSION_12` → `BLOCK_MAJOR_VERSION_11` in `Blockchain.cpp` + `WalletGreen.cpp`, with the legacy branches re-anchored to `< BLOCK_MAJOR_VERSION_11`, followed by full build + guardian verification.

## 9. Definition of done

- Auction matcher unit tests + rollback symmetry tests green.
- `test_orderbook` + `core_tests` green; full build green.
- Guardian depth-2 pass on the diff.
- Docs updated (how-hearth-works, fee-earning, AGENTS.md).
