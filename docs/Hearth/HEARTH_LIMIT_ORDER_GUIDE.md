# Hearth v11+ Developer Guide

## Pool-Backed Limit Order Architecture

### Overview

One pool inventory, two uses — not AMM *or* depth band. `reserveXfg × reserveHeat`
is the balance sheet (LP shares, direct swaps, mint-spot fallback, backstop).
The depth band is that inventory quoted as makers in the call auction. There is
no UTXO-level counterparty settlement (Phase 6b cancelled).

- **Depth band (pool makers)**: `generatePoolOrders()` builds 0xF0-prefixed
  orders from `HEARTH_DEPTH_BAND_PCT` of reserves at adaptive spread (30–300 bps).
  Injected into the per-block call auction. Fills move `reserve*`. Not display-only.
- **User limits**: `TransactionExtraLimitDeposit` one-sided escrow in pending.
  Auction first (may fill vs other users or vs pool makers). Unfilled remainder
  hits the AMM curve (backstop). Proceeds claimable via `TransactionExtraLimitWithdraw`.

### Three-Layer Reserve Model

```
┌────────────────────────────────────────────────────────────┐
│  Layer 1: Reserve Ratio (XFG / HEAT)                       │
│  ──────────────────────────────────                        │
│  reserveXfg + reserveHeat  ←  determines P_clear, mint ratio│
│  LP providers earn fees on this (spread from fills)        │
├────────────────────────────────────────────────────────────┤
│  Layer 2: Pending Deposits (one-sided)                     │
│  ──────────────────────────────────                        │
│  pendingXfg + pendingHeat  ←  limit order collateral       │
│  EXCLUDED from reserve ratio. Does NOT earn LP fees.       │
│  User can withdraw at any time before fill.               │
├────────────────────────────────────────────────────────────┤
│  Layer 3: Orderbook (mempool)                              │
│  ──────────────────────────────────                        │
│  User orders with targetPrice != 0 → Out-Of-Band           │
│  Pool orders with price in band → In-Band                  │
│  Market orders → immediate fill against pool orders        │
└────────────────────────────────────────────────────────────┘
```

### Key Invariants

| Concern                          | Rule                                     |
|----------------------------------|------------------------------------------|
| Reserve ratio calculation        | `reserveXfg / reserveHeat` ONLY          |
| pending does NOT affect ratio    | Always excluded                          |
| Limit orders earn LP fees?       | NO — only two-sided LP providers do      |
| Who gets the spread?             | LP providers (accumulatedLpFeesHeat/Xfg) |
| Limit order cancellation         | User withdraws from pending, order canceled |
| Pool order fill checks           | `data[0] == 0xF0` marks pool order      |
| User-first priority              | User orders fill before pool at same price |

### Data Structures

#### 1. AmmPoolState (src/CryptoNoteCore/AmmPool.h)
```
DONE:
  reserveXfg, reserveHeat          # two-sided, earns LP fees
  totalLpShares                    # LP tokens
  accumulatedLpFeesHeat/Xfg        # spread from fills
  pendingXfg, pendingHeat          # one-sided limit order deposits
  totalReserve() = reserveXfg + reserveHeat
  isEmpty() = reserveXfg==0 && reserveHeat==0
  serialize() includes pending fields
```

#### 2. Order (src/CryptoNoteCore/OrderbookTypes.h)
```
  side, amount, price              # standard order fields
  targetPrice                      # DONE: out-of-band target (0 = in-band)
  expiration, orderId, utxoTxHash
  outputIndex, spendKey, viewKey
  partialSigs                      # pre-signed adaptor signatures
```

#### 3. TransactionExtra Tags (src/CryptoNoteCore/TransactionExtra.h)
```
DONE:
  TX_EXTRA_LIMIT_DEPOSIT (0xFB)    # one-sided deposit into pending
  TX_EXTRA_LIMIT_WITHDRAW (0xFE)   # cancel + withdraw from pending
  Struct: TransactionExtraLimitDeposit { side, targetPrice, expiration, orderId }
  Struct: TransactionExtraLimitWithdraw { orderId }
  Parser cases in parseTransactionExtra
  Writer functions: addLimitDepositToExtra, addLimitWithdrawToExtra
  Added to TransactionExtraField variant type
```

