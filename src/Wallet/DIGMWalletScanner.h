// Copyright (c) 2017-2025 Elderfire Privacy Council
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
#include <unordered_map>
#include <memory>
#include <string>
#include <iostream>
#include "../../include/CryptoTypes.h"

// Forward declarations
namespace CryptoNote {
    class DIGMTokenManager;
    class ITransactionReader;
    class ISerializer;
}

namespace CryptoNote {

// DIGM Output Information
struct DIGMOutputInfo {
    uint64_t token_id;
    uint64_t amount;                    // Amount in heat (0.0000001 XFG)
    uint32_t output_index;
    Crypto::Hash transaction_hash;
    uint32_t block_height;
    bool is_spent;
    Crypto::KeyImage key_image;
    Crypto::PublicKey public_key;
    std::string address;                // Owner address
    uint64_t unlock_time;
    
    // DIGM-specific metadata
    bool is_mint_output;
    uint64_t digm_amount;               // Amount in DIGM tokens (1 heat = 1 DIGM)
};

// DIGM Balance Information
struct DIGMBalanceInfo {
    uint64_t total_balance;             // Total DIGM balance
    uint64_t available_balance;         // Available for spending
    uint64_t locked_balance;            // Locked/unconfirmed
    uint64_t pending_balance;           // Pending transactions
    std::vector<DIGMOutputInfo> outputs; // All DIGM outputs
};

// DIGM Transaction Information
struct DIGMTransactionInfo {
    Crypto::Hash transaction_hash;
    uint32_t block_height;
    uint64_t timestamp;
    bool is_incoming;
    uint64_t digm_amount;
    std::string address;
    std::vector<DIGMOutputInfo> inputs;
    std::vector<DIGMOutputInfo> outputs;
    uint64_t fee;
    bool is_mint;
    bool is_burn;
    bool is_transfer;
};

// DIGM Wallet Scanner Class
class DIGMWalletScanner {
public:
    DIGMWalletScanner();
    ~DIGMWalletScanner();

    // Initialize scanner with wallet addresses
    void initialize(const std::vector<std::string>& addresses);
    
    // Scan transaction for DIGM outputs
    bool scanTransaction(const ITransactionReader& transaction, 
                        uint32_t blockHeight,
                        uint64_t timestamp,
                        const std::vector<std::string>& myAddresses);
    
    // Scan block for DIGM transactions
    void scanBlock(const std::vector<ITransactionReader>& transactions,
                   uint32_t blockHeight,
                   uint64_t timestamp,
                   const std::vector<std::string>& myAddresses);
    
    // Get DIGM balance for specific address
    DIGMBalanceInfo getDIGMBalance(const std::string& address) const;
    
    // Get total DIGM balance across all addresses
    DIGMBalanceInfo getTotalDIGMBalance() const;
    
    // Get all DIGM outputs for address
    std::vector<DIGMOutputInfo> getDIGMOutputs(const std::string& address) const;
    
    // Get DIGM transaction history
    std::vector<DIGMTransactionInfo> getDIGMTransactionHistory(const std::string& address) const;
    
    // Get all DIGM transactions
    std::vector<DIGMTransactionInfo> getAllDIGMTransactions() const;
    
    // Check if transaction contains DIGM outputs
    bool isDIGMTransaction(const ITransactionReader& transaction) const;
    
    // Parse DIGM outputs from transaction
    std::vector<DIGMOutputInfo> parseDIGMOutputs(const ITransactionReader& transaction,
                                                uint32_t blockHeight,
                                                const std::vector<std::string>& myAddresses) const;
    
    // Parse DIGM inputs from transaction
    std::vector<DIGMOutputInfo> parseDIGMInputs(const ITransactionReader& transaction) const;
    
    // Update output spent status
    void markOutputSpent(const Crypto::Hash& transactionHash, uint32_t outputIndex);
    
    // Clear all data
    void clear();
    
    // Save/load scanner state
    void save(std::ostream& os) const;
    void load(std::istream& is);

private:
    // Internal data structures
    std::unordered_map<std::string, std::vector<DIGMOutputInfo>> m_address_outputs;
    std::unordered_map<Crypto::Hash, DIGMTransactionInfo> m_transactions;
    std::vector<std::string> m_my_addresses;
    
    // DIGM token manager
    std::unique_ptr<DIGMTokenManager> m_digm_manager;
    
    // Helper functions
    bool isMyAddress(const std::string& address) const;
    bool parseDIGMTxExtra(const std::vector<uint8_t>& tx_extra, uint64_t& digm_amount, bool& is_mint) const;
    Crypto::PublicKey derivePublicKey(const Crypto::PublicKey& spendPublicKey, uint32_t outputIndex) const;
    std::string deriveAddress(const Crypto::PublicKey& publicKey) const;
    void updateBalances(const std::string& address);
    
    // Balance tracking
    mutable std::unordered_map<std::string, DIGMBalanceInfo> m_balance_cache;
    mutable bool m_balance_cache_dirty;
    
    // Serialization helpers
    void serializeOutputInfo(ISerializer& s, DIGMOutputInfo& info);
    void serializeTransactionInfo(ISerializer& s, DIGMTransactionInfo& info);
    void serializeBalanceInfo(ISerializer& s, DIGMBalanceInfo& info);
};

// DIGM Wallet Integration Interface
class IDIGMWallet {
public:
    virtual ~IDIGMWallet() = default;
    
    // DIGM balance queries
    virtual DIGMBalanceInfo getDIGMBalance(const std::string& address) const = 0;
    virtual DIGMBalanceInfo getTotalDIGMBalance() const = 0;
    
    // DIGM transaction queries
    virtual std::vector<DIGMTransactionInfo> getDIGMTransactionHistory(const std::string& address) const = 0;
    virtual std::vector<DIGMTransactionInfo> getAllDIGMTransactions() const = 0;
    
    // DIGM output queries
    virtual std::vector<DIGMOutputInfo> getDIGMOutputs(const std::string& address) const = 0;
    virtual std::vector<DIGMOutputInfo> getSpendableDIGMOutputs(const std::string& address, uint64_t amount) const = 0;
    
    // DIGM transaction creation
    virtual size_t createDIGMTransfer(const std::string& sourceAddress,
                                     const std::string& destinationAddress,
                                     uint64_t digmAmount,
                                     uint64_t fee,
                                     std::string& transactionHash) = 0;
    
    virtual size_t createDIGMBurn(const std::string& sourceAddress,
                                 uint64_t digmAmount,
                                 std::string& transactionHash) = 0;
    
    // DIGM transaction management
    virtual void commitDIGMTransaction(size_t transactionId) = 0;
    virtual void rollbackDIGMTransaction(size_t transactionId) = 0;
    
    // DIGM scanning
    virtual void scanForDIGMOutputs() = 0;
    virtual bool isDIGMTransaction(const Crypto::Hash& transactionHash) const = 0;
};

} // namespace CryptoNote
