#include "DcrRpcClient.h"
#include "DcrHtlcScript.h"
#include "Common/JsonValue.h"
#include "../Crypto/Secp256k1Signer.h"
#include <openssl/sha.h>
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

// Default transaction fee in atoms (10000 atoms ≈ 0.0001 DCR)
static constexpr uint64_t DCR_DEFAULT_FEE = 10000;

namespace XfgSwap {

// ---- Helpers ----------------------------------------------------------------

static void writeLE32(std::vector<uint8_t>& buf, uint32_t val) {
  buf.push_back(static_cast<uint8_t>(val));
  buf.push_back(static_cast<uint8_t>(val >> 8));
  buf.push_back(static_cast<uint8_t>(val >> 16));
  buf.push_back(static_cast<uint8_t>(val >> 24));
}

static void writeLE64(std::vector<uint8_t>& buf, uint64_t val) {
  for (int i = 0; i < 8; ++i) {
    buf.push_back(static_cast<uint8_t>((val >> (8 * i)) & 0xFF));
  }
}

static void writeCompactSize(std::vector<uint8_t>& buf, uint64_t val) {
  if (val < 0xFD) {
    buf.push_back(static_cast<uint8_t>(val));
  } else if (val <= 0xFFFF) {
    buf.push_back(0xFD);
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
  } else if (val <= 0xFFFFFFFF) {
    buf.push_back(0xFE);
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
  } else {
    buf.push_back(0xFF);
    for (int i = 0; i < 8; ++i) {
      buf.push_back(static_cast<uint8_t>((val >> (8 * i)) & 0xFF));
    }
  }
}

// DCR legacy sighash: SHA256d(tx_modified || sighash_type_LE)
// For SIGHASH_ALL: input being signed gets scriptCode, all others empty.
static std::vector<uint8_t> dcrSighash(
    uint32_t txVersion,
    const std::string& inputTxid,
    uint32_t inputVout,
    const std::vector<uint8_t>& scriptCode,
    uint32_t nSequence,
    const std::vector<std::vector<uint8_t>>& outputScripts,
    const std::vector<uint64_t>& outputAmounts,
    uint32_t nLocktime,
    uint32_t expiry) {

  std::vector<uint8_t> tx;

  // Version
  writeLE32(tx, txVersion);

  // Input count = 1
  writeCompactSize(tx, 1);

  // Input: txid (32 bytes reversed LE) + vout (4 bytes LE) + tree (0) + blockHeight (0) + blockIndex (0) + scriptCode + sequence
  auto txidBytes = DcrHtlcScript::hexToBytes(inputTxid);
  if (txidBytes.size() != 32) return {};
  std::reverse(txidBytes.begin(), txidBytes.end());
  tx.insert(tx.end(), txidBytes.begin(), txidBytes.end());
  writeLE32(tx, inputVout);
  tx.push_back(0x00);  // tree
  writeCompactSize(tx, 0);  // blockHeight (irregular, not coinbase)
  writeCompactSize(tx, 0);  // blockIndex
  writeCompactSize(tx, scriptCode.size());
  tx.insert(tx.end(), scriptCode.begin(), scriptCode.end());
  writeLE32(tx, nSequence);

  // Output count
  writeCompactSize(tx, outputScripts.size());
  for (size_t i = 0; i < outputScripts.size(); ++i) {
    writeLE64(tx, outputAmounts[i]);
    // DCR output: value (8) + version (2) + script length + script
    writeCompactSize(tx, 2);  // output version 2
    tx.pop_back();  // remove version prefix; DCR output format: value(8) + scriptPubKeyLen(varint) + scriptPubKey
    // Actually DCR output: value (8 bytes LE) + version (2 bytes LE) + script length (varint) + script
    tx.push_back(0x02);  // output version low byte
    tx.push_back(0x00);  // output version high byte
    writeCompactSize(tx, outputScripts[i].size());
    tx.insert(tx.end(), outputScripts[i].begin(), outputScripts[i].end());
  }

  // Locktime + expiry
  writeLE32(tx, nLocktime);
  writeLE32(tx, expiry);

  // SIGHASH_ALL = 0x01
  writeLE32(tx, 0x01);

  // Double SHA256
  std::vector<uint8_t> hash1(32);
  SHA256(tx.data(), tx.size(), hash1.data());
  std::vector<uint8_t> hash2(32);
  SHA256(hash1.data(), 32, hash2.data());
  return hash2;
}

// Sign a DCR P2SH input and produce a DER-encoded signature with sighash byte.
static std::vector<uint8_t> signDcrP2shInput(
    const std::array<uint8_t, 32>& privKey,
    uint32_t txVersion,
    const std::string& inputTxid,
    uint32_t inputVout,
    const std::vector<uint8_t>& redeemScript,
    uint32_t nSequence,
    const std::vector<std::vector<uint8_t>>& outputScripts,
    const std::vector<uint64_t>& outputAmounts,
    uint32_t nLocktime,
    uint32_t expiry) {

  auto sighashVec = dcrSighash(txVersion, inputTxid, inputVout,
                             redeemScript, nSequence,
                             outputScripts, outputAmounts,
                             nLocktime, expiry);
  if (sighashVec.empty() || sighashVec.size() != 32) return {};

  std::array<uint8_t, 32> sighash;
  std::memcpy(sighash.data(), sighashVec.data(), 32);

  CryptoNote::SwapDaemon::Crypto::Secp256k1Signer signer;
  auto sig = signer.signRecoverable(sighash, privKey);

  // DER encode: 0x30 <len> 0x02 <rlen> <r> 0x02 <slen> <s> <sighash_type>
  auto& r = sig.r;
  auto& s = sig.s;

  size_t rStart = 0, sStart = 0;
  while (rStart < 31 && r[rStart] == 0) ++rStart;
  while (sStart < 31 && s[sStart] == 0) ++sStart;

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
  der.push_back(0x01);  // SIGHASH_ALL

  return der;
}

// WIF decode helper (strip version byte and checksum).
static bool dcrWifToPrivKey(const std::string& wif, std::array<uint8_t, 32>& privKey) {
  auto bytes = DcrHtlcScript::hexToBytes(wif);
  if (bytes.size() == 33) {
    std::memcpy(privKey.data(), bytes.data() + 1, 32);
    return true;
  }
  if (bytes.size() == 34 && bytes.back() == 0x01) {
    std::memcpy(privKey.data(), bytes.data() + 1, 32);
    return true;
  }
  return false;
}

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

// ---- RAII socket wrapper ---------------------------------------------------

struct ScopedSocket {
  int fd;
  ScopedSocket() : fd(-1) {}
  ~ScopedSocket() { if (fd >= 0) close(fd); }
  ScopedSocket(const ScopedSocket&) = delete;
  ScopedSocket& operator=(const ScopedSocket&) = delete;
  operator bool() const { return fd >= 0; }
};

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
  ScopedSocket sock;
  sock.fd = socket(AF_INET, SOCK_STREAM, 0);
  if (sock.fd < 0) throw std::runtime_error("DcrRpcClient: socket failed");

#ifdef _WIN32
  DWORD tvMs = 10000;
  setsockopt(sock.fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tvMs), sizeof(tvMs));
  setsockopt(sock.fd, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tvMs), sizeof(tvMs));
