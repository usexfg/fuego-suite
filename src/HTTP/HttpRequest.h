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

#pragma once

#include <iostream>
#include <map>
#include <string>

namespace CryptoNote {
  class HttpRequest {
  public:
    typedef std::map<std::string, std::string> Headers;

    const std::string& getMethod() const;
    const std::string& getUrl() const;
    const Headers& getHeaders() const;
    const std::string& getBody() const;

    void addHeader(const std::string& name, const std::string& value);
    void setBody(const std::string& b);
    void setUrl(const std::string& uri);

  private:
    friend class HttpParser;

    std::string method;
    std::string url;
    Headers headers;
    std::string body;

    friend std::ostream& operator<<(std::ostream& os, const HttpRequest& resp);
    std::ostream& printHttpRequest(std::ostream& os) const;
  };

  inline std::ostream& operator<<(std::ostream& os, const HttpRequest& resp) {
    return resp.printHttpRequest(os);
  }
}
