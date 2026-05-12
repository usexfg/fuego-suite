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

#include "DIGMWalletIntegration.h"
#include <algorithm>
#include <cstring>

namespace CryptoNote {

// Simple DIGM Wallet Scanner Implementation
class DIGMWalletScanner : public IDIGMWalletScanner {
public:
    DIGMWalletScanner() = default;
    ~DIGMWalletScanner() = default;

    void initialize(const std::vector<std::string>& addresses) override {
        m_my_addresses = addresses;
        clear();
    }
    
    bool scanTransaction(const std::vector<uint8_t>& transactionData,
                        uint32_t blockHeight,
                        uint64_t timestamp,
                        const std::vector<std::string>& myAddresses) override {
        if (!isDIGMTransaction(transactionData)) {
            return false;
        }
        
        // Parse DIGM outputs from transaction data
        auto digmOutputs = parseDIGMOutputs(transactionData, blockHeight, myAddresses);
        
        // Create transaction info
        DIGMTransaction txInfo;
        txInfo.block_height = blockHeight;
        txInfo.timestamp = timestamp;
        txInfo.is_mint = false;
        txInfo.is_burn = false;
        txInfo.is_transfer = true;
        txInfo.fee = 0;
        
        // Determine transaction type based on outputs
        uint64_t totalAmount = 0;
        for (const auto& output : digmOutputs) {
            totalAmount += output.digm_amount;
        }
        
        if (totalAmount > 0) {
            txInfo.is_mint = true;
            txInfo.digm_amount = totalAmount;
        }
        
        // Store transaction
        m_transactions.push_back(txInfo);
        
        // Add outputs to address tracking
        for (const auto& output : digmOutputs) {
            if (isMyAddress(output.address)) {
                m_address_outputs[output.address].push_back(output);
                updateBalances(output.address);
            }
        }
        
        return true;
    }
    
    DIGMBalance getDIGMBalance(const std::string& address) const override {
        auto it = m_balance_cache.find(address);
        if (it != m_balance_cache.end()) {
            return it->second;
        }
        
        // Return empty balance if address not found
        DIGMBalance emptyBalance;
        emptyBalance.total_balance = 0;
        emptyBalance.available_balance = 0;
        emptyBalance.locked_balance = 0;
        emptyBalance.pending_balance = 0;
        return emptyBalance;
    }
    
    DIGMBalance getTotalDIGMBalance() const override {
        DIGMBalance totalBalance;
        totalBalance.total_balance = 0;
        totalBalance.available_balance = 0;
        totalBalance.locked_balance = 0;
        totalBalance.pending_balance = 0;
        
        for (const auto& address : m_my_addresses) {
            auto balance = getDIGMBalance(address);
            totalBalance.total_balance += balance.total_balance;
            totalBalance.available_balance += balance.available_balance;
            totalBalance.locked_balance += balance.locked_balance;
            totalBalance.pending_balance += balance.pending_balance;
        }
        
        return totalBalance;
    }
    
    std::vector<DIGMOutput> getDIGMOutputs(const std::string& address) const override {
        auto it = m_address_outputs.find(address);
        if (it != m_address_outputs.end()) {
            return it->second;
        }
        return std::vector<DIGMOutput>();
    }
    
    std::vector<DIGMTransaction> getDIGMTransactionHistory(const std::string& address) const override {
        std::vector<DIGMTransaction> history;
        
        for (const auto& tx : m_transactions) {
            // Check if this transaction involves the address
            bool involvesAddress = false;
            
            auto outputs = getDIGMOutputs(address);
            for (const auto& output : outputs) {
                if (output.address == address) {
                    involvesAddress = true;
                    break;
                }
            }
            
            if (involvesAddress) {
                history.push_back(tx);
            }
        }
        
        // Sort by timestamp (newest first)
        std::sort(history.begin(), history.end(), 
                  [](const DIGMTransaction& a, const DIGMTransaction& b) {
                      return a.timestamp > b.timestamp;
                  });
        
        return history;
    }
    
