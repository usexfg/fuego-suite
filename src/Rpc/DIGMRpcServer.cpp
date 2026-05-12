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

#include "DIGMRpcServer.h"
#include <sstream>
#include <iostream>
#include <memory>

namespace CryptoNote {

DIGMRpcServer::DIGMRpcServer() : m_is_running(false) {
}

DIGMRpcServer::~DIGMRpcServer() {
    stop();
}

void DIGMRpcServer::initialize() {
    // Initialize RPC server components
    m_is_running = false;
}

void DIGMRpcServer::start() {
    if (!m_is_running) {
        m_is_running = true;
        // todo start the RPC server
    }
}

void DIGMRpcServer::stop() {
    if (m_is_running) {
        m_is_running = false;
        // todo stop the RPC server
    }
}

void DIGMRpcServer::setDIGMHandler(std::shared_ptr<IDIGMRpcHandler> handler) {
    m_digm_handler = handler;
}

std::shared_ptr<IDIGMRpcHandler> DIGMRpcServer::getDIGMHandler() const {
    return m_digm_handler;
}

std::string DIGMRpcServer::handleDIGMRequest(const std::string& request) {
    try {
        DIGMRpcRequest parsedRequest = parseRequest(request);
        DIGMRpcResponse response = generateResponse(parsedRequest);
        return serializeResponse(response);
    } catch (const std::exception& e) {
        return createErrorResponse("", "Internal server error: " + std::string(e.what()));
    }
}

bool DIGMRpcServer::isRunning() const {
    return m_is_running;
}

DIGMRpcRequest DIGMRpcServer::parseRequest(const std::string& request) {
    DIGMRpcRequest parsedRequest;
    
    // Simplified JSON parsing - 
    // todo use a proper JSON library
    
    // Extract method from request
    size_t methodPos = request.find("\"method\":\"");
    if (methodPos != std::string::npos) {
        methodPos += 10; // Skip "method":"
        size_t methodEnd = request.find("\"", methodPos);
        if (methodEnd != std::string::npos) {
            parsedRequest.method = request.substr(methodPos, methodEnd - methodPos);
            parsedRequest.type = parseRequestType(parsedRequest.method);
        }
    }
    
    // Extract request ID
    size_t idPos = request.find("\"id\":\"");
    if (idPos != std::string::npos) {
        idPos += 6; // Skip "id":"
        size_t idEnd = request.find("\"", idPos);
        if (idEnd != std::string::npos) {
            parsedRequest.request_id = request.substr(idPos, idEnd - idPos);
        }
    }
    
    // Extract parameters (simplified)
    size_t paramsPos = request.find("\"params\":{");
    if (paramsPos != std::string::npos) {
        size_t paramsEnd = request.find("}", paramsPos);
        if (paramsEnd != std::string::npos) {
            parsedRequest.params = request.substr(paramsPos, paramsEnd - paramsPos + 1);
        }
    }
    
    return parsedRequest;
}

DIGMRpcRequestType DIGMRpcServer::parseRequestType(const std::string& method) {
    if (method == "get_digm_info") return DIGMRpcRequestType::GET_DIGM_INFO;
    if (method == "get_digm_balance") return DIGMRpcRequestType::GET_DIGM_BALANCE;
    if (method == "get_digm_transactions") return DIGMRpcRequestType::GET_DIGM_TRANSACTIONS;
    if (method == "get_digm_outputs") return DIGMRpcRequestType::GET_DIGM_OUTPUTS;
    if (method == "create_digm_transfer") return DIGMRpcRequestType::CREATE_DIGM_TRANSFER;
    if (method == "release_album") return DIGMRpcRequestType::CREATE_DIGM_TRANSFER; // Album release
    if (method == "update_album") return DIGMRpcRequestType::CREATE_DIGM_TRANSFER; // Album update
    if (method == "scan_digm_outputs") return DIGMRpcRequestType::SCAN_DIGM_OUTPUTS;
    
    return DIGMRpcRequestType::UNKNOWN;
}

DIGMRpcResponse DIGMRpcServer::generateResponse(const DIGMRpcRequest& request) {
    switch (request.type) {
        case DIGMRpcRequestType::GET_DIGM_INFO:
            return handleGetDIGMInfo(request);
        case DIGMRpcRequestType::GET_DIGM_BALANCE:
            return handleGetDIGMBalance(request);
        case DIGMRpcRequestType::GET_DIGM_TRANSACTIONS:
            return handleGetDIGMTransactions(request);
        case DIGMRpcRequestType::GET_DIGM_OUTPUTS:
            return handleGetDIGMOutputs(request);
        case DIGMRpcRequestType::CREATE_DIGM_TRANSFER:
            return handleCreateDIGMTransfer(request);
        case DIGMRpcRequestType::SCAN_DIGM_OUTPUTS:
            return handleScanDIGMOutputs(request);
        default:
            return DIGMRpcResponse();
    }
}

std::string DIGMRpcServer::serializeResponse(const DIGMRpcResponse& response) {
    std::stringstream ss;
    ss << "{";
    ss << "\"jsonrpc\":\"2.0\",";
    ss << "\"id\":\"" << response.request_id << "\",";
    
    if (response.success) {
        ss << "\"result\":" << response.result;
    } else {
        ss << "\"error\":{\"code\":-1,\"message\":\"" << response.error << "\"}";
    }
    
    ss << "}";
    return ss.str();
}

DIGMRpcResponse DIGMRpcServer::handleGetDIGMInfo(const DIGMRpcRequest& request) {
    DIGMRpcResponse response;
    response.request_id = request.request_id;
    
    if (!m_digm_handler) {
        response.success = false;
        response.error = "DIGM handler not available";
        return response;
    }
    
    try {
        std::string tokenInfo = m_digm_handler->getDIGMTokenInfo();
        response.success = true;
        response.result = tokenInfo;
    } catch (const std::exception& e) {
        response.success = false;
        response.error = "Failed to get DIGM info: " + std::string(e.what());
    }
    
    return response;
}

DIGMRpcResponse DIGMRpcServer::handleGetDIGMBalance(const DIGMRpcRequest& request) {
    DIGMRpcResponse response;
    response.request_id = request.request_id;
    
    if (!m_digm_handler) {
        response.success = false;
        response.error = "DIGM handler not available";
        return response;
    }
    
    try {
        // Extract address from params
        std::string address = extractAddressFromParams(request.params);
        if (address.empty()) {
            response.success = false;
            response.error = "Address parameter required";
            return response;
        }
        
        DIGMBalanceInfo balance = m_digm_handler->getDIGMBalance(address);
        
        std::stringstream ss;
        ss << "{";
        ss << "\"total_balance\":" << balance.total_balance << ",";
        ss << "\"available_balance\":" << balance.available_balance << ",";
        ss << "\"locked_balance\":" << balance.locked_balance << ",";
        ss << "\"pending_balance\":" << balance.pending_balance << ",";
        ss << "\"address\":\"" << balance.address << "\"";
        ss << "}";
        
        response.success = true;
        response.result = ss.str();
    } catch (const std::exception& e) {
        response.success = false;
        response.error = "Failed to get DIGM balance: " + std::string(e.what());
    }
    
    return response;
}

DIGMRpcResponse DIGMRpcServer::handleGetDIGMTransactions(const DIGMRpcRequest& request) {
    DIGMRpcResponse response;
    response.request_id = request.request_id;
    
    if (!m_digm_handler) {
        response.success = false;
        response.error = "DIGM handler not available";
        return response;
    }
    
    try {
        std::string address = extractAddressFromParams(request.params);
        std::vector<DIGMTransactionInfo> transactions;
        
        if (address.empty()) {
            transactions = m_digm_handler->getAllDIGMTransactions();
        } else {
            transactions = m_digm_handler->getDIGMTransactionHistory(address);
        }
        
        std::stringstream ss;
        ss << "[";
        for (size_t i = 0; i < transactions.size(); ++i) {
            if (i > 0) ss << ",";
            ss << "{";
            ss << "\"transaction_hash\":\"" << transactions[i].transaction_hash << "\",";
            ss << "\"block_height\":" << transactions[i].block_height << ",";
            ss << "\"timestamp\":" << transactions[i].timestamp << ",";
            ss << "\"is_incoming\":" << (transactions[i].is_incoming ? "true" : "false") << ",";
            ss << "\"digm_amount\":" << transactions[i].digm_amount << ",";
            ss << "\"address\":\"" << transactions[i].address << "\",";
            ss << "\"fee\":" << transactions[i].fee << ",";
            ss << "\"is_mint\":" << (transactions[i].is_mint ? "true" : "false") << ",";
            ss << "\"is_transfer\":" << (transactions[i].is_transfer ? "true" : "false");
            ss << "}";
        }
        ss << "]";
        
        response.success = true;
        response.result = ss.str();
    } catch (const std::exception& e) {
        response.success = false;
        response.error = "Failed to get DIGM transactions: " + std::string(e.what());
    }
    
    return response;
}

DIGMRpcResponse DIGMRpcServer::handleGetDIGMOutputs(const DIGMRpcRequest& request) {
    DIGMRpcResponse response;
    response.request_id = request.request_id;
    
    if (!m_digm_handler) {
        response.success = false;
        response.error = "DIGM handler not available";
        return response;
    }
    
    try {
        std::string address = extractAddressFromParams(request.params);
        if (address.empty()) {
            response.success = false;
            response.error = "Address parameter required";
            return response;
        }
        
        std::vector<DIGMOutputInfo> outputs = m_digm_handler->getDIGMOutputs(address);
        
        std::stringstream ss;
        ss << "[";
        for (size_t i = 0; i < outputs.size(); ++i) {
            if (i > 0) ss << ",";
            ss << "{";
            ss << "\"token_id\":" << outputs[i].token_id << ",";
            ss << "\"amount\":" << outputs[i].amount << ",";
            ss << "\"output_index\":" << outputs[i].output_index << ",";
            ss << "\"transaction_hash\":\"" << outputs[i].transaction_hash << "\",";
            ss << "\"block_height\":" << outputs[i].block_height << ",";
            ss << "\"is_spent\":" << (outputs[i].is_spent ? "true" : "false") << ",";
            ss << "\"address\":\"" << outputs[i].address << "\",";
            ss << "\"digm_amount\":" << outputs[i].digm_amount << ",";
            ss << "\"is_mint_output\":" << (outputs[i].is_mint_output ? "true" : "false");
            ss << "}";
        }
        ss << "]";
        
        response.success = true;
        response.result = ss.str();
    } catch (const std::exception& e) {
        response.success = false;
        response.error = "Failed to get DIGM outputs: " + std::string(e.what());
    }
    
    return response;
}

DIGMRpcResponse DIGMRpcServer::handleCreateDIGMTransfer(const DIGMRpcRequest& request) {
    DIGMRpcResponse response;
    response.request_id = request.request_id;
    
    if (!m_digm_handler) {
        response.success = false;
        response.error = "DIGM handler not available";
        return response;
    }
    
    try {
        // Extract transfer parameters
        std::string sourceAddress = extractSourceAddressFromParams(request.params);
        std::string destinationAddress = extractDestinationAddressFromParams(request.params);
        uint64_t amount = extractAmountFromParams(request.params);
        uint64_t fee = extractFeeFromParams(request.params);
        
        if (sourceAddress.empty() || destinationAddress.empty() || amount == 0) {
            response.success = false;
            response.error = "Invalid transfer parameters";
            return response;
        }
        
        std::string transactionHash = m_digm_handler->createDIGMTransfer(
            sourceAddress, destinationAddress, amount, fee);
        
        std::stringstream ss;
        ss << "{";
        ss << "\"transaction_hash\":\"" << transactionHash << "\"";
        ss << "}";
        
        response.success = true;
        response.result = ss.str();
    } catch (const std::exception& e) {
        response.success = false;
        response.error = "Failed to create DIGM transfer: " + std::string(e.what());
    }
    
    return response;
}

DIGMRpcResponse DIGMRpcServer::handleScanDIGMOutputs(const DIGMRpcRequest& request) {
    DIGMRpcResponse response;
    response.request_id = request.request_id;
    
    if (!m_digm_handler) {
        response.success = false;
        response.error = "DIGM handler not available";
        return response;
    }
    
    try {
        m_digm_handler->scanForDIGMOutputs();
        
        response.success = true;
        response.result = "{\"status\":\"scan_completed\"}";
    } catch (const std::exception& e) {
        response.success = false;
        response.error = "Failed to scan DIGM outputs: " + std::string(e.what());
    }
    
    return response;
}

std::string DIGMRpcServer::createErrorResponse(const std::string& requestId, const std::string& error) {
    std::stringstream ss;
    ss << "{";
    ss << "\"jsonrpc\":\"2.0\",";
    ss << "\"id\":\"" << requestId << "\",";
    ss << "\"error\":{\"code\":-1,\"message\":\"" << error << "\"}";
    ss << "}";
    return ss.str();
}

std::string DIGMRpcServer::createSuccessResponse(const std::string& requestId, const std::string& result) {
    std::stringstream ss;
    ss << "{";
    ss << "\"jsonrpc\":\"2.0\",";
    ss << "\"id\":\"" << requestId << "\",";
    ss << "\"result\":" << result;
    ss << "}";
    return ss.str();
}

// Helper methods for parameter extraction
std::string DIGMRpcServer::extractAddressFromParams(const std::string& params) {
    // Simplified parameter extraction - in practice, use proper JSON parsing
    size_t pos = params.find("\"address\":\"");
    if (pos != std::string::npos) {
        pos += 11; // Skip "address":"
        size_t end = params.find("\"", pos);
        if (end != std::string::npos) {
            return params.substr(pos, end - pos);
        }
    }
    return "";
}

std::string DIGMRpcServer::extractSourceAddressFromParams(const std::string& params) {
    size_t pos = params.find("\"source_address\":\"");
    if (pos != std::string::npos) {
        pos += 17; // Skip "source_address":"
        size_t end = params.find("\"", pos);
        if (end != std::string::npos) {
            return params.substr(pos, end - pos);
        }
    }
    return "";
}

std::string DIGMRpcServer::extractDestinationAddressFromParams(const std::string& params) {
    size_t pos = params.find("\"destination_address\":\"");
    if (pos != std::string::npos) {
        pos += 22; // Skip "destination_address":"
        size_t end = params.find("\"", pos);
        if (end != std::string::npos) {
            return params.substr(pos, end - pos);
        }
    }
    return "";
}

uint64_t DIGMRpcServer::extractAmountFromParams(const std::string& params) {
    size_t pos = params.find("\"amount\":");
    if (pos != std::string::npos) {
        pos += 8; // Skip "amount":"
        size_t end = params.find_first_of(",}", pos);
        if (end != std::string::npos) {
            std::string amountStr = params.substr(pos, end - pos);
            return std::stoull(amountStr);
        }
    }
    return 0;
}

uint64_t DIGMRpcServer::extractFeeFromParams(const std::string& params) {
    size_t pos = params.find("\"fee\":");
    if (pos != std::string::npos) {
        pos += 5; // Skip "fee":"
        size_t end = params.find_first_of(",}", pos);
        if (end != std::string::npos) {
            std::string feeStr = params.substr(pos, end - pos);
            return std::stoull(feeStr);
        }
    }
    return 0;
}

// Factory function
std::unique_ptr<IDIGMRpcServer> createDIGMRpcServer() {
    return std::make_unique<DIGMRpcServer>();
}

} // namespace CryptoNote
