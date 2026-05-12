// Copyright (c) 2017-2025 Fuego Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include "../Wallet/DIGMWalletIntegration.h"
#include "../Rpc/DIGMRpcHandler.h"

namespace CryptoNote {

class DIGMSimpleWalletExtension {
public:
    DIGMSimpleWalletExtension();
    ~DIGMSimpleWalletExtension();

    // Initialize DIGM scanner
    bool initialize(const std::string& wallet_address);
    
    // DIGM balance commands
    bool show_digm_balance(const std::vector<std::string>& args);
    bool show_digm_transactions(const std::vector<std::string>& args);
    bool show_digm_outputs(const std::vector<std::string>& args);
    
    // DIGM transfer commands
    bool transfer_digm(const std::vector<std::string>& args);
    bool release_album(const std::vector<std::string>& args);
    bool update_album(const std::vector<std::string>& args);
    
    // DIGM info commands
    bool show_digm_info(const std::vector<std::string>& args);
    bool list_albums(const std::vector<std::string>& args);
    bool info_album(const std::vector<std::string>& args);
    
    // Scanning and synchronization
    bool scan_digm_transactions();
    bool refresh_digm_balance();
    
    // Utility functions
    std::string get_digm_balance_str() const;
    std::string get_digm_transaction_history_str() const;
    
    // Command help
    std::string get_digm_commands_str() const;

private:
    std::unique_ptr<IDIGMWalletScanner> m_scanner;
    std::unique_ptr<IDIGMRpcHandler> m_rpc_handler;
    std::string m_wallet_address;
    
    // Cached data
    mutable std::unordered_map<std::string, DIGMBalance> m_balance_cache;
    mutable std::unordered_map<std::string, std::vector<DIGMTransaction>> m_transaction_cache;
    
    // Helper functions
    bool parse_digm_amount(const std::string& amount_str, uint64_t& amount);
    bool validate_digm_address(const std::string& address);
    std::string format_digm_amount(uint64_t amount) const;
    std::string format_digm_transaction(const DIGMTransaction& tx) const;
};

} // namespace CryptoNote