### Transaction Flow

#### Placing a Limit Order (SELL_XFG example)
```
User                                   Pool
  │                                      │
  │  1. Create tx with:                  │
  │     - TransactionExtraLimitDeposit   │
  │       {side=1, targetPrice=P,        │
  │        expiration=H, orderId=hash}   │
  │     - Transfer XFG to pool output    │
  │─────────────────────────────────────>│
  │                                      │
  │                             2. connectTransaction:
  │                                pendingXfg += depositAmount
  │                                orderbookMempool.addOrder(order)
  │                                      │
  │  3. tx confirmed in block            │
  │     Order sits in mempool at        │
  │     targetPrice (outside band)       │
```

#### Order Fills (per block, processOrderbookForBlock)
```
At each block:
  1. expireOrders()
  2. PoolOrderOrchestrator regenerates band orders
  3. copyToIndex → snapshot for matching
  4. Matcher runs → P_clear computed

  FOR each out-of-band order in getOutOfBandOrders():
    IF side==SELL AND P_clear >= targetPrice:
      pendingXfg -= amount           # consume deposit
      reserveXfg += amount           # deposit → reserves
      reserveHeat -= fillCost        # pool pays counterparty
      accumulatedLpFeesHeat += spread
      orderbookMempool.fillOrder(orderId, amount)
      numMatches++
    IF side==BUY AND P_clear <= targetPrice:
      pendingHeat -= fillCost
      reserveHeat += fillCost
      reserveXfg -= amount
      accumulatedLpFeesHeat += spread
      orderbookMempool.fillOrder(orderId, amount)
      numMatches++
```

#### Withdrawing a Limit Order
```
User                                   Pool
  │                                      │
  │  1. Create tx with:                  │
  │     - TransactionExtraLimitWithdraw  │
  │       {orderId}                      │
  │     - Pool output → user output     │
  │─────────────────────────────────────>│
  │                                      │
  │                             2. connectTransaction:
  │                                pendingXfg -= depositAmount
  │                                (or pendingHeat)
  │                                orderbookMempool.cancelOrder(orderId)
  │                                      │
  │  3. Funds returned to user           │
```

### Remaining Work (Consolidated)

All phases complete except the items below. No more "Phase 2/3/4" splits.

#### PRIORITY 1: HTTP RPC Endpoints for Market Makers

Expose orderbook operations via HTTP JSON-RPC so bots and market makers
can provide liquidity programmatically.

| Endpoint | Method | Params | Returns |
|----------|--------|--------|---------|
| `/place_order` | POST | `{side, amount, price, type?"limit"\|"market"}` | `{orderId, status}` |
| `/cancel_order` | POST | `{orderId}` | `{status}` |
| `/get_orderbook` | GET | `{depth?50}` | `{bids[], asks[], clearingPrice}` |
| `/market_buy` | POST | `{amount}` | `{fillPrice, totalCost, fills[]}` |
| `/market_sell` | POST | `{amount}` | `{fillPrice, totalProceeds, fills[]}` |
| `/get_ooo_orders` | GET | `{}` | `{pendingXfg, pendingHeat, orders[]}` |

Files:
- `src/Rpc/CoreRpcServerCommandsDefinitions.h` — request/response structs
- `src/Rpc/RpcServer.cpp` — handler implementations + route registration
- `src/Rpc/RpcServer.h` — handler declarations

#### PRIORITY 2: Wallet Transaction Building

The wallet-side stubs currently return `INTERNAL_WALLET_ERROR`. Implement
actual transaction construction for order placement/cancellation.

| Stub | Location | What to build |
|------|----------|---------------|
| `makePlaceOrderV13Request` | `WalletTransactionSender.cpp:2772` | Build tx with `TX_EXTRA_LIMIT_DEPOSIT` + transfer output |
| `makeCancelOrderV13Request` | `WalletTransactionSender.cpp:2781` | Build tx with `TX_EXTRA_LIMIT_WITHDRAW` |
| `makeMarketBuyV13Request` | `WalletTransactionSender.cpp:2793` | Build market buy tx (no deposit, direct swap) |
| `makeMarketSellV13Request` | `WalletTransactionSender.cpp:2806` | Build market sell tx (no deposit, direct swap) |

