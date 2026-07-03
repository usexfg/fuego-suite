# Fuego On-Chain Batch Orderbook — Design

Status: **APPROVED** | Date: 2026-06-26 | Review: 13 objections, all resolved

---

## Purpose

Internal on-chain price discovery for XFG/HEAT without a centralized exchange. HEARTH constant-product AMM is replaced as the price authority; HEARTH is retained as a protocol-managed market depth band for instant fills.

---

## Understanding Summary

- **What**: Hybrid batch auction orderbook + HEARTH depth band for XFG/HEAT
- **Why**: No CEX dependency; HEARTH constant-product curve doesn't provide efficient price discovery
- **How**: Per-block batch auction (480s) with uniform clearing price; HEARTH re-centers to clearing price each block
- **Who**: XFG holders, HEAT holders, LPs
- **Constraints**: 480s block time, internal only (no external oracles), CryptoNote privacy model, `MAX_ORDERS_PER_BLOCK = 1000`
- **Non-goals**: Multi-pair support, HFT, sub-block execution, millions of users in v1

---

## Architecture

```
                             ┌──────────────────────────────┐
                             │        MEMPOOL                │
                             │  ORDER_PLACE  tx              │
                             │  BUY_XFG  (market)            │
                             │  SELL_XFG (market)            │
                             │  ORDER_CANCEL tx              │
                             └──────────────┬───────────────┘
                                            │
                    ┌───────────────────────▼────────────────────────┐
                    │            BLOCK FINALIZATION                   │
                    │                                                 │
                    │  ┌───────────────────────────────────────────┐  │
                    │  │ 1. Collect open orders from UTXO set       │  │
                    │  │ 2. Build bid curve (desc) + ask curve (asc)│  │
                    │  │ 3. Walk curves → find P_clear               │  │
                    │  │ 4. Construct settlement transactions        │  │
                    │  │ 5. Create remainder UTXOs (partial fills)   │  │
                    │  └──────────────────┬────────────────────────┘  │
                    │                     │                           │
                    │  ┌──────────────────▼────────────────────────┐  │
                    │  │ 6. Compute HEARTH pool rebalance swap      │  │
                    │  │ 7. Treasury executes rebalance (pays fee)   │  │
                    │  │ 8. Write P_clear to block header           │  │
                    │  └──────────────────────────────────────────┘  │
                    └───────────────────────┬────────────────────────┘
                                            │
         ┌──────────────────────────────────┼──────────────────────────────┐
         │                                  │                              │
         ▼                                  ▼                              ▼
┌──────────────────┐            ┌───────────────────┐          ┌──────────────────┐
│     HEARTH       │            │    ORDERBOOK      │          │  BLOCK HEADER    │
│                  │            │    (UTXO set)     │          │                  │
│  ┌────────────┐  │            │                   │          │  P_clear         │
│  │ Depth Band │  │            │  bids: B-tree     │          │  num_matches     │
│  │ (10% pool) │  │            │  asks: B-tree     │          │  depth_bid       │
│  └────────────┘  │            │                   │          │  depth_ask       │
│                  │            │  per-sender count │          │  pool_ratio      │
│  Instant fills   │            │                   │          │                  │
│  0.3% fee → LPs  │            │  Limit orders     │          │                  │
└──────────────────┘            └───────────────────┘          └──────────────────┘
```

### Bootstrap Phase (First 144 Blocks ~24 Hours)

```
BLOCK 0 ──── BLOCK 143
    │
    │  Orderbook: empty → no P_clear
    │  HEARTH: pool ratio used as P_clear
    │  Market orders: HEARTH band ONLY (no cascade)
    │
    ▼
BLOCK 144+
    │
    │  Orderbook: open for matching
    │  P_clear: computed from orderbook + prior P_clear fallback
    │  Full hybrid operation
```

---

## Components

### Orderbook

Orders stored as locked UTXOs with `TX_OUT_ORDER` output type. The blockchain IS the orderbook state — no separate database. Wallet scanning naturally discovers orders and fill remainder outputs.

