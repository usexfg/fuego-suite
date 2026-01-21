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

extern "C" {
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#ifdef _WIN32
// Windows includes with proper guards against redefinitions
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <io.h>
// Define our compatibility macros only if not already defined
#ifndef _close
#define _close close
#endif
#ifndef _popen
#define _popen _popen
#endif
#ifndef _pclose
#define _pclose _pclose
#endif
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "wsock32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#endif
}

// Standard C++ includes
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <locale>

#include "../include/Fuegotor.h"
#include <mutex>
#include <thread>
#include <atomic>
#include <cstring>

// Boost includes for variant and optional support
#include <boost/variant.hpp>
#include <boost/optional.hpp>

// Fuego core includes
#include "Common/StringTools.h"
#include "Logging/ILogger.h"

namespace CryptoNote {

// FuegoTorManager Implementation
FuegoTorManager::FuegoTorManager(const FuegoTorConfig& config) 
    : m_config(config), m_status(FuegoTorStatus::DISCONNECTED), m_initialized(false) {
    // Initialize stats
    memset(&m_stats, 0, sizeof(m_stats));
    
    // Set default values for config if not provided
    if (m_config.socksHost == nullptr) {
        m_config.socksHost = "127.0.0.1";
    }
    if (m_config.controlHost == nullptr) {
        m_config.controlHost = "127.0.0.1";
    }
}

FuegoTorManager::~FuegoTorManager() {
    shutdown();
}

bool FuegoTorManager::initialize() {
    if (m_initialized) {
        return true; // Already initialized
    }
    
    // Check if Tor is available
    if (!TorUtils::isTorInstalled()) {
        m_status = FuegoTorStatus::ERROR;
        return false;
    }
    
    // Initialize Tor connection
    if (!testSocksConnection()) {
        m_status = FuegoTorStatus::ERROR;
        return false;
    }
    
    m_status = FuegoTorStatus::CONNECTED;
    m_initialized = true;
    
    return true;
}

void FuegoTorManager::shutdown() {
    if (!m_initialized) {
        return;
    }
    
    // Clean up connections
    m_status = FuegoTorStatus::DISCONNECTED;
    m_initialized = false;
}

bool FuegoTorManager::isTorAvailable() const {
    return m_status == FuegoTorStatus::CONNECTED;
}

FuegoTorStatus FuegoTorManager::getStatus() const {
    return m_status;
}

FuegoTorStats FuegoTorManager::getStats() const {
    return m_stats;
}

// Stub implementations for Windows compatibility without libtor
bool FuegoTorManager::startTorProcess() {
    // In CI/build environments, return true as we don't need actual Tor
    return true;
}

void FuegoTorManager::stopTorProcess() {
    // Nothing to stop without actual Tor
}

bool FuegoTorManager::sendTorCommand(const char* command, char* response, size_t responseSize) {
    if (!command || !response || responseSize == 0) {
        return false;
    }
    response[0] = '\0';
    return false; // Command not available in SOCKS5-only mode
}

bool FuegoTorManager::createConnection(const char* address, uint16_t port, FuegoTorConnectionInfo& info) {
    if (!address) {
        return false;
    }
    
    // Initialize info
    memset(&info, 0, sizeof(info));
    strncpy(info.address, address, sizeof(info.address) - 1);
    info.address[sizeof(info.address) - 1] = '\0';
    info.port = port;
    info.status = FuegoTorStatus::CONNECTING;
    
    // Create SOCKS5 connection
    if (createSocksConnection(address, port, info)) {
        info.status = FuegoTorStatus::CONNECTED;
        m_stats.successfulConnections++;
    } else {
        info.status = FuegoTorStatus::ERROR;
        m_stats.failedConnections++;
    }
    
    m_stats.totalConnections++;
    
    return info.status == FuegoTorStatus::CONNECTED;
}

bool FuegoTorManager::getHiddenServiceAddress(char* buffer, size_t bufferSize) const {
    if (!buffer || bufferSize == 0) {
        return false;
    }
    
    if (m_config.hiddenServiceAddress) {
        strncpy(buffer, m_config.hiddenServiceAddress, bufferSize - 1);
        buffer[bufferSize - 1] = '\0';
        return true;
    }
    
    // No hidden service configured
    buffer[0] = '\0';
    return false;
}

bool FuegoTorManager::updateConfig(const FuegoTorConfig& config) {
    // Validate new configuration
    if (config.socksPort == 0) {
        return false;
    }
    
    if (config.controlPort == 0) {
        return false;
    }
    
    if (config.hiddenServicePort == 0) {
        return false;
    }

    m_config = config;
    
    // Set default values if not provided
    if (m_config.socksHost == nullptr || strlen(m_config.socksHost) == 0) {
        static const char* defaultHost = "127.0.0.1";
        m_config.socksHost = const_cast<char*>(defaultHost);
    }
    if (m_config.controlHost == nullptr || strlen(m_config.controlHost) == 0) {
        static const char* defaultControlHost = "127.0.0.1";
        m_config.controlHost = const_cast<char*>(defaultControlHost);
    }
    
    // Reconnect if necessary
    if (m_status == FuegoTorStatus::CONNECTED) {
        if (!testSocksConnection()) {
            m_status = FuegoTorStatus::ERROR;
            return false;
        }
    }
    
    return true;
}

FuegoTorConfig FuegoTorManager::getConfig() const {
    return m_config;
}

bool FuegoTorManager::testSocksConnection() {
    // Simple SOCKS5 connection test
#ifdef _WIN32
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
#else
    int sock = socket(AF_INET, SOCK_STREAM, 0);
#endif
    
    if (sock < 0) {
        return false;
    }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(m_config.socksPort);
    addr.sin_addr.s_addr = inet_addr(m_config.socksHost);
    
    bool connected = (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0);
    
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
    
    return connected;
}


bool FuegoTorManager::createSocksConnection(const char* /* address */, uint16_t /* port */, FuegoTorConnectionInfo& info) {
    // SOCKS5 connection implementation - simplified for CI compatibility
    // Initialize error message
    info.errorMessage[0] = '\0';
    
    // Create socket
#ifdef _WIN32
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        snprintf(info.errorMessage, sizeof(info.errorMessage), "Failed to create socket: %d", WSAGetLastError());
        return false;
    }
#else
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        snprintf(info.errorMessage, sizeof(info.errorMessage), "Failed to create socket");
        return false;
    }
