// Copyright (c) 2017-2026 Fuego Developers
// Copyright (c) 2020-2026 Elderfire Privacy Group
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
#include <chrono>
#include "../src/crypto/hash.h"
#include "../src/crypto/crypto.h"

namespace CryptoNote {

// Service ID types for Elderfier nodes
enum class ServiceIdType : uint8_t {
    STANDARD_ADDRESS = 0,    // Standard fee address (like basic Eldernodes)
    CUSTOM_NAME = 1,         // Custom name (exactly 8 letters, all caps) - links to hashed public fee address
    HASHED_ADDRESS = 2       // Hashed public fee address (for added privacy)
};

// Service ID structure for Elderfier nodes
struct ElderfierServiceId {
    ServiceIdType type;
    std::string identifier;  // Raw identifier (address, name, or hash)
    std::string displayName; // Human-readable display name
    std::string linkedAddress; // Actual wallet address
    std::string hashedAddress; // SHA256 hash of the wallet address (for all Elderfier nodes)

    bool isValid() const;
    std::string toString() const;
    static ElderfierServiceId createStandardAddress(const std::string& address);
    static ElderfierServiceId createCustomName(const std::string& name, const std::string& walletAddress);
    static ElderfierServiceId createHashedAddress(const std::string& address);
};

// Eldernode tier levels
enum class EldernodeTier : uint8_t {
    ELDERFIER = 1,       // Elderfier service node (4444 XFG stake required)
    ELDERADO = 2         // Elderado validator (4000 XFG stake required)
};

// Note: Old stake proof system removed - now using 0xEF tag deposits for Elderfiers

// Security window configuration
namespace SecurityWindow {
    static const uint64_t DEFAULT_DURATION_SECONDS = 28800;      // 8 hours
    static const uint64_t MINIMUM_SIGNATURE_INTERVAL = 3600;     // 1 hour minimum between signatures
    static const uint64_t GRACE_PERIOD_SECONDS = 300;            // 5 minute grace period
    static const uint64_t MAX_OFFLINE_TIME = 86400;              // 24 hours max offline
}

// Mempool buffer security window for spending transactions
struct MempoolSecurityWindow {
    Crypto::Hash transactionHash;        // Hash of spending transaction
    Crypto::PublicKey elderfierPublicKey; // Elderfier attempting to spend
    uint64_t timestamp;                  // When transaction entered buffer
    uint64_t securityWindowEnd;          // When security window ends
    bool signatureValidated;             // Whether last signature was valid
    bool elderCouncilVoteRequired;       // Whether Elder Council vote is needed
    std::vector<Crypto::PublicKey> votes; // Elder Council votes (for/against)
    uint32_t requiredVotes;              // Required votes for quorum
    uint32_t currentVotes;               // Current vote count

    bool isSecurityWindowActive() const;
    bool hasQuorumReached() const;
    bool canReleaseTransaction() const;
    void addVote(const Crypto::PublicKey& voter);
    std::string toString() const;
    bool isConstantProof() const;
    bool isConstantProofExpired() const;
};

// Vote types for Elder Council decisions
enum class ElderCouncilVoteType : uint8_t {
    SLASH_ALL = 1,      // Slash/burn ALL of Elderfier's stake
    SLASH_HALF = 2,      // Slash/burn HALF of Elderfier's stake
    SLASH_NONE = 3,      // Slash/burn NONE of Elderfier's stake (default)
    GOOD_KEEPALL = 4     // No slashing - acknowledge good participation
};

// Consensus types for Elderfier messages (0xEF transactions)
enum class ElderfierConsensusType : uint8_t {
    QUORUM = 1,         // Requires >80% Elder Council agreement (for slashing)
    PROOF = 2,          // Requires cryptographic proof validation
    WITNESS = 3         // Requires witness attestation
};

// Elder Council voting system
struct ElderCouncilVote {
    Crypto::PublicKey voterPublicKey;    // Elderfier who voted
    Crypto::PublicKey targetPublicKey;   // Elderfier being voted on
    bool voteFor;                        // true = allow spending, false = deny
    uint64_t timestamp;                  // Vote timestamp
    Crypto::Hash voteHash;               // Hash of vote data
    std::vector<uint8_t> signature;      // Vote signature

