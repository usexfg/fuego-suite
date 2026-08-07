// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
// of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See file labeled LICENSE for more details.

#include "HeatMintEngine.h"
#include "CryptoNoteConfig.h"
#include <cstdio>

namespace CryptoNote {

HeatMintEngine::HeatMintEngine() {
  setbuf(stderr, NULL);
}

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

  if (redemptionPrice.isZero()) {
    fprintf(stderr, "[HeatMint] validateMint FAIL: redemptionPrice is zero\n");
    return false;
  }

  uint64_t xfgInputs  = 0;
  uint64_t xfgOutputs = 0;
  uint64_t heatOutputs = 0;
  size_t inputCount = tx.inputs.size();
  size_t outputCount = tx.outputs.size();
  size_t heatOutputCount = 0;

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
      fprintf(stderr, "[HeatMint]   output[%zu] Commitment term=%u amount=%llu\n",
        &out - &tx.outputs[0], commitment.term, (unsigned long long)out.amount);
      if (commitment.term == parameters::HEAT_TERM) {
        heatOutputs += out.amount;
        ++heatOutputCount;
      } else {
        xfgOutputs += out.amount;
      }
    } else {
      fprintf(stderr, "[HeatMint]   output[%zu] type=%s amount=%llu\n",
        &out - &tx.outputs[0], out.target.type().name(), (unsigned long long)out.amount);
      xfgOutputs += out.amount;
    }
  }

  if (heatOutputs == 0) {
    fprintf(stderr, "[HeatMint] validateMint FAIL: heatOutputs=0, inputs=%zu outputs=%zu xfgInputs=%llu xfgOutputs=%llu HEAT_TERM=0x%x\n",
      inputCount, outputCount, (unsigned long long)xfgInputs, (unsigned long long)xfgOutputs, parameters::HEAT_TERM);
    return false;
  }

  if (xfgInputs < xfgOutputs + fee) {
    fprintf(stderr, "[HeatMint] validateMint FAIL: xfgInputs(%llu) < xfgOutputs(%llu) + fee(%llu)\n",
      (unsigned long long)xfgInputs, (unsigned long long)xfgOutputs, (unsigned long long)fee);
    return false;
  }

  xfgBurned = xfgInputs - xfgOutputs - fee;

  FixedPoint64 xfgFp = FixedPoint64::fromUint64(xfgBurned);
  FixedPoint64 heatFp = xfgFp.div(redemptionPrice);
  uint64_t expectedHeat = heatFp.toUint64();

  fprintf(stderr, "[HeatMint] validateMint: xfgInputs=%llu xfgOutputs=%llu heatOutputs=%llu heatOutputCount=%zu fee=%llu xfgBurned=%llu expectedHeat=%llu price.raw=%lld\n",
    (unsigned long long)xfgInputs, (unsigned long long)xfgOutputs, (unsigned long long)heatOutputs,
    heatOutputCount, (unsigned long long)fee, (unsigned long long)xfgBurned,
    (unsigned long long)expectedHeat, (long long)redemptionPrice.raw());

  if (heatOutputs > expectedHeat) {
    fprintf(stderr, "[HeatMint] validateMint FAIL: heatOutputs(%llu) > expectedHeat(%llu)\n",
      (unsigned long long)heatOutputs, (unsigned long long)expectedHeat);
    return false;
  }

  heatMinted = heatOutputs;
  return true;
}

bool HeatMintEngine::validateMintAuth(const Transaction& tx,
                                        uint64_t fee,
                                        FixedPoint64 redemptionPrice,
                                        uint64_t xfgBurned,
                                        uint64_t heatMinted) const {
  fprintf(stderr, "[HeatMint] validateMintAuth called: fee=%llu xfgBurned=%llu heatMinted=%llu price.raw=%lld\n",
    (unsigned long long)fee, (unsigned long long)xfgBurned, (unsigned long long)heatMinted, (long long)redemptionPrice.raw());

  if (redemptionPrice.isZero()) {
    fprintf(stderr, "[HeatMint] validateMintAuth FAIL: redemptionPrice is zero\n");
    return false;
  }
  if (xfgBurned == 0 || heatMinted == 0) {
    fprintf(stderr, "[HeatMint] validateMintAuth FAIL: xfgBurned=%llu heatMinted=%llu\n",
      (unsigned long long)xfgBurned, (unsigned long long)heatMinted);
    return false;
  }

  uint64_t actualXfgBurned = 0, actualHeatMinted = 0;
  if (!validateMint(tx, fee, redemptionPrice, actualXfgBurned, actualHeatMinted)) {
    fprintf(stderr, "[HeatMint] validateMintAuth FAIL: validateMint returned false\n");
    return false;
  }

  fprintf(stderr, "[HeatMint] validateMintAuth: declared xfgBurned=%llu heatMinted=%llu actual xfgBurned=%llu heatMinted=%llu\n",
    (unsigned long long)xfgBurned, (unsigned long long)heatMinted,
    (unsigned long long)actualXfgBurned, (unsigned long long)actualHeatMinted);

  if (actualXfgBurned < xfgBurned) {
    fprintf(stderr, "[HeatMint] validateMintAuth FAIL: actualXfgBurned(%llu) < declared(%llu)\n",
      (unsigned long long)actualXfgBurned, (unsigned long long)xfgBurned);
    return false;
  }

  if (actualHeatMinted != heatMinted) {
    fprintf(stderr, "[HeatMint] validateMintAuth FAIL: actualHeatMinted(%llu) != declared(%llu)\n",
      (unsigned long long)actualHeatMinted, (unsigned long long)heatMinted);
    return false;
  }

  FixedPoint64 xfgFp = FixedPoint64::fromUint64(actualXfgBurned);
  uint64_t expectedHeat = xfgFp.div(redemptionPrice).toUint64();
  if (heatMinted > expectedHeat) {
    fprintf(stderr, "[HeatMint] validateMintAuth FAIL: heatMinted(%llu) > expectedHeat(%llu) price.raw=%lld\n",
      (unsigned long long)heatMinted, (unsigned long long)expectedHeat, (long long)redemptionPrice.raw());
    return false;
  }

  fprintf(stderr, "[HeatMint] validateMintAuth PASS\n");
  return true;
}

} // namespace CryptoNote
