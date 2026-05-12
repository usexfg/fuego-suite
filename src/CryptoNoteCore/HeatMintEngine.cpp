// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See file labeled LICENSE for more details.

#include "HeatMintEngine.h"
#include "CryptoNoteCore/CryptoNoteTools.h"
#include "CryptoNoteCore/AssetId.h"
#include <CryptoNote.h>

namespace CryptoNote {

HeatMintEngine::HeatMintEngine() = default;

bool HeatMintEngine::isHeatMint(const Transaction& tx) const {
  for (const auto& out : tx.outputs) {
    if (out.assetId == static_cast<uint8_t>(AssetId::HEAT)) {
      if (out.amount > 0)
        return true;
    }
    if (out.target.type() == typeid(TransactionOutputCommitment)) {
      const auto& commitment = boost::get<TransactionOutputCommitment>(out.target);
      if (commitment.amount > 0 && commitment.term == 0)
        return true;
    }
  }
  return false;
}

bool HeatMintEngine::validateMint(const Transaction& tx,
                                   uint64_t fee,
                                   FixedPoint64 redemptionPrice,
                                   uint64_t& xfgBurned,
                                   uint64_t& heatMinted) const {
  xfgBurned = 0;
  heatMinted = 0;

  if (redemptionPrice.isZero())
    return false;

  uint64_t xfgInputs  = 0;
  uint64_t xfgOutputs = 0;
  uint64_t heatOutputs = 0;

  for (const auto& in : tx.inputs) {
    if (in.type() == typeid(KeyInput)) {
      const auto& ki = boost::get<KeyInput>(in);
      if (ki.assetId == static_cast<uint8_t>(AssetId::XFG)) {
        xfgInputs += ki.amount;
      } else {
        // Non-XFG input in a HEAT mint — reject
        return false;
      }
    }
    // Other input types (BaseInput, MultisignatureInput) default to XFG
  }

  for (const auto& out : tx.outputs) {
    if (out.target.type() == typeid(TransactionOutputCommitment)) {
      const auto& commitment = boost::get<TransactionOutputCommitment>(out.target);
      if (commitment.term == 0) {
        heatOutputs += commitment.amount;
      } else {
        xfgOutputs += commitment.amount;
      }
    } else if (out.assetId == static_cast<uint8_t>(AssetId::HEAT)) {
      heatOutputs += out.amount;
    } else {
      xfgOutputs += out.amount;
    }
  }

  if (heatOutputs == 0)
    return false; // not a mint tx

  // XFG consumed (burned) = total XFG inputs - XFG change outputs - fee
  if (xfgInputs < xfgOutputs + fee)
    return false; // insufficient XFG to cover change + fee

  xfgBurned = xfgInputs - xfgOutputs - fee;

  // HEAT_minted = xfgBurned / redemptionPrice
  // = xfgBurned * (1 / redemptionPrice)
  // Using FixedPoint64: heat = xfgBurned * ONE / redemptionPrice
  FixedPoint64 xfgFp = FixedPoint64::fromUint64(xfgBurned);
  FixedPoint64 heatFp = xfgFp.div(redemptionPrice);
  uint64_t expectedHeat = heatFp.toUint64();

  // Allow 1 atomic unit tolerance for rounding
  uint64_t delta = (heatOutputs > expectedHeat) ? (heatOutputs - expectedHeat) : (expectedHeat - heatOutputs);
  if (delta > 1)
    return false;

  heatMinted = heatOutputs;
  return true;
}

} // namespace CryptoNote
