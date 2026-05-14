// Copyright (c) 2017-2025 Fuego Developers
// Copyright (c) 2025 Elderfire Privacy Group
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

#include "BurnDepositValidationService.h"
#include "CryptoNoteCore/Core.h"
#include "CryptoNoteCore/Currency.h"
#include "CryptoNoteCore/TransactionPool.h"
#include "Logging/LoggerRef.h"
#include "Common/StringTools.h"
#include "crypto/crypto.h"
#include "EldernodeIndexManager.h"
#include "EldernodeIndexManager/EldernodeRandomSelector.h"
#include "EldernodeIndexTypes.h"
#include <chrono>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace CryptoNote {

// BurnDepositValidationResult implementation
BurnDepositValidationResult BurnDepositValidationResult::success(uint64_t amount, const Crypto::Hash& hash, uint64_t time, bool commitMatch, bool amountMatch, const std::string& txCommitment, uint64_t txAmount) {
    BurnDepositValidationResult result;
    result.isValid = true;
    result.validatedAmount = amount;
    result.burnProofHash = hash;
    result.timestamp = time;
    result.commitmentMatch = commitMatch;
    result.burnAmountMatch = amountMatch;
    result.txExtraCommitment = txCommitment;
    result.txBurnAmount = txAmount;
    return result;
}

BurnDepositValidationResult BurnDepositValidationResult::failure(const std::string& error) {
    BurnDepositValidationResult result;
    result.isValid = false;
    result.errorMessage = error;
    return result;
}

// BurnDepositConfig implementation
BurnDepositConfig BurnDepositConfig::getDefault() {
    BurnDepositConfig config;
    config.minimumBurnAmount = 8000000;  // 0.8 XFG minimum
    config.maximumBurnAmount = 8000000000;  // 800 XFG maximum
    config.proofExpirationSeconds = 3600;  // 1 hour
    config.requireProofValidation = true;
    config.treasuryAddress = "";
    config.fastPassConsensusThreshold = 2;  // 2/2 fastpass Eldernodes
    config.fallbackConsensusThreshold = 4;  // 4/5 fallback Eldernodes
    config.totalEldernodes = 5;     // Total Eldernodes 
    config.enableDualValidation = true;  // Both commitment and burn amount validation
    config.enableFastPass = true;   // Enable 2/2 fast pass consensus
    return config;
}

bool BurnDepositConfig::isValid() const {
    return minimumBurnAmount > 0 && 
           maximumBurnAmount > minimumBurnAmount && 
           proofExpirationSeconds > 0 && 
           fastPassConsensusThreshold > 0 && 
           fallbackConsensusThreshold > 0 && 
           totalEldernodes > 0 && 
           fastPassConsensusThreshold <= totalEldernodes &&
           fallbackConsensusThreshold <= totalEldernodes &&
           fastPassConsensusThreshold <= fallbackConsensusThreshold;
}

// BurnProofData implementation
bool BurnProofData::isValid() const {
    return burnAmount > 0 && 
           timestamp > 0 && 
           !depositorAddress.empty() && 
           !commitment.empty() && 
           !txHash.empty();
}

std::string BurnProofData::toString() const {
    std::ostringstream oss;
    oss << "BurnProofData{"
        << "burnHash=" << Common::podToHex(burnHash) << ", "
        << "burnAmount=" << burnAmount << ", "
        << "timestamp=" << timestamp << ", "
        << "depositorAddress=" << depositorAddress << ", "
        << "commitment=" << commitment << ", "
        << "txHash=" << txHash << "}";
    return oss.str();
}

// EldernodeConsensus implementation
bool EldernodeConsensus::isValid() const {
    return !eldernodeIds.empty() && 
           eldernodeIds.size() == signatures.size() && 
           !messageHash.empty() && 
           timestamp > 0 && 
           fastPassConsensusThreshold > 0 && 
           fallbackConsensusThreshold > 0 && 
           totalEldernodes > 0 && 
           verifiedInputs.isValid();
}

