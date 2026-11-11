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
#include "Common/Int128.h"
#include <cstdio>
#include <limits>

namespace CryptoNote {

namespace {

// Every sum below is over amounts the transaction author chose, so each one
// must be checked: a wrapping sum turns an unbacked mint into a "valid" one.
bool addChecked(uint64_t& sum, uint64_t value) {
  if (sum > std::numeric_limits<uint64_t>::max() - value) return false;
  sum += value;
  return true;
}

// expectedHeat = xfgBurned × price / COIN, price = HEAT atomics per XFG atomic × COIN.
uint64_t expectedHeatFor(uint64_t xfgBurned, uint64_t price) {
  return static_cast<uint64_t>(((uint128_t)xfgBurned * price) / parameters::COIN);
}

// Scan a mint tx: sum XFG inputs, XFG outputs, HEAT outputs; verify conservation.
// Returns false on structural failure. Shared by legacy and v12 validation paths.
bool scanMintTx(const Transaction& tx, uint64_t fee,
                uint64_t& xfgInputs, uint64_t& xfgOutputs,
                uint64_t& heatOutputs, uint64_t& xfgBurned) {
  xfgInputs = 0;
  xfgOutputs = 0;
  heatOutputs = 0;

  for (const auto& in : tx.inputs) {
    if (in.type() == typeid(KeyInput)) {
      if (!addChecked(xfgInputs, boost::get<KeyInput>(in).amount)) return false;
    } else if (in.type() == typeid(TransactionInputCommitmentSpend)) {
      if (!addChecked(xfgInputs, boost::get<TransactionInputCommitmentSpend>(in).amount)) return false;
    } else if (in.type() == typeid(TransactionInputCommitmentTransfer)) {
      // CommitmentTransfer inputs are not a valid XFG burn source.
      // Allowing them without counting their value would break supply conservation.
      return false;
    }
  }

  for (const auto& out : tx.outputs) {
    if (out.target.type() == typeid(TransactionOutputCommitment)) {
      const auto& commitment = boost::get<TransactionOutputCommitment>(out.target);
      if (commitment.term == parameters::HEAT_TERM) {
        if (!addChecked(heatOutputs, out.amount)) return false;
      } else {
        if (!addChecked(xfgOutputs, out.amount)) return false;
      }
    } else {
      if (!addChecked(xfgOutputs, out.amount)) return false;
    }
  }

  if (heatOutputs == 0) return false;

  // `xfgOutputs + fee` wraps for a large enough output amount, which makes a
  // transaction spending far more than it brings in look conserved and reports
  // a burn of a few atomics (or zero).
  uint64_t xfgSpent = xfgOutputs;
  if (!addChecked(xfgSpent, fee)) return false;
  if (xfgInputs < xfgSpent) return false;

  xfgBurned = xfgInputs - xfgSpent;
  return true;
}

} // anonymous namespace

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

uint64_t HeatMintEngine::premiumHeatFor(uint64_t xfgBurned, uint64_t price,
                                        uint8_t blockMajorVersion) const {
  if (price == 0) return 0;
  return expectedHeatFor(
      heatMintPremiumXfg(xfgBurned, blockMajorVersion), price);
}

bool HeatMintEngine::validateMint(const Transaction& tx,
                                    uint64_t fee,
                                    uint64_t price,
                                    uint64_t& xfgBurned,
                                    uint64_t& heatMinted,
                                    uint8_t blockMajorVersion,
                                    uint64_t& premiumHeat) const {
  xfgBurned = 0;
  heatMinted = 0;
  premiumHeat = 0;

  if (price == 0) {
#ifdef HEAT_MINT_DEBUG
    fprintf(stderr, "[HeatMint] validateMint FAIL: price is zero\n");
#endif
    return false;
  }

  uint64_t xfgInputs = 0, xfgOutputs = 0, heatOutputs = 0;
  if (!scanMintTx(tx, fee, xfgInputs, xfgOutputs, heatOutputs, xfgBurned)) {
#ifdef HEAT_MINT_DEBUG
    fprintf(stderr, "[HeatMint] validateMint FAIL: structural (inputs=%llu outputs=%llu heat=%llu fee=%llu)\n",
      (unsigned long long)xfgInputs, (unsigned long long)xfgOutputs,
      (unsigned long long)heatOutputs, (unsigned long long)fee);
#endif
    return false;
  }

  const uint64_t expectedHeat = expectedHeatFor(xfgBurned, price);
  premiumHeat = premiumHeatFor(xfgBurned, price, blockMajorVersion);
  // The premium is a fraction of the burn, so it can never exceed the HEAT the
  // whole burn buys. Guard anyway: a wrapped subtraction here would hand the
  // minter an unbounded cap.
  if (premiumHeat > expectedHeat) return false;
  const uint64_t userCap = expectedHeat - premiumHeat;

#ifdef HEAT_MINT_DEBUG
  fprintf(stderr, "[HeatMint] validateMint: xfgBurned=%llu expectedHeat=%llu premiumHeat=%llu price=%llu heatOutputs=%llu\n",
    (unsigned long long)xfgBurned, (unsigned long long)expectedHeat,
    (unsigned long long)premiumHeat, (unsigned long long)price,
    (unsigned long long)heatOutputs);
#endif

  if (heatOutputs > userCap) return false;

  heatMinted = heatOutputs;
  return true;
}

bool HeatMintEngine::validateMintAuth(const Transaction& tx,
                                        uint64_t fee,
                                        uint64_t price,
                                        uint64_t xfgBurned,
                                        uint64_t heatMinted,
                                        uint8_t blockMajorVersion,
                                        uint64_t& premiumHeat) const {
  premiumHeat = 0;
  if (price == 0) return false;
  if (xfgBurned == 0 || heatMinted == 0) return false;

  uint64_t actualXfgBurned = 0, actualHeatMinted = 0;
  if (!validateMint(tx, fee, price, actualXfgBurned, actualHeatMinted,
                    blockMajorVersion, premiumHeat)) return false;

  if (actualXfgBurned < xfgBurned) return false;
  if (actualHeatMinted != heatMinted) return false;

  const uint64_t expectedHeat = expectedHeatFor(actualXfgBurned, price);
  if (premiumHeat > expectedHeat) return false;
  if (heatMinted > expectedHeat - premiumHeat) return false;

  return true;
}

// ── Legacy pre-v12 paths (Q64.64 XFG-per-HEAT) — bit-identical to original ──

bool HeatMintEngine::validateMint(const Transaction& tx,
                                    uint64_t fee,
                                    FixedPoint64 redemptionPrice,
                                    uint64_t& xfgBurned,
                                    uint64_t& heatMinted) const {
  xfgBurned = 0;
  heatMinted = 0;

  if (redemptionPrice.isZero()) {
#ifdef HEAT_MINT_DEBUG
    fprintf(stderr, "[HeatMint] validateMint FAIL: redemptionPrice is zero\n");
#endif
    return false;
  }

  uint64_t xfgInputs = 0, xfgOutputs = 0, heatOutputs = 0;
  if (!scanMintTx(tx, fee, xfgInputs, xfgOutputs, heatOutputs, xfgBurned)) {
    return false;
  }

  FixedPoint64 xfgFp = FixedPoint64::fromUint64(xfgBurned);
  FixedPoint64 heatFp = xfgFp.div(redemptionPrice);
  uint64_t expectedHeat = heatFp.toUint64();

  if (heatOutputs > expectedHeat) return false;

  heatMinted = heatOutputs;
  return true;
}

bool HeatMintEngine::validateMintAuth(const Transaction& tx,
                                        uint64_t fee,
                                        FixedPoint64 redemptionPrice,
                                        uint64_t xfgBurned,
                                        uint64_t heatMinted) const {
  if (redemptionPrice.isZero()) return false;
  if (xfgBurned == 0 || heatMinted == 0) return false;

  uint64_t actualXfgBurned = 0, actualHeatMinted = 0;
  if (!validateMint(tx, fee, redemptionPrice, actualXfgBurned, actualHeatMinted)) return false;

  if (actualXfgBurned < xfgBurned) return false;
  if (actualHeatMinted != heatMinted) return false;

  FixedPoint64 xfgFp = FixedPoint64::fromUint64(actualXfgBurned);
  uint64_t expectedHeat = xfgFp.div(redemptionPrice).toUint64();
  if (heatMinted > expectedHeat) return false;

  return true;
}

} // namespace CryptoNote
