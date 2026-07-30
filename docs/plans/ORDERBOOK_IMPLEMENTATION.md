# SWAPXFG — P2P Order Book Implementation Guide

## Architecture Overview

**Current:** P2P RFQ (Request-for-Quote) — makers post asks, takers request specific offers. No matching engine, no bid side, no price-time priority.

**Target:** P2P Central Limit Order Book (CLOB) — each node gossips orders and maintains a local matching engine. When orders cross, the existing adaptor-signature atomic swap protocol executes automatically. The gas/blockchain settlement layer is unchanged.

| Layer | Current | Target |
|---|---|---|
| Order types | Ask only (`SwapOfferMsg`) | Bid + Ask (`Order`) |
| Matching | None (taker picks offerId) | Price-time priority auto-matching |
| P2P gossip | `COMMAND_SWAP_OFFER` (2 types) | `COMMAND_ORDER_OPEN/CANCEL/FILL` (3 types) |
| RPC | `/getswapoffers`, `/submitswap` etc. | `/orderbook`, `/placeorder`, `/cancelorder`, `/openorders` |
| TUI display | Naive median-split renderer | Real bid/ask depth ladder |

**Key design invariant: When the matching engine pairs a bid with an ask, ownership does not transfer in the order book. The match becomes a SwapStateMachine, and settlement proceeds via the existing 8-step adaptor signature protocol. The order book is an *order discovery* system, not a *custody* system.**

---

## ⚠️ Recommendations

### 1. Start With a Read-Only Probe (Ship an Order Book Display First)

Before building the matching engine, add just the `/getorderbook` RPC endpoint and the `Order` struct on both sides. Ship an order book *display* in the TUI first — it uses the existing `SwapOfferRelay` data (no matching). This gives you a working depth ladder immediately and proves the P2P gossip + display pipeline without touching swap execution.

**How:** In Phase 1, the `/getorderbook` endpoint reads existing `SwapOfferRelay::m_offers`, splits them by a naive median as the current `orderbook.go` does, and returns bid/ask arrays. This requires zero changes to matching, zero changes to swap execution. Once the display works end-to-end, Phase 2 adds real matching.

### 2. Phantom Fill Problem: Use Fill-Reservation P2P Messages

In a P2P gossip network, two takers can see the same maker order and both try to fill it. The maker's daemon will auto-lock for the first one and reject the second — the second taker gets stuck.

**Fix:** Add two P2P message types for a reservation handshake:

| Message | Purpose |
|---|---|
| `COMMAND_ORDER_RESERVE` | Taker broadcasts intent to fill makerOrderId for amount |
| `COMMAND_ORDER_RESERVE_ACK` | Maker confirms reservation (30s TTL), includes Musig2 nonce |

Flow:
1. Taker's matching engine detects a cross
2. Taker broadcasts `COMMAND_ORDER_RESERVE(makerOrderId, amount, takerPubKey)` via Dandelion stem to the maker
3. Maker's daemon receives it, reserves the amount (marks `filled` temporarily), broadcasts `COMMAND_ORDER_RESERVE_ACK(reservationId, nonce0)`
4. Taker sees the ACK, proceeds with the adaptor signature protocol
5. If the ACK does not arrive within 30s, the taker broadcasts `COMMAND_ORDER_CANCEL` for its own order and the maker's temporary reserve expires

This adds one round-trip of latency but prevents the double-spend problem inherent in P2P order books.

### 3. Keep SwapOfferRelay Backwards-Compatible

Don't replace `submitOffer`. Add `submitOrder` alongside it. Old nodes that don't understand order book messages still see offers via the existing `COMMAND_SWAP_OFFER` channel. This lets you deploy incrementally without a hard fork of the P2P protocol.

**How:** In `CryptoNoteProtocolHandler`, register new handlers but do NOT remove the old `COMMAND_SWAP_OFFER` handler. The `SwapOfferRelay` maintains both `m_offers` (legacy) and `m_orderBooks[6]` (new). The `/getorderbook` RPC can optionally merge both sources. The OfferManager continues to work via the legacy path until you're ready to switch it to place orders.

### 4. OfferManager Becomes the First Order Book Market Maker

Its `ManagedOffer` config already has `pair`, `xfgAmount`, `slippagePct`. Add a `side` field and it can place both bids and asks at the composite price. This gives you automated liquidity from day one.

**New config format:**
```json
{
  "orders": [
    { "pair": 0, "side": "ASK", "xfgAmount": 5000000000, "slippagePct": 2.0 },
    { "pair": 0, "side": "BID", "xfgAmount": 3000000000, "slippagePct": 1.5 }
  ],
  "ttlBlocks": 8640
}
```

### 5. Delay the Auto-Execution Hook (Phase 4)

Ship Phases 1-3 first (order book display + manual order placement). Let users place orders and see them in the book. Then wire the matching engine to auto-execute swaps. This decouples the UI/display work from the settlement changes and makes debugging easier.

Phase 4 (auto-execution) should gate on:
- Order book display stable for 1+ week of usage
- Reservation handshake tested in isolation (two nodes, manual cross)
- At least 3 manual swaps completed through the order book flow before automating

---

## Phase 1: C++ Data Structures and P2P Wire Format

### 1.1 New Order Struct

**File:** `src/CryptoNoteCore/SwapOfferRelay.h`

Add above `SwapOfferMsg`:

```cpp
struct Order {
    enum class Side : uint8_t { BID = 0, ASK = 1 };

    std::string        orderId;      // SHA-256 of (makerPubkey || side || pair || price || amount || nonce)
    Side               side;         // BID (buying XFG) or ASK (selling XFG)
    uint8_t            pair;         // 0-5 per existing pair constants
    uint64_t           price;        // XFG per 1 CTR, scaled by 1e7
    uint64_t           amount;       // XFG amount (atomic units)
    uint64_t           filled;       // already matched + in-flight
    Crypto::PublicKey  makerPubKey;
    Crypto::Signature  signature;    // signs orderId
    uint64_t           nonce;        // monotonic counter from maker
    uint64_t           timestamp;
    uint32_t           ttlBlocks;
    uint32_t           postedHeight;
};

// Price level: all orders at same price, FIFO deque
struct PriceLevel {
    std::deque<Order> orders;
    uint64_t          totalAmount;  // sum of (amount - filled) across all orders

    // Returns the best order (front of FIFO queue)
    const Order* best() const {
        return orders.empty() ? nullptr : &orders.front();
    }
};
```

### 1.2 New P2P Message Types

**File:** `src/P2p/P2pProtocolDefinitions.h` (append after `COMMAND_CD_CANCEL`)