**Order entry stored in UTXO unlocking condition**:

```
type:       BUY_XFG (pay HEAT → receive XFG) | SELL_XFG (pay XFG → receive HEAT)
price:      XFG/HEAT ratio × 10^8
amount:     atomic units (XFG for sells, HEAT for buys)
expiration: block height
keys:       sender public spend key + view key (for stealth remainder returns)
```

**In-memory indexes** (built at block finalization, not persisted):

```
OrderbookIndex:
  bid_curve:  B-tree by (price DESC, block_height ASC)   — time priority at equal price
  ask_curve:  B-tree by (price ASC,  block_height ASC)
  per_sender: map<sender_key, open_order_count>            — flood guard
```

**Per-block processing limit**: `MAX_ORDERS_PER_BLOCK = 1000`. Beyond this, orders carry forward to next block.

**UTXO set impact**: Order UTXOs are transient — spent on fill/cancel/expire. Remainder outputs are standard wallet UTXOs. Net UTXO growth is equivalent to normal transaction volume. No additional bloat beyond activity level.

---

### HEARTH Depth Band

Protocol-managed liquidity band around the clearing price.

```
                    HEARTH Pool Depth
                         │
       ┌─────────────────┼─────────────────┐
       │    SELL SIDE     │    BUY SIDE     │
       │  (XFG for HEAT)  │  (HEAT for XFG) │
       │                  │                 │
       │  ~10% of pool    │  ~10% of pool   │
       │  reserves        │  reserves       │
       └────────┬─────────┼────────┬────────┘
                │                  │
                ▼                  ▼
         Market Sells        Market Buys
         (instant fill)      (instant fill)
```

- Re-centers to `P_clear` every block via treasury rebalance swap
- Market orders consume HEARTH depth first, then cascade into orderbook
- Depth = `HEARTH_DEPTH_BAND_PCT` (e.g. 10%) of pool reserves on each side
- Depth calculated as percentage of total pool — scales naturally with pool growth
- LPs provide base capital, earn 0.3% swap fees on band trades
- Treasury can participate as LP

---

### Rebalance Transaction

At block finalization, protocol computes the swap to bring pool ratio from post-trade `R_drifted` → `P_clear`:

```
R_drifted = xfg_reserve / heat_reserve (after this block's trades)

if R_drifted < P_clear:
    treasury swaps HEAT → XFG   (pushes ratio up)
    pool: xfg_out = xfg_reserve - k / (heat_reserve + heat_in * 0.997)
if R_drifted > P_clear:
    treasury swaps XFG → HEAT   (pushes ratio down)
    pool: heat_out = heat_reserve - k / (xfg_reserve + xfg_in * 0.997)
```

Treasury pays standard 0.3% LP fee on rebalance swaps.

**Funding**: Mint premium (2-3% on HEAT mint) accumulates in treasury, providing both XFG and HEAT. The treasury naturally accumulates both assets over alternating market cycles. Per-block rebalance volume is bounded by depth band (10% of pool), preventing single-block drain.

**Separate from SWF**: The SWF (Sovereign Wealth Fund) receives 50% of minted XFG for cross-chain foreign peg defense. The rebalance fund is the treasury's on-chain swap account, funded by mint premium. These are distinct pools.

---

### Matching Algorithm

```
                    ┌──────────────────────────┐
                    │  Collect open orders from │
                    │  UTXO set (≤MAX_PER_BLOCK)│
                    └────────────┬─────────────┘
                                 │
                    ┌────────────▼─────────────┐
                    │  Sort: bids DESC, asks ASC│
                    └────────────┬─────────────┘
                                 │
                    ┌────────────▼─────────────┐
                    │  WHILE bid_price >=       │
                    │        ask_price:         │
                    │    match at ask_price     │
                    │    if partial fill:       │
                    │      create remainder UTXO │
                    └────────────┬─────────────┘
                                 │
                    ┌────────────▼─────────────┐
                    │  Compute P_clear =        │
                    │  volume-weighted average  │
                    │  of all matched ask prices│
                    └────────────┬─────────────┘
                                 │
                    ┌────────────▼─────────────┐
                    │  Filter: only fill orders │
                    │  where P_clear is         │
                    │  favorable to limit:      │
                    │  - bid: P_clear ≤ bid_price│
                    │  - ask: P_clear ≥ ask_price│
                    └────────────┬─────────────┘
                                 │
                    ┌────────────▼─────────────┐
                    │  Write P_clear to header  │
                    └──────────────────────────┘
```

