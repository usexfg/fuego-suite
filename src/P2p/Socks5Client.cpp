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

#include "Socks5Client.h"

#include <cstring>
#include <stdexcept>

#include <System/Dispatcher.h>
#include <System/TcpConnection.h>
#include <System/TcpConnector.h>
#include <System/Ipv4Address.h>
#include <System/Ipv4Resolver.h>

namespace CryptoNote {

Socks5Client::Socks5Client(System::Dispatcher& dispatcher,
                           const std::string& proxyHost,
                           uint16_t proxyPort)
  : m_dispatcher(dispatcher)
  , m_proxyHost(proxyHost)
  , m_proxyPort(proxyPort)
{}

System::TcpConnection Socks5Client::connect(const std::string& destinationHost, uint16_t destinationPort) {
  System::Ipv4Resolver resolver(m_dispatcher);
  System::TcpConnector connector(m_dispatcher);

  auto proxyAddr = resolver.resolve(m_proxyHost);
  System::TcpConnection conn = connector.connect(proxyAddr, m_proxyPort);

  socks5Handshake(conn);
  socks5ConnectDomain(conn, destinationHost, destinationPort);

  return conn;
}

System::TcpConnection Socks5Client::connect(System::Ipv4Address destinationIp, uint16_t destinationPort) {
  System::Ipv4Resolver resolver(m_dispatcher);
  System::TcpConnector connector(m_dispatcher);

  auto proxyAddr = resolver.resolve(m_proxyHost);
  System::TcpConnection conn = connector.connect(proxyAddr, m_proxyPort);

  socks5Handshake(conn);
  socks5ConnectIPv4(conn, destinationIp.getValue(), destinationPort);

  return conn;
}

void Socks5Client::socks5Handshake(System::TcpConnection& connection) {
  // Phase 1: Authentication method selection
  uint8_t methodRequest[] = { 0x05, 0x01, 0x00 }; // SOCKS5, 1 method, no-auth
  connection.write(methodRequest, sizeof(methodRequest));

  uint8_t methodResponse[2] = { 0 };
  readExactly(connection, methodResponse, 2);

  if (methodResponse[0] != 0x05) {
    throw std::runtime_error("SOCKS5 proxy: invalid version in method response");
  }
  if (methodResponse[1] == 0xFF) {
    throw std::runtime_error("SOCKS5 proxy: no acceptable authentication method");
  }
  if (methodResponse[1] != 0x00) {
    throw std::runtime_error("SOCKS5 proxy: unexpected auth method " + std::to_string(methodResponse[1]));
  }
}

void Socks5Client::socks5ConnectIPv4(System::TcpConnection& connection,
                                      uint32_t ip, uint16_t port) {
  uint8_t request[10] = {
    0x05,                   // SOCKS version
    0x01,                   // CONNECT command
    0x00,                   // reserved
    0x01,                   // IPv4 address type
    uint8_t((ip >> 24) & 0xFF),
    uint8_t((ip >> 16) & 0xFF),
    uint8_t((ip >> 8) & 0xFF),
    uint8_t(ip & 0xFF),
    uint8_t((port >> 8) & 0xFF),
    uint8_t(port & 0xFF)
  };

  connection.write(request, sizeof(request));

  // IPv4 response is always 10 bytes
  uint8_t response[10] = { 0 };
  readExactly(connection, response, 10);

  if (response[0] != 0x05) {
    throw std::runtime_error("SOCKS5 proxy: invalid version in connect response");
  }

  checkSocks5Response(response[1]);
}

void Socks5Client::socks5ConnectDomain(System::TcpConnection& connection,
                                        const std::string& host, uint16_t port) {
  if (host.size() > 255) {
    throw std::runtime_error("SOCKS5 proxy: hostname too long");
  }

  uint8_t hostLen = static_cast<uint8_t>(host.size());

  // Build request: header(4) + 1(len) + host + 2(port)
  std::vector<uint8_t> request;
  request.reserve(4 + 1 + hostLen + 2);
  request.push_back(0x05);            // SOCKS version
  request.push_back(0x01);            // CONNECT command
  request.push_back(0x00);            // reserved
  request.push_back(0x03);            // domain name type
  request.push_back(hostLen);          // domain length
  request.insert(request.end(), host.begin(), host.end());
  request.push_back(uint8_t((port >> 8) & 0xFF));
  request.push_back(uint8_t(port & 0xFF));

  connection.write(request.data(), request.size());

  // Read response header: 4 bytes (version, reply, reserved, addrType)
  uint8_t header[4] = { 0 };
  readExactly(connection, header, 4);

  if (header[0] != 0x05) {
    throw std::runtime_error("SOCKS5 proxy: invalid version in connect response");
  }

  uint8_t replyCode = header[1];
  uint8_t addrType = header[3];

  // Read remaining bound address based on type
  switch (addrType) {
    case 0x01: { // IPv4
      uint8_t remaining[4 + 2] = { 0 }; // 4 bytes IP + 2 bytes port
      readExactly(connection, remaining, sizeof(remaining));
      break;
    }
    case 0x03: { // Domain name
      uint8_t lenByte[1] = { 0 };
      readExactly(connection, lenByte, 1);
      uint8_t drainLen = lenByte[0] + 2; // domain + 2 port bytes
      std::vector<uint8_t> drain(drainLen);
      readExactly(connection, drain.data(), drainLen);
      break;
    }
    case 0x04: { // IPv6
      uint8_t remaining[16 + 2] = { 0 }; // 16 bytes IP + 2 bytes port
      readExactly(connection, remaining, sizeof(remaining));
      break;
    }
    default:
      throw std::runtime_error("SOCKS5 proxy: unknown address type in response");
  }

  checkSocks5Response(replyCode);
}

void Socks5Client::readExactly(System::TcpConnection& connection, uint8_t* buf, size_t size) {
  size_t offset = 0;
  while (offset < size) {
    size_t n = connection.read(buf + offset, size - offset);
    if (n == 0) {
      throw std::runtime_error("SOCKS5 proxy: connection closed while reading");
    }
    offset += n;
  }
}

void Socks5Client::checkSocks5Response(uint8_t replyCode) {
  switch (replyCode) {
    case 0x00: return; // success
    case 0x01: throw std::runtime_error("SOCKS5: general SOCKS server failure");
    case 0x02: throw std::runtime_error("SOCKS5: connection not allowed by ruleset");
    case 0x03: throw std::runtime_error("SOCKS5: network unreachable");
    case 0x04: throw std::runtime_error("SOCKS5: host unreachable");
    case 0x05: throw std::runtime_error("SOCKS5: connection refused");
    case 0x06: throw std::runtime_error("SOCKS5: TTL expired");
    case 0x07: throw std::runtime_error("SOCKS5: command not supported");
    case 0x08: throw std::runtime_error("SOCKS5: address type not supported");
    default:   throw std::runtime_error("SOCKS5: unknown error code " + std::to_string(replyCode));
  }
}

} // namespace CryptoNote