```cpp
const static int P2P_COMMANDS_POOL_BASE_ORDERBOOK = P2P_COMMANDS_POOL_BASE + 20;

// ── Core order book messages ──

struct COMMAND_ORDER_OPEN {
    struct request {
        char     orderId[64];
        uint8_t  side;          // 0=BID, 1=ASK
        uint8_t  pair;
        uint64_t price;
        uint64_t amount;
        char     makerPubKey[32];
        char     signature[64];
        uint64_t nonce;
        uint64_t timestamp;
        uint32_t ttlBlocks;
        uint32_t postedHeight;
        uint8_t  dandelion_stem;
        uint8_t  hop_count;

        void serialize(ISerializer& s) {
            s(orderId, "orderId");
            s(side, "side");
            s(pair, "pair");
            s(price, "price");
            s(amount, "amount");
            s(makerPubKey, "makerPubKey");
            s(signature, "signature");
            s(nonce, "nonce");
            s(timestamp, "timestamp");
            s(ttlBlocks, "ttlBlocks");
            s(postedHeight, "postedHeight");
            s(dandelion_stem, "dandelion_stem");
            s(hop_count, "hop_count");
        }
    };
};

struct COMMAND_ORDER_CANCEL {
    struct request {
        char     orderId[64];
        uint8_t  side;
        uint8_t  pair;
        char     makerPubKey[32];
        char     signature[64];
        uint64_t timestamp;

        void serialize(ISerializer& s) {
            s(orderId, "orderId");
            s(side, "side");
            s(pair, "pair");
            s(makerPubKey, "makerPubKey");
            s(signature, "signature");
            s(timestamp, "timestamp");
        }
    };
};

struct COMMAND_ORDER_FILL {
    struct request {
        char     takerOrderId[64];   // the taker's order that consumed liquidity
        char     makerOrderId[64];   // the maker's order that was filled against
        uint64_t fillAmount;         // XFG amount matched
        uint64_t fillPrice;          // execution price
        uint64_t timestamp;
        uint64_t blockHeight;

        void serialize(ISerializer& s) {
            s(takerOrderId, "takerOrderId");
            s(makerOrderId, "makerOrderId");
            s(fillAmount, "fillAmount");
            s(fillPrice, "fillPrice");
            s(timestamp, "timestamp");
            s(blockHeight, "blockHeight");
        }
    };
};

// ── Fill-reservation protocol (prevents phantom fills) ──

struct COMMAND_ORDER_RESERVE {
    struct request {
        char     reservationId[64];  // SHA-256(takerOrderId || makerOrderId || amount || nonce)
        char     takerOrderId[64];
        char     makerOrderId[64];
        uint64_t amount;             // how much the taker wants to fill
        char     takerPubKey[32];
        uint64_t timestamp;
        uint32_t ttlSeconds;         // how long the reserve is valid (default 30)

        void serialize(ISerializer& s) {
            s(reservationId, "reservationId");
            s(takerOrderId, "takerOrderId");
            s(makerOrderId, "makerOrderId");
            s(amount, "amount");
            s(takerPubKey, "takerPubKey");
            s(timestamp, "timestamp");
            s(ttlSeconds, "ttlSeconds");
        }
    };
};

struct COMMAND_ORDER_RESERVE_ACK {
    struct request {
        char reservationId[64];
        char makerOrderId[64];
        char makerPubKey[32];
        char nonce0[32];             // Musig2 public nonce for this swap
        char signature[64];          // maker signs the reservationId
        uint64_t timestamp;

        void serialize(ISerializer& s) {
            s(reservationId, "reservationId");
            s(makerOrderId, "makerOrderId");
            s(makerPubKey, "makerPubKey");
            s(nonce0, "nonce0");
            s(signature, "signature");
            s(timestamp, "timestamp");
        }
    };
};
```

### 1.3 P2P Command Registration

**File:** `src/CryptoNoteProtocol/CryptoNoteProtocolHandler.cpp`

Add to the `HANDLE_NOTIFY` block (after CD handlers):

```cpp
// Order book messages (backwards-compatible: old COMMAND_SWAP_OFFER handlers remain)
HANDLE_NOTIFY(CryptoNote::COMMAND_ORDER_OPEN,   &m_swap_handler, &CryptoNoteProtocolHandler::handle_order_open);
HANDLE_NOTIFY(CryptoNote::COMMAND_ORDER_CANCEL, &m_swap_handler, &CryptoNoteProtocolHandler::handle_order_cancel);
HANDLE_NOTIFY(CryptoNote::COMMAND_ORDER_FILL,   &m_swap_handler, &CryptoNoteProtocolHandler::handle_order_fill);
HANDLE_NOTIFY(CryptoNote::COMMAND_ORDER_RESERVE,    &m_swap_handler, &CryptoNoteProtocolHandler::handle_order_reserve);
HANDLE_NOTIFY(CryptoNote::COMMAND_ORDER_RESERVE_ACK, &m_swap_handler, &CryptoNoteProtocolHandler::handle_order_reserve_ack);
```

### 1.4 Message Handlers in ProtocolHandler

**File:** Same file — add handler methods that route to `SwapOfferRelay`:

```cpp
bool CryptoNoteProtocolHandler::handle_order_open(const COMMAND_ORDER_OPEN::request& arg) {
    Order order;
    order.orderId.assign(arg.orderId, sizeof(arg.orderId));
    order.side = static_cast<Order::Side>(arg.side);
    order.pair = arg.pair;
    order.price = arg.price;
    order.amount = arg.amount;
    order.nonce = arg.nonce;
    order.timestamp = arg.timestamp;
    order.ttlBlocks = arg.ttlBlocks;
    order.postedHeight = arg.postedHeight;
    std::memcpy(order.makerPubKey.data, arg.makerPubKey, 32);
    std::memcpy(order.signature.data, arg.signature, 64);

    m_core.getSwapRelay().handleOrderOpen(order, arg.dandelion_stem);
    return true;
}

bool CryptoNoteProtocolHandler::handle_order_cancel(const COMMAND_ORDER_CANCEL::request& arg) {
    std::string orderId(arg.orderId, strnlen(arg.orderId, 64));
    m_core.getSwapRelay().handleOrderCancel(orderId);
    return true;
}

bool CryptoNoteProtocolHandler::handle_order_fill(const COMMAND_ORDER_FILL::request& arg) {
    std::string takerId(arg.takerOrderId, strnlen(arg.takerOrderId, 64));
    std::string makerId(arg.makerOrderId, strnlen(arg.makerOrderId, 64));
    m_core.getSwapRelay().handleOrderFill(takerId, makerId, arg.fillAmount, arg.fillPrice);
    return true;
}

bool CryptoNoteProtocolHandler::handle_order_reserve(const COMMAND_ORDER_RESERVE::request& arg) {
    std::string reservationId(arg.reservationId, strnlen(arg.reservationId, 64));
    std::string makerOrderId(arg.makerOrderId, strnlen(arg.makerOrderId, 64));
    m_core.getSwapRelay().handleOrderReserve(reservationId, makerOrderId, arg.amount);
    return true;
}

bool CryptoNoteProtocolHandler::handle_order_reserve_ack(const COMMAND_ORDER_RESERVE_ACK::request& arg) {
    std::string reservationId(arg.reservationId, strnlen(arg.reservationId, 64));
    m_core.getSwapRelay().handleOrderReserveAck(reservationId, arg.nonce0);
    return true;
}
```

