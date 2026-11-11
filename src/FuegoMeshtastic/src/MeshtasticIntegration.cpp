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

#include "../include/MeshtasticIntegration.h"

#include <algorithm>
#include <cstring>
#include <chrono>
#include <random>
#include <thread>
#include <atomic>
#include <queue>

#ifdef __APPLE__
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <sys/ioctl.h>
#elif __linux__
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <sys/ioctl.h>
#elif _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

#include "../Logging/LoggerRef.h"

namespace CryptoNote {

namespace {

uint32_t generateRandomNodeNum() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dis(0x100000, 0xFFFFFF);
    return dis(gen) << 8;
}

class MeshSocket {
public:
    MeshSocket() : m_fd(-1) {}
    ~MeshSocket() { close(); }
    
    bool connect(const std::string& host, uint16_t port) {
#ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
        
        m_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (m_fd < 0) {
            return false;
        }

        struct hostent* server = gethostbyname(host.c_str());
        if (server == nullptr) {
            close();
            return false;
        }

        struct sockaddr_in serv_addr;
        std::memset(&serv_addr, 0, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        std::memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
        serv_addr.sin_port = htons(port);

        if (::connect(m_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
            close();
            return false;
        }

        int flag = 1;
        setsockopt(m_fd, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(int));

        return true;
    }
    
    void close() {
        if (m_fd >= 0) {
#ifdef _WIN32
            ::closesocket(m_fd);
#else
            ::close(m_fd);
#endif
            m_fd = -1;
        }
    }
    
    bool isConnected() const { return m_fd >= 0; }
    
    bool sendData(const uint8_t* data, size_t len) {
        if (m_fd < 0) return false;
        size_t totalSent = 0;
        while (totalSent < len) {
#ifdef _WIN32
            int sent = ::send(m_fd, (const char*)data + totalSent, len - totalSent, 0);
#else
            ssize_t sent = ::send(m_fd, data + totalSent, len - totalSent, 0);
#endif
            if (sent < 0) return false;
            totalSent += sent;
        }
        return true;
    }
    
    bool receiveData(uint8_t* buffer, size_t maxLen, size_t& bytesRead, uint32_t timeoutMs) {
        if (m_fd < 0) return false;
        
#ifdef __APPLE__
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(m_fd, &readfds);
        
        struct timeval tv;
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
        
        int ret = select(m_fd + 1, &readfds, NULL, NULL, &tv);
#elif __linux__
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(m_fd, &readfds);
        
        struct timeval tv;
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
        
        int ret = select(m_fd + 1, &readfds, NULL, NULL, &tv);
#elif _WIN32
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET((SOCKET)m_fd, &readfds);
        
        struct timeval tv;
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
        
        int ret = select(0, &readfds, NULL, NULL, &tv);
#endif
        
        if (ret <= 0) {
            bytesRead = 0;
            return ret == 0;
        }
        
#ifdef _WIN32
        bytesRead = recv(m_fd, (char*)buffer, maxLen, 0);
#else
        bytesRead = recv(m_fd, buffer, maxLen, 0);
#endif
        return bytesRead > 0;
    }

private:
#ifdef _WIN32
    SOCKET m_fd;
#else
    int m_fd;
#endif
};

}

struct MeshtasticIntegration::Impl {
    Impl(Logging::ILogger& logger) : logger(logger, "meshtastic"), m_serialFd(-1) {}
    
    Logging::LoggerRef logger;
    std::atomic<MeshtasticStatus> status{ MeshtasticStatus::DISCONNECTED };
    std::atomic<bool> running{ false };
    std::atomic<uint32_t> packetId{ 0 };
    uint32_t localNodeNum = generateRandomNodeNum();
    
    int m_serialFd;
    std::unique_ptr<MeshSocket> tcpSocket;
    
    MeshtasticConfig config;
    mutable std::mutex configMutex;
    
    std::string lastError;
    mutable std::mutex statusMutex;
    
    MeshtasticStatusCallback statusCallback;
    MeshtasticMessageCallback messageCallback;
    
    std::thread workerThread;
    std::condition_variable cv;
    std::mutex cvMutex;
    
    std::vector<MeshtasticPeerInfo> peers;
    mutable std::mutex peersMutex;
    
