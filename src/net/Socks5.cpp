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

#include "Socks5.h"

#include <cstring>
#include <vector>

namespace CryptoNote {
namespace net {

const char* socks5_error_string(Socks5Error err) {
  switch (err) {
    case Socks5Error::Success:            return "success";
    case Socks5Error::ConnectionFailed:   return "connection to proxy failed";
    case Socks5Error::AuthRejected:       return "proxy rejected authentication method";
    case Socks5Error::ConnectRejected:    return "proxy rejected CONNECT request";
    case Socks5Error::ProtocolError:      return "SOCKS5 protocol error";
    case Socks5Error::Timeout:            return "proxy connection timed out";
    case Socks5Error::HostUnreachable:    return "target host unreachable via proxy";
    case Socks5Error::NetworkUnreachable: return "network unreachable via proxy";
    case Socks5Error::ConnectionRefused:  return "target connection refused via proxy";
    default:                              return "unknown SOCKS5 error";
  }
}

static void write_all(System::TcpConnection& conn, const uint8_t* data, size_t size) {
  size_t offset = 0;
  while (offset < size) {
    offset += conn.write(data + offset, size - offset);
  }
}

static void read_all(System::TcpConnection& conn, uint8_t* data, size_t size) {
  size_t offset = 0;
  while (offset < size) {
    size_t n = conn.read(data + offset, size - offset);
    if (n == 0) throw std::runtime_error("SOCKS5: connection closed during read");
    offset += n;
  }
}

Socks5Error socks5_connect(
  System::TcpConnection& connection,
  const std::string& target_host,
  uint16_t target_port)
{
  // Phase 1: Version + authentication method negotiation
  // Client sends: VER(1) NMETHODS(1) METHODS(1..255)
  // We offer no-auth only
  uint8_t greeting[3] = { socks5::VERSION, 1, socks5::AUTH_NONE };
  write_all(connection, greeting, sizeof(greeting));

  // Server responds: VER(1) METHOD(1)
  uint8_t greeting_reply[2];
  read_all(connection, greeting_reply, sizeof(greeting_reply));

  if (greeting_reply[0] != socks5::VERSION) {
    return Socks5Error::ProtocolError;
  }

  if (greeting_reply[1] == socks5::AUTH_REJECT) {
    return Socks5Error::AuthRejected;
  }

  if (greeting_reply[1] != socks5::AUTH_NONE) {
    return Socks5Error::AuthRejected;
  }

  // Phase 2: CONNECT request
  // VER(1) CMD(1) RSV(1) ATYP(1) DST.ADDR(variable) DST.PORT(2)
  // Use ATYP=0x03 (domain) so DNS resolution happens at the proxy
  if (target_host.size() > 255) {
    return Socks5Error::ProtocolError;
  }

  std::vector<uint8_t> request;
  request.reserve(4 + 1 + target_host.size() + 2);
  request.push_back(socks5::VERSION);
  request.push_back(socks5::CMD_CONNECT);
  request.push_back(0x00); // reserved
  request.push_back(socks5::ATYP_DOMAIN);
  request.push_back(static_cast<uint8_t>(target_host.size()));
  request.insert(request.end(), target_host.begin(), target_host.end());
  request.push_back(static_cast<uint8_t>((target_port >> 8) & 0xFF));
  request.push_back(static_cast<uint8_t>(target_port & 0xFF));

  write_all(connection, request.data(), request.size());

  // Server responds: VER(1) REP(1) RSV(1) ATYP(1) BND.ADDR(variable) BND.PORT(2)
  // Read the fixed header first
  uint8_t reply_hdr[4];
  read_all(connection, reply_hdr, sizeof(reply_hdr));

  if (reply_hdr[0] != socks5::VERSION) {
    return Socks5Error::ProtocolError;
  }

  // Map reply codes to errors
  if (reply_hdr[1] != socks5::REPLY_SUCCESS) {
    switch (reply_hdr[1]) {
      case 0x03: return Socks5Error::NetworkUnreachable;
      case 0x04: return Socks5Error::HostUnreachable;
      case 0x05: return Socks5Error::ConnectionRefused;
      default:   return Socks5Error::ConnectRejected;
    }
  }

  // Read and discard the bind address (we don't need it)
  uint8_t atyp = reply_hdr[3];
  if (atyp == socks5::ATYP_IPV4) {
    uint8_t discard[4 + 2]; // 4 bytes IP + 2 bytes port
    read_all(connection, discard, sizeof(discard));
  } else if (atyp == socks5::ATYP_DOMAIN) {
    uint8_t domain_len;
    read_all(connection, &domain_len, 1);
    std::vector<uint8_t> discard(domain_len + 2); // domain + 2 bytes port
    read_all(connection, discard.data(), discard.size());
  } else if (atyp == socks5::ATYP_IPV6) {
    uint8_t discard[16 + 2]; // 16 bytes IPv6 + 2 bytes port
    read_all(connection, discard, sizeof(discard));
  } else {
    return Socks5Error::ProtocolError;
  }

  return Socks5Error::Success;
}

Socks5ConnectResult socks5_connect_to(
  System::Dispatcher& dispatcher,
  const std::string& proxy_host,
  uint16_t proxy_port,
  const std::string& target_host,
  uint16_t target_port)
{
  Socks5ConnectResult result;
  result.error = Socks5Error::ConnectionFailed;

  try {
    System::TcpConnector connector(dispatcher);
    result.connection = connector.connect(System::Ipv4Address(proxy_host), proxy_port);
  } catch (...) {
    result.error = Socks5Error::ConnectionFailed;
    return result;
  }

  result.error = socks5_connect(result.connection, target_host, target_port);
  return result;
}

} // namespace net
} // namespace CryptoNote
