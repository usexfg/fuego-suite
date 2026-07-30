# Fuego Orderbook — Implementation Plan

Status: **APPROVED** | Design: `docs/orderbook/FUEGO_ORDERBOOK_DESIGN.md` | 2026-06-26

---

## Phase Map

```
Phase 1         Phase 2          Phase 3          Phase 4          Phase 5
[Data+Match] → [Consensus]   →  [HEARTH+Cascade]→ [Wallet+RPC]  → [Guards+Tests]
 ~2 weeks       ~2 weeks        ~2 weeks          ~1 week          ~1 week
```

---

## Phase 1: Orderbook Data Structures + Matching Engine

**Goal**: Order data structures, serialization, and matching logic — testable in isolation, no consensus integration.

### 1.1 New Output Type

- [ ] Define `TX_OUT_ORDER` in `src/CryptoNoteCore/CryptoNoteBasic.h` / `CryptoNoteFormatUtils`
- [ ] Extend transaction output types enum with `TX_OUT_ORDER`
- [ ] Order fields in unlocking condition struct:
  - `uint8_t side` (0 = BUY_XFG, 1 = SELL_XFG)
  - `uint64_t price` (XFG/HEAT × 10^7)
  - `uint64_t amount` (atomic units)
  - `uint32_t expiration` (block height)
  - `Crypto::PublicKey spend_key`
  - `Crypto::PublicKey view_key`
- [ ] Serialization for `TransactionOutput` including order fields
- [ ] Deserialization with size validation
- [ ] Hash inclusion in transaction merkle tree

### 1.2 Transaction Subtypes

- [ ] Define `ORDER_PLACE`, `ORDER_CANCEL`, `BUY_XFG`, `SELL_XFG` in transaction extra
- [ ] Transaction validation stubs (return true, Phase 2 adds real checks)
- [ ] Ring signature mixing for order transactions (standard CryptoNote mixing)

### 1.3 Orderbook Index

- [ ] `OrderbookIndex` class in `src/CryptoNoteCore/OrderbookIndex.h/.cpp`
  - `bid_curve`: `std::map<uint64_t, std::vector<OrderEntry>, std::greater<uint64_t>>`
  - `ask_curve`: `std::map<uint64_t, std::vector<OrderEntry>>`
  - `per_sender_count`: `std::unordered_map<Crypto::Hash, uint32_t>`
- [ ] `addOrder(OrderEntry)` — insert into correct curve
- [ ] `removeOrder(order_id)` — remove from curve, decrement sender count
- [ ] `getBidCurve()` / `getAskCurve()` — return iterators
- [ ] `getSenderCount(sender_key)` — flood guard query
- [ ] Rebuild index from UTXO set on blockchain load

### 1.4 Matching Engine

- [ ] `OrderbookMatcher` class in `src/CryptoNoteCore/OrderbookMatcher.h/.cpp`
- [ ] `match(OrderbookIndex& index, uint64_t prev_P_clear) → MatchResult`
  - Sort both curves
  - Walk bids descending, asks ascending
  - Fulfillable filter: only orders where P_clear is favorable to limit
  - Compute volume-weighted average P_clear
  - Return matched pairs + remainder amounts + P_clear
- [ ] `MatchResult` struct:
  - `vector<Fill> fills` (order_id_bid, order_id_ask, amount, price)
  - `vector<Remainder> remainders` (order_id, remaining_amount)
  - `uint64_t P_clear`
  - `uint32_t num_matches`
  - `bool clearing_valid` (false if < MIN_DISTINCT_PARTIES)

### 1.5 Unit Tests

- [ ] Single bid + single ask, same price → full match
- [ ] Multiple overlapping bids/asks → priority by price then time
- [ ] One-sided book → no matches, P_clear unchanged
- [ ] Partial fill → remainder computed correctly
- [ ] Limit price guard: bid at 0.130, P_clear=0.135 → bid excluded (unfavorable)
- [ ] Limit price guard: ask at 0.120, P_clear=0.115 → ask excluded (unfavorable)
- [ ] Empty book → no crash, no matches
- [ ] MAX_ORDERS_PER_BLOCK enforced → excess carries forward
- [ ] Min 2 distinct parties → 1 party = no clearing
- [ ] Order expiration → expired orders excluded from matching
- [ ] Serialization roundtrip for TX_OUT_ORDER

