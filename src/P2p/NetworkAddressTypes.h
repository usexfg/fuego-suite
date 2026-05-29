// Copyright (c) 2017-2026 Fuego Developers
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

#include <cstdint>
#include <cstring>
#include <string>
#include <tuple>
#include <ostream>

#include <boost/variant.hpp>

#include "P2pProtocolTypes.h"

namespace CryptoNote {

enum class NetworkZone : uint8_t {
  Public = 0,
  I2P    = 1,
  Tor    = 2
};

enum class AddressType : uint8_t {
  Invalid = 0,
  IPv4    = 1,
  I2P     = 2,
  Tor     = 3
};

inline bool is_anonymity_zone(NetworkZone zone) {
  return zone == NetworkZone::I2P || zone == NetworkZone::Tor;
}

inline const char* zone_to_string(NetworkZone zone) {
  switch (zone) {
    case NetworkZone::Public: return "public";
    case NetworkZone::I2P:    return "i2p";
    case NetworkZone::Tor:    return "tor";
    default:                  return "unknown";
  }
}

// ---------------------------------------------------------------------------
// IPv4 address (wraps the existing packed NetworkAddress for use in variant)
// ---------------------------------------------------------------------------
struct ipv4_network_address {
  uint32_t m_ip;
  uint32_t m_port;

  ipv4_network_address() : m_ip(0), m_port(0) {}
  ipv4_network_address(uint32_t ip, uint32_t port) : m_ip(ip), m_port(port) {}
  explicit ipv4_network_address(const NetworkAddress& na) : m_ip(na.ip), m_port(na.port) {}

  NetworkAddress to_packed() const {
    NetworkAddress na;
    na.ip = m_ip;
    na.port = m_port;
    return na;
  }

  uint32_t ip() const { return m_ip; }
  uint32_t port() const { return m_port; }
  static constexpr AddressType type_id() { return AddressType::IPv4; }
  static constexpr NetworkZone zone() { return NetworkZone::Public; }

  bool is_loopback() const {
    return (m_ip & 0xFF) == 127; // network byte order: first byte
  }

  bool is_local() const;

  bool operator==(const ipv4_network_address& o) const {
    return m_ip == o.m_ip && m_port == o.m_port;
  }

  bool operator!=(const ipv4_network_address& o) const { return !(*this == o); }

  bool operator<(const ipv4_network_address& o) const {
    return std::tie(m_ip, m_port) < std::tie(o.m_ip, o.m_port);
  }
};

std::ostream& operator<<(std::ostream& s, const ipv4_network_address& addr);

// ---------------------------------------------------------------------------
// I2P address (.b32.i2p destination)
// ---------------------------------------------------------------------------
struct i2p_address {
  static constexpr size_t B32_LENGTH = 52;
  static constexpr const char* B32_SUFFIX = ".b32.i2p";
  static constexpr size_t SUFFIX_LENGTH = 8;
  static constexpr size_t FULL_LENGTH = B32_LENGTH + SUFFIX_LENGTH; // 60
  static constexpr size_t HOST_SIZE = FULL_LENGTH + 1;              // 61 with null
  static constexpr uint16_t DEFAULT_PORT = 1; // SAM streams don't use ports

  char m_host[HOST_SIZE];

  i2p_address() { std::memset(m_host, 0, HOST_SIZE); }

  static bool from_string(const std::string& src, i2p_address& out);

  const char* host_str() const { return m_host; }
  uint16_t port() const { return DEFAULT_PORT; }
  static constexpr AddressType type_id() { return AddressType::I2P; }
  static constexpr NetworkZone zone() { return NetworkZone::I2P; }
  bool is_loopback() const { return false; }
  bool is_local() const { return false; }
  bool is_empty() const { return m_host[0] == '\0'; }

