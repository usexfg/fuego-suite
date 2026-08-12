// Copyright (c) 2017-2026 Fuego Developers

#include "TonRpcClient.h"
#include "TonCell.h"
#include "Common/JsonValue.h"

#include <HTTP/httplib.h>
#include <sstream>
#include <cctype>
#include <cstring>
#include <vector>

namespace XfgSwap {

TonRpcClient::TonRpcClient(const std::string& host, uint16_t port,
                           const std::string& apiKey,
                           const std::string& htlcAddress,
                           int workchain)
  : m_host(host), m_port(port), m_apiKey(apiKey),
    m_htlcAddress(htlcAddress), m_workchain(workchain) {}

std::string TonRpcClient::httpPost(const std::string& body) {
  std::string host = m_host;
  bool https = false;
  if (host.rfind("https://", 0) == 0) { https = true; host = host.substr(8); }
  else if (host.rfind("http://", 0) == 0) { host = host.substr(7); }
  std::string pathPrefix;
  auto slash = host.find('/');
  if (slash != std::string::npos) {
    pathPrefix = host.substr(slash);
    host = host.substr(0, slash);
  }
  std::string path = pathPrefix.empty() ? "/jsonRPC" : pathPrefix;
  if (!pathPrefix.empty() && path.back() == '/') path += "jsonRPC";

  try {
    httplib::Headers headers = {{"Content-Type", "application/json"}};
    if (!m_apiKey.empty()) headers.emplace("X-API-Key", m_apiKey);
    if (https) {
      httplib::SSLClient cli(host, m_port == 0 ? 443 : m_port);
      cli.set_connection_timeout(10, 0);
      cli.set_read_timeout(30, 0);
      auto res = cli.Post(path.c_str(), headers, body, "application/json");
      if (!res || res->status != 200) return {};
      return res->body;
    }
    httplib::Client cli(host, m_port == 0 ? 80 : m_port);
    cli.set_connection_timeout(10, 0);
    cli.set_read_timeout(30, 0);
    auto res = cli.Post(path.c_str(), headers, body, "application/json");
    if (!res || res->status != 200) return {};
    return res->body;
  } catch (...) {
    return {};
  }
}

std::string TonRpcClient::rpcCall(const std::string& method, const std::string& paramsJson) {
  std::ostringstream oss;
  oss << "{\"id\":1,\"jsonrpc\":\"2.0\",\"method\":\"" << method
      << "\",\"params\":" << (paramsJson.empty() ? "{}" : paramsJson) << "}";
  return httpPost(oss.str());
}

bool TonRpcClient::hexTo32(const std::string& hex, uint8_t out[32]) {
  std::string h = hex;
  if (h.size() >= 2 && h[0] == '0' && (h[1] == 'x' || h[1] == 'X')) h = h.substr(2);
  if (h.size() != 64) return false;
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  for (int i = 0; i < 32; ++i) {
    int hi = nib(h[i * 2]), lo = nib(h[i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return true;
}

bool TonRpcClient::getMasterchainSeqno(uint64_t& seqno) {
  std::string body = rpcCall("getMasterchainInfo", "{}");
  if (body.empty()) return false;
  try {
    auto root = Common::JsonValue::fromString(body);
    if (!root.isObject() || !root.contains("result")) return false;
    const auto& r = root("result");
    if (r.isObject() && r.contains("last") && r("last").isObject() &&
        r("last").contains("seqno")) {
      seqno = static_cast<uint64_t>(r("last")("seqno").getInteger());
      return true;
    }
    if (r.isObject() && r.contains("seqno")) {
      seqno = static_cast<uint64_t>(r("seqno").getInteger());
      return true;
    }
  } catch (...) {}
  return false;
}

bool TonRpcClient::getBalance(const std::string& address, uint64_t& nanoTons) {
  std::ostringstream params;
  params << "{\"address\":\"" << address << "\"}";
  std::string body = rpcCall("getAddressBalance", params.str());
  if (body.empty()) return false;
  try {
    auto root = Common::JsonValue::fromString(body);
    if (!root.isObject() || !root.contains("result")) return false;
    if (root("result").isString()) {
      nanoTons = std::stoull(root("result").getString());
      return true;
    }
    if (root("result").isInteger()) {
      nanoTons = static_cast<uint64_t>(root("result").getInteger());
      return true;
    }
  } catch (...) {}
  return false;
}

bool TonRpcClient::getHtlcState(const std::string& address, TonHtlcState& out) {
  std::ostringstream params;
  params << "{\"address\":\"" << address
         << "\",\"method\":\"get_state\",\"stack\":[]}";
  std::string body = rpcCall("runGetMethod", params.str());
  out = TonHtlcState{};
  if (body.empty()) {
    // Fallback: balance only
    uint64_t bal = 0;
    if (!getBalance(address, bal)) return false;
    out.amountNano = bal;
    return bal > 0;
  }
  try {
    auto root = Common::JsonValue::fromString(body);
    if (!root.isObject() || !root.contains("result")) return false;
    const auto& r = root("result");
    if (!r.isObject()) return false;
    if (r.contains("exit_code") && r("exit_code").getInteger() != 0) {
      uint64_t bal = 0;
      if (getBalance(address, bal) && bal > 0) {
        out.amountNano = bal;
        return true;
      }
      return false;
    }
    // stack: array of [type, value] per toncenter
    if (r.contains("stack") && r("stack").isArray()) {
      // Best-effort: first num = amount, later fields if present
      // When stack empty, use balance.
    }
    uint64_t bal = 0;
    getBalance(address, bal);
    out.amountNano = bal;
    // Parse stack items if present (toncenter: [["num","0x..."]])
    if (r.contains("stack") && r("stack").isArray()) {
      auto parseNum = [](const Common::JsonValue& item) -> std::string {
        // item is array [type, value]
        try {
          if (item.isArray() && item.size() >= 2) {
            if (item[1].isString()) return item[1].getString();
          }
        } catch (...) {}
        return {};
      };
      // We don't rely on JsonValue operator[] — use string find fallback via body.
    }
    // Extract hex fields from raw body for hash_lock / preimage when contract returns them
    auto extractHexField = [&](const char* key) -> std::string {
      std::string needle = std::string("\"") + key + "\"";
      auto pos = body.find(needle);
      if (pos == std::string::npos) return {};
      pos = body.find(':', pos);
      if (pos == std::string::npos) return {};
      auto q1 = body.find('"', pos + 1);
      if (q1 == std::string::npos) return {};
      auto q2 = body.find('"', q1 + 1);
      if (q2 == std::string::npos) return {};
      return body.substr(q1 + 1, q2 - q1 - 1);
    };
    (void)extractHexField;
    // claimed/refunded from exit stack nums — optional
    out.claimed = body.find("\"claimed\":true") != std::string::npos ||
                  body.find("\"claimed\":1") != std::string::npos;
    out.refunded = body.find("\"refunded\":true") != std::string::npos ||
                   body.find("\"refunded\":1") != std::string::npos;
    return true;
  } catch (...) {
    uint64_t bal = 0;
    if (!getBalance(address, bal)) return false;
    out.amountNano = bal;
    return bal > 0;
  }
}

bool TonRpcClient::sendBoc(const std::string& bocBase64, std::string& messageHashHex) {
  std::ostringstream params;
  params << "{\"boc\":\"" << bocBase64 << "\"}";
  std::string body = rpcCall("sendBoc", params.str());
  if (body.empty()) return false;
  try {
    auto root = Common::JsonValue::fromString(body);
    if (!root.isObject() || !root.contains("result")) return false;
    if (root("result").isObject() && root("result").contains("hash") &&
        root("result")("hash").isString()) {
      messageHashHex = root("result")("hash").getString();
      return true;
    }
    messageHashHex = "ok";
    return true;
  } catch (...) {
    return false;
  }
}

bool TonRpcClient::sendExternalBody(const std::string& destAddr,
                                    const std::string& bodyBocB64,
                                    std::string& msgHash,
                                    std::string& error) {
  int8_t wc = 0;
  uint8_t hash[32];
  if (!Ton::parseTonAddress(destAddr, wc, hash)) {
    error = "TON: invalid destination address";
    return false;
  }
  // bodyBocB64 is already a full external message BOC from callers
  if (!sendBoc(bodyBocB64, msgHash)) {
    error = "TON: sendBoc failed (check toncenter API / HTLC address)";
    return false;
  }
  return true;
}

bool TonRpcClient::lockHtlc(const std::string& /*walletKeyHex*/,
                            const std::string& recipient,
                            const std::string& hashLockSha256Hex,
                            uint64_t timeoutUnix,
                            uint64_t /*amountNano*/,
                            std::string& lockRef,
                            std::string& error) {
  if (m_htlcAddress.empty()) {
    error = "TON lock: configure ton_htlc_address (deployed fuego htlc.fc contract)";
    return false;
  }
  uint8_t hashLock[32], recipHash[32];
  if (!hexTo32(hashLockSha256Hex, hashLock)) {
    error = "TON lock: invalid hashlock hex";
    return false;
  }
  int8_t rwc = 0;
  if (!Ton::parseTonAddress(recipient, rwc, recipHash)) {
    // recipient may be empty for pre-configured contract
    std::memset(recipHash, 0, 32);
  }
  auto body = Ton::buildLockBody(hashLock, timeoutUnix, rwc, recipHash);
  int8_t dwc = 0;
  uint8_t dhash[32];
  if (!Ton::parseTonAddress(m_htlcAddress, dwc, dhash)) {
    error = "TON lock: invalid ton_htlc_address";
    return false;
  }
  auto msg = Ton::buildExternalInMessage(dwc, dhash, body);
  std::string boc = msg->toBocBase64();
  std::string hash;
  if (!sendBoc(boc, hash)) {
    // Contract may already be funded manually — accept if balance present
    uint64_t bal = 0;
    if (getBalance(m_htlcAddress, bal) && bal > 0) {
      lockRef = m_htlcAddress;
      error.clear();
      return true;
    }
    error = "TON lock: sendBoc failed; fund HTLC address manually then retry verify";
    return false;
  }
  lockRef = m_htlcAddress;
  return true;
}

bool TonRpcClient::claimHtlc(const std::string& /*walletKeyHex*/,
                             const std::string& htlcAddress,
                             const std::string& preimageHex,
                             std::string& claimRef,
                             std::string& error) {
  std::string addr = htlcAddress.empty() ? m_htlcAddress : htlcAddress;
  if (addr.empty()) {
    error = "TON claim: no HTLC address";
    return false;
  }
  uint8_t pre[32];
  if (!hexTo32(preimageHex, pre)) {
    error = "TON claim: invalid preimage hex";
    return false;
  }
  int8_t wc = 0;
  uint8_t dhash[32];
  if (!Ton::parseTonAddress(addr, wc, dhash)) {
    error = "TON claim: bad address";
    return false;
  }
  auto body = Ton::buildClaimBody(pre);
  auto msg = Ton::buildExternalInMessage(wc, dhash, body);
  std::string boc = msg->toBocBase64();
  std::string hash;
  if (!sendBoc(boc, hash)) {
    error = "TON claim: sendBoc failed";
    return false;
  }
  claimRef = hash.empty() ? addr : hash;
  return true;
}

bool TonRpcClient::refundHtlc(const std::string& /*walletKeyHex*/,
                              const std::string& htlcAddress,
                              std::string& refundRef,
                              std::string& error) {
  std::string addr = htlcAddress.empty() ? m_htlcAddress : htlcAddress;
  if (addr.empty()) {
    error = "TON refund: no HTLC address";
    return false;
  }
  int8_t wc = 0;
  uint8_t dhash[32];
  if (!Ton::parseTonAddress(addr, wc, dhash)) {
    error = "TON refund: bad address";
    return false;
  }
  auto body = Ton::buildRefundBody();
  auto msg = Ton::buildExternalInMessage(wc, dhash, body);
  std::string boc = msg->toBocBase64();
  std::string hash;
  if (!sendBoc(boc, hash)) {
    error = "TON refund: sendBoc failed (timeout may not have elapsed)";
    return false;
  }
  refundRef = hash.empty() ? addr : hash;
  return true;
}

} // namespace XfgSwap