### 1.5 Read-Only Probe: /getorderbook Without Matching

In Phase 1, `/getorderbook` returns data from the existing `SwapOfferRelay::m_offers` map — no matching engine yet. This proves the entire pipeline (P2P gossip → storage → RPC → Go TUI display) without touching swap execution.

```cpp
// Temporary Phase 1 implementation — replaces m_offers data with bid/ask split
OrderBookSnapshot SwapOfferRelay::getOrderBookSnapshot(uint8_t pair, int depth) {
    OrderBookSnapshot snap;
    std::lock_guard<std::mutex> lock(m_offersMutex);

    // Collect offers for this pair
    std::vector<SwapOfferMsg> pairOffers;
    for (const auto& [id, offer] : m_offers) {
        if (offer.pair == pair && !offer.isSoftOrder) {
            pairOffers.push_back(offer);
        }
    }

    // Sort by rate descending
    std::sort(pairOffers.begin(), pairOffers.end(),
        [](const auto& a, const auto& b) { return a.rateNum > b.rateNum; });

    // Split: top half = asks, bottom half = bids (naive median)
    // Updated: use median of TWAP as divider instead of equal split
    uint64_t twap = getCompositePrice(pair).rateNum; // reuses existing TWAP
    int askCount = 0, bidCount = 0;

    for (const auto& o : pairOffers) {
        if (askCount < depth && o.rateNum >= twap) {
            snap.asks.push_back({o.rateNum, o.xfgAmount, 1});
            askCount++;
        }
        if (bidCount < depth && o.rateNum < twap) {
            snap.bids.push_back({o.rateNum, o.xfgAmount, 1});
            bidCount++;
        }
    }

    if (!snap.bids.empty() && !snap.asks.empty()) {
        snap.spread = snap.asks[0].price - snap.bids[0].price;
    }
    return snap;
}
```

---

## Phase 2: Matching Engine in SwapOfferRelay

### 2.1 Add Order Book State

**File:** `src/CryptoNoteCore/SwapOfferRelay.h`

Add to the `SwapOfferRelay` class members:

```cpp
// Order book: sorted price ladders per pair
// Bids: descending by price (highest first). Stored as (1e8 - price) for descending iteration.
// Asks: ascending by price (lowest first).
using BidLadder = std::map<uint64_t, PriceLevel, std::greater<uint64_t>>;
using AskLadder = std::map<uint64_t, PriceLevel>;
using OrderMap  = std::unordered_map<std::string, Order>;

// Reservation tracking (prevents phantom fills)
struct Reservation {
    std::string reservationId;
    std::string makerOrderId;
    uint64_t    reservedAmount;
    uint64_t    expiresAt;   // unix timestamp
    bool        acked;
};
using ReservationMap = std::unordered_map<std::string, Reservation>;

// Per-pair order books (index: pair 0-5)
struct PairOrderBook {
    BidLadder      bids;
    AskLadder      asks;
    OrderMap       allOrders;   // orderId -> Order, for O(1) cancel
    ReservationMap reservations; // reservationId -> Reservation
    std::mutex     mtx;
};

PairOrderBook m_orderBooks[6];
```

### 2.2 Reservation Protocol

```cpp
void SwapOfferRelay::handleOrderReserve(const std::string& reservationId,
                                         const std::string& makerOrderId,
                                         uint64_t amount) {
    // Find the maker order
    for (auto& book : m_orderBooks) {
        std::lock_guard<std::mutex> lock(book.mtx);
        auto it = book.allOrders.find(makerOrderId);
        if (it == book.allOrders.end()) continue;

        Order& maker = it->second;
        uint64_t available = maker.amount - maker.filled;

        if (available < amount) return; // insufficient

        // Record reservation (temporary, 30s TTL)
        Reservation res;
        res.reservationId = reservationId;
        res.makerOrderId = makerOrderId;
        res.reservedAmount = amount;
        res.expiresAt = time(nullptr) + 30;
        res.acked = false;
        book.reservations[reservationId] = res;

        // Temporarily mark as filled (prevent other takers from consuming)
        maker.filled += amount;
        book.allOrders[makerOrderId].filled = maker.filled;

        // Auto-cancel reservation after 30s if not acked
        m_scheduler.schedule(res.expiresAt, [this, &book, reservationId]() {
            std::lock_guard<std::mutex> lock(book.mtx);
            auto it = book.reservations.find(reservationId);
            if (it != book.reservations.end() && !it->second.acked) {
                // Revert the temporary fill
                auto orderIt = book.allOrders.find(it->second.makerOrderId);
                if (orderIt != book.allOrders.end()) {
                    orderIt->second.filled -= it->second.reservedAmount;
                }
                book.reservations.erase(it);
            }
        });

        // Broadcast ACK to taker
        broadcastReserveAck(reservationId, makerOrderId);
        return;
    }
}
```

### 2.3 Implement handleOrderOpen (with matching)

**File:** `src/CryptoNoteCore/SwapOfferRelay.cpp`

```cpp
void SwapOfferRelay::handleOrderOpen(const Order& order, bool isStem) {
    auto& book = m_orderBooks[order.pair];
    {
        std::lock_guard<std::mutex> lock(book.mtx);

        // Validate orderId and signature
        if (!validateOrder(order)) return;

        // Deduplicate by orderId
        if (book.allOrders.find(order.orderId) != book.allOrders.end()) return;

        // Insert into allOrders map
        Order stored = order;
        stored.filled = 0;
        book.allOrders[order.orderId] = stored;

        // --- Matching Engine ---
        uint64_t remaining = stored.amount;
        std::vector<Order> filledMakers;

        if (order.side == Order::Side::BID) {
            // Bid matches against asks (lowest ask first)
            auto it = book.asks.begin();
            while (it != book.asks.end() && remaining > 0) {
                uint64_t askPrice = it->first;
                if (askPrice > order.price) break; // cross the spread

                PriceLevel& level = it->second;
                while (!level.orders.empty() && remaining > 0) {
                    Order& maker = level.orders.front();
                    uint64_t makerAvail = maker.amount - maker.filled;
                    if (makerAvail == 0) {
                        level.orders.pop_front();
                        continue;
                    }

                    uint64_t fillQty = std::min(remaining, makerAvail);
                    maker.filled += fillQty;
                    book.allOrders[maker.orderId].filled = maker.filled;
                    remaining -= fillQty;
                    level.totalAmount -= fillQty;

                    filledMakers.push_back(maker);

                    if (maker.filled >= maker.amount) {
                        level.orders.pop_front();
                        book.allOrders[maker.orderId].filled = maker.amount;
                    }
                }

                if (level.orders.empty()) {
                    it = book.asks.erase(it);
                } else {
                    break;
                }
            }
        } else { // ASK
            // Ask matches against bids (highest bid first)
            auto it = book.bids.begin();
            while (it != book.bids.end() && remaining > 0) {
                uint64_t truePrice = 100000000UL - it->first;
                if (truePrice < order.price) break;

                PriceLevel& level = it->second;
                while (!level.orders.empty() && remaining > 0) {
                    Order& maker = level.orders.front();
                    uint64_t makerAvail = maker.amount - maker.filled;
                    if (makerAvail == 0) {
                        level.orders.pop_front();
                        continue;
                    }

                    uint64_t fillQty = std::min(remaining, makerAvail);
                    maker.filled += fillQty;
                    book.allOrders[maker.orderId].filled = maker.filled;
                    remaining -= fillQty;
                    level.totalAmount -= fillQty;

                    filledMakers.push_back(maker);

                    if (maker.filled >= maker.amount) {
                        level.orders.pop_front();
                        book.allOrders[maker.orderId].filled = maker.amount;
                    }
                }

                if (level.orders.empty()) {
                    it = book.bids.erase(it);
                } else {
                    break;
                }
            }
        }

        // If taker has remaining amount, insert into book
        if (remaining > 0) {
            stored.filled = stored.amount - remaining;
            if (stored.side == Order::Side::BID) {
                uint64_t invPrice = 100000000UL - stored.price;
                book.bids[invPrice].orders.push_back(stored);
                book.bids[invPrice].totalAmount += remaining;
            } else {
                book.asks[stored.price].orders.push_back(stored);
                book.asks[stored.price].totalAmount += remaining;
            }
        }

        // Record trades and broadcast fills
        for (const auto& maker : filledMakers) {
            uint64_t execPrice = stored.side == Order::Side::BID ? maker.price : stored.price;
            uint64_t fillQty = maker.filled;

            recordTrade(stored.pair, fillQty, execPrice);
            broadcastOrderFill(stored.orderId, maker.orderId, fillQty, execPrice);
        }
    }

    // Relay to P2P network
    if (!isStem) {
        broadcastOrderOpen(order, true /* fluff */);
    } else {
        relayOrderStem(order);
    }
}
```

