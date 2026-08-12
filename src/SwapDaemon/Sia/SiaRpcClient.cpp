// Copyright (c) 2017-2026 Fuego Developers

#include "SiaRpcClient.h"
#include "Common/JsonValue.h"

#include <HTTP/httplib.h>
#include <sstream>

namespace XfgSwap {

SiaRpcClient::SiaRpcClient(const std::string& host, uint16_t port,
                           const std::string& apiPassword)
  : m_host(host), m_port(port), m_apiPassword(apiPassword) {}

std::string SiaRpcClient::http(const std::string& method, const std::string& path,
                               const std::string& body) {
  std::string host = m_host;
  if (host.rfind("http://", 0) == 0) host = host.substr(7);
  if (host.rfind("https://", 0) == 0) host = host.substr(8);
  auto slash = host.find('/');
  if (slash != std::string::npos) host = host.substr(0, slash);

  try {
    httplib::Client cli(host, m_port == 0 ? 9980 : m_port);
    cli.set_connection_timeout(10, 0);
    cli.set_read_timeout(60, 0);
    // Sia API: User-Agent required; auth is ":"+password basic
    httplib::Headers headers = {
      {"User-Agent", "Sia-Agent"},
      {"Content-Type", "application/json"}
    };
    if (!m_apiPassword.empty()) {
      cli.set_basic_auth("", m_apiPassword.c_str());
    }
    if (method == "GET") {
      auto res = cli.Get(path.c_str(), headers);
      if (!res || res->status < 200 || res->status >= 300) return {};
      return res->body;
    }
    auto res = cli.Post(path.c_str(), headers, body, "application/x-www-form-urlencoded");
    if (!res || res->status < 200 || res->status >= 300) {
      // try JSON body for walletd
      res = cli.Post(path.c_str(), headers, body, "application/json");
      if (!res || res->status < 200 || res->status >= 300) return {};
    }
    return res->body;
  } catch (...) {
    return {};
  }
}

bool SiaRpcClient::getBlockHeight(uint64_t& height) {
  std::string body = http("GET", "/consensus");
  if (body.empty()) return false;
  try {
    auto root = Common::JsonValue::fromString(body);
    if (root.isObject() && root.contains("height")) {
      height = static_cast<uint64_t>(root("height").getInteger());
      return true;
    }
  } catch (...) {}
  // walletd: /api/consensus/tip
  body = http("GET", "/api/consensus/tip");
  if (body.empty()) return false;
  try {
    auto root = Common::JsonValue::fromString(body);
    if (root.isObject() && root.contains("height")) {
      height = static_cast<uint64_t>(root("height").getInteger());
      return true;
    }
  } catch (...) {}
  return false;
}

bool SiaRpcClient::getBalance(uint64_t& hastings) {
  std::string body = http("GET", "/wallet");
  if (body.empty()) return false;
  try {
    auto root = Common::JsonValue::fromString(body);
    // confirmedsiacoinbalance is string of hastings
    if (root.isObject() && root.contains("confirmedsiacoinbalance")) {
      if (root("confirmedsiacoinbalance").isString())
        hastings = std::stoull(root("confirmedsiacoinbalance").getString());
      else
        hastings = static_cast<uint64_t>(root("confirmedsiacoinbalance").getInteger());
      return true;
    }
  } catch (...) {}
  return false;
}

bool SiaRpcClient::getAddress(std::string& address) {
  std::string body = http("GET", "/wallet/address");
  if (body.empty()) return false;
  try {
    auto root = Common::JsonValue::fromString(body);
    if (root.isObject() && root.contains("address")) {
      address = root("address").getString();
      return !address.empty();
    }
  } catch (...) {}
  return false;
}

bool SiaRpcClient::sendSiacoins(const std::string& destAddress,
                                uint64_t amountHastings,
                                const std::string& memo,
                                std::string& txid) {
  // siad: POST /wallet/siacoins amount=<hastings>&destination=<addr>
  std::ostringstream form;
  form << "amount=" << amountHastings
       << "&destination=" << destAddress;
  if (!memo.empty()) {
    // siad may ignore; embed in fee-included path via /wallet/tpool/raw if needed
    form << "&feeIncluded=true";
  }
  std::string body = http("POST", "/wallet/siacoins", form.str());
  if (body.empty()) return false;
  try {
    auto root = Common::JsonValue::fromString(body);
    // returns transactionids array
    if (root.isObject() && root.contains("transactionids") &&
        root("transactionids").isArray() && root("transactionids").size() > 0) {
      txid = root("transactionids")[0].getString();
      return !txid.empty();
    }
  } catch (...) {}
  // Some versions return empty 204-ish — treat non-empty body without error as ok
  if (body.find("transaction") != std::string::npos || body == "{}" ) {
    txid = memo.empty() ? "sia-send-ok" : memo; // fallback id
    return true;
  }
  return false;
}

bool SiaRpcClient::getTransactions(std::string& jsonOut) {
  jsonOut = http("GET", "/wallet/transactions?startheight=0&endheight=-1");
  return !jsonOut.empty();
}

bool SiaRpcClient::getTransaction(const std::string& txid, SiaTxInfo& info) {
  info.txid = txid;
  info.confirmed = false;
  info.confirmations = 0;
  std::string all;
  if (!getTransactions(all)) return false;
  if (all.find(txid) != std::string::npos) {
    info.confirmed = true;
    info.confirmations = 1;
    return true;
  }
  return false;
}

bool SiaRpcClient::findClaimPreimage(const std::string& /*lockTxidHint*/,
                                     std::string& preimageHex) {
  std::string all;
  if (!getTransactions(all)) return false;
  // Look for "preimage:" + 64 hex in wallet tx JSON/arbitrary data
  const std::string tag = "preimage:";
  auto pos = all.find(tag);
  while (pos != std::string::npos) {
    size_t start = pos + tag.size();
    if (start + 64 <= all.size()) {
      std::string cand = all.substr(start, 64);
      bool ok = true;
      for (char c : cand) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) { ok = false; break; }
      }
      if (ok) {
        preimageHex = cand;
        for (char& c : preimageHex)
          if (c >= 'A' && c <= 'F') c = static_cast<char>(c - 'A' + 'a');
        return true;
      }
    }
    pos = all.find(tag, pos + 1);
  }
  return false;
}

} // namespace XfgSwap
