// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed under the GNU General Public License v3.
// See file labeled LICENSE for more details.

#pragma once

#include <vector>
#include <cstdint>
#include <algorithm>
#include <cmath>

namespace CryptoNote {

    // Fuego DMWDA — Dynamic Multi-Window Difficulty Algorithm
    //
    // Parameter epochs allow tuning DMWDA per-network at specific heights WITHOUT a
    // major version upgrade or chain reset. To add a new epoch:
    //   1. Add a row to TESTNET_DMWDA_EPOCHS or MAINNET_DMWDA_EPOCHS in AdaptiveDifficulty.cpp
    //   2. Set activationHeight to a future block height
    //   3. Rebuild — old blocks re-validate with the epoch they were mined under

    class AdaptiveDifficulty {
    public:
        // Full parameter set for one DMWDA epoch.
        // All tunable knobs are here — nothing else needs to change.
        struct DifficultyConfig {
            uint64_t targetTime;                 // Target block time in seconds (480)
            uint32_t shortWindow;                // Recent-block window for fast response
            uint32_t mediumWindow;               // Medium window — stability anchor
            uint32_t longWindow;                 // Longer window — trend dampening
            uint32_t emergencyWindow;            // Window used in emergency path
            double   minAdjustment;              // Minimum ratio per block (floor on drop)
            double   maxAdjustment;              // Maximum ratio per block (ceiling on rise)
            double   emergencyThreshold;         // Emergency path clamp: [thresh, 1/thresh]
            double   smoothingFactor;            // EMA alpha: fraction of new value to use
            double   weightShort;                // Max weight for short window
            double   weightMedium;               // Fixed weight for medium window
            double   weightLong;                 // Max weight for long window
            double   confidenceMin;              // Lower bound on confidence score
            double   confidenceMax;              // Upper bound on confidence score
            double   defaultConfidence;          // Starting confidence when data is sparse
            uint32_t recentWindowSize;           // Blocks considered "recent" for anomaly check
            uint32_t historicalWindowSize;       // Historical comparison window for anomaly check
            uint32_t blockStealingCheckBlocks;   // Number of recent blocks inspected for stealing
            double   blockStealingTimeThreshold; // Fraction of targetTime below which a block is "fast"
            uint32_t blockStealingThreshold;     // Minimum fast-block count to trigger stealing detection
            double   hashRateChangeThreshold;    // Ratio of recent/historical solve time for anomaly
            uint64_t minDifficultyFloor;         // Hard floor on returned difficulty (replaces hardcoded 10000)
        };

        AdaptiveDifficulty(const DifficultyConfig& config);

        uint64_t calculateNextDifficulty(
            uint32_t height,
            const std::vector<uint64_t>& timestamps,
            const std::vector<uint64_t>& cumulativeDifficulties,
            bool testnet = false
        );

        uint64_t calculateEmergencyDifficulty(
            const std::vector<uint64_t>& timestamps,
            const std::vector<uint64_t>& cumulativeDifficulties,
            bool testnet = false
        );

        bool detectBlockStealingAttempt(
            const std::vector<uint64_t>& timestamps,
            const std::vector<uint64_t>& difficulties,
            bool testnet = false
        );

    private:
        DifficultyConfig m_config;

        uint64_t calculateMultiWindowDifficulty(
            const std::vector<uint64_t>& timestamps,
            const std::vector<uint64_t>& cumulativeDifficulties,
            bool testnet = false
        );

        double calculateLWMA(
            const std::vector<uint64_t>& timestamps,
            const std::vector<uint64_t>& cumulativeDifficulties,
            uint32_t windowSize
        );

        double calculateEMA(
            const std::vector<uint64_t>& timestamps,
            uint32_t windowSize,
            double alpha = 0.1
        );

        bool detectHashRateAnomaly(
            const std::vector<uint64_t>& timestamps,
            const std::vector<uint64_t>& difficulties,
            bool testnet = false
        );

        uint64_t applySmoothing(uint64_t newDifficulty, uint64_t previousDifficulty, bool testnet = false);

        double calculateConfidenceScore(
            const std::vector<uint64_t>& timestamps,
            const std::vector<uint64_t>& difficulties,
            bool testnet = false
        );
    };

    // Returns the DifficultyConfig for the epoch active at `height`.
    // Add new epochs to the tables in AdaptiveDifficulty.cpp — no other changes needed.
    AdaptiveDifficulty::DifficultyConfig getDefaultFuegoConfig(bool testnet, uint32_t height);

} // namespace CryptoNote
