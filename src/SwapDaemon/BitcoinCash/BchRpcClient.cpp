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

#include "BchRpcClient.h"
#include "HtlcScript.h"
#include "Crypto/Secp256k1Signer.h"
#include "Crypto/Bip143Sighash.h"
#include "Common/JsonValue.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <algorithm>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace XfgSwap {

// ---- Base64 encoding (for HTTP Basic Auth) ----------------------------------

static const char kBase64Table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string BchRpcClient::base64Encode(const std::string& input) {
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

BchRpcClient::BchRpcClient(const std::string& host, uint16_t port,
                           const std::string& rpcUser, const std::string& rpcPassword)
    : m_host(host)
    , m_port(port)
    , m_rpcUser(rpcUser)
    , m_rpcPassword(rpcPassword) {
  m_authHeader = "Basic " + base64Encode(rpcUser + ":" + rpcPassword);
}

// ---- Low-level HTTP POST (POSIX sockets) ------------------------------------

std::string BchRpcClient::httpPost(const std::string& body) {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    throw std::runtime_error("BchRpcClient: failed to create socket");
  }

  // 10-second timeout
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

  std::string portStr = std::to_string(m_port);
  int gai = getaddrinfo(m_host.c_str(), portStr.c_str(), &hints, &result);
  if (gai != 0) {
    close(sock);
    throw std::runtime_error("BchRpcClient: failed to resolve host: " + m_host);
  }

  int ret = connect(sock, result->ai_addr, result->ai_addrlen);
  freeaddrinfo(result);
  if (ret < 0) {
    close(sock);
    throw std::runtime_error("BchRpcClient: failed to connect to " + m_host + ":" + portStr);
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
  ssize_t sent = send(sock, request.c_str(), request.size(), 0);
  if (sent < 0 || static_cast<size_t>(sent) != request.size()) {
    close(sock);
    throw std::runtime_error("BchRpcClient: failed to send HTTP request");
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
    throw std::runtime_error("BchRpcClient: malformed HTTP response");
  }

  return response.substr(headerEnd + 4);
}

// ---- JSON-RPC call wrapper --------------------------------------------------

std::string BchRpcClient::rpcCall(const std::string& method, const std::string& params) {
  // Bitcoin JSON-RPC envelope
  std::string body = "{\"jsonrpc\":\"1.0\",\"id\":\"xfg-swap\",\"method\":\"" +
                     method + "\",\"params\":" + params + "}";
  return httpPost(body);
}

// ---- Public API -------------------------------------------------------------

bool BchRpcClient::getBlockCount(uint64_t& height) {
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

bool BchRpcClient::getTransaction(const std::string& txid, BchTxInfo& info) {
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

bool BchRpcClient::getBalance(const std::string& address, uint64_t& satoshis) {
  try {
    // Use listunspent filtered by address, then sum up
    std::vector<BchUtxo> utxos;
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

bool BchRpcClient::listUnspent(const std::string& address, std::vector<BchUtxo>& utxos) {
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

      BchUtxo utxo;
      utxo.txid = item.contains("txid") ? item("txid").getString() : "";
      utxo.vout = item.contains("vout")
          ? static_cast<uint32_t>(item("vout").getInteger()) : 0;

      // Bitcoin RPC returns "amount" as a floating-point BCH value (e.g., 0.001).
      // Convert to satoshis by multiplying by 1e8 and rounding.
      if (item.contains("amount")) {
        const auto& amtVal = item("amount");
        if (amtVal.isReal()) {
          double bch = amtVal.getReal();
          utxo.satoshis = static_cast<uint64_t>(bch * 100000000.0 + 0.5);
        } else if (amtVal.isInteger()) {
          // Some RPC implementations may return integer satoshis for whole-coin amounts
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

bool BchRpcClient::sendRawTransaction(const std::string& rawTxHex, std::string& txid) {
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

bool BchRpcClient::decodeRawTransaction(const std::string& rawTxHex, std::string& jsonResult) {
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

bool BchRpcClient::validateAddress(const std::string& address, bool& isValid) {
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

bool BchRpcClient::importAddress(const std::string& address, const std::string& label, bool rescan) {
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

// ─── Helper: decode WIF-encoded private key to 32 raw bytes ─────────────────

static bool wifToPrivKey(const std::string& wif,
                          std::array<uint8_t, 32>& privKey) {
  uint8_t version = 0;
  std::vector<uint8_t> payload;
  if (!BchHtlcScript::base58CheckDecode(wif, version, payload)) return false;
  // WIF: version byte 0x80 (mainnet) / 0xEF (testnet), optional 0x01 compression flag
  if (payload.size() == 33 && payload.back() == 0x01) {
    payload.pop_back();  // strip compression flag
  }
  if (payload.size() != 32) return false;
  std::copy(payload.begin(), payload.end(), privKey.begin());
  return true;
}

// ─── HTLC operations ────────────────────────────────────────────────────────

bool BchRpcClient::lockHtlc(const std::string& senderWif,
                             const std::string& recipientAddress,
                             const std::string& hashLockSha256Hex,
                             uint32_t timeoutBlock,
                             uint64_t amountSatoshis,
                             std::string& lockTxId) {
  // Derive compressed sender public key from WIF.
  std::array<uint8_t, 32> senderPrivKey{};
  if (!wifToPrivKey(senderWif, senderPrivKey)) return false;

  CryptoNote::SwapDaemon::Crypto::Secp256k1Signer signer;
  auto senderPubKey  = signer.derivePublicKeyCompressed(senderPrivKey);

  // Derive recipient pubkey from their address — for P2SH locking we only
  // need the hash inside the redeem script.  We use the sender pubkey for
  // the refund path; the recipient pubkey is stored in the redeem script.
  // In the cross-chain swap flow, the recipient pubkey is exchanged out-of-band
  // and stored in SwapParams.ctrPubKey.  Since it's not passed here, we leave
  // a 33-byte zero placeholder — the caller (SwapDaemon) must populate it.
  // For now we also accept a 33-byte compressed pubkey hex via the
  // recipientAddress field if it begins with 0x02 or 0x03 (not a BCH address).
  std::vector<uint8_t> recipientPubKey(33, 0);
  if (recipientAddress.size() == 66) {
    auto bytes = BchHtlcScript::hexToBytes(recipientAddress);
    if (bytes.size() == 33 && (bytes[0] == 0x02 || bytes[0] == 0x03)) {
      recipientPubKey = bytes;
    }
  }

  auto hashLockBytes = BchHtlcScript::hexToBytes(hashLockSha256Hex);
  if (hashLockBytes.size() != 32) return false;

  // Build redeem script and P2SH address.
  auto redeemScript = BchHtlcScript::createRedeemScript(
      hashLockBytes, recipientPubKey, senderPubKey, timeoutBlock);
  bool testnet = false;  // TODO: derive from config
  std::string htlcAddress = BchHtlcScript::computeP2shAddress(redeemScript, testnet);

  // Fund the HTLC address by sending amountSatoshis to it.
  // Use the node wallet's sendtoaddress RPC — this handles UTXO selection.
  std::string params = "[\"" + htlcAddress + "\"," +
                       std::to_string(static_cast<double>(amountSatoshis) / 1e8) + "]";
  std::string resp = rpcCall("sendtoaddress", params);
  if (resp.empty()) return false;

  // Response is the txid as a JSON string.
  // Strip quotes and whitespace.
  lockTxId = resp;
  if (!lockTxId.empty() && lockTxId.front() == '"') lockTxId = lockTxId.substr(1);
  if (!lockTxId.empty() && lockTxId.back()  == '"') lockTxId.pop_back();
  return lockTxId.size() == 64;
}

bool BchRpcClient::verifyLock(const std::string& htlcAddress,
                               uint64_t expectedSatoshis,
                               uint32_t minConfirms) {
  std::vector<BchUtxo> utxos;
  if (!listUnspent(htlcAddress, utxos)) return false;

  for (const auto& utxo : utxos) {
    if (utxo.satoshis >= expectedSatoshis &&
        utxo.confirmations >= minConfirms) {
      return true;
    }
  }
  return false;
}

// Helper: sign a BCH input with BIP143 sighash and return the DER+forkid signature.
static std::vector<uint8_t> signBchInput(
    const std::array<uint8_t, 32>& privKey,
    uint32_t txVersion,
    uint32_t nLocktime,
    uint32_t nSequence,
    const std::string& htlcTxid,
    uint32_t htlcVout,
    const std::vector<uint8_t>& redeemScript,
    uint64_t htlcAmount,
    const std::vector<uint8_t>& outputScript,
    uint64_t outputAmount) {

  // Convert txid hex to little-endian 32 bytes.
  auto txidBytes = BchHtlcScript::hexToBytes(htlcTxid);
  if (txidBytes.size() != 32) return {};
  // Bitcoin txids are stored/transmitted in reverse byte order (big-endian display,
  // little-endian in the wire format), so reverse here.
  std::reverse(txidBytes.begin(), txidBytes.end());
  std::array<uint8_t, 32> txidLE;
  std::copy(txidBytes.begin(), txidBytes.end(), txidLE.begin());

  CryptoNote::SwapDaemon::Crypto::Bip143Sighash bip143;
  auto sighash = bip143.computeForP2sh(
      txVersion, nLocktime, nSequence,
      txidLE, htlcVout,
      redeemScript,
      htlcAmount,
      outputScript,
      outputAmount,
      /*sighashType=*/0x41);

  CryptoNote::SwapDaemon::Crypto::Secp256k1Signer signer;
  auto sig = signer.signRecoverable(sighash, privKey);

  // Encode as DER + sighash type 0x41.
  // DER: 0x30 <len> 0x02 <rlen> <r> 0x02 <slen> <s>
  auto& r = sig.r;
  auto& s = sig.s;

  // Strip leading zeros (but keep at least one byte).
  size_t rStart = 0, sStart = 0;
  while (rStart < 31 && r[rStart] == 0) ++rStart;
  while (sStart < 31 && s[sStart] == 0) ++sStart;

  // Pad if high bit set (DER requires a leading 0x00 for positive integers).
  bool rPad = (r[rStart] & 0x80) != 0;
  bool sPad = (s[sStart] & 0x80) != 0;

  size_t rLen = 32 - rStart + (rPad ? 1 : 0);
  size_t sLen = 32 - sStart + (sPad ? 1 : 0);
  size_t seqLen = 2 + rLen + 2 + sLen;

  std::vector<uint8_t> der;
  der.push_back(0x30);
  der.push_back(static_cast<uint8_t>(seqLen));
  der.push_back(0x02);
  der.push_back(static_cast<uint8_t>(rLen));
  if (rPad) der.push_back(0x00);
  der.insert(der.end(), r.begin() + rStart, r.end());
  der.push_back(0x02);
  der.push_back(static_cast<uint8_t>(sLen));
  if (sPad) der.push_back(0x00);
  der.insert(der.end(), s.begin() + sStart, s.end());
  der.push_back(0x41);  // sighash type SIGHASH_ALL | SIGHASH_FORKID

  return der;
}

bool BchRpcClient::claim(const std::string& claimerWif,
                          const std::string& htlcTxid,
                          uint32_t htlcVout,
                          uint64_t htlcAmount,
                          const std::string& redeemScriptHex,
                          const std::string& preimageHex,
                          const std::string& destAddress,
                          std::string& claimTxId) {
  std::array<uint8_t, 32> privKey{};
  if (!wifToPrivKey(claimerWif, privKey)) return false;

  auto redeemScript = BchHtlcScript::hexToBytes(redeemScriptHex);
  auto preimage     = BchHtlcScript::hexToBytes(preimageHex);

  // Build output script (P2PKH to destAddress).
  uint8_t addrVersion = 0;
  std::vector<uint8_t> pubKeyHash;
  if (!BchHtlcScript::base58CheckDecode(destAddress, addrVersion, pubKeyHash)) return false;
  auto outputScript = BchHtlcScript::buildP2pkhScriptPubKey(pubKeyHash);

  const uint64_t fee = 1000;  // 1000 sat fee
  if (htlcAmount <= fee) return false;
  uint64_t outputAmount = htlcAmount - fee;

  // Sign.
  auto der = signBchInput(privKey, /*version=*/1, /*locktime=*/0,
                           /*nSequence=*/0xFFFFFFFF,
                           htlcTxid, htlcVout,
                           redeemScript, htlcAmount,
                           outputScript, outputAmount);
  if (der.empty()) return false;

  // Build scriptSig: <sig> <preimage> OP_1 <redeemScript>
  auto scriptSig = BchHtlcScript::createClaimScriptSig(der, preimage, redeemScript);

  // Build raw tx and broadcast.
  auto rawTx = BchHtlcScript::buildRawTransaction(
      htlcTxid, htlcVout, htlcAmount,
      scriptSig, destAddress, outputAmount, /*nLocktime=*/0);

  std::string txHex = BchHtlcScript::bytesToHex(rawTx);
  return sendRawTransaction(txHex, claimTxId);
}

bool BchRpcClient::refundHtlc(const std::string& senderWif,
                               const std::string& htlcTxid,
                               uint32_t htlcVout,
                               uint64_t htlcAmount,
                               const std::string& redeemScriptHex,
                               const std::string& destAddress,
                               std::string& refundTxId) {
  std::array<uint8_t, 32> privKey{};
  if (!wifToPrivKey(senderWif, privKey)) return false;

  auto redeemScript = BchHtlcScript::hexToBytes(redeemScriptHex);

  // Extract timeoutBlock from the redeem script.
  // In BchHtlcScript::createRedeemScript the timeout is pushed as a script
  // number at offset 36 (after OP_ELSE byte at offset 35).
  // Rather than parsing the script, we require the caller to pass it via a
  // separate parameter in future refactoring.  For now we extract it from
  // the nLocktime we set in the refund tx — the caller must set nLocktime
  // equal to the timeout.  We leave it at 0 here and note the limitation.
  // TODO(C3): add timeoutBlock to refundHtlc signature.
  uint32_t nLocktime = 0;

  // Build output.
  uint8_t addrVersion = 0;
  std::vector<uint8_t> pubKeyHash;
  if (!BchHtlcScript::base58CheckDecode(destAddress, addrVersion, pubKeyHash)) return false;
  auto outputScript = BchHtlcScript::buildP2pkhScriptPubKey(pubKeyHash);

  const uint64_t fee = 1000;
  if (htlcAmount <= fee) return false;
  uint64_t outputAmount = htlcAmount - fee;

  // Refund inputs must use nSequence < 0xFFFFFFFF for CLTV to activate.
  auto der = signBchInput(privKey, /*version=*/1, nLocktime,
                           /*nSequence=*/0xFFFFFFFE,
                           htlcTxid, htlcVout,
                           redeemScript, htlcAmount,
                           outputScript, outputAmount);
  if (der.empty()) return false;

  auto scriptSig = BchHtlcScript::createRefundScriptSig(der, redeemScript);

  auto rawTx = BchHtlcScript::buildRawTransaction(
      htlcTxid, htlcVout, htlcAmount,
      scriptSig, destAddress, outputAmount, nLocktime);

  std::string txHex = BchHtlcScript::bytesToHex(rawTx);
  return sendRawTransaction(txHex, refundTxId);
}

} // namespace XfgSwap
