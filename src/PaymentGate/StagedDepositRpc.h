// Copyright (c) 2017-2025 Fuego Developers
// Copyright (c) 2018-2019 Conceal Network & Conceal Devs
// Copyright (c) 2014-2017 The XDN developers
// Copyright (c) 2012-2018 The CryptoNote developers
//
// This file is part of Fuego.
//
// Fuego is free & open source software distributed in the hope
// that it will be useful, but WITHOUT ANY WARRANTY; without even
// the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You may redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation. Fuego includes elements written
// by third parties. See file labeled LICENSE for more details.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include "CryptoNoteCore/StagedUnlock.h"
#include "CryptoNoteCore/StagedDepositUnlock.h"
#include "Serialization/ISerializer.h"
#include "IWallet.h"

// Staged deposit info for RPC responses
struct StagedDepositInfo {
    uint64_t depositId;
    uint64_t amount;
    uint32_t term;
    uint64_t interest;
    uint32_t height;
    uint32_t unlockHeight;
    bool locked;
    bool useStagedUnlock;
    // Get all deposits with staged unlock info
    std::vector<CryptoNote::UnlockStage> stages;
    std::string status;
    uint64_t totalUnlockedAmount;
    uint64_t remainingLockedAmount;
    std::string creatingTransactionHash;
    std::string spendingTransactionHash;
    std::string address;

    void serialize(CryptoNote::ISerializer& serializer);
};

namespace PaymentService {

// Get staged unlock schedule for a deposit
struct GetStagedUnlockSchedule {
    struct Request {
        uint64_t depositId;

        void serialize(CryptoNote::ISerializer& serializer);
    };

    struct Response {
        bool useStagedUnlock;
        std::vector<CryptoNote::UnlockStage> stages;
        std::string status;
        uint64_t totalUnlockedAmount;
        uint64_t remainingLockedAmount;

        void serialize(CryptoNote::ISerializer& serializer);
    };
};

// Get all deposits with staged unlock info
struct GetStagedDeposits {
    struct Request {
        void serialize(CryptoNote::ISerializer& serializer);
    };

    struct Response {
        std::vector<StagedDepositInfo> deposits;
        uint64_t totalUnlockedAmount;
        uint64_t totalRemainingLockedAmount;

        void serialize(CryptoNote::ISerializer& serializer);
    };
};



// Process staged unlocks
struct ProcessStagedUnlocks {
    struct Request {
        void serialize(CryptoNote::ISerializer& serializer);
    };

    struct Response {
        std::vector<uint64_t> newlyUnlockedDeposits;
        uint64_t totalUnlockedAmount;
        std::string message;

        void serialize(CryptoNote::ISerializer& serializer);
    };
};

} // namespace PaymentService
