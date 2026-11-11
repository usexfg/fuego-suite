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

#ifndef HTTPPARSER_H_
#define HTTPPARSER_H_

#include <iostream>
#include <map>
#include <string>
#include "HttpRequest.h"
#include "HttpResponse.h"

namespace CryptoNote {

//Blocking HttpParser
class HttpParser {
public:
  HttpParser() {};

  void receiveRequest(std::istream& stream, HttpRequest& request);
  void receiveResponse(std::istream& stream, HttpResponse& response);
  static HttpResponse::HTTP_STATUS parseResponseStatusFromString(const std::string& status);
private:
  void readWord(std::istream& stream, std::string& word);
  void readHeaders(std::istream& stream, HttpRequest::Headers &headers);
  bool readHeader(std::istream& stream, std::string& name, std::string& value);
  size_t getBodyLen(const HttpRequest::Headers& headers);
  void readBody(std::istream& stream, std::string& body, const size_t bodyLen);
};

} //namespace CryptoNote

#endif /* HTTPPARSER_H_ */
