// Copyright (c) 2017-2026 Fuego Developers
// Copyright (c) 2018-2019 Conceal Network & Conceal Devs
// Copyright (c) 2016-2019 The Karbowanec developers
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

#include <string.h>
#include <tuple>
#include <string>
#include <cstdint>
#include <boost/uuid/uuid.hpp>
#include "../Common/StringTools.h"

namespace CryptoNote
{
  typedef boost::uuids::uuid uuid;
  typedef boost::uuids::uuid net_connection_id;
  typedef uint64_t PeerIdType;

  enum class AddressType : uint8_t {
    Invalid = 0,
    IPv4 = 1,
    IPv6 = 2,
    I2P = 3,
    Tor = 4
  };

  enum class Zone : uint8_t {
    Invalid = 0,
    Public = 1,
    I2P = 2,
    Tor = 3
  };

#pragma pack (push, 1)

  struct NetworkAddress
  {
    uint32_t ip;
    uint32_t port;
  };

  struct I2PAddress
  {
    char host[61];  // max b32.i2p destination length is ~56 chars + null
    uint16_t port;

    I2PAddress() : port(1) { host[0] = '\0'; }
    I2PAddress(const std::string& b32, uint16_t p = 1) : port(p) {
      size_t len = b32.size() < 60 ? b32.size() : 60;
      memcpy(host, b32.c_str(), len);
      host[len] = '\0';
    }

    bool isValid() const { return host[0] != '\0'; }
    std::string toString() const { return std::string(host) + ".b32.i2p"; }
  };

  struct TorAddress
  {
    char host[63];  // v3 onion addresses are 56 chars + ".onion" + null
    uint16_t port;

    TorAddress() : port(80) { host[0] = '\0'; }
    TorAddress(const std::string& onion, uint16_t p = 80) : port(p) {
      size_t len = onion.size() < 62 ? onion.size() : 62;
      memcpy(host, onion.c_str(), len);
      host[len] = '\0';
    }

    bool isValid() const { return host[0] != '\0'; }
    std::string toString() const { return std::string(host); }
  };

  struct PeerlistEntry
  {
    NetworkAddress adr;
    PeerIdType id;
    uint64_t last_seen;
  };

  struct AnchorPeerlistEntry
  {
    NetworkAddress adr;
    PeerIdType id;
    int64_t first_seen;
  };

  struct connection_entry
  {
    NetworkAddress adr;
    PeerIdType id;
    bool is_income;
  };

#pragma pack(pop)

  struct PeerAddress
  {
    AddressType type = AddressType::IPv4;
    std::string host;
    uint32_t ip = 0;
    uint16_t port = 0;

    PeerAddress() = default;

    static PeerAddress fromIPv4(uint32_t ip_addr, uint16_t p) {
      PeerAddress addr;
      addr.type = AddressType::IPv4;
      addr.ip = ip_addr;
      addr.port = p;
      addr.host = Common::ipAddressToString(ip_addr);
      return addr;
    }

    static PeerAddress fromI2P(const std::string& destination, uint16_t p = 1) {
      PeerAddress addr;
      addr.type = AddressType::I2P;
      addr.host = destination;
      addr.port = p;
      return addr;
    }

    static PeerAddress fromTor(const std::string& onion, uint16_t p = 80) {
      PeerAddress addr;
      addr.type = AddressType::Tor;
      addr.host = onion;
      addr.port = p;
      return addr;
    }

    static PeerAddress unknown(AddressType t) {
      PeerAddress addr;
      addr.type = t;
      addr.host = "unknown";
      addr.port = 0;
      return addr;
    }

    Zone getZone() const {
      switch (type) {
        case AddressType::IPv4:
        case AddressType::IPv6:
          return Zone::Public;
        case AddressType::I2P:
          return Zone::I2P;
        case AddressType::Tor:
          return Zone::Tor;
        default:
          return Zone::Invalid;
      }
    }

    bool isPrivacyNet() const {
      return type == AddressType::I2P || type == AddressType::Tor;
    }

    bool isLoopback() const {
      return false;
    }

    bool isLocal() const {
      return false;
    }

    std::string toString() const {
      switch (type) {
        case AddressType::IPv4:
          return Common::ipAddressToString(ip) + ":" + std::to_string(port);
        case AddressType::I2P:
          return host + ":" + std::to_string(port) + " [I2P]";
        case AddressType::Tor:
          return host + ":" + std::to_string(port) + " [Tor]";
        default:
          return host + ":" + std::to_string(port);
      }
    }

    bool operator==(const PeerAddress& other) const {
      if (type != other.type) return false;
      if (type == AddressType::IPv4)
        return ip == other.ip && port == other.port;
      return host == other.host && port == other.port;
    }

    bool operator!=(const PeerAddress& other) const {
      return !(*this == other);
    }
  };

  struct PrivacyNetConfig {
    bool useI2P = false;
    std::string i2pSocksHost = "127.0.0.1";
    uint16_t i2pSocksPort = 4447;

    bool useTor = false;
    std::string torSocksHost = "127.0.0.1";
    uint16_t torSocksPort = 9050;

    bool restrictToPrivacyNet = false;

    enum class Priority { I2P_First, Tor_First };
    Priority priority = Priority::I2P_First;

    bool anyProxyEnabled() const {
      return useI2P || useTor;
    }
  };

  inline bool operator < (const NetworkAddress& a, const NetworkAddress& b) {
    return std::tie(a.ip, a.port) < std::tie(b.ip, b.port);
  }

  inline bool operator == (const NetworkAddress& a, const NetworkAddress& b) {
    return memcmp(&a, &b, sizeof(a)) == 0;
  }

  inline std::ostream& operator << (std::ostream& s, const NetworkAddress& na) {
    return s << Common::ipAddressToString(na.ip) << ":" << std::to_string(na.port);
  }

  inline uint32_t hostToNetwork(uint32_t n) {
    return (n << 24) | (n & 0xff00) << 8 | (n & 0xff0000) >> 8 | (n >> 24);
  }

  inline uint32_t networkToHost(uint32_t n) {
    return hostToNetwork(n); // the same
  }

}
