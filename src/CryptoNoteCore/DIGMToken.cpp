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

#include "DIGMToken.h"
#include <algorithm>
#include <cstring>

namespace CryptoNote {

DIGMTokenManager::DIGMTokenManager() : m_is_initialized(false) {
    // Initialize DIGM token info
    m_digm_token.token_id = DIGMConstants::DIGM_TOKEN_ID;
    m_digm_token.token_name = DIGMConstants::DIGM_TOKEN_NAME;
    m_digm_token.total_supply = DIGMConstants::DIGM_TOTAL_SUPPLY;
    m_digm_token.amount_per_output = DIGMConstants::DIGM_AMOUNT_PER_OUTPUT;
    m_digm_token.tx_extra_tag = DIGMConstants::DIGM_TX_EXTRA_TAG;
    m_digm_token.is_minted = false;
    m_digm_token.mint_height = DIGMConstants::DIGM_MINT_HEIGHT;
    
    // Initialize hash to zero
    std::memset(&m_digm_token.mint_transaction_hash, 0, sizeof(Crypto::Hash));
    
    m_is_initialized = true;
}

DIGMTokenManager::~DIGMTokenManager() {
    // Cleanup if needed
}

bool DIGMTokenManager::isValidDIGMToken(const DIGMTokenInfo& token) {
    return token.token_id == DIGMConstants::DIGM_TOKEN_ID &&
           token.token_name == DIGMConstants::DIGM_TOKEN_NAME &&
           token.total_supply == DIGMConstants::DIGM_TOTAL_SUPPLY &&
           token.amount_per_output == DIGMConstants::DIGM_AMOUNT_PER_OUTPUT &&
           token.tx_extra_tag == DIGMConstants::DIGM_TX_EXTRA_TAG;
}

bool DIGMTokenManager::isValidDIGMOutput(const DIGMOutput& output) {
    return output.token_id == DIGMConstants::DIGM_TOKEN_ID &&
           output.amount == DIGMConstants::DIGM_AMOUNT_PER_OUTPUT &&
           output.amount > 0;
}

bool DIGMTokenManager::isValidDIGMTransaction(const DIGMTransaction& transaction) {
    if (transaction.token_id != DIGMConstants::DIGM_TOKEN_ID) {
        return false;
    }
    
    // Validate inputs
    for (const auto& input : transaction.inputs) {
        if (!isValidDIGMOutput(input)) {
            return false;
        }
    }
    
    // Validate outputs
    for (const auto& output : transaction.outputs) {
        if (!isValidDIGMOutput(output)) {
            return false;
        }
    }
    
    // Check input/output balance
    uint64_t input_sum = 0;
    for (const auto& input : transaction.inputs) {
        input_sum += input.amount;
    }
    
    uint64_t output_sum = 0;
    for (const auto& output : transaction.outputs) {
        output_sum += output.amount;
    }
    
    // For mint transactions, inputs can be 0
    if (transaction.is_mint) {
        return output_sum <= DIGMConstants::DIGM_TOTAL_SUPPLY;
    }
    
    // For regular transfers, input must equal output (minus fee)
    return input_sum >= output_sum;
}

bool DIGMTokenManager::createDIGMToken(DIGMTokenInfo& token) {
    if (m_is_initialized) {
        token = m_digm_token;
        return true;
    }
    return false;
}

bool DIGMTokenManager::mintDIGMTokens(uint64_t amount, uint32_t height, const Crypto::Hash& tx_hash) {
    if (!m_is_initialized) {
        return false;
    }
    
    if (!validateDIGMAmount(amount)) {
        return false;
    }
    
    if (m_digm_token.is_minted) {
        return false; // Already minted
    }
    
    // Create mint outputs - each output is 10 heat (0.000001 XFG) = 1 DIGM
    for (uint64_t i = 0; i < amount; ++i) {
        DIGMOutput output;
        output.token_id = DIGMConstants::DIGM_TOKEN_ID;
        output.amount = DIGMConstants::DIGM_AMOUNT_PER_OUTPUT; // 10 heat = 1 DIGM
        output.output_index = static_cast<uint32_t>(i);
        output.transaction_hash = tx_hash;
        output.block_height = height;
        
        m_digm_outputs.push_back(output);
    }
    
    m_digm_token.is_minted = true;
    m_digm_token.mint_height = height;
    m_digm_token.mint_transaction_hash = tx_hash;
    
    return true;
}



bool DIGMTokenManager::transferDIGMTokens(const std::vector<DIGMOutput>& inputs, 
                                         const std::vector<DIGMOutput>& outputs) {
    if (!m_is_initialized) {
        return false;
    }
    
    // Validate inputs
    for (const auto& input : inputs) {
        if (!isValidDIGMOutput(input)) {
            return false;
        }
    }
    
    // Validate outputs
    for (const auto& output : outputs) {
        if (!isValidDIGMOutput(output)) {
            return false;
        }
    }
    
    // Check balance
    uint64_t input_sum = 0;
    for (const auto& input : inputs) {
        input_sum += input.amount;
    }
    
    uint64_t output_sum = 0;
    for (const auto& output : outputs) {
        output_sum += output.amount;
    }
    
    if (input_sum < output_sum) {
        return false; // Insufficient balance
    }
    
    return true;
}

uint64_t DIGMTokenManager::getDIGMBalance(const Crypto::PublicKey& address) {
    // This is a simplified implementation
    // In a real implementation, you'd need to track outputs by address
    uint64_t balance = 0;
    for (const auto& output : m_digm_outputs) {
        balance += output.amount;
    }
    return balance;
}

std::vector<DIGMOutput> DIGMTokenManager::getDIGMOutputs(const Crypto::PublicKey& address) {
    // This is a simplified implementation
    // In a real implementation, you'd need to track outputs by address
    return m_digm_outputs;
}

DIGMTokenInfo DIGMTokenManager::getDIGMTokenInfo() {
    return m_digm_token;
}

uint64_t DIGMTokenManager::getDIGMTotalSupply() {
    return DIGMConstants::DIGM_TOTAL_SUPPLY;
}

uint64_t DIGMTokenManager::getDIGMCirculatingSupply() {
    uint64_t circulating = 0;
    for (const auto& output : m_digm_outputs) {
        circulating += output.amount;
    }
    return circulating;
}

bool DIGMTokenManager::validateDIGMMintTransaction(const std::vector<uint8_t>& tx_extra) {
    if (tx_extra.size() < 2) {
        return false;
    }
    
    if (tx_extra[0] != DIGMConstants::DIGM_TX_EXTRA_TAG) {
        return false;
    }
    
    // Check if it's a mint transaction
    if (tx_extra.size() >= 3 && tx_extra[1] == 0x01) { // Mint flag
        uint64_t amount = parseDIGMAmount(tx_extra);
        return validateDIGMAmount(amount);
    }
    
    return false;
}

bool DIGMTokenManager::validateDIGMTransferTransaction(const std::vector<uint8_t>& tx_extra) {
    if (tx_extra.size() < 2) {
        return false;
    }
    
    if (tx_extra[0] != DIGMConstants::DIGM_TX_EXTRA_TAG) {
        return false;
    }
    
    // Check if it's a transfer transaction
    if (tx_extra.size() >= 3 && tx_extra[1] == 0x02) { // Transfer flag
        uint64_t amount = parseDIGMAmount(tx_extra);
        return validateDIGMAmount(amount);
    }
    
    return false;
}



bool DIGMTokenManager::isDIGMTransaction(const std::vector<uint8_t>& tx_extra) {
    if (tx_extra.size() < 1) {
        return false;
    }
    
    return tx_extra[0] == DIGMConstants::DIGM_TX_EXTRA_TAG;
}

uint64_t DIGMTokenManager::parseDIGMAmount(const std::vector<uint8_t>& tx_extra) {
    if (tx_extra.size() < 10) { // Minimum size for tag + flag + amount
        return 0;
    }
    
    // Parse 8-byte amount starting at position 2
    uint64_t amount = 0;
    for (int i = 0; i < 8; ++i) {
        amount |= static_cast<uint64_t>(tx_extra[2 + i]) << (i * 8);
    }
    
    return amount;
}

std::vector<uint8_t> DIGMTokenManager::createDIGMTxExtra(uint64_t amount, bool is_mint) {
    std::vector<uint8_t> tx_extra;
    
    // Add DIGM tag
    tx_extra.push_back(DIGMConstants::DIGM_TX_EXTRA_TAG);
    
    // Add transaction type flag
    if (is_mint) {
        tx_extra.push_back(0x01); // Mint
    } else {
        tx_extra.push_back(0x02); // Transfer
    }
    
    // Add amount (8 bytes, little-endian)
    for (int i = 0; i < 8; ++i) {
        tx_extra.push_back(static_cast<uint8_t>(amount & 0xFF));
        amount >>= 8;
    }
    
    return tx_extra;
}

bool DIGMTokenManager::validateDIGMTokenId(uint64_t token_id) {
    return token_id == DIGMConstants::DIGM_TOKEN_ID;
}

bool DIGMTokenManager::validateDIGMAmount(uint64_t amount) {
    return amount > 0 && amount <= DIGMConstants::DIGM_TOTAL_SUPPLY;
}

bool DIGMTokenManager::validateDIGMTotalSupply(uint64_t amount) {
    return amount <= DIGMConstants::DIGM_TOTAL_SUPPLY;
}

} // namespace CryptoNote