    bool isDIGMTransaction(const std::vector<uint8_t>& transactionData) const override {
        // Simple check for DIGM tag in transaction data
        if (transactionData.size() < 2) {
            return false;
        }
        
        // Look for DIGM tag (0x44) in the data
        for (size_t i = 0; i < transactionData.size() - 1; ++i) {
            if (transactionData[i] == DIGMConstants::DIGM_TX_EXTRA_TAG) {
                return true;
            }
        }
        
        return false;
    }
    
    void markOutputSpent(const Crypto::Hash& transactionHash, uint32_t outputIndex) override {
        // Find and mark the output as spent
        for (auto& addressOutputs : m_address_outputs) {
            for (auto& output : addressOutputs.second) {
                // Convert Crypto::Hash to string for comparison
                std::string outputHashStr = std::string(reinterpret_cast<const char*>(output.transaction_hash.data), sizeof(Crypto::Hash));
                std::string inputHashStr = std::string(reinterpret_cast<const char*>(transactionHash.data), sizeof(Crypto::Hash));
                if (outputHashStr == inputHashStr && output.output_index == outputIndex) {
                    output.is_spent = true;
                    updateBalances(addressOutputs.first);
                    break;
                }
            }
        }
    }
    
    void clear() override {
        m_address_outputs.clear();
        m_transactions.clear();
        m_balance_cache.clear();
    }

private:
    std::vector<std::string> m_my_addresses;
    std::unordered_map<std::string, std::vector<DIGMOutput>> m_address_outputs;
    std::vector<DIGMTransaction> m_transactions;
    mutable std::unordered_map<std::string, DIGMBalance> m_balance_cache;
    
    bool isMyAddress(const std::string& address) const {
        return std::find(m_my_addresses.begin(), m_my_addresses.end(), address) != m_my_addresses.end();
    }
    
    std::vector<DIGMOutput> parseDIGMOutputs(const std::vector<uint8_t>& transactionData,
                                            uint32_t blockHeight,
                                            const std::vector<std::string>& myAddresses) const {
        std::vector<DIGMOutput> digmOutputs;
        
        // Simplified parsing - in practice, you'd parse the actual transaction structure
        // For now, we'll create a placeholder output if DIGM tag is found
        
        if (isDIGMTransaction(transactionData)) {
            DIGMOutput output;
            output.token_id = DIGMConstants::DIGM_TOKEN_ID;
            output.amount = DIGMConstants::DIGM_AMOUNT_PER_OUTPUT; // 1 heat
            output.output_index = 0;
            output.block_height = blockHeight;
            output.is_spent = false;
            output.digm_amount = 1; // 1 DIGM per 10 heat
            output.is_mint_output = true;
            output.address = "DIGM_ADDRESS"; // Placeholder
            
            // Generate a simple hash for transaction_hash
            std::memset(&output.transaction_hash, 0, sizeof(Crypto::Hash));
            output.transaction_hash.data[0] = static_cast<uint8_t>(blockHeight & 0xFF);
            
            // Generate a simple public key
            std::memset(&output.public_key, 0, sizeof(Crypto::PublicKey));
            output.public_key.data[0] = static_cast<uint8_t>(blockHeight & 0xFF);
            
            digmOutputs.push_back(output);
        }
        
        return digmOutputs;
    }
    
    void updateBalances(const std::string& address) const {
        DIGMBalance balance;
        balance.total_balance = 0;
        balance.available_balance = 0;
        balance.locked_balance = 0;
        balance.pending_balance = 0;
        
        auto it = m_address_outputs.find(address);
        if (it != m_address_outputs.end()) {
            for (const auto& output : it->second) {
                balance.total_balance += output.digm_amount;
                
                if (!output.is_spent) {
                    balance.available_balance += output.digm_amount;
                }
            }
            balance.outputs = it->second;
        }
        
        m_balance_cache[address] = balance;
    }
};

// Factory function to create DIGM wallet scanner
std::unique_ptr<IDIGMWalletScanner> createDIGMWalletScanner() {
    return std::make_unique<DIGMWalletScanner>();
}

} // namespace CryptoNote