    std::queue<MeshtasticMessage> messageQueue;
    mutable std::mutex messageQueueMutex;
    std::condition_variable messageQueueCv;
};

MeshtasticIntegration::MeshtasticIntegration(Logging::ILogger& logger)
    : m_impl(new Impl(logger))
{
}

MeshtasticIntegration::~MeshtasticIntegration() {
    shutdown();
}

bool MeshtasticIntegration::initialize(const MeshtasticConfig& config) {
    std::lock_guard<std::mutex> lock(m_impl->configMutex);
    m_impl->config = config;
    
    if (m_impl->config.enabled) {
        m_impl->logger(Logging::INFO) << "Meshtastic integration initialized";
        m_impl->logger(Logging::INFO) << "  Transport: " << static_cast<int>(m_impl->config.transportType);
        
        if (m_impl->config.transportType == MeshtasticTransportType::SERIAL) {
            m_impl->logger(Logging::INFO) << "  Device: " << m_impl->config.devicePath;
        } else {
            m_impl->logger(Logging::INFO) << "  Host: " << m_impl->config.host << ":" << m_impl->config.port;
        }
        m_impl->logger(Logging::INFO) << "  Channel: " << static_cast<int>(m_impl->config.meshChannel);
        m_impl->logger(Logging::INFO) << "  Node ID: 0x" << std::hex << m_impl->localNodeNum << std::dec;
    }
    
    return true;
}

void MeshtasticIntegration::shutdown() {
    stop();
    std::lock_guard<std::mutex> lock(m_impl->configMutex);
    m_impl->config.enabled = false;
}

bool MeshtasticIntegration::start() {
    if (!m_impl->config.enabled) {
        m_impl->lastError = "Meshtastic integration is disabled";
        return false;
    }

    if (m_impl->running.load()) {
        return true;
    }

    updateStatus(MeshtasticStatus::CONNECTING);

    if (!connectToDevice()) {
        updateStatus(MeshtasticStatus::ERROR, m_impl->lastError);
        return false;
    }

    m_impl->running = true;
    m_impl->workerThread = std::thread(&MeshtasticIntegration::workerThread, this);

    updateStatus(MeshtasticStatus::CONNECTED);
    m_impl->logger(Logging::INFO) << "Meshtastic integration started";
    
    return true;
}

void MeshtasticIntegration::stop() {
    if (!m_impl->running.load()) {
        return;
    }

    m_impl->running = false;
    m_impl->cv.notify_all();

    if (m_impl->workerThread.joinable()) {
        m_impl->workerThread.join();
    }

    disconnectDevice();
    updateStatus(MeshtasticStatus::DISCONNECTED);
    m_impl->logger(Logging::INFO) << "Meshtastic integration stopped";
}

MeshtasticStatus MeshtasticIntegration::getStatus() const {
    return m_impl->status.load();
}

std::string MeshtasticIntegration::getErrorMessage() const {
    std::lock_guard<std::mutex> lock(m_impl->statusMutex);
    return m_impl->lastError;
}

bool MeshtasticIntegration::isRunning() const {
    return m_impl->running.load() && getStatus() == MeshtasticStatus::CONNECTED;
}

void MeshtasticIntegration::setStatusCallback(MeshtasticStatusCallback callback) {
    m_impl->statusCallback = callback;
}

void MeshtasticIntegration::setMessageCallback(MeshtasticMessageCallback callback) {
    m_impl->messageCallback = callback;
}

void MeshtasticIntegration::setPeerCallback(MeshtasticPeerCallback callback) {
    m_impl->peerCallback = callback;
}

bool MeshtasticIntegration::sendMeshPacket(const std::vector<uint8_t>& data, uint32_t destination) {
    if (!isRunning()) {
        m_impl->lastError = "Not connected to meshtastic network";
        return false;
    }

    auto packet = buildMeshPacket(destination, m_impl->config.meshChannel, data);
    return writeToDevice(packet.data(), packet.size());
}

bool MeshtasticIntegration::broadcastBlock(const std::vector<uint8_t>& blockData) {
    return sendMeshPacket(blockData, MESHASTIC_BROADCAST_ADDR);
}

bool MeshtasticIntegration::relayTransaction(const std::vector<uint8_t>& txData) {
    return sendMeshPacket(txData, MESHASTIC_BROADCAST_ADDR);
}

bool MeshtasticIntegration::sendBlockSignal(uint32_t height, const uint8_t* blockHash, size_t hashLen) {
    if (!isRunning()) return false;
    
    std::vector<uint8_t> signal;
    signal.push_back(0x01);
    signal.push_back(static_cast<uint8_t>(2));
    signal.push_back(static_cast<uint8_t>((height >> 24) & 0xFF));
    signal.push_back(static_cast<uint8_t>((height >> 16) & 0xFF));
    signal.push_back(static_cast<uint8_t>((height >> 8) & 0xFF));
    signal.push_back(static_cast<uint8_t>(height & 0xFF));
    
    signal.push_back(static_cast<uint8_t>(hashLen));
    signal.insert(signal.end(), blockHash, blockHash + hashLen);
    
    return sendMeshPacket(signal, MESHASTIC_BROADCAST_ADDR);
}

std::vector<MeshtasticPeerInfo> MeshtasticIntegration::getPeers() const {
    std::lock_guard<std::mutex> lock(m_impl->peersMutex);
    return m_impl->peers;
}

bool MeshtasticIntegration::pingNode(uint32_t nodeNum) {
    if (!isRunning()) {
        return false;
    }

    std::vector<uint8_t> pingData = {0x01, 0x00};
    return sendMeshPacket(pingData, nodeNum);
}

MeshtasticConfig MeshtasticIntegration::getConfig() const {
    std::lock_guard<std::mutex> lock(m_impl->configMutex);
    return m_impl->config;
}

bool MeshtasticIntegration::updateConfig(const MeshtasticConfig& config) {
    if (m_impl->running.load()) {
        m_impl->lastError = "Cannot update config while running";
        return false;
    }
    
    std::lock_guard<std::mutex> lock(m_impl->configMutex);
    m_impl->config = config;
    return true;
}

bool MeshtasticIntegration::connectToDevice() {
    switch (m_impl->config.transportType) {
        case MeshtasticTransportType::SERIAL:
            return connectSerial();
        case MeshtasticTransportType::TCP:
            return connectTcp();
        case MeshtasticTransportType::BLUETOOTH:
            m_impl->lastError = "Bluetooth transport not implemented";
            return false;
        default:
            m_impl->lastError = "Unknown transport type";
            return false;
    }
}

void MeshtasticIntegration::disconnectFromDevice() {
    disconnectDevice();
}

void MeshtasticIntegration::workerThread() {
    m_impl->logger(Logging::DEBUGGING) << "Meshtastic worker thread started";

    while (m_impl->running.load()) {
        try {
            if (readFromDevice()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        } catch (const std::exception& e) {
            m_impl->logger(Logging::ERROR) << "Meshtastic worker exception: " << e.what();
            break;
        }
    }

    m_impl->logger(Logging::DEBUGGING) << "Meshtastic worker thread stopped";
}

bool MeshtasticIntegration::connectSerial() {
#ifdef __APPLE__
    m_impl->m_serialFd = ::open(m_impl->config.devicePath.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
#elif __linux__
    m_impl->m_serialFd = ::open(m_impl->config.devicePath.c_str(), O_RDWR | O_NOCTTY);
#elif _WIN32
    m_impl->m_serialFd = ::open(m_impl->config.devicePath.c_str(), O_RDWR | O_NOCTTY);
#endif
    if (m_impl->m_serialFd < 0) {
        m_impl->lastError = "Failed to open serial device: " + m_impl->config.devicePath;
        m_impl->logger(Logging::ERROR) << m_impl->lastError;
        return false;
    }

#ifdef __APPLE__
    struct termios tty;
    if (tcgetattr(m_impl->m_serialFd, &tty) != 0) {
        m_impl->lastError = "Failed to get terminal attributes";
        ::close(m_impl->m_serialFd);
        m_impl->m_serialFd = -1;
        return false;
    }

    cfsetospeed(&tty, B115200);
    cfsetispeed(&tty, B115200);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_iflag &= ~IGNBRK;
    tty.c_lflag = 0;
    tty.c_oflag = 0;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 5;

    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~(PARENB | PARODD);
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;

    if (tcsetattr(m_impl->m_serialFd, TCSANOW, &tty) != 0) {
        m_impl->lastError = "Failed to set terminal attributes";
        ::close(m_impl->m_serialFd);
        m_impl->m_serialFd = -1;
        return false;
    }
#endif

    m_impl->logger(Logging::INFO) << "Connected to meshtastic device via serial: " << m_impl->config.devicePath;
    return true;
}

bool MeshtasticIntegration::connectTcp() {
    m_impl->tcpSocket = std::make_unique<MeshSocket>();
    
    if (!m_impl->tcpSocket->connect(m_impl->config.host, m_impl->config.port)) {
        m_impl->lastError = "Failed to connect to meshtastic MQTT bridge at " + 
                           m_impl->config.host + ":" + std::to_string(m_impl->config.port);
        m_impl->logger(Logging::ERROR) << m_impl->lastError;
        m_impl->tcpSocket.reset();
        return false;
    }

    m_impl->logger(Logging::INFO) << "Connected to meshtastic MQTT bridge: " 
                                   << m_impl->config.host << ":" << m_impl->config.port;
    return true;
}

void MeshtasticIntegration::disconnectDevice() {
    if (m_impl->m_serialFd >= 0) {
#ifdef _WIN32
        ::closesocket(m_impl->m_serialFd);
#else
        ::close(m_impl->m_serialFd);
#endif
        m_impl->m_serialFd = -1;
    }
    
    m_impl->tcpSocket.reset();
    m_impl->logger(Logging::INFO) << "Disconnected from meshtastic device";
}

bool MeshtasticIntegration::readFromDevice() {
    uint8_t buffer[MESHASTIC_MAX_PACKET_SIZE];
    size_t bytesRead = 0;

    if (m_impl->m_serialFd >= 0) {
#ifdef _WIN32
        bytesRead = recv(m_impl->m_serialFd, (char*)buffer, sizeof(buffer), 0);
#else
        bytesRead = ::read(m_impl->m_serialFd, buffer, sizeof(buffer));
#endif
    } else if (m_impl->tcpSocket && m_impl->tcpSocket->isConnected()) {
        if (!m_impl->tcpSocket->receiveData(buffer, sizeof(buffer), bytesRead, 100)) {
            return false;
        }
    } else {
        return false;
    }

    if (bytesRead > 0) {
        MeshtasticMessage msg;
        if (parseMeshPacket(buffer, bytesRead, msg)) {
            std::lock_guard<std::mutex> lock(m_impl->messageQueueMutex);
            m_impl->messageQueue.push(msg);
            m_impl->messageQueueCv.notify_one();
            
            if (m_impl->messageCallback) {
                m_impl->messageCallback(msg);
            }
        }
        return true;
    }

    return false;
}

bool MeshtasticIntegration::writeToDevice(const uint8_t* data, size_t len) {
    if (m_impl->m_serialFd >= 0) {
#ifdef _WIN32
        ssize_t written = send(m_impl->m_serialFd, (const char*)data, len, 0);
#else
        ssize_t written = ::write(m_impl->m_serialFd, data, len);
#endif
        return written == static_cast<ssize_t>(len);
    } else if (m_impl->tcpSocket && m_impl->tcpSocket->isConnected()) {
        return m_impl->tcpSocket->sendData(data, len);
    }
    return false;
}

bool MeshtasticIntegration::parseMeshPacket(const uint8_t* data, size_t len, MeshtasticMessage& msg) {
    if (len < 8) {
        return false;
    }

    msg.from = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
    msg.to = (data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7];
    
    if (len > 8) {
        msg.channel = data[8];
        if (len > 9) {
            msg.payload.assign(data + 9, data + len);
        }
    }

    msg.id = ++m_impl->packetId;
    msg.timestamp = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());

    return true;
}

std::vector<uint8_t> MeshtasticIntegration::buildMeshPacket(uint32_t destination, uint8_t channel, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> packet;
    packet.reserve(4 + 4 + 1 + payload.size() + 2);

    packet.push_back((m_impl->localNodeNum >> 24) & 0xFF);
    packet.push_back((m_impl->localNodeNum >> 16) & 0xFF);
    packet.push_back((m_impl->localNodeNum >> 8) & 0xFF);
    packet.push_back(m_impl->localNodeNum & 0xFF);

    packet.push_back((destination >> 24) & 0xFF);
    packet.push_back((destination >> 16) & 0xFF);
    packet.push_back((destination >> 8) & 0xFF);
    packet.push_back(destination & 0xFF);

    packet.push_back(channel);
    packet.insert(packet.end(), payload.begin(), payload.end());

    uint16_t crc = calculateCrc(packet.data(), packet.size());
    packet.push_back(crc & 0xFF);
    packet.push_back((crc >> 8) & 0xFF);

    return packet;
}

uint16_t MeshtasticIntegration::calculateCrc(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc = crc >> 1;
            }
        }
    }
    return crc;
}

void MeshtasticIntegration::updateStatus(MeshtasticStatus newStatus, const std::string& errorMessage) {
    MeshtasticStatus oldStatus = m_impl->status.exchange(newStatus);
    
    {
        std::lock_guard<std::mutex> lock(m_impl->statusMutex);
        m_impl->lastError = errorMessage;
    }
    
    if (m_impl->statusCallback) {
        m_impl->statusCallback(newStatus, errorMessage);
    }
}

uint32_t MeshtasticIntegration::getLocalNodeNum() const { 
    return m_impl->localNodeNum; 
}

std::string MeshtasticIntegration::getLocalNodeName() const { 
    std::lock_guard<std::mutex> lock(m_impl->configMutex);
    return m_impl->config.nodeName; 
}

} // namespace CryptoNote
