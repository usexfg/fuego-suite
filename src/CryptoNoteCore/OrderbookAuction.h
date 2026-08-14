// Copyright (c) 2026 Fuego Developers
//
// Per-block call auction over tx-extra-backed limit deposits.
// Matches user-vs-user at a single clearing price that maximizes executed
// volume (exchange opening-auction rules), with deterministic tie-breaks,
// price-time priority, and same-key self-trade exclusion. Pure integer math;
// no pool involvement — price discovery comes from the crossing of committed
// orders, not from the AMM ratio.

#pragma once

#include <cstdint>
#include <vector>
#include "crypto/hash.h"

namespace CryptoNote {

struct AuctionOrder {
  Crypto::Hash orderId;
  uint64_t price;        // canonical: HEAT atomics per XFG atomic × COIN
  uint64_t volumeXfg;    // XFG atomics
  uint32_t createdHeight;
  Crypto::Hash addressHash;
};

struct AuctionFill {
  Crypto::Hash orderId;
  uint8_t side;      // 0 = bid (BUY_XFG), 1 = ask (SELL_XFG)
  uint64_t fillXfg;  // XFG atomics exchanged
  uint64_t heat;     // fillXfg × clearingPrice / COIN (buyer pays, seller receives)
};

struct AuctionResult {
  bool crossed = false;
  uint64_t clearingPrice = 0;
  uint64_t matchedVolume = 0;
  std::vector<AuctionFill> fills;
};

// Bids (BUY_XFG) sorted price-desc; asks (SELL_XFG) sorted price-asc.
// prevPclear: last block's clearing price, used only for tie-breaks.
AuctionResult runAuction(const std::vector<AuctionOrder>& bids,
                         const std::vector<AuctionOrder>& asks,
                         uint64_t prevPclear);

} // namespace CryptoNote
