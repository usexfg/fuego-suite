// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See file labeled LICENSE for more details.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace CryptoNote {

enum class AssetType : uint8_t {
    XFG  = 0,
    HEAT = 1,
    LP   = 2,
};

struct AssetBalance {
    uint64_t xfg  = 0;
    uint64_t heat = 0;
    uint64_t lp   = 0;
};

// Canonical commitment-term -> asset mapping. Single source of truth for BOTH the
// input side (Blockchain::classifyInputAsset) and the output side
// (Currency::classifyOutputAsset), so an output and the input that later spends
// it can never be classified as different assets.
//
// Returns false for any unrecognised term, including term 0. Callers must reject
// rather than substitute a default: an unknown term means the asset is unknown.
//
// term 0 is NOT a valid commitment output term. No producer emits it — every
// creation site assigns HEAT_TERM, a validated CD term, or a protocol marker —
// and it previously classified as HEAT on the output side but XFG on the input
// side, which broke per-asset conservation for any output of that term. The
// "unlocked commitment output" case is already HEAT_TERM.
//
// The CD range is runtime and differs on testnet, so the caller supplies it.
bool classifyCommitmentTermAsset(uint32_t term,
                                 uint32_t cdMinTerm,
                                 uint32_t cdMaxTerm,
                                 AssetType& outAsset);

// The all-members-same-asset rule for a commitment-spend ring, as a pure
// function so it is testable without a Blockchain instance.
//
// `terms` are the terms of every ring member, in ring order (decoy order is
// attacker-chosen). `outputIndex` selects which member of `terms` is being
// resolved, so the rule can be asserted to be independent of that choice.
//
// Returns false when the ring cannot be resolved to exactly one asset: a member
// whose term is not a recognised output class, or two members that disagree.
// Callers must not substitute a default — an unresolvable ring means the input's
// asset is unknown.
bool resolveCommitmentRingAsset(const std::vector<uint32_t>& terms,
                                size_t outputIndex,
                                uint32_t cdMinTerm,
                                uint32_t cdMaxTerm,
                                AssetType& outAsset);

} // namespace CryptoNote