    bool isValid() const;
    Crypto::Hash calculateVoteHash() const;
    std::string toString() const;
};

// Elder Council voting message (like email inbox)
struct ElderCouncilVotingMessage {
    Crypto::Hash messageId;              // Unique message ID
    Crypto::PublicKey targetElderfier;   // Elderfier being voted on
    std::string subject;                  // Subject line
    std::string description;              // Detailed description of situation
    uint64_t timestamp;                  // When message was created
    uint64_t votingDeadline;             // When voting closes
    bool isRead;                         // Whether Elderfier has read the message
    bool hasVoted;                       // Whether Elderfier has voted on this
    bool hasConfirmedVote;               // Whether Elderfier has confirmed their vote
    ElderCouncilVoteType pendingVoteType; // Pending vote type (before confirmation)
    ElderCouncilVoteType confirmedVoteType; // Confirmed vote type (after confirmation)
    std::vector<ElderCouncilVote> votes;  // Votes cast so far
    uint32_t requiredVotes;              // Required votes for quorum
    uint32_t currentVotes;               // Current vote count

    bool isVotingActive() const;
    bool hasQuorumReached() const;
    std::string getVotingStatus() const;
    std::string toString() const;
};

// Misbehavior evidence for Elder Council voting
struct MisbehaviorEvidence {
    Crypto::PublicKey elderfierPublicKey; // Elderfier who misbehaved
    uint32_t invalidSignatures;           // Number of invalid signatures
    uint32_t totalAttempts;              // Total signature attempts
    uint64_t firstInvalidSignature;      // Timestamp of first invalid signature
    uint64_t lastInvalidSignature;      // Timestamp of last invalid signature
    std::vector<Crypto::Hash> invalidSignatureHashes; // Hashes of invalid signatures
    std::string misbehaviorType;        // Type of misbehavior (e.g., "Invalid Signatures")
    std::string evidenceDescription;     // Detailed description of evidence

    bool isValid() const;
    std::string getSummary() const;
    std::string toString() const;
};

// Monitoring configuration
struct ElderfierMonitoringConfig {
    bool enableBlockBasedMonitoring;      // Monitor each block for 0x06 spending transactions
    bool enableMempoolBuffer;            // Enable mempool security window buffer
    bool enableElderCouncilVoting;        // Enable Elder Council voting system
    uint64_t mempoolBufferDuration;      // How long to hold transactions in buffer (default: 8 hours)
    uint32_t elderCouncilQuorumSize;     // Required votes for Elder Council quorum (default: 5)
    uint64_t votingWindowDuration;      // How long voting window stays open (default: 24 hours)

    static ElderfierMonitoringConfig getDefault() {
        ElderfierMonitoringConfig config;
        config.enableBlockBasedMonitoring = true;  // Monitor each block
        config.enableMempoolBuffer = true;          // Enable mempool buffer
        config.enableElderCouncilVoting = true;     // Enable Elder Council voting
        config.mempoolBufferDuration = 28800;        // 8 hours buffer
        config.elderCouncilQuorumSize = 5;           // 5 votes for quorum
        config.votingWindowDuration = 86400;         // 24 hours voting window
        return config;
    }

    bool isValid() const {
        return elderCouncilQuorumSize > 0 && elderCouncilQuorumSize <= 20; // Max 20 votes
    }
};

// Elderfier deposit data structure
struct ElderfierDepositData {
    Crypto::Hash depositHash;
    Crypto::PublicKey elderfierPublicKey;
    uint64_t depositAmount;
    uint64_t depositTimestamp;
    uint64_t lastSeenTimestamp;
    uint64_t totalUptimeSeconds;
    uint32_t selectionMultiplier;
    std::string elderfierAddress;
    ElderfierServiceId serviceId;
    bool isActive;
    bool isSlashable;
    bool isUnlocked;                 // Can be unlocked after security window
    bool isSpent;                    // True if deposit funds have been spent

    // Post-quantum hybrid extension (backward compatible)
    std::string public_key_type = "Ed25519";  // "Ed25519" or "ML-DSA-65"
    std::vector<uint8_t> pq_public_key;       // Empty for Ed25519

