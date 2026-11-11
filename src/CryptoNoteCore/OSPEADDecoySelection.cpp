// Copyright (c) 2017-2025 Fuego Developers
// Copyright (c) 2014-2025 The Monero project
// Copyright (c) 2020-2025 Elderfire Privacy Group
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


#include "OSPEADDecoySelection.h"
#include "../CryptoNoteConfig.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <fstream>

extern "C" {
#include "../crypto/random.h"
}

namespace {
  // CSPRNG-backed uniform random in [0, max). Rejection sampling to avoid modulo bias.
  uint32_t csprng_uniform(uint32_t max) {
    if (max <= 1) return 0;
    uint32_t threshold = (~max + 1) % max; // 2^32 - max, mod max
    uint32_t r;
    do {
      generate_random_bytes(sizeof(r), &r);
    } while (r < threshold);
    return r % max;
  }
}

namespace CryptoNote {

std::vector<OutputAgeBin> OSPEADDecoySelector::analyzeSpendPatterns(
  const std::vector<TransactionOutputInfo>& recentTransactions,
  uint64_t currentBlockHeight,
  size_t numBins
) {
  if (recentTransactions.empty()) {
    return createLogarithmicAgeBins(currentBlockHeight, numBins);
  }

  // Calculate ages of all outputs
  std::vector<uint64_t> ages;
  for (const auto& tx : recentTransactions) {
    uint64_t age = currentBlockHeight - tx.creationHeight;
    if (age > 0) {
      ages.push_back(age);
    }
  }

  if (ages.empty()) {
    return createLogarithmicAgeBins(currentBlockHeight, numBins);
  }

  // Sort ages
  std::sort(ages.begin(), ages.end());

  // Create logarithmic bins
  auto bins = createLogarithmicAgeBins(currentBlockHeight, numBins);

  // Count outputs in each bin
  for (uint64_t age : ages) {
    for (auto& bin : bins) {
      if (age >= bin.minAge && age <= bin.maxAge) {
        bin.outputCount++;
        break;
      }
    }
  }

  // Calculate spend probabilities (inverse of age distribution)
  for (auto& bin : bins) {
    if (bin.outputCount > 0) {
      // Higher age = lower probability (older outputs less likely to be spent)
      double ageFactor = 1.0 / std::log(1.0 + bin.maxAge);
      bin.spendProbability = bin.outputCount * ageFactor;
    }
  }

  normalizeProbabilities(bins);
  return bins;
}

std::vector<uint32_t> OSPEADDecoySelector::selectOptimalDecoys(
  uint64_t amount,
  const std::vector<OutputAgeBin>& ageBins,
  size_t requiredRingSize,
  uint64_t currentBlockHeight,
  const std::vector<uint32_t>& availableOutputs
) {
  std::vector<uint32_t> selectedDecoys;
  if (availableOutputs.size() < requiredRingSize) {
    return selectedDecoys; // Not enough outputs available
  }

  // Use CSPRNG-backed weighted random selection based on spend probabilities.
  // Build cumulative distribution from bin probabilities for rejection-free sampling.
  std::vector<double> cumWeights;
  cumWeights.reserve(ageBins.size());
  double cumSum = 0.0;
  for (const auto& bin : ageBins) {
    cumSum += bin.spendProbability;
    cumWeights.push_back(cumSum);
  }

  // Select decoys using spend pattern distribution
  std::vector<bool> used(availableOutputs.size(), false);

  for (size_t i = 0; i < requiredRingSize - 1; ++i) { // -1 because real output is already selected
    size_t attempts = 0;
    const size_t maxAttempts = 100;

    while (attempts < maxAttempts) {
      // CSPRNG-backed weighted bin selection via cumulative distribution
      uint32_t rval;
      generate_random_bytes(sizeof(rval), &rval);
      double sample = static_cast<double>(rval) / static_cast<double>(UINT32_MAX) * cumSum;
      size_t binIndex = 0;
      for (size_t b = 0; b < cumWeights.size(); ++b) {
        if (sample <= cumWeights[b]) { binIndex = b; break; }
      }
      const auto& bin = ageBins[binIndex];

      // Find unused outputs in this age bin.
      // availableOutputs contains global output indices (uint32_t).
      // Age is approximated from the index relative to current height.
      std::vector<size_t> candidates;
      for (size_t j = 0; j < availableOutputs.size(); ++j) {
        if (!used[j]) {
          // Global index serves as a height proxy — outputs created at lower
          // indices are older.  This is an approximation; the OSPEAD pattern
          // analysis compensates for imprecision.
          uint64_t outputAge = (availableOutputs[j] < currentBlockHeight)
            ? (currentBlockHeight - availableOutputs[j]) : 0;
          if (outputAge >= bin.minAge && outputAge <= bin.maxAge) {
            candidates.push_back(j);
          }
        }
      }

      if (!candidates.empty()) {
        size_t selectedIndex = candidates[csprng_uniform(static_cast<uint32_t>(candidates.size()))];
        selectedDecoys.push_back(availableOutputs[selectedIndex]);
        used[selectedIndex] = true;
        break;
      }

      attempts++;
    }

    if (attempts >= maxAttempts) {
      // Fallback: select any unused output
      for (size_t j = 0; j < availableOutputs.size(); ++j) {
        if (!used[j]) {
          selectedDecoys.push_back(availableOutputs[j]);
          used[j] = true;
          break;
        }
      }
    }
  }

  return selectedDecoys;
}

double OSPEADDecoySelector::calculateSpendProbability(
  uint64_t outputAge,
  uint64_t currentBlockHeight,
  const std::vector<OutputAgeBin>& spendPattern
) {
  for (const auto& bin : spendPattern) {
    if (outputAge >= bin.minAge && outputAge <= bin.maxAge) {
      return bin.spendProbability;
    }
  }
  return 0.0; // Output age outside of pattern range
}

std::vector<uint32_t> OSPEADDecoySelector::filterNonCoinbaseOutputs(
  const std::vector<uint32_t>& candidateOutputs,
  const std::map<uint32_t, bool>& isCoinbaseMap
) {
  std::vector<uint32_t> filtered;
  for (uint32_t output : candidateOutputs) {
    auto it = isCoinbaseMap.find(output);
    if (it == isCoinbaseMap.end() || !it->second) {
      filtered.push_back(output);
    }
  }
  return filtered;
}

std::vector<OutputAgeBin> OSPEADDecoySelector::createSpendPatternFromHistory(
  const std::vector<TransactionOutputInfo>& spentOutputs,
  uint64_t currentBlockHeight,
  size_t numBins
) {
  std::vector<uint64_t> ages;
  for (const auto& output : spentOutputs) {
    uint64_t age = currentBlockHeight - output.creationHeight;
    if (age > 0) {
      ages.push_back(age);
    }
  }

  if (ages.empty()) {
    return createLogarithmicAgeBins(currentBlockHeight, numBins);
  }

  std::sort(ages.begin(), ages.end());
  auto bins = createLogarithmicAgeBins(currentBlockHeight, numBins);

  // Count spent outputs in each bin
  for (uint64_t age : ages) {
    for (auto& bin : bins) {
      if (age >= bin.minAge && age <= bin.maxAge) {
        bin.outputCount++;
        break;
      }
    }
  }

  // Calculate spend probabilities (normalize by bin width and count)
  for (auto& bin : bins) {
    if (bin.outputCount > 0) {
      double binWidth = bin.maxAge - bin.minAge + 1;
      bin.spendProbability = bin.outputCount / binWidth;
    }
  }

  normalizeProbabilities(bins);
  return bins;
}

std::vector<OutputAgeBin> OSPEADDecoySelector::createLogarithmicAgeBins(
  uint64_t maxAge,
  size_t numBins
) {
  std::vector<OutputAgeBin> bins;
  if (maxAge == 0) {
    return bins;
  }

  // Create logarithmically spaced bins
  double logMax = std::log(maxAge + 1);
  double logMin = 0;

  for (size_t i = 0; i < numBins; ++i) {
    double ratio1 = static_cast<double>(i) / numBins;
    double ratio2 = static_cast<double>(i + 1) / numBins;

    uint64_t minAge = static_cast<uint64_t>(std::exp(logMin + ratio1 * (logMax - logMin))) - 1;
    uint64_t maxAge = static_cast<uint64_t>(std::exp(logMin + ratio2 * (logMax - logMin))) - 1;

    // Ensure minimum age of 1 block
    minAge = std::max(minAge, static_cast<uint64_t>(1));

    bins.emplace_back(minAge, maxAge, 0, 0.0);
  }

  return bins;
}

void OSPEADDecoySelector::normalizeProbabilities(std::vector<OutputAgeBin>& bins) {
  double totalProb = 0.0;
  for (const auto& bin : bins) {
    totalProb += bin.spendProbability;
  }

  if (totalProb > 0) {
    for (auto& bin : bins) {
      bin.spendProbability /= totalProb;
    }
  } else {
    // Uniform distribution if no pattern data
    double uniformProb = 1.0 / bins.size();
    for (auto& bin : bins) {
      bin.spendProbability = uniformProb;
    }
  }
}

void SpendPatternAnalyzer::updatePattern(const std::vector<TransactionOutputInfo>& newTransactions) {
  recentTransactions_.insert(recentTransactions_.end(),
                           newTransactions.begin(),
                           newTransactions.end());

  // Keep only recent transactions
  if (recentTransactions_.size() > MAX_HISTORY_SIZE) {
    size_t toRemove = recentTransactions_.size() - MAX_HISTORY_SIZE;
    recentTransactions_.erase(recentTransactions_.begin(),
                            recentTransactions_.begin() + toRemove);
  }

  // Recalculate pattern based on recent data
  uint64_t currentHeight = 0;
  if (!recentTransactions_.empty()) {
    currentHeight = recentTransactions_.back().creationHeight;
  }

  currentPattern_ = OSPEADDecoySelector::analyzeSpendPatterns(
    recentTransactions_, currentHeight);
}

bool SpendPatternAnalyzer::savePattern(const std::string& filePath) const {
  std::ofstream file(filePath, std::ios::binary);
  if (!file) {
    return false;
  }

  // Write pattern size
  size_t patternSize = currentPattern_.size();
  file.write(reinterpret_cast<const char*>(&patternSize), sizeof(patternSize));

  // Write pattern data
  for (const auto& bin : currentPattern_) {
    file.write(reinterpret_cast<const char*>(&bin.minAge), sizeof(bin.minAge));
    file.write(reinterpret_cast<const char*>(&bin.maxAge), sizeof(bin.maxAge));
    file.write(reinterpret_cast<const char*>(&bin.outputCount), sizeof(bin.outputCount));
    file.write(reinterpret_cast<const char*>(&bin.spendProbability), sizeof(bin.spendProbability));
  }

  return true;
}

bool SpendPatternAnalyzer::loadPattern(const std::string& filePath) {
  std::ifstream file(filePath, std::ios::binary);
  if (!file) {
    return false;
  }

  // Read pattern size
  size_t patternSize;
  file.read(reinterpret_cast<char*>(&patternSize), sizeof(patternSize));

  currentPattern_.clear();
  currentPattern_.reserve(patternSize);

  // Read pattern data
  for (size_t i = 0; i < patternSize; ++i) {
    OutputAgeBin bin(0, 0, 0, 0.0);
    file.read(reinterpret_cast<char*>(&bin.minAge), sizeof(bin.minAge));
    file.read(reinterpret_cast<char*>(&bin.maxAge), sizeof(bin.maxAge));
    file.read(reinterpret_cast<char*>(&bin.outputCount), sizeof(bin.outputCount));
    file.read(reinterpret_cast<char*>(&bin.spendProbability), sizeof(bin.spendProbability));
    currentPattern_.push_back(bin);
  }

  return true;
}

} // namespace CryptoNote
