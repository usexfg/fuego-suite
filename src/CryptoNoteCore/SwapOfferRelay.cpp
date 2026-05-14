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

#include "SwapOfferRelay.h"
#include "Core.h"
#include "P2p/NetNode.h"
#include "P2p/NetNodeCommon.h"
#include "P2p/P2pProtocolDefinitions.h"
#include "P2p/LevinProtocol.h"
#include "crypto/crypto.h"
#include <algorithm>
#include <chrono>

namespace CryptoNote {

// Seed rates: XFG per 1 whole CTR coin (1 XFG = $0.01 USD, March 2026)
//   ETH = $2,140 → 214,000 XFG/ETH
//   BCH = $469   →  46,900 XFG/BCH
//   XMR = $343   →  34,300 XFG/XMR
double SwapOfferRelay::getSeedRate(uint8_t pair) {
  switch (pair) {
    case 1: return 214000.0;  // ETH
    case 2: return 46900.0;   // BCH
    case 0: return 34300.0;   // XMR
    default: return 0.0;
  }
}

// Reference USD prices for counterparty coins (for cross-pair triangulation)
// These are bootstrap values; external sources override when available.
double SwapOfferRelay::getCtrUsdPrice(uint8_t pair) {
  switch (pair) {
    case 1: return 2140.0;   // ETH
    case 2: return 469.0;    // BCH
    case 0: return 343.0;    // XMR
    default: return 0.0;
  }
}

SwapOfferRelay::SwapOfferRelay(core& ccore, NodeServer& p2psrv, IP2pEndpoint* p2pEndpoint)
  : m_core(ccore), m_p2p(p2psrv), m_p2pEndpoint(p2pEndpoint) {
}

SwapOfferRelay::~SwapOfferRelay() {
  stop();
}

void SwapOfferRelay::start() {
  m_running = true;
  m_cleanupThread = std::thread([this] { cleanupThread(); });
}

void SwapOfferRelay::stop() {
  m_running = false;
  if (m_cleanupThread.joinable()) {
    m_cleanupThread.join();
  }
}

void SwapOfferRelay::cleanupThread() {
  while (m_running) {
    try {
      uint32_t currentHeight = 0;
      Crypto::Hash topId;
      m_core.get_blockchain_top(currentHeight, topId);

      {
        std::lock_guard<std::mutex> lock(m_mutex);
        // Remove expired offers
        for (auto it = m_offers.begin(); it != m_offers.end(); ) {
          if (currentHeight > it->second.postedHeight + it->second.ttlBlocks) {
            it = m_offers.erase(it);
          } else {
            ++it;
          }
        }

        // Trim old trades
        while (m_trades.size() > MAX_TRADE_HISTORY) {
          m_trades.pop_front();
        }
      }
    } catch (...) {
      // Non-fatal
    }

    // Clean up every 30 seconds
    for (int i = 0; i < 30 && m_running; ++i) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }
}

bool SwapOfferRelay::validateOffer(const SwapOfferMsg& offer) const {
  if (offer.offerId.empty()) return false;
  if (offer.xfgAmount == 0) return false;
  if (offer.rateNum == 0) return false;
  if (offer.pair > 2) return false;
  if (offer.ttlBlocks == 0 || offer.ttlBlocks > 1080) return false;  // max ~6 days at 8min blocks

  // Verify signature: maker signs the offerId hash
  Crypto::Hash offerHash;
  cn_fast_hash(offer.offerId.data(), offer.offerId.size(), offerHash);
  return Crypto::check_signature(offerHash, offer.makerPubKey, offer.signature);
}

void SwapOfferRelay::handleOfferMessage(const SwapOfferMsg& offer) {
  if (!validateOffer(offer)) return;

  std::lock_guard<std::mutex> lock(m_mutex);
  // Don't replace existing offers — first-seen wins
  if (m_offers.find(offer.offerId) != m_offers.end()) return;
  m_offers[offer.offerId] = offer;
}

void SwapOfferRelay::handleCancelMessage(const std::string& offerId,
                                          const Crypto::PublicKey& pubkey,
                                          const Crypto::Signature& sig) {
  // Verify the canceller is the maker
  Crypto::Hash cancelHash;
  std::string cancelData = "cancel:" + offerId;
  cn_fast_hash(cancelData.data(), cancelData.size(), cancelHash);
  if (!Crypto::check_signature(cancelHash, pubkey, sig)) return;

  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_offers.find(offerId);
  if (it != m_offers.end() && it->second.makerPubKey == pubkey) {
    m_offers.erase(it);
  }
}

void SwapOfferRelay::handleSwapRequest(const std::string& offerId, uint64_t amount,
                                       const std::string& takerPubKey, const std::string& proofOfFunds) {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_offers.find(offerId);
  if (it != m_offers.end()) {
    m_pendingRequests.push_back(std::make_tuple(offerId, amount, takerPubKey, proofOfFunds));
  }
}

std::vector<std::tuple<std::string, uint64_t, std::string, std::string>> SwapOfferRelay::getPendingSwapRequests() {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::vector<std::tuple<std::string, uint64_t, std::string, std::string>> result = m_pendingRequests;
  m_pendingRequests.clear();
  return result;
}

void SwapOfferRelay::handleTradeCompleted(const SwapTradeRecord& trade) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_trades.push_back(trade);
  while (m_trades.size() > MAX_TRADE_HISTORY) {
    m_trades.pop_front();
  }
}