**Key rule — limit order price protection**:

- A **bid** (buy XFG with HEAT) fills only if `P_clear <= bid_price`. Buyer pays P_clear (or less).
- An **ask** (sell XFG for HEAT) fills only if `P_clear >= ask_price`. Seller receives P_clear (or more).

This is a multi-unit uniform price auction. Users never fill at a price worse than their limit.

**Partial fills**: Original order UTXO is consumed. New remainder UTXO created with same price limit, routed to sender's stealth address via provided keys. If remainder < dust threshold, goes to sender wallet instead.

**Single-sided market**: No matches. P_clear unchanged from prior block. Orders carry forward.

**Empty book (bootstrap)**: P_clear = HEARTH pool ratio. Orders carry forward.

---

### Market Orders

```
                         ┌───────────────┐
                         │  MARKET ORDER  │
                         └───────┬───────┘
                                 │
                    ┌────────────▼─────────────┐
                    │  Step 1: Consume HEARTH   │
                    │  depth band at P_clear    │
                    │  (± spread, 0.3% fee)     │
                    └────────────┬─────────────┘
                                 │
                         ┌───────▼────────┐
                         │ Fully filled?  │
                         └───┬────────┬───┘
                         YES │        │ NO
                             │        │
                             ▼        ▼
                    ┌───────────┐  ┌─────────────────────┐
                    │  DONE     │  │ Step 2: Cascade into │
                    └───────────┘  │ orderbook limit asks │
                                   │ (or bids) at best    │
                                   │ available prices     │
                                   └──────────┬──────────┘
                                              │
                                   ┌──────────▼──────────┐
                                   │ Step 3: Stop when:   │
                                   │ - fully filled       │
                                   │ - 5 levels consumed  │
                                   │ - 150% price deviation│
                                   │ - orderbook exhausted │
                                   └──────────┬──────────┘
                                              │
                                   ┌──────────▼──────────┐
                                   │ Remaining unfilled   │
                                   │ returned to sender   │
                                   └─────────────────────┘
```

**Per-order guard**: max 5 orderbook levels OR 150% price deviation from P_clear — whichever triggers first. Prevents single market order from exhausting the book.

---

### Order Lifecycle

```
                    ┌──────────────────┐
                    │   ORDER_PLACE     │
                    │   (funds locked   │
                    │    in UTXO)       │
                    └────────┬─────────┘
                             │
              ┌──────────────┼──────────────┐
              │              │              │
              ▼              ▼              ▼
     ┌────────────┐  ┌────────────┐  ┌────────────┐
     │  MATCHED   │  │  CANCELLED │  │  EXPIRED   │
     │  (at block │  │  (sender   │  │  (block    │
     │   finalize)│  │   tx)      │  │   height)  │
     └─────┬──────┘  └─────┬──────┘  └─────┬──────┘
           │               │               │
           ▼               ▼               ▼
    ┌────────────┐  ┌────────────┐  ┌────────────┐
    │  FILLED    │  │  FUNDS     │  │  FUNDS     │
    │  + possible│  │  RETURNED  │  │  RETURNED  │
    │  remainder │  │  TO SENDER │  │  TO SENDER │
    └────────────┘  └────────────┘  └────────────┘
```

