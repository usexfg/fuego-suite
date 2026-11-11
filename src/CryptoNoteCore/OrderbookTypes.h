// Copyright (c) 2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You can redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>

#include "../../include/CryptoNote.h"
#include "../Serialization/ISerializer.h"

namespace CryptoNote {

struct Order {
  uint8_t  side;
  uint64_t amount;
  uint64_t price;
  uint64_t targetPrice;
  uint32_t expiration;
  Crypto::Hash orderId;
  Crypto::Hash utxoTxHash;
  uint32_t outputIndex;
  Crypto::Hash addressHash; // cn_fast_hash(spendKey||viewKey) — sender identity (privacy-preserving)
  std::vector<Crypto::Signature> partialSigs;
};

struct SenderKey {
  Crypto::Hash hash; // cn_fast_hash(spendKey||viewKey)

  bool operator<(const SenderKey& o) const {
    return memcmp(hash.data, o.hash.data, sizeof(hash.data)) < 0;
  }
  bool operator==(const SenderKey& o) const {
    return memcmp(hash.data, o.hash.data, sizeof(hash.data)) == 0;
  }
};

struct OrderPartialSig {
  Crypto::EllipticCurvePoint nonceCommitment;
  Crypto::EllipticCurveScalar challengeShare;
  Crypto::EllipticCurveScalar responseShare;
};

struct OrderbookReceipt {
  uint64_t clearingPrice;
  uint32_t numBidLevels;
  uint32_t numAskLevels;

  struct Level {
    uint64_t price;
    uint64_t depth;
    void serialize(ISerializer& s);
  };

  std::vector<Level> bidLevels;
  std::vector<Level> askLevels;

  void serialize(ISerializer& s);
};

struct BatchedSettlement {
  struct FillEntry {
    Crypto::Hash bidOrderId;
    Crypto::Hash askOrderId;
    uint64_t amount;
    uint64_t price;
    uint32_t bidInputIndex;
    uint32_t askInputIndex;
  };

  std::vector<FillEntry> fills;
  uint64_t totalFee;
  uint64_t perPartyFee;
  uint64_t totalXfgTransferred;
  uint64_t totalHeatTransferred;

  bool isEmpty() const { return fills.empty(); }
  size_t size() const { return fills.size(); }
};

struct PoolOrderParams {
  uint64_t P_clear;
  uint64_t bandPct;
  uint64_t reserveXfg;
  uint64_t reserveHeat;
  uint64_t spreadBps;
};

std::vector<Order> generatePoolOrders(const PoolOrderParams& params);

} // namespace CryptoNote