#### PRIORITY 3: Band Consumption Tracking

`bandFilled` is hardcoded to 0 in `processOrderbookForBlock()` (Blockchain.cpp:3614).
This means the adaptive spread consumption multiplier never triggers.

Fix: track how much of the pool order band was consumed each block and pass
it to `PoolOrderOrchestrator::onBlockProcessed()`.

#### PRIORITY 4: Re-enable Phase 3 Tests

`tests/CoreTests/Phase3_OrderbookTest.cpp` is entirely `#if 0`.
References deleted `HearthRebalance` class and old `MarketOrderExecutor` API.

Update to current API and re-enable.

#### PRIORITY 5: Expired Order Auto-Return

When `currentHeight >= order.expiration`:
1. Order expired from mempool ✅ (already works)
2. Pending deposit must be returned to user

Options:
- **A**: Miner creates return transaction (consensus rule)
- **B**: User submits withdrawal tx referencing expired orderId (simpler)

Recommendation: Option B — user calls `cancel_limit` after expiry, withdrawal
validates `height >= expiration` and returns full deposit.

#### PRIORITY 6: Market Maker Documentation

Create `HEARTH_MARKET_MAKER_GUIDE.md` with:
- How to connect to the daemon
- How to place/cancel orders via HTTP RPC
- How to read the orderbook
- Fee model (LP fees, spread, no deposit fees)
- Risk management (price deviation guards, band consumption)
- Example bot integration (Python/curl)

---

### Completed Work (Reference)

| Component | Status | Location |
|-----------|--------|----------|
| OrderbookIndex (CLOB) | ✅ Done | OrderbookIndex.h/cpp |
| OrderbookMempool | ✅ Done | OrderbookMempool.h/cpp |
| OrderbookMatcher (batch) | ✅ Done | OrderbookMatcher.h/cpp |
| PoolOrderOrchestrator | ✅ Done | PoolOrderOrchestrator.h/cpp |
| MarketOrderExecutor | ✅ Done | MarketOrderExecutor.h/cpp |
| OrderbookP2pHandler | ✅ Done | OrderbookP2pHandler.h/cpp |
| Pool order generation | ✅ Done | OrderbookTypes.h/cpp |
| Block processing integration | ✅ Done | Blockchain.cpp:3589 |
| AMM constant product | ✅ Done | AmmPool.h/cpp |
| AMM LP deposit/withdrawal | ✅ Done | AmmPool.h/cpp |
| HEAT minting (HeatMintEngine) | ✅ Done | HeatMintEngine.h/cpp |
| connectTransaction / rollback | ✅ Done | Blockchain.cpp |
| Out-of-band fill settlement | ✅ Done | Blockchain.cpp |
| RPC: /heat_metrics, /amm_quote, /amm_pool_info | ✅ Done | RpcServer.cpp |
| Unit tests (34) | ✅ Done | tests/CoreTests/ |

### Fee Model Summary

| Fill type          | Who pays        | Who earns         |
|-------------------|-----------------|-------------------|
| Pool order fills  | Buyer (in price)| LP reserves       |
| User market buy   | No extra fee   | Pool via spread   |
| User market sell  | No extra fee   | Pool via spread   |
| Out-of-band fills | No extra fee   | LP reserves via spread |
| Mint premium      | Mint user      | Treasury          |

The pool earns spread on every fill (|fillPrice - P_clear| × amount / COIN).
This accumulates in `accumulatedLpFeesHeat`/`accumulatedLpFeesXfg`.

Limit order depositors do NOT earn any LP fees — they are not providing
two-sided liquidity. They place a one-sided order, deposit collateral,
and receive exactly their target price when filled.

### Files Modified (this implementation)

