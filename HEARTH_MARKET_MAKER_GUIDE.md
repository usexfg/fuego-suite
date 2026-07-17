# Hearth Market Maker Guide

## Overview

Hearth is a CLOB (Central Limit Order Book) with pool-generated depth bands.
Market makers provide two-sided liquidity by placing limit orders that sit in
the orderbook until filled.

### Architecture

```
User Limit Orders (out-of-band)
    │
    ▼
┌─────────────────────────────────────────┐
│  Orderbook Mempool                      │
│  ┌─────────┐  ┌─────────┐              │
│  │  Bids   │  │  Asks   │              │
│  │ (buy XFG)│  │(sell XFG)│             │
│  └─────────┘  └─────────┘              │
│       │            │                    │
│       ▼            ▼                    │
│  ┌─────────────────────────┐            │
│  │  Pool Depth Band        │            │
│  │  (auto-generated orders)│            │
│  │  Adaptive spread 30-300bps│          │
│  └─────────────────────────┘            │
└─────────────────────────────────────────┘
            │
            ▼
    Block Finalization
    (VWAP P_clear computed)
```

### How It Works

1. Each block, the `PoolOrderOrchestrator` generates pool orders at an adaptive
   spread around the current price (P_clear)
2. Users place limit orders with a `targetPrice` (out-of-band)
3. At block finalization, `OrderbookMatcher` batch-matches all overlapping orders
4. VWAP clearing price (P_clear) is written to the block header
5. Pool order fills update reserves and accumulate LP fees
6. Out-of-band fills move pending deposits into reserves

## Connecting to the Daemon

### HTTP JSON-RPC

All endpoints are HTTP JSON-RPC. Default port: 18081.

```bash
# Check daemon is running
curl http://localhost:18081/height

# Get orderbook
curl http://localhost:18081/get_orderbook -d '{"depth": 20}'
```

### Authentication

- Read-only endpoints (`/get_orderbook`, `/heat_metrics`, `/amm_pool_info`):
  no auth required
- Write endpoints (`/place_order`, `/cancel_order`, `/market_buy`, `/market_sell`):
  require restricted RPC access or wallet keys

## HTTP RPC Endpoints

### GET /get_orderbook

Returns the current orderbook state with bid/ask depth.

**Request:**
```json
{
  "depth": 50  // optional, number of price levels per side (default 50)
}
```

**Response:**
```json
{
  "clearing_price": 12640000,      // P_clear × 10^8
  "pool_ratio": 10000000,          // reserveXfg/reserveHeat × 10^8
  "pending_xfg": 50000000000,      // total pending XFG deposits
  "pending_heat": 1000000000,      // total pending HEAT deposits
  "num_matches": 15,               // matches in last block
  "bids": [                        // sorted descending by price
    {"price": 12630000, "total_amount": 5000000000, "order_count": 3},
    {"price": 12620000, "total_amount": 8000000000, "order_count": 5}
  ],
  "asks": [                        // sorted ascending by price
    {"price": 12650000, "total_amount": 3000000000, "order_count": 2},
    {"price": 12660000, "total_amount": 6000000000, "order_count": 4}
  ],
  "status": "OK"
}
```

**Python example:**
```python
import requests

def get_orderbook(depth=50):
    r = requests.post("http://localhost:18081/get_orderbook",
                       json={"depth": depth})
    data = r.json()
    print(f"P_clear: {data['clearing_price'] / 1e8:.4f}")
    print(f"Bids: {len(data['bids'])} levels")
    print(f"Asks: {len(data['asks'])} levels")
    return data
```

### POST /place_order

Place a limit order on the orderbook.

**Request:**
```json
{
  "side": 1,              // 0 = buy XFG (pay HEAT), 1 = sell XFG (receive HEAT)
  "amount": 1000000000,   // atomic units (1 XFG = 10^7)
  "target_price": 12700000, // limit price × 10^8 (0 = market order)
  "expiration": 0          // block height for expiry (0 = no expiry)
}
```