---

## Phase 2: Consensus Integration

**Goal**: Block finalization runs matching, constructs settlement txs, writes P_clear to header.

### 2.1 Block Header Extension

- [ ] Add fields to `BlockTemplate` / `Block` in `src/CryptoNoteCore/Block.h`:
  - `uint64_t orderbookClearingPrice`
  - `uint32_t orderbookNumMatches`
  - `uint32_t orderbookDepthBidXfg`
  - `uint32_t orderbookDepthAskXfg`
  - `uint64_t hearthPoolRatio`
- [ ] Serialization in `CryptoNoteSerialization.h/.cpp`
- [ ] Default values (0) for pre-orderbook blocks (backward compatibility)

### 2.2 Block Finalization Hook

- [ ] In `Blockchain.cpp::addBlock()` or `Core::handle_incoming_block()`:
  1. After standard tx validation, before block acceptance
  2. Collect `TX_OUT_ORDER` UTXOs from current UTXO set → OrderbookIndex
  3. Run `OrderbookMatcher::match()`
  4. If `clearing_valid`:
     - Construct settlement transactions (see 2.3)
     - Write P_clear to block header
  5. If not valid (or bootstrap):
     - P_clear = prior block's P_clear (or pool ratio if bootstrap)
  6. After matching, execute rebalance (Phase 3, stub for now)

### 2.3 Settlement Transaction Construction

- [ ] `OrderbookSettlement::build()` in `src/CryptoNoteCore/OrderbookSettlement.h/.cpp`
  - For each `Fill` in `MatchResult`:
    - Input: bid_order_UTXO + ask_order_UTXO
    - Output: `amount` XFG to bidder, `amount × P_clear` HEAT to asker
  - For each `Remainder`:
    - New `TX_OUT_ORDER` UTXO with same price/expiration, remaining amount
    - Stealth address derived from original sender's keys
  - Ring signatures: use matched counterparty outputs as mixins
  - Transaction constructed atomically — all fills in one block are a single settlement tx or a batch
- [ ] Settlement transaction must be valid under standard CryptoNote rules
- [ ] Fee handling: taker pays fee; subtracted from output amount

### 2.4 Memory Pool Integration

- [ ] `ORDER_PLACE` tx: validate order params (amount > dust, price > 0, expiration > current height)
- [ ] `ORDER_CANCEL` tx: verify sender owns the order UTXO
- [ ] `BUY_XFG` / `SELL_XFG` (market): validate max slippage param
- [ ] All passed to block finalization, not mined into blocks directly — consensus builds settlement

### 2.5 Genesis / Bootstrap

- [ ] Block version check: `if (height < UPGRADE_HEIGHT_ORDERBOOK)` → skip all orderbook logic
- [ ] Bootstrap blocks (height `UPGRADE_HEIGHT_ORDERBOOK` to `UPGRADE_HEIGHT_ORDERBOOK + 144`):
  - P_clear = HEARTH pool ratio
  - Market orders: HEARTH band only (no cascade)
  - Limit orders accepted but not matched (orderbook building)

### 2.6 Tests

- [ ] Full block: place 3 orders, mine block, verify P_clear computed
- [ ] Block with no orders → P_clear unchanged from prior
- [ ] Block with 1 party only → no clearing (min parties guard)
- [ ] Block with expired + active orders → only active matched
- [ ] Settlement transaction roundtrip validation
- [ ] Remainder UTXOs spendable by original sender
- [ ] Bootstrap: block < UPGRADE_HEIGHT skips orderbook logic

---

## Phase 3: HEARTH Depth Band + Market Orders + Cascade

**Goal**: Market orders consume HEARTH band, cascade into orderbook, treasury rebalances.

### 3.1 HEARTH Depth Band Calculations

- [ ] `HearthDepthBand` class in `src/CryptoNoteCore/HearthDepthBand.h/.cpp`
  - `calculateBandDepth()`: `pool_reserve * HEARTH_DEPTH_BAND_PCT / 100`
  - `availableSellDepth()`: XFG available at current P_clear
  - `availableBuyDepth()`: HEAT available at current P_clear
  - `consumeBand(amount, side)` → `{filled, remaining}`

### 3.2 Rebalance Transaction

