// Copyright (c) 2017-2026 Fuego Developers
//
// Fuego is free & open source software distributed in the hope
// that it will be useful, but WITHOUT ANY WARRANTY; without even
// the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. Fuego includes elements written by third parties, some of
// which are licensed under the GNU GPL, version 2 or later. See the
// LICENSE file for details.

#include "AssetType.h"
#include "CryptoNoteConfig.h"

namespace CryptoNote {

// Returns false for any term that is not a recognised output class. Callers must
// reject rather than substitute a default: an unknown term means the asset is
// unknown, and defaulting it to XFG is what let a decoy reorder change the value
// an input was credited.
//
// The CD range is runtime (Currency::depositMinTerm/depositMaxTerm) and differs
// on testnet, so callers pass the bounds rather than this function reading
// constants.
bool classifyCommitmentTermAsset(uint32_t term,
                                 uint32_t cdMinTerm,
                                 uint32_t cdMaxTerm,
                                 AssetType& outAsset) {
    if (term == parameters::HEAT_TERM) { outAsset = AssetType::HEAT; return true; }
    if (term == parameters::DEPOSIT_TERM_LP) { outAsset = AssetType::LP; return true; }
    if (term == parameters::DEPOSIT_TERM_POOL_XFG) { outAsset = AssetType::XFG; return true; }
    if (term == parameters::DEPOSIT_TERM_POOL_HEAT) { outAsset = AssetType::HEAT; return true; }
    if (term == parameters::DEPOSIT_TERM_SWAP_RECEIVE_XFG) { outAsset = AssetType::XFG; return true; }
    if (term >= cdMinTerm && term <= cdMaxTerm) { outAsset = AssetType::HEAT; return true; }
    // DIGM_TERM is deliberately absent: it has no sound per-asset accounting
    // (DigmMintEngine sums commitment inputs as HEAT without knowing the real ring
    // member's asset) and must not be minted. If it ever appears in an output it
    // will be rejected here rather than silently classified as HEAT.
    return false;
}

bool resolveCommitmentRingAsset(const std::vector<uint32_t>& terms,
                                size_t outputIndex,
                                uint32_t cdMinTerm,
                                uint32_t cdMaxTerm,
                                AssetType& outAsset) {
    if (terms.empty() || outputIndex >= terms.size()) {
        return false;
    }
    AssetType ringAsset = AssetType::XFG;
    for (size_t i = 0; i < terms.size(); ++i) {
        AssetType memberAsset = AssetType::XFG;
        if (!classifyCommitmentTermAsset(terms[i], cdMinTerm, cdMaxTerm, memberAsset)) {
            return false;  // unrecognised term anywhere makes the ring unresolvable
        }
        if (i == 0) {
            ringAsset = memberAsset;
        } else if (ringAsset != memberAsset) {
            return false;  // mixed ring
        }
    }
    outAsset = ringAsset;
    return true;
}

} // namespace CryptoNote
