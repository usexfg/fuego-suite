// Copyright (c) 2017-2025 Fuego Developers
// Copyright (c) 2020-2025 Elderfire Privacy Group
// Copyright (c) 2014-2025 The Monero project
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

#include "DynamicRingSize.h"
#include "OSPEADDecoySelection.h"
#include "../CryptoNoteConfig.h"
#include "../BlockchainExplorer/BlockchainExplorer.h"
#include "../../include/BlockchainExplorerData.h"
#include <algorithm>
#include <sstream>
#include <fstream>
#include <iostream>
#include <typeinfo>
#include <boost/variant.hpp>

namespace CryptoNote {

size_t DynamicRingSizeCalculator::calculateOptimalRingSize(
  uint64_t amount,
  const std::vector<OutputInfo>& availableOutputs,
  uint8_t blockMajorVersion,
  size_t minRingSize,
  size_t maxRingSize
) {
  // For older block versions, use static ring size
  if (blockMajorVersion < BLOCK_MAJOR_VERSION_10) {
    return minRingSize;
  }

  // Get target ring sizes in order of preference
  std::vector<size_t> targetRingSizes = getTargetRingSizes();

  // Find the largest achievable ring size from approved sizes only
  for (size_t targetSize : targetRingSizes) {
    if (targetSize >= minRingSize && targetSize <= maxRingSize) {
      if (isRingSizeAchievable(targetSize, availableOutputs)) {
        return targetSize;
      }
    }
  }

  // No approved ring size ({18,15,12,10,8}) is achievable with current decoy pool.
  // On testnet (minRingSize == 0) allow any ring size so fresh chains can bootstrap.
  if (minRingSize == 0) {
    size_t available = 0;
    for (const auto& output : availableOutputs) {
      available = std::max(available, output.availableCount);
    }
    if (available > 0) {
      return std::min(available, maxRingSize);
    }
  }

  // Mainnet: reject — insufficient decoys for any approved ring size.
  return 0;
}

std::vector<size_t> DynamicRingSizeCalculator::getTargetRingSizes() {
  // Only allow specific uniform ring sizes: 18, 15, 12, 10, 8
  // Transactions with other ring sizes will be rejected
  return {18, 15, 12, 10, 8};
}

bool DynamicRingSizeCalculator::isRingSizeAchievable(
  size_t ringSize,
  const std::vector<OutputInfo>& availableOutputs
) {
  // Check if we have enough outputs of any amount to achieve the ring size
  for (const auto& output : availableOutputs) {
    if (output.availableCount >= ringSize) {
      return true;
    }
  }
  // Check if we can combine outputs from different amounts
  size_t totalAvailable = 0;
  for (const auto& output : availableOutputs) {
    totalAvailable += output.availableCount;
  }

  return totalAvailable >= ringSize;
}

// Check if a ring size is one of our approved uniform sizes
bool DynamicRingSizeCalculator::isApprovedRingSize(size_t ringSize) {
  std::vector<size_t> approvedSizes = getTargetRingSizes();
  return std::find(approvedSizes.begin(), approvedSizes.end(), ringSize) != approvedSizes.end();
}


std::string DynamicRingSizeCalculator::getPrivacyLevelDescription(size_t ringSize) {
  if (ringSize == 0) {
    return "Transaction Rejected - Use Optimizer (Insufficient outputs for approved ring sizes)";
  } else if (ringSize == 18) {
    return "Fuego Max Privacy (Ring Size 18)";
  } else if (ringSize == 15) {
    return "Strong Privacy (Ring Size 15)";
  } else if (ringSize == 12) {
    return "Better Privacy (Ring Size 12)";
  } else if (ringSize == 10) {
    return "Solid Privacy (Ring Size 10)";
  } else if (ringSize == 8) {
    return "Standard Privacy (Ring Size 8)";
  } else {
    return "Invalid Ring Size (Ring Size " + std::to_string(ringSize) + ") - Use Optimizer";
  }
}

// Blockchain data provider for OSPEAD pattern analysis
// Uses block-level data; per-output analysis requires BlockchainExplorer extensions
class BlockchainDataProvider {
public:
  BlockchainDataProvider(IBlockchainExplorer& explorer) : m_explorer(explorer) {}

