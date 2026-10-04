// Copyright (c) 2017-2026 Fuego Developers

#include "AssetType.h"
#include "CryptoNoteConfig.h"

namespace CryptoNote {

// The single commitment-term -> asset table. Currency::classifyCommitmentTermAsset delegates
// here, so an output and the input that later spends it cannot be classified as different assets.
//
// Total on purpose: there is no "unrecognised term" result. Every finite term is a certificate
// of deposit, and a CD is HEAT. A CD used to fall through to an XFG default, which made
// inAssets.heat != outAssets.heat, so consensus rejected every HEAT CD ever built. Term 0 is
// plain HEAT (check_tx_outputs admits it as such). A ring that mixes asset classes is rejected by
// checkCommitmentSpendInput / checkCommitmentTransferInput, which is what makes classifying an
// input by its first ring member exact.
AssetType classifyCommitmentTermAsset(uint32_t term) {
  if (term == parameters::DEPOSIT_TERM_LP) {
    return AssetType::LP;
  }
  if (term == parameters::DEPOSIT_TERM_POOL_XFG ||
      term == parameters::DEPOSIT_TERM_SWAP_RECEIVE_XFG) {
    return AssetType::XFG;
  }
  return AssetType::HEAT;  // HEAT_TERM, DEPOSIT_TERM_POOL_HEAT, term 0 and every CD term
}

} // namespace CryptoNote