std::string EldernodeConsensus::toString() const {
    std::ostringstream oss;
    oss << "EldernodeConsensus{"
        << "eldernodeIds=[" << eldernodeIds.size() << "], "
        << "signatures=[" << signatures.size() << "], "
        << "messageHash=" << messageHash << ", "
        << "timestamp=" << timestamp << ", "
        << "fastPassThreshold=" << fastPassConsensusThreshold << "/" << totalEldernodes << ", "
        << "fallbackThreshold=" << fallbackConsensusThreshold << "/" << totalEldernodes << ", "
        << "fastPassUsed=" << (fastPassUsed ? "true" : "false") << ", "
        << "fallbackPathUsed=" << (fallbackPathUsed ? "true" : "false") << ", "
        << "commitmentMatch=" << (commitmentMatch ? "true" : "false") << ", "
        << "burnAmountMatch=" << (burnAmountMatch ? "true" : "false") << "}";
    return oss.str();
}

// EldernodeVerificationInputs implementation
bool EldernodeVerificationInputs::isValid() const {
    return !txHash.empty() && 
           !commitment.empty() && 
           burnAmount > 0;
}

std::string EldernodeVerificationInputs::toString() const {
    std::ostringstream oss;
    oss << "EldernodeVerificationInputs{"
        << "txHash=" << txHash << ", "
        << "commitment=" << commitment << ", "
        << "burnAmount=" << burnAmount << "}";
    return oss.str();
}

// BurnDepositValidationService implementation
BurnDepositValidationService::BurnDepositValidationService(core& core, std::shared_ptr<IEldernodeIndexManager> eldernodeManager)
    : m_core(core), m_eldernodeManager(eldernodeManager), m_config(BurnDepositConfig::getDefault()), m_totalBurnedAmount(0) {
}

BurnDepositValidationService::~BurnDepositValidationService() {
}

BurnDepositValidationResult BurnDepositValidationService::validateBurnDeposit(const BurnProofData& proof) {
    if (!proof.isValid()) {
        return BurnDepositValidationResult::failure("Invalid burn proof data");
    }

    if (!validateBurnAmount(proof.burnAmount)) {
        return BurnDepositValidationResult::failure("Burn amount outside valid range");
    }

    if (isProofExpired(proof)) {
        return BurnDepositValidationResult::failure("Burn proof has expired");
    }

    // Request Eldernode consensus for dual validation
    EldernodeVerificationInputs inputs;
    inputs.txHash = proof.txHash;
    inputs.commitment = proof.commitment;
    inputs.burnAmount = proof.burnAmount;

    auto consensus = requestEldernodeConsensus(inputs);
    if (!consensus) {
        return BurnDepositValidationResult::failure("Failed to obtain Eldernode consensus");
    }

    if (!verifyEldernodeConsensus(*consensus)) {
        return BurnDepositValidationResult::failure("Eldernode consensus verification failed");
    }

    // Check dual validation results
    if (m_config.enableDualValidation) {
        if (!consensus->commitmentMatch) {
            return BurnDepositValidationResult::failure("Commitment mismatch detected");
        }
        if (!consensus->burnAmountMatch) {
            return BurnDepositValidationResult::failure("Burn amount mismatch detected");
        }
    }

    // Add to burn proofs list
    m_burnProofs.push_back(proof);
    m_totalBurnedAmount += proof.burnAmount;

    return BurnDepositValidationResult::success(
        proof.burnAmount, 
        proof.burnHash, 
        proof.timestamp,
        consensus->commitmentMatch,
        consensus->burnAmountMatch,
        consensus->txExtraCommitment,
        consensus->txBurnAmount
    );
}

bool BurnDepositValidationService::verifyBurnProof(const BurnProofData& proof) {
    if (!proof.isValid()) {
        return false;
    }

    // Verify the proof signature
    if (!validateBurnProofSignature(proof)) {
        return false;
    }

    // Check if proof exists in our list
    auto it = std::find_if(m_burnProofs.begin(), m_burnProofs.end(),
        [&proof](const BurnProofData& existing) {
            return existing.burnHash == proof.burnHash;
        });

    return it != m_burnProofs.end();
}

std::optional<BurnProofData> BurnDepositValidationService::generateBurnProof(uint64_t amount, const std::string& depositorAddress, const std::string& commitment, const std::string& txHash) {
    if (!validateBurnAmount(amount)) {
        return std::nullopt;
    }

    BurnProofData proof;
    proof.burnAmount = amount;
    proof.depositorAddress = depositorAddress;
    proof.commitment = commitment;
    proof.txHash = txHash;
    proof.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    proof.treasuryAddress = m_config.treasuryAddress;
    proof.burnHash = calculateBurnHash(amount, depositorAddress, proof.timestamp);

    // Generate proof signature (simplified - in real implementation would use proper cryptographic signing)
    std::string signatureData = proof.toString();
    proof.proofSignature.resize(64);  // Placeholder for actual signature
    std::fill(proof.proofSignature.begin(), proof.proofSignature.end(), 0);

    return proof;
}

