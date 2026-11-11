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

#include "NetworkAddressTypes.h"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "../Common/StringTools.h"

namespace CryptoNote {

// ---------------------------------------------------------------------------
// ipv4_network_address
// ---------------------------------------------------------------------------

bool ipv4_network_address::is_local() const {
  uint8_t first = m_ip & 0xFF;
  if (first == 10) return true;                  // 10.0.0.0/8
  if (first == 127) return true;                 // 127.0.0.0/8
  if (first == 172) {
    uint8_t second = (m_ip >> 8) & 0xFF;
    if (second >= 16 && second <= 31) return true; // 172.16.0.0/12
  }
  if (first == 192) {
    uint8_t second = (m_ip >> 8) & 0xFF;
    if (second == 168) return true;              // 192.168.0.0/16
  }
  return false;
}

std::ostream& operator<<(std::ostream& s, const ipv4_network_address& addr) {
  return s << Common::ipAddressToString(addr.m_ip) << ":" << addr.m_port;
}

// ---------------------------------------------------------------------------
// i2p_address
// ---------------------------------------------------------------------------

static bool is_valid_b32_char(char c) {
  return (c >= 'a' && c <= 'z') || (c >= '2' && c <= '7');
}

bool i2p_address::from_string(const std::string& src, i2p_address& out) {
  std::memset(out.m_host, 0, HOST_SIZE);

  if (src.size() != FULL_LENGTH) return false;

  // Check suffix
  if (src.compare(B32_LENGTH, SUFFIX_LENGTH, B32_SUFFIX) != 0) return false;

  // Validate b32 characters
  for (size_t i = 0; i < B32_LENGTH; ++i) {
    if (!is_valid_b32_char(src[i])) return false;
  }

  std::memcpy(out.m_host, src.data(), FULL_LENGTH);
  out.m_host[FULL_LENGTH] = '\0';
  return true;
}

std::ostream& operator<<(std::ostream& s, const i2p_address& addr) {
  return s << addr.m_host << ":" << addr.port();
}

// ---------------------------------------------------------------------------
// tor_address
// ---------------------------------------------------------------------------

static bool is_valid_onion_char(char c) {
  return (c >= 'a' && c <= 'z') || (c >= '2' && c <= '7');
}

bool tor_address::from_string(const std::string& src, tor_address& out, uint16_t default_port) {
  std::memset(out.m_host, 0, HOST_SIZE);
  out.m_port = default_port;

  // Parse host:port or just host
  std::string host_part = src;
  size_t colon = src.rfind(':');
  if (colon != std::string::npos) {
    std::string port_str = src.substr(colon + 1);
    // Only treat as port if what follows the colon is numeric
    bool all_digits = !port_str.empty();
    for (char c : port_str) {
      if (!std::isdigit(static_cast<unsigned char>(c))) { all_digits = false; break; }
    }
    if (all_digits) {
      try {
        unsigned long p = std::stoul(port_str);
        if (p > 65535) return false;
        out.m_port = static_cast<uint16_t>(p);
      } catch (...) {
        return false;
      }
      host_part = src.substr(0, colon);
    }
  }

  if (host_part.size() != FULL_LENGTH) return false;

  // Check .onion suffix
  if (host_part.compare(V3_LENGTH, SUFFIX_LENGTH, ONION_SUFFIX) != 0) return false;

  // Validate base32 characters in the address portion
  for (size_t i = 0; i < V3_LENGTH; ++i) {
    if (!is_valid_onion_char(host_part[i])) return false;
  }

  std::memcpy(out.m_host, host_part.data(), FULL_LENGTH);
  out.m_host[FULL_LENGTH] = '\0';
  return true;
}

std::ostream& operator<<(std::ostream& s, const tor_address& addr) {
  return s << addr.m_host << ":" << addr.m_port;
}

// ---------------------------------------------------------------------------
// network_address (type-erased wrapper via boost::variant)
// ---------------------------------------------------------------------------

namespace {

struct zone_visitor : boost::static_visitor<NetworkZone> {
  template<typename T>
  NetworkZone operator()(const T& addr) const { return T::zone(); }
};

struct type_id_visitor : boost::static_visitor<AddressType> {
  template<typename T>
  AddressType operator()(const T& addr) const { return T::type_id(); }
};

struct loopback_visitor : boost::static_visitor<bool> {
  template<typename T>
  bool operator()(const T& addr) const { return addr.is_loopback(); }
};

struct local_visitor : boost::static_visitor<bool> {
  template<typename T>
  bool operator()(const T& addr) const { return addr.is_local(); }
};

struct str_visitor : boost::static_visitor<std::string> {
  std::string operator()(const ipv4_network_address& addr) const {
    std::ostringstream oss;
    oss << addr;
    return oss.str();
  }
  std::string operator()(const i2p_address& addr) const {
    return std::string(addr.host_str());
  }
  std::string operator()(const tor_address& addr) const {
    std::ostringstream oss;
    oss << addr;
    return oss.str();
  }
};

struct port_visitor : boost::static_visitor<uint32_t> {
  uint32_t operator()(const ipv4_network_address& addr) const { return addr.port(); }
  uint32_t operator()(const i2p_address& addr) const { return addr.port(); }
  uint32_t operator()(const tor_address& addr) const { return addr.port(); }
};

struct ostream_visitor : boost::static_visitor<std::ostream&> {
  std::ostream& m_os;
  explicit ostream_visitor(std::ostream& os) : m_os(os) {}
  template<typename T>
  std::ostream& operator()(const T& addr) const { return m_os << addr; }
};

} // anonymous namespace

network_address::network_address() : m_address(ipv4_network_address{}) {}
network_address::network_address(const ipv4_network_address& addr) : m_address(addr) {}
network_address::network_address(const i2p_address& addr) : m_address(addr) {}
network_address::network_address(const tor_address& addr) : m_address(addr) {}
network_address::network_address(const NetworkAddress& na) : m_address(ipv4_network_address(na)) {}

NetworkZone network_address::get_zone() const {
  return boost::apply_visitor(zone_visitor{}, m_address);
}

AddressType network_address::get_type_id() const {
  return boost::apply_visitor(type_id_visitor{}, m_address);
}

bool network_address::is_loopback() const {
  return boost::apply_visitor(loopback_visitor{}, m_address);
}

bool network_address::is_local() const {
  return boost::apply_visitor(local_visitor{}, m_address);
}

const ipv4_network_address& network_address::as_ipv4() const {
  return boost::get<ipv4_network_address>(m_address);
}

const i2p_address& network_address::as_i2p() const {
  return boost::get<i2p_address>(m_address);
}

const tor_address& network_address::as_tor() const {
  return boost::get<tor_address>(m_address);
}

NetworkAddress network_address::to_packed_ipv4() const {
  if (is_ipv4()) {
    return as_ipv4().to_packed();
  }
  return NetworkAddress{0, 0};
}

std::string network_address::str() const {
  return boost::apply_visitor(str_visitor{}, m_address);
}

uint32_t network_address::port() const {
  return boost::apply_visitor(port_visitor{}, m_address);
}

std::ostream& operator<<(std::ostream& s, const network_address& addr) {
  return boost::apply_visitor(ostream_visitor(s), addr.m_address);
}

} // namespace CryptoNote
