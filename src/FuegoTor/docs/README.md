# FuegoTor - Tor Integration for Fuego

## 🌐 **Overview**

FuegoTor provides Tor network integration for Fuego, enabling enhanced privacy and anonymity for network communications. This implementation allows Fuego nodes to communicate through Tor's onion routing network, significantly improving user privacy.

## 🔒 **Privacy Benefits**

### **IP Address Protection**
- Hide your real IP address from network peers
- Prevent geographic location tracking
- Resist network-level surveillance

### **Traffic Analysis Resistance**
- Make transaction correlation much harder
- Protect against timing attacks
- Enhance overall network privacy

### **Censorship Resistance**
- Bypass network-level censorship
- Access Fuego network from restricted regions
- Maintain connectivity in hostile environments

## 🚀 **Quick Start**

### **Prerequisites**
- Tor installed on your system
- Fuego compiled with Tor support
- Basic understanding of Tor concepts

### **Installation**

1. **Install Tor** (if not already installed):
   ```bash
   # Ubuntu/Debian
   sudo apt-get install tor
   
   # macOS
   brew install tor
   
   # Windows
   # Download from https://www.torproject.org/
   ```

2. **Configure Tor**:
   ```bash
   # Start Tor service
   sudo systemctl start tor
   
   # Or start manually
   tor --SOCKSPort 9050 --ControlPort 9051
   ```

3. **Configure FuegoTor**:
   ```bash
   # Copy configuration file
   cp config/tor.conf ~/.fuego/tor.conf
   
   # Edit configuration
   nano ~/.fuego/tor.conf
   ```

### **Basic Usage**

```cpp
#include "TorIntegration.h"

// Create Tor configuration
TorConfig config = TorUtils::getDefaultConfig();
config.enabled = true;
config.socksHost = "127.0.0.1";
config.socksPort = 9050;

// Create Tor manager
TorManager torManager(config);

// Initialize Tor
if (torManager.initialize()) {
    std::cout << "Tor initialized successfully!" << std::endl;
    
    // Create Tor connection
    TorConnectionInfo info = torManager.createConnection("example.com", 80);
    
    if (info.status == TorStatus::CONNECTED) {
        std::cout << "Connected via Tor!" << std::endl;
    }
}
```

## ⚙️ **Configuration**

### **Configuration File** (`tor.conf`)

```ini
[tor]
# Enable Tor integration
enabled = true

# SOCKS5 proxy settings
socks_host = 127.0.0.1
socks_port = 9050

# Tor control interface
control_host = 127.0.0.1
control_port = 9051

# Hidden service settings
enable_hidden_service = false
hidden_service_port = 8081

# Connection settings
auto_start = false
connection_timeout = 30000
circuit_timeout = 60000
```

### **Configuration Options**

| Option | Description | Default | Values |
|--------|-------------|---------|--------|
| `enabled` | Enable Tor integration | `false` | `true`/`false` |
| `socks_host` | SOCKS5 proxy host | `127.0.0.1` | IP address |
| `socks_port` | SOCKS5 proxy port | `9050` | 1-65535 |
| `control_host` | Tor control host | `127.0.0.1` | IP address |
| `control_port` | Tor control port | `9051` | 1-65535 |
| `auto_start` | Auto-start Tor | `false` | `true`/`false` |
| `connection_timeout` | Connection timeout (ms) | `30000` | 1000-300000 |
| `circuit_timeout` | Circuit timeout (ms) | `60000` | 1000-600000 |
| `enable_hidden_service` | Enable hidden service | `false` | `true`/`false` |
| `hidden_service_port` | Hidden service port | `8081` | 1-65535 |

## 🔧 **API Reference**

### **TorManager Class**

```cpp
class TorManager {
public:
    // Constructor
    explicit TorManager(const TorConfig& config);
    
    // Initialization
    bool initialize();
    void shutdown();
    
    // Status
    bool isTorAvailable() const;
    TorStatus getStatus() const;
    TorStats getStats() const;
    
    // Connections
    TorConnectionInfo createConnection(const std::string& address, uint16_t port);
    
    // Hidden services
    std::string getHiddenServiceAddress() const;
    
    // Callbacks
    void setStatusCallback(TorStatusCallback callback);
    void setConnectionCallback(TorConnectionCallback callback);
    void setErrorCallback(TorErrorCallback callback);
    
    // Configuration
    bool updateConfig(const TorConfig& config);
    TorConfig getConfig() const;
};
```

### **TorConnection Class**

```cpp
class TorConnection {
public:
    // Constructor
    TorConnection(TorManager& manager, const std::string& address, uint16_t port);
    
    // Connection management
    bool connect();
    void disconnect();
    bool isConnected() const;
    
    // Data transfer
    size_t send(const void* data, size_t size);
    size_t receive(void* buffer, size_t size);
    
    // Information
    TorConnectionInfo getInfo() const;
    uint32_t getLatency() const;
};
```

### **TorUtils Namespace**

```cpp
namespace TorUtils {
    // System checks
    bool isTorInstalled();
    std::string getTorVersion();
    
    // Onion addresses
    std::string generateOnionAddress();
    bool isValidOnionAddress(const std::string& address);
    std::string resolveToOnion(const std::string& address);
    
    // Configuration
    TorConfig getDefaultConfig();
    TorConfig loadConfigFromFile(const std::string& filename);
    bool saveConfigToFile(const TorConfig& config, const std::string& filename);
}
```

## 📊 **Status and Statistics**

### **TorStatus Enumeration**

