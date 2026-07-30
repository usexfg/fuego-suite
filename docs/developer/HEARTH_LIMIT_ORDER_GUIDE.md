# Hearth v11+ Developer Guide

## Pool-Backed Limit Order Architecture

### Overview

Hearth settles ALL orders through the pool. There is no UTXO-level
counterparty settlement (Phase 6b is cancelled). Instead:

- **In-band (depth band)**: pool-generated orders at adaptive spread. User
  market orders fill against pool orders.
- **Out-of-band (limit orders)**: user places limit order with one-sided
  deposit into pool's pending reserves. When P_clear reaches target price,
  pool provides counterparty from reserves. Pending → reserves on fill.

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

When pool orders (the auto-generated depth band) are consumed by trades:

1. **bandFilled is tracked** — each block, `g_orderbookLastBandFilled` records
   the total XFG amount of pool orders filled in the prior block
2. **Spread widens** — the adaptive spread's consumption multiplier kicks in,
   increasing the spread (base 30 bps → up to 300 bps). A consumed band means
   the pool's liquidity is thinner, so the spread reflects that risk
3. **Regeneration triggers** — if consumption exceeds 50% of the band
   (`bandFilled > 0.5 × bandPlaced`), pool orders are regenerated at the next block
   with the widened spread
4. **Pool reserves adjust** — pool order fills directly update `reserveXfg`/`reserveHeat`,
   so a filled band shifts the pool's reserve ratio

In short: filling the band costs the taker more (wider spread) and the pool
refreshes its liquidity at the new price level.

#### Why and when does an order expire?

Orders expire because this is a **P2P orderbook** — orders live in the mempool
and propagate via P2P gossip. If orders never expired, stale orders from
offline nodes would permanently clutter the book and waste validator work.

| Setting | Value | Meaning |
|---------|-------|---------|
| Default expiry | 4,320 blocks (~3 days) | `ORDERBOOK_DEFAULT_EXPIRY` |
| Maximum endurance | 20,160 blocks (~14 days) | `ORDERBOOK_MAX_ENDURANCE` |
| No expiry | `expiration = 0` | Disabled (not recommended for P2P) |

At each block, `OrderbookMempool::expireOrders(currentHeight)` removes any
order where `expiration != 0 && currentHeight > expiration`. The deposit
stays in `pendingXfg`/`pendingHeat` until the user calls `cancel_order`.

**Why 3 days?** Nobody wants to re-add orders constantly, but P2P mempool
orders shouldn't sit forever. 3 days gives market makers enough runway
without polluting the mempool. Users can set shorter expiry for short-term
trades or longer (up to 14 days) for passive strategies.

### Testing Strategy

1. **Unit**: deposit/withdraw pending, fill validates pending≥amount
2. **Integration**: full block flow — deposit, price moves, fill, LP fees
3. **Edge cases**: 
   - Reserve exhaustion during fill → partial fill only
   - Expired order auto-return
   - Withdraw after partial fill
   - Multiple orders at same targetPrice
   - Bootstrap window: pending deposits allowed during bootstrap
