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

#include "SwapTypes.h"
#include "crypto/crypto.h"
#include "crypto/hash.h"
#include "../Logging/ILogger.h"
#include "../Logging/LoggerRef.h"
#include <string>
#include <vector>
#include <cstdint>

namespace CryptoNote {
  class SwapOfferRelay;
}

namespace XfgSwap {

class OfferManager {
public:
  struct ManagedOffer {
    uint8_t  pair;          // 0=SOL, 1=ETH, 2=XMR, 3=BCH
    uint64_t xfgAmount;     // total atomic XFG to offer
    uint8_t  slippagePct;   // reprice if composite drifts by more than this %
  };

  OfferManager(CryptoNote::SwapOfferRelay& relay,
               const Crypto::SecretKey& makerSecretKey,
               const Crypto::PublicKey& makerPublicKey,
               Logging::ILogger& logger);

  bool loadConfig(const std::string& jsonPath);
  bool loadConfigFromJson(const std::string& json);
  void tick(uint32_t currentHeight);
  void shutdown();
  size_t activeOfferCount() const { return m_states.size(); }

  struct OfferState {
    ManagedOffer config;
    std::string offerId;
    uint64_t rateNum;
    uint32_t postedHeight;
    uint64_t postedTimestamp;
  };

  const std::vector<OfferState>& getActiveOffers() const { return m_states; }

private:
  uint64_t compositeToRateNum(uint8_t pair);
  void submitManagedOffer(OfferState& state, uint32_t currentHeight, uint64_t rateNum);
  void cancelManagedOffer(OfferState& state);

  CryptoNote::SwapOfferRelay& m_relay;
  Crypto::SecretKey m_makerSecretKey;
  Crypto::PublicKey m_makerPublicKey;
  Logging::LoggerRef m_logger;
  std::vector<OfferState> m_states;
  uint32_t m_ttlBlocks = 60;
  bool m_running = false;
};

} // namespace XfgSwap