#else
  struct timeval tv;
  tv.tv_sec = 10;
  tv.tv_usec = 0;
  setsockopt(sock.fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(sock.fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif

  struct addrinfo hints, *result;
  std::memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  std::string portStr = std::to_string(m_port);
  int gai = getaddrinfo(m_host.c_str(), portStr.c_str(), &hints, &result);
  if (gai != 0) {
    throw std::runtime_error("DcrRpcClient: getaddrinfo failed for " + m_host);
  }

  int ret = connect(sock.fd, result->ai_addr, result->ai_addrlen);
  freeaddrinfo(result);
  if (ret < 0) {
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
  int sent = send(sock.fd, request.c_str(), static_cast<int>(request.size()),
#ifndef _WIN32
                       MSG_NOSIGNAL
#else
                       0
#endif
                      );
  if (sent < 0 || static_cast<size_t>(sent) != request.size()) {
    throw std::runtime_error("DcrRpcClient: send failed");
  }

  std::string response;
  char buf[4096];
  while (true) {
    int n = recv(sock.fd, buf, sizeof(buf), 0);
    if (n <= 0) break;
    response.append(buf, static_cast<size_t>(n));
  }
  // ScopedSocket destructor closes fd

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
    // Empty address → all wallet UTXOs (funder selection). Non-empty → filter.
    // first of pair is "txid:vout" so callers can createrawtransaction correctly.
    std::string params = address.empty()
        ? "[1, 9999999]"
        : "[1, 9999999, [\"" + address + "\"]]";
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
      int64_t vout = utxo.contains("vout") ? utxo("vout").getInteger() : 0;
      uint64_t amount = 0;
      if (utxo.contains("amount")) {
        double dcr = utxo("amount").isReal() ? utxo("amount").getReal() : 0;
        amount = static_cast<uint64_t>(dcr * 1e8);
      }
      if (!txid.empty() && amount > 0) {
        utxos.push_back({txid + ":" + std::to_string(vout), amount});
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
    std::array<uint8_t, 32> privKey{};
    if (!dcrWifToPrivKey(claimerWif, privKey)) return false;

    auto redeemScript = DcrHtlcScript::hexToBytes(redeemScriptHex);
    auto preimage     = DcrHtlcScript::hexToBytes(preimageHex);

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

    const uint64_t fee = DCR_DEFAULT_FEE;
    if (htlcAmount <= fee) return false;
    uint64_t outputAmount = htlcAmount - fee;

    // Sign locally with redeemScript as scriptCode
    std::vector<std::vector<uint8_t>> outScripts = { outputScript };
    std::vector<uint64_t> outAmounts = { outputAmount };

    auto der = signDcrP2shInput(
        privKey, /*txVersion=*/1, htlcTxid, htlcVout,
        redeemScript, /*nSequence=*/0xFFFFFFFE,
        outScripts, outAmounts,
        /*nLocktime=*/0, /*expiry=*/0);
    if (der.empty()) return false;

    // Build HTLC claim scriptSig: <sig> <preimage> OP_TRUE <redeemScript>
    auto scriptSig = DcrHtlcScript::createClaimScriptSig(der, preimage, redeemScript);

    // Build raw tx and broadcast
    auto rawTx = DcrHtlcScript::buildRawTransaction(
        htlcTxid, htlcVout, htlcAmount,
        scriptSig, destAddress, outputAmount, /*lockTime=*/0);

    std::string txHex = DcrHtlcScript::bytesToHex(rawTx);
    return sendRawTransaction(txHex, claimTxId);
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
    std::array<uint8_t, 32> privKey{};
    if (!dcrWifToPrivKey(senderWif, privKey)) return false;

    auto redeemScript = DcrHtlcScript::hexToBytes(redeemScriptHex);

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

    const uint64_t fee = DCR_DEFAULT_FEE;
    if (htlcAmount <= fee) return false;
    uint64_t outputAmount = htlcAmount - fee;

    // Sign locally — refund uses nLocktime=timeoutBlock, nSequence<0xFFFFFFFF for CLTV
    std::vector<std::vector<uint8_t>> outScripts = { outputScript };
    std::vector<uint64_t> outAmounts = { outputAmount };

    auto der = signDcrP2shInput(
        privKey, /*txVersion=*/1, htlcTxid, htlcVout,
        redeemScript, /*nSequence=*/0xFFFFFFFE,
        outScripts, outAmounts,
        timeoutBlock, /*expiry=*/0);
    if (der.empty()) return false;

    // Build refund scriptSig: <sig> OP_FALSE <redeemScript>
    auto scriptSig = DcrHtlcScript::createRefundScriptSig(der, redeemScript);

    // Build raw tx — lockTime must be >= timeoutBlock for CLTV
    auto rawTx = DcrHtlcScript::buildRawTransaction(
        htlcTxid, htlcVout, htlcAmount,
        scriptSig, destAddress, outputAmount, timeoutBlock);

    std::string txHex = DcrHtlcScript::bytesToHex(rawTx);
    return sendRawTransaction(txHex, refundTxId);
  } catch (...) { return false; }
}

} // namespace XfgSwap