std::vector<SwapOfferMsg> SwapOfferRelay::getOffers(uint8_t pair) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::vector<SwapOfferMsg> result;
  for (const auto& kv : m_offers) {
    if (kv.second.pair == pair) {
      result.push_back(kv.second);
    }
  }
  return result;
}

std::vector<SwapOfferMsg> SwapOfferRelay::getAllOffers() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::vector<SwapOfferMsg> result;
  result.reserve(m_offers.size());
  for (const auto& kv : m_offers) {
    result.push_back(kv.second);
  }
  return result;
}

std::vector<SwapTradeRecord> SwapOfferRelay::getRecentTrades(uint8_t pair, size_t limit) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::vector<SwapTradeRecord> result;
  for (auto it = m_trades.rbegin(); it != m_trades.rend() && result.size() < limit; ++it) {
    if (it->pair == pair) {
      result.push_back(*it);
    }
  }
  return result;
}

double SwapOfferRelay::getTwap(uint8_t pair) const {
  std::lock_guard<std::mutex> lock(m_mutex);

  time_t now = std::time(nullptr);
  double weightedSum = 0.0;
  double volumeSum = 0.0;
  size_t count = 0;

  for (auto it = m_trades.rbegin(); it != m_trades.rend() && count < TWAP_WINDOW; ++it) {
    if (it->pair != pair) continue;
    if (TWAP_MAX_AGE > 0 && (now - static_cast<time_t>(it->timestamp)) > static_cast<time_t>(TWAP_MAX_AGE)) {
      continue;
    }
    double volume = static_cast<double>(it->xfgAmount) / 1e7;
    weightedSum += it->rate * volume;
    volumeSum += volume;
    ++count;
  }

  if (count < TWAP_MIN_TRADES || volumeSum <= 0.0) {
    return getSeedRate(pair);  // Not enough data, use seed rate
  }

  return weightedSum / volumeSum;
}

bool SwapOfferRelay::submitOffer(const SwapOfferMsg& offer) {
  if (!validateOffer(offer)) return false;

  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_offers.find(offer.offerId) != m_offers.end()) return false;  // duplicate
    m_offers[offer.offerId] = offer;
  }

  // Relay to P2P peers
  if (m_p2pEndpoint) {
    COMMAND_SWAP_OFFER::request msg;
    msg.offerId = offer.offerId;
    msg.xfgAmount = offer.xfgAmount;
    msg.rateNum = offer.rateNum;
    msg.pair = offer.pair;
    msg.makerPubKey = offer.makerPubKey;
    msg.signature = offer.signature;
    msg.timestamp = offer.timestamp;
    msg.ttlBlocks = offer.ttlBlocks;
    msg.postedHeight = offer.postedHeight;
    msg.isSoftOrder = offer.isSoftOrder;

    auto buf = LevinProtocol::encode(msg);
    m_p2pEndpoint->externalRelayNotifyToAll(COMMAND_SWAP_OFFER::ID, buf, nullptr);
  }

  return true;
}

bool SwapOfferRelay::cancelOffer(const std::string& offerId,
                                  const Crypto::PublicKey& pubkey,
                                  const Crypto::Signature& sig) {
  handleCancelMessage(offerId, pubkey, sig);

  // Relay cancel to P2P peers
  if (m_p2pEndpoint) {
    COMMAND_SWAP_CANCEL::request msg;
    msg.offerId = offerId;
    msg.makerPubKey = pubkey;
    msg.signature = sig;

    auto buf = LevinProtocol::encode(msg);
    m_p2pEndpoint->externalRelayNotifyToAll(COMMAND_SWAP_CANCEL::ID, buf, nullptr);
  }

  return true;
}

// ============================================================================
// External price sources
// ============================================================================

void SwapOfferRelay::addExternalSource(const PoolSourceConfig& config) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_poolConfigs.push_back(config);

  // Initialize the source entry (rate=0, stale until first update)
  PriceSource src;
  src.name      = config.name;
  src.pair      = config.pair;
  src.weight    = config.weight;
  src.rate      = 0.0;
  src.updatedAt = 0;
  src.stale     = true;
  m_externalSources[config.name] = src;
}

void SwapOfferRelay::updateExternalPrice(const std::string& sourceName, uint8_t pair, double rate) {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_externalSources.find(sourceName);
  if (it == m_externalSources.end()) {
    // Auto-register unknown source with default weight
    PriceSource src;
    src.name      = sourceName;
    src.pair      = pair;
    src.weight    = 1.0;
    src.rate      = rate;
    src.updatedAt = static_cast<uint64_t>(std::time(nullptr));
    src.stale     = false;
    m_externalSources[sourceName] = src;
    return;
  }
  it->second.rate      = rate;
  it->second.pair      = pair;
  it->second.updatedAt = static_cast<uint64_t>(std::time(nullptr));
  it->second.stale     = false;
}