  // Get recent transactions for pattern analysis
  std::vector<TransactionOutputInfo> getRecentTransactionsForAnalysis(
    uint64_t currentHeight,
    size_t maxTransactions = 1000
  ) {
    std::vector<TransactionOutputInfo> recentTransactions;

    try {
      // Get recent blocks (last 100 blocks for pattern analysis)
      uint32_t startHeight = currentHeight > 100 ? currentHeight - 100 : 1;
      std::vector<uint32_t> blockHeights;

      for (uint32_t height = startHeight; height <= currentHeight; ++height) {
        blockHeights.push_back(height);
      }

      std::vector<std::vector<BlockDetails>> blocks;
      if (m_explorer.getBlocks(blockHeights, blocks)) {
        for (const auto& blockList : blocks) {
          for (const auto& block : blockList) {
            // Record block-level info for OSPEAD pattern analysis
            // Per-output granularity requires BlockchainExplorer output enumeration
            TransactionOutputInfo blockInfo(
              block.alreadyGeneratedCoins,  // Use block coinbase as representative amount
              block.height,
              block.transactions.size()     // Transaction count as global index proxy
            );
            recentTransactions.push_back(blockInfo);
          }
        }
      }
    } catch (const std::exception& e) {
      // Log error but don't fail - use empty data
      std::cerr << "Error getting blockchain data for OSPEAD: " << e.what() << std::endl;
    }

    return recentTransactions;
  }

private:
  IBlockchainExplorer& m_explorer;
};

// OSPEAD output filtering using blockchain data
std::vector<OutputInfo> DynamicRingSizeCalculator::filterOutputsByOSPEAD(
  const std::vector<OutputInfo>& availableOutputs,
  uint64_t amount,
  uint64_t currentBlockHeight,
  const std::vector<TransactionOutputInfo>& recentTransactions,
  IBlockchainExplorer* blockchainExplorer
) {
  // If we have blockchain explorer, use actual txn data for pattern analysis
  std::vector<TransactionOutputInfo> patternTransactions = recentTransactions;

  if (blockchainExplorer && patternTransactions.empty()) {
    // Get txn data for pattern analysis
    BlockchainDataProvider dataProvider(*blockchainExplorer);
    patternTransactions = dataProvider.getRecentTransactionsForAnalysis(currentBlockHeight);
  }

  // filter outputs that match spend patterns
  std::vector<CryptoNote::TransactionOutputInfo> convertedTransactions;
  for (const auto& tx : patternTransactions) {
    convertedTransactions.emplace_back(tx.amount, tx.creationHeight, tx.globalIndex);
  }

  auto spendPattern = OSPEADDecoySelector::analyzeSpendPatterns(
    convertedTransactions, currentBlockHeight);

  std::vector<OutputInfo> filteredOutputs;

  for (const auto& output : availableOutputs) {
    // Calculate real output age using creation height when available
    uint64_t outputAge;
    if (output.creationHeight > 0) {
      // Use real creation height for accurate age calculation
      outputAge = currentBlockHeight - output.creationHeight;
    } else {
      // Fallback to amount approximation for existing data without creation height
      outputAge = currentBlockHeight - (output.amount / 1000000);
    }

    // Check if output age matches spend patterns
    double spendProb = OSPEADDecoySelector::calculateSpendProbability(
      outputAge, currentBlockHeight, spendPattern);

    // Only include outputs with reasonable spend probability (>1% threshold)
    if (spendProb > 0.01) {
      filteredOutputs.push_back(output);
    }
  }

  return filteredOutputs;
}

} // namespace CryptoNote
