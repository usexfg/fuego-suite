// Copyright (c) 2017-2025 Fuego Developers
// Copyright (c) 2018-2019 Conceal Network & Conceal Devs
// Copyright (c) 2014-2017 The XDN developers
// Copyright (c) 2012-2018 The CryptoNote developers
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

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <boost/asio.hpp>
#include <boost/filesystem.hpp>

#ifdef ENABLE_TOR_LIB
#include <tor/tor.h>
#endif

namespace CryptoNote {

// Forward declarations
class FuegoTorManager;
class FuegoTorConnection;

/**
 * @brief FuegoTor connection status enumeration
 */
enum class FuegoTorStatus {
    DISCONNECTED,   // Not connected to Tor
    CONNECTING,     // Attempting to connect
    CONNECTED,      // Successfully connected
    ERROR,          // Connection error
    UNKNOWN         // Status unknown
};

/**
 * @brief FuegoTor configuration structure
 */
struct FuegoTorConfig {
    bool enabled = false;                    // Enable FuegoTor integration
    std::string socksHost = "127.0.0.1";     // SOCKS5 proxy host
    uint16_t socksPort = 9050;              // SOCKS5 proxy port
    std::string controlHost = "127.0.0.1";  // Tor control host
    uint16_t controlPort = 9051;            // Tor control port
    std::string dataDirectory = "";         // Tor data directory
    std::string hiddenServiceDir = "";      // Hidden service directory
    uint16_t hiddenServicePort = 8081;      // Hidden service port
    bool autoStart = false;                 // Auto-start Tor if not running
    uint32_t connectionTimeout = 30000;    // Connection timeout (ms)
    uint32_t circuitTimeout = 60000;       // Circuit timeout (ms)
    bool enableHiddenService = false;       // Enable hidden service
    std::string hiddenServiceAddress = "";  // Hidden service address
    
    // Tor authentication
    std::string controlPassword = "";        // Control connection password
    bool cookieAuthentication = true;        // Use cookie authentication
    
    // Circuit configuration
    uint32_t maxCircuitBuildTime = 60000;   // Maximum time to build a circuit (ms)
    uint32_t circuitIdleTime = 300000;      // Time before circuit is considered idle (ms)
    std::vector<std::string> exitNodes;     // Preferred exit node fingerprints
    std::vector<std::string> entryNodes;    // Preferred entry node fingerprints
    std::vector<std::string> excludeNodes;  // Nodes to exclude from circuits
    
    // Stream isolation
    bool streamIsolation = true;             // Enable stream isolation
    std::string streamIsolationGroupId = "fuego"; // Stream isolation group ID
    
    // Logging
    std::string logLevel = "NOTICE";         // Tor log level
    std::string logFile = "";                // Tor log file path
};

/**
 * @brief FuegoTor connection information
 */
struct FuegoTorConnectionInfo {
    std::string address;                     // Tor hidden service address
    uint16_t port;                          // Port
    std::string privateKey;                  // Hidden service private key
    std::string serviceId;                  // Hidden service ID
    bool isConnected;                        // Connection status
    FuegoTorStatus status;                   // Detailed status
    std::string errorMessage;                // Error message if any
};

/**
 * @brief Callback for FuegoTor status changes
 */
using FuegoTorStatusCallback = std::function<void(FuegoTorStatus, const std::string&)>;

/**
 * @brief Main FuegoTor integration class
 * 
 * This class provides integration with Tor network for privacy features.
 * It supports both libtor integration and SOCKS5 proxy mode.
 */
class TorIntegration {
public:
    /**
     * @brief Constructor
     */
    TorIntegration();
    
    /**
     * @brief Destructor
     */
    ~TorIntegration();
    
    /**
     * @brief Initialize FuegoTor with configuration
     * 
     * @param config The configuration to use
     * @return true if initialization succeeded, false otherwise
     */
    bool initialize(const FuegoTorConfig& config);
    
    /**
     * @brief Shutdown FuegoTor
     */
    void shutdown();
    
    /**
     * @brief Start FuegoTor connection
     * 
     * @return true if connection started successfully, false otherwise
     */
    bool start();
    
    /**
     * @brief Stop FuegoTor connection
     */
    void stop();
    
    /**
     * @brief Get current connection status
     * 
     * @return Current connection status
     */
    FuegoTorStatus getStatus() const;
    
    /**
     * @brief Get connection information
     * 
     * @return Connection information
     */
    FuegoTorConnectionInfo getConnectionInfo() const;
    
    /**
     * @brief Set status change callback
     * 
     * @param callback The callback to call on status changes
     */
    void setStatusCallback(FuegoTorStatusCallback callback);
    
    /**
     * @brief Create a new hidden service
     * 
     * @param servicePort The port to map to the hidden service
     * @param servicePath Path to store the hidden service data
     * @return Connection information for the created service
     */
    FuegoTorConnectionInfo createHiddenService(uint16_t servicePort, const std::string& servicePath = "");
    
