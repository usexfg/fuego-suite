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
}

namespace CryptoNote {

/**
 * @brief FuegoTor connection status enumeration
 */
enum class FuegoTorStatus {
    DISCONNECTED = 0,  // Not connected to Tor
    CONNECTING,       // Attempting to connect
    CONNECTED,        // Successfully connected
    ERROR             // Connection error
};

/**
 * @brief FuegoTor configuration structure
 */
struct FuegoTorConfig {
    bool enabled;                             // Enable FuegoTor integration
    const char* socksHost;                    // SOCKS5 proxy host
    uint16_t socksPort;                      // SOCKS5 proxy port
    const char* controlHost;                  // Tor control host
    uint16_t controlPort;                    // Tor control port
    const char* dataDirectory;                // Tor data directory
    const char* hiddenServiceDir;             // Hidden service directory
    uint16_t hiddenServicePort;               // Hidden service port
    bool autoStart;                           // Auto-start Tor if not running
    uint32_t connectionTimeout;               // Connection timeout (ms)
    uint32_t circuitTimeout;                  // Circuit timeout (ms)
    bool enableHiddenService;                 // Enable hidden service
    const char* hiddenServiceAddress;         // Hidden service address
};

/**
 * @brief FuegoTor connection information
 */
struct FuegoTorConnectionInfo {
    char address[256];                        // Connection address
    uint16_t port;                           // Connection port
    char onionAddress[64];                   // Onion address (if applicable)
    FuegoTorStatus status;                   // Connection status
    uint32_t latency;                        // Connection latency (ms)
    char errorMessage[256];                  // Error message (if any)
};

/**
 * @brief FuegoTor statistics
 */
struct FuegoTorStats {
    uint32_t totalConnections;                // Total connections made
    uint32_t successfulConnections;           // Successful connections
    uint32_t failedConnections;              // Failed connections
    uint32_t bytesTransferred;               // Total bytes transferred
    uint32_t averageLatency;                 // Average connection latency
    uint32_t circuitCount;                   // Active circuit count
    char torVersion[32];                     // Tor version string
};

/**
 * @brief FuegoTor manager operations
 */
class FuegoTorManager {
public:
    /**
     * @brief Constructor
     * @param config FuegoTor configuration
     */
    explicit FuegoTorManager(const FuegoTorConfig& config);
    
    /**
     * @brief Destructor
     */
    ~FuegoTorManager();
    
    /**
     * @brief Initialize FuegoTor integration
     * @return true if initialization successful
     */
    bool initialize();
    
    /**
     * @brief Shutdown FuegoTor integration
     */
    void shutdown();
    
    /**
     * @brief Check if Tor is available and running
     * @return true if Tor is available
     */
    bool isTorAvailable() const;
    
    /**
     * @brief Get current FuegoTor status
     * @return Current status
     */
    FuegoTorStatus getStatus() const;
    
    /**
     * @brief Get FuegoTor statistics
     * @return Current statistics
     */
    FuegoTorStats getStats() const;
    
    /**
     * @brief Create a Tor connection
     * @param address Target address
     * @param port Target port
     * @param info Output connection information
     * @return true if connection created successfully
     */
    bool createConnection(const char* address, uint16_t port, FuegoTorConnectionInfo& info);
    
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
    bool updateConfig(const FuegoTorConfig& config);
    
    /**
     * @brief Get current configuration
     * @return Current configuration
     */
    FuegoTorConfig getConfig() const;

private:
    FuegoTorConfig m_config;
    FuegoTorStatus m_status;
    FuegoTorStats m_stats;
    bool m_initialized;
    
    /**
     * @brief Start Tor process (stub for compatibility)
     * @return true if successful
     */
    bool startTorProcess();
    
    /**
     * @brief Stop Tor process (stub for compatibility)
     */
    void stopTorProcess();
    
    /**
     * @brief Send command to Tor (stub for compatibility)
     * @param command Command to send
     * @param response Output buffer for response
     * @param responseSize Size of output buffer
     * @return true if successful
     */
    bool sendTorCommand(const char* command, char* response, size_t responseSize);
    
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
    bool createSocksConnection(const char* address, uint16_t port, FuegoTorConnectionInfo& info);
};

/**
 * @brief Tor utility functions
 */
namespace TorUtils {
    /**
     * @brief Check if Tor is installed on system
     * @return true if Tor is installed
     */
    bool isTorInstalled();
    
    /**
     * @brief Get Tor version string
     * @param buffer Output buffer for version
     * @param bufferSize Size of the buffer
     * @return true if version was retrieved successfully
     */
    bool getTorVersion(char* buffer, size_t bufferSize);
    
    /**
     * @brief Get default Tor configuration
     * @return Default configuration
     */
    FuegoTorConfig getDefaultConfig();
}

} // namespace CryptoNote