#include "DcrRpcClient.h"
#include "Common/JsonValue.h"
#include <algorithm>
#include <cstring>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#define close closesocket
#else
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#endif

namespace XfgSwap {

// ---- Base64 -----------------------------------------------------------------

static const char kBase64Table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string DcrRpcClient::base64Encode(const std::string& input) {
  std::string out;
  out.reserve(((input.size() + 2) / 3) * 4);
  const uint8_t* data = reinterpret_cast<const uint8_t*>(input.data());
  size_t len = input.size();
  for (size_t i = 0; i < len; i += 3) {
    uint32_t n = static_cast<uint32_t>(data[i]) << 16;
    if (i + 1 < len) n |= static_cast<uint32_t>(data[i + 1]) << 8;
    if (i + 2 < len) n |= static_cast<uint32_t>(data[i + 2]);
    out.push_back(kBase64Table[(n >> 18) & 0x3F]);
    out.push_back(kBase64Table[(n >> 12) & 0x3F]);
    out.push_back((i + 1 < len) ? kBase64Table[(n >> 6) & 0x3F] : '=');
    out.push_back((i + 2 < len) ? kBase64Table[n & 0x3F] : '=');
  }
  return out;
}

// ---- Constructor ------------------------------------------------------------

DcrRpcClient::DcrRpcClient(const std::string& host, uint16_t port,
                             const std::string& rpcUser, const std::string& rpcPass)
  : m_host(host)
  , m_port(port)
  , m_rpcUser(rpcUser)
  , m_rpcPass(rpcPass) {
  m_authHeader = "Basic " + base64Encode(rpcUser + ":" + rpcPass);
}

// ---- HTTP POST --------------------------------------------------------------

std::string DcrRpcClient::httpPost(const std::string& body) {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) throw std::runtime_error("DcrRpcClient: socket failed");

#ifdef _WIN32
  DWORD tvMs = 10000;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tvMs), sizeof(tvMs));
  setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tvMs), sizeof(tvMs));
#else
  struct timeval tv;
  tv.tv_sec = 10;
  tv.tv_usec = 0;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif

  struct addrinfo hints, *result;
  std::memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  std::string portStr = std::to_string(m_port);
  int gai = getaddrinfo(m_host.c_str(), portStr.c_str(), &hints, &result);
  if (gai != 0) {
    close(sock);
    throw std::runtime_error("DcrRpcClient: getaddrinfo failed for " + m_host);
  }

  int ret = connect(sock, result->ai_addr, result->ai_addrlen);
  freeaddrinfo(result);
  if (ret < 0) {
    close(sock);
    throw std::runtime_error("DcrRpcClient: connect failed");
  }

  std::ostringstream req;
  req << "POST / HTTP/1.1\r\n";
  req << "Host: " << m_host << ":" << m_port << "\r\n";
  req << "Authorization: " << m_authHeader << "\r\n";
  req << "Content-Type: application/json\r\n";
  req << "Content-Length: " << body.size() << "\r\n";
  req << "Connection: close\r\n";
  req << "\r\n";
  req << body;

  std::string request = req.str();
  ssize_t sent = send(sock, request.c_str(), request.size(), 0);
  if (sent < 0 || static_cast<size_t>(sent) != request.size()) {
    close(sock);
    throw std::runtime_error("DcrRpcClient: send failed");
  }

  std::string response;
  char buf[4096];
  while (true) {
    ssize_t n = recv(sock, buf, sizeof(buf), 0);
    if (n <= 0) break;
    response.append(buf, static_cast<size_t>(n));
  }
  close(sock);

  size_t headerEnd = response.find("\r\n\r\n");
  if (headerEnd == std::string::npos) {
    throw std::runtime_error("DcrRpcClient: malformed HTTP response");
  }
  return response.substr(headerEnd + 4);
}

// ---- JSON-RPC ---------------------------------------------------------------

std::string DcrRpcClient::rpcCall(const std::string& method, const std::string& params) {
  std::string body = "{\"jsonrpc\":\"1.0\",\"id\":\"xfg-swapd\",\"method\":\"" +
                     method + "\",\"params\":" + params + "}";
  return httpPost(body);
}

// ---- Public API -------------------------------------------------------------

bool DcrRpcClient::getBlockCount(uint64_t& height) {
  try {
    std::string resp = rpcCall("getblockcount", "[]");
    Common::JsonValue json = Common::JsonValue::fromString(resp);
    if (!json.isObject() || !json.contains("result")) return false;
    const auto& result = json("result");
    if (!result.isInteger()) return false;
    height = static_cast<uint64_t>(result.getInteger());
    return true;
  } catch (...) { return false; }
}

bool DcrRpcClient::getRawTransaction(const std::string& txid, std::string& rawTxHex) {
  try {
    std::string params = "[\"" + txid + "\", true]";
    std::string resp = rpcCall("getrawtransaction", params);
    Common::JsonValue json = Common::JsonValue::fromString(resp);
    if (!json.isObject() || !json.contains("result")) return false;
    const auto& result = json("result");
    if (!result.isString()) return false;
    rawTxHex = result.getString();
    return !rawTxHex.empty();
  } catch (...) { return false; }
}