// ============================================================================
// Composite pricing — weighted average across all sources for a pair
// ============================================================================

CompositePrice SwapOfferRelay::getCompositePrice(uint8_t pair) const {
  std::lock_guard<std::mutex> lock(m_mutex);

  CompositePrice result;
  result.rate        = 0.0;
  result.totalWeight = 0.0;
  result.sourceCount = 0;

  time_t now = std::time(nullptr);

  // Source 1: native atomic swap TWAP (always weight=10 when available)
  {
    double weightedSum = 0.0;
    double volumeSum   = 0.0;
    size_t count       = 0;

    for (auto it = m_trades.rbegin(); it != m_trades.rend() && count < TWAP_WINDOW; ++it) {
      if (it->pair != pair) continue;
      if (TWAP_MAX_AGE > 0 && (now - static_cast<time_t>(it->timestamp)) > static_cast<time_t>(TWAP_MAX_AGE)) {
        continue;
      }
      double volume = static_cast<double>(it->xfgAmount) / 1e7;
      weightedSum += it->rate * volume;
      volumeSum += volume;
      ++count;
    }

    if (count >= TWAP_MIN_TRADES && volumeSum > 0.0) {
      double twapRate = weightedSum / volumeSum;
      double swapWeight = 10.0;  // native swaps are highest-trust source

      PriceSource src;
      src.name      = "atomic_swap";
      src.pair      = pair;
      src.weight    = swapWeight;
      src.rate      = twapRate;
      src.updatedAt = (m_trades.empty()) ? 0 : m_trades.back().timestamp;
      src.stale     = false;
      result.sources.push_back(src);

      result.rate        += twapRate * swapWeight;
      result.totalWeight += swapWeight;
      result.sourceCount++;
    }
  }

  // Source 2+: external sources (HEAT pool, DEX, CEX)
  for (const auto& kv : m_externalSources) {
    const PriceSource& ext = kv.second;
    if (ext.pair != pair) continue;
    if (ext.rate <= 0.0) continue;
    if (ext.stale) continue;

    // Check staleness against config maxAgeSec
    bool isStale = false;
    for (const auto& cfg : m_poolConfigs) {
      if (cfg.name == ext.name && cfg.maxAgeSec > 0) {
        isStale = (now - static_cast<time_t>(ext.updatedAt)) > static_cast<time_t>(cfg.maxAgeSec);
        break;
      }
    }
    if (isStale) continue;

    PriceSource snap = ext;
    result.sources.push_back(snap);

    result.rate        += ext.rate * ext.weight;
    result.totalWeight += ext.weight;
    result.sourceCount++;
  }

  // Compute weighted average
  if (result.totalWeight > 0.0) {
    result.rate /= result.totalWeight;
  } else {
    // No sources — fall back to seed rate
    result.rate = getSeedRate(pair);

    PriceSource seed;
    seed.name      = "seed";
    seed.pair      = pair;
    seed.weight    = 1.0;
    seed.rate      = result.rate;
    seed.updatedAt = 0;
    seed.stale     = false;
    result.sources.push_back(seed);
    result.totalWeight = 1.0;
    result.sourceCount = 1;
  }

  return result;
}

// ============================================================================
// Cross-pair native XFG price range
// ============================================================================

NativeXfgPriceRange SwapOfferRelay::getNativeXfgPrice() const {
  // For each pair, compute implied XFG/USD:
  //   XFG/USD = CTR_USD_price / (XFG_per_CTR rate)
  // Use composite price for each pair.

  NativeXfgPriceRange range;
  range.lowUsd   = 0.0;
  range.highUsd  = 0.0;
  range.midUsd   = 0.0;
  range.pairCount = 0;

  double sum = 0.0;

  for (uint8_t p = 0; p <= 2; ++p) {
    CompositePrice cp = getCompositePrice(p);
    if (cp.rate <= 0.0) continue;

    double ctrUsd = getCtrUsdPrice(p);
    if (ctrUsd <= 0.0) continue;

    // Implied XFG price in USD
    // rate = XFG per 1 CTR, so 1 XFG = ctrUsd / rate
    double impliedUsd = ctrUsd / cp.rate;

    range.pairImplied[p] = impliedUsd;

    if (range.pairCount == 0) {
      range.lowUsd  = impliedUsd;
      range.highUsd = impliedUsd;
    } else {
      if (impliedUsd < range.lowUsd)  range.lowUsd  = impliedUsd;
      if (impliedUsd > range.highUsd) range.highUsd = impliedUsd;
    }

    sum += impliedUsd;
    range.pairCount++;
  }

  if (range.pairCount > 0) {
    range.midUsd = sum / static_cast<double>(range.pairCount);
  }

  return range;
}

}  // namespace CryptoNote
