// Copyright (c) 2017-2025 Elderfire Privacy Council
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

#include <vector>
#include <cstdint>
#include "CryptoNote.h"
#include "StagedDepositUnlock.h"
#include "IWallet.h"
#include "Serialization/ISerializer.h"

// Forward declarations for types not defined in main headers
namespace CryptoNote {
    typedef size_t TransactionId;
    typedef size_t DepositId;
}

namespace CryptoNote {

// StagedUnlock structure with staged unlocking support
struct StagedUnlock {
    // Basic deposit fields (existing)
    uint64_t amount;
    uint32_t term;
    uint64_t interest;
    uint32_t height;
    uint32_t unlockHeight;
    bool locked;
    TransactionId creatingTransactionId;
    TransactionId spendingTransactionId;
    
    // Staged unlocking support
    bool useStagedUnlock;
    StagedDepositUnlock stagedUnlock;
    uint64_t totalUnlockedAmount;
    uint64_t remainingLockedAmount;
    
    // Constructor
    StagedUnlock() : amount(0), term(0), interest(0), height(0), unlockHeight(0), 
                    locked(true), creatingTransactionId(0), spendingTransactionId(0),
                    useStagedUnlock(false), totalUnlockedAmount(0), remainingLockedAmount(0) {}
    
    // Initialize from basic deposit
    void initializeFromDeposit(const Deposit& deposit);
    
    // Check if deposit can be unlocked (either traditional or staged)
    bool canUnlock(uint32_t currentHeight) const;
    
    // Get unlockable amount at current height
    uint64_t getUnlockableAmount(uint32_t currentHeight) const;
    
    // Process unlock at current height
    std::vector<UnlockStage> processUnlock(uint32_t currentHeight);
    
    // Check if fully unlocked
    bool isFullyUnlocked() const;
    
    // Get next unlock info
    UnlockStage getNextUnlockInfo(uint32_t currentHeight) const;
    
    // Serialization
    void serialize(ISerializer& s);
};



} // namespace CryptoNote