### 2.4 Cancel

```cpp
void SwapOfferRelay::handleOrderCancel(const std::string& orderId) {
    for (auto& book : m_orderBooks) {
        std::lock_guard<std::mutex> lock(book.mtx);
        auto it = book.allOrders.find(orderId);
        if (it == book.allOrders.end()) continue;

        Order& order = it->second;
        uint64_t unfilled = order.amount - order.filled;

        if (order.side == Order::Side::BID) {
            uint64_t invPrice = 100000000UL - order.price;
            auto levelIt = book.bids.find(invPrice);
            if (levelIt != book.bids.end()) {
                levelIt->second.totalAmount -= unfilled;
                auto& deq = levelIt->second.orders;
                deq.erase(std::remove_if(deq.begin(), deq.end(),
                    [&](const Order& o) { return o.orderId == orderId; }), deq.end());
                if (deq.empty()) book.bids.erase(levelIt);
            }
        } else {
            auto levelIt = book.asks.find(order.price);
            if (levelIt != book.asks.end()) {
                levelIt->second.totalAmount -= unfilled;
                auto& deq = levelIt->second.orders;
                deq.erase(std::remove_if(deq.begin(), deq.end(),
                    [&](const Order& o) { return o.orderId == orderId; }), deq.end());
                if (deq.empty()) book.asks.erase(levelIt);
            }
        }

        book.allOrders.erase(it);
        break;
    }
}
```

### 2.5 Query Methods

```cpp
struct OrderBookLevel {
    uint64_t price;
    uint64_t amount;
    int      orderCount;
};

struct OrderBookSnapshot {
    std::vector<OrderBookLevel> bids;
    std::vector<OrderBookLevel> asks;
    uint64_t spread;
};

OrderBookSnapshot SwapOfferRelay::getOrderBookSnapshot(uint8_t pair, int depth) {
    OrderBookSnapshot snap;
    auto& book = m_orderBooks[pair];
    std::lock_guard<std::mutex> lock(book.mtx);

    int count = 0;
    for (const auto& [price, level] : book.bids) {
        if (count++ >= depth) break;
        snap.bids.push_back({100000000UL - price, level.totalAmount, (int)level.orders.size()});
    }

    count = 0;
    for (const auto& [price, level] : book.asks) {
        if (count++ >= depth) break;
        snap.asks.push_back({price, level.totalAmount, (int)level.orders.size()});
    }

    if (!snap.bids.empty() && !snap.asks.empty()) {
        snap.spread = snap.asks[0].price - snap.bids[0].price;
    }
    return snap;
}

std::vector<Order> SwapOfferRelay::getOpenOrders(const Crypto::PublicKey& pubkey) {
    std::vector<Order> result;
    for (auto& book : m_orderBooks) {
        std::lock_guard<std::mutex> lock(book.mtx);
        for (const auto& [id, order] : book.allOrders) {
            if (order.makerPubKey == pubkey && order.filled < order.amount) {
                result.push_back(order);
            }
        }
    }
    return result;
}
```

### 2.6 Extended Cleanup Thread

Add to the existing 30-second cleanup loop:

```cpp
// Prune expired orders and reservations
for (auto& book : m_orderBooks) {
    std::lock_guard<std::mutex> lock(book.mtx);
    uint64_t now = time(nullptr);

    // Prune expired reservations
    for (auto it = book.reservations.begin(); it != book.reservations.end(); ) {
        if (it->second.expiresAt < now) {
            // Revert temporary fill
            auto orderIt = book.allOrders.find(it->second.makerOrderId);
            if (orderIt != book.allOrders.end()) {
                orderIt->second.filled -= it->second.reservedAmount;
            }
            it = book.reservations.erase(it);
        } else {
            ++it;
        }
    }

    // Prune expired orders
    auto prune = [&](auto& ladder) {
        for (auto it = ladder.begin(); it != ladder.end(); ) {
            auto& deq = it->second.orders;
            deq.erase(std::remove_if(deq.begin(), deq.end(),
                [&](const Order& o) { return o.timestamp + o.ttlBlocks * 30 < now; }),
                deq.end());
            if (deq.empty()) {
                it = ladder.erase(it);
            } else {
                uint64_t total = 0;
                for (const auto& o : deq) total += (o.amount - o.filled);
                it->second.totalAmount = total;
                ++it;
            }
        }
    };
    prune(book.bids);
    prune(book.asks);

    for (auto it = book.allOrders.begin(); it != book.allOrders.end(); ) {
        if (it->second.timestamp + it->second.ttlBlocks * 30 < now) {
            it = book.allOrders.erase(it);
        } else {
            ++it;
        }
    }
}
```

---

## Phase 3: RPC Endpoints

### 3.1 New Command Definitions

**File:** `src/Rpc/CoreRpcServerCommandsDefinitions.h` (append)