- [ ] `HearthRebalance::execute()` in `src/CryptoNoteCore/HearthRebalance.h/.cpp`
  - Read current pool ratio from treasury tracker
  - Compute swap direction and amount:
    ```
    if R_drifted < P_clear:
        compute how much HEAT → XFG needed to reach P_clear
    else:
        compute how much XFG → HEAT needed to reach P_clear
    ```
  - Construct swap tx (standard HEARTH swap, protocol-signed)
  - Pay 0.3% LP fee from treasury balance
  - Execute at block finalization (after matching, before header seal)
- [ ] Treasury balance tracking: `treasury_balance[XFG]` and `treasury_balance[HEAT]`
- [ ] Funding source: mint premium accumulator

### 3.3 Market Order Cascade

- [ ] `MarketOrderExecutor` class in `src/CryptoNoteCore/MarketOrderExecutor.h/.cpp`
  - `executeMarketBuy(amount_xfg, max_heat_cost, P_clear)`:
    1. Consume HEARTH sell band @ P_clear
    2. If unfilled > 0: walk ask_curve ascending
    3. Fill limit asks until: filled OR 5 levels consumed OR 150% deviation OR book empty
    4. Return `{filled_amount, total_cost_heat, levels_consumed}`
  - `executeMarketSell()`: symmetric, walking bid_curve descending
- [ ] Per-order guard enforcement:
  - Track levels consumed (increment when price level changes)
  - Track max price deviation (`price / P_clear > MAX_MARKET_PRICE_DEVIATION_PCT`)

### 3.4 Treasury / Mint Premium Integration

- [ ] Mint premium (2-3%) diverted to treasury rebalance fund
- [ ] Modify `HeatMintEngine::validateMint()`:
  - Calculate premium: `xfg_amount * MINT_PREMIUM_BPS / 10000`
  - Route premium XFG to treasury balance
  - Apply per-block mint cap check
- [ ] Remove PI controller references from mint logic
- [ ] Mint cap: `treasury.mint_this_block + request <= MAX_MINT_PER_BLOCK`

### 3.5 Tests

- [ ] Market buy within HEARTH band → instant fill, no cascade
- [ ] Market buy exceeds band → cascade into orderbook at next ask level
- [ ] Cascade hits 5-level guard → stops, remainder returned
- [ ] Cascade hits 150% deviation → stops, remainder returned
- [ ] Cascade against empty orderbook → HEARTH only, remainder returned
- [ ] Rebalance: pool drifted down → treasury swaps HEAT→XFG, ratio = P_clear
- [ ] Rebalance: pool drifted up → treasury swaps XFG→HEAT, ratio = P_clear
- [ ] Rebalance: LP fee correctly paid by treasury
- [ ] Mint premium routed to treasury (not burned)
- [ ] Per-block mint cap enforced
- [ ] Bootstrap blocks: no matching, P_clear = pool ratio
- [ ] Post-bootstrap: full hybrid operation

---

## Phase 4: Wallet Commands + RPC Endpoints

**Goal**: Users can place orders, cancel, trade, view book from CLI wallet and RPC.

### 4.1 Wallet Commands

- [ ] `trade <buy|sell> <amount>` in `src/SimpleWallet/SimpleWallet.cpp`
  - Fetch P_clear + orderbook state via RPC
  - Show pre-flight estimate (HEARTH band + orderbook cascade)
  - Confirm → submit market order
- [ ] `place_order <buy|sell> <amount> <price> [expiration_blocks]`
  - Build `ORDER_PLACE` transaction
  - Lock funds in TX_OUT_ORDER
  - Display order ID + expiration
- [ ] `cancel_order <order_id>`
  - Build `ORDER_CANCEL` referencing order UTXO
- [ ] `show_orders`
  - Scan UTXO set for own TX_OUT_ORDER outputs
  - Display ID, side, amount, filled, price, expiration
- [ ] `orderbook [depth]`
  - RPC call to `get_orderbook`
  - Format as bid/ask table

### 4.2 RPC Server Endpoints

- [ ] `GET /get_orderbook?depth=N`
  - Returns `{ bids: [{price, depth}], asks: [{price, depth}], P_clear }`
- [ ] `GET /get_orderbook_clearing_price`
  - Returns `{ P_clear, block_height }`