| File                                    | Change                                      |
|-----------------------------------------|---------------------------------------------|
| `src/CryptoNoteCore/AmmPool.h`          | + pendingXfg, pendingHeat, totalReserve()    |
| `src/CryptoNoteCore/AmmPool.cpp`        | serialize pending fields                    |
| `src/CryptoNoteCore/TransactionExtra.h` | + LimitDeposit/Withdraw structs, tags, decls|
| `src/CryptoNoteCore/TransactionExtra.cpp`| + parser, writer, helper functions         |
| `src/CryptoNoteCore/OrderbookTypes.h`   | + targetPrice on Order                      |
| `src/CryptoNoteCore/OrderbookMempool.h` | + fillOrder, OutOfBandOrder, getOutOfBandOrders|
| `src/CryptoNoteCore/OrderbookMempool.cpp`| + fillOrder, getOutOfBandOrders impl       |
| `src/CryptoNoteCore/Blockchain.cpp`     | out-of-band fill in processOrderbookForBlock|

### Files to Modify (remaining)

| File | Task |
|------|------|
| `src/Rpc/CoreRpcServerCommandsDefinitions.h` | Add place_order, cancel_order, get_orderbook, market_buy, market_sell request/response structs |
| `src/Rpc/RpcServer.cpp` | Add HTTP route handlers for all endpoints above |
| `src/Rpc/RpcServer.h` | Add handler declarations |
| `src/WalletLegacy/WalletTransactionSender.cpp` | Implement makePlaceOrderV13Request, makeCancelOrderV13Request, makeMarketBuyV13Request, makeMarketSellV13Request |
| `src/CryptoNoteCore/Blockchain.cpp` | Fix bandFilled tracking (line 3614) |
| `tests/CoreTests/Phase3_OrderbookTest.cpp` | Re-enable tests, update to current API |
| `HEARTH_MARKET_MAKER_GUIDE.md` | New file — market maker integration docs |

### FAQ

#### What happens when the band is filled?

The depth band is the pool quoting its own reserves as auction makers
(`generatePoolOrders`, 0xF0 order ids):

1. **Rebuilt every block** from the reserves as the block's transactions left
   them, centred on the pool's spot price — not on the last clearing price.
2. **Priced on the curve** — each of the 10 tiers per side sits at what the
   constant-product curve pays (bids) or charges (asks) for that tier once the
   nearer tiers are gone, less/plus half the spread. A band fill never beats a
   swap of the same size, so the product of the reserves cannot fall.
3. **Spread widens with consumption** — `g_poolBandFilledLastBlock` records the
   XFG of pool orders filled in the prior block; the spread rises from 30 bps
   toward 300 bps as more of the band is consumed.
4. **The pool is always the maker** — whoever trades against the band pays the
   1% taker fee; the pool keeps the maker rebate in its reserves (LPs).

#### Why and when does an order expire?

Every resting deposit is scanned in every block (expiry, auction, backstop),
so consensus bounds the set:

| Rule | Value | Constant |
|------|-------|----------|
| Expiry required | `height < expiration ≤ height + 5,400` (~30 days) | `LIMIT_ORDER_MAX_TTL_BLOCKS` |
| Wallet default | 1,260 blocks (~7 days) when the caller passes 0 | `LIMIT_ORDER_DEFAULT_TTL_BLOCKS` |
| Minimum size | 1 XFG (sell) / 0.1 HEAT (buy) | `LIMIT_ORDER_MIN_XFG`, `LIMIT_ORDER_MIN_HEAT` |
| New deposits per block | 100 | `LIMIT_DEPOSITS_MAX_PER_BLOCK` |

`expiration` is an absolute block height. At that height the order stops
filling; the remaining deposit and all proceeds stay claimable with
`TransactionExtraLimitWithdraw`. Withdrawn orders are pruned from the set once
they are older than the 1,000-block rollback window.

### Testing Strategy

1. **Unit**: deposit/withdraw pending, fill validates pending≥amount
2. **Integration**: full block flow — deposit, price moves, fill, LP fees
3. **Edge cases**: 
   - Reserve exhaustion during fill → partial fill only
   - Expired order auto-return
   - Withdraw after partial fill
   - Multiple orders at same targetPrice
   - Bootstrap window: pending deposits allowed during bootstrap