void BurnDepositValidationService::setBurnDepositConfig(const BurnDepositConfig& config) {
    if (config.isValid()) {
        m_config = config;
    }
}

BurnDepositConfig BurnDepositValidationService::getBurnDepositConfig() const {
    return m_config;
}

uint64_t BurnDepositValidationService::getTotalBurnedAmount() const {
    return m_totalBurnedAmount;
}

uint32_t BurnDepositValidationService::getTotalBurnProofs() const {
    return static_cast<uint32_t>(m_burnProofs.size());
}

std::vector<BurnProofData> BurnDepositValidationService::getRecentBurnProofs(uint32_t count) const {
    std::vector<BurnProofData> recent;
    auto start = m_burnProofs.size() > count ? m_burnProofs.end() - count : m_burnProofs.begin();
    recent.assign(start, m_burnProofs.end());
    return recent;
}

// Eldernode consensus methods
std::optional<EldernodeConsensus> BurnDepositValidationService::requestEldernodeConsensus(const EldernodeVerificationInputs& inputs) {
    if (!inputs.isValid()) {
        return std::nullopt;
    }

    // Get available Eldernodes for consensus
    auto participants = getEldernodeConsensusParticipants();
    if (participants.size() < m_config.fastPassConsensusThreshold) {
        return std::nullopt;
    }

    // Extract commitment and burn amount from transaction
    std::string txExtraCommitment = extractCommitmentFromTxExtra(inputs.txHash);
    uint64_t txBurnAmount = extractBurnAmountFromTransaction(inputs.txHash);

    // Verify dual validation
    bool commitmentMatch = verifyCommitmentMatch(inputs.commitment, txExtraCommitment);
    bool burnAmountMatch = verifyBurnAmountMatch(inputs.burnAmount, txBurnAmount);

    // Create consensus structure
    EldernodeConsensus consensus;
    consensus.verifiedInputs = BurnProofData();  // Simplified - would populate from inputs
    consensus.verifiedInputs.txHash = inputs.txHash;
    consensus.verifiedInputs.commitment = inputs.commitment;
    consensus.verifiedInputs.burnAmount = inputs.burnAmount;
    consensus.txExtraCommitment = txExtraCommitment;
    consensus.txBurnAmount = txBurnAmount;
    consensus.commitmentMatch = commitmentMatch;
    consensus.burnAmountMatch = burnAmountMatch;
    consensus.fastPassConsensusThreshold = m_config.fastPassConsensusThreshold;
    consensus.fallbackConsensusThreshold = m_config.fallbackConsensusThreshold;
    consensus.totalEldernodes = m_config.totalEldernodes;
    consensus.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    consensus.messageHash = calculateConsensusMessageHash(inputs);

    // Try fast pass consensus first (2/2)
    if (m_config.enableFastPass && participants.size() >= m_config.fastPassConsensusThreshold) {
        consensus.fastPassUsed = true;
        consensus.fallbackPathUsed = false;
        
        // Simulate fast pass Eldernode responses
        for (size_t i = 0; i < participants.size() && i < m_config.fastPassConsensusThreshold; ++i) {
            consensus.eldernodeIds.push_back(participants[i].address);
            consensus.signatures.push_back("fast_pass_signature_" + std::to_string(i));  // Placeholder
        }
        
        return consensus;
    }
    
    // Fallback to robust consensus (4/5)
    if (participants.size() >= m_config.fallbackConsensusThreshold) {
        consensus.fastPassUsed = false;
        consensus.fallbackPathUsed = true;
        
        // Simulate fallback Eldernode responses
        for (size_t i = 0; i < participants.size() && i < m_config.fallbackConsensusThreshold; ++i) {
            consensus.eldernodeIds.push_back(participants[i].address);
            consensus.signatures.push_back("fallback_signature_" + std::to_string(i));  // Placeholder
        }
        
        return consensus;
    }

    return std::nullopt;
}

