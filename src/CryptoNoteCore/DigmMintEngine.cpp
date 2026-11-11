// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even
// the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See file labeled LICENSE for more details.

#include "DigmMintEngine.h"
#include "CryptoNoteConfig.h"
#include "Common/Int128.h"

namespace CryptoNote {

DigmMintEngine::DigmMintEngine() = default;

bool DigmMintEngine::isDigmMint(const Transaction& tx) const {
  for (const auto& out : tx.outputs) {
    if (out.target.type() == typeid(TransactionOutputCommitment)) {
      const auto& commitment = boost::get<TransactionOutputCommitment>(out.target);
      if (out.amount > 0 && commitment.term == parameters::DIGM_TERM)
        return true;
    }
  }
  return false;
}

bool DigmMintEngine::validateMint(const Transaction& tx,
                                    uint64_t fee,
                                    uint64_t& heatLocked,
                                    uint64_t& digmMinted) const {
  heatLocked = 0;
  digmMinted = 0;

  uint64_t heatInputs = 0;
  uint64_t digmOutputs = 0;

  // Sum HEAT inputs (commitment spends with HEAT_TERM)
  for (const auto& in : tx.inputs) {
    if (in.type() == typeid(TransactionInputCommitmentSpend)) {
      const auto& spend = boost::get<TransactionInputCommitmentSpend>(in);
      // Note: We check term in the calling code since spend doesn't carry term
      // Here we assume commitment spends are validated elsewhere
      heatInputs += spend.amount;
    }
  }

  // Sum DIGM outputs (commitments with DIGM_TERM)
  for (const auto& out : tx.outputs) {
    if (out.target.type() == typeid(TransactionOutputCommitment)) {
      const auto& commitment = boost::get<TransactionOutputCommitment>(out.target);
      if (commitment.term == parameters::DIGM_TERM) {
        digmOutputs += out.amount;
      }
    }
  }

  // Must have DIGM outputs
  if (digmOutputs == 0)
    return false;

  // HEAT input must cover DIGM output * peg rate
  // 1 DIGM = DIGM_PEG_HEAT_ATOMIC (0.10 HEAT in atomic units)
  uint128_t requiredHeat = (uint128_t)digmOutputs * parameters::DIGM_PEG_HEAT_ATOMIC / parameters::COIN;
  if (heatInputs < (uint64_t)requiredHeat + fee)
    return false;

  heatLocked = (uint64_t)requiredHeat;
  digmMinted = digmOutputs;

  return true;
}

} // namespace CryptoNote
