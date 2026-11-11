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
#include <atomic>

#include <System/Dispatcher.h>
#include <System/TcpConnection.h>

namespace CryptoNote {
namespace net {

// Tor control port client for creating ephemeral hidden services.
// Uses ADD_ONION to create an onion v3 service that maps to a local TCP port.
//
// Flow:
//   1. Connect to Tor control port (default 9051)
//   2. AUTHENTICATE (cookie or password)
//   3. ADD_ONION NEW:ED25519-V3 Port=<virtual_port>,<target>
//   4. Read back the ServiceID (.onion address)
class TorHiddenService {
public:
  TorHiddenService(System::Dispatcher& dispatcher,
                   const std::string& control_host = "127.0.0.1",
                   uint16_t control_port = 9051);
  ~TorHiddenService();

  // Create an ephemeral hidden service.
  //   virtual_port:  port that .onion:virtual_port maps to
  //   target:        local address to forward to (e.g. "127.0.0.1:18080")
  //   auth_cookie:   path to Tor's control_auth_cookie file (empty = no auth)
  //   auth_password:  control port password (empty = no password)
  // Returns true on success, populates onion_address.
  bool create_hidden_service(uint16_t virtual_port,
                            const std::string& target,
                            const std::string& auth_cookie = "",
                            const std::string& auth_password = "");

  // Get the .onion address (without .onion suffix, 56 chars for v3)
  std::string get_onion_address() const { return m_onion_address; }

  // Get the full .onion hostname
  std::string get_onion_host() const { return m_onion_address + ".onion"; }

  // Remove the hidden service
  void remove_hidden_service();

  void shutdown();

  bool is_active() const { return m_active; }

private:
  System::Dispatcher& m_dispatcher;
  std::string m_control_host;
  uint16_t m_control_port;
  std::string m_onion_address;
  std::atomic<bool> m_active{false};

  System::TcpConnection m_control_conn;

  std::string read_reply(System::TcpConnection& conn);
  void write_command(System::TcpConnection& conn, const std::string& cmd);
  bool authenticate(System::TcpConnection& conn,
                   const std::string& cookie_path,
                   const std::string& password);
};

} // namespace net
} // namespace CryptoNote