bool BurnDepositValidationService::verifyEldernodeConsensus(const EldernodeConsensus& consensus) {
    if (!consensus.isValid()) {
        return false;
    }

    if (!checkConsensusThreshold(consensus)) {
        return false;
    }

    if (!validateEldernodeSignatures(consensus)) {
        return false;
    }

    return true;
}

std::string BurnDepositValidationService::extractCommitmentFromTxExtra(const std::string& txHash) {
    // TODO: Implement actual Fuego RPC call to extract commitment from tx_extra
    // For now, return a placeholder
    return "0x1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef";
}

uint64_t BurnDepositValidationService::extractBurnAmountFromTransaction(const std::string& txHash) {
    // TODO: Implement actual Fuego RPC call to extract burn amount from undefined output key
    // For now, return a placeholder
    return 1000000;  // 1 XFG
}

bool BurnDepositValidationService::verifyCommitmentMatch(const std::string& providedCommitment, const std::string& txExtraCommitment) {
    return providedCommitment == txExtraCommitment;
}

bool BurnDepositValidationService::verifyBurnAmountMatch(uint64_t providedAmount, uint64_t txBurnAmount) {
    return providedAmount == txBurnAmount;
}

// Private helper methods
bool BurnDepositValidationService::validateBurnAmount(uint64_t amount) const {
    return amount >= m_config.minimumBurnAmount && amount <= m_config.maximumBurnAmount;
}

bool BurnDepositValidationService::validateBurnProofSignature(const BurnProofData& proof) const {
    // TODO: Implement actual signature verification
    // For now, just check that signature exists
    return !proof.proofSignature.empty();
}

Crypto::Hash BurnDepositValidationService::calculateBurnHash(uint64_t amount, const std::string& depositorAddress, uint64_t timestamp) const {
    std::string data = std::to_string(amount) + depositorAddress + std::to_string(timestamp);
    Crypto::Hash hash;
    Crypto::cn_fast_hash(data.data(), data.size(), hash);
    return hash;
}

bool BurnDepositValidationService::isProofExpired(const BurnProofData& proof) const {
    uint64_t currentTime = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return (currentTime - proof.timestamp) > m_config.proofExpirationSeconds;
}

std::vector<EldernodeConsensusParticipant> BurnDepositValidationService::getEldernodeConsensusParticipants() const {
    if (!m_eldernodeManager) {
        return {};
    }

    // Get all Eldernodes and sort by tier (Elderfier first)
    auto allEldernodes = m_eldernodeManager->getAllEldernodes();
    std::sort(allEldernodes.begin(), allEldernodes.end());

    // Convert to consensus participants
    std::vector<EldernodeConsensusParticipant> participants;
    for (const auto& eldernode : allEldernodes) {
        EldernodeConsensusParticipant participant;
        participant.address = eldernode.feeAddress;
        participant.tier = eldernode.tier;
        participant.stakeAmount = eldernode.stakeAmount;
        participants.push_back(participant);
    }

    return participants;
}

bool BurnDepositValidationService::validateEldernodeSignatures(const EldernodeConsensus& consensus) const {
    // TODO: Implement actual signature validation
    // For now, just check that we have the expected number of signatures
    if (consensus.fastPassUsed) {
        return consensus.signatures.size() >= consensus.fastPassConsensusThreshold;
    } else if (consensus.fallbackPathUsed) {
        return consensus.signatures.size() >= consensus.fallbackConsensusThreshold;
    }
    return false;
}

std::string BurnDepositValidationService::calculateConsensusMessageHash(const EldernodeVerificationInputs& inputs) const {
    std::string message = inputs.txHash + inputs.commitment + std::to_string(inputs.burnAmount);
    Crypto::Hash hash;
    Crypto::cn_fast_hash(message.data(), message.size(), hash);
    return Common::podToHex(hash);
}

bool BurnDepositValidationService::checkConsensusThreshold(const EldernodeConsensus& consensus) const {
    if (consensus.fastPassUsed) {
        return consensus.eldernodeIds.size() >= consensus.fastPassConsensusThreshold;
    } else if (consensus.fallbackPathUsed) {
        return consensus.eldernodeIds.size() >= consensus.fallbackConsensusThreshold;
    }
    return false;
}

} // namespace CryptoNote