**Response:**
```json
{
  "order_id": "a1b2c3d4...",
  "tx_hash": "e5f6g7h8...",
  "deposited_amount": 1000000000,
  "status": "OK"
}
```

**Python example:**
```python
def place_limit_sell(xfg_amount, target_price):
    """Sell XFG at a specific price or better."""
    r = requests.post("http://localhost:18081/place_order", json={
        "side": 1,
        "amount": xfg_amount,
        "target_price": target_price,
        "expiration": 0
    })
    return r.json()

def place_limit_buy(heat_amount, target_price):
    """Buy XFG at a specific price or better."""
    r = requests.post("http://localhost:18081/place_order", json={
        "side": 0,
        "amount": heat_amount,
        "target_price": target_price,
        "expiration": 0
    })
    return r.json()
```

### POST /cancel_order

Cancel an existing limit order and return the deposit.

**Request:**
```json
{
  "order_id": "a1b2c3d4..."
}
```

**Response:**
```json
{
  "returned_amount": 1000000000,
  "tx_hash": "i9j0k1l2...",
  "status": "OK"
}
```

### POST /market_buy

Instant market buy — fills immediately against the orderbook.

**Request:**
```json
{
  "amount": 5000000000  // XFG to buy (atomic units)
}
```

**Response:**
```json
{
  "total_cost_heat": 6350000000,
  "average_price": 12700000,
  "fills": [
    {"price": 12650000, "filled_amount": 3000000000, "is_pool_order": true},
    {"price": 12700000, "filled_amount": 2000000000, "is_pool_order": false}
  ],
  "status": "OK"
}
```

### POST /market_sell

Instant market sell — fills immediately against the orderbook.

**Request:**
```json
{
  "amount": 5000000000  // XFG to sell (atomic units)
}
```

**Response:**
```json
{
  "total_proceeds_heat": 6320000000,
  "average_price": 12640000,
  "fills": [
    {"price": 12650000, "filled_amount": 2000000000, "is_pool_order": true},
    {"price": 12640000, "filled_amount": 3000000000, "is_pool_order": false}
  ],
  "status": "OK"
}
```

### GET /heat_metrics

Returns HEAT supply, treasury, and redemption price.

```json
{
  "heat_supply": "1234567890",
  "burned_xfg": "50000000000",
  "redemption_price_num": "158",
  "redemption_price_denom": "100",
  "treasury_balance": "25000000000",
  "epoch_swap_fees": "100000000",
  "status": "OK"
}
```

### GET /amm_pool_info

Returns AMM pool reserves and LP info.

```json
{
  "reserve_xfg": 10000000000,
  "reserve_heat": 1000000000,
  "total_lp_shares": 3162277,
  "spot_price": 10000000,
  "accumulated_lp_fees": 50000000,
  "epoch_swap_fees": 100000000,
  "status": "OK"
}
```

## Price Model

### Clearing Price (P_clear)

The VWAP of all matched trades in a block. Written to every block header.

```
P_clear = sum(fill_amount_i × fill_price_i) / sum(fill_amount_i)
```

### Pool Ratio

The current XFG/HEAT reserve ratio determines the mint redemption price:

```
pool_ratio = reserveXfg / reserveHeat
```

### Price Hierarchy

1. **P_clear** (30-block TWAP) — primary price feed
2. **Pool ratio** — fallback when no orderbook matches
3. **$1.58 peg** — HEAT reference price (static, CPI adjustment planned)

## Fee Model

| Fill Type | Fee | Who Pays | Who Earns |
|-----------|-----|----------|-----------|
| Pool order fill | Spread from P_clear | Taker | LP reserves |
| Out-of-band fill | No extra fee | — | LP reserves via spread |
| Market buy/sell | Spread from P_clear | Taker | LP reserves |
| Limit order deposit | None | — | — |
| Mint premium | 3.33% | Mint user | Treasury |

### LP Fee Accumulation

Fees accumulate in `accumulatedLpFeesHeat` and `accumulatedLpFeesXfg`:

```
fee = |fill_price - P_clear| × fill_amount / COIN
```