    // Security window fields
    uint64_t lastSignatureTimestamp; // Last signature timestamp
    uint64_t securityWindowEnd;      // When security window ends
    uint64_t securityWindowDuration; // Duration of security window
    bool isInSecurityWindow;         // Currently in security window

    // User-Initiated Unstaking Model (Dynamigo)
    // Stake held indefinitely until user requests unstaking
    // Then 1000 blocks countdown begins before claiming is allowed
    bool unstakingRequested;         // true = user initiated unstaking, false = still staking
    uint64_t unstakingRequestBlock;  // Block height when user called initiate-unstake (0 if not requested)
    uint64_t unstakeClaimableBlock;  // Block height = unstakingRequestBlock + 1000 when claim becomes possible (8 days)

    // Methods
    bool isValid() const;
    bool isOnline() const;
    bool isDepositValid() const;     // Check if deposit is still valid (not spent)
    bool canUnlock() const;          // Check if deposit can be unlocked (outside security window)
    uint64_t getSecurityWindowRemaining() const; // Get remaining time in security window
    uint32_t calculateSelectionMultiplier() const;
    void updateUptime(uint64_t currentTimestamp);
    void markOffline(uint64_t currentTimestamp);
    void markSpent();                // Mark deposit as spent (invalidates Elderfier status)
    void updateLastSignature(uint64_t timestamp); // Update last signature timestamp

    // EFier requests to unstake: sets flag and records block height
    void initiateUnstake(uint64_t blockHeight) {
      unstakingRequested = true;
      unstakingRequestBlock = blockHeight;
      unstakeClaimableBlock = blockHeight + 1000;  // use 1k  //8 days (180 blocks/day * 8 = 1440 blocks at 8 min/block)
    }

    // Check if unstaking window has passed and funds can be claimed
    bool canClaimUnstakedFunds(uint64_t currentBlock) const {
      return unstakingRequested && currentBlock >= unstakeClaimableBlock;
    }

    std::string toString() const;
};

// Fee structure constants
namespace EldernodeFees {
    static const uint64_t LARGE_BURN_FEE = 8000000;      // 0.8 XFG for large burns (800 XFG+)
    static const uint64_t DEFAULT_BURN_FEE = 80000;       // 0.008 XFG for default burns
    static const uint64_t ELDERFIER_STAKE_AMOUNT = 4444000000000;  // 4444 XFG stake required
    static const uint64_t ELDERADO_STAKE_AMOUNT  = 4000000000000;  // 4000 XFG stake required
}

// Selection multiplier mapping based on uptime duration
namespace SelectionMultipliers {
    static const uint64_t MONTH_1_SECONDS = 2592000;    // 30 days
    static const uint64_t MONTH_3_SECONDS = 7776000;   // 90 days
    static const uint64_t MONTH_6_SECONDS = 15552000;   // 180 days
    static const uint64_t YEAR_1_SECONDS = 31536000;    // 365 days
    static const uint64_t YEAR_2_SECONDS = 63072000;    // 730 days

    static const uint32_t UPTIME_1_MONTH_MULTIPLIER = 1;   // 1x (0-1 month)
    static const uint32_t UPTIME_3_MONTH_MULTIPLIER = 2;   // 2x (1-3 months)
    static const uint32_t UPTIME_6_MONTH_MULTIPLIER = 4;   // 4x (3-6 months)
    static const uint32_t UPTIME_1_YEAR_MULTIPLIER = 8;    // 8x (6-12 months)
    static const uint32_t UPTIME_2_YEAR_MULTIPLIER = 16;   // 16x (1-2 years)
    static const uint32_t MAX_MULTIPLIER = 16;             // Cap at 2 years
}

// Eldernode consensus participant
struct EldernodeConsensusParticipant {
    Crypto::PublicKey publicKey;
    std::string address;
    uint64_t stakeAmount;
    uint32_t selectionMultiplier;  // Selection probability multiplier
    bool isActive;
    std::chrono::system_clock::time_point lastSeen;
    EldernodeTier tier;
    ElderfierServiceId serviceId;  // Only used for ELDERFIER tier

