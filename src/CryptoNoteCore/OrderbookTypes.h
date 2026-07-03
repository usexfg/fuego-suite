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
  uint8_t  side;           // 0 = BUY_XFG, 1 = SELL_XFG
  uint64_t amount;         // atomic units (XFG for sells, HEAT for buys)
  uint64_t price;          // XFG/HEAT ratio × COIN
  uint32_t expiration;     // block height for expiry
  Crypto::Hash orderId;    // H(tx_prefix_hash || output_index)
  Crypto::Hash utxoTxHash; // funding UTXO's tx hash
  uint32_t outputIndex;    // funding UTXO's output index
  Crypto::PublicKey spendKey;
  Crypto::PublicKey viewKey;
  std::vector<Crypto::Signature> partialSigs; // pre-signed for any fill amount ≤ amount
};

// Adaptor pre-signature: signer commits to a range [0, amount] that the
// miner can complete with the exact fill. Uses same adaptor-sig technique
// as SwapDaemon (discrete-log relationship) but for order settlement.
// The partial signature covers: "spend UTXO, fill X amount to counterparty
// at P_clear or better, remainder to stealth address."
struct OrderPartialSig {
  Crypto::EllipticCurvePoint nonceCommitment;
  Crypto::EllipticCurveScalar challengeShare;
  Crypto::EllipticCurveScalar responseShare;
};

// Compact orderbook state receipt embedded in settlement blocks.
// Allows chain restart to recover aggregate orderbook state without
// re-gossiping individual orders.
struct OrderbookReceipt {
  uint64_t clearingPrice;          // P_clear × COIN
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

// Batched settlement transaction: one tx per block containing N fills.
// Fee = MINIMUM_FEE / N per party. Miner constructs from matched pairs.
struct BatchedSettlement {
  struct FillEntry {
    Crypto::Hash bidOrderId;
    Crypto::Hash askOrderId;
    uint64_t amount;   // XFG atomic units matched
    uint64_t price;    // match price × COIN
    uint32_t bidInputIndex;   // index in settlement tx inputs
    uint32_t askInputIndex;
  };

  std::vector<FillEntry> fills;
  uint64_t totalFee;           // MINIMUM_FEE (same regardless of fill count)
  uint64_t perPartyFee;        // totalFee / fills.size()

  uint64_t totalXfgTransferred;
  uint64_t totalHeatTransferred;

  bool isEmpty() const { return fills.empty(); }
  size_t size() const { return fills.size(); }
};

// Pool-generated orders: HEARTH pool auto-places limit orders forming
// the floating band around P_clear. Pool uses its reserves as backing.
// Pool orders have last-in-time priority at the same price level (user
// orders fill first).
struct PoolOrderParams {
  uint64_t P_clear;           // current clearing price
  uint64_t bandPct;           // 10% of pool reserves
  uint64_t reserveXfg;
  uint64_t reserveHeat;
  uint64_t spreadBps;         // spread from P_clear for pool orders
};

std::vector<Order> generatePoolOrders(const PoolOrderParams& params);

} // namespace CryptoNote
