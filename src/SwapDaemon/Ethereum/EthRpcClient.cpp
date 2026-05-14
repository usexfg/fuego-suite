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

#include "EthRpcClient.h"
#include "ContractAbi.h"
#include "Crypto/Secp256k1Signer.h"
#include "Crypto/RlpEncoder.h"
#include "crypto/keccak.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <iomanip>
#include <algorithm>

namespace XfgSwap {

// ---------------------------------------------------------------------------
// Hex helpers
// ---------------------------------------------------------------------------

static uint64_t hexToUint64(const std::string& hex) {
  std::string s = hex;
  if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    s = s.substr(2);
  }
  uint64_t result = 0;
  for (char c : s) {
    result <<= 4;
    if (c >= '0' && c <= '9')      result |= static_cast<uint64_t>(c - '0');
    else if (c >= 'a' && c <= 'f') result |= static_cast<uint64_t>(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') result |= static_cast<uint64_t>(c - 'A' + 10);
  }
  return result;
}

static std::string uint64ToHex(uint64_t val) {
  if (val == 0) return "0x0";
  std::ostringstream oss;
  oss << "0x" << std::hex << val;
  return oss.str();
}

// ---------------------------------------------------------------------------
// Minimal JSON value extraction helpers (no external JSON library dependency)
// ---------------------------------------------------------------------------

// Extract a string value for a given key from a flat JSON object.
// Returns empty string if not found. Handles null values.
static std::string jsonGetString(const std::string& json, const std::string& key) {
  std::string needle = "\"" + key + "\"";
  auto pos = json.find(needle);
  if (pos == std::string::npos) return "";

  // Skip past key and colon
  pos = json.find(':', pos + needle.size());
  if (pos == std::string::npos) return "";
  ++pos;

  // Skip whitespace
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                                json[pos] == '\n' || json[pos] == '\r')) {
    ++pos;
  }

  if (pos >= json.size()) return "";

  // Handle null
  if (json.compare(pos, 4, "null") == 0) return "";

  // Must be a quoted string
  if (json[pos] != '"') return "";
  ++pos;

  std::string result;
  while (pos < json.size() && json[pos] != '"') {
    if (json[pos] == '\\' && pos + 1 < json.size()) {
      ++pos; // skip escape
    }
    result += json[pos];
    ++pos;
  }
  return result;
}

// Check if a JSON object has an "error" field that is non-null
static bool jsonHasError(const std::string& json) {
  std::string needle = "\"error\"";
  auto pos = json.find(needle);
  if (pos == std::string::npos) return false;

  pos = json.find(':', pos + needle.size());
  if (pos == std::string::npos) return false;
  ++pos;

  // Skip whitespace
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                                json[pos] == '\n' || json[pos] == '\r')) {
    ++pos;
  }

  // null means no error
  if (pos + 4 <= json.size() && json.compare(pos, 4, "null") == 0) return false;
  return true;
}

// Extract "result" from top-level JSON-RPC response.
// For string results, strips quotes. For object results, returns the raw substring.
static std::string jsonGetResult(const std::string& json) {
  std::string needle = "\"result\"";
  auto pos = json.find(needle);
  if (pos == std::string::npos) return "";

  pos = json.find(':', pos + needle.size());
  if (pos == std::string::npos) return "";
  ++pos;

  // Skip whitespace
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                                json[pos] == '\n' || json[pos] == '\r')) {
    ++pos;
  }

  if (pos >= json.size()) return "";

  // null
  if (json.compare(pos, 4, "null") == 0) return "";

  // Quoted string
  if (json[pos] == '"') {
    ++pos;
    std::string result;
    while (pos < json.size() && json[pos] != '"') {
      if (json[pos] == '\\' && pos + 1 < json.size()) {
        ++pos;
      }
      result += json[pos];
      ++pos;
    }
    return result;
  }

  // Object or array — find matching brace/bracket
  if (json[pos] == '{' || json[pos] == '[') {
    char open = json[pos];
    char close = (open == '{') ? '}' : ']';
    int depth = 1;
    size_t start = pos;
    ++pos;
    bool inStr = false;
    while (pos < json.size() && depth > 0) {
      if (json[pos] == '\\' && inStr) {
        ++pos; // skip escaped char
      } else if (json[pos] == '"') {
        inStr = !inStr;
      } else if (!inStr) {
        if (json[pos] == open)  ++depth;
        if (json[pos] == close) --depth;
      }
      ++pos;
    }
    return json.substr(start, pos - start);
  }

  // Number or boolean — read until delimiter
  {
    size_t start = pos;
    while (pos < json.size() && json[pos] != ',' && json[pos] != '}' &&
           json[pos] != ']' && json[pos] != ' ' && json[pos] != '\n') {
      ++pos;
    }
    return json.substr(start, pos - start);
  }
}

