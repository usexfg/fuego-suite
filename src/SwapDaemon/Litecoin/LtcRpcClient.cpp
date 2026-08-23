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

#include "LtcRpcClient.h"
#include "LtcHtlcScript.h"
#include "Crypto/Secp256k1Signer.h"
#include "Crypto/Bip143Sighash.h"
#include "Common/JsonValue.h"
#include "Common/WinCompat.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace XfgSwap {

// ---- Base64 encoding (for HTTP Basic Auth) ----------------------------------

static const char kBase64Table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string LtcRpcClient::base64Encode(const std::string& input) {
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

LtcRpcClient::LtcRpcClient(const std::string& host, uint16_t port,
                           const std::string& rpcUser, const std::string& rpcPassword)
    : m_host(host)
    , m_port(port)
    , m_rpcUser(rpcUser)
    , m_rpcPassword(rpcPassword) {
  m_authHeader = "Basic " + base64Encode(rpcUser + ":" + rpcPassword);
}

// ---- Low-level HTTP POST (POSIX sockets) ------------------------------------

std::string LtcRpcClient::httpPost(const std::string& body) {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    throw std::runtime_error("LtcRpcClient: failed to create socket");
  }

  // 10-second timeout
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

  // Resolve host
  struct addrinfo hints, *result;
  std::memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  std::string portStr = std::to_string(m_port);
  int gai = getaddrinfo(m_host.c_str(), portStr.c_str(), &hints, &result);
  if (gai != 0) {
    close(sock);
    throw std::runtime_error("LtcRpcClient: failed to resolve host: " + m_host);
  }

  int ret = connect(sock, result->ai_addr, result->ai_addrlen);
  freeaddrinfo(result);
  if (ret < 0) {
    close(sock);
    throw std::runtime_error("LtcRpcClient: failed to connect to " + m_host + ":" + portStr);
  }

  // Build HTTP request with Basic Auth
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
  ssize_t sent = send(sock, request.c_str(), request.size(),
#ifndef _WIN32
                      MSG_NOSIGNAL
#else
                      0
#endif
                      );
  if (sent < 0 || static_cast<size_t>(sent) != request.size()) {
    close(sock);
    throw std::runtime_error("LtcRpcClient: failed to send HTTP request");
  }

  // Read entire response
  std::string response;
  char buf[4096];
  while (true) {
    ssize_t n = recv(sock, buf, sizeof(buf), 0);
    if (n <= 0) break;
    response.append(buf, static_cast<size_t>(n));
  }
  close(sock);

  // Extract body after \r\n\r\n
  size_t headerEnd = response.find("\r\n\r\n");
  if (headerEnd == std::string::npos) {
    throw std::runtime_error("LtcRpcClient: malformed HTTP response");
  }

  return response.substr(headerEnd + 4);
}

// ---- JSON-RPC call wrapper --------------------------------------------------

std::string LtcRpcClient::rpcCall(const std::string& method, const std::string& params) {
  std::string body = "{\"jsonrpc\":\"1.0\",\"id\":\"xfg-swapd\",\"method\":\"" +
                     method + "\",\"params\":" + params + "}";
  return httpPost(body);
}

// ---- Public API -------------------------------------------------------------

