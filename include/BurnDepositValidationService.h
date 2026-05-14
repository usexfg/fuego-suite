// Copyright (c) 2017-2025 Fuego Developers
// Copyright (c) 2025 Elderfire Privcy Group
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
#include <memory>
#include <optional>
#include <unordered_map>
#include "crypto/hash.h"
#include "crypto/crypto.h"
#include "Common/StringTools.h"
#include "EldernodeIndexTypes.h"

namespace CryptoNote {

struct BurnDepositValidationResult {
    bool isValid;
    std::string errorMessage;
    uint64_t validatedAmount;
    Crypto::Hash burnProofHash;
    uint64_t timestamp;
    bool commitmentMatch;
    bool burnAmountMatch;
    std::string txExtraCommitment;
    uint64_t txBurnAmount;
    static BurnDepositValidationResult success(uint64_t amount, const Crypto::Hash& hash, uint64_t time, bool commitMatch, bool amountMatch, const std::string& txCommitment, uint64_t txAmount);
    static BurnDepositValidationResult failure(const std::string& error);
};

struct BurnDepositConfig {
    uint64_t minimumBurnAmount;
    uint64_t maximumBurnAmount;
    uint32_t proofExpirationSeconds;
    bool requireProofValidation;
    std::string treasuryAddress;
    uint32_t fastPassConsensusThreshold;  // 2/2 fast pass threshold
    uint32_t fallbackConsensusThreshold;  // 4/5 fallback threshold
    uint32_t totalEldernodes;     // Total number of Eldernodes in network
    bool enableDualValidation;    // Both commitment and burn amount validation
    bool enableFastPass;          // Enable 2/2 fast pass consensus
    static BurnDepositConfig getDefault();
    bool isValid() const;
};

struct BurnProofData {
    Crypto::Hash burnHash;
    uint64_t burnAmount;
    uint64_t timestamp;
    std::vector<uint8_t> proofSignature;
    std::string depositorAddress;
    std::string treasuryAddress;
    std::string commitment;       // 32-byte hex string commitment
    std::string txHash;          // Fuego transaction hash
    bool isValid() const;
    std::string toString() const;
};

struct EldernodeConsensus {
    std::vector<std::string> eldernodeIds;
    std::vector<std::string> signatures;
    std::string messageHash;
    uint64_t timestamp;
    uint32_t fastPassConsensusThreshold;     // 2/2 fast pass threshold
    uint32_t fallbackConsensusThreshold; // 4/5 fallback threshold
    uint32_t totalEldernodes;
    bool fastPassUsed;                   // Whether fast pass (2/2) was used
    bool fallbackPathUsed;               // Whether fallback path (4/5) was used
    BurnProofData verifiedInputs;
    std::string txExtraCommitment;  // Commitment extracted from tx_extra
    uint64_t txBurnAmount;          // Burn amount from transaction (undefined output key)
    bool commitmentMatch;           // Whether commitments match
    bool burnAmountMatch;          // Whether burn amounts match
    bool isValid() const;
    std::string toString() const;
};

struct EldernodeVerificationInputs {
    std::string txHash;           // Fuego transaction hash
    std::string commitment;       // The commitment as a whole (32-byte hex string)
    uint64_t burnAmount;          // Burn amount (amount with undefined output key)
    bool isValid() const;
    std::string toString() const;
};

class core;
class IEldernodeIndexManager;

class IBurnDepositValidationService {
public:
    virtual ~IBurnDepositValidationService() = default;
    virtual BurnDepositValidationResult validateBurnDeposit(const BurnProofData& proof) = 0;
    virtual bool verifyBurnProof(const BurnProofData& proof) = 0;
    virtual std::optional<BurnProofData> generateBurnProof(uint64_t amount, const std::string& depositorAddress, const std::string& commitment, const std::string& txHash) = 0;
    virtual void setBurnDepositConfig(const BurnDepositConfig& config) = 0;
    virtual BurnDepositConfig getBurnDepositConfig() const = 0;
    virtual uint64_t getTotalBurnedAmount() const = 0;
    virtual uint32_t getTotalBurnProofs() const = 0;
    virtual std::vector<BurnProofData> getRecentBurnProofs(uint32_t count) const = 0;
    
    // Eldernode consensus methods
    virtual std::optional<EldernodeConsensus> requestEldernodeConsensus(const EldernodeVerificationInputs& inputs) = 0;
    virtual bool verifyEldernodeConsensus(const EldernodeConsensus& consensus) = 0;
    virtual std::string extractCommitmentFromTxExtra(const std::string& txHash) = 0;
    virtual uint64_t extractBurnAmountFromTransaction(const std::string& txHash) = 0;
    virtual bool verifyCommitmentMatch(const std::string& providedCommitment, const std::string& txExtraCommitment) = 0;
    virtual bool verifyBurnAmountMatch(uint64_t providedAmount, uint64_t txBurnAmount) = 0;
};

class BurnDepositValidationService : public IBurnDepositValidationService {
public:
    explicit BurnDepositValidationService(core& core, std::shared_ptr<IEldernodeIndexManager> eldernodeManager);
    ~BurnDepositValidationService();
    
    BurnDepositValidationResult validateBurnDeposit(const BurnProofData& proof) override;
    bool verifyBurnProof(const BurnProofData& proof) override;
    std::optional<BurnProofData> generateBurnProof(uint64_t amount, const std::string& depositorAddress, const std::string& commitment, const std::string& txHash) override;
    void setBurnDepositConfig(const BurnDepositConfig& config) override;
    BurnDepositConfig getBurnDepositConfig() const override;
    uint64_t getTotalBurnedAmount() const override;
    uint32_t getTotalBurnProofs() const override;
    std::vector<BurnProofData> getRecentBurnProofs(uint32_t count) const override;
    
    // Eldernode consensus methods
    std::optional<EldernodeConsensus> requestEldernodeConsensus(const EldernodeVerificationInputs& inputs) override;
    bool verifyEldernodeConsensus(const EldernodeConsensus& consensus) override;
    std::string extractCommitmentFromTxExtra(const std::string& txHash) override;
    uint64_t extractBurnAmountFromTransaction(const std::string& txHash) override;
    bool verifyCommitmentMatch(const std::string& providedCommitment, const std::string& txExtraCommitment) override;
    bool verifyBurnAmountMatch(uint64_t providedAmount, uint64_t txBurnAmount) override;

private:
    core& m_core;
    std::shared_ptr<IEldernodeIndexManager> m_eldernodeManager;
    BurnDepositConfig m_config;
    std::vector<BurnProofData> m_burnProofs;
    uint64_t m_totalBurnedAmount;
    
    bool validateBurnAmount(uint64_t amount) const;
    bool validateBurnProofSignature(const BurnProofData& proof) const;
    Crypto::Hash calculateBurnHash(uint64_t amount, const std::string& depositorAddress, uint64_t timestamp) const;
    bool isProofExpired(const BurnProofData& proof) const;
    
    // Eldernode consensus helpers
    std::vector<EldernodeConsensusParticipant> getEldernodeConsensusParticipants() const;
    bool validateEldernodeSignatures(const EldernodeConsensus& consensus) const;
    std::string calculateConsensusMessageHash(const EldernodeVerificationInputs& inputs) const;
    bool checkConsensusThreshold(const EldernodeConsensus& consensus) const;
};

} // namespace CryptoNote
