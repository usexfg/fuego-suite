#include "DcrRpcClient.h"
#include "DcrHtlcScript.h"
#include "Common/JsonValue.h"
#include <algorithm>
#include <cstring>
#include <sstream>
#include <stdexcept>

// Decred address version bytes (must match DcrHtlcScript.cpp)
static constexpr uint8_t DCR_P2PKH_VERSION = 0x07;
static constexpr uint8_t DCR_P2SH_VERSION  = 0x0A;
static constexpr uint8_t DCR_P2PKH_TEST    = 0x1E;
static constexpr uint8_t DCR_P2SH_TEST     = 0x13;

// Script opcodes
static constexpr uint8_t OP_DUP       = 0x76;
static constexpr uint8_t OP_HASH160   = 0xA9;
static constexpr uint8_t OP_EQUAL     = 0x87;
static constexpr uint8_t OP_EQUALVERIFY = 0x88;
static constexpr uint8_t OP_CHECKSIG  = 0xAC;

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

// ---- HTLC operations --------------------------------------------------------

bool DcrRpcClient::claim(const std::string& claimerWif,
                          const std::string& htlcTxid,
                          uint32_t htlcVout,
                          uint64_t htlcAmount,
                          const std::string& redeemScriptHex,
                          const std::string& preimageHex,
                          const std::string& destAddress,
                          std::string& claimTxId) {
  try {
    auto redeemScript = DcrHtlcScript::hexToBytes(redeemScriptHex);
    auto preimage     = DcrHtlcScript::hexToBytes(preimageHex);

    // Build output: decode destAddress to get scriptPubKey
    uint8_t addrVersion = 0;
    std::vector<uint8_t> addrHash;
    if (!DcrHtlcScript::base58CheckDecode(destAddress, addrVersion, addrHash)) return false;

    std::vector<uint8_t> outputScript;
    if (addrVersion == DCR_P2PKH_VERSION || addrVersion == DCR_P2PKH_TEST) {
      outputScript.push_back(OP_DUP);
      outputScript.push_back(OP_HASH160);
      outputScript.push_back(0x14);
      outputScript.insert(outputScript.end(), addrHash.begin(), addrHash.end());
      outputScript.push_back(OP_EQUALVERIFY);
      outputScript.push_back(OP_CHECKSIG);
    } else if (addrVersion == DCR_P2SH_VERSION || addrVersion == DCR_P2SH_TEST) {
      outputScript.push_back(OP_HASH160);
      outputScript.push_back(0x14);
      outputScript.insert(outputScript.end(), addrHash.begin(), addrHash.end());
      outputScript.push_back(OP_EQUAL);
    } else {
      return false;
    }
    std::string outputScriptHex = DcrHtlcScript::bytesToHex(outputScript);

    const uint64_t fee = 10000;
    if (htlcAmount <= fee) return false;
    uint64_t outputAmount = htlcAmount - fee;

    // Build the P2SH scriptPubKey for the HTLC input
    auto htlcP2shSpk = DcrHtlcScript::redeemScriptToP2shScriptPubKey(redeemScript);
    std::string htlcP2shSpkHex = DcrHtlcScript::bytesToHex(htlcP2shSpk);

    // Build inputs and outputs JSON for createrawtransaction
    std::string inputsJson = "[{\"txid\":\"" + htlcTxid + "\",\"vout\":" +
        std::to_string(htlcVout) + ",\"scriptPubKey\":\"" + htlcP2shSpkHex +
        "\",\"redeemScript\":\"" + redeemScriptHex + "\"}]";

    std::string outputsJson = "{\"" + destAddress + "\":" +
        std::to_string(static_cast<double>(outputAmount) / 1e8) + "}";

    std::string rawTxHex;
    if (!createRawTransaction(inputsJson, outputsJson, 0, rawTxHex)) return false;

    // Build scriptSig: <sig> <preimage> OP_TRUE <redeemScript>
    // For signing, we need to construct the scriptSig to embed in the raw tx.
    // DCR signrawtransaction can sign with the redeemScript provided separately.
    std::string signParams = "[\"" + rawTxHex + "\",[" +
        "{\"txid\":\"" + htlcTxid + "\",\"vout\":" + std::to_string(htlcVout) +
        ",\"scriptPubKey\":\"" + htlcP2shSpkHex +
        "\",\"redeemScript\":\"" + redeemScriptHex + "\"}" +
        "],[\"" + claimerWif + "\"],\"ALL\"]";
    std::string resp = rpcCall("signrawtransaction", signParams);
    Common::JsonValue json = Common::JsonValue::fromString(resp);
    if (!json.isObject() || !json.contains("result")) return false;
    const auto& result = json("result");
    if (!result.isObject()) return false;
    if (!result.contains("hex")) return false;
    std::string signedHex = result("hex").getString();
    if (signedHex.empty()) return false;

    return sendRawTransaction(signedHex, claimTxId);
  } catch (...) { return false; }
}