// ---------------------------------------------------------------------------
// EthRpcClient
// ---------------------------------------------------------------------------

// ─── hex utilities ─────────────────────────────────────────────────────────

static std::vector<uint8_t> hexToBytes(const std::string& hex) {
  std::string s = hex;
  if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s = s.substr(2);
  if (s.size() % 2 != 0) s = "0" + s;
  std::vector<uint8_t> out(s.size() / 2);
  for (size_t i = 0; i < out.size(); ++i) {
    unsigned b;
    sscanf(s.c_str() + i * 2, "%02x", &b);
    out[i] = static_cast<uint8_t>(b);
  }
  return out;
}

static std::string bytesToHex(const uint8_t* data, size_t len, bool prefix = true) {
  std::string s = prefix ? "0x" : "";
  char buf[3];
  for (size_t i = 0; i < len; ++i) {
    snprintf(buf, 3, "%02x", data[i]);
    s += buf;
  }
  return s;
}

static std::string bytesToHex(const std::vector<uint8_t>& v, bool prefix = true) {
  return bytesToHex(v.data(), v.size(), prefix);
}

// ─── EthRpcClient constructors ──────────────────────────────────────────────

EthRpcClient::EthRpcClient(const std::string& host, uint16_t port)
  : m_host(host), m_port(port) {
  m_privKey.fill(0);
}

EthRpcClient::EthRpcClient(const std::string& host, uint16_t port,
                            const std::string& privKeyHex,
                            const std::string& signerAddress,
                            uint64_t chainId)
  : m_host(host), m_port(port),
    m_signerAddress(signerAddress), m_chainId(chainId), m_hasSigner(true) {
  auto keyBytes = hexToBytes(privKeyHex);
  if (keyBytes.size() != 32) {
    throw std::invalid_argument("EthRpcClient: privKeyHex must be 32 bytes (64 hex chars)");
  }
  std::copy(keyBytes.begin(), keyBytes.end(), m_privKey.begin());
}

// ---------------------------------------------------------------------------
// HTTP POST over POSIX sockets
// ---------------------------------------------------------------------------

std::string EthRpcClient::httpPost(const std::string& path, const std::string& body) {
  // Resolve host
  struct addrinfo hints{}, *res = nullptr;
  hints.ai_family   = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  std::string portStr = std::to_string(m_port);
  int gaiRet = getaddrinfo(m_host.c_str(), portStr.c_str(), &hints, &res);
  if (gaiRet != 0 || !res) {
    return "";
  }

  int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (sock < 0) {
    freeaddrinfo(res);
    return "";
  }

  // Set a 10-second timeout on send/recv
  struct timeval tv;
  tv.tv_sec  = 10;
  tv.tv_usec = 0;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
    close(sock);
    freeaddrinfo(res);
    return "";
  }
  freeaddrinfo(res);

  // Build HTTP request
  std::ostringstream req;
  req << "POST " << path << " HTTP/1.1\r\n";
  req << "Host: " << m_host << ":" << m_port << "\r\n";
  req << "Content-Type: application/json\r\n";
  req << "Content-Length: " << body.size() << "\r\n";
  req << "Connection: close\r\n";
  req << "\r\n";
  req << body;

  std::string request = req.str();
  ssize_t sent = send(sock, request.data(), request.size(), 0);
  if (sent < 0 || static_cast<size_t>(sent) != request.size()) {
    close(sock);
    return "";
  }

  // Read response
  std::string response;
  char buf[4096];
  while (true) {
    ssize_t n = recv(sock, buf, sizeof(buf), 0);
    if (n <= 0) break;
    response.append(buf, static_cast<size_t>(n));
  }
  close(sock);

  // Strip HTTP headers — body starts after \r\n\r\n
  auto headerEnd = response.find("\r\n\r\n");
  if (headerEnd == std::string::npos) return "";
  return response.substr(headerEnd + 4);
}