bool LtcRpcClient::getBlockCount(uint64_t& height) {
  try {
    std::string responseBody = rpcCall("getblockcount", "[]");
    Common::JsonValue json = Common::JsonValue::fromString(responseBody);

    if (!json.isObject() || !json.contains("result")) {
      return false;
    }

    const auto& result = json("result");
    if (!result.isInteger()) {
      return false;
    }

    height = static_cast<uint64_t>(result.getInteger());
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

bool LtcRpcClient::getTransaction(const std::string& txid, LtcTxInfo& info) {
  try {
    std::string params = "[\"" + txid + "\",true]";
    std::string responseBody = rpcCall("getrawtransaction", params);
    Common::JsonValue json = Common::JsonValue::fromString(responseBody);

    if (!json.isObject() || !json.contains("result")) {
      return false;
    }

    const auto& result = json("result");
    if (!result.isObject()) {
      return false;
    }

    info.txid = result.contains("txid") ? result("txid").getString() : txid;
    info.confirmations = result.contains("confirmations")
        ? static_cast<uint32_t>(result("confirmations").getInteger()) : 0;
    info.blockHeight = result.contains("blockheight")
        ? static_cast<uint64_t>(result("blockheight").getInteger()) : 0;
    info.inMempool = (info.confirmations == 0);

    return true;
  } catch (const std::exception&) {
    return false;
  }
}

bool LtcRpcClient::getBalance(const std::string& address, uint64_t& litoshis) {
  try {
    std::vector<LtcUtxo> utxos;
    if (!listUnspent(address, utxos)) {
      return false;
    }

    uint64_t total = 0;
    for (const auto& u : utxos) {
      total += u.litoshis;
    }
    litoshis = total;
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

bool LtcRpcClient::listUnspent(const std::string& address, std::vector<LtcUtxo>& utxos) {
  try {
    std::string params = "[0,9999999,[\"" + address + "\"]]";
    std::string responseBody = rpcCall("listunspent", params);
    Common::JsonValue json = Common::JsonValue::fromString(responseBody);

    if (!json.isObject() || !json.contains("result")) {
      return false;
    }

    const auto& result = json("result");
    if (!result.isArray()) {
      return false;
    }

    utxos.clear();
    for (size_t i = 0; i < result.size(); ++i) {
      const auto& item = result[i];
      if (!item.isObject()) continue;

      LtcUtxo utxo;
      utxo.txid = item.contains("txid") ? item("txid").getString() : "";
      utxo.vout = item.contains("vout")
          ? static_cast<uint32_t>(item("vout").getInteger()) : 0;

      if (item.contains("amount")) {
        const auto& amtVal = item("amount");
        if (amtVal.isReal()) {
          double ltc = amtVal.getReal();
          utxo.litoshis = static_cast<uint64_t>(ltc * 100000000.0 + 0.5);
        } else if (amtVal.isInteger()) {
          int64_t raw = amtVal.getInteger();
          utxo.litoshis = (raw >= 0) ? static_cast<uint64_t>(raw) * 100000000ULL : 0;
        } else {
          utxo.litoshis = 0;
        }
      } else {
        utxo.litoshis = 0;
      }

      utxo.scriptPubKey = item.contains("scriptPubKey")
          ? item("scriptPubKey").getString() : "";
      utxo.confirmations = item.contains("confirmations")
          ? static_cast<uint32_t>(item("confirmations").getInteger()) : 0;

      utxos.push_back(std::move(utxo));
    }

    return true;
  } catch (const std::exception&) {
    return false;
  }
}

bool LtcRpcClient::getAddressPubkey(const std::string& address, std::string& pubkeyHex) {
  try {
    std::string params = "[\"" + address + "\"]";
    std::string responseBody = rpcCall("getaddressinfo", params);
    Common::JsonValue json = Common::JsonValue::fromString(responseBody);
    if (!json.isObject() || !json.contains("result")) return false;
    const auto& result = json("result");
    if (!result.isObject() || !result.contains("pubkey")) return false;
    pubkeyHex = result("pubkey").getString();
    if (pubkeyHex.size() >= 2 && pubkeyHex[0] == '0' &&
        (pubkeyHex[1] == 'x' || pubkeyHex[1] == 'X'))
      pubkeyHex = pubkeyHex.substr(2);
    return pubkeyHex.size() == 66;
  } catch (const std::exception&) {
    return false;
  }
}

bool LtcRpcClient::estimateFeeLitoshis(uint64_t& feeLitoshi, int confTarget) {
  feeLitoshi = 1000;
  try {
    std::string params = "[" + std::to_string(confTarget) + "]";
    std::string responseBody = rpcCall("estimatesmartfee", params);
    Common::JsonValue json = Common::JsonValue::fromString(responseBody);
    if (!json.isObject() || !json.contains("result")) return true;
    const auto& result = json("result");
    if (!result.isObject() || !result.contains("feerate")) return true;
    double ltcPerKb = 0.0;
    try {
      ltcPerKb = result("feerate").getReal();
    } catch (const std::exception&) {
      return true;
    }
    if (ltcPerKb <= 0.0) return true;
    // ~110 vB SegWit claim tx -> fee ≈ feerate * 0.110 kB
    double ltcFee = ltcPerKb * 0.110;
    uint64_t est = static_cast<uint64_t>(ltcFee * 1e8 + 0.5);
    if (est > feeLitoshi) feeLitoshi = est;
    if (feeLitoshi > 100000) feeLitoshi = 100000;
    return true;
  } catch (const std::exception&) {
    return true;
  }
}

bool LtcRpcClient::getRawTransaction(const std::string& txid, std::string& rawTxHex) {
  try {
    std::string params = "[\"" + txid + "\", false]";
    std::string responseBody = rpcCall("getrawtransaction", params);
    Common::JsonValue json = Common::JsonValue::fromString(responseBody);

    if (!json.isObject() || !json.contains("result")) {
      return false;
    }

    const auto& result = json("result");
    if (!result.isString()) {
      return false;
    }

    rawTxHex = result.getString();
    return !rawTxHex.empty();
  } catch (const std::exception&) {
    return false;
  }
}

bool LtcRpcClient::sendRawTransaction(const std::string& rawTxHex, std::string& txid) {
  try {
    std::string params = "[\"" + rawTxHex + "\"]";
    std::string responseBody = rpcCall("sendrawtransaction", params);
    Common::JsonValue json = Common::JsonValue::fromString(responseBody);

    if (!json.isObject() || !json.contains("result")) {
      return false;
    }

    const auto& result = json("result");
    if (!result.isString()) {
      return false;
    }

    txid = result.getString();
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

bool LtcRpcClient::decodeRawTransaction(const std::string& rawTxHex, std::string& jsonResult) {
  try {
    std::string params = "[\"" + rawTxHex + "\"]";
    std::string responseBody = rpcCall("decoderawtransaction", params);
    Common::JsonValue json = Common::JsonValue::fromString(responseBody);

    if (!json.isObject() || !json.contains("result")) {
      return false;
    }

    jsonResult = json("result").toString();
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

bool LtcRpcClient::validateAddress(const std::string& address, bool& isValid) {
  try {
    std::string params = "[\"" + address + "\"]";
    std::string responseBody = rpcCall("validateaddress", params);
    Common::JsonValue json = Common::JsonValue::fromString(responseBody);

    if (!json.isObject() || !json.contains("result")) {
      return false;
    }

    const auto& result = json("result");
    if (!result.isObject() || !result.contains("isvalid")) {
      return false;
    }

    isValid = result("isvalid").getBool();
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

bool LtcRpcClient::importAddress(const std::string& address, const std::string& label, bool rescan) {
  try {
    std::string params = "[\"" + address + "\",\"" + label + "\"," +
                         (rescan ? "true" : "false") + "]";
    std::string responseBody = rpcCall("importaddress", params);
    Common::JsonValue json = Common::JsonValue::fromString(responseBody);

    if (!json.isObject()) {
      return false;
    }

    if (json.contains("error")) {
      const auto& err = json("error");
      if (err.isObject()) {
        return false;
      }
    }

    return true;
  } catch (const std::exception&) {
    return false;
  }
}

// ---- Base58Check decode (WIF helper) ----------------------------------------

static const char kBase58Alphabet[] =
    "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

static std::vector<uint8_t> base58Decode(const std::string& s) {
  std::vector<uint8_t> result;
  for (char c : s) {
    const char* pos = strchr(kBase58Alphabet, c);
    if (!pos) return {};
    size_t digit = static_cast<size_t>(pos - kBase58Alphabet);
    uint32_t carry = static_cast<uint32_t>(digit);
    for (auto& b : result) {
      carry += static_cast<uint32_t>(b) * 58;
      b = static_cast<uint8_t>(carry & 0xFF);
      carry >>= 8;
    }
    while (carry > 0) {
      result.push_back(static_cast<uint8_t>(carry & 0xFF));
      carry >>= 8;
    }
  }
  size_t leading = 0;
  while (leading < s.size() && s[leading] == '1') {
    result.push_back(0);
    ++leading;
  }
  std::reverse(result.begin(), result.end());
  return result;
}

static bool base58CheckDecode(const std::string& encoded, uint8_t& version,
                              std::vector<uint8_t>& payload) {
  auto decoded = base58Decode(encoded);
  if (decoded.size() < 5) return false;
  std::vector<uint8_t> data(decoded.begin(), decoded.end() - 4);
  std::array<uint8_t, 4> checksum;
  std::copy(decoded.end() - 4, decoded.end(), checksum.begin());
  auto hash = LtcHtlcScript::doubleSha256(data);
  if (hash[0] != checksum[0] || hash[1] != checksum[1] ||
      hash[2] != checksum[2] || hash[3] != checksum[3]) {
    return false;
  }
  if (data.empty()) return false;
  version = data[0];
  payload.assign(data.begin() + 1, data.end());
  return true;
}

// ---- WIF helper (now in LtcHtlcScript) --------------------------------------

// ---- Bech32 helpers (LTC P2WSH address encoding) ----------------------------

static const char kBech32Charset[] = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";

static const uint32_t kBech32Gen[] = {
  0x3b6a57b2, 0x26508e6d, 0x1ea119fa, 0x3d4233dd, 0x2a1462b3
};

static uint32_t bech32Polymod(const std::vector<uint8_t>& values) {
  uint32_t chk = 1;
  for (uint8_t v : values) {
    uint32_t b = chk >> 25;
    chk = ((chk & 0x1ffffff) << 5) ^ v;
    for (size_t i = 0; i < 5; ++i) {
      if ((b >> i) & 1) chk ^= kBech32Gen[i];
    }
  }
  return chk;
}

static std::vector<uint8_t> hrpExpand(const std::string& hrp) {
  std::vector<uint8_t> exp;
  exp.reserve(hrp.size() * 2 + 1);
  for (char c : hrp) exp.push_back(static_cast<uint8_t>(c >> 5));
  exp.push_back(0);
  for (char c : hrp) exp.push_back(static_cast<uint8_t>(c & 31));
  return exp;
}

static std::vector<uint8_t> convertBits8to5(const std::vector<uint8_t>& data) {
  uint32_t acc = 0;
  int bits = 0;
  const uint8_t maxv = 31;
  std::vector<uint8_t> result;
  for (uint8_t v : data) {
    acc = (acc << 8) | v;
    bits += 8;
    while (bits >= 5) {
      bits -= 5;
      result.push_back(static_cast<uint8_t>((acc >> bits) & maxv));
    }
  }
  if (bits > 0) {
    result.push_back(static_cast<uint8_t>((acc << (5 - bits)) & maxv));
  }
  return result;
}

static std::string bech32Encode(const std::string& hrp,
                                 const std::vector<uint8_t>& data5) {
  auto hrpExp = hrpExpand(hrp);
  std::vector<uint8_t> combined = hrpExp;
  combined.insert(combined.end(), data5.begin(), data5.end());
  combined.insert(combined.end(), 6, 0);
  uint32_t checksum = bech32Polymod(combined) ^ 1;
  std::string result = hrp + "1";
  for (uint8_t v : data5) result += kBech32Charset[v];
  for (int i = 0; i < 6; ++i)
    result += kBech32Charset[(checksum >> (5 * (5 - i))) & 31];
  return result;
}

static std::string ltcWitnessScriptToAddress(
    const std::vector<uint8_t>& witnessScript, const std::string& hrp = "ltc") {
  auto hash = LtcHtlcScript::sha256(witnessScript);
  std::vector<uint8_t> witnessProgram;
  witnessProgram.push_back(0x00);
  witnessProgram.insert(witnessProgram.end(), hash.begin(), hash.end());
  auto data5 = convertBits8to5(witnessProgram);
  return bech32Encode(hrp, data5);
}

// ---- Transaction serialization helpers --------------------------------------

static void writeLE32(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

static void writeLE64(std::vector<uint8_t>& out, uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    v >>= 8;
  }
}

static void writeVarInt(std::vector<uint8_t>& out, uint64_t n) {
  if (n < 0xFD) {
    out.push_back(static_cast<uint8_t>(n));
  } else if (n <= 0xFFFF) {
    out.push_back(0xFD);
    out.push_back(static_cast<uint8_t>(n & 0xFF));
    out.push_back(static_cast<uint8_t>((n >> 8) & 0xFF));
  } else if (n <= 0xFFFFFFFF) {
    out.push_back(0xFE);
    writeLE32(out, static_cast<uint32_t>(n));
  } else {
    out.push_back(0xFF);
    writeLE64(out, n);
  }
}

// ---- HTLC operations --------------------------------------------------------

bool LtcRpcClient::lockHtlc(const std::string& senderWif,
                             const std::string& recipientAddress,
                             const std::string& hashLockSha256Hex,
                             uint32_t timeoutBlock,
                             uint64_t amountLitoshis,
                             std::string& lockTxId,
                             std::string& redeemScriptHex) {
  std::array<uint8_t, 32> senderPrivKey{};
  if (!LtcHtlcScript::wifToPrivKey(senderWif, senderPrivKey)) return false;

  CryptoNote::SwapDaemon::Crypto::Secp256k1Signer signer;
  auto senderPubKey = signer.derivePublicKeyCompressed(senderPrivKey);

  std::vector<uint8_t> recipientPubKey(33, 0);
  if (recipientAddress.size() == 66) {
    auto bytes = LtcHtlcScript::hexToBytes(recipientAddress);
    if (bytes.size() == 33 && (bytes[0] == 0x02 || bytes[0] == 0x03)) {
      recipientPubKey = bytes;
    }
  }

  auto hashLockBytes = LtcHtlcScript::hexToBytes(hashLockSha256Hex);
  if (hashLockBytes.size() != 32) return false;

  // Build witness script (same as HTLC redeem script) and P2WSH bech32 address.
  auto witnessScript = LtcHtlcScript::createHashTimeLockScript(
      hashLockBytes, recipientPubKey, senderPubKey, timeoutBlock);
  redeemScriptHex = LtcHtlcScript::bytesToHex(witnessScript);
  std::string htlcAddress = ltcWitnessScriptToAddress(witnessScript, "ltc");

  if (recipientPubKey.size() != 33 ||
      (recipientPubKey[0] != 0x02 && recipientPubKey[0] != 0x03)) {
    return false;
  }

  std::string params = "[\"" + htlcAddress + "\"," +
                       std::to_string(static_cast<double>(amountLitoshis) / 1e8) + "]";
  try {
    std::string resp = rpcCall("sendtoaddress", params);
    if (resp.empty()) return false;

    Common::JsonValue json = Common::JsonValue::fromString(resp);
    if (!json.isObject() || !json.contains("result")) return false;
    const auto& result = json("result");
    if (!result.isString()) return false;
    lockTxId = result.getString();
    return lockTxId.size() == 64;
  } catch (const std::exception&) {
    return false;
  }
}

bool LtcRpcClient::verifyLock(const std::string& htlcAddress,
                               uint64_t expectedLitoshis,
                               uint32_t minConfirms) {
  std::vector<LtcUtxo> utxos;
  if (!listUnspent(htlcAddress, utxos)) return false;

  for (const auto& utxo : utxos) {
    if (utxo.litoshis >= expectedLitoshis &&
        utxo.confirmations >= minConfirms) {
      return true;
    }
  }
  return false;
}

// ---- Claim/Refund witness and raw tx builders -------------------------------

static std::vector<std::vector<uint8_t>> createClaimWitness(
    const std::vector<uint8_t>& signature,
    const std::vector<uint8_t>& preimage,
    const std::vector<uint8_t>& witnessScript) {
  std::vector<std::vector<uint8_t>> witness;
  witness.reserve(4);
  witness.push_back(signature);
  witness.push_back(preimage);
  witness.push_back({0x51});  // OP_1
  witness.push_back(witnessScript);
  return witness;
}

static std::vector<std::vector<uint8_t>> createRefundWitness(
    const std::vector<uint8_t>& signature,
    const std::vector<uint8_t>& witnessScript) {
  std::vector<std::vector<uint8_t>> witness;
  witness.reserve(3);
  witness.push_back(signature);
  witness.push_back({0x00});  // OP_0
  witness.push_back(witnessScript);
  return witness;
}

static std::vector<uint8_t> buildRawSegWitTx(
    const std::string& inputTxid, uint32_t inputVout, uint64_t inputAmount,
    const std::vector<uint8_t>& scriptSig,
    const std::vector<std::vector<uint8_t>>& witnessStack,
    const std::string& outputAddress, uint64_t outputAmount,
    uint32_t nLockTime) {
  (void)inputAmount;

  std::vector<uint8_t> tx;

  // Version (4 bytes LE)
  writeLE32(tx, 2);

  // SegWit marker + flag
  tx.push_back(0x00);
  tx.push_back(0x01);

  // Input count
  writeVarInt(tx, 1);

  // Input txid (little-endian: reverse the hex bytes)
  auto txidBytes = LtcHtlcScript::hexToBytes(inputTxid);
  std::reverse(txidBytes.begin(), txidBytes.end());
  tx.insert(tx.end(), txidBytes.begin(), txidBytes.end());

  // Input vout
  writeLE32(tx, inputVout);

  // scriptSig
  writeVarInt(tx, scriptSig.size());
  tx.insert(tx.end(), scriptSig.begin(), scriptSig.end());

  // Sequence
  writeLE32(tx, 0xFFFFFFFE);

  // Output count
  writeVarInt(tx, 1);

  // Output value
  writeLE64(tx, outputAmount);

  // Output scriptPubKey (P2PKH)
  uint8_t addrVersion = 0;
  std::vector<uint8_t> pubKeyHash;
  if (!LtcHtlcScript::base58CheckDecode(outputAddress, addrVersion, pubKeyHash) ||
      pubKeyHash.size() != 20) {
    throw std::runtime_error("buildRawSegWitTx: invalid P2PKH output address");
  }
  if (addrVersion != 0x30) {
    throw std::runtime_error("buildRawSegWitTx: not a LTC P2PKH address");
  }
  auto scriptPubKey = LtcHtlcScript::buildP2pkhScriptPubKey(pubKeyHash);
  writeVarInt(tx, scriptPubKey.size());
  tx.insert(tx.end(), scriptPubKey.begin(), scriptPubKey.end());

  // Witness data
  writeVarInt(tx, witnessStack.size());
  for (const auto& item : witnessStack) {
    writeVarInt(tx, item.size());
    tx.insert(tx.end(), item.begin(), item.end());
  }

  // nLockTime
  writeLE32(tx, nLockTime);

  return tx;
}

bool LtcRpcClient::claim(const std::string& claimerWif,
                          const std::string& htlcTxid,
                          uint32_t htlcVout,
                          uint64_t htlcAmount,
                          const std::string& redeemScriptHex,
                          const std::string& preimageHex,
                          const std::string& destAddress,
                          std::string& claimTxId) {
  std::array<uint8_t, 32> privKey{};
  if (!LtcHtlcScript::wifToPrivKey(claimerWif, privKey)) return false;

  auto witnessScript = LtcHtlcScript::hexToBytes(redeemScriptHex);
  auto preimage      = LtcHtlcScript::hexToBytes(preimageHex);

  uint8_t addrVersion = 0;
  std::vector<uint8_t> pubKeyHash;
  if (!LtcHtlcScript::base58CheckDecode(destAddress, addrVersion, pubKeyHash)) return false;
  auto outputScript = LtcHtlcScript::buildP2pkhScriptPubKey(pubKeyHash);

  uint64_t fee = 1000;
  estimateFeeLitoshis(fee, /*confTarget=*/2);
  if (htlcAmount <= fee) return false;
  uint64_t outputAmount = htlcAmount - fee;

  const uint32_t nSequence = 0xFFFFFFFD;

  auto der = LtcHtlcScript::signInput(privKey, /*version=*/2, /*locktime=*/0,
                                       nSequence,
                                       htlcTxid, htlcVout,
                                       witnessScript, htlcAmount,
                                       outputScript, outputAmount);
  if (der.empty()) return false;

  // P2WSH: empty scriptSig, witness stack carries the data.
  std::vector<uint8_t> emptyScriptSig;
  auto witnessStack = createClaimWitness(der, preimage, witnessScript);

  auto rawTx = buildRawSegWitTx(
      htlcTxid, htlcVout, htlcAmount,
      emptyScriptSig, witnessStack, destAddress, outputAmount, /*nLocktime=*/0);

  (void)nSequence;

  std::string txHex = LtcHtlcScript::bytesToHex(rawTx);
  return sendRawTransaction(txHex, claimTxId);
}

bool LtcRpcClient::refundHtlc(const std::string& senderWif,
                               const std::string& htlcTxid,
                               uint32_t htlcVout,
                               uint64_t htlcAmount,
                               const std::string& redeemScriptHex,
                               uint32_t timeoutBlock,
                               const std::string& destAddress,
                               std::string& refundTxId) {
  std::array<uint8_t, 32> privKey{};
  if (!LtcHtlcScript::wifToPrivKey(senderWif, privKey)) return false;

  auto witnessScript = LtcHtlcScript::hexToBytes(redeemScriptHex);

  uint32_t nLocktime = timeoutBlock;

  uint8_t addrVersion = 0;
  std::vector<uint8_t> pubKeyHash;
  if (!LtcHtlcScript::base58CheckDecode(destAddress, addrVersion, pubKeyHash)) return false;
  auto outputScript = LtcHtlcScript::buildP2pkhScriptPubKey(pubKeyHash);

  const uint64_t fee = 1000;
  if (htlcAmount <= fee) return false;
  uint64_t outputAmount = htlcAmount - fee;

  auto der = LtcHtlcScript::signInput(privKey, /*version=*/2, nLocktime,
                                       /*nSequence=*/0xFFFFFFFE,
                                       htlcTxid, htlcVout,
                                       witnessScript, htlcAmount,
                                       outputScript, outputAmount);
  if (der.empty()) return false;

  std::vector<uint8_t> emptyScriptSig;
  auto witnessStack = createRefundWitness(der, witnessScript);

  auto rawTx = buildRawSegWitTx(
      htlcTxid, htlcVout, htlcAmount,
      emptyScriptSig, witnessStack, destAddress, outputAmount, nLocktime);

  std::string txHex = LtcHtlcScript::bytesToHex(rawTx);
  return sendRawTransaction(txHex, refundTxId);
}

bool LtcRpcClient::verifyMessage(const std::string& address, const std::string& signature,
                                  const std::string& message, bool& valid) {
  try {
    std::string params = "[\"" + address + "\",\"" + signature + "\",\"" + message + "\"]";
    std::string resp = rpcCall("verifymessage", params);
    if (resp.empty()) return false;
    valid = resp.find("\"result\":true") != std::string::npos
            || resp.find("\"result\": true") != std::string::npos;
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

} // namespace XfgSwap
