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

#include "DogeRpcClient.h"
#include "DogeHtlcScript.h"
#include "Crypto/Secp256k1Signer.h"
#include "Common/JsonValue.h"
#include "Common/WinCompat.h"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace XfgSwap {

// ---- Base64 encoding (for HTTP Basic Auth) ----------------------------------

static const char kBase64Table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string DogeRpcClient::base64Encode(const std::string& input) {
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

DogeRpcClient::DogeRpcClient(const std::string& host, uint16_t port,
                             const std::string& rpcUser, const std::string& rpcPassword)
    : m_host(host)
    , m_port(port)
    , m_rpcUser(rpcUser)
    , m_rpcPassword(rpcPassword) {
  m_authHeader = "Basic " + base64Encode(rpcUser + ":" + rpcPassword);
}

// ---- Low-level HTTP POST (POSIX sockets) ------------------------------------

std::string DogeRpcClient::httpPost(const std::string& body) {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    throw std::runtime_error("DogeRpcClient: failed to create socket");
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
    throw std::runtime_error("DogeRpcClient: failed to resolve host: " + m_host);
  }

  int ret = connect(sock, result->ai_addr, result->ai_addrlen);
  freeaddrinfo(result);
  if (ret < 0) {
    close(sock);
    throw std::runtime_error("DogeRpcClient: failed to connect to " + m_host + ":" + portStr);
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
    throw std::runtime_error("DogeRpcClient: failed to send HTTP request");
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
    throw std::runtime_error("DogeRpcClient: malformed HTTP response");
  }

  return response.substr(headerEnd + 4);
}

// ---- JSON-RPC call wrapper --------------------------------------------------

std::string DogeRpcClient::rpcCall(const std::string& method, const std::string& params) {
  // Bitcoin JSON-RPC envelope (Doge Core speaks the same protocol)
  std::string body = "{\"jsonrpc\":\"1.0\",\"id\":\"xfg-swapd\",\"method\":\"" +
                     method + "\",\"params\":" + params + "}";
  return httpPost(body);
}

// ---- Public API -------------------------------------------------------------

bool DogeRpcClient::getBlockCount(uint64_t& height) {
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

bool DogeRpcClient::getTransaction(const std::string& txid, DogeTxInfo& info) {
  try {
    // getrawtransaction <txid> true  (verbose=true returns JSON)
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

bool DogeRpcClient::getBalance(const std::string& address, uint64_t& koinu) {
  try {
    // Use listunspent filtered by address, then sum up
    std::vector<DogeUtxo> utxos;
    if (!listUnspent(address, utxos)) {
      return false;
    }

    uint64_t total = 0;
    for (const auto& u : utxos) {
      total += u.koinu;
    }
    koinu = total;
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

bool DogeRpcClient::listUnspent(const std::string& address, std::vector<DogeUtxo>& utxos) {
  try {
    // listunspent minconf maxconf [addresses]
    // minconf=0 to include mempool, maxconf=9999999
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

      DogeUtxo utxo;
      utxo.txid = item.contains("txid") ? item("txid").getString() : "";
      utxo.vout = item.contains("vout")
          ? static_cast<uint32_t>(item("vout").getInteger()) : 0;

      // Bitcoin RPC returns "amount" as a floating-point DOGE value (e.g., 0.001).
      // Convert to koinu by multiplying by 1e8 and rounding.
      if (item.contains("amount")) {
        const auto& amtVal = item("amount");
        if (amtVal.isReal()) {
          double doge = amtVal.getReal();
          utxo.koinu = static_cast<uint64_t>(doge * 100000000.0 + 0.5);
        } else if (amtVal.isInteger()) {
          int64_t raw = amtVal.getInteger();
          utxo.koinu = (raw >= 0) ? static_cast<uint64_t>(raw) * 100000000ULL : 0;
        } else {
          utxo.koinu = 0;
        }
      } else {
        utxo.koinu = 0;
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

bool DogeRpcClient::getAddressPubkey(const std::string& address, std::string& pubkeyHex) {
  try {
    std::string params = "[\"" + address + "\"]";
    std::string responseBody = rpcCall("getaddressinfo", params);
    Common::JsonValue json = Common::JsonValue::fromString(responseBody);
    if (!json.isObject() || !json.contains("result")) return false;
    const auto& result = json("result");
    if (!result.isObject() || !result.contains("pubkey")) return false;
    pubkeyHex = result("pubkey").getString();
    // Normalize: strip 0x, require 66 hex chars
    if (pubkeyHex.size() >= 2 && pubkeyHex[0] == '0' &&
        (pubkeyHex[1] == 'x' || pubkeyHex[1] == 'X'))
      pubkeyHex = pubkeyHex.substr(2);
    return pubkeyHex.size() == 66;
  } catch (...) {
    return false;
  }
}

bool DogeRpcClient::estimateFeeKoinu(uint64_t& feeKoinu, int confTarget) {
  // Floor: 1000 koinu (previous hardcode). Prefer estimatesmartfee when available.
  feeKoinu = 1000;
  try {
    std::string params = "[" + std::to_string(confTarget) + "]";
    std::string responseBody = rpcCall("estimatesmartfee", params);
    Common::JsonValue json = Common::JsonValue::fromString(responseBody);
    if (!json.isObject() || !json.contains("result")) return true;  // use floor
    const auto& result = json("result");
    if (!result.isObject() || !result.contains("feerate")) return true;
    // feerate is DOGE/kB as float
    double dogePerKb = 0.0;
    try {
      dogePerKb = result("feerate").getReal();
    } catch (...) {
      return true;
    }
    if (dogePerKb <= 0.0) return true;
    // ~250 vB claim tx → fee ≈ feerate * 0.25 kB
    double dogeFee = dogePerKb * 0.25;
    uint64_t est = static_cast<uint64_t>(dogeFee * 1e8 + 0.5);
    if (est > feeKoinu) feeKoinu = est;
    // Cap at 0.001 DOGE to avoid runaway estimates
    if (feeKoinu > 100000) feeKoinu = 100000;
    return true;
  } catch (...) {
    return true;
  }
}

bool DogeRpcClient::getRawTransaction(const std::string& txid, std::string& rawTxHex) {
  try {
    // verbose=false → result is hex string (verbose=true returns an object)
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
  } catch (...) {
    return false;
  }
}

bool DogeRpcClient::sendRawTransaction(const std::string& rawTxHex, std::string& txid) {
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

bool DogeRpcClient::decodeRawTransaction(const std::string& rawTxHex, std::string& jsonResult) {
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

bool DogeRpcClient::validateAddress(const std::string& address, bool& isValid) {
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

bool DogeRpcClient::importAddress(const std::string& address, const std::string& label, bool rescan) {
  try {
    std::string params = "[\"" + address + "\",\"" + label + "\"," +
                         (rescan ? "true" : "false") + "]";
    std::string responseBody = rpcCall("importaddress", params);
    Common::JsonValue json = Common::JsonValue::fromString(responseBody);

    // importaddress returns null on success
    if (!json.isObject()) {
      return false;
    }

    // If "error" is present and non-null, it failed
    if (json.contains("error")) {
      const auto& err = json("error");
      // null means no error (success)
      if (err.isObject()) {
        return false;
      }
    }

    return true;
  } catch (const std::exception&) {
    return false;
  }
}

// ─── HTLC operations ────────────────────────────────────────────────────────

bool DogeRpcClient::lockHtlc(const std::string& senderWif,
                             const std::string& recipientAddress,
                             const std::string& hashLockSha256Hex,
                             uint32_t timeoutBlock,
                             uint64_t amountKoinu,
                             std::string& lockTxId,
                             std::string& redeemScriptHex) {
  // Derive compressed sender public key from WIF.
  std::array<uint8_t, 32> senderPrivKey{};
  if (!DogeHtlcScript::wifToPrivKey(senderWif, senderPrivKey)) return false;

  CryptoNote::SwapDaemon::Crypto::Secp256k1Signer signer;
  auto senderPubKey = signer.derivePublicKeyCompressed(senderPrivKey);

  // Recipient compressed pubkey for the claim path. Accept either a 66-char
  // compressed pubkey hex (0x02/0x03 prefix) or, as in BCH, an address whose
  // pubkey the caller resolves out-of-band via getAddressPubkey.
  std::vector<uint8_t> recipientPubKey(33, 0);
  if (recipientAddress.size() == 66) {
    auto bytes = DogeHtlcScript::hexToBytes(recipientAddress);
    if (bytes.size() == 33 && (bytes[0] == 0x02 || bytes[0] == 0x03)) {
      recipientPubKey = bytes;
    }
  }

  auto hashLockBytes = DogeHtlcScript::hexToBytes(hashLockSha256Hex);
  if (hashLockBytes.size() != 32) return false;

  // Build redeem script and P2SH address.
  auto redeemScript = DogeHtlcScript::createRedeemScript(
      hashLockBytes, recipientPubKey, senderPubKey, timeoutBlock);
  redeemScriptHex = DogeHtlcScript::bytesToHex(redeemScript);
  bool testnet = false;  // TODO: derive from config
  std::string htlcAddress = DogeHtlcScript::computeP2shAddress(redeemScript, testnet);

  // Fail closed: zero recipient pubkey produces a permanently unclaimable HTLC.
  if (recipientPubKey.size() != 33 ||
      (recipientPubKey[0] != 0x02 && recipientPubKey[0] != 0x03)) {
    return false;
  }

  // Fund the HTLC address by sending amountKoinu to it.
  // Use the node wallet's sendtoaddress RPC — this handles UTXO selection.
  std::string params = "[\"" + htlcAddress + "\"," +
                       std::to_string(static_cast<double>(amountKoinu) / 1e8) + "]";
  try {
    std::string resp = rpcCall("sendtoaddress", params);
    if (resp.empty()) return false;

    // rpcCall returns the full JSON-RPC envelope; extract result string.
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

bool DogeRpcClient::verifyLock(const std::string& htlcAddress,
                               uint64_t expectedKoinu,
                               uint32_t minConfirms) {
  std::vector<DogeUtxo> utxos;
  if (!listUnspent(htlcAddress, utxos)) return false;

  for (const auto& utxo : utxos) {
    if (utxo.koinu >= expectedKoinu &&
        utxo.confirmations >= minConfirms) {
      return true;
    }
  }
  return false;
}

bool DogeRpcClient::claim(const std::string& claimerWif,
                          const std::string& htlcTxid,
                          uint32_t htlcVout,
                          uint64_t htlcAmount,
                          const std::string& redeemScriptHex,
                          const std::string& preimageHex,
                          const std::string& destAddress,
                          std::string& claimTxId) {
  std::array<uint8_t, 32> privKey{};
  if (!DogeHtlcScript::wifToPrivKey(claimerWif, privKey)) return false;

  auto redeemScript = DogeHtlcScript::hexToBytes(redeemScriptHex);
  auto preimage     = DogeHtlcScript::hexToBytes(preimageHex);

  // Build output script (P2PKH to destAddress).
  uint8_t addrVersion = 0;
  std::vector<uint8_t> pubKeyHash;
  if (!DogeHtlcScript::base58CheckDecode(destAddress, addrVersion, pubKeyHash)) return false;
  auto outputScript = DogeHtlcScript::buildP2pkhScriptPubKey(pubKeyHash);

  uint64_t fee = 1000;
  estimateFeeKoinu(fee, /*confTarget=*/2);
  if (htlcAmount <= fee) return false;
  uint64_t outputAmount = htlcAmount - fee;

  // nSequence = 0xFFFFFFFD enables BIP-125 opt-in RBF so a stuck claim can be
  // fee-bumped by rebroadcasting with a higher fee (same inputs, higher fee).
  const uint32_t nSequence = 0xFFFFFFFD;

  // Sign using the pre-SegWit legacy sighash (Doge is NOT SegWit).
  auto der = DogeHtlcScript::signInput(privKey, /*version=*/1, /*locktime=*/0,
                                       nSequence,
                                       htlcTxid, htlcVout,
                                       redeemScript, htlcAmount,
                                       outputScript, outputAmount);
  if (der.empty()) return false;

  // Build scriptSig: <sig> <preimage> OP_1 <redeemScript>
  auto scriptSig = DogeHtlcScript::createClaimScriptSig(der, preimage, redeemScript);

  // Build raw tx and broadcast.
  auto rawTx = DogeHtlcScript::buildRawTransaction(
      htlcTxid, htlcVout, htlcAmount,
      scriptSig, destAddress, outputAmount, /*nLocktime=*/0);

  std::string txHex = DogeHtlcScript::bytesToHex(rawTx);
  return sendRawTransaction(txHex, claimTxId);
}

bool DogeRpcClient::refundHtlc(const std::string& senderWif,
                               const std::string& htlcTxid,
                               uint32_t htlcVout,
                               uint64_t htlcAmount,
                               const std::string& redeemScriptHex,
                               uint32_t timeoutBlock,
                               const std::string& destAddress,
                               std::string& refundTxId) {
  std::array<uint8_t, 32> privKey{};
  if (!DogeHtlcScript::wifToPrivKey(senderWif, privKey)) return false;

  auto redeemScript = DogeHtlcScript::hexToBytes(redeemScriptHex);

  // CLTV requires nLocktime >= timeoutBlock for the refund path to activate.
  uint32_t nLocktime = timeoutBlock;

  // Build output.
  uint8_t addrVersion = 0;
  std::vector<uint8_t> pubKeyHash;
  if (!DogeHtlcScript::base58CheckDecode(destAddress, addrVersion, pubKeyHash)) return false;
  auto outputScript = DogeHtlcScript::buildP2pkhScriptPubKey(pubKeyHash);

  const uint64_t fee = 1000;
  if (htlcAmount <= fee) return false;
  uint64_t outputAmount = htlcAmount - fee;

  // Refund inputs must use nSequence < 0xFFFFFFFF for CLTV to activate.
  auto der = DogeHtlcScript::signInput(privKey, /*version=*/1, nLocktime,
                                       /*nSequence=*/0xFFFFFFFE,
                                       htlcTxid, htlcVout,
                                       redeemScript, htlcAmount,
                                       outputScript, outputAmount);
  if (der.empty()) return false;

  auto scriptSig = DogeHtlcScript::createRefundScriptSig(der, redeemScript);

  auto rawTx = DogeHtlcScript::buildRawTransaction(
      htlcTxid, htlcVout, htlcAmount,
      scriptSig, destAddress, outputAmount, nLocktime);

  std::string txHex = DogeHtlcScript::bytesToHex(rawTx);
  return sendRawTransaction(txHex, refundTxId);
}

bool DogeRpcClient::verifyMessage(const std::string& address, const std::string& signature,
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