- [ ] `GET /get_orderbook_order?id=<order_id>`
  - Returns `{ id, side, amount, filled, price, expiration, status }`
- [ ] `POST /place_order` — body: `{ side, amount, price, expiration_blocks }`
  - Returns `{ order_id, tx_hash }`
- [ ] `POST /cancel_order` — body: `{ order_id }`
  - Returns `{ tx_hash }`
- [ ] `POST /market_buy` — body: `{ xfg_amount, max_heat_cost }`
  - Returns `{ filled_amount, total_cost, tx_hash }`
- [ ] `POST /market_sell` — body: `{ xfg_amount, min_heat_receive }`
  - Returns `{ filled_amount, total_receive, tx_hash }`
- [ ] `GET /get_orderbook_estimates` — body: `{ side, amount }`
  - Returns pre-flight estimate without executing

### 4.3 Wallet Storage

- [ ] Track own order IDs in wallet cache (for `show_orders` without full UTXO scan)
- [ ] Detect remainder UTXOs during standard wallet sync (same stealth address scanning)

### 4.4 Tests

- [ ] CLI: `trade buy 50` shows pre-flight, user confirms, tx submitted
- [ ] CLI: `place_order sell 100 0.1275` creates order, shows ID
- [ ] CLI: `cancel_order` reclaims funds
- [ ] CLI: `show_orders` lists open orders with correct filled amounts
- [ ] CLI: `orderbook 10` renders correct price/depth table
- [ ] RPC: all endpoints return correct JSON schemas
- [ ] RPC: invalid params return error codes

---

## Phase 5: Anti-Manipulation Guards + Adversarial Testing

**Goal**: All guards enforced, adversarial scenarios tested, edge cases covered.

### 5.1 Guard Enforcement

- [ ] Per-order price deviation guard (150% or 5 levels → hard stop)
- [ ] Min 2 distinct parties per clearing (check ring signature key images)
- [ ] Max orders per block per sender
- [ ] Min order amount (dust threshold)
- [ ] Cancel fee deduction
- [ ] Per-block mint cap
- [ ] Order expiration enforcement at block finalization

### 5.2 Adversarial Test Scenarios

- [ ] **Wash trade attempt**: 2 wallets from same seed, bid + ask at same price → clearing rejected (same key image set?)
- [ ] **Sybil flood**: 100 orders from 100 wallets, same sender flag → capped by per-sender limit
- [ ] **Market order exhaustion**: market buy for 1,000,000 XFG against thin book → stops at 5 levels
- [ ] **Price pump**: market buy at 200% deviation → rejected by guard
- [ ] **Mint-pump-mint**: mint → market buy → price moves → mint again → cap + premium prevent infinite loop
- [ ] **Stale order attack**: place order at absurd price, let it sit → expiration removes it
- [ ] **Rebalance drain**: prolonged one-directional market → verify treasury doesn't go insolvent (simulate)
- [ ] **Bootstrap race**: user places limit order during bootstrap → correctly carried forward to block 144
- [ ] **Remainder sweep**: multiple partial fills on same order → each remainder correctly tracked
- [ ] **Empty book cascade**: market buy on empty book + empty HEARTH → graceful 0 fill

### 5.3 Edge Cases

- [ ] Order amount = exactly dust threshold → allowed or rejected?
- [ ] Order expiration = current block → excluded from matching this block
- [ ] Cancel during same block order is partially filled → remainder of remainder cancelled
- [ ] Clearing price at exactly 0 (empty book, first block after bootstrap) → handled
- [ ] P_clear moves across integer boundaries → no overflow in price calculations
- [ ] Multiple market orders in same block → processed sequentially, HEARTH band consumption serialized

### 5.4 Performance Benchmarks

- [ ] 1,000 orders matched in block: time < 30s
- [ ] 10,000 orders in book, 1,000 matched: time < 60s
- [ ] Wallet sync with 500 remainder outputs: < 5s additional
- [ ] Rebalance computation: < 1s

---

## File Manifest

### New Files

