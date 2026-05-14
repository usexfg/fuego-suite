// Copyright (c) 2017-2026 Fuego Developers
// Copyright (c) 2024-2026 Meshtastic Project
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
#include <queue>
#include <condition_variable>
#include <cstdint>
#include <chrono>

namespace Logging {
class ILogger;
}

namespace CryptoNote {

enum class MeshtasticStatus {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    ERROR,
    UNKNOWN
};

enum class MeshtasticTransportType {
    SERIAL,
    TCP,
    BLUETOOTH
};

struct MeshtasticConfig {
    bool enabled = false;
    MeshtasticTransportType transportType = MeshtasticTransportType::TCP;
    std::string devicePath = "/dev/ttyUSB0";
    std::string host = "192.168.1.1";
    uint16_t port = 1883;
    uint32_t connectionTimeout = 30000;
    uint8_t meshChannel = 0;
    uint64_t meshChannelKey = 0;
    bool relayMode = true;
    uint32_t maxHops = 3;
    std::string nodeName = "fuego-node";
    uint32_t broadcastInterval = 60000;
};

struct MeshtasticPeerInfo {
    uint32_t nodeNum;
    std::string userName;
    uint32_t latitude;
    uint32_t longitude;
    uint32_t altitude;
    uint32_t snr;
    uint32_t rssi;
    bool isOnline;
    uint64_t lastHeard;
};

struct MeshtasticMessage {
    uint32_t from;
    uint32_t to;
    uint32_t id;
    uint8_t channel;
    std::vector<uint8_t> payload;
    uint64_t timestamp;
    int32_t snr;
    uint32_t rssi;
};

using MeshtasticStatusCallback = std::function<void(MeshtasticStatus, const std::string&)>;
using MeshtasticMessageCallback = std::function<void(const MeshtasticMessage&)>;
using MeshtasticPeerCallback = std::function<void(const MeshtasticPeerInfo&)>;

constexpr size_t MESHASTIC_MAX_PACKET_SIZE = 512;
constexpr size_t MESHASTIC_MAX_PAYLOAD_SIZE = 238;
constexpr uint32_t MESHASTIC_BROADCAST_ADDR = 0xFFFFFFFF;

class MeshtasticIntegration {
public:
    MeshtasticIntegration(Logging::ILogger& logger);
    ~MeshtasticIntegration();

    bool initialize(const MeshtasticConfig& config);
    void shutdown();
    bool start();
    void stop();
    
    MeshtasticStatus getStatus() const;
    std::string getErrorMessage() const;
    bool isRunning() const;
    
    void setStatusCallback(MeshtasticStatusCallback callback);
    void setMessageCallback(MeshtasticMessageCallback callback);
    void setPeerCallback(MeshtasticPeerCallback callback);
    
    bool sendMeshPacket(const std::vector<uint8_t>& data, uint32_t destination = MESHASTIC_BROADCAST_ADDR);
    bool broadcastBlock(const std::vector<uint8_t>& blockData);
    bool relayTransaction(const std::vector<uint8_t>& txData);
    bool sendBlockSignal(uint32_t height, const uint8_t* blockHash, size_t hashLen);
    
    std::vector<MeshtasticPeerInfo> getPeers() const;
    bool pingNode(uint32_t nodeNum);
    
    MeshtasticConfig getConfig() const;
    bool updateConfig(const MeshtasticConfig& config);

    uint32_t getLocalNodeNum() const;
    std::string getLocalNodeName() const;

private:
    void workerThread();
    bool connectToDevice();
    void disconnectFromDevice();
    
    bool connectSerial();
    bool connectTcp();
    
    bool readFromDevice();
    bool writeToDevice(const uint8_t* data, size_t len);
    
    bool parseMeshPacket(const uint8_t* data, size_t len, MeshtasticMessage& msg);
    std::vector<uint8_t> buildMeshPacket(uint32_t destination, uint8_t channel, const std::vector<uint8_t>& payload);
    
    uint16_t calculateCrc(const uint8_t* data, size_t len);
    void updateStatus(MeshtasticStatus newStatus, const std::string& errorMessage = "");

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace CryptoNote