    /**
     * @brief Connect to a Tor hidden service
     * 
     * @param address The hidden service address
     * @param port The port to connect to
     * @return true if connection succeeded, false otherwise
     */
    bool connectToHiddenService(const std::string& address, uint16_t port);
    
    /**
     * @brief Get SOCKS5 proxy endpoint
     * 
     * @return SOCKS5 proxy endpoint in format "host:port"
     */
    std::string getSocksEndpoint() const;
    
    /**
     * @brief Update configuration
     * 
     * @param config The new configuration
     * @return true if configuration updated successfully, false otherwise
     */
    bool updateConfig(const FuegoTorConfig& config);
    
    /**
     * @brief Get current configuration
     * 
     * @return Current configuration
     */
    FuegoTorConfig getConfig() const;
    
#ifdef ENABLE_TOR_LIB
    /**
     * @brief Get Tor control connection
     * 
     * @return Tor control connection handle (when using libtor)
     */
    tor_control_connection_t* getTorControlConnection();
#endif
    
    /**
     * @brief Get error message if available
     * 
     * @return Error message
     */
    std::string getErrorMessage() const;
    
    /**
     * @brief Check if FuegoTor is running
     * 
     * @return true if FuegoTor is running
     */
    bool isRunning() const;
    
    /**
     * @brief Get current circuit information
     * 
     * @return Vector of circuit information strings
     */
    std::vector<std::string> getCircuitInfo() const;
    
    /**
     * @brief Build a new circuit
     * 
     * @return true if circuit build started successfully, false otherwise
     */
    bool buildNewCircuit();
    
    /**
     * @brief Close all circuits
     */
    void closeAllCircuits();
    
    /**
     * @brief Check if libtor is being used
     * 
     * @return true if libtor is being used, false if SOCKS5 proxy mode
     */
    bool usingLibtor() const;

private:
    /**
     * @brief Worker thread function
     */
    void workerThread();
    
    /**
     * @brief Start Tor process (when not using libtor)
     * 
     * @return true if process started successfully, false otherwise
     */
    bool startTorProcess();
    
    /**
     * @brief Stop Tor process (when not using libtor)
     */
    void stopTorProcess();
    
    /**
     * @brief Initialize libtor (when available)
     * 
     * @return true if libtor initialized successfully, false otherwise
     */
    bool initializeLibtor();
    
    /**
     * @brief Shutdown libtor (when available)
     */
    void shutdownLibtor();
    
    /**
     * @brief Connect to Tor control port
     * 
     * @return true if connection succeeded, false otherwise
     */
    bool connectToControlPort();
    
    /**
     * @brief Disconnect from Tor control port
     */
    void disconnectFromControlPort();
    
    /**
     * @brief Authenticate with Tor control port
     * 
     * @return true if authentication succeeded, false otherwise
     */
    bool authenticateWithControlPort();
    
    /**
     * @brief Create SOCKS5 proxy connection
     * 
     * @return true if proxy connection established successfully, false otherwise
     */
    bool createSocksProxy();
    
    /**
     * @brief Update status
     * 
     * @param newStatus The new status
     * @param errorMessage Error message if any
     */
    void updateStatus(FuegoTorStatus newStatus, const std::string& errorMessage = "");
    
    /**
     * @brief Initialize hidden service
     * 
     * @param servicePort Port for the service
     * @param servicePath Path for service data
     * @return true if initialization succeeded, false otherwise
     */
    bool initializeHiddenService(uint16_t servicePort, const std::string& servicePath);
    
    /**
     * @brief Check if Tor is running on the system
     * 
     * @return true if Tor is running, false otherwise
     */
    bool isTorRunning() const;

private:
    // Configuration
    FuegoTorConfig m_config;
    mutable std::mutex m_configMutex;
    
    // Status
    std::atomic<FuegoTorStatus> m_status;
    std::string m_lastError;
    mutable std::mutex m_statusMutex;
    FuegoTorStatusCallback m_statusCallback;
    
    // Worker thread
    std::thread m_workerThread;
    std::atomic<bool> m_running;
    std::condition_variable m_cv;
    std::mutex m_cvMutex;
    
    // Connection info
    FuegoTorConnectionInfo m_connectionInfo;
    mutable std::mutex m_connectionMutex;
    
    // libtor integration
#ifdef ENABLE_TOR_LIB
    tor_control_connection_t* m_torControlConnection;
    tor_main_configuration_t* m_torMainConfig;
    std::atomic<bool> m_libtorInitialized;
#endif
    
    // SOCKS5 proxy
    std::unique_ptr<boost::asio::io_service> m_ioService;
    std::unique_ptr<boost::asio::io_service::work> m_work;
    std::thread m_ioThread;
    
    // Tor process
    std::string m_torExecutablePath;
    std::string m_torProcessId;
    std::atomic<bool> m_torProcessRunning;
    
    // Hidden services
    struct HiddenService {
        uint16_t servicePort;
        std::string servicePath;
        std::string serviceId;
        std::string privateKey;
    };
    std::vector<HiddenService> m_hiddenServices;
    mutable std::mutex m_hiddenServicesMutex;
};

} // namespace CryptoNote
