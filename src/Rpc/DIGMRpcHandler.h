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
#include <cstdint>
#include "../../include/CryptoTypes.h"

namespace CryptoNote {

// DIGM Balance Information
struct DIGMBalanceInfo {
    uint64_t total_balance;             // Total DIGM balance
    uint64_t available_balance;         // Available for spending
    uint64_t locked_balance;            // Locked/unconfirmed
    uint64_t pending_balance;           // Pending transactions
    std::string address;                // Address
};

// DIGM Transaction Information
struct DIGMTransactionInfo {
    std::string transaction_hash;
    uint32_t block_height;
    uint64_t timestamp;
    bool is_incoming;
    uint64_t digm_amount;
    std::string address;
    uint64_t fee;
    bool is_mint;
    bool is_burn;
    bool is_transfer;
};

// DIGM Output Information
struct DIGMOutputInfo {
    uint64_t token_id;
    uint64_t amount;                    // Amount in heat (0.0000001 XFG)
    uint32_t output_index;
    std::string transaction_hash;
    uint32_t block_height;
    bool is_spent;
    std::string address;                // Owner address
    uint64_t digm_amount;               // Amount in DIGM tokens (1 heat = 1 DIGM)
    bool is_mint_output;
};

// DIGM RPC Handler Interface
class IDIGMRpcHandler {
public:
    virtual ~IDIGMRpcHandler() = default;
    
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
    virtual std::string createDIGMTransfer(const std::string& sourceAddress,
                                          const std::string& destinationAddress,
                                          uint64_t digmAmount,
                                          uint64_t fee) = 0;
    
    virtual std::string createDIGMBurn(const std::string& sourceAddress,
                                      uint64_t digmAmount) = 0;
    
    // DIGM scanning
    virtual void scanForDIGMOutputs() = 0;
    virtual bool isDIGMTransaction(const std::string& transactionHash) const = 0;
    
    // DIGM token info
    virtual std::string getDIGMTokenInfo() const = 0;
};

// DIGM Constants
#ifndef DIGM_TOKEN_ID_DEFINED
#define DIGM_TOKEN_ID_DEFINED
namespace DIGMConstants {
    const uint64_t DIGM_TOKEN_ID = 0x4449474D00000000;  // "DIGM" in hex
    const uint64_t DIGM_TOTAL_SUPPLY = 100000;          // 100,000 DIGM tokens
    const uint64_t DIGM_AMOUNT_PER_OUTPUT = 10;         // 10 heat (0.000001 XFG) = 1 DIGM
    const uint64_t DIGM_TOTAL_XFG_AMOUNT = 100000;      // 0.1 XFG total (100,000 * 0.000001)
    const uint8_t DIGM_TX_EXTRA_TAG = 0x0A;             // Album tag for DIGM in tx_extra
    
    const std::string DIGM_TOKEN_NAME = "DIGM";
    const uint32_t DIGM_MINT_HEIGHT = 1000000; // Example mint height
    
    // Atomic unit conversion
    const uint64_t XFG_TO_HEAT = 10000000;              // 1 XFG = 10,000,000 heat
    const uint64_t HEAT_TO_DIGM = 10;                   // 10 heat = 1 DIGM
}
#endif // DIGM_TOKEN_ID_DEFINED

} // namespace CryptoNote