#endif
    
    // Set up address
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(m_config.socksPort);
    
    // Convert IP address
    if (inet_pton(AF_INET, m_config.socksHost, &addr.sin_addr) <= 0) {
        snprintf(info.errorMessage, sizeof(info.errorMessage), "Invalid SOCKS host address");
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        return false;
    }
    
    // Attempt connection
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
#ifdef _WIN32
        snprintf(info.errorMessage, sizeof(info.errorMessage), "Failed to connect to SOCKS5 proxy: %d", WSAGetLastError());
        closesocket(sock);
#else
        snprintf(info.errorMessage, sizeof(info.errorMessage), "Failed to connect to SOCKS5 proxy");
        close(sock);
#endif
        return false;
    }
    
    // Connection successful - close socket (this is just a test)
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
    
    return true;
}

// TorUtils Implementation
namespace TorUtils {

bool isTorInstalled() {
#ifdef _WIN32
    // Check if tor.exe is in PATH
    FILE* pipe = _popen("where tor", "r");
#else
    // Check if tor is in PATH
    FILE* pipe = popen("which tor", "r");
#endif
    
    if (!pipe) return false;
    
    char buffer[128];
    bool found = (fgets(buffer, sizeof(buffer), pipe) != nullptr);
#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    
    return found;
}

bool getTorVersion(char* buffer, size_t bufferSize) {
    if (!buffer || bufferSize == 0) {
        return false;
    }
    
    buffer[0] = '\0'; // Initialize buffer
    
#ifdef _WIN32
    FILE* pipe = _popen("tor --version", "r");
#else
    FILE* pipe = popen("tor --version", "r");
#endif
    
    if (!pipe) {
        return false;
    }
    
    char tmpBuffer[256];
    if (fgets(tmpBuffer, sizeof(tmpBuffer), pipe) != nullptr) {
        // Remove newline character
        size_t len = strlen(tmpBuffer);
        if (len > 0 && tmpBuffer[len - 1] == '\n') {
            tmpBuffer[len - 1] = '\0';
        }
        
        // Copy to output buffer
        strncpy(buffer, tmpBuffer, bufferSize - 1);
        buffer[bufferSize - 1] = '\0';
    }
    
#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    
    return strlen(buffer) > 0;
}

FuegoTorConfig getDefaultConfig() {
    FuegoTorConfig config;
    std::memset(&config, 0, sizeof(config));
    
    config.enabled = false;
    config.socksHost = "127.0.0.1";
    config.socksPort = 9050;
    config.controlHost = "127.0.0.1";
    config.controlPort = 9051;
    config.hiddenServicePort = 8081;
    config.autoStart = false;
    config.connectionTimeout = 30000;
    config.circuitTimeout = 60000;
    config.enableHiddenService = false;
    config.hiddenServiceAddress = "";
    
    return config;
}

} // namespace TorUtils

} // namespace CryptoNote