```cpp
// ── Order Book ──
struct COMMAND_RPC_GET_ORDER_BOOK {
    struct request {
        uint8_t pair;
        int     depth;

        request() : pair(0), depth(20) {}

        void serialize(ISerializer& s) {
            KV_MEMBER(pair);
            KV_MEMBER(depth);
        }
    };

    struct response {
        std::vector<OrderBookLevelJson> bids;
        std::vector<OrderBookLevelJson> asks;
        uint64_t spread;
        uint64_t height;
        std::string status;

        struct OrderBookLevelJson {
            uint64_t price;
            uint64_t amount;
            int      orderCount;

            void serialize(ISerializer& s) {
                KV_MEMBER(price);
                KV_MEMBER(amount);
                KV_MEMBER(orderCount);
            }
        };

        void serialize(ISerializer& s) {
            KV_MEMBER(bids);
            KV_MEMBER(asks);
            KV_MEMBER(spread);
            KV_MEMBER(height);
            KV_MEMBER(status);
        }
    };
};

struct COMMAND_RPC_PLACE_ORDER {
    struct request {
        uint8_t  side;
        uint8_t  pair;
        uint64_t price;
        uint64_t amount;
        uint32_t ttlBlocks;

        void serialize(ISerializer& s) {
            KV_MEMBER(side);
            KV_MEMBER(pair);
            KV_MEMBER(price);
            KV_MEMBER(amount);
            KV_MEMBER(ttlBlocks);
        }
    };

    struct response {
        std::string orderId;
        std::string status;
        uint64_t    filled;
        std::string statusMsg;

        void serialize(ISerializer& s) {
            KV_MEMBER(orderId);
            KV_MEMBER(status);
            KV_MEMBER(filled);
            KV_MEMBER(statusMsg);
        }
    };
};

struct COMMAND_RPC_CANCEL_ORDER {
    struct request {
        std::string orderId;

        void serialize(ISerializer& s) { KV_MEMBER(orderId); }
    };

    struct response {
        std::string status;

        void serialize(ISerializer& s) { KV_MEMBER(status); }
    };
};

struct COMMAND_RPC_GET_OPEN_ORDERS {
    struct request {
        std::string address;

        void serialize(ISerializer& s) { KV_MEMBER(address); }
    };

    struct response {
        std::vector<OrderJson> orders;
        std::string status;

        struct OrderJson {
            std::string orderId;
            std::string side;
            uint8_t     pair;
            uint64_t    price;
            uint64_t    amount;
            uint64_t    filled;
            uint64_t    timestamp;
            uint32_t    ttlBlocks;

            void serialize(ISerializer& s) {
                KV_MEMBER(orderId);
                KV_MEMBER(side);
                KV_MEMBER(pair);
                KV_MEMBER(price);
                KV_MEMBER(amount);
                KV_MEMBER(filled);
                KV_MEMBER(timestamp);
                KV_MEMBER(ttlBlocks);
            }
        };

        void serialize(ISerializer& s) {
            KV_MEMBER(orders);
            KV_MEMBER(status);
        }
    };
};
```

### 3.2 Register Handlers

**File:** `src/Rpc/RpcServer.cpp` — add to `s_handlers`:

```cpp
{ "/getorderbook", { jsonMethod<COMMAND_RPC_GET_ORDER_BOOK>(&RpcServer::on_get_order_book), true } },
{ "/placeorder",   { jsonMethod<COMMAND_RPC_PLACE_ORDER>(&RpcServer::on_place_order), false } },
{ "/cancelorder",  { jsonMethod<COMMAND_RPC_CANCEL_ORDER>(&RpcServer::on_cancel_order), false } },
{ "/openorders",   { jsonMethod<COMMAND_RPC_GET_OPEN_ORDERS>(&RpcServer::on_get_open_orders), true } },
```

### 3.3 Handler Implementations

**File:** `src/Rpc/RpcServer.cpp`

```cpp
bool RpcServer::on_get_order_book(const COMMAND_RPC_GET_ORDER_BOOK::request& req,
                                   COMMAND_RPC_GET_ORDER_BOOK::response& resp) {
    auto snap = m_swapRelay->getOrderBookSnapshot(req.pair, req.depth > 0 ? req.depth : 20);
    for (const auto& b : snap.bids) {
        resp.bids.push_back({b.price, b.amount, b.orderCount});
    }
    for (const auto& a : snap.asks) {
        resp.asks.push_back({a.price, a.amount, a.orderCount});
    }
    resp.spread = snap.spread;
    resp.status = "OK";
    return true;
}

bool RpcServer::on_place_order(const COMMAND_RPC_PLACE_ORDER::request& req,
                                COMMAND_RPC_PLACE_ORDER::response& resp) {
    if (req.pair > 5) {
        resp.status = "ERROR";
        resp.statusMsg = "Invalid pair";
        return true;
    }
    if (req.side > 1) {
        resp.status = "ERROR";
        resp.statusMsg = "Invalid side (0=BID, 1=ASK)";
        return true;
    }

    Order order;
    order.side = static_cast<Order::Side>(req.side);
    order.pair = req.pair;
    order.price = req.price;
    order.amount = req.amount;
    order.ttlBlocks = req.ttlBlocks > 0 ? req.ttlBlocks : 8640;
    order.timestamp = time(nullptr);
    order.nonce = m_swapRelay->nextNonce();

    std::stringstream idStream;
    idStream << std::hex << order.makerPubKey << order.side << order.pair
             << order.price << order.amount << order.nonce;
    order.orderId = Crypto::Hash::toHex(Crypto::cn_fast_hash(idStream.str()));

    order.signature = signOrder(order.orderId);

    m_swapRelay->handleOrderOpen(order, false);

    resp.orderId = order.orderId;
    resp.status = "OPEN";
    resp.statusMsg = "Order submitted";
    return true;
}

bool RpcServer::on_cancel_order(const COMMAND_RPC_CANCEL_ORDER::request& req,
                                 COMMAND_RPC_CANCEL_ORDER::response& resp) {
    m_swapRelay->handleOrderCancel(req.orderId);
    resp.status = "OK";
    return true;
}

bool RpcServer::on_get_open_orders(const COMMAND_RPC_GET_OPEN_ORDERS::request& req,
                                    COMMAND_RPC_GET_OPEN_ORDERS::response& resp) {
    Crypto::PublicKey key;
    if (!req.address.empty()) {
        Common::podFromHex(req.address, key);
    }
    auto orders = m_swapRelay->getOpenOrders(key);
    for (const auto& o : orders) {
        COMMAND_RPC_GET_OPEN_ORDERS::response::OrderJson j;
        j.orderId = o.orderId;
        j.side = (o.side == Order::Side::BID) ? "BID" : "ASK";
        j.pair = o.pair;
        j.price = o.price;
        j.amount = o.amount;
        j.filled = o.filled;
        j.timestamp = o.timestamp;
        j.ttlBlocks = o.ttlBlocks;
        resp.orders.push_back(j);
    }
    resp.status = "OK";
    return true;
}
```

---

## Phase 4: Atomic Swap Integration (Match → Adaptor Protocol)

> **⚠️ Gate this phase** on Phase 1-3 being stable for 1+ week of usage, reservation handshake tested in isolation, and at least 3 manual swaps completed through the order book flow.

When a matching engine cross occurs (and the reservation ACK arrives), the taker's daemon auto-initiates the adaptor signature protocol.