- **Place**: Funds locked in `TX_OUT_ORDER` UTXO. Sender pre-authorizes any fill amount from 0 to full.
- **Match**: At block finalization. Partial fill creates remainder UTXO. Full fill consumes entire order.
- **Cancel**: Sender submits `ORDER_CANCEL` referencing order ID. At next block, remainder returned. Dust fee to Treasury → Marketing.
- **Expire**: Auto-return at expiration block height. No fee.

---

## Transaction Types

| Type | Direction | Fee |
|------|-----------|-----|
| `ORDER_PLACE` | Create locked order UTXO | Standard tx fee |
| `ORDER_CANCEL` | Return unfilled remainder to sender | Dust → Treasury/Marketing |
| `BUY_XFG` (market) | Pay HEAT → receive XFG | 0.1-0.3% taker fee |
| `SELL_XFG` (market) | Pay XFG → receive HEAT | 0.1-0.3% taker fee |

Matching and remainder splitting handled by consensus at block finalization.

---

## Block Header Additions

```cpp
uint64_t orderbook_clearing_price;   // XFG/HEAT ratio × 10^8
uint32_t orderbook_num_matches;
uint32_t orderbook_depth_bid_xfg;    // total bid depth in XFG-equivalent
uint32_t orderbook_depth_ask_xfg;    // total ask depth
uint64_t hearth_pool_ratio;          // post-rebalance pool ratio
```

---

## Fee Model

```
                    ┌──────────────────────────────┐
                    │          FEES                 │
                    └──────────────┬───────────────┘
                                   │
         ┌─────────────────────────┼──────────────────────────┐
         │                         │                          │
         ▼                         ▼                          ▼
┌─────────────────┐    ┌─────────────────────┐    ┌──────────────────┐
│  HEARTH Band    │    │    Orderbook        │    │  HEAT Mint       │
│  0.3% → LPs     │    │  Maker: 0%          │    │  2-3% premium    │
│                 │    │  Taker: 0.1-0.3%    │    │  → Treasury      │
│                 │    │  → LPs + protocol   │    │  (rebalance fund) │
└─────────────────┘    └─────────────────────┘    └──────────────────┘
                                                                     │
                                            ┌────────────────────────┘
                                            ▼
                                   ┌──────────────────┐
                                   │  Cancel order     │
                                   │  Dust → Treasury  │
                                   │  → Marketing      │
                                   └──────────────────┘
```

---

## Mint Economics

```
                        User wants to mint HEAT
                                  │
                                  ▼
                    ┌──────────────────────────┐
                    │  Burn XFG at incinerator  │
                    │  address                  │
                    └────────────┬─────────────┘
                                 │
                    ┌────────────▼─────────────┐
                    │  50% intercepted          │
                    │  → Sovereign Wealth Fund  │
                    │  (cross-chain foreign     │
                    │   peg defense)            │
                    └──────────────────────────┘
                                 │
                    ┌────────────▼─────────────┐
                    │  50% completes burn       │
                    │  → Eternal Flame          │
                    │  (permanent deflation)    │
                    └──────────────────────────┘
                                 │
                                 ▼
                    ┌──────────────────────────┐
                    │  HEAT minted =            │
                    │  XFG_amount × P_clear     │
                    │  ─────────────────────    │
                    │       1.58                │
                    └──────────────────────────┘
```

**Mint premium**: 2-3% on top of the burn amount, paid to treasury. Funds the on-chain rebalance mechanism. Acts as a structural friction against mint-pump-mint arbitrage loops.

**Per-block mint cap**: Prevents large-scale single-block mint attacks. Set at a fixed HEAT amount per block (configurable). Combined with the 50% Eternal Flame burn, repeated mint-unmint cycles hemorrhage user XFG — structurally self-defeating.

---

## Anti-Manipulation

