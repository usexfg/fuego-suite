// Copyright (c) 2017-2022 Fuego Developers
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

#include "HttpRequest.h"

namespace CryptoNote {

  const std::string& HttpRequest::getMethod() const {
    return method;
  }

  const std::string& HttpRequest::getUrl() const {
    return url;
  }

  const HttpRequest::Headers& HttpRequest::getHeaders() const {
    return headers;
  }

  const std::string& HttpRequest::getBody() const {
    return body;
  }

  void HttpRequest::addHeader(const std::string& name, const std::string& value) {
    headers[name] = value;
  }
  void HttpRequest::setBody(const std::string& b) {
    body = b;
    if (!body.empty()) {
      headers["Content-Length"] = std::to_string(body.size());
    }
    else {
      headers.erase("Content-Length");
    }
  }

  void HttpRequest::setUrl(const std::string& u) {
    url = u;
  }

  std::ostream& HttpRequest::printHttpRequest(std::ostream& os) const {
    os << "POST " << url << " HTTP/1.1\r\n";
    auto host = headers.find("Host");
    if (host == headers.end()) {
      os << "Host: " << "127.0.0.1" << "\r\n";
    }

    for (auto pair : headers) {
      os << pair.first << ": " << pair.second << "\r\n";
    }
    
    os << "\r\n";
    if (!body.empty()) {
      os << body;
    }

    return os;
  }
}
