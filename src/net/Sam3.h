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
#include <functional>
#include <atomic>
#include <thread>

#include <System/Dispatcher.h>
#include <System/TcpConnection.h>

namespace CryptoNote {
namespace net {

// SAM v3.1 session for I2P inbound connections.
// Protocol: connect to i2pd SAM bridge, create a STREAM session, then
// accept incoming I2P streams which are forwarded as TCP connections.
//
// Flow:
//   1. HELLO VERSION handshake
//   2. SESSION CREATE STYLE=STREAM (get our .b32.i2p destination)
//   3. STREAM ACCEPT loop (each accept yields a TCP stream from an I2P peer)
class Sam3Session {
public:
  Sam3Session(System::Dispatcher& dispatcher,
              const std::string& sam_host = "127.0.0.1",
              uint16_t sam_port = 7656);
  ~Sam3Session();

  // Create the SAM session. Returns true on success and populates our_destination.
  bool create_session(const std::string& session_id = "fuego");

  // Get our base32 I2P address (e.g., abcdef...xyz.b32.i2p)
  std::string get_b32_address() const { return m_b32_address; }

  // Get the raw base64 destination (full I2P destination key)
  std::string get_destination() const { return m_destination; }

  // Accept one incoming I2P stream. Blocks until a peer connects.
  // Returns a TcpConnection to the forwarded stream.
  System::TcpConnection accept_stream();

  // Shut down the SAM session
  void shutdown();

  bool is_active() const { return m_active; }

private:
  System::Dispatcher& m_dispatcher;
  std::string m_sam_host;
  uint16_t m_sam_port;
  std::string m_session_id;
  std::string m_destination;
  std::string m_b32_address;
  std::atomic<bool> m_active{false};

  System::TcpConnection m_control_conn;

  bool do_hello(System::TcpConnection& conn);
  std::string read_line(System::TcpConnection& conn);
  void write_line(System::TcpConnection& conn, const std::string& line);

  // Convert base64 I2P destination to .b32.i2p address
  static std::string destination_to_b32(const std::string& dest_b64);
};

} // namespace net
} // namespace CryptoNote
