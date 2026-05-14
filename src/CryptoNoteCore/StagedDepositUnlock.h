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
#include <chrono>
#include "CryptoNote.h"
#include "Serialization/ISerializer.h"
#include "IWallet.h"

namespace CryptoNote {

// Forward declaration
struct StagedUnlock;

// Staged unlock configuration
namespace StagedUnlockConfig {
    static const uint32_t STAGE_INTERVAL_BLOCKS = 18 * 24 * 60; // 18 days in blocks (assuming 1 block per minute)
    static const uint32_t TOTAL_STAGES = 5;
    static const uint32_t STAGE_1_UNLOCK_PERCENT = 20; // 1/5
    static const uint32_t STAGE_2_UNLOCK_PERCENT = 20; // 1/5
    static const uint32_t STAGE_3_UNLOCK_PERCENT = 20; // 1/5
    static const uint32_t STAGE_4_UNLOCK_PERCENT = 20; // 1/5
    static const uint32_t STAGE_5_UNLOCK_PERCENT = 20; // 1/5 (remaining principal)
    static const uint32_t STAGE_1_INTEREST_PERCENT = 0; // Interest paid off-chain at deposit time, on-chain interest always 0
}

// Individual unlock stage information
struct UnlockStage {
    uint32_t stageNumber;
    uint32_t unlockHeight;
    uint64_t principalAmount;
    uint64_t interestAmount;
    bool isUnlocked;
    uint64_t unlockTimestamp;
    
    UnlockStage() : stageNumber(0), unlockHeight(0), principalAmount(0), 
                   interestAmount(0), isUnlocked(false), unlockTimestamp(0) {}
    
    UnlockStage(uint32_t stage, uint32_t height, uint64_t principal, uint64_t interest)
        : stageNumber(stage), unlockHeight(height), principalAmount(principal),
          interestAmount(interest), isUnlocked(false), unlockTimestamp(0) {}
    
    void serialize(ISerializer& s);
};

// Staged unlock manager for individual deposits
class StagedDepositUnlock {
public:
    StagedDepositUnlock();
    StagedDepositUnlock(uint64_t totalAmount, uint64_t totalInterest, uint32_t depositHeight);
    
    // Initialize staged unlock schedule
    void initializeStagedUnlock(uint64_t totalAmount, uint64_t totalInterest, uint32_t depositHeight);
    
    // Check if any stages can be unlocked at current height
    std::vector<UnlockStage> checkUnlockStages(uint32_t currentHeight);
    
    // Get total unlocked amount so far
    uint64_t getTotalUnlockedAmount() const;
    
    // Get remaining locked amount
    uint64_t getRemainingLockedAmount() const;
    
    // Get next unlock stage info
    UnlockStage getNextUnlockStage(uint32_t currentHeight) const;
    
    // Check if all stages are unlocked
    bool isFullyUnlocked() const;
    
    // Get all stages
    const std::vector<UnlockStage>& getStages() const { return m_stages; }
    
    // Serialization
    void serialize(ISerializer& s);
    
private:
    std::vector<UnlockStage> m_stages;
    uint64_t m_totalAmount;
    uint64_t m_totalInterest;
    uint32_t m_depositHeight;
    bool m_initialized;
    
    void calculateStages();
};

// Global staged unlock manager
class StagedUnlockManager {
public:
    // Process all deposits for potential unlocks at current height
    static std::vector<DepositId> processStagedUnlocks(uint32_t currentHeight, 
                                                      const std::vector<Deposit>& deposits);
    
    // Create staged unlock for a new deposit
    static StagedDepositUnlock createStagedUnlock(uint64_t amount, uint64_t interest, uint32_t height);
    
    // Check if deposit should use staged unlocking
    static bool shouldUseStagedUnlock(uint32_t term);
    
    // Get unlock schedule for a deposit
    static std::vector<UnlockStage> getUnlockSchedule(uint64_t amount, uint64_t interest, uint32_t height);
    
    // Convert traditional deposits to staged unlocks
    static std::vector<StagedUnlock> convertDeposits(const std::vector<Deposit>& deposits);
    
    // Process all staged unlocks for potential unlocks at current height
    static std::vector<DepositId> processAllUnlocks(uint32_t currentHeight, 
                                                   const std::vector<StagedUnlock>& deposits);
    
    // Get unlock status string for a deposit
    static std::string getUnlockStatus(const StagedUnlock& deposit, uint32_t currentHeight);
    
    // Get total unlocked amount from all deposits
    static uint64_t getTotalUnlockedAmount(const std::vector<StagedUnlock>& deposits);
    
    // Get total remaining locked amount from all deposits
    static uint64_t getTotalRemainingLockedAmount(const std::vector<StagedUnlock>& deposits);
};

} // namespace CryptoNote