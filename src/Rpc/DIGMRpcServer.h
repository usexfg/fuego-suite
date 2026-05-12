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

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include "DIGMRpcHandler.h"

namespace CryptoNote {

// DIGM RPC Server Interface
class IDIGMRpcServer {
public:
    virtual ~IDIGMRpcServer() = default;
    
    // Initialize RPC server
    virtual void initialize() = 0;
    
    // Start RPC server
    virtual void start() = 0;
    
    // Stop RPC server
    virtual void stop() = 0;
    
    // Set DIGM RPC handler
    virtual void setDIGMHandler(std::shared_ptr<IDIGMRpcHandler> handler) = 0;
    
    // Get DIGM RPC handler
    virtual std::shared_ptr<IDIGMRpcHandler> getDIGMHandler() const = 0;
    
    // Handle DIGM RPC requests
    virtual std::string handleDIGMRequest(const std::string& request) = 0;
    
    // Check if RPC server is running
    virtual bool isRunning() const = 0;
};

// DIGM RPC Request Types
enum class DIGMRpcRequestType {
    GET_DIGM_INFO,
    GET_DIGM_BALANCE,
    GET_DIGM_TRANSACTIONS,
    GET_DIGM_OUTPUTS,
    CREATE_DIGM_TRANSFER,
    CREATE_DIGM_BURN,
    SCAN_DIGM_OUTPUTS,
    UNKNOWN
};

// DIGM RPC Request
struct DIGMRpcRequest {
    DIGMRpcRequestType type;
    std::string address;
    uint64_t amount;
    uint64_t fee;
    std::string destination_address;
    std::string source_address;
    std::string transaction_hash;
    std::string request_id;
    std::string method;
    std::string params;
};

// DIGM RPC Response
struct DIGMRpcResponse {
    std::string request_id;
    std::string result;
    std::string error;
    bool success;
    
    DIGMRpcResponse() : success(false) {}
};

// DIGM RPC Server Implementation
class DIGMRpcServer : public IDIGMRpcServer {
public:
    DIGMRpcServer();
    ~DIGMRpcServer();
    
    void initialize() override;
    void start() override;
    void stop() override;
    void setDIGMHandler(std::shared_ptr<IDIGMRpcHandler> handler) override;
    std::shared_ptr<IDIGMRpcHandler> getDIGMHandler() const override;
    std::string handleDIGMRequest(const std::string& request) override;
    bool isRunning() const override;

private:
    std::shared_ptr<IDIGMRpcHandler> m_digm_handler;
    bool m_is_running;
    
    // Request parsing
    DIGMRpcRequest parseRequest(const std::string& request);
    DIGMRpcRequestType parseRequestType(const std::string& method);
    
    // Response generation
    DIGMRpcResponse generateResponse(const DIGMRpcRequest& request);
    std::string serializeResponse(const DIGMRpcResponse& response);
    
    // Specific request handlers
    DIGMRpcResponse handleGetDIGMInfo(const DIGMRpcRequest& request);
    DIGMRpcResponse handleGetDIGMBalance(const DIGMRpcRequest& request);
    DIGMRpcResponse handleGetDIGMTransactions(const DIGMRpcRequest& request);
    DIGMRpcResponse handleGetDIGMOutputs(const DIGMRpcRequest& request);
    DIGMRpcResponse handleCreateDIGMTransfer(const DIGMRpcRequest& request);
    DIGMRpcResponse handleCreateDIGMBurn(const DIGMRpcRequest& request);
    DIGMRpcResponse handleScanDIGMOutputs(const DIGMRpcRequest& request);
    
    // Utility functions
    std::string createErrorResponse(const std::string& requestId, const std::string& error);
    std::string createSuccessResponse(const std::string& requestId, const std::string& result);
    
    // Parameter extraction helpers
    std::string extractAddressFromParams(const std::string& params);
    std::string extractSourceAddressFromParams(const std::string& params);
    std::string extractDestinationAddressFromParams(const std::string& params);
    uint64_t extractAmountFromParams(const std::string& params);
    uint64_t extractFeeFromParams(const std::string& params);
};

// Factory function to create DIGM RPC server
std::unique_ptr<IDIGMRpcServer> createDIGMRpcServer();

} // namespace CryptoNote