LP providers earn these fees proportional to their share of the pool.

### No Deposit Fees

Placing and cancelling limit orders costs only the transaction fee.
There is no additional fee for depositing into pending reserves.

## Risk Management

### Price Deviation Guard

Market orders have a configurable max price deviation:

```python
MAX_DEVIATION_PCT = 5  # 5% max deviation from spot

def safe_market_buy(amount):
    quote = get_orderbook_estimate(amount)
    spot = get_pool_spot_price()
    deviation = abs(quote['average_price'] - spot) / spot * 100

    if deviation > MAX_DEVIATION_PCT:
        raise Exception(f"Price deviation {deviation:.1f}% exceeds limit")
    return market_buy(amount)
```

### Spread Monitoring

The adaptive spread adjusts based on:

- **Volatility**: Higher volatility → wider spread (up to 300 bps)
- **Band consumption**: More fills → wider spread
- **Time since regen**: More blocks → wider spread

Monitor spread via `/get_orderbook`:
```python
def get_spread():
    book = get_orderbook(depth=1)
    if book['bids'] and book['asks']:
        best_bid = book['bids'][0]['price']
        best_ask = book['asks'][0]['price']
        spread_bps = (best_ask - best_bid) / best_bid * 10000
        return spread_bps
    return 0
```

### Order Expiration

Set `expiration` to a block height to auto-expire orders:

```python
current_height = get_height()
expiration = current_height + 1000  # expire in ~1000 blocks (~16 hours)
place_limit_sell(amount, target_price, expiration)
```

After expiry, call `/cancel_order` to withdraw the deposit.

### Position Sizing

Recommended max position per order:

```python
def max_order_size(book_depth_pct=0.1):
    """Max order at 10% of visible depth."""
    book = get_orderbook(depth=20)
    total_depth = sum(l['total_amount'] for l in book['bids'])
    return int(total_depth * book_depth_pct)
```

## Example Market Maker Bot

```python
import requests
import time

DAEMON = "http://localhost:18081"
SPREAD_BPS = 50  # 0.5% spread target

def get_height():
    r = requests.get(f"{DAEMON}/height")
    return r.json()['height']

def get_orderbook(depth=10):
    r = requests.post(f"{DAEMON}/get_orderbook", json={"depth": depth})
    return r.json()

def get_pool_spot():
    r = requests.get(f"{DAEMON}/amm_pool_info")
    info = r.json()
    return info['spot_price']

def place_order(side, amount, price):
    r = requests.post(f"{DAEMON}/place_order", json={
        "side": side,
        "amount": amount,
        "target_price": price,
        "expiration": get_height() + 2000
    })
    return r.json()

def cancel_order(order_id):
    r = requests.post(f"{DAEMON}/cancel_order", json={"order_id": order_id})
    return r.json()

class MarketMaker:
    def __init__(self, spread_bps=50):
        self.spread_bps = spread_bps
        self.active_orders = {}

    def update_orders(self):
        """Cancel stale orders and place new ones around P_clear."""
        book = get_orderbook(depth=5)
        p_clear = book['clearing_price']

        # Cancel orders that are too far from current price
        for oid, order in list(self.active_orders.items()):
            if abs(order['price'] - p_clear) / p_clear > 0.05:
                cancel_order(oid)
                del self.active_orders[oid]

        # Place new bid/ask if none exist
        half_spread = p_clear * self.spread_bps / 10000 / 2
        bid_price = int(p_clear - half_spread)
        ask_price = int(p_clear + half_spread)

        if not any(o['side'] == 1 for o in self.active_orders.values()):
            amount = 1000000000  # 100 XFG
            result = place_order(1, amount, ask_price)
            if result['status'] == 'OK':
                self.active_orders[result['order_id']] = {
                    'side': 1, 'price': ask_price, 'amount': amount
                }

        if not any(o['side'] == 0 for o in self.active_orders.values()):
            amount = 1000000000  # 100 XFG equivalent
            result = place_order(0, amount, bid_price)
            if result['status'] == 'OK':
                self.active_orders[result['order_id']] = {
                    'side': 0, 'price': bid_price, 'amount': amount
                }

    def run(self, interval=60):
        """Run market maker, updating every `interval` seconds."""
        print("Starting Hearth market maker...")
        while True:
            try:
                self.update_orders()
                print(f"[{get_height()}] Orders: {len(self.active_orders)}")
            except Exception as e:
                print(f"Error: {e}")
            time.sleep(interval)

if __name__ == "__main__":
    mm = MarketMaker(spread_bps=SPREAD_BPS)
    mm.run()
```