| File | Phase | Purpose |
|------|-------|---------|
| `src/CryptoNoteCore/OrderbookIndex.h/.cpp` | 1 | In-memory orderbook index |
| `src/CryptoNoteCore/OrderbookMatcher.h/.cpp` | 1 | Matching engine |
| `src/CryptoNoteCore/OrderbookSettlement.h/.cpp` | 2 | Settlement tx construction |
| `src/CryptoNoteCore/HearthDepthBand.h/.cpp` | 3 | Depth band calculations |
| `src/CryptoNoteCore/HearthRebalance.h/.cpp` | 3 | Rebalance tx construction |
| `src/CryptoNoteCore/MarketOrderExecutor.h/.cpp` | 3 | Market order cascade logic |
| `tests/CoreTests/OrderbookTests.cpp` | 1-5 | Test suite |

### Modified Files

| File | Phase | Change |
|------|-------|--------|
| `src/CryptoNoteCore/CryptoNoteBasic.h` | 1 | TX_OUT_ORDER type |
| `src/CryptoNoteCore/CryptoNoteFormatUtils.h/.cpp` | 1 | Serialization |
| `src/CryptoNoteCore/TransactionExtra.h/.cpp` | 1 | Order tx subtypes |
| `src/CryptoNoteCore/Block.h` | 2 | Header extension |
| `src/CryptoNoteCore/CryptoNoteSerialization.h/.cpp` | 2 | Block serialization |
| `src/CryptoNoteCore/Blockchain.h/.cpp` | 2 | Finalization hook |
| `src/CryptoNoteCore/Core.h/.cpp` | 2 | Block processing |
| `src/CryptoNoteCore/HeatMintEngine.h/.cpp` | 3 | Mint premium, remove PI |
| `src/CryptoNoteConfig.h` | 2-3 | All new constants |
| `src/Rpc/RpcServer.cpp` | 4 | Orderbook endpoints |
| `src/Rpc/CoreRpcServerCommandsDefinitions.h` | 4 | RPC structs |
| `src/SimpleWallet/SimpleWallet.cpp` | 4 | Wallet commands |
| `src/Wallet/WalletGreen.h/.cpp` | 4 | Wallet order tracking |
| `CMakeLists.txt` | 1 | Build new files |

---

## Constants (add to `src/CryptoNoteConfig.h`)

```cpp
// Orderbook
constexpr uint64_t UPGRADE_HEIGHT_ORDERBOOK   = TBD;
constexpr uint32_t BOOTSTRAP_BLOCKS            = 144;
constexpr uint32_t MAX_ORDERS_PER_BLOCK        = 1000;
constexpr uint32_t MAX_MARKET_PRICE_DEVIATION_PCT = 150;
constexpr uint32_t MAX_MARKET_ORDER_LEVELS     = 5;
constexpr uint32_t MIN_DISTINCT_PARTIES        = 2;
constexpr uint32_t DEFAULT_ORDER_EXPIRATION_BLOCKS = 12600;  // ~1 week
constexpr uint32_t MIN_ORDER_AMOUNT            = DUST_THRESHOLD;  // reuse existing

// HEARTH Depth Band
constexpr uint32_t HEARTH_DEPTH_BAND_PCT       = 10;

// Mint (replacing PI)
constexpr uint32_t MINT_PREMIUM_BPS            = 200;  // 2%, configurable to 300
constexpr uint64_t MAX_MINT_HEAT_PER_BLOCK     = TBD;  // atomic units

// Fees
constexpr uint64_t CANCEL_FEE_ATOMIC         = 1000;// 0.0001   
constexpr uint32_t ORDERBOOK_TAKER_FEE_BPS     = 10;   // 0.1%, configurable to 30
```

---

## Estimated Effort



---

## Checkpoints After Each Phase

| Phase | Verify |
|-------|--------|
| 1 | `OrderbookMatcher` unit tests pass (15+ cases). Serialization roundtrip clean. |
| 2 | Block processed with orders → P_clear in header. Settlement UTXOs spendable. |
| 3 | Market order cascades correctly. Rebalance brings pool to P_clear. Mint premium routes to treasury. |
| 4 | Wallet `trade` command works end-to-end. All RPC endpoints return valid responses. |
| 5 | All adversarial scenarios pass. No crash on empty book. Guards enforced. |

---

## Rollback Plan

If orderbook logic causes a consensus fork:
1. Disable via `UPGRADE_HEIGHT_ORDERBOOK = UINT64_MAX` (never activates)
2. HEARTH continues operating as standard AMM
3. Rebuild without orderbook code (feature flag approach preferred — `#ifdef ORDERBOOK_ENABLED`)
