// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You can redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation. Fuego includes elements written
// by third parties. See file labeled LICENSE for more details.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <string>
#include <vector>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <cstdint>
#include <ctime>
#include "crypto/crypto.h"
#include "crypto/hash.h"

namespace CryptoNote {

class core;
class NodeServer;
class IP2pEndpoint;

// A swap offer relayed over P2P
struct SwapOfferMsg {
  std::string offerId;      // SHA-256 of (maker_pubkey || pair || amount || rate || timestamp)
  bool        isSell;       // true = selling XFG for CTR
  uint64_t    xfgAmount;    // atomic units (7 decimals)
  uint64_t    rateNum;      // rate numerator (XFG per 1 CTR, scaled by 1e7)
  uint8_t     pair;         // 0=XMR, 1=ETH, 2=BCH
  Crypto::PublicKey makerPubKey;  // maker's wallet pubkey
  Crypto::Signature signature;   // signs (offerId) with maker's key
  uint64_t    timestamp;
  uint32_t    ttlBlocks;    // offer expires after this many blocks from posting
  uint32_t    postedHeight; // block height when posted
  bool        isSoftOrder;  // true if this is an intent without an on-chain lock
  uint8_t     allowedSlippagePct; // allowed slippage
};

// A completed swap trade (for TWAP tracking)
struct SwapTradeRecord {
  uint8_t     pair;
  uint64_t    xfgAmount;
  uint64_t    ctrAmount;
  double      rate;
  uint32_t    blockHeight;
  uint64_t    timestamp;
};

// ============================================================================
// External price source abstraction
// ============================================================================

// Each source contributes a rate (XFG per 1 CTR) for a given pair.
// Weight determines influence in the composite price.
// Sources with rate=0 or stale=true are excluded from the composite.
struct PriceSource {
  std::string name;       // e.g. "atomic_swap", "heat_eth_pool", "coingecko"
  uint8_t     pair;       // 0=XMR, 1=ETH, 2=BCH, 255=USD (direct)
  double      weight;     // relative weight (higher = more influence)
  double      rate;       // XFG per 1 CTR coin (0 = no data)
  uint64_t    updatedAt;  // unix timestamp of last update
  bool        stale;      // true if data is too old to trust
};

// Composite price result with source breakdown
struct CompositePrice {
  double      rate;                       // weighted average XFG/CTR
  double      totalWeight;                // sum of contributing weights
  size_t      sourceCount;                // how many sources contributed
  std::vector<PriceSource> sources;       // individual source snapshots
};

// Cross-pair native XFG price range (USD-equivalent via CTR reference prices)
struct NativeXfgPriceRange {
  double      lowUsd;       // lowest implied USD price across all pairs
  double      highUsd;      // highest implied USD price across all pairs
  double      midUsd;       // average
  size_t      pairCount;    // how many pairs contributed

  // Per-pair implied prices (pair index → implied USD)
  std::map<uint8_t, double> pairImplied;
};

// HEAT/ETH pool config — drop-in when pool address is known
// 1 private XFG = 10,000,000 HEAT (public ERC-20)
static const uint64_t HEAT_PER_XFG = 10000000ULL;

struct PoolSourceConfig {
  std::string name;             // "heat_eth_pool", "uniswap_v3", etc.
  std::string endpoint;         // RPC/API URL (empty = disabled)
  std::string poolAddress;      // contract address (empty = disabled)
  uint8_t     pair;             // which pair this maps to
  double      weight;           // relative weight
  uint64_t    pollIntervalSec;  // how often to fetch (0 = manual only)
  uint64_t    maxAgeSec;        // mark stale after this many seconds
};

// ============================================================================
// Swap offer relay service
// ============================================================================

// Swap offer relay service — runs alongside ElderfierSignatureBroadcaster in fuegod
class SwapOfferRelay {
public:
  SwapOfferRelay(core& ccore, NodeServer& p2psrv, IP2pEndpoint* p2pEndpoint = nullptr);
  ~SwapOfferRelay();

  // Start/stop the relay service
  void start();
  void stop();

  // Handle incoming offer from P2P
  void handleOfferMessage(const SwapOfferMsg& offer);

  // Handle incoming cancel from P2P
  void handleCancelMessage(const std::string& offerId, const Crypto::PublicKey& pubkey,
                           const Crypto::Signature& sig);

  // Handle incoming swap request from taker P2P (auto-execution trigger)
  void handleSwapRequest(const std::string& offerId, uint64_t amount,
                         const std::string& takerPubKey, const std::string& proofOfFunds);

  std::vector<std::tuple<std::string, uint64_t, std::string, std::string>> getPendingSwapRequests();

  // Handle completed swap notification (for TWAP)
  void handleTradeCompleted(const SwapTradeRecord& trade);

  // Query interface (used by RPC handlers)
  std::vector<SwapOfferMsg> getOffers(uint8_t pair) const;
  std::vector<SwapOfferMsg> getAllOffers() const;
  std::vector<SwapTradeRecord> getRecentTrades(uint8_t pair, size_t limit) const;
  double getTwap(uint8_t pair) const;

  // Composite pricing — weighted across all sources
  CompositePrice getCompositePrice(uint8_t pair) const;

  // Cross-pair native XFG price range (USD-equivalent)
  NativeXfgPriceRange getNativeXfgPrice() const;

  // Register an external price source (HEAT pool, DEX, CEX)
  // Call updateExternalPrice() to push new data when available.
  void addExternalSource(const PoolSourceConfig& config);
  void updateExternalPrice(const std::string& sourceName, uint8_t pair, double rate);

  // Submit a new offer (from local RPC)
  bool submitOffer(const SwapOfferMsg& offer);

  // Cancel an offer (from local RPC)
  bool cancelOffer(const std::string& offerId, const Crypto::PublicKey& pubkey,
                   const Crypto::Signature& sig);

  // Seed rates: XFG per 1 whole CTR coin (1 XFG = $0.01 USD)
  static double getSeedRate(uint8_t pair);

  // Reference USD prices for CTR coins (for cross-pair triangulation)
  static double getCtrUsdPrice(uint8_t pair);

private:
  core& m_core;
  NodeServer& m_p2p;
  IP2pEndpoint* m_p2pEndpoint;
  mutable std::mutex m_mutex;
  std::atomic<bool> m_running{false};

  // Cleanup thread — prunes expired offers
  std::thread m_cleanupThread;
  void cleanupThread();

  // Active offers indexed by offerId
  std::map<std::string, SwapOfferMsg> m_offers;
  std::vector<std::tuple<std::string, uint64_t, std::string, std::string>> m_pendingRequests;

  // Completed trades for TWAP (bounded deque, newest at back)
  std::deque<SwapTradeRecord> m_trades;
  static const size_t MAX_TRADE_HISTORY = 200;

  // TWAP parameters
  static const size_t TWAP_WINDOW = 20;
  static const uint64_t TWAP_MAX_AGE = 604800;  // 7 days
  static const size_t TWAP_MIN_TRADES = 5;

  // External price sources (keyed by name)
  std::map<std::string, PriceSource> m_externalSources;
  std::vector<PoolSourceConfig> m_poolConfigs;

  // Validate offer signature
  bool validateOffer(const SwapOfferMsg& offer) const;
};

}  // namespace CryptoNote