## Orderbook Depth Analysis

### Reading Depth

```python
def analyze_depth(book):
    """Analyze orderbook depth and liquidity."""
    bid_depth = sum(l['total_amount'] for l in book['bids'])
    ask_depth = sum(l['total_amount'] for l in book['asks'])

    # Find walls (large orders)
    bid_walls = [l for l in book['bids']
                 if l['total_amount'] > bid_depth * 0.1]
    ask_walls = [l for l in book['asks']
                 if l['total_amount'] > ask_depth * 0.1]

    return {
        'bid_depth': bid_depth,
        'ask_depth': ask_depth,
        'imbalance': bid_depth / max(ask_depth, 1),
        'bid_walls': bid_walls,
        'ask_walls': ask_walls
    }
```

### Fair Value Estimation

```python
def fair_value(book):
    """Volume-weighted average of top N levels."""
    top_n = 5
    bids = book['bids'][:top_n]
    asks = book['asks'][:top_n]

    total_vol = sum(l['total_amount'] for l in bids + asks)
    if total_vol == 0:
        return book['clearing_price']

    vwap = sum(l['price'] * l['total_amount'] for l in bids + asks) / total_vol
    return vwap
```

## Troubleshooting

### Common Errors

| Error | Cause | Fix |
|-------|-------|-----|
| `"Amount must be > 0"` | Zero amount | Use atomic units (1 XFG = 10^7) |
| `"Side must be 0 or 1"` | Invalid side | 0=buy XFG, 1=sell XFG |
| `"placeOrder not yet implemented"` | Wallet tx building not done | Use wallet CLI instead |
| `"Core is busy"` | Daemon syncing | Wait for sync to complete |

### Current Limitations

1. **place_order/cancel_order** HTTP endpoints return stub errors — use wallet
   CLI (`place_limit_sell`, `cancel_limit`) for actual order placement
2. **market_buy/market_sell** HTTP endpoints return stub errors — use wallet
   CLI (`hearth_xfg`, `hearth_heat`) for actual market orders
3. **Expired order auto-return** requires user to call `cancel_order` after expiry

### Wallet CLI Alternatives

Until HTTP RPC is fully implemented, use the wallet CLI:

```bash
# Place limit sell (100 XFG at price 1.27)
./fire_wallet place_limit_sell 100 12700000

# Place limit buy (50 HEAT equivalent at price 1.27)
./fire_wallet place_limit_buy 500000000 12700000

# Cancel order
./fire_wallet cancel_limit <order_id>

# Market buy (100 XFG)
./fire_wallet hearth_xfg 100

# Market sell (100 XFG)
./fire_wallet hearth_heat 100
```

## Constants Reference

| Constant | Value | Meaning |
|----------|-------|---------|
| HEARTH_FEE_BPS | 30 | 0.3% swap fee to LPs |
| HEAT_PEG_USD | 1.58 | HEAT reference price |
| HEAT_MINT_PREMIUM_BPS | 333 | 3.33% mint premium |
| MIN_DISTINCT_PARTIES | 2 | Anti-manipulation minimum |
| HEARTH_DEPTH_BAND_PCT | 10 | Pool order band width |
| HEARTH_POOL_SEED_XFG | 10,000 | Genesis pool XFG |
| HEARTH_POOL_SEED_HEAT | 1,000 | Genesis pool HEAT |
| BOOTSTRAP_BLOCKS | 144 | AMM-only bootstrap period |
| MAX_SPREAD_BPS | 300 | Maximum adaptive spread |
