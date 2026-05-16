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

} // namespace CryptoNote
