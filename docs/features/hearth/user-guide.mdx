# Hearth User Guide

This guide is about **what Hearth lets you do**, not what it is under the hood. If you want the mechanics, read the [technical README](/features/hearth/README).

## What Hearth enables you to do

1. **Trade XFG ⇄ HEAT at a price you choose** — place a limit order and it fills automatically when the market crosses your price. No watching the screen.
2. **Trade instantly against the pool** — market-style buys and sells whenever you want, at the current price.
3. **Mint HEAT** — burn XFG and receive HEAT at the live market price.
4. **Earn a market-maker rebate** — resting orders that fill earn 30% of the 1% taker fee, paid automatically to your proceeds.
5. **Earn as a liquidity provider** — supply a balanced XFG+HEAT pair to the pool and earn the pool's trading P&L plus the maker share of fees when the pool is the counterparty.
6. **Earn yield on your HEAT** — lock HEAT into a Certificate of Deposit and earn the CD yield pool, funded by the 70% fee share and cross-chain swap fees.
7. **Recover everything you commit** — every order deposit, partial-fill proceeds, and expired order is claimable on-chain. Nothing is ever lost to a silent fill failure.

## How to do each thing

Wallet commands below are the CLI (`fire_wallet` / console) form. RPC equivalents exist for all of them (`place_limit_order`, `create_heat_cd`, etc.).

### 1. Trade at your price — limit orders

**What it is:** commit funds to an order saying "buy XFG at price P or better" or "sell XFG at price P or better." Once per block, the auction crosses resting orders at a single clearing price. If your order participates, the proceeds are credited to your order; you claim them when you cancel or the order finishes.

**Commands:**

```
place_order buy  <amount_heat> <price> [exp_blocks]
place_order sell <amount_xfg>  <price> [exp_blocks]
```

- `amount` is in the asset you commit (HEAT for buy, XFG for sell).
- `price` is XFG per HEAT (or equivalently the ratio you want). Prices must be multiples of the order tick (0.01); the wallet rounds and rejects gross deviations.
- `exp_blocks` optional — after this many blocks, the order expires and everything (remaining deposit + proceeds) becomes claimable.

```
cancel_order <tx_hash>      # claim everything (deposit + fill proceeds)
orderbook                   # view the book (bid/ask depth, P_clear)
```

**What you earn:** if your order fills as a maker, you receive the **30% rebate** on top of the price. If you were the crossing (taker) side, you pay the 1% fee (70% CD yield / 30% to the makers).

### 2. Trade instantly — market swaps

**What it is:** swap directly with the AMM backstop at the current pool price (1% taker fee, 70/30 split).

```
buy_xfg  <amount_heat> <min_xfg>     # pay HEAT, receive XFG
sell_xfg <amount_xfg>  <min_heat>    # pay XFG, receive HEAT
```

`min_*` protects you from price movement between quoting and confirmation.

### 3. Mint HEAT

**What it is:** burn XFG, receive HEAT at the market price (the 8-block TWAP of the discovered clearing price, with the pool spot as fallback). The burned XFG goes 50% to the Eternal Flame (deflation) and 50% to SWF.

```
mint_heat <xfg_amount>
```

### 4. Earn as a market maker

**What it is:** any resting order that fills as the maker side earns the 30% rebate automatically. No extra action — the rebate is added to your order's proceeds, claimed with `cancel_order`.

**How to think about it:** post resting liquidity on both sides at your fair prices; the rebate is your compensation for making the market.

### 5. Earn as a liquidity provider (LP)

**What it is:** deposit a **balanced** XFG+HEAT pair into the pool (single-sided deposits mint nothing). You earn the pool's trading P&L and the 30% maker share when the pool is the counterparty. LP shares are proportional and fair.

```
# via RPC: lp_deposit (balanced XFG+HEAT) / lp_withdraw
```

Note: check the current pool ratio before depositing — your pair must match it (within tolerance).

### 6. Earn yield — HEAT Certificates of Deposit

**What it is:** lock HEAT for a term (in epochs) and earn the CD yield pool. Yield is funded by protocol fees only (never inflation): 70% of Hearth taker fees + 69% of cross-chain swap fees. Loyalty tiers multiply your rate for long terms.

```
heat_cd <amount_heat> <term_epochs>
```

- Two-step: mint HEAT first (`mint_heat`), then deposit (`heat_cd`). You cannot deposit a deposit.
- The 0.1% creation fee is burned to the Treasury LP Manager (protocol-owned liquidity).
- Your deposit secret stays local; different term lengths don't hurt privacy.

### 7. Recovery — nothing is stranded

- **Partial fills:** proceeds accrue on your order; claim via `cancel_order`.
- **Expired orders:** remaining deposit + proceeds are claimable via `cancel_order`.
- **Withdrawals:** `legacy_withdraw <deposit_id>` for old multisig deposits (withdraw-only — legacy XFG deposits earn no interest in the new system).

## Fee cheat-sheet

| You do | You pay | You can earn |
|---|---|---|
| Market swap | 1% taker fee | — |
| Limit order, maker fill | nothing | 30% rebate |
| Limit order, taker fill | 1% (70 CD / 30 rebate to makers) | — |
| Mint HEAT | network fee only (0% premium) | — |
| Provide liquidity | network fee | pool P&L + maker share on pool fills |
| HEAT CD | 0.1% creation fee (burned to Treasury LP) | CD yield (fee-funded) |

## Where the numbers go

- 70% of Hearth fees + 69% of swap fees → **CD yield pool** (paid to CD holders).
- 11% of swap fees → **bonus vault** (loyalty bonuses, HEAT).
- 20% of swap fees → **treasury** → the protocol's own LP position (which compounds and repays the bootstrap).
- Every XFG burned in conversions: 50% Eternal Flame / 50% SWF, all counted in the public burn tally (`total_burned_xfg`).

## RPC quick reference

| Endpoint | Purpose |
|---|---|
| `/heat_metrics` | supply, burned tally, treasury, pool |
| `/amm_pool_info` | reserves + spot price |
| `/amm_quote` | pre-trade quote |
| `/get_limit_orders` | your resting orders |
| `/get_treasury_info` | live treasury routes |
| `/get_fee_pool_info` | CD fee pool + bonus vault |
