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

namespace System {
class Dispatcher;
class TcpConnection;
class Ipv4Address;
}

namespace CryptoNote {

class Socks5Client {
public:
  Socks5Client(System::Dispatcher& dispatcher,
               const std::string& proxyHost,
               uint16_t proxyPort);

  System::TcpConnection connect(const std::string& destinationHost, uint16_t destinationPort);
  System::TcpConnection connect(System::Ipv4Address destinationIp, uint16_t destinationPort);

private:
  void socks5Handshake(System::TcpConnection& connection);
  void socks5ConnectIPv4(System::TcpConnection& connection,
                         uint32_t ip, uint16_t port);
  void socks5ConnectDomain(System::TcpConnection& connection,
                           const std::string& host, uint16_t port);

  void readExactly(System::TcpConnection& connection, uint8_t* buf, size_t size);
  void checkSocks5Response(uint8_t replyCode);

  System::Dispatcher& m_dispatcher;
  std::string m_proxyHost;
  uint16_t m_proxyPort;
};

} // namespace CryptoNote