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

#include "BtcRpcClient.h"
#include "BtcHtlcScript.h"
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

std::string BtcRpcClient::base64Encode(const std::string& input) {
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

BtcRpcClient::BtcRpcClient(const std::string& host, uint16_t port,
                           const std::string& rpcUser, const std::string& rpcPassword)
    : m_host(host)
    , m_port(port)
    , m_rpcUser(rpcUser)
    , m_rpcPassword(rpcPassword) {
  m_authHeader = "Basic " + base64Encode(rpcUser + ":" + rpcPassword);
}

// ---- Low-level HTTP POST (POSIX sockets) ------------------------------------

std::string BtcRpcClient::httpPost(const std::string& body) {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    throw std::runtime_error("BtcRpcClient: failed to create socket");
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
    throw std::runtime_error("BtcRpcClient: failed to resolve host: " + m_host);
  }

  int ret = connect(sock, result->ai_addr, result->ai_addrlen);
  freeaddrinfo(result);
  if (ret < 0) {
    close(sock);
    throw std::runtime_error("BtcRpcClient: failed to connect to " + m_host + ":" + portStr);
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
    throw std::runtime_error("BtcRpcClient: failed to send HTTP request");
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
    throw std::runtime_error("BtcRpcClient: malformed HTTP response");
  }

  return response.substr(headerEnd + 4);
}

// ---- JSON-RPC call wrapper --------------------------------------------------

std::string BtcRpcClient::rpcCall(const std::string& method, const std::string& params) {
  std::string body = "{\"jsonrpc\":\"1.0\",\"id\":\"xfg-swapd\",\"method\":\"" +
                     method + "\",\"params\":" + params + "}";
  return httpPost(body);
}

// ---- Public API -------------------------------------------------------------

