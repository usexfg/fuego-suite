// Copyright (c) 2017-2025 Fuego Developers
// Copyright (c) 2018-2019 Conceal Network & Conceal Devs
// Copyright (c) 2016-2019 The Karbowanec developers
// Copyright (c) 2012-2018 The CryptoNote developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You may redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation. Fuego includes elements written
// by third parties. See file labeled LICENSE for more details.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

#include "AdaptiveDifficulty.h"
#include "CryptoNoteConfig.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace CryptoNote {

    AdaptiveDifficulty::AdaptiveDifficulty(const DifficultyConfig& config) 
        : m_config(config) {
    }

    uint64_t AdaptiveDifficulty::calculateNextDifficulty(
        uint32_t height,
        const std::vector<uint64_t>& timestamps,
        const std::vector<uint64_t>& cumulativeDifficulties,
        bool testnet) {
        
        // Early chain protection
        if (timestamps.size() < 3) {
            return 10000; // Minimum difficulty
        }
        
        // Check for emergency conditions
        if (detectHashRateAnomaly(timestamps, cumulativeDifficulties, testnet)) {
            return calculateEmergencyDifficulty(timestamps, cumulativeDifficulties);
        }
        
        // Check for block stealing attempts
        if (detectBlockStealingAttempt(timestamps, cumulativeDifficulties, testnet)) {
            return calculateEmergencyDifficulty(timestamps, cumulativeDifficulties);
        }
        
        // Use multi-window adaptive algorithm
        return calculateMultiWindowDifficulty(timestamps, cumulativeDifficulties, testnet);
    }

    uint64_t AdaptiveDifficulty::calculateMultiWindowDifficulty(
        const std::vector<uint64_t>& timestamps,
        const std::vector<uint64_t>& cumulativeDifficulties,
        bool testnet) {
        
        // Calculate LWMA for different windows
        double shortLWMA = calculateLWMA(timestamps, cumulativeDifficulties, m_config.shortWindow);
        double mediumLWMA = calculateLWMA(timestamps, cumulativeDifficulties, m_config.mediumWindow);
        double longLWMA = calculateLWMA(timestamps, cumulativeDifficulties, m_config.longWindow);
        
        // Calculate EMA for trend analysis (currently unused but available for future enhancements)
        // double shortEMA = calculateEMA(timestamps, m_config.shortWindow, 0.2);
        // double mediumEMA = calculateEMA(timestamps, m_config.mediumWindow, 0.1);
        
        // Weighted combination based on confidence
        double confidence = calculateConfidenceScore(timestamps, cumulativeDifficulties, testnet);
        
        // Adaptive weighting based on network conditions
        double shortWeight = (testnet ? CryptoNote::TESTNET_DMWDA_WEIGHT_SHORT : CryptoNote::parameters::DMWDA_WEIGHT_SHORT) * confidence;
        double mediumWeight = (testnet ? CryptoNote::TESTNET_DMWDA_WEIGHT_MEDIUM : CryptoNote::parameters::DMWDA_WEIGHT_MEDIUM) * confidence;
        double longWeight = (testnet ? CryptoNote::TESTNET_DMWDA_WEIGHT_LONG : CryptoNote::parameters::DMWDA_WEIGHT_LONG) * (1.0 - confidence);
        
        // Calculate weighted average solve time
        double weightedSolveTime = (shortLWMA * shortWeight + 
                                   mediumLWMA * mediumWeight + 
                                   longLWMA * longWeight) / 
                                   (shortWeight + mediumWeight + longWeight);
        
        // Calculate current average difficulty
        uint32_t effectiveWindow = std::min(static_cast<uint32_t>(timestamps.size() - 1), m_config.mediumWindow);

        // Prevent division by zero
        if (effectiveWindow == 0) {
            return 10000; // Minimum difficulty
        }

        uint64_t avgDifficulty = (cumulativeDifficulties[effectiveWindow] - cumulativeDifficulties[0]) / effectiveWindow;
        
        // Calculate new difficulty
        if (weightedSolveTime <= 0.0) {
            return 10000; // Minimum difficulty for invalid solve time
        }

        // Additional safety check: prevent extremely small solve times that could cause overflow
        if (weightedSolveTime < m_config.targetTime / 1000.0) {
            weightedSolveTime = m_config.targetTime / 1000.0;
        }

        double difficultyRatio = static_cast<double>(m_config.targetTime) / weightedSolveTime;

        // Apply bounds - use more conservative bounds to prevent extreme values
        difficultyRatio = std::max(m_config.minAdjustment,
                                  std::min(m_config.maxAdjustment, difficultyRatio));
        
        // Additional safety check: prevent extreme difficulty ratios
        if (difficultyRatio > 1000.0) {
            difficultyRatio = 1000.0;
        }
        
        // Prevent overflow in multiplication
        double calculatedDifficulty = static_cast<double>(avgDifficulty) * difficultyRatio;

        // Additional safety check: prevent extreme calculated difficulties
        if (calculatedDifficulty > static_cast<double>(avgDifficulty) * 1000.0) {
            calculatedDifficulty = static_cast<double>(avgDifficulty) * 1000.0;
        }

        // Clamp to prevent overflow
        if (calculatedDifficulty > static_cast<double>(std::numeric_limits<uint64_t>::max())) {
            calculatedDifficulty = static_cast<double>(std::numeric_limits<uint64_t>::max());
        }

        uint64_t newDifficulty = static_cast<uint64_t>(calculatedDifficulty);

        // Apply smoothing to prevent oscillations
        if (timestamps.size() > 1) {
            uint64_t prevDifficulty = cumulativeDifficulties[effectiveWindow] - cumulativeDifficulties[effectiveWindow - 1];
            newDifficulty = applySmoothing(newDifficulty, prevDifficulty, testnet);
        }

        // Minimum difficulty protection
        return std::max(static_cast<uint64_t>(10000), newDifficulty);
    }

    double AdaptiveDifficulty::calculateLWMA(
        const std::vector<uint64_t>& timestamps,
        const std::vector<uint64_t>& cumulativeDifficulties,
        uint32_t windowSize) {
        
        uint32_t effectiveWindow = std::min(static_cast<uint32_t>(timestamps.size() - 1), windowSize);
        
        // Safety check: prevent excessive window sizes that could cause overflow
        if (effectiveWindow > 200) {
            effectiveWindow = 200;
        }
        
        double weightedSum = 0.0;
        double weightSum = 0.0;
        
        for (uint32_t i = 1; i <= effectiveWindow; ++i) {
            int64_t solveTime = static_cast<int64_t>(timestamps[i]) - static_cast<int64_t>(timestamps[i - 1]);

            // Clamp solve time to prevent manipulation and overflow
            solveTime = std::max(static_cast<int64_t>(m_config.targetTime / 10),
                                std::min(static_cast<int64_t>(m_config.targetTime * 10), solveTime));

            double weight = static_cast<double>(i);
            
            // Prevent overflow in weighted sum calculation
            double weightedContribution = static_cast<double>(solveTime) * weight;
            if (weightedContribution > 1e15) {
                weightedContribution = 1e15; // Cap to prevent overflow
            }
            
            weightedSum += weightedContribution;
            weightSum += weight;
        }

        // Prevent division by zero
        if (weightSum == 0.0) {
            return static_cast<double>(m_config.targetTime);
        }

        double lwma = weightedSum / weightSum;
        
        // Additional safety check: prevent extremely small LWMA values that could cause overflow
        if (lwma < m_config.targetTime / 100.0) {
            lwma = m_config.targetTime / 100.0;
        }
        
        return lwma;
    }

    double AdaptiveDifficulty::calculateEMA(
        const std::vector<uint64_t>& timestamps,
        uint32_t windowSize,
        double alpha) {
        
        uint32_t effectiveWindow = std::min(static_cast<uint32_t>(timestamps.size() - 1), windowSize);
        
        if (effectiveWindow == 0) return static_cast<double>(m_config.targetTime);
        
        double ema = static_cast<double>(timestamps[1] - timestamps[0]);
        
        for (uint32_t i = 2; i <= effectiveWindow; ++i) {
            int64_t solveTime = static_cast<int64_t>(timestamps[i]) - static_cast<int64_t>(timestamps[i - 1]);
            solveTime = std::max(static_cast<int64_t>(m_config.targetTime / 10), 
                               std::min(static_cast<int64_t>(m_config.targetTime * 10), solveTime));
            
            ema = alpha * solveTime + (1.0 - alpha) * ema;
        }
        
        return ema;
    }

    uint64_t AdaptiveDifficulty::calculateEmergencyDifficulty(
        const std::vector<uint64_t>& timestamps,
        const std::vector<uint64_t>& cumulativeDifficulties) {
        
        uint32_t emergencyWindow = std::min(static_cast<uint32_t>(timestamps.size() - 1), m_config.emergencyWindow);

        if (emergencyWindow == 0) return 10000;

        // Calculate recent solve time
        double recentSolveTime = static_cast<double>(timestamps[emergencyWindow] - timestamps[0]) / emergencyWindow;

        if (recentSolveTime <= 0.0) {
            return 10000; // Minimum difficulty for invalid solve time
        }

        // Calculate current difficulty
        uint64_t currentDifficulty = (cumulativeDifficulties[emergencyWindow] - cumulativeDifficulties[0]) / emergencyWindow;
        
        // Emergency adjustment
        double emergencyRatio = static_cast<double>(m_config.targetTime) / recentSolveTime;

        // Apply emergency bounds
        emergencyRatio = std::max(m_config.emergencyThreshold,
                                 std::min(1.0 / m_config.emergencyThreshold, emergencyRatio));

        // Prevent overflow in emergency calculation
        double emergencyDifficultyCalc = static_cast<double>(currentDifficulty) * emergencyRatio;

        // Clamp to prevent overflow
        if (emergencyDifficultyCalc > static_cast<double>(std::numeric_limits<uint64_t>::max())) {
            emergencyDifficultyCalc = static_cast<double>(std::numeric_limits<uint64_t>::max());
        }

        uint64_t emergencyDifficulty = static_cast<uint64_t>(emergencyDifficultyCalc);

        return std::max(static_cast<uint64_t>(10000), emergencyDifficulty);
    }

    bool AdaptiveDifficulty::detectHashRateAnomaly(
        const std::vector<uint64_t>& timestamps,
        const std::vector<uint64_t>& difficulties,
        bool testnet) {
        
        if (timestamps.size() < 5) return false;
        
        // Calculate recent vs historical solve times
        uint32_t recentWindow = std::min(testnet ? CryptoNote::TESTNET_DMWDA_RECENT_WINDOW_SIZE : CryptoNote::parameters::DMWDA_RECENT_WINDOW_SIZE, static_cast<uint32_t>(timestamps.size() - 1));
        uint32_t historicalWindow = std::min(testnet ? CryptoNote::TESTNET_DMWDA_HISTORICAL_WINDOW_SIZE : CryptoNote::parameters::DMWDA_HISTORICAL_WINDOW_SIZE, static_cast<uint32_t>(timestamps.size() - 1));
        
        double recentSolveTime = static_cast<double>(timestamps[recentWindow] - timestamps[0]) / recentWindow;
        double historicalSolveTime = static_cast<double>(timestamps[historicalWindow] - timestamps[historicalWindow - recentWindow]) / recentWindow;
        
        // Detect if recent solve time is significantly different
        double ratio = recentSolveTime / historicalSolveTime;
        
        double threshold = testnet ? CryptoNote::TESTNET_DMWDA_HASH_RATE_CHANGE_THRESHOLD : CryptoNote::parameters::DMWDA_HASH_RATE_CHANGE_THRESHOLD;
        return (ratio < (1.0 / threshold) || ratio > threshold); // Configurable change threshold
    }

    bool AdaptiveDifficulty::detectBlockStealingAttempt(
        const std::vector<uint64_t>& timestamps,
        const std::vector<uint64_t>& difficulties,
        bool testnet) {
        
        if (timestamps.size() < 3) return false;
        
        // Detect suspiciously fast consecutive blocks
        uint32_t fastBlockCount = 0;
        uint32_t checkBlocks = std::min(CryptoNote::parameters::DMWDA_BLOCK_STEALING_CHECK_BLOCKS, static_cast<uint32_t>(timestamps.size() - 1));
        
        for (size_t i = 1; i <= checkBlocks; ++i) {
            int64_t solveTime = static_cast<int64_t>(timestamps[i]) - static_cast<int64_t>(timestamps[i - 1]);
            
            // If blocks are coming too fast (configurable threshold)
            double timeThreshold = testnet ? CryptoNote::TESTNET_DMWDA_BLOCK_STEALING_TIME_THRESHOLD : CryptoNote::parameters::DMWDA_BLOCK_STEALING_TIME_THRESHOLD;
            if (solveTime < static_cast<int64_t>(m_config.targetTime * timeThreshold)) {
                fastBlockCount++;
            }
        }
        
        // Trigger if more than threshold blocks are suspiciously fast
        return fastBlockCount >= (testnet ? CryptoNote::TESTNET_DMWDA_BLOCK_STEALING_THRESHOLD : CryptoNote::parameters::DMWDA_BLOCK_STEALING_THRESHOLD);
    }

    uint64_t AdaptiveDifficulty::applySmoothing(uint64_t newDifficulty, uint64_t previousDifficulty, bool testnet) {
        // Apply exponential smoothing to prevent oscillations
        double alpha = testnet ? CryptoNote::TESTNET_DMWDA_SMOOTHING_FACTOR : CryptoNote::parameters::DMWDA_SMOOTHING_FACTOR; // Smoothing factor

        // Prevent overflow by using double precision arithmetic
        double smoothed = alpha * static_cast<double>(newDifficulty) +
                         (1.0 - alpha) * static_cast<double>(previousDifficulty);

        // Clamp to prevent overflow
        if (smoothed > static_cast<double>(std::numeric_limits<uint64_t>::max())) {
            return std::numeric_limits<uint64_t>::max();
        }

        return static_cast<uint64_t>(smoothed);
    }

    double AdaptiveDifficulty::calculateConfidenceScore(
        const std::vector<uint64_t>& timestamps,
        const std::vector<uint64_t>& difficulties,
        bool testnet) {
        
        if (timestamps.size() < 3) return testnet ? CryptoNote::TESTNET_DMWDA_DEFAULT_CONFIDENCE : CryptoNote::parameters::DMWDA_DEFAULT_CONFIDENCE;
        
        // Calculate coefficient of variation for solve times
        std::vector<double> solveTimes;
        for (size_t i = 1; i < timestamps.size(); ++i) {
            solveTimes.push_back(static_cast<double>(timestamps[i] - timestamps[i - 1]));
        }
        
        double mean = std::accumulate(solveTimes.begin(), solveTimes.end(), 0.0) / solveTimes.size();
        double variance = 0.0;
        
        for (double solveTime : solveTimes) {
            variance += (solveTime - mean) * (solveTime - mean);
        }
        variance /= solveTimes.size();
        
        double coefficientOfVariation = std::sqrt(variance) / mean;
        
        // Convert to confidence score (lower variation = higher confidence)
        return std::max(testnet ? CryptoNote::TESTNET_DMWDA_CONFIDENCE_MIN : CryptoNote::parameters::DMWDA_CONFIDENCE_MIN, 
                       std::min(testnet ? CryptoNote::TESTNET_DMWDA_CONFIDENCE_MAX : CryptoNote::parameters::DMWDA_CONFIDENCE_MAX, 1.0 - coefficientOfVariation));
    }

    AdaptiveDifficulty::DifficultyConfig getDefaultFuegoConfig(bool testnet) {
        AdaptiveDifficulty::DifficultyConfig config;
        config.targetTime = CryptoNote::parameters::DIFFICULTY_TARGET; // 480 seconds
        
        if (testnet) {
            // Use testnet-specific DMWDA parameters
            config.shortWindow = CryptoNote::TESTNET_DMWDA_SHORT_WINDOW;    // Rapid response
            config.mediumWindow = CryptoNote::TESTNET_DMWDA_MEDIUM_WINDOW;   // Current window
            config.longWindow = CryptoNote::TESTNET_DMWDA_LONG_WINDOW;    // Trend analysis
            config.minAdjustment = CryptoNote::TESTNET_DMWDA_MIN_ADJUSTMENT; // 50% minimum change
            config.maxAdjustment = CryptoNote::TESTNET_DMWDA_MAX_ADJUSTMENT; // 400% maximum change
            config.emergencyThreshold = CryptoNote::TESTNET_DMWDA_EMERGENCY_THRESHOLD; // 10% emergency threshold
            config.emergencyWindow = CryptoNote::TESTNET_DMWDA_EMERGENCY_WINDOW; // Emergency response window
        } else {
            // Use mainnet DMWDA parameters
            config.shortWindow = CryptoNote::parameters::DMWDA_SHORT_WINDOW;    // Rapid response
            config.mediumWindow = CryptoNote::parameters::DMWDA_MEDIUM_WINDOW;   // Current window
            config.longWindow = CryptoNote::parameters::DMWDA_LONG_WINDOW;    // Trend analysis
            config.minAdjustment = CryptoNote::parameters::DMWDA_MIN_ADJUSTMENT; // 50% minimum change
            config.maxAdjustment = CryptoNote::parameters::DMWDA_MAX_ADJUSTMENT; // 400% maximum change
            config.emergencyThreshold = CryptoNote::parameters::DMWDA_EMERGENCY_THRESHOLD; // 10% emergency threshold
            config.emergencyWindow = CryptoNote::parameters::DMWDA_EMERGENCY_WINDOW; // Emergency response window
        }
        
        return config;
    }

} // namespace CryptoNote
