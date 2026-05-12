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

#pragma once

#include <memory>

#include <Common/Base64.h>
#include <HTTP/HttpRequest.h>
#include <HTTP/HttpResponse.h>
#include <System/TcpConnection.h>
#include <System/TcpStream.h>
#include "JsonRpc.h"

#include "Serialization/SerializationTools.h"

namespace CryptoNote {

class ConnectException : public std::runtime_error  {
public:
  ConnectException(const std::string& whatArg);
};

class HttpClient {
public:

  HttpClient(System::Dispatcher& dispatcher, const std::string& address, uint16_t port);
  ~HttpClient();
  void request(const HttpRequest& req, HttpResponse& res);
  
  bool isConnected() const;

private:
  void connect();
  void disconnect();

  const std::string m_address;
  const uint16_t m_port;

  bool m_connected = false;
  System::Dispatcher& m_dispatcher;
  System::TcpConnection m_connection;
  std::unique_ptr<System::TcpStreambuf> m_streamBuf;
};

template <typename Request, typename Response>
void invokeJsonCommand(HttpClient& client, const std::string& url, const Request& req, Response& res, const std::string& user = "", const std::string& password = "") {
  HttpRequest hreq;
  HttpResponse hres;

  hreq.addHeader("Content-Type", "application/json");
  if (!user.empty() || !password.empty()) {
    hreq.addHeader("Authorization", "Basic " + Tools::Base64::encode(user + ":" + password));
  }
  hreq.setUrl(url);
  hreq.setBody(storeToJson(req));
  client.request(hreq, hres);

  if (hres.getStatus() != HttpResponse::STATUS_200) {
    throw std::runtime_error("HTTP status: " + std::to_string(hres.getStatus()));
  }

  if (!loadFromJson(res, hres.getBody())) {
    throw std::runtime_error("Failed to parse JSON response");
  }
}

template <typename Request, typename Response>
void invokeJsonRpcCommand(HttpClient& client, const std::string& method, const Request& req, Response& res, const std::string& user = "", const std::string& password = "") {
  try {

    JsonRpc::JsonRpcRequest jsReq;

    jsReq.setMethod(method);
    jsReq.setParams(req);

    HttpRequest httpReq;
    HttpResponse httpRes;

    httpReq.addHeader("Content-Type", "application/json");
    if (!user.empty() || !password.empty()) {
      httpReq.addHeader("Authorization", "Basic " + Tools::Base64::encode(user + ":" + password));
    }
    httpReq.setUrl("/json_rpc");
    httpReq.setBody(jsReq.getBody());

    client.request(httpReq, httpRes);

    JsonRpc::JsonRpcResponse jsRes;

    //if (httpRes.getStatus() == HttpResponse::STATUS_200) {
      jsRes.parse(httpRes.getBody());
      if (!jsRes.getResult(res)) {
        throw std::runtime_error("HTTP status: " + std::to_string(httpRes.getStatus()));
      }
    //}

  } catch (const ConnectException&) {
    throw std::runtime_error("HTTP status: CONNECT_ERROR");
  } catch (const std::exception&) {
    throw std::runtime_error("HTTP status: NETWORK_ERROR");
  }
}

template <typename Request, typename Response>
void invokeBinaryCommand(HttpClient& client, const std::string& url, const Request& req, Response& res, const std::string& user = "", const std::string& password = "") {
  HttpRequest hreq;
  HttpResponse hres;

  if (!user.empty() || !password.empty()) {
    hreq.addHeader("Authorization", "Basic " + Tools::Base64::encode(user + ":" + password));
  }
  hreq.setUrl(url);
  hreq.setBody(storeToBinaryKeyValue(req));
  client.request(hreq, hres);

  if (!loadFromBinaryKeyValue(res, hres.getBody())) {
    throw std::runtime_error("Failed to parse binary response");
  }
}
  
}