| Threat | Defense | Mechanism |
|--------|---------|-----------|
| Wash trading | Min 2 distinct parties per clearing | Known Sybil-vulnerable at 2; 150% price guard is primary wall |
| Market order exhaustion | 5 levels or 150% per-order | Stops cascade, returns remainder |
| Sybil order flooding | Max orders per block per sender | Bounded per-sender count |
| MEV / front-running | Batch auction uniform clearing price | All orders fill at same P_clear |
| Stale orders | Expiration block height | Configurable, default ~1 week |
| Mint-pump-mint loop | Per-block mint cap + premium | Volume throttle + cost friction |
| Spam | Min order size + dust cancel fee | Dust below threshold rejected |
| Price manipulation | Per-order deviation guard | 150% max movement in single order |

**Privacy note**: Amounts are visible (no bulletproofs in current protocol). Ring signatures hide sender identity, but timing correlation is inherent to CryptoNote — in low-volume blocks, the anonymity set is limited by concurrent activity. This is no worse than existing transfer transactions.

---

## Parameters

| Parameter | Value | Configurable |
|-----------|-------|-------------|
| `HEARTH_DEPTH_BAND_PCT` | 10% | Yes |
| `MAX_ORDERS_PER_BLOCK` | 1,000 | Yes |
| `MAX_MARKET_PRICE_DEVIATION_PCT` | 150% | Yes |
| `MAX_MARKET_ORDER_LEVELS` | 5 | Yes |
| `MIN_DISTINCT_PARTIES` | 2 | Yes |
| `BOOTSTRAP_BLOCKS` | 144 (~24 hours) | Yes |
| `DEFAULT_ORDER_EXPIRATION` | 12,600 (~1 week) | Configurable per order |
| `MINT_PREMIUM_BPS` | 200-300 | Yes |
| `CANCEL_FEE_ATOMIC` | TBD | Yes |
| `MIN_ORDER_AMOUNT` | Dust threshold | Yes |

---

## RPC Endpoints

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `get_orderbook` | GET | Current bid/ask curves with aggregated depth at each price level |
| `get_orderbook_clearing_price` | GET | Last block's P_clear |
| `get_orderbook_order` | GET | Look up order by ID (status, filled amount) |
| `get_orderbook_estimates` | GET | Pre-flight: max fill size at current band + orderbook |
| `place_order` | POST | Submit ORDER_PLACE tx |
| `cancel_order` | POST | Submit ORDER_CANCEL tx |
| `market_buy` | POST | Submit market buy (HEAT → XFG) |
| `market_sell` | POST | Submit market sell (XFG → HEAT) |

---

## Wallet Commands

### Simple (recommended for most users)

| Command | Syntax |
|---------|--------|
| `trade` | `trade <buy|sell> <amount>` — shows pre-flight estimate, asks confirm, executes market order |

### Advanced

| Command | Syntax |
|---------|--------|
| `place_order` | `place_order <buy|sell> <amount> <price> [expiration_blocks]` |
| `cancel_order` | `cancel_order <order_id>` |
| `market_buy` | `market_buy <xfg_amount>` — buy XFG with HEAT |
| `market_sell` | `market_sell <xfg_amount>` — sell XFG for HEAT |
| `show_orders` | List own open orders with ID, side, amount, filled, price, expiration |
| `orderbook` | `orderbook [depth]` — top N bid/ask levels with aggregated depth |

### Pre-flight Confirmation

All market order commands (`trade`, `market_buy`, `market_sell`) output before executing:

```
=== MARKET SELL PREVIEW ===
Amount:          100.00 XFG
Clearing price:  0.12700000
HEARTH depth:    85.00 XFG available at 0.1270 (± spread)
Orderbook depth: 4 levels / max price 0.1445 (113% of clearing)
Worst case:      100.00 XFG filled, last fill at 0.1445
Fee:             0.3%
Proceed? [y/N]: _
```

### `place_order` Confirmation

```
Order placed: SELL 100.00 XFG @ 0.1275
Order ID:     abc123...
Filled:       0.00 / 100.00 XFG
Expires:      block 1,097,650 (approx 6 days 22 hours)
```

### `show_orders`

```
 ID         Side  Amount   Filled   Price    Expires (block)  Remaining
 abc123...  SELL  100.00   35.50    0.1275   1097650          4d 12h
 def456...  BUY    50.00    0.00    0.1265   1097800          6d 8h
```

