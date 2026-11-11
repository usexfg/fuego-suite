// Copyright (c) 2017-2025 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free & open source software distributed in the hope
// that it will be useful, but WITHOUT ANY WARRANTY; without even
// the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You may redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation. Fuego includes elements written
// by third parties. See file labeled LICENSE for more details.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

#pragma once

// Use C-style types to avoid C++ standard library issues
extern "C" {
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
}

namespace CryptoNote {

/**
 * @brief FuegoI2P connection status enumeration
 */
enum class FuegoI2PStatus {
    DISCONNECTED = 0,  // Not connected to I2P
    CONNECTING,       // Attempting to connect
    CONNECTED,        // Successfully connected
    ERROR             // Connection error
};

/**
 * @brief FuegoI2P configuration structure
 */
struct FuegoI2PConfig {
    bool enabled;                             // Enable FuegoI2P integration
    const char* socksHost;                    // SOCKS5 proxy host
    uint16_t socksPort;                      // SOCKS5 proxy port
    const char* dataDirectory;                // I2P data directory
    const char* hiddenServiceDir;             // Hidden service directory
    uint16_t hiddenServicePort;               // Hidden service port
    bool autoStart;                           // Auto-start I2P if not running
    uint32_t connectionTimeout;               // Connection timeout (ms)
    uint32_t circuitTimeout;                  // Circuit timeout (ms)
    bool enableHiddenService;                 // Enable hidden service
    const char* hiddenServiceAddress;         // Hidden service address
};

/**
 * @brief FuegoI2P connection information
 */
struct FuegoI2PConnectionInfo {
    char address[256];                        // Connection address
    uint16_t port;                           // Connection port
    char b32Address[64];                   // Base32 address (if applicable)
    FuegoI2PStatus status;                   // Connection status
    uint32_t latency;                        // Connection latency (ms)
    char errorMessage[256];                  // Error message (if any)
};

/**
 * @brief FuegoI2P statistics
 */
struct FuegoI2PStats {
    uint32_t totalConnections;                // Total connections made
    uint32_t successfulConnections;           // Successful connections
    uint32_t failedConnections;              // Failed connections
    uint32_t bytesTransferred;               // Total bytes transferred
    uint32_t averageLatency;                 // Average connection latency
    uint32_t tunnelCount;                   // Active tunnel count
    char i2pVersion[32];                     // I2P version string
};

/**
 * @brief FuegoI2P manager operations
 */
class FuegoI2PManager {
public:
    /**
     * @brief Constructor
     * @param config FuegoI2P configuration
     */
    explicit FuegoI2PManager(const FuegoI2PConfig& config);
    
    /**
     * @brief Destructor
     */
    ~FuegoI2PManager();
    
    /**
     * @brief Initialize FuegoI2P integration
     * @return true if initialization successful
     */
    bool initialize();
    
    /**
     * @brief Shutdown FuegoI2P integration
     */
    void shutdown();
    
    /**
     * @brief Check if I2P is available and running
     * @return true if I2P is available
     */
    bool isI2PAvailable() const;
    
    /**
     * @brief Get current FuegoI2P status
     * @return Current status
     */
    FuegoI2PStatus getStatus() const;
    
    /**
     * @brief Get FuegoI2P statistics
     * @return Current statistics
     */
    FuegoI2PStats getStats() const;
    
    /**
     * @brief Create an I2P connection
     * @param address Target address
     * @param port Target port
     * @param info Output connection information
     * @return true if connection created successfully
     */
    bool createConnection(const char* address, uint16_t port, FuegoI2PConnectionInfo& info);
    
    /**
     * @brief Get hidden service address
     * @param buffer Output buffer for address
     * @param bufferSize Size of the buffer
     * @return true if hidden service is available
     */
    bool getHiddenServiceAddress(char* buffer, size_t bufferSize) const;
    
    /**
     * @brief Update configuration
     * @param config New configuration
     * @return true if update successful
     */
    bool updateConfig(const FuegoI2PConfig& config);
    
    /**
     * @brief Get current configuration
     * @return Current configuration
     */
    FuegoI2PConfig getConfig() const;

private:
    FuegoI2PConfig m_config;
    FuegoI2PStatus m_status;
    FuegoI2PStats m_stats;
    bool m_initialized;
    
    /**
     * @brief Start I2P process (stub for compatibility)
     * @return true if successful
     */
    bool startI2PProcess();
    
    /**
     * @brief Stop I2P process (stub for compatibility)
     */
    void stopI2PProcess();
    
    /**
     * @brief Send command to I2P (stub for compatibility)
     * @param command Command to send
     * @param response Output buffer for response
     * @param responseSize Size of output buffer
     * @return true if successful
     */
    bool sendI2PCommand(const char* command, char* response, size_t responseSize);
    
    /**
     * @brief Test SOCKS5 connection
     * @return true if connection is successful
     */
    bool testSocksConnection();
    
    /**
     * @brief Create SOCKS5 connection
     * @param address Target address
     * @param port Target port
     * @param info Output connection information
     * @return true if connection is successful
     */
    bool createSocksConnection(const char* address, uint16_t port, FuegoI2PConnectionInfo& info);
};

/**
 * @brief I2P utility functions
 */
namespace I2PUtils {
    /**
     * @brief Check if I2P is installed on system
     * @return true if I2P is installed
     */
    bool isI2PInstalled();
    
    /**
     * @brief Get I2P version string
     * @param buffer Output buffer for version
     * @param bufferSize Size of the buffer
     * @return true if version was retrieved successfully
     */
    bool getI2PVersion(char* buffer, size_t bufferSize);
    
    /**
     * @brief Get default I2P configuration
     * @return Default configuration
     */
    FuegoI2PConfig getDefaultConfig();
}

} // namespace CryptoNote