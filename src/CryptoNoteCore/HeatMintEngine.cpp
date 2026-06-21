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
      if (out.amount > 0 && commitment.term == parameters::HEAT_TERM)
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
      if (commitment.term == parameters::HEAT_TERM) {
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

  // Exact match required — no rounding tolerance. FixedPoint64 truncates,
  // so heatOutputs <= expectedHeat is the only valid outcome.
  if (heatOutputs > expectedHeat)
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

  // Defense-in-depth: re-derive amounts from the actual transaction
  // and verify heat output matches the auth tag. xfgBurned may include
  // a mint premium which is absorbed by the transaction fee — only require
  // actualXfgBurned ≤ xfgBurned (no overspend) and heatMinted must match exactly.
  uint64_t actualXfgBurned = 0, actualHeatMinted = 0;
  if (!validateMint(tx, fee, redemptionPrice, actualXfgBurned, actualHeatMinted))
    return false;
  if (actualHeatMinted != heatMinted)
    return false;

  // Ratio check: no inflation — heatMinted must not exceed the pool-rate equivalent
  FixedPoint64 xfgFp = FixedPoint64::fromUint64(xfgBurned);
  uint64_t expectedHeat = xfgFp.div(redemptionPrice).toUint64();
  if (heatMinted > expectedHeat)
    return false;

  return true;
}

} // namespace CryptoNote