### `orderbook 5`

```
=== ORDERBOOK (XFG/HEAT) ===
Clearing price: 0.12700000

ASKS (Sell XFG)         BIDS (Buy XFG)
Price      Depth(XFG)   Price      Depth(XFG)
0.1275     150.0        0.1265     200.0
0.1280      80.0        0.1260     450.0
0.1290     120.0        0.1255     100.0
0.1300     300.0        0.1250      75.0
0.1320      50.0        0.1245     300.0
```

---

## Wallet Scan Impact

Each partial fill creates one additional output for the seller to scan. Remainders use the same stealth address derivation as standard change outputs. In POC scale (tens of orders/block), overhead is negligible. If wallet sync degrades at scale, remainder outputs can be batched into a single per-block transaction per seller.

---

## Assumptions

- Participants accept ~8-min fill latency
- HEAT has sufficient circulating supply as quote asset
- Amounts are visible (no bulletproofs)
- Rebalance fund self-sustaining via mint premium over market cycles
- Wallet scan overhead from remainder outputs remains negligible at POC scale

---

## Decision Log

| # | Decision | Rationale |
|---|----------|-----------|
| D1 | Hybrid orderbook + HEARTH | Orderbook = price authority; HEARTH = instant depth band |
| D2 | Batch auction per 480s block | Fits block time; uniform clearing price resists MEV |
| D3 | Protocol rebalance at block close | Treasury swaps own pool to re-center to P_clear |
| D4 | Partial fills via remainder UTXOs | Destroy old order UTXO, create new remainder at same price |
| D5 | No PI controller | Hardcoded $1.58 peg; no dynamic error signal |
| D6 | Per-block mint cap | Replaces PI rate-limit as volume throttle |
| D7 | 2-3% mint premium | Funds rebalance treasury; structural friction against loops |
| D8 | 50% EF / 50% SWF on mint burn | EF = permanent deflation; SWF = cross-chain peg defense (separate from rebalance fund) |
| D9 | Cancel fee → Treasury → Marketing | Dust cost prevents cancel spam |
| D10 | 5 levels or 150% per-order guard | Whichever triggers first; bounds market order impact |
| D11 | Min 2 distinct parties per clearing | Tripwire; real defense is 150% price guard |
| D12 | UTXO-based orderbook storage | Blockchain IS orderbook state; wallet scanning discovers orders |
| D13 | LPs earn band trade fees | Treasury can participate as LP |
| D14 | HEARTH-only bootstrap (144 blocks) | Pool ratio serves as P_clear until orderbook reaches min depth |
| D15 | `trade` command as simple entry | Two-tier UX: simple (`trade`) and advanced (`place_order`) |
| D16 | Pre-flight confirmation mandatory | Market orders show estimate + worst case before executing |
| D17 | Limit orders fill only if P_clear favorable | Multi-unit uniform price auction semantics |
| D18 | MAX_ORDERS_PER_BLOCK = 1000 | Upper bound on block processing; configurable |
| D19 | SWF separate from rebalance fund | SWF = cross-chain; rebalance fund = on-chain, mint-premium-funded |

---

## Risks

| Risk | Mitigation |
|------|------------|
| Cold start — empty orderbook | HEARTH-only bootstrap (144 blocks); pool ratio establishes initial P_clear |
| 480s fill latency | Inherent to CryptoNote; acceptable for design goals |
| Rebalance fund drain (one-sided prolonged market) | Mint cap bounds per-block rebalance; premium + natural cycling refills both sides |
| Sybil parties at min-2 | 150% price guard is the real defense; raise to 3+ if exploited |
| Timing correlation reduces privacy | Same limitation as existing CryptoNote transfers; not orderbook-specific |
| Wallet scan bloat from remainder outputs | Batch remainder outputs per seller if detected at scale |
| Amounts visible to consensus/miners | No bulletproofs — known limitation; can upgrade later |
