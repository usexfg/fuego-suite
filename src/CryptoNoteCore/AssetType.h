// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See file labeled LICENSE for more details.

#pragma once

#include <cstdint>

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

// One transaction's per-asset flows. `in` and `out` are what its inputs spend
// and its outputs create. Sources are value the protocol pays into the
// transaction with no input behind it (pool and escrow payouts, LP shares,
// minted HEAT); sinks are value that leaves it with no output (pool and escrow
// deposits, burns, treasury funding). Every source and sink comes from the
// transaction's declared settlement amounts (Blockchain::computeAssetFlows).
struct AssetFlows {
    AssetBalance in;
    AssetBalance out;
    uint64_t xfgSource  = 0;
    uint64_t xfgSink    = 0;
    uint64_t heatSource = 0;
    uint64_t heatSink   = 0;
    uint64_t lpSource   = 0;
    uint64_t lpSink     = 0;
};

// v11+ balance rule. The block reward is XFG, so a fee is XFG only: the XFG
// surplus. HEAT and LP must balance exactly — neither can be created from
// nothing, and neither leaves a transaction except through a declared sink.
// Returns false on any imbalance or overflow.
inline bool settleAssetFlows(const AssetFlows& f, uint64_t& xfgFee) {
    auto add = [](uint64_t a, uint64_t b, uint64_t& sum) {
        if (a > UINT64_MAX - b) return false;
        sum = a + b;
        return true;
    };
    uint64_t xIn = 0, xOut = 0, hIn = 0, hOut = 0, lIn = 0, lOut = 0;
    if (!add(f.in.xfg, f.xfgSource, xIn) || !add(f.out.xfg, f.xfgSink, xOut) ||
        !add(f.in.heat, f.heatSource, hIn) || !add(f.out.heat, f.heatSink, hOut) ||
        !add(f.in.lp, f.lpSource, lIn) || !add(f.out.lp, f.lpSink, lOut)) {
        return false;
    }
    if (xIn < xOut || hIn != hOut || lIn != lOut) return false;
    xfgFee = xIn - xOut;
    return true;
}

} // namespace CryptoNote