```cpp
enum class TorStatus {
    DISCONNECTED,   // Not connected to Tor
    CONNECTING,     // Attempting to connect
    CONNECTED,      // Successfully connected
    ERROR,          // Connection error
    UNKNOWN         // Status unknown
};
```

### **TorStats Structure**

```cpp
struct TorStats {
    uint32_t totalConnections;          // Total connections made
    uint32_t successfulConnections;     // Successful connections
    uint32_t failedConnections;        // Failed connections
    uint32_t bytesTransferred;          // Total bytes transferred
    uint32_t averageLatency;            // Average connection latency
    uint32_t circuitCount;              // Active circuit count
    std::string torVersion;             // Tor version string
};
```

## 🛡️ **Security Considerations**

### **Privacy Benefits**
- **IP Address Hiding**: Your real IP address is hidden from peers
- **Traffic Analysis Resistance**: Harder to correlate transactions
- **Location Privacy**: Geographic location protection
- **Network Surveillance Resistance**: Bypass monitoring

### **Security Measures**
- **Circuit Isolation**: Separate Tor circuits for different connections
- **Connection Encryption**: All communications encrypted
- **Authentication**: Proper peer authentication
- **Fallback Security**: Maintain security when Tor disabled

### **Best Practices**
- Always verify Tor is running before enabling FuegoTor
- Use strong authentication for Tor control interface
- Regularly update Tor to latest version
- Monitor Tor connection status
- Use bridges in censored regions

## 🔍 **Troubleshooting**

### **Common Issues**

#### **Tor Not Installed**
```
Error: Tor is not installed on this system
```
**Solution**: Install Tor using your system's package manager

#### **Connection Failed**
```
Tor Error: Failed to connect to Tor
```
**Solution**: 
- Check if Tor is running: `systemctl status tor`
- Verify SOCKS5 port: `netstat -tlnp | grep 9050`
- Check firewall settings

#### **SOCKS5 Proxy Error**
```
Connection Error: Failed to connect to SOCKS5 proxy
```
**Solution**:
- Verify Tor configuration
- Check proxy settings
- Test with `curl --socks5 127.0.0.1:9050 http://check.torproject.org/`

#### **Hidden Service Issues**
```
Hidden service creation failed
```
**Solution**:
- Check Tor configuration
- Verify hidden service directory permissions
- Ensure Tor has write access to data directory

### **Debug Mode**

Enable debug logging:
```ini
[tor]
log_level = debug
log_file = tor_debug.log
```

### **Testing Tor Connection**

Test Tor connectivity:
```bash
# Test SOCKS5 proxy
curl --socks5 127.0.0.1:9050 http://check.torproject.org/

# Test Tor control interface
telnet 127.0.0.1 9051
```

## 📈 **Performance**

### **Expected Overhead**
- **Latency**: 200-500ms additional delay
- **Bandwidth**: 10-20% overhead
- **CPU**: 5-10% additional usage
- **Memory**: 50-100MB additional usage

### **Optimization Tips**
- Use connection pooling for frequent connections
- Enable circuit reuse when possible
- Monitor performance metrics
- Adjust timeout values based on network conditions

## 🔄 **Integration with Fuego**

### **P2P Network Integration**
- Tor-enabled peer discovery
- Onion address support
- Tor peer connections
- Network topology adaptation

### **Wallet Integration**
- Tor-enabled transaction broadcasting
- Block synchronization over Tor
- Peer communication via Tor
- Network monitoring

### **RPC Interface**
- Hidden service support
- Proxy configuration
- Status monitoring
- Error handling

## 📚 **Examples**

### **Basic Tor Connection**
```cpp
// Create Tor configuration
TorConfig config = TorUtils::getDefaultConfig();
config.enabled = true;

// Create Tor manager
TorManager torManager(config);

// Initialize
if (torManager.initialize()) {
    // Create connection
    TorConnectionInfo info = torManager.createConnection("example.com", 80);
    
    if (info.status == TorStatus::CONNECTED) {
        std::cout << "Connected via Tor!" << std::endl;
    }
}
```

### **Hidden Service**
```cpp
// Enable hidden service
config.enableHiddenService = true;
config.hiddenServicePort = 8081;

// Update configuration
torManager.updateConfig(config);

// Get hidden service address
std::string onionAddress = torManager.getHiddenServiceAddress();
std::cout << "Hidden service: " << onionAddress << std::endl;
```

### **Status Monitoring**
```cpp
// Set up status callback
torManager.setStatusCallback([](TorStatus status, const std::string& message) {
    std::cout << "Tor Status: " << static_cast<int>(status) << " - " << message << std::endl;
});

// Get statistics
TorStats stats = torManager.getStats();
std::cout << "Total connections: " << stats.totalConnections << std::endl;
```

## 🤝 **Contributing**

### **Development Setup**
1. Clone the repository
2. Install Tor development libraries
3. Compile with Tor support
4. Run tests

### **Testing**
```bash
# Run unit tests
make test

# Run integration tests
make test-integration

# Run example
./examples/tor_example
```

### **Reporting Issues**
- Check existing issues first
- Provide detailed error messages
- Include system information
- Attach relevant logs

## 📄 **License**

FuegoTor is part of the Fuego project and is licensed under the GNU General Public License v3 or later.

## 🔗 **Links**

- [Tor Project](https://www.torproject.org/)
- [Fuego Project](https://github.com/usexfg/)
- [Tor Documentation](https://2019.www.torproject.org/docs/documentation.html.en)
- [SOCKS5 Protocol](https://tools.ietf.org/html/rfc1928)

---

**Note**: FuegoTor enhances Fuego's privacy capabilities while maintaining compatibility and performance. Always ensure Tor is properly configured and running before enabling FuegoTor.
