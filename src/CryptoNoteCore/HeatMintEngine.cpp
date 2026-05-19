// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See file labeled LICENSE for more details.

#include "HeatMintEngine.h"
#include "CryptoNoteConfig.h"

namespace CryptoNote {

HeatMintEngine::HeatMintEngine() = default;

bool HeatMintEngine::isHeatMint(const Transaction& tx) const {
  for (const auto& out : tx.outputs) {
    if (out.target.type() == typeid(TransactionOutputCommitment)) {
      const auto& commitment = boost::get<TransactionOutputCommitment>(out.target);
      if (out.amount > 0 && commitment.term == parameters::DEPOSIT_TERM_FOREVER)
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
      xfgInputs += boost::get<KeyInput>(in).amount;
    } else if (in.type() == typeid(TransactionInputCommitmentSpend)) {
      xfgInputs += boost::get<TransactionInputCommitmentSpend>(in).amount;
    }
  }

  for (const auto& out : tx.outputs) {
    if (out.target.type() == typeid(TransactionOutputCommitment)) {
      const auto& commitment = boost::get<TransactionOutputCommitment>(out.target);
      if (commitment.term == parameters::DEPOSIT_TERM_FOREVER) {
        heatOutputs += out.amount;
      } else {
        xfgOutputs += out.amount;
      }
    } else {
      xfgOutputs += out.amount;
    }
  }

  if (heatOutputs == 0)
    return false;

  if (xfgInputs < xfgOutputs + fee)
    return false;

  xfgBurned = xfgInputs - xfgOutputs - fee;

  FixedPoint64 xfgFp = FixedPoint64::fromUint64(xfgBurned);
  FixedPoint64 heatFp = xfgFp.div(redemptionPrice);
  uint64_t expectedHeat = heatFp.toUint64();

  uint64_t delta = (heatOutputs > expectedHeat) ? (heatOutputs - expectedHeat) : (expectedHeat - heatOutputs);
  if (delta > 1)
    return false;

  heatMinted = heatOutputs;
  return true;
}

bool HeatMintEngine::validateMintAuth(const Transaction& tx,
                                       uint64_t fee,
                                       FixedPoint64 redemptionPrice,
                                       uint64_t xfgBurned,
                                       uint64_t heatMinted) const {
  if (redemptionPrice.isZero())
    return false;
  if (xfgBurned == 0 || heatMinted == 0)
    return false;

  FixedPoint64 xfgFp = FixedPoint64::fromUint64(xfgBurned);
  FixedPoint64 heatFp = xfgFp.div(redemptionPrice);
  uint64_t expectedHeat = heatFp.toUint64();

  // Premium allowed: heatMinted can be less than expected (mint premium → burned XFG → treasury).
  // More than expected would be inflation — rejected.
  if (heatMinted > expectedHeat + 1)
    return false;

  return true;
}

} // namespace CryptoNote
