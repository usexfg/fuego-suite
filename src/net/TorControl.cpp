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

#include "TorControl.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>

#include <System/TcpConnector.h>
#include <System/Ipv4Address.h>

namespace CryptoNote {
namespace net {

TorHiddenService::TorHiddenService(System::Dispatcher& dispatcher,
                                   const std::string& control_host,
                                   uint16_t control_port)
  : m_dispatcher(dispatcher)
  , m_control_host(control_host)
  , m_control_port(control_port)
{
}

TorHiddenService::~TorHiddenService() {
  shutdown();
}

std::string TorHiddenService::read_reply(System::TcpConnection& conn) {
  // Tor control protocol: replies are lines ending in \r\n.
  // Multi-line replies use "250-" prefix, final line uses "250 " (space).
  std::string full_reply;
  while (true) {
    std::string line;
    char c;
    while (true) {
      size_t n = conn.read(reinterpret_cast<uint8_t*>(&c), 1);
      if (n == 0) throw std::runtime_error("Tor control: connection closed");
      if (c == '\n') break;
      if (c != '\r') line.push_back(c);
    }

    full_reply += line + "\n";

    // Check if this is the final line
    // Final line: "250 OK" or "5xx error" — status code followed by space
    if (line.size() >= 4 && line[3] == ' ') break;
    // Also break on error codes
    if (line.size() >= 3 && line[0] >= '4') break;
  }

  return full_reply;
}

void TorHiddenService::write_command(System::TcpConnection& conn, const std::string& cmd) {
  std::string msg = cmd + "\r\n";
  const uint8_t* data = reinterpret_cast<const uint8_t*>(msg.data());
  size_t offset = 0;
  while (offset < msg.size()) {
    offset += conn.write(data + offset, msg.size() - offset);
  }
}

bool TorHiddenService::authenticate(System::TcpConnection& conn,
                                    const std::string& cookie_path,
                                    const std::string& password) {
  if (!cookie_path.empty()) {
    // Read cookie file and send as hex
    std::ifstream cookie_file(cookie_path, std::ios::binary);
    if (!cookie_file) return false;
    std::string cookie_data((std::istreambuf_iterator<char>(cookie_file)),
                            std::istreambuf_iterator<char>());
    // Convert to hex
    std::ostringstream hex_ss;
    hex_ss << std::hex;
    for (unsigned char ch : cookie_data) {
      hex_ss << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
    }
    write_command(conn, "AUTHENTICATE " + hex_ss.str());
  } else if (!password.empty()) {
    write_command(conn, "AUTHENTICATE \"" + password + "\"");
  } else {
    write_command(conn, "AUTHENTICATE");
  }

  std::string reply = read_reply(conn);
  return reply.find("250") == 0;
}

bool TorHiddenService::create_hidden_service(uint16_t virtual_port,
                                             const std::string& target,
                                             const std::string& auth_cookie,
                                             const std::string& auth_password) {
  try {
    System::TcpConnector connector(m_dispatcher);
    m_control_conn = connector.connect(System::Ipv4Address(m_control_host), m_control_port);

    if (!authenticate(m_control_conn, auth_cookie, auth_password)) {
      return false;
    }

    // ADD_ONION: create ephemeral v3 hidden service
    // Flags=DiscardPK means we don't get the private key back (ephemeral)
    std::ostringstream cmd;
    cmd << "ADD_ONION NEW:ED25519-V3 Flags=DiscardPK Port="
        << virtual_port << "," << target;

    write_command(m_control_conn, cmd.str());
    std::string reply = read_reply(m_control_conn);

    if (reply.find("250") == std::string::npos) {
      return false;
    }

    // Parse ServiceID from reply
    // "250-ServiceID=<56char_onion_address>"
    size_t sid_pos = reply.find("ServiceID=");
    if (sid_pos == std::string::npos) return false;

    size_t start = sid_pos + 10;
    size_t end = reply.find_first_of("\n\r ", start);
    m_onion_address = reply.substr(start, end - start);

    m_active = true;
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

void TorHiddenService::remove_hidden_service() {
  if (!m_active || m_onion_address.empty()) return;

  try {
    write_command(m_control_conn, "DEL_ONION " + m_onion_address);
    read_reply(m_control_conn);
  } catch (...) {
    // Best effort
  }

  m_onion_address.clear();
  m_active = false;
}

void TorHiddenService::shutdown() {
  if (m_active) {
    remove_hidden_service();
  }
}

} // namespace net
} // namespace CryptoNote
