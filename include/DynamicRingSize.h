// Copyright (c) 2024 Fuego Developers
// Copyright (c) 2024 Elderfire Privacy Group
// Copyright (c) 2024 Monero Developers
//
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You can redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation. Fuego includes elements written
// by third parties. See file labeled LICENSE for more details.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <algorithm>
#include <memory>
#include "OSPEADDecoySelection.h"
#include "IBlockchainExplorer.h"

// Forward declaration for TransactionOutputInfo (defined in OSPEADDecoySelection.h)
using TransactionOutputInfo = CryptoNote::TransactionOutputInfo;

namespace CryptoNote {

// Structure to hold output info for ring size calculation
struct OutputInfo {
  uint64_t amount;
  size_t availableCount;
  std::string description;
  uint32_t creationHeight; // when output was created (0 if unknown)

  OutputInfo(uint64_t amt, size_t count, const std::string& desc = "", uint32_t height = 0)
    : amount(amt), availableCount(count), description(desc), creationHeight(height) {}
};

// Dynamic ring size calculator
class DynamicRingSizeCalculator {
public:
  // Calculate optimal ring size for a given amount and available outputs
  static size_t calculateOptimalRingSize(
    uint64_t amount,
    const std::vector<OutputInfo>& availableOutputs,
    uint8_t blockMajorVersion,
    size_t minRingSize = 8,
    size_t maxRingSize = 18
  );
  
  // Get target ring sizes in order of preference
  static std::vector<size_t> getTargetRingSizes();
  
  // Check if a ring size is achievable with available outputs
  static bool isRingSizeAchievable(
    size_t ringSize,
    const std::vector<OutputInfo>& availableOutputs
  );

  // Check if a ring size is one of our approved uniform sizes
  static bool isApprovedRingSize(size_t ringSize);
  
  // Get privacy level description for a ring size
  static std::string getPrivacyLevelDescription(size_t ringSize);

  // use OSPEAD principles from Monero for better decoy distribution
  static std::vector<OutputInfo> filterOutputsByOSPEAD(
    const std::vector<OutputInfo>& availableOutputs,
    uint64_t amount,
    uint64_t currentBlockHeight,
    const std::vector<TransactionOutputInfo>& recentTransactions,
    IBlockchainExplorer* blockchainExplorer = nullptr
  );
};

// Privacy levels for different ring sizes
enum class PrivacyLevel {
  MINIMUM = 8,      //   Fuego Standard (minimum of 8)
  SOLID = 10,      //   Solid privacy (10)
  BETTER = 12,    //   Better privacy (12)
  STRONG = 15,    //  Monero's Ring Size (15)
  MAXIMUM = 18    // Fuego Max Privacy (18)
};

} // namespace CryptoNote
