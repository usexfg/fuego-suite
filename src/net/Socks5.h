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
#include <string>

#include <System/Dispatcher.h>
#include <System/TcpConnection.h>
#include <System/TcpConnector.h>
#include <System/Ipv4Address.h>

namespace CryptoNote {
namespace net {

// SOCKS5 protocol constants
namespace socks5 {
  constexpr uint8_t VERSION       = 0x05;
  constexpr uint8_t AUTH_NONE     = 0x00;
  constexpr uint8_t AUTH_USERPASS = 0x02;
  constexpr uint8_t AUTH_REJECT   = 0xFF;
  constexpr uint8_t CMD_CONNECT   = 0x01;
  constexpr uint8_t ATYP_IPV4     = 0x01;
  constexpr uint8_t ATYP_DOMAIN   = 0x03;
  constexpr uint8_t ATYP_IPV6     = 0x04;
  constexpr uint8_t REPLY_SUCCESS = 0x00;
}

// SOCKS5 connection errors
enum class Socks5Error {
  Success = 0,
  ConnectionFailed,
  AuthRejected,
  ConnectRejected,
  ProtocolError,
  Timeout,
  HostUnreachable,
  NetworkUnreachable,
  ConnectionRefused
};

const char* socks5_error_string(Socks5Error err);

// Performs SOCKS5 handshake + CONNECT on an already-established TCP connection.
// After success, the connection is tunneled to the target.
//
// Uses domain-type (ATYP=0x03) CONNECT so DNS resolution happens at the proxy,
// not at the node — critical for preventing DNS leaks with I2P/Tor addresses.
Socks5Error socks5_connect(
  System::TcpConnection& connection,
  const std::string& target_host,
  uint16_t target_port
);

// Connects to a SOCKS5 proxy, negotiates the handshake, and issues a CONNECT
// to the target host:port. Returns the tunneled TcpConnection on success.
// Throws System::InterruptedException on timeout/interrupt.
struct Socks5ConnectResult {
  Socks5Error error;
  System::TcpConnection connection;
};

Socks5ConnectResult socks5_connect_to(
  System::Dispatcher& dispatcher,
  const std::string& proxy_host,
  uint16_t proxy_port,
  const std::string& target_host,
  uint16_t target_port
);

} // namespace net
} // namespace CryptoNote
