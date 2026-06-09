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

#include "FuegoRpcClient.h"
#include "Common/JsonValue.h"
#include "Common/StringTools.h"
#include "CryptoNoteCore/CryptoNoteTools.h"
#include "CryptoNoteCore/CryptoNoteBasic.h"
#include "Common/WinCompat.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace XfgSwap {

FuegoRpcClient::FuegoRpcClient(const std::string& host, uint16_t port)
  : m_host(host)
  , m_port(port) {
}

void FuegoRpcClient::setWalletRpc(const std::string& host, uint16_t port) {
  m_walletHost = host;
  m_walletPort = port;
}

// ── Low-level HTTP ───────────────────────────────────────────────────

std::string FuegoRpcClient::httpPost(const std::string& host, uint16_t port,
                                     const std::string& path, const std::string& body) {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    throw std::runtime_error("Failed to create socket");
  }

  // Set socket timeout (10 seconds)
  struct timeval tv;
  tv.tv_sec = 10;
  tv.tv_usec = 0;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  // Resolve host
  struct addrinfo hints, *result;
  std::memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  std::string portStr = std::to_string(port);
  int gai = getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result);
  if (gai != 0) {
    close(sock);
    throw std::runtime_error("Failed to resolve host: " + host);
  }

  // Connect
  int ret = connect(sock, result->ai_addr, result->ai_addrlen);
  freeaddrinfo(result);
  if (ret < 0) {
    close(sock);
    throw std::runtime_error("Failed to connect to " + host + ":" + portStr);
  }

  // Build HTTP request
  std::ostringstream req;
  req << "POST " << path << " HTTP/1.1\r\n";
  req << "Host: " << host << ":" << port << "\r\n";
  req << "Content-Type: application/json\r\n";
  req << "Content-Length: " << body.size() << "\r\n";
  req << "Connection: close\r\n";
  req << "\r\n";
  req << body;

  std::string request = req.str();
  ssize_t sent = send(sock, request.c_str(), request.size(), 0);
  if (sent < 0 || static_cast<size_t>(sent) != request.size()) {
    close(sock);
    throw std::runtime_error("Failed to send HTTP request");
  }

  // Read response.
  //
  // The peer (fuegod, fuegod-testnet) does NOT honor `Connection: close` and
  // keeps the socket open after the response body. Without Content-Length
  // termination, recv() would block for the full SO_RCVTIMEO (10s) on every
  // RPC — a 20s+ wedge on daemon startup (start() + checkTimeouts()).
  //
  // Read strategy:
  //   1. Read until we see end-of-headers (`\r\n\r\n`).
  //   2. Parse `Content-Length` (case-insensitive).
  //   3. Read exactly Content-Length more bytes of body, then stop.
  //   4. If no Content-Length is present (legacy / chunked), fall back to
  //      read-until-close — still bounded by SO_RCVTIMEO.
  std::string response;
  char buf[4096];

  // Phase 1: read until end of headers
  size_t headerEnd = std::string::npos;
  while (headerEnd == std::string::npos) {
    ssize_t n = recv(sock, buf, sizeof(buf), 0);
    if (n <= 0) break;  // closed or timeout before headers complete
    response.append(buf, static_cast<size_t>(n));
    headerEnd = response.find("\r\n\r\n");
  }

  if (headerEnd == std::string::npos) {
    close(sock);
    throw std::runtime_error("Malformed HTTP response (no header terminator)");
  }

  // Phase 2: parse Content-Length from headers (case-insensitive)
  auto parseContentLength = [](const std::string& hdrs) -> long long {
    static const char* needles[] = {
      "\r\nContent-Length:", "\r\ncontent-length:", "\r\nContent-length:"
    };
    size_t pos = std::string::npos;
    for (auto* n : needles) {
      pos = hdrs.find(n);
      if (pos != std::string::npos) { pos += std::strlen(n); break; }
    }
    if (pos == std::string::npos) return -1;
    while (pos < hdrs.size() && (hdrs[pos] == ' ' || hdrs[pos] == '\t')) ++pos;
    long long val = 0; bool any = false;
    while (pos < hdrs.size() && std::isdigit(static_cast<unsigned char>(hdrs[pos]))) {
      val = val * 10 + (hdrs[pos] - '0'); ++pos; any = true;
    }
    return any ? val : -1;
  };

  long long contentLength = parseContentLength(response.substr(0, headerEnd + 2));
  size_t bodyStart = headerEnd + 4;

  if (contentLength >= 0) {
    // Phase 3: read exactly Content-Length more bytes of body
    while (response.size() - bodyStart < static_cast<size_t>(contentLength)) {
      size_t want = static_cast<size_t>(contentLength) - (response.size() - bodyStart);
      ssize_t n = recv(sock, buf, std::min(sizeof(buf), want), 0);
      if (n <= 0) break;
      response.append(buf, static_cast<size_t>(n));
    }
  } else {
    // Phase 4: no Content-Length — fall back to read-until-close/timeout
    while (true) {
      ssize_t n = recv(sock, buf, sizeof(buf), 0);
      if (n <= 0) break;
      response.append(buf, static_cast<size_t>(n));
    }
  }
  close(sock);

  return response.substr(bodyStart);
}