    bool operator==(const EldernodeConsensusParticipant& other) const;
    bool operator<(const EldernodeConsensusParticipant& other) const;
};

// Random selection result for Elderfier verification
struct ElderfierSelectionResult {
    std::vector<EldernodeConsensusParticipant> selectedElderfiers;  // Exactly 2 Elderfiers
    Crypto::Hash selectionHash;  // Provably fair random seed
    uint64_t blockHeight;        // Block height used for selection
    uint64_t totalWeight;        // Sum of all selection multipliers
    std::vector<uint32_t> selectionWeights;  // Individual weights used in selection

    bool isValid() const;        // Verify exactly 2 Elderfiers selected
    std::string toString() const;
};

// Consensus result structure
struct EldernodeConsensusResult {
    bool consensusReached;
    uint32_t requiredThreshold;
    uint32_t actualVotes;
    std::vector<Crypto::PublicKey> participatingEldernodes;
    std::vector<uint8_t> aggregatedSignature;
    uint64_t consensusTimestamp;

    bool isValid() const;
    std::string toString() const;
};

// ENindex entry structure
struct ENindexEntry {
    Crypto::PublicKey eldernodePublicKey;
    std::string feeAddress;
    uint64_t stakeAmount;
    uint64_t registrationTimestamp;
    bool isActive;
    uint32_t consensusParticipationCount;
    std::chrono::system_clock::time_point lastActivity;
    EldernodeTier tier;
    ElderfierServiceId serviceId;  // Only used for ELDERFIER tier

    bool operator==(const ENindexEntry& other) const;
    bool operator<(const ENindexEntry& other) const;

};

// Consensus thresholds configuration
struct ConsensusThresholds {
    uint32_t minimumEldernodes;
    uint32_t requiredAgreement; // e.g., 4/5 instead of 3/5
    uint32_t timeoutSeconds;
    uint32_t retryAttempts;

    static ConsensusThresholds getDefault();
    bool isValid() const;
};

// Deposit validation result
struct DepositValidationResult {
    bool isValid;
    std::string errorMessage;
    uint64_t validatedAmount;
    Crypto::Hash validatedDepositHash;

    static DepositValidationResult success(uint64_t amount, const Crypto::Hash& hash);
    static DepositValidationResult failure(const std::string& error);
};

// Slashing configuration - ONLY BURN ALLOWED
enum class SlashingDestination : uint8_t {
    BURN = 0,           // Burn slashed stakes (remove from circulation) - ONLY OPTION
    // DEPRECATED: Other destinations removed to prevent perverse incentives
    // TREASURY = 1,    // DEPRECATED: No longer allowed
    // REDISTRIBUTE = 2, // DEPRECATED: No longer allowed
    // CHARITY = 3      // DEPRECATED: No longer allowed
};

struct SlashingConfig {
    SlashingDestination destination;
    std::string destinationAddress; // Address for treasury/charity
    uint64_t slashingPercentage;    // Default percentage of stake to slash (legacy)
    uint64_t halfSlashPercentage;   // Percentage for SLASH_HALF votes (default 50%)
    uint64_t fullSlashPercentage;   // Percentage for SLASH_ALL votes (default 100%)
    bool enableSlashing;            // Whether slashing is enabled
    bool allowForceSlashing;        // Whether Elder Council can slash outside security window

    static SlashingConfig getDefault();
    bool isValid() const;

    // Get slashing percentage based on vote type
    uint64_t getSlashingPercentage(ElderCouncilVoteType voteType = ElderCouncilVoteType::SLASH_ALL) const;
};

// Elderfier service configuration
struct ElderfierServiceConfig {
    uint64_t minimumStakeAmount;      // 4444 XFG minimum for Elderfier
    uint64_t customNameLength;        // Exactly 8 letters for custom names
    bool allowHashedAddresses;        // Whether to allow hashed addresses
    std::vector<std::string> reservedNames; // Reserved custom names
    SlashingConfig slashingConfig;    // Slashing configuration

    static ElderfierServiceConfig getDefault();
    bool isValid() const;
    bool isCustomNameReserved(const std::string& name) const;
    bool isValidCustomName(const std::string& name) const;
};

} // namespace CryptoNote
