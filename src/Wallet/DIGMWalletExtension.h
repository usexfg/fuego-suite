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

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include "DIGMWalletIntegration.h"
#include "../Rpc/DIGMRpcHandler.h"

namespace CryptoNote {

// DIGM Wallet Extension Interface
class IDIGMWalletExtension {
public:
    virtual ~IDIGMWalletExtension() = default;
    
    // Initialize DIGM support
    virtual void initialize() = 0;
    
    // DIGM balance queries
    virtual DIGMBalance getDIGMBalance(const std::string& address) const = 0;
    virtual DIGMBalance getTotalDIGMBalance() const = 0;
    
    // DIGM transaction queries
    virtual std::vector<DIGMTransaction> getDIGMTransactionHistory(const std::string& address) const = 0;
    virtual std::vector<DIGMTransaction> getAllDIGMTransactions() const = 0;
    
    // DIGM output queries
    virtual std::vector<DIGMOutput> getDIGMOutputs(const std::string& address) const = 0;
    virtual std::vector<DIGMOutput> getSpendableDIGMOutputs(const std::string& address, uint64_t amount) const = 0;
    
    // DIGM transaction creation
    virtual std::string createDIGMTransfer(const std::string& sourceAddress,
                                          const std::string& destinationAddress,
                                          uint64_t digmAmount,
                                          uint64_t fee) = 0;
    
    // Album operations
    virtual std::string releaseAlbum(const std::string& artistAddress,
                                    uint64_t albumId,
                                    uint64_t priceAtomic,
                                    const std::string& metadataHash,
                                    const std::vector<uint8_t>& signature) = 0;
    
    virtual std::string updateAlbum(const std::string& artistAddress,
                                   uint64_t albumId,
                                   uint64_t newPriceAtomic,
                                   const std::string& newMetadataHash,
                                   uint32_t updateReason,
                                   const std::vector<uint8_t>& signature) = 0;
    
    // DIGM scanning
    virtual void scanForDIGMOutputs() = 0;
    virtual bool isDIGMTransaction(const std::string& transactionHash) const = 0;
    
    // DIGM token info
    virtual std::string getDIGMTokenInfo() const = 0;
    
    // Utility functions
    virtual void refreshBalances() = 0;
    virtual void clearCache() = 0;
};

// DIGM Wallet Extension Implementation
class DIGMWalletExtension : public IDIGMWalletExtension {
public:
    DIGMWalletExtension();
    ~DIGMWalletExtension();
    
    void initialize() override;
    
    // DIGM balance queries
    DIGMBalance getDIGMBalance(const std::string& address) const override;
    DIGMBalance getTotalDIGMBalance() const override;
    
    // DIGM transaction queries
    std::vector<DIGMTransaction> getDIGMTransactionHistory(const std::string& address) const override;
    std::vector<DIGMTransaction> getAllDIGMTransactions() const override;
    
    // DIGM output queries
    std::vector<DIGMOutput> getDIGMOutputs(const std::string& address) const override;
    std::vector<DIGMOutput> getSpendableDIGMOutputs(const std::string& address, uint64_t amount) const override;
    
    // DIGM transaction creation
    std::string createDIGMTransfer(const std::string& sourceAddress,
                                  const std::string& destinationAddress,
                                  uint64_t digmAmount,
                                  uint64_t fee) override;
    
    // Album operations
    std::string releaseAlbum(const std::string& artistAddress,
                            uint64_t albumId,
                            uint64_t priceAtomic,
                            const std::string& metadataHash,
                            const std::vector<uint8_t>& signature) override;
    
    std::string updateAlbum(const std::string& artistAddress,
                           uint64_t albumId,
                           uint64_t newPriceAtomic,
                           const std::string& newMetadataHash,
                           uint32_t updateReason,
                           const std::vector<uint8_t>& signature) override;
    
    // DIGM scanning
    void scanForDIGMOutputs() override;
    bool isDIGMTransaction(const std::string& transactionHash) const override;
    
    // DIGM token info
    std::string getDIGMTokenInfo() const override;
    
    // Utility functions
    void refreshBalances() override;
    void clearCache() override;

private:
    std::shared_ptr<IDIGMWalletScanner> m_scanner;
    std::shared_ptr<IDIGMRpcHandler> m_rpc_handler;
    bool m_is_initialized;
    
    // Cached data
    mutable std::unordered_map<std::string, DIGMBalance> m_balance_cache;
    mutable std::unordered_map<std::string, std::vector<DIGMTransaction> > m_transaction_cache;
    mutable std::unordered_map<std::string, std::vector<DIGMOutput> > m_output_cache;
    
    // Helper methods
    void updateBalanceCache(const std::string& address) const;
    void updateTransactionCache(const std::string& address) const;
    void updateOutputCache(const std::string& address) const;
    bool validateDIGMAmount(uint64_t amount) const;
    bool validateAddress(const std::string& address) const;
};

// Factory function
std::unique_ptr<IDIGMWalletExtension> createDIGMWalletExtension();

} // namespace CryptoNote