bool DcrRpcClient::getRawTransactionBytes(const std::string& txid, std::vector<uint8_t>& rawTx) {
  std::string hex;
  if (!getRawTransaction(txid, hex)) return false;

  rawTx.clear();
  rawTx.reserve(hex.size() / 2);
  for (size_t i = 0; i + 1 < hex.size(); i += 2) {
    uint8_t hi = 0, lo = 0;
    auto hd = [](char c, uint8_t& o) -> bool {
      if (c >= '0' && c <= '9') { o = c - '0'; return true; }
      if (c >= 'a' && c <= 'f') { o = c - 'a' + 10; return true; }
      if (c >= 'A' && c <= 'F') { o = c - 'A' + 10; return true; }
      return false;
    };
    if (!hd(hex[i], hi) || !hd(hex[i + 1], lo)) return false;
    rawTx.push_back(static_cast<uint8_t>((hi << 4) | lo));
  }
  return !rawTx.empty();
}

bool DcrRpcClient::getTxOut(const std::string& txid, uint32_t vout, uint64_t& amount) {
  try {
    std::string params = "[\"" + txid + "\", " + std::to_string(vout) + "]";
    std::string resp = rpcCall("gettxout", params);
    Common::JsonValue json = Common::JsonValue::fromString(resp);
    if (!json.isObject() || !json.contains("result")) return false;
    const auto& result = json("result");
    if (!result.isObject()) return false;
    if (result.contains("value")) {
      // dcrd returns value in DCR (float), convert to atoms
      double dcr = 0;
      if (result("value").isReal()) {
        dcr = result("value").getReal();
      } else if (result("value").isInteger()) {
        dcr = static_cast<double>(result("value").getInteger());
      }
      amount = static_cast<uint64_t>(dcr * 1e8);
      return true;
    }
    return false;
  } catch (...) { return false; }
}

bool DcrRpcClient::sendRawTransaction(const std::string& rawTxHex, std::string& txid) {
  try {
    std::string params = "[\"" + rawTxHex + "\"]";
    std::string resp = rpcCall("sendrawtransaction", params);
    Common::JsonValue json = Common::JsonValue::fromString(resp);
    if (!json.isObject() || !json.contains("result")) return false;
    const auto& result = json("result");
    if (!result.isString()) return false;
    txid = result.getString();
    return !txid.empty();
  } catch (...) { return false; }
}

bool DcrRpcClient::createRawTransaction(const std::string& inputsJson,
                                         const std::string& outputsJson,
                                         uint32_t locktime,
                                         std::string& rawTxHex) {
  try {
    std::string params = "[" + inputsJson + "," + outputsJson + "," +
                         std::to_string(locktime) + "]";
    std::string resp = rpcCall("createrawtransaction", params);
    Common::JsonValue json = Common::JsonValue::fromString(resp);
    if (!json.isObject() || !json.contains("result")) return false;
    const auto& result = json("result");
    if (!result.isString()) return false;
    rawTxHex = result.getString();
    return !rawTxHex.empty();
  } catch (...) { return false; }
}

bool DcrRpcClient::signRawTransaction(const std::string& rawTxHex, std::string& signedTxHex) {
  try {
    std::string params = "[\"" + rawTxHex + "\", null, null, \"ALL\"]";
    std::string resp = rpcCall("signrawtransaction", params);
    Common::JsonValue json = Common::JsonValue::fromString(resp);
    if (!json.isObject() || !json.contains("result")) return false;
    const auto& result = json("result");
    if (!result.isObject()) return false;
    if (result.contains("hex")) {
      signedTxHex = result("hex").getString();
      return !signedTxHex.empty();
    }
    return false;
  } catch (...) { return false; }
}

bool DcrRpcClient::importAddress(const std::string& address,
                                  const std::string& label, bool rescan) {
  try {
    std::string params = "[\"" + address + "\", \"" + label + "\", " +
                         (rescan ? "true" : "false") + "]";
    std::string resp = rpcCall("importaddress", params);
    Common::JsonValue json = Common::JsonValue::fromString(resp);
    return json.isObject() && json.contains("result");
  } catch (...) { return false; }
}

bool DcrRpcClient::listUnspent(const std::string& address,
                                std::vector<std::pair<std::string, uint64_t>>& utxos) {
  try {
    std::string params = "[1, 9999999, [\"" + address + "\"]]";
    std::string resp = rpcCall("listunspent", params);
    Common::JsonValue json = Common::JsonValue::fromString(resp);
    if (!json.isObject() || !json.contains("result")) return false;
    const auto& result = json("result");
    if (!result.isArray()) return false;

    size_t count = result.size();
    for (size_t i = 0; i < count; ++i) {
      const auto& utxo = result[i];
      if (!utxo.isObject()) continue;
      std::string txid = utxo.contains("txid") ? utxo("txid").getString() : "";
      uint64_t amount = 0;
      if (utxo.contains("amount")) {
        double dcr = utxo("amount").isReal() ? utxo("amount").getReal() : 0;
        amount = static_cast<uint64_t>(dcr * 1e8);
      }
      if (!txid.empty() && amount > 0) {
        utxos.push_back({txid, amount});
      }
    }
    return true;
  } catch (...) { return false; }
}

} // namespace XfgSwap
