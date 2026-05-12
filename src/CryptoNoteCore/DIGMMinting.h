// Copyright (c) 2017-2025 Fuego Developers
// Copyright (c) 2012-2018 The CryptoNote developers
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

#include <vector>
#include <string>
#include <cstdint>
#include "../../include/CryptoTypes.h"

namespace CryptoNote {

// Forward declarations
class ITransactionReader;
class ITransaction;

// DIGM Mint Transaction Information
struct DIGMMintInfo {
    uint32_t mint_height;              // Height at which minting is allowed
    uint64_t total_supply;             // Total DIGM supply (100,000)
    uint64_t total_xfg_amount;         // Total XFG amount (0.1 XFG)
    uint64_t amount_per_output;        // Amount per output (10 heat)
    uint32_t output_count;             // Number of outputs (100,000)
    bool is_minted;                    // Whether minting has occurred
    Crypto::Hash mint_transaction_hash; // Hash of mint transaction
    uint64_t mint_timestamp;           // Timestamp of mint transaction
};

// DIGM Mint Output
struct DIGMMintOutput {
    uint32_t output_index;             // Output index in transaction
    uint64_t amount;                   // Amount in heat (10 heat = 1 DIGM)
    Crypto::PublicKey public_key;      // Output public key
    std::string address;               // Destination address
    bool is_mint_output;               // Always true for mint outputs
};

// DIGM Minting System Interface
class IDIGMMinting {
public:
    virtual ~IDIGMMinting() = default;
    
    // Mint transaction creation
    virtual bool createMintTransaction(const std::string& destinationAddress,
                                      uint64_t fee,
                                      uint32_t mixin,
                                      std::vector<uint8_t>& transactionData,
                                      std::string& transactionHash) = 0;
    
    // Mint transaction validation
    virtual bool validateMintTransaction(const std::vector<uint8_t>& transactionData,
                                        uint32_t currentHeight) = 0;
    
    // Mint status queries
    virtual DIGMMintInfo getMintInfo() const = 0;
    virtual bool isMintingAllowed(uint32_t currentHeight) const = 0;
    virtual bool hasMintingOccurred() const = 0;
    
    // Mint transaction parsing
    virtual std::vector<DIGMMintOutput> parseMintOutputs(const std::vector<uint8_t>& transactionData) const = 0;
    virtual uint64_t getMintTransactionSize() const = 0;
    
    // Mint transaction verification
    virtual bool verifyMintTransaction(const std::vector<uint8_t>& transactionData,
                                      const Crypto::Hash& transactionHash) = 0;
    
    // Mint transaction storage
    virtual void storeMintTransaction(const Crypto::Hash& transactionHash,
                                     uint64_t timestamp) = 0;
    
    // Mint transaction retrieval
    virtual Crypto::Hash getMintTransactionHash() const = 0;
    virtual uint64_t getMintTimestamp() const = 0;
};

// DIGM Minting Constants
namespace DIGMMintingConstants {
    const uint32_t DIGM_MINT_HEIGHT = 1000000;           // Height at which minting is allowed
    const uint64_t DIGM_TOTAL_SUPPLY = 100000;           // 100,000 DIGM tokens
    const uint64_t DIGM_TOTAL_XFG_AMOUNT = 100000;       // 0.1 XFG total (100,000 * 0.000001)
    const uint64_t DIGM_AMOUNT_PER_OUTPUT = 10;          // 10 heat (0.000001 XFG) per output
    const uint32_t DIGM_OUTPUT_COUNT = 100000;           // 100,000 outputs
    const uint64_t DIGM_MINT_FEE = 1000000;              // 0.1 XFG fee for mint transaction
    const uint32_t DIGM_MINT_MIXIN = 10;                 // Mixin for mint transaction
    
    // Transaction size estimates
    const uint64_t DIGM_OUTPUT_SIZE = 100;               // ~100 bytes per output
    const uint64_t DIGM_MINT_TX_SIZE = DIGM_OUTPUT_COUNT * DIGM_OUTPUT_SIZE; // ~10MB
    const uint64_t DIGM_MINT_TX_SIZE_LIMIT = 20000000;   // 20MB limit for mint transaction
}

} // namespace CryptoNote

