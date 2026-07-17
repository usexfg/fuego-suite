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
#include <string>
#include <CryptoTypes.h>
#include "CryptoNoteCore/AssetType.h"

namespace CryptoNote {

enum class VaultPartition : uint8_t {
    CD_APY_POOL     = 0,
    LP_RESERVE      = 1,
    GENERAL_RESERVE = 2,
    SWF             = 3,
};

const char* vaultPartitionName(VaultPartition p);

struct VaultOutput {
    uint64_t        globalOutputIndex = 0;
    uint64_t        amount = 0;
    AssetType       asset = AssetType::XFG;
    VaultPartition  partition = VaultPartition::GENERAL_RESERVE;
    Crypto::Hash    txHash;
    Crypto::PublicKey outputKey;
    bool            spent = false;
};

struct VaultBalance {
    uint64_t xfgCdFeePool    = 0;
    uint64_t heatCdFeePool   = 0;
    uint64_t xfgLpReserve    = 0;
    uint64_t heatLpReserve   = 0;
    uint64_t xfgGeneral      = 0;
    uint64_t heatGeneral     = 0;
    uint64_t heatSwf         = 0;
};

} // namespace CryptoNote