// ---------------------------------------------------------------------------
// JSON-RPC wrapper
// ---------------------------------------------------------------------------

std::string EthRpcClient::jsonRpc(const std::string& method, const std::string& params) {
  static std::atomic<int> requestId{1};

  std::ostringstream body;
  body << "{\"jsonrpc\":\"2.0\",\"method\":\"" << method
       << "\",\"params\":" << params
       << ",\"id\":" << requestId++ << "}";

  return httpPost("/", body.str());
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool EthRpcClient::getBlockNumber(uint64_t& blockNum) {
  std::string resp = jsonRpc("eth_blockNumber", "[]");
  if (resp.empty() || jsonHasError(resp)) return false;

  std::string result = jsonGetResult(resp);
  if (result.empty()) return false;

  blockNum = hexToUint64(result);
  return true;
}

bool EthRpcClient::getBalance(const std::string& address, uint64_t& balanceWei) {
  std::string params = "[\"" + address + "\",\"latest\"]";
  std::string resp = jsonRpc("eth_getBalance", params);
  if (resp.empty() || jsonHasError(resp)) return false;

  std::string result = jsonGetResult(resp);
  if (result.empty()) return false;

  balanceWei = hexToUint64(result);
  return true;
}

bool EthRpcClient::getTransactionReceipt(const std::string& txHash, EthTxReceipt& receipt) {
  std::string params = "[\"" + txHash + "\"]";
  std::string resp = jsonRpc("eth_getTransactionReceipt", params);
  if (resp.empty() || jsonHasError(resp)) return false;

  std::string result = jsonGetResult(resp);
  if (result.empty()) return false;

  receipt.txHash          = jsonGetString(result, "transactionHash");
  receipt.contractAddress = jsonGetString(result, "contractAddress");
  receipt.gasUsed         = hexToUint64(jsonGetString(result, "gasUsed"));
  receipt.blockNumber     = hexToUint64(jsonGetString(result, "blockNumber"));

  std::string status = jsonGetString(result, "status");
  receipt.success = (status == "0x1" || status == "0x01");

  return true;
}

bool EthRpcClient::getNonce(const std::string& address, uint64_t& nonce) {
  std::string params = "[\"" + address + "\",\"pending\"]";
  std::string resp = jsonRpc("eth_getTransactionCount", params);
  if (resp.empty() || jsonHasError(resp)) return false;
  std::string result = jsonGetResult(resp);
  if (result.empty()) return false;
  nonce = hexToUint64(result);
  return true;
}

// ─── EIP-155 signing internals ──────────────────────────────────────────────

std::vector<uint8_t> EthRpcClient::buildSignedTx(uint64_t nonce,
                                                   uint64_t gasPriceWei,
                                                   uint64_t gasLimit,
                                                   const std::vector<uint8_t>& to,
                                                   uint64_t valueWei,
                                                   const std::vector<uint8_t>& data) {
  if (!m_hasSigner) {
    throw std::runtime_error("EthRpcClient::buildSignedTx: no signer configured");
  }

  using namespace CryptoNote::SwapDaemon::Crypto;

  // Step 1: RLP-encode the pre-sign payload: [nonce, gasPrice, gasLimit, to, value, data, chainId, 0, 0]
  RlpEncoder preSig;
  preSig.beginList();
  preSig.writeUint(nonce);
  preSig.writeUint(gasPriceWei);
  preSig.writeUint(gasLimit);
  preSig.writeBytes(to);            // empty = contract deploy
  preSig.writeUint(valueWei);
  preSig.writeBytes(data);
  preSig.writeUint(m_chainId);      // EIP-155: chain ID
  preSig.writeBytes({});            // r = 0
  preSig.writeBytes({});            // s = 0
  preSig.endList();
  auto preSigBytes = preSig.finalize();

  // Step 2: keccak256 of the pre-sign encoding
  std::array<uint8_t, 32> msgHash;
  keccak(preSigBytes.data(), static_cast<int>(preSigBytes.size()), msgHash.data(), 32);

  // Step 3: sign with secp256k1
  Secp256k1Signer signer;
  auto sig = signer.signRecoverable(msgHash, m_privKey);

  // Step 4: EIP-155 v = chainId * 2 + 35 + recid
  uint64_t v = m_chainId * 2 + 35 + sig.recid;

  // Step 5: RLP-encode the signed transaction: [nonce, gasPrice, gasLimit, to, value, data, v, r, s]
  RlpEncoder signedEnc;
  signedEnc.beginList();
  signedEnc.writeUint(nonce);
  signedEnc.writeUint(gasPriceWei);
  signedEnc.writeUint(gasLimit);
  signedEnc.writeBytes(to);
  signedEnc.writeUint(valueWei);
  signedEnc.writeBytes(data);
  signedEnc.writeUint(v);
  signedEnc.writeBytes(sig.r.data(), sig.r.size());
  signedEnc.writeBytes(sig.s.data(), sig.s.size());
  signedEnc.endList();
  return signedEnc.finalize();
}

bool EthRpcClient::signAndSend(const std::vector<uint8_t>& to,
                                const std::vector<uint8_t>& data,
                                uint64_t valueWei,
                                uint64_t gasLimit,
                                std::string& txHash) {
  if (!m_hasSigner) {
    throw std::runtime_error("EthRpcClient::signAndSend: no signer configured — "
                             "construct with EthRpcClient(host, port, privKeyHex, address, chainId)");
  }

  // Fetch current nonce for the signer address.
  uint64_t nonce = 0;
  if (!getNonce(m_signerAddress, nonce)) {
    return false;
  }

  // Use a fixed gas price of 20 gwei for now; a real implementation would
  // call eth_gasPrice to fetch the current suggested price.
  const uint64_t gasPriceWei = 20000000000ULL;  // 20 gwei

  auto rawTx = buildSignedTx(nonce, gasPriceWei, gasLimit, to, valueWei, data);
  std::string rawHex = bytesToHex(rawTx);
  return sendRawTransaction(rawHex, txHash);
}

bool EthRpcClient::deployContract(const std::string& /*fromAddress*/,
                                  const std::string& bytecode,
                                  uint64_t gasLimit,
                                  std::string& txHash) {
  auto bytecodeBytes = hexToBytes(bytecode);
  return signAndSend(/*to=*/{}, bytecodeBytes, /*valueWei=*/0, gasLimit, txHash);
}

bool EthRpcClient::sendTransaction(const std::string& /*from*/, const std::string& to,
                                   const std::string& data, uint64_t value,
                                   uint64_t gasLimit, std::string& txHash) {
  auto toBytes   = hexToBytes(to);
  auto dataBytes = hexToBytes(data);
  return signAndSend(toBytes, dataBytes, value, gasLimit, txHash);
}

bool EthRpcClient::callContract(const std::string& to, const std::string& data,
                                std::string& result) {
  std::ostringstream params;
  params << "[{\"to\":\"" << to
         << "\",\"data\":\"" << data
         << "\"},\"latest\"]";

  std::string resp = jsonRpc("eth_call", params.str());
  if (resp.empty() || jsonHasError(resp)) return false;

  result = jsonGetResult(resp);
  return !result.empty();
}

// ─── HTLC operations (stubs — EIP-155 signing not yet implemented) ──────────

bool EthRpcClient::deployHtlc(const std::string& /*fromAddress*/,
                               const std::string& recipientAddress,
                               const std::string& hashLockHex,
                               uint64_t timeoutBlock,
                               uint64_t valueWei,
                               std::string& contractAddress) {
  // HTLC bytecode must be pre-loaded via setHtlcBytecode().
  if (m_htlcBytecode.empty()) {
    throw std::runtime_error("EthRpcClient::deployHtlc: HTLC bytecode not configured — "
                             "call setHtlcBytecode() with the HashedTimelock .bin contents");
  }

  // Build deploy calldata: bytecode || ABI-encoded constructor args.
  // HashedTimelock constructor: (address recipient, bytes32 hashLock, uint256 timeoutBlock).
  // No function selector for constructors — just the three ABI-encoded params.
  auto hashBytes = hexToBytes(hashLockHex);
  if (hashBytes.size() != 32) return false;

  std::string ctorArgs =
      EthAbi::padAddress(recipientAddress) +
      bytesToHex(hashBytes.data(), 32, /*prefix=*/false) +
      EthAbi::encodeUint256(timeoutBlock);

  // Concatenate bytecode + ctor args (both hex, no 0x prefix) into raw bytes.
  auto deployData = hexToBytes(m_htlcBytecode + ctorArgs);

  // Build nonce + signed tx manually so we can include the ETH value.
  uint64_t nonce = 0;
  if (!getNonce(m_signerAddress, nonce)) return false;

  const uint64_t gasPriceWei = 20000000000ULL;  // 20 gwei
  auto rawTx = buildSignedTx(nonce, gasPriceWei, /*gasLimit=*/800000,
                              /*to=*/{}, valueWei, deployData);
  std::string txHash;
  std::string rawHex = bytesToHex(rawTx);
  if (!sendRawTransaction(rawHex, txHash)) return false;

  // Poll for receipt to get the contract address.
  EthTxReceipt receipt;
  for (int i = 0; i < 60; ++i) {
    if (getTransactionReceipt(txHash, receipt)) break;
  }
  contractAddress = receipt.contractAddress;
  return !contractAddress.empty();
}

bool EthRpcClient::verifyLock(const std::string& contractAddress,
                               uint64_t expectedWei,
                               uint64_t minConfirmBlocks) {
  // Read-only check: call eth_getBalance on the HTLC contract address.
  // This does not require signing and works without secp256k1.
  uint64_t balance = 0;
  if (!getBalance(contractAddress, balance)) return false;
  if (balance < expectedWei) return false;

  // Optionally check confirmations via eth_getCode (contract must exist)
  if (minConfirmBlocks > 0) {
    std::string result;
    std::string params = "[\"" + contractAddress + "\",\"latest\"]";
    std::string resp = jsonRpc("eth_getCode", params);
    if (resp.empty()) return false;
  }
  return true;
}

bool EthRpcClient::claimHtlc(const std::string& /*fromAddress*/,
                               const std::string& contractAddress,
                               const std::string& preimageHex,
                               std::string& claimTxHash) {
  // ABI-encode claim(contractId, preimage).
  // The contractId in the HashedTimelock contract is the hash of the lock params;
  // here we use the contractAddress itself as the identifier (as stored in SwapParams).
  Crypto::Hash preimage;
  auto preimageBytes = hexToBytes(preimageHex);
  if (preimageBytes.size() != 32) return false;
  std::copy(preimageBytes.begin(), preimageBytes.end(),
            reinterpret_cast<uint8_t*>(&preimage));

  std::string calldata = EthAbi::encodeClaim(contractAddress, preimage);
  auto toBytes   = hexToBytes(contractAddress);
  auto dataBytes = hexToBytes(calldata.substr(2));  // strip 0x
  return signAndSend(toBytes, dataBytes, /*valueWei=*/0, /*gasLimit=*/150000, claimTxHash);
}

bool EthRpcClient::refundHtlc(const std::string& /*fromAddress*/,
                                const std::string& contractAddress,
                                std::string& refundTxHash) {
  // ABI-encode refund(contractId).
  std::string calldata = EthAbi::encodeRefund(contractAddress);
  auto toBytes   = hexToBytes(contractAddress);
  auto dataBytes = hexToBytes(calldata.substr(2));  // strip 0x
  return signAndSend(toBytes, dataBytes, /*valueWei=*/0, /*gasLimit=*/80000, refundTxHash);
}

bool EthRpcClient::sendRawTransaction(const std::string& signedTxHex, std::string& txHash) {
  std::string params = "[\"" + signedTxHex + "\"]";
  std::string resp = jsonRpc("eth_sendRawTransaction", params);
  if (resp.empty() || jsonHasError(resp)) return false;

  txHash = jsonGetResult(resp);
  return !txHash.empty();
}

} // namespace XfgSwap
