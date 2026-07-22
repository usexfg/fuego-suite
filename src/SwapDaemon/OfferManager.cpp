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

#include "OfferManager.h"
#include "PriceOracle.h"
#include "CryptoNoteCore/SwapOfferRelay.h"
#include "Common/JsonValue.h"
#include <fstream>
#include <sstream>
#include <cmath>
#include <ctime>

namespace XfgSwap {

OfferManager::OfferManager(CryptoNote::SwapOfferRelay& relay,
                           const Crypto::SecretKey& makerSecretKey,
                           const Crypto::PublicKey& makerPublicKey,
                           Logging::ILogger& logger)
  : m_relay(relay),
    m_makerSecretKey(makerSecretKey),
    m_makerPublicKey(makerPublicKey),
    m_logger(logger, "OfferManager") {}

bool OfferManager::loadConfig(const std::string& jsonPath) {
  std::ifstream f(jsonPath);
  if (!f.is_open()) {
    m_logger(Logging::ERROR) << "Cannot open config: " << jsonPath;
    return false;
  }
  std::stringstream ss;
  ss << f.rdbuf();
  return loadConfigFromJson(ss.str());
}

bool OfferManager::loadConfigFromJson(const std::string& json) {
  try {
    Common::JsonValue root = Common::JsonValue::fromString(json);
    if (!root.isObject() || !root.contains("offers")) return false;

    uint32_t ttlOverride = 60;
    if (root.contains("ttlBlocks")) {
      ttlOverride = static_cast<uint32_t>(root("ttlBlocks").getInteger());
    }
    m_ttlBlocks = ttlOverride;

    const auto& offers = root("offers");
    if (!offers.isArray()) return false;

    for (size_t i = 0; i < offers.size(); ++i) {
      const auto& entry = offers[i];
      if (!entry.isObject()) continue;

      ManagedOffer mo;
      mo.pair         = static_cast<uint8_t>(entry("pair").getInteger());
      mo.xfgAmount    = static_cast<uint64_t>(entry("xfgAmount").getInteger());
      mo.slippagePct  = static_cast<uint8_t>(entry("slippagePct").getInteger());
      if (mo.slippagePct == 0) mo.slippagePct = 5;

      m_logger(Logging::INFO) << "Loaded managed offer: pair=" << (int)mo.pair
                              << " amount=" << mo.xfgAmount
                              << " slippage=" << (int)mo.slippagePct << "%";
      m_states.push_back({mo, "", 0, 0, 0});
    }
    m_logger(Logging::INFO) << "Loaded " << m_states.size() << " managed offers";
    return true;
  } catch (const std::exception& e) {
    m_logger(Logging::ERROR) << "Config parse error: " << e.what();
    return false;
  }
}

uint64_t OfferManager::compositeToRateNum(uint8_t pair) {
  CryptoNote::CompositePrice cp = m_relay.getCompositePrice(pair);
  if (cp.sourceCount == 0) {
    double seedRate = PriceOracle::getSeedRate(static_cast<SwapPair>(pair));
    return static_cast<uint64_t>(seedRate * 1e7);
  }
  return static_cast<uint64_t>(cp.rate * 1e7);
}

void OfferManager::submitManagedOffer(OfferState& state, uint32_t currentHeight, uint64_t rateNum) {
  CryptoNote::SwapOfferMsg offer;
  std::string offerSeed = std::to_string(state.config.pair) + ":" +
    std::to_string(state.config.xfgAmount) + ":" +
    std::to_string(rateNum) + ":" +
    std::to_string(currentHeight) + ":" +
    std::to_string(time(nullptr));

  Crypto::Hash offerIdHash;
  cn_fast_hash(offerSeed.data(), offerSeed.size(), offerIdHash);
  offer.offerId = Common::podToHex(offerIdHash);
  offer.isSell = true;
  offer.xfgAmount = state.config.xfgAmount;
  offer.filledAmount = 0;
  offer.rateNum = rateNum;
  offer.pair = state.config.pair;
  offer.makerPubKey = m_makerPublicKey;
  offer.timestamp = time(nullptr);
  offer.ttlBlocks = m_ttlBlocks;
  offer.postedHeight = currentHeight;
  offer.isSoftOrder = true;
  offer.allowedSlippagePct = state.config.slippagePct;

  // Sign canonical economic fields (must match SwapOfferRelay::validateOffer).
  std::string sigData;
  sigData.reserve(offer.offerId.size() + 64);
  sigData.append(offer.offerId);
  sigData.append(1, static_cast<char>(offer.pair));
  sigData.append(reinterpret_cast<const char*>(&offer.xfgAmount), sizeof(offer.xfgAmount));
  sigData.append(reinterpret_cast<const char*>(&offer.rateNum), sizeof(offer.rateNum));
  sigData.append(1, offer.isSoftOrder ? '\x01' : '\x00');
  sigData.append(reinterpret_cast<const char*>(&offer.ttlBlocks), sizeof(offer.ttlBlocks));
  sigData.append(1, static_cast<char>(offer.allowedSlippagePct));
  sigData.append(reinterpret_cast<const char*>(&offer.timestamp), sizeof(offer.timestamp));
  Crypto::Hash sigHash;
  cn_fast_hash(sigData.data(), sigData.size(), sigHash);
  Crypto::generate_signature(sigHash, offer.makerPubKey, m_makerSecretKey, offer.signature);

  if (m_relay.submitOffer(offer)) {
    state.offerId = offer.offerId;
    state.rateNum = rateNum;
    state.postedHeight = currentHeight;
    state.postedTimestamp = offer.timestamp;
    m_logger(Logging::INFO) << "Submitted managed offer " << state.offerId
                            << " pair=" << (int)state.config.pair
                            << " rate=" << (rateNum / 1e7) << " XFG/CTR";
  } else {
    m_logger(Logging::ERROR) << "Failed to submit managed offer";
  }
}

void OfferManager::cancelManagedOffer(OfferState& state) {
  if (state.offerId.empty()) return;
  Crypto::Hash cancelHash;
  std::string cancelData = "cancel:" + state.offerId;
  cn_fast_hash(cancelData.data(), cancelData.size(), cancelHash);
  Crypto::Signature sig;
  Crypto::generate_signature(cancelHash, m_makerPublicKey, m_makerSecretKey, sig);

  if (m_relay.cancelOffer(state.offerId, m_makerPublicKey, sig)) {
    m_logger(Logging::INFO) << "Cancelled managed offer " << state.offerId;
    state.offerId.clear();
  }
}

void OfferManager::tick(uint32_t currentHeight) {
  if (m_states.empty()) return;
  m_running = true;

  for (auto& state : m_states) {
    uint64_t compositeRateNum = compositeToRateNum(state.config.pair);
    if (compositeRateNum == 0) continue;

    bool needsReprice = false;

    if (!state.offerId.empty()) {
      uint32_t age = currentHeight - state.postedHeight;
      if (age >= m_ttlBlocks) {
        m_logger(Logging::INFO) << "Offer " << state.offerId << " expired (age=" << age << " blocks)";
        cancelManagedOffer(state);
        needsReprice = true;
      } else if (state.rateNum > 0 && compositeRateNum > 0) {
        uint64_t diff = (state.rateNum > compositeRateNum)
                        ? (state.rateNum - compositeRateNum)
                        : (compositeRateNum - state.rateNum);
        double driftPct = (static_cast<double>(diff) / static_cast<double>(state.rateNum)) * 100.0;
        if (driftPct > static_cast<double>(state.config.slippagePct)) {
          m_logger(Logging::INFO) << "Offer " << state.offerId << " drifted "
                                  << driftPct << "% from composite";
          cancelManagedOffer(state);
          needsReprice = true;
        }
      }
    } else {
      needsReprice = true;
    }

    if (needsReprice) {
      submitManagedOffer(state, currentHeight, compositeRateNum);
    }
  }
}

void OfferManager::shutdown() {
  m_running = false;
  for (auto& state : m_states) {
    cancelManagedOffer(state);
  }
  m_states.clear();
  m_logger(Logging::INFO) << "OfferManager shutdown complete";
}

} // namespace XfgSwap