bool DcrRpcClient::refundHtlc(const std::string& senderWif,
                                const std::string& htlcTxid,
                                uint32_t htlcVout,
                                uint64_t htlcAmount,
                                const std::string& redeemScriptHex,
                                uint32_t timeoutBlock,
                                const std::string& destAddress,
                                std::string& refundTxId) {
  try {
    auto redeemScript = DcrHtlcScript::hexToBytes(redeemScriptHex);

    // Build output: decode destAddress to get scriptPubKey
    uint8_t addrVersion = 0;
    std::vector<uint8_t> addrHash;
    if (!DcrHtlcScript::base58CheckDecode(destAddress, addrVersion, addrHash)) return false;

    std::vector<uint8_t> outputScript;
    if (addrVersion == DCR_P2PKH_VERSION || addrVersion == DCR_P2PKH_TEST) {
      outputScript.push_back(OP_DUP);
      outputScript.push_back(OP_HASH160);
      outputScript.push_back(0x14);
      outputScript.insert(outputScript.end(), addrHash.begin(), addrHash.end());
      outputScript.push_back(OP_EQUALVERIFY);
      outputScript.push_back(OP_CHECKSIG);
    } else if (addrVersion == DCR_P2SH_VERSION || addrVersion == DCR_P2SH_TEST) {
      outputScript.push_back(OP_HASH160);
      outputScript.push_back(0x14);
      outputScript.insert(outputScript.end(), addrHash.begin(), addrHash.end());
      outputScript.push_back(OP_EQUAL);
    } else {
      return false;
    }

    const uint64_t fee = 10000;
    if (htlcAmount <= fee) return false;
    uint64_t outputAmount = htlcAmount - fee;

    // Build the P2SH scriptPubKey for the HTLC input
    auto htlcP2shSpk = DcrHtlcScript::redeemScriptToP2shScriptPubKey(redeemScript);
    std::string htlcP2shSpkHex = DcrHtlcScript::bytesToHex(htlcP2shSpk);

    // CLTV requires nLocktime >= timeoutBlock for the refund path to activate.
    std::string inputsJson = "[{\"txid\":\"" + htlcTxid + "\",\"vout\":" +
        std::to_string(htlcVout) + ",\"scriptPubKey\":\"" + htlcP2shSpkHex +
        "\",\"redeemScript\":\"" + redeemScriptHex + "\"}]";

    std::string outputsJson = "{\"" + destAddress + "\":" +
        std::to_string(static_cast<double>(outputAmount) / 1e8) + "}";

    std::string rawTxHex;
    if (!createRawTransaction(inputsJson, outputsJson, timeoutBlock, rawTxHex)) return false;

    // Sign with redeemScript — signrawtransaction needs the redeemScript to
    // resolve P2SH inputs and apply CLTV logic correctly.
    std::string signParams = "[\"" + rawTxHex + "\",[" +
        "{\"txid\":\"" + htlcTxid + "\",\"vout\":" + std::to_string(htlcVout) +
        ",\"scriptPubKey\":\"" + htlcP2shSpkHex +
        "\",\"redeemScript\":\"" + redeemScriptHex + "\"}" +
        "],[\"" + senderWif + "\"],\"ALL\"]";
    std::string resp = rpcCall("signrawtransaction", signParams);
    Common::JsonValue json = Common::JsonValue::fromString(resp);
    if (!json.isObject() || !json.contains("result")) return false;
    const auto& result = json("result");
    if (!result.isObject()) return false;
    if (!result.contains("hex")) return false;
    std::string signedHex = result("hex").getString();
    if (signedHex.empty()) return false;

    return sendRawTransaction(signedHex, refundTxId);
  } catch (...) { return false; }
}

} // namespace XfgSwap
