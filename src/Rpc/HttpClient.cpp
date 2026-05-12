// Copyright (c) 2017-2025 Elderfire Privacy Council
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

#include "HttpClient.h"

#include <HTTP/HttpParser.h>
#include <System/Ipv4Resolver.h>
#include <System/Ipv4Address.h>
#include <System/TcpConnector.h>

namespace CryptoNote {

HttpClient::HttpClient(System::Dispatcher& dispatcher, const std::string& address, uint16_t port) :
  m_dispatcher(dispatcher), m_address(address), m_port(port) {
}

HttpClient::~HttpClient() {
  if (m_connected) {
    disconnect();
  }
}

void HttpClient::request(const HttpRequest &req, HttpResponse &res) {
  if (!m_connected) {
    connect();
  }

  try {
    std::iostream stream(m_streamBuf.get());
    HttpParser parser;
    stream << req;
    stream.flush();
    parser.receiveResponse(stream, res);
  } catch (const std::exception &) {
    disconnect();
    throw;
  }
}

void HttpClient::connect() {
  try {
    auto ipAddr = System::Ipv4Resolver(m_dispatcher).resolve(m_address);
    m_connection = System::TcpConnector(m_dispatcher).connect(ipAddr, m_port);
    m_streamBuf.reset(new System::TcpStreambuf(m_connection));
    m_connected = true;
  } catch (const std::exception& e) {
    throw ConnectException(e.what());
  }
}

bool HttpClient::isConnected() const {
  return m_connected;
}

void HttpClient::disconnect() {
  m_streamBuf.reset();
  try {
    m_connection.write(nullptr, 0); //Socket shutdown.
  } catch (std::exception&) {
    //Ignoring possible exception.
  }

  try {
    m_connection = System::TcpConnection();
  } catch (std::exception&) {
    //Ignoring possible exception.
  }

  m_connected = false;
}

ConnectException::ConnectException(const std::string& whatArg) : std::runtime_error(whatArg.c_str()) {
}

}