  bool operator==(const i2p_address& o) const { return std::strcmp(m_host, o.m_host) == 0; }
  bool operator!=(const i2p_address& o) const { return !(*this == o); }
  bool operator<(const i2p_address& o) const { return std::strcmp(m_host, o.m_host) < 0; }
};

std::ostream& operator<<(std::ostream& s, const i2p_address& addr);

// ---------------------------------------------------------------------------
// Tor address (onion v3)
// ---------------------------------------------------------------------------
struct tor_address {
  static constexpr size_t V3_LENGTH = 56;
  static constexpr const char* ONION_SUFFIX = ".onion";
  static constexpr size_t SUFFIX_LENGTH = 6;
  static constexpr size_t FULL_LENGTH = V3_LENGTH + SUFFIX_LENGTH; // 62
  static constexpr size_t HOST_SIZE = FULL_LENGTH + 1;             // 63 with null

  char m_host[HOST_SIZE];
  uint16_t m_port;

  tor_address() : m_port(0) { std::memset(m_host, 0, HOST_SIZE); }

  static bool from_string(const std::string& src, tor_address& out, uint16_t default_port = 0);

  const char* host_str() const { return m_host; }
  uint16_t port() const { return m_port; }
  static constexpr AddressType type_id() { return AddressType::Tor; }
  static constexpr NetworkZone zone() { return NetworkZone::Tor; }
  bool is_loopback() const { return false; }
  bool is_local() const { return false; }
  bool is_empty() const { return m_host[0] == '\0'; }

  bool operator==(const tor_address& o) const {
    return std::strcmp(m_host, o.m_host) == 0 && m_port == o.m_port;
  }
  bool operator!=(const tor_address& o) const { return !(*this == o); }
  bool operator<(const tor_address& o) const {
    int cmp = std::strcmp(m_host, o.m_host);
    return cmp < 0 || (cmp == 0 && m_port < o.m_port);
  }
};

std::ostream& operator<<(std::ostream& s, const tor_address& addr);

// ---------------------------------------------------------------------------
// Type-erased network address (can hold IPv4, I2P, or Tor)
// ---------------------------------------------------------------------------
class network_address {
public:
  network_address();
  network_address(const ipv4_network_address& addr);
  network_address(const i2p_address& addr);
  network_address(const tor_address& addr);
  explicit network_address(const NetworkAddress& na);

  NetworkZone get_zone() const;
  AddressType get_type_id() const;
  bool is_loopback() const;
  bool is_local() const;

  bool is_ipv4() const { return get_type_id() == AddressType::IPv4; }
  bool is_i2p() const { return get_type_id() == AddressType::I2P; }
  bool is_tor() const { return get_type_id() == AddressType::Tor; }
  bool is_anonymity() const { return is_anonymity_zone(get_zone()); }

  const ipv4_network_address& as_ipv4() const;
  const i2p_address& as_i2p() const;
  const tor_address& as_tor() const;

  NetworkAddress to_packed_ipv4() const;

  std::string str() const;
  uint32_t port() const;

  bool operator==(const network_address& o) const { return m_address == o.m_address; }
  bool operator!=(const network_address& o) const { return !(*this == o); }
  bool operator<(const network_address& o) const { return m_address < o.m_address; }

  friend std::ostream& operator<<(std::ostream& s, const network_address& addr);

private:
  boost::variant<ipv4_network_address, i2p_address, tor_address> m_address;
};

// ---------------------------------------------------------------------------
// Zone-aware peerlist entry (used for internal storage, not wire format)
// ---------------------------------------------------------------------------
struct ZonePeerlistEntry {
  network_address adr;
  PeerIdType id;
  uint64_t last_seen;

  ZonePeerlistEntry() : id(0), last_seen(0) {}
  ZonePeerlistEntry(const network_address& a, PeerIdType pid, uint64_t ls)
    : adr(a), id(pid), last_seen(ls) {}

  explicit ZonePeerlistEntry(const PeerlistEntry& pe)
    : adr(pe.adr), id(pe.id), last_seen(pe.last_seen) {}
};

// Conversion helpers
inline PeerlistEntry to_packed_peerlist_entry(const ZonePeerlistEntry& ze) {
  PeerlistEntry pe;
  pe.adr = ze.adr.to_packed_ipv4();
  pe.id = ze.id;
  pe.last_seen = ze.last_seen;
  return pe;
}

} // namespace CryptoNote