std::string FuegoRpcClient::daemonPost(const std::string& path, const std::string& body) {
  return httpPost(m_host, m_port, path, body);
}

std::string FuegoRpcClient::walletJsonRpc(const std::string& method, const std::string& params) {
  if (m_walletPort == 0) {
    throw std::runtime_error("Wallet RPC not configured (call setWalletRpc first)");
  }

  std::ostringstream body;
  body << "{\"jsonrpc\":\"2.0\",\"id\":0,\"method\":\"" << method << "\"";
  if (!params.empty()) {
    body << ",\"params\":" << params;
  }
  body << "}";

  return httpPost(m_walletHost, m_walletPort, "/json_rpc", body.str());
}

// ── Daemon RPC methods ───────────────────────────────────────────────

bool FuegoRpcClient::getHeight(uint32_t& height) {
  try {
    std::string responseBody = daemonPost("/getheight", "{}");
    Common::JsonValue json = Common::JsonValue::fromString(responseBody);

    if (!json.isObject() || !json.contains("height")) {
      return false;
    }

    height = static_cast<uint32_t>(json("height").getInteger());
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

bool FuegoRpcClient::sendRawTransaction(const std::string& txHex) {
  try {
    Common::JsonValue reqJson(Common::JsonValue::OBJECT);
    reqJson.insert("tx_as_hex", txHex);

    std::string responseBody = daemonPost("/sendrawtransaction", reqJson.toString());
    Common::JsonValue json = Common::JsonValue::fromString(responseBody);

    if (!json.isObject() || !json.contains("status")) {
      return false;
    }

    return json("status").getString() == "OK";
  } catch (const std::exception&) {
    return false;
  }
}

bool FuegoRpcClient::addSwapFee(uint64_t amount) {
  try {
    Common::JsonValue reqJson(Common::JsonValue::OBJECT);
    reqJson.insert("amount", static_cast<int64_t>(amount));

    std::string responseBody = daemonPost("/addswapfee", reqJson.toString());
    Common::JsonValue json = Common::JsonValue::fromString(responseBody);

    if (!json.isObject() || !json.contains("status")) {
      return false;
    }

    return json("status").getString() == "OK";
  } catch (const std::exception&) {
    return false;
  }
}

bool FuegoRpcClient::getInfo(NodeInfo& info) {
  try {
    std::string responseBody = daemonPost("/getinfo", "{}");
    Common::JsonValue json = Common::JsonValue::fromString(responseBody);

    if (!json.isObject() || !json.contains("height")) {
      return false;
    }

    info.height = static_cast<uint32_t>(json("height").getInteger());
    info.difficulty = json.contains("difficulty")
      ? static_cast<uint64_t>(json("difficulty").getInteger()) : 0;
    info.txCount = json.contains("tx_count")
      ? static_cast<uint64_t>(json("tx_count").getInteger()) : 0;
    info.status = json.contains("status")
      ? json("status").getString() : "UNKNOWN";

    return true;
  } catch (const std::exception&) {
    return false;
  }
}

// ── Wallet RPC methods ───────────────────────────────────────────────

bool FuegoRpcClient::sendTransfer(const std::string& address, uint64_t amount,
                                  uint64_t mixin, TransferResult& result) {
  try {
    // Build JSON-RPC params for the "transfer" method:
    //   { "destinations": [{"amount": N, "address": "fire..."}],
    //     "fee": 10000, "mixin": M, "unlock_time": 0 }
    //
    // fee: 10000 atomic = 0.001 XFG (minimum fee)
    std::ostringstream params;
    params << "{\"destinations\":[{\"amount\":" << amount
           << ",\"address\":\"" << address << "\"}]"
           << ",\"fee\":10000"
           << ",\"mixin\":" << mixin
           << ",\"unlock_time\":0}";

    std::string responseBody = walletJsonRpc("transfer", params.str());
    Common::JsonValue json = Common::JsonValue::fromString(responseBody);

    if (!json.isObject()) {
      return false;
    }

    // Check for JSON-RPC error
    if (json.contains("error")) {
      return false;
    }

    if (!json.contains("result")) {
      return false;
    }

    const auto& res = json("result");
    if (!res.isObject() || !res.contains("tx_hash")) {
      return false;
    }

    result.txHash = res("tx_hash").getString();
    result.txSecretKey = res.contains("tx_secret_key")
      ? res("tx_secret_key").getString() : "";

    return true;
  } catch (const std::exception&) {
    return false;
  }
}

// ── Daemon RPC: transaction inspection ───────────────────────────────

bool FuegoRpcClient::getTransactionOutputs(const std::string& txHashHex,
                                           std::vector<TxOutputInfo>& outputs) {
  try {
    // POST /gettransactions with {"txs_hashes": ["<hash>"]}
    std::ostringstream body;
    body << "{\"txs_hashes\":[\"" << txHashHex << "\"]}";

    std::string responseBody = daemonPost("/gettransactions", body.str());
    Common::JsonValue json = Common::JsonValue::fromString(responseBody);

    if (!json.isObject()) {
      return false;
    }

    // Check that the tx was found (not in missed_tx)
    if (json.contains("missed_tx")) {
      const auto& missed = json("missed_tx");
      if (missed.isArray() && missed.size() > 0) {
        return false;  // tx not found on chain or in pool
      }
    }

    if (!json.contains("txs_as_hex")) {
      return false;
    }

    const auto& txsHex = json("txs_as_hex");
    if (!txsHex.isArray() || txsHex.size() == 0) {
      return false;
    }

    // Parse the raw transaction binary to extract outputs.
    // The tx blob is hex-encoded CryptoNote::Transaction.
    std::string txHex = txsHex[0].getString();
    std::vector<uint8_t> txBlob;
    if (!Common::fromHex(txHex, txBlob) || txBlob.empty()) {
      return false;
    }

    CryptoNote::Transaction tx;
    if (!CryptoNote::fromBinaryArray(tx, txBlob)) {
      return false;
    }

    outputs.clear();
    outputs.reserve(tx.outputs.size());
    for (const auto& out : tx.outputs) {
      if (out.target.type() == typeid(CryptoNote::KeyOutput)) {
        TxOutputInfo info;
        info.amount = out.amount;
        info.targetKey = boost::get<CryptoNote::KeyOutput>(out.target).key;
        outputs.push_back(info);
      }
    }
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

bool FuegoRpcClient::getRandomOutputs(uint64_t amount, uint64_t count,
                                      std::vector<RandomOutputEntry>& entries) {
  try {
    std::ostringstream body;
    body << "{\"amount\":" << amount << ",\"count\":" << count << "}";

    std::string responseBody = daemonPost("/getrandom_outs_json", body.str());
    Common::JsonValue json = Common::JsonValue::fromString(responseBody);

    if (!json.isObject() || !json.contains("status")) return false;
    if (json("status").getString() != "OK") return false;
    if (!json.contains("outs")) return false;

    const auto& outs = json("outs");
    if (!outs.isArray()) return false;

    entries.clear();
    entries.reserve(outs.size());
    for (size_t i = 0; i < outs.size(); ++i) {
      const auto& e = outs[i];
      RandomOutputEntry re;
      re.globalIndex = static_cast<uint64_t>(e("global_index").getInteger());
      Common::podFromHex(e("out_key").getString(), re.outKey);
      entries.push_back(re);
    }
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

bool FuegoRpcClient::resolveAlias(const std::string& alias, std::string& addressOut) {
  try {
    std::string body = "{\"alias\":\"" + alias + "\"}";
    std::string responseBody = daemonPost("/get_alias", body);
    Common::JsonValue json = Common::JsonValue::fromString(responseBody);
    if (!json.isObject() || !json.contains("found")) return false;
    if (!json("found").getBool()) return false;
    addressOut = json("address").getString();
    return true;
  } catch (const std::exception&) { return false; }
}


bool FuegoRpcClient::createAfkLock(uint64_t amount, uint32_t timeout_hours, uint8_t pair, std::string& lockId, std::string& adaptorPoint, std::string& preSig) {
  if (m_walletHost.empty() || m_walletPort == 0) {
    throw std::runtime_error("Wallet RPC not configured (call setWalletRpc first)");
  }

  std::stringstream params;
  params << "{"
         << "\"amount\": " << amount << ","
         << "\"timeout_hours\": " << timeout_hours << ","
         << "\"pair\": " << static_cast<int>(pair)
         << "}";

  std::string respBody = walletJsonRpc("create_afk_lock", params.str());
  if (respBody.empty()) {
    return false;
  }

  size_t lockIdPos = respBody.find("\"lockId\":\"");
  if (lockIdPos != std::string::npos) {
    lockIdPos += 11;
    size_t lockIdEnd = respBody.find("\"", lockIdPos);
    lockId = respBody.substr(lockIdPos, lockIdEnd - lockIdPos);
  } else {
      return false;
  }

  size_t apPos = respBody.find("\"adaptorPoint\":\"");
  if (apPos != std::string::npos) {
    apPos += 17;
    size_t apEnd = respBody.find("\"", apPos);
    adaptorPoint = respBody.substr(apPos, apEnd - apPos);
  } else {
      return false;
  }

  size_t psPos = respBody.find("\"preSig\":\"");
  if (psPos != std::string::npos) {
    psPos += 11;
    size_t psEnd = respBody.find("\"", psPos);
    preSig = respBody.substr(psPos, psEnd - psPos);
  } else {
      return false;
  }

  return true;
}

bool FuegoRpcClient::checkReserveProof(const std::string& address, const std::string& message,
                                        const std::string& signature, bool& good, uint64_t& total) {
  try {
    std::string body = "{\"address\":\"" + address + "\","
                       "\"message\":\"" + message + "\","
                       "\"signature\":\"" + signature + "\"}";
    std::string respBody = daemonPost("/check_reserve_proof", body);
    if (respBody.empty()) return false;

    Common::JsonValue json = Common::JsonValue::fromString(respBody);
    if (!json.isObject()) return false;
    if (json.contains("status") && json("status").getString() != "OK") return false;

    good = json.contains("good") && json("good").getBool();
    total = json.contains("total") ? static_cast<uint64_t>(json("total").getInteger()) : 0;
    return true;
  } catch (const std::exception&) { return false; }
}

} // namespace XfgSwap