bool BtcRpcClient::getBlockCount(uint64_t& height) {
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

bool BtcRpcClient::getTransaction(const std::string& txid, BtcTxInfo& info) {
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

bool BtcRpcClient::getBalance(const std::string& address, uint64_t& satoshis) {
  try {
    std::vector<BtcUtxo> utxos;
    if (!listUnspent(address, utxos)) {
      return false;
    }

    uint64_t total = 0;
    for (const auto& u : utxos) {
      total += u.satoshis;
    }
    satoshis = total;
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

bool BtcRpcClient::listUnspent(const std::string& address, std::vector<BtcUtxo>& utxos) {
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

      BtcUtxo utxo;
      utxo.txid = item.contains("txid") ? item("txid").getString() : "";
      utxo.vout = item.contains("vout")
          ? static_cast<uint32_t>(item("vout").getInteger()) : 0;

      if (item.contains("amount")) {
        const auto& amtVal = item("amount");
        if (amtVal.isReal()) {
          double btc = amtVal.getReal();
          utxo.satoshis = static_cast<uint64_t>(btc * 100000000.0 + 0.5);
        } else if (amtVal.isInteger()) {
          int64_t raw = amtVal.getInteger();
          utxo.satoshis = (raw >= 0) ? static_cast<uint64_t>(raw) * 100000000ULL : 0;
        } else {
          utxo.satoshis = 0;
        }
      } else {
        utxo.satoshis = 0;
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

bool BtcRpcClient::getAddressPubkey(const std::string& address, std::string& pubkeyHex) {
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

bool BtcRpcClient::estimateFeeSatoshis(uint64_t& feeSats, int confTarget) {
  feeSats = 1000;
  try {
    std::string params = "[" + std::to_string(confTarget) + "]";
    std::string responseBody = rpcCall("estimatesmartfee", params);
    Common::JsonValue json = Common::JsonValue::fromString(responseBody);
    if (!json.isObject() || !json.contains("result")) return true;
    const auto& result = json("result");
    if (!result.isObject() || !result.contains("feerate")) return true;
    double btcPerKb = 0.0;
    try {
      btcPerKb = result("feerate").getReal();
    } catch (const std::exception&) {
      return true;
    }
    if (btcPerKb <= 0.0) return true;
    // ~110 vB SegWit claim tx -> fee ≈ feerate * 0.110 kB
    double btcFee = btcPerKb * 0.110;
    uint64_t est = static_cast<uint64_t>(btcFee * 1e8 + 0.5);
    if (est > feeSats) feeSats = est;
    if (feeSats > 100000) feeSats = 100000;
    return true;
  } catch (const std::exception&) {
    return true;
  }
}

bool BtcRpcClient::getRawTransaction(const std::string& txid, std::string& rawTxHex) {
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

bool BtcRpcClient::sendRawTransaction(const std::string& rawTxHex, std::string& txid) {
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

bool BtcRpcClient::decodeRawTransaction(const std::string& rawTxHex, std::string& jsonResult) {
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

bool BtcRpcClient::validateAddress(const std::string& address, bool& isValid) {
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

bool BtcRpcClient::importAddress(const std::string& address, const std::string& label, bool rescan) {
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

// ---- WIF and signing helpers (now in BtcHtlcScript) -------------------------

// --- HTLC operations --------------------------------------------------------

bool BtcRpcClient::lockHtlc(const std::string& senderWif,
                             const std::string& recipientAddress,
                             const std::string& hashLockSha256Hex,
                             uint32_t timeoutBlock,
                             uint64_t amountSatoshis,
                             std::string& lockTxId,
                             std::string& redeemScriptHex) {
  std::array<uint8_t, 32> senderPrivKey{};
  if (!BtcHtlcScript::wifToPrivKey(senderWif, senderPrivKey)) return false;

  CryptoNote::SwapDaemon::Crypto::Secp256k1Signer signer;
  auto senderPubKey = signer.derivePublicKeyCompressed(senderPrivKey);

  std::vector<uint8_t> recipientPubKey(33, 0);
  if (recipientAddress.size() == 66) {
    auto bytes = BtcHtlcScript::hexToBytes(recipientAddress);
    if (bytes.size() == 33 && (bytes[0] == 0x02 || bytes[0] == 0x03)) {
      recipientPubKey = bytes;
    }
  }

  auto hashLockBytes = BtcHtlcScript::hexToBytes(hashLockSha256Hex);
  if (hashLockBytes.size() != 32) return false;

  // Build witness script (same as HTLC redeem script) and P2WSH bech32 address.
  auto witnessScript = BtcHtlcScript::createHashTimeLockScript(
      hashLockBytes, 0, recipientPubKey, senderPubKey, timeoutBlock);
  redeemScriptHex = BtcHtlcScript::bytesToHex(witnessScript);
  std::string htlcAddress = BtcHtlcScript::witnessScriptToAddress(witnessScript);

  if (recipientPubKey.size() != 33 ||
      (recipientPubKey[0] != 0x02 && recipientPubKey[0] != 0x03)) {
    return false;
  }

  std::string params = "[\"" + htlcAddress + "\"," +
                       std::to_string(static_cast<double>(amountSatoshis) / 1e8) + "]";
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

bool BtcRpcClient::verifyLock(const std::string& htlcAddress,
                               uint64_t expectedSatoshis,
                               uint32_t minConfirms) {
  std::vector<BtcUtxo> utxos;
  if (!listUnspent(htlcAddress, utxos)) return false;

  for (const auto& utxo : utxos) {
    if (utxo.satoshis >= expectedSatoshis &&
        utxo.confirmations >= minConfirms) {
      return true;
    }
  }
  return false;
}

bool BtcRpcClient::claim(const std::string& claimerWif,
                          const std::string& htlcTxid,
                          uint32_t htlcVout,
                          uint64_t htlcAmount,
                          const std::string& redeemScriptHex,
                          const std::string& preimageHex,
                          const std::string& destAddress,
                          std::string& claimTxId) {
  std::array<uint8_t, 32> privKey{};
  if (!BtcHtlcScript::wifToPrivKey(claimerWif, privKey)) return false;

  auto witnessScript = BtcHtlcScript::hexToBytes(redeemScriptHex);
  auto preimage      = BtcHtlcScript::hexToBytes(preimageHex);

  uint8_t addrVersion = 0;
  std::vector<uint8_t> pubKeyHash;
  if (!BtcHtlcScript::base58CheckDecode(destAddress, addrVersion, pubKeyHash)) return false;
  auto outputScript = BtcHtlcScript::buildP2pkhScriptPubKey(pubKeyHash);

  uint64_t fee = 1000;
  estimateFeeSatoshis(fee, /*confTarget=*/2);
  if (htlcAmount <= fee) return false;
  uint64_t outputAmount = htlcAmount - fee;

  const uint32_t nSequence = 0xFFFFFFFD;

  auto der = BtcHtlcScript::signInput(privKey, /*version=*/2, /*locktime=*/0,
                                       nSequence,
                                       htlcTxid, htlcVout,
                                       witnessScript, htlcAmount,
                                       outputScript, outputAmount);
  if (der.empty()) return false;

  // P2WSH: empty scriptSig, witness stack carries the data.
  std::vector<uint8_t> emptyScriptSig;
  auto witnessStack = BtcHtlcScript::createClaimWitness(der, preimage, witnessScript);

  auto rawTx = BtcHtlcScript::buildRawSegWitTx(
      htlcTxid, htlcVout, htlcAmount,
      emptyScriptSig, witnessStack, destAddress, outputAmount, /*nLocktime=*/0);

  (void)nSequence;

  std::string txHex = BtcHtlcScript::bytesToHex(rawTx);
  return sendRawTransaction(txHex, claimTxId);
}

bool BtcRpcClient::refundHtlc(const std::string& senderWif,
                               const std::string& htlcTxid,
                               uint32_t htlcVout,
                               uint64_t htlcAmount,
                               const std::string& redeemScriptHex,
                               uint32_t timeoutBlock,
                               const std::string& destAddress,
                               std::string& refundTxId) {
  std::array<uint8_t, 32> privKey{};
  if (!BtcHtlcScript::wifToPrivKey(senderWif, privKey)) return false;

  auto witnessScript = BtcHtlcScript::hexToBytes(redeemScriptHex);

  uint32_t nLocktime = timeoutBlock;

  uint8_t addrVersion = 0;
  std::vector<uint8_t> pubKeyHash;
  if (!BtcHtlcScript::base58CheckDecode(destAddress, addrVersion, pubKeyHash)) return false;
  auto outputScript = BtcHtlcScript::buildP2pkhScriptPubKey(pubKeyHash);

  const uint64_t fee = 1000;
  if (htlcAmount <= fee) return false;
  uint64_t outputAmount = htlcAmount - fee;

  auto der = BtcHtlcScript::signInput(privKey, /*version=*/2, nLocktime,
                                       /*nSequence=*/0xFFFFFFFE,
                                       htlcTxid, htlcVout,
                                       witnessScript, htlcAmount,
                                       outputScript, outputAmount);
  if (der.empty()) return false;

  std::vector<uint8_t> emptyScriptSig;
  auto witnessStack = BtcHtlcScript::createRefundWitness(der, witnessScript);

  auto rawTx = BtcHtlcScript::buildRawSegWitTx(
      htlcTxid, htlcVout, htlcAmount,
      emptyScriptSig, witnessStack, destAddress, outputAmount, nLocktime);

  std::string txHex = BtcHtlcScript::bytesToHex(rawTx);
  return sendRawTransaction(txHex, refundTxId);
}

bool BtcRpcClient::verifyMessage(const std::string& address, const std::string& signature,
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
