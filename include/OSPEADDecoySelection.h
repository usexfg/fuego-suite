// Copyright (c) 2017-2025 Fuego Developers
// Copyright (c) 2020-2025 Elderfire Privacy Group
// Copyright (c) 2014-2018 The Monero project
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
#include <map>

namespace CryptoNote {

// Structure to hold transaction output information for analysis
struct TransactionOutputInfo {
  uint64_t amount;
  uint64_t creationHeight;  
  uint64_t globalIndex;

  TransactionOutputInfo(uint64_t amt, uint64_t height, uint64_t index)
    : amount(amt), creationHeight(height), globalIndex(index) {}
};

// OSPEAD-inspired structure for output binning based on age
struct OutputAgeBin {
  uint64_t minAge;        // Minimum age in blocks
  uint64_t maxAge;        // Maximum age in blocks
  size_t outputCount;     // Number of outputs in this age range
  double spendProbability; // Probability of being spent from this bin

  OutputAgeBin(uint64_t min, uint64_t max, size_t count, double prob)
    : minAge(min), maxAge(max), outputCount(count), spendProbability(prob) {}
};

// Decoy selection based on principles of Monero's OSPEAD
class OSPEADDecoySelector {
public:
  // Analyze spend patterns and create output age bins
  static std::vector<OutputAgeBin> analyzeSpendPatterns(
    const std::vector<TransactionOutputInfo>& recentTransactions,
    uint64_t currentBlockHeight,
    size_t numBins = 10
  );

  // Select decoys that match real spend probability distribution
  static std::vector<uint32_t> selectOptimalDecoys(
    uint64_t amount,
    const std::vector<OutputAgeBin>& ageBins,
    size_t requiredRingSize,
    uint64_t currentBlockHeight,
    const std::vector<uint32_t>& availableOutputs
  );

  // Calculate spend probability for a given output age
  static double calculateSpendProbability(
    uint64_t outputAge,
    uint64_t currentBlockHeight,
    const std::vector<OutputAgeBin>& spendPattern
  );

  // Filter out coinbase outputs from decoy selection
  static std::vector<uint32_t> filterNonCoinbaseOutputs(
    const std::vector<uint32_t>& candidateOutputs,
    const std::map<uint32_t, bool>& isCoinbaseMap
  );

  // Create spend pattern from historical transaction data
  static std::vector<OutputAgeBin> createSpendPatternFromHistory(
    const std::vector<TransactionOutputInfo>& spentOutputs,
    uint64_t currentBlockHeight,
    size_t numBins = 10
  );

private:
  // Helper function to create age bins with logarithmic distribution
  static std::vector<OutputAgeBin> createLogarithmicAgeBins(
    uint64_t maxAge,
    size_t numBins
  );

  // Normalize probabilities to sum to 1.0
  static void normalizeProbabilities(std::vector<OutputAgeBin>& bins);
};

// Spend pattern analyzer for continuous learning
class SpendPatternAnalyzer {
public:
  // Update spend pattern based on new transactions
  void updatePattern(const std::vector<TransactionOutputInfo>& newTransactions);

  // Get current spend pattern
  std::vector<OutputAgeBin> getCurrentPattern() const { return currentPattern_; }

  // Save/load pattern to/from persistent storage
  bool savePattern(const std::string& filePath) const;
  bool loadPattern(const std::string& filePath);

private:
  std::vector<OutputAgeBin> currentPattern_;
  std::vector<TransactionOutputInfo> recentTransactions_;
  static const size_t MAX_HISTORY_SIZE = 100000; // Keep last 100k transactions
};

} // namespace CryptoNote