### 4.1 SwapDaemon::onOrderMatched

**File:** `src/SwapDaemon/SwapDaemon.h` — add:

```cpp
void onOrderMatched(const Order& takerOrder, const Order& makerOrder,
                     uint64_t fillAmount, uint64_t fillPrice);
```

**File:** `src/SwapDaemon/SwapDaemon.cpp` — implement:

```cpp
void SwapDaemon::onOrderMatched(const Order& taker, const Order& maker,
                                 uint64_t fillAmount, uint64_t fillPrice) {
    // ASK = selling XFG → Alice (locks XFG in Musig2 escrow)
    // BID = buying XFG  → Bob (provides counterparty coins)
    Order aliceOrder = (taker.side == Order::Side::ASK) ? taker : maker;
    Order bobOrder   = (taker.side == Order::Side::BID)  ? taker : maker;

    XfgSwap::SwapParams params;
    params.swapId = generateSwapId(aliceOrder.orderId, bobOrder.orderId, fillAmount);
    params.pair = aliceOrder.pair;
    params.role = XfgSwap::Role::ALICE;  // or BOB depending on our side
    params.xfgAmount = fillAmount;
    params.ctrAmount = fillAmount * 10000000UL / fillPrice;

    startSwap(params);
}
```

### 4.2 Wire into Matching Engine

In `SwapOfferRelay::handleOrderOpen()`, after receiving `COMMAND_ORDER_RESERVE_ACK`, call:

```cpp
m_swapDaemon->onOrderMatched(takerOrder, makerOrder, fillQty, execPrice);
```

### 4.3 OfferManager: First Order Book Market Maker

Add `side` to `ManagedOffer` struct and update the tick loop to place both bids and asks:

```cpp
// New ManagedOffer struct
struct ManagedOffer {
    uint8_t  pair;
    std::string side;    // "ASK" or "BID"
    uint64_t xfgAmount;
    double   slippagePct;
};

// In tick loop:
void OfferManager::tick() {
    for (const auto& config : m_configs) {
        uint64_t compositeRate = m_relay.getCompositePrice(config.pair).rateNum;
        uint64_t slippageRate = compositeRate * (100.0 + config.slippagePct) / 100.0;

        for (const auto& mo : config.orders) {
            // If no active order for this config, or price drifted, replace it
            if (needsReprice(mo, compositeRate)) {
                cancelExisting(mo);
                submitOrder(mo, slippageRate);
            }
        }
    }
}
```

---

## Phase 5: Go TUI — Order Book Display

### 5.1 New Data Structures

**File:** `swapxfg/app/rpc.go` — add:

```go
type Order struct {
    OrderID   string `json:"orderId"`
    Side      string `json:"side"`       // "BID" or "ASK"
    Pair      uint8  `json:"pair"`
    Price     uint64 `json:"price"`
    Amount    uint64 `json:"amount"`
    Filled    uint64 `json:"filled"`
    Timestamp uint64 `json:"timestamp"`
    TTLBlocks uint32 `json:"ttlBlocks"`
}

type OrderBookLevel struct {
    Price      uint64 `json:"price"`
    Amount     uint64 `json:"amount"`
    OrderCount int    `json:"orderCount"`
}

type OrderBookSnapshot struct {
    Bids   []OrderBookLevel `json:"bids"`
    Asks   []OrderBookLevel `json:"asks"`
    Spread uint64           `json:"spread"`
    Height uint64           `json:"height"`
    Status string           `json:"status"`
}

type PlaceOrderRequest struct {
    Side      uint8  `json:"side"`   // 0=BID, 1=ASK
    Pair      uint8  `json:"pair"`
    Price     uint64 `json:"price"`
    Amount    uint64 `json:"amount"`
    TTLBlocks uint32 `json:"ttlBlocks"`
}

type OrderResult struct {
    OrderID   string `json:"orderId"`
    Status    string `json:"status"`
    Filled    uint64 `json:"filled"`
    StatusMsg string `json:"statusMsg"`
}
```

### 5.2 New RPC Methods

**File:** `swapxfg/app/rpc.go` — add to `FuegoClient`:

```go
func (c *FuegoClient) GetOrderBook(pair uint8, depth int) (*OrderBookSnapshot, error) {
    req := map[string]interface{}{"pair": pair, "depth": depth}
    var resp OrderBookSnapshot
    if err := c.post("/getorderbook", req, &resp); err != nil {
        return nil, err
    }
    return &resp, nil
}

func (c *FuegoClient) PlaceOrder(side uint8, pair uint8, price uint64, amount uint64, ttlBlocks uint32) (*OrderResult, error) {
    req := PlaceOrderRequest{Side: side, Pair: pair, Price: price, Amount: amount, TTLBlocks: ttlBlocks}
    var resp OrderResult
    if err := c.post("/placeorder", req, &resp); err != nil {
        return nil, err
    }
    return &resp, nil
}

func (c *FuegoClient) CancelOrder(orderID string) error {
    req := map[string]interface{}{"orderId": orderID}
    var resp struct{ Status string }
    if err := c.post("/cancelorder", req, &resp); err != nil {
        return err
    }
    if resp.Status != "OK" {
        return fmt.Errorf("cancel order failed")
    }
    return nil
}

func (c *FuegoClient) GetOpenOrders() ([]Order, error) {
    var resp struct {
        Orders []Order `json:"orders"`
        Status string  `json:"status"`
    }
    if err := c.post("/openorders", nil, &resp); err != nil {
        return nil, err
    }
    return resp.Orders, nil
}
```

### 5.3 Update AllPairData

**File:** `swapxfg/app/rpc.go`:

```go
type AllPairData struct {
    OrderBooks map[uint8]*OrderBookSnapshot  // replaces Offers
    Prices     map[uint8]*SwapPriceResponse
    Trades     map[uint8][]SwapTrade
    Height     uint64
    CdOffers   []CdOffer
    CdPrices   map[uint64]*CdPriceStats
}
```

Update `FetchAll` to call `GetOrderBook` instead of `GetOffers`:

```go
orderBook, err := c.GetOrderBook(p, 30)
if err == nil {
    data.OrderBooks[p] = orderBook
}
```

### 5.4 Rewrite RenderOrderbook

**File:** `swapxfg/app/orderbook.go` — full replacement:

```go
func RenderOrderbook(snap *OrderBookSnapshot, width, height int) string {
    title := lipgloss.NewStyle().Bold(true).Foreground(ColorActiveTab).
        Width(width).Align(lipgloss.Center).Render("ORDER BOOK")

    sep := lipgloss.NewStyle().Foreground(ColorMuted).
        Width(width).Align(lipgloss.Center).Render(strings.Repeat("─", width-2))

    if snap == nil || (len(snap.Asks) == 0 && len(snap.Bids) == 0) {
        empty := StyleMuted.Render("  no orders")
        return lipgloss.JoinVertical(lipgloss.Left, title, sep, empty)
    }

    maxRows := (height - 4) / 2
    if maxRows < 1 {
        maxRows = 1
    }

    var lines []string
    lines = append(lines, title, sep)

    header := StyleMuted.Render(fmt.Sprintf("  %-14s %12s %6s", "PRICE", "AMOUNT", "ORDS"))
    lines = append(lines, header)

    // Asks: reverse (lowest ask closest to spread)
    askEntries := snap.Asks
    for i := 0; i < len(askEntries) && i < maxRows; i++ {
        idx := len(askEntries) - 1 - i
        e := askEntries[idx]
        line := fmt.Sprintf("  %-14s %12s %6d",
            formatPrice(e.Price), formatFuegoAmount(e.Amount), e.OrderCount)
        lines = append(lines, StyleBear.Render(truncPad(line, width)))
    }

    // Spread
    spreadLine := "━━━ spread — ━━━"
    if snap.Spread > 0 {
        spreadLine = fmt.Sprintf("━━━ spread %s ━━━", formatPrice(snap.Spread))
    }
    lines = append(lines, StyleSpread.Render(
        lipgloss.NewStyle().Width(width).Align(lipgloss.Center).Render(spreadLine)))

    // Bids: highest bid first
    bidEntries := snap.Bids
    for i := 0; i < len(bidEntries) && i < maxRows; i++ {
        e := bidEntries[i]
        line := fmt.Sprintf("  %-14s %12s %6d",
            formatPrice(e.Price), formatFuegoAmount(e.Amount), e.OrderCount)
        lines = append(lines, StyleBull.Render(truncPad(line, width)))
    }

    return lipgloss.JoinVertical(lipgloss.Left, lines...)
}

func formatPrice(rateNum uint64) string {
    return fmt.Sprintf("%d.%07d", rateNum/10000000, rateNum%10000000)
}

func formatFuegoAmount(atomic uint64) string {
    return fmt.Sprintf("%.1f XFG", float64(atomic)/1e7)
}
```

### 5.5 Update tuiModel and View

**File:** `swapxfg/app/tui.go` — update the View method's right panel:

```go
// Replace:
// offers := m.data.Offers[m.activePair]
// ob := RenderOrderbook(offers, rightW, obH)

// With:
ob := RenderOrderbook(m.data.OrderBooks[m.activePair], rightW, obH)
```

### 5.6 Order Entry Form

**File:** `swapxfg/app/order_entry.go` (new)

```go
package app

import (
    "fmt"
    "strings"

    tea "github.com/charmbracelet/bubbletea"
    "github.com/charmbracelet/lipgloss"
)

type orderEntryModel struct {
    active   bool
    side     string // "BID" or "ASK"
    pair     uint8
    fields   [3]string // price, amount, ttl
    focusIdx int       // 0=price, 1=amount, 2=ttl
    done     bool
    result   string
}

var orderEntryFields = [3]string{"Price", "Amount", "TTL (blocks)"}

func newOrderEntryModel(pair uint8) orderEntryModel {
    return orderEntryModel{
        active:   true,
        side:     "ASK",
        pair:     pair,
        fields:   [3]string{"", "", "8640"},
        focusIdx: 0,
    }
}

func (m orderEntryModel) Init() tea.Cmd { return nil }

func (m *orderEntryModel) Update(msg tea.Msg) (tea.Model, tea.Cmd) {
    if !m.active {
        return m, nil
    }
    switch msg := msg.(type) {
    case tea.KeyMsg:
        k := msg.String()
        switch k {
        case "tab":
            m.focusIdx = (m.focusIdx + 1) % 3
        case "shift+tab":
            m.focusIdx = (m.focusIdx + 2) % 3
        case "enter":
            m.done = true
            m.result = fmt.Sprintf("placeorder %s %d %s %s %s",
                m.side, m.pair, m.fields[0], m.fields[1], m.fields[2])
        case "backspace":
            if m.focusIdx < 3 && len(m.fields[m.focusIdx]) > 0 {
                m.fields[m.focusIdx] = m.fields[m.focusIdx][:len(m.fields[m.focusIdx])-1]
            }
        case "esc":
            m.active = false
            m.done = true
        default:
            if len(k) == 1 && m.focusIdx < 3 {
                m.fields[m.focusIdx] += k
            }
        }
    }
    return m, nil
}

func (m orderEntryModel) View() string {
    if !m.active {
        return ""
    }
    var lines []string
    lines = append(lines, "  ┌─ Place Order ─────────────────────────────")
    for i, field := range orderEntryFields {
        prefix := "  │"
        if i == m.focusIdx {
            prefix = "  >"
        }
        val := m.fields[i]
        cursor := ""
        if i == m.focusIdx {
            cursor = "_"
        }
        lines = append(lines, fmt.Sprintf("%s %-12s: %s%s", prefix, field, val, cursor))
    }
    lines = append(lines, "  └─────────────────────────────────────────────")
    return lipgloss.JoinVertical(lipgloss.Left, lines...)
}
```

### 5.7 New Commands in handleCommand

**File:** `swapxfg/app/tui.go` — add to `handleCommand`:

```go
case "placeorder", "place":
    if len(parts) < 4 {
        m.statusMsg = "usage: placeorder BID|ASK <price> <amount> [ttl_blocks]"
        return nil
    }
    side := parts[1]
    price, _ := strconv.ParseFloat(parts[2], 64)
    amount, _ := strconv.ParseFloat(parts[3], 64)
    ttlBlocks := uint32(8640)
    if len(parts) >= 5 {
        if t, err := strconv.ParseUint(parts[4], 10, 32); err == nil {
            ttlBlocks = uint32(t)
        }
    }
    sideUint := uint8(1) // ASK
    if strings.ToUpper(side) == "BID" {
        sideUint = 0 // BID
    }
    priceNum := uint64(price * 1e7)
    amountAtomic := uint64(amount * 1e7)
    m.statusMsg = fmt.Sprintf("placing %s %.7f @ %.7f ...", strings.ToUpper(side), amount, price)

    client := m.client
    return func() tea.Msg {
        result, err := client.PlaceOrder(sideUint, m.activePair, priceNum, amountAtomic, ttlBlocks)
        if err != nil {
            return statusUpdateMsg{"place order failed: " + err.Error()}
        }
        return statusUpdateMsg{fmt.Sprintf("Order %s: %s (%s)...", result.Status, result.OrderID[:12], result.StatusMsg)}
    }

case "cancelorder":
    if len(parts) < 2 {
        m.statusMsg = "usage: cancelorder <order_id>"
        return nil
    }
    orderID := parts[1]
    client := m.client
    return func() tea.Msg {
        if err := client.CancelOrder(orderID); err != nil {
            return statusUpdateMsg{"cancel failed: " + err.Error()}
        }
        return statusUpdateMsg{"Order cancelled: " + orderID[:12]}
    }

case "myorders":
    client := m.client
    return func() tea.Msg {
        orders, err := client.GetOpenOrders()
        if err != nil {
            return statusUpdateMsg{"fetch open orders failed: " + err.Error()}
        }
        if len(orders) == 0 {
            return statusUpdateMsg{"No open orders"}
        }
        return statusUpdateMsg{fmt.Sprintf("%d open orders: %v", len(orders), orders)}
    }
```

### 5.8 Command Bar Update

```go
func RenderCmdBar(w int) string {
    items := []string{
        "placeorder BID|ASK <price> <amount> [ttl]",
        "cancelorder <id>",
        "myorders",
        "connect metamask|phantom",
        "r:refresh",
        "q:quit",
    }
    // ...
}
```

---

## Phase 6: Wallet Integration (Order Signing)

### 6.1 Order Signature Scheme

Orders must be signed by the maker's Ed25519 key to prevent spoofing:

```cpp
std::string orderIdDeterministic(const Order& o) {
    std::stringstream ss;
    ss << Common::podToHex(o.makerPubKey) << ":"
       << static_cast<int>(o.side) << ":" << static_cast<int>(o.pair) << ":"
       << o.price << ":" << o.amount << ":" << o.nonce;
    return Crypto::Hash::toHex(Crypto::cn_fast_hash(ss.str()));
}
```

### 6.2 New Wallet RPC Methods

Add to `fire_wallet`:

```
sign_order     <orderId>
cancel_order   <orderId>
```

### 6.3 Go Wallet Client

**File:** `swapxfg/app/wallet.go` — add:

```go
type SignOrderResult struct {
    OrderID     string `json:"orderId"`
    MakerPubKey string `json:"makerPubKey"`
    Signature   string `json:"signature"`
    Nonce       uint64 `json:"nonce"`
}

func (w *WalletClient) SignOrder(amount uint64, price uint64, pair uint8, ttlBlocks uint32) (*SignOrderResult, error) {
    var result SignOrderResult
    req := map[string]interface{}{
        "amount":    amount,
        "price":     price,
        "pair":      pair,
        "ttlBlocks": ttlBlocks,
    }
    if err := w.call("sign_order", req, &result); err != nil {
        return nil, err
    }
    return &result, nil
}
```

---

## Phase 7: Tests

### 7.1 Unit Tests

**File:** `tests/SwapOfferRelayTests.cpp`

```cpp
TEST_CASE("OrderBook.Matching.BidTakesAsk") {
    SwapOfferRelay relay;
    Order ask = makeOrder(Order::Side::ASK, PairSOL, 15000000UL, 1000000000UL);
    relay.handleOrderOpen(ask, false);

    Order bid = makeOrder(Order::Side::BID, PairSOL, 20000000UL, 500000000UL);
    relay.handleOrderOpen(bid, false);

    // bid is fully filled, ask has 50 XFG remaining
    auto snap = relay.getOrderBookSnapshot(PairSOL, 5);
    REQUIRE(snap.asks.size() == 1);
    REQUIRE(snap.asks[0].amount == 500000000UL); // 50 XFG remaining
    REQUIRE(snap.bids.empty());
}

TEST_CASE("OrderBook.Matching.PriceTimePriority") {
    // Two asks at same price, verify FIFO fill order
}

TEST_CASE("OrderBook.Matching.NoCross") {
    // Bid below ask, both remain in book
    Order ask = makeOrder(Order::Side::ASK, PairSOL, 20000000UL, 1000000000UL);
    relay.handleOrderOpen(ask, false);
    Order bid = makeOrder(Order::Side::BID, PairSOL, 15000000UL, 500000000UL);
    relay.handleOrderOpen(bid, false);

    auto snap = relay.getOrderBookSnapshot(PairSOL, 5);
    REQUIRE(snap.asks.size() == 1);
    REQUIRE(snap.bids.size() == 1);
}

TEST_CASE("OrderBook.Cancel.RemoveFromLadder") {
    // Place, cancel, verify removed
}

TEST_CASE("OrderBook.Reserve.PreventsDoubleFill") {
    // Two takers, same maker — first reserves, second rejected
}

TEST_CASE("OrderBook.Reserve.ExpiryReturnsAmount") {
    // Reserve expires after 30s, amount returned to book
}

TEST_CASE("OrderBook.Prune.ExpiredOrders") {
    // TTL=1 block, advance time, verify pruned
}
```

### 7.2 Integration Test Script

```bash
#!/bin/bash
# test_orderbook.sh — end-to-end order book flow

DAEMON="http://127.0.0.1:18180"
PAIR=0  # SOL

echo "--- 1. Empty book ---"
curl -s "$DAEMON/getorderbook" -d '{"pair":0,"depth":5}' | jq .

echo "--- 2. Place ASK: 100 XFG @ 0.01 ---"
curl -s "$DAEMON/placeorder" -d '{"side":1,"pair":0,"price":100000,"amount":1000000000,"ttlBlocks":100}' | jq .

echo "--- 3. Place BID: 50 XFG @ 0.02 (crosses) ---"
curl -s "$DAEMON/placeorder" -d '{"side":0,"pair":0,"price":200000,"amount":500000000,"ttlBlocks":100}' | jq .

echo "--- 4. Book after match ---"
curl -s "$DAEMON/getorderbook" -d '{"pair":0,"depth":5}' | jq .

echo "--- 5. Place BID: 25 XFG @ 0.008 (no cross) ---"
curl -s "$DAEMON/placeorder" -d '{"side":0,"pair":0,"price":80000,"amount":250000000,"ttlBlocks":100}' | jq .

echo "--- 6. Final snapshot ---"
curl -s "$DAEMON/getorderbook" -d '{"pair":0,"depth":5}' | jq .
```

---

## Implementation Order & Dependencies

| Phase | Scope | Effort | Depends On | Risk |
|---|---|---|---|---|
| 1 | Data structures + P2P wire format + read-only probe | Medium | None | Low |
| 2 | Matching engine + reservation protocol | Large | Phase 1 | Medium |
| 3 | RPC endpoints (4 new) | Medium | Phase 2 | Low |
| 4 | Atomic swap integration (match→adaptor) | Medium | Phase 2 + existing SwapDaemon | **High** |
| 5 | Go TUI display + order entry | Medium | Phase 3 (RPC contracts only) | Low |
| 6 | Wallet signing integration | Small | Phase 3 | Low |
| 7 | Tests | Medium | Phase 2+4 | Low |

**Total estimate:** ~15 files changed, ~4 new files, ~1400 lines of new code.

Phases 1-4 form the core (C++ daemon). Phase 5 is the Go TUI and can be developed in parallel once Phase 3 RPC contracts are defined.

**Recommended delivery sequence:**
1. Phase 1 (read-only order book display) — ship immediately, zero risk to existing swaps
2. Phase 5 (TUI display) — ship alongside Phase 1, users see a real order book
3. Phase 2+3 (matching engine) — ship after reservation protocol is tested in isolation
4. Phase 4 (auto-execution) — gate on Phase 2 stability, manual swap testing
5. Phase 6+7 — concurrent with Phase 4

Each phase produces a working increment. You can stop at Phase 3 and have a fully functional order book display + manual order placement system without touching the swap execution path.
