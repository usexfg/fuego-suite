// Copyright (c) 2017-2025 Elderfire Privacy Group
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

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include "CryptoNoteCore/TransactionExtra.h"
#include "crypto/crypto.h"

namespace CryptoNote {

/**
 * Burn Transaction Handler
 * Handles detection of burn transactions and automatic STARK proof generation
 */
class BurnTransactionHandler {
public:
    // Callback types
    using BurnDetectedCallback = std::function<void(const std::string& txHash, uint64_t amount, const std::string& ethAddress)>;
    using StarkProofGeneratedCallback = std::function<void(const std::string& txHash, const std::string& proofData)>;
    using ErrorCallback = std::function<void(const std::string& error)>;

    BurnTransactionHandler();
    ~BurnTransactionHandler();

    /**
     * Check if transaction extra data contains a HEAT commitment (burn transaction)
     * @param txExtra Raw transaction extra data
     * @return true if this is a burn transaction
     */
    bool isBurnTransaction(const std::vector<uint8_t>& txExtra);

    /**
     * Parse burn transaction data from tx_extra
     * @param txExtra Raw transaction extra data
     * @return Burn transaction data or empty if not a burn
     */
    struct BurnTransactionData {
        std::string commitmentHash;
        uint64_t amount;
        std::string metadata;
        std::string ethAddress;
        bool isValid = false;
    };
    
    BurnTransactionData parseBurnTransaction(const std::vector<uint8_t>& txExtra);

    /**
     * Generate STARK proof for a burn transaction
     * @param txHash Transaction hash
     * @param ethAddress Ethereum address to receive HEAT tokens
     * @param amount Burn amount
     * @param blockHeight Block height
     * @return true if proof generation started successfully
     */
    bool generateStarkProof(const std::string& txHash, 
                           const std::string& ethAddress, 
                           uint64_t amount, 
                           uint64_t blockHeight = 0);

    /**
     * Set callbacks for burn detection and proof generation
     */
    void setBurnDetectedCallback(BurnDetectedCallback callback);
    void setStarkProofGeneratedCallback(StarkProofGeneratedCallback callback);
    void setErrorCallback(ErrorCallback callback);

    /**
     * Enable/disable automatic STARK proof generation
     */
    void setAutoGenerateProofs(bool enabled);
    bool isAutoGenerateProofsEnabled() const;

    /**
     * Get the path to the STARK CLI binary
     */
    std::string getStarkCliPath() const;
    void setStarkCliPath(const std::string& path);

    /**
     * Check if burn detected callback is set
     */
    bool hasBurnDetectedCallback() const;

    /**
     * Trigger burn detected callback
     */
    void triggerBurnDetectedCallback(const std::string& txHash, uint64_t amount, const std::string& ethAddress);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
    
    // Helper functions
    BurnTransactionData parseHeatCommitment(const std::vector<uint8_t>& txExtra, size_t pos);
    BurnTransactionData parseYieldCommitment(const std::vector<uint8_t>& txExtra, size_t pos);
    
public:
    static std::string extractEthereumAddress(const std::string& metadata);
};

/**
 * Integrated Burn Transaction Manager
 * Manages burn transactions within the wallet context
 */
class BurnTransactionManager {
public:
    BurnTransactionManager();
    ~BurnTransactionManager();

    /**
     * Initialize the burn transaction manager
     * @param starkCliPath Path to the STARK CLI binary
     * @return true if initialization successful
     */
    bool initialize(const std::string& starkCliPath = "");

    /**
     * Process a transaction for burn detection
     * Called automatically when transactions are processed
     * @param txHash Transaction hash
     * @param txExtra Transaction extra data
     * @param amount Transaction amount
     */
    void processTransaction(const std::string& txHash, 
                          const std::vector<uint8_t>& txExtra, 
                          uint64_t amount);

    /**
     * Get burn transaction handler
     */
    BurnTransactionHandler& getHandler();

    /**
     * Set callbacks
     */
    void setBurnDetectedCallback(BurnTransactionHandler::BurnDetectedCallback callback);
    void setStarkProofGeneratedCallback(BurnTransactionHandler::StarkProofGeneratedCallback callback);
    void setErrorCallback(BurnTransactionHandler::ErrorCallback callback);

    /**
     * Enable/disable automatic processing
     */
    void setAutoProcessing(bool enabled);
    bool isAutoProcessingEnabled() const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace CryptoNote
