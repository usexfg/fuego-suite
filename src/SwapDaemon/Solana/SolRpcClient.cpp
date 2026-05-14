// Copyright (c) 2017-2026 Fuego Developers
// Copyright (c) 2020-2026 Elderfire Privacy Group
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

#include "SolRpcClient.h"

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
#include <chrono>
#include <thread>
#include <algorithm>
#include <cassert>

#include <openssl/sha.h>
#include "../../crypto/musig2.h"

extern "C" {
#include "../../crypto/crypto-ops.h"
}

namespace XfgSwap {

// ---------------------------------------------------------------------------
// Base58 encode/decode (Bitcoin alphabet)
// ---------------------------------------------------------------------------

static const char BASE58_ALPHABET[] =
    "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

static const int8_t BASE58_MAP[256] = {
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1, 0, 1, 2, 3, 4, 5, 6, 7, 8,-1,-1,-1,-1,-1,-1, // '1'..'9'
  -1, 9,10,11,12,13,14,15,16,-1,17,18,19,20,21,-1,   // 'A'..'N' (no 'I')
  22,23,24,25,26,27,28,29,30,31,32,-1,-1,-1,-1,-1,   // 'P'..'Z'
  -1,33,34,35,36,37,38,39,40,41,42,43,-1,44,45,46,   // 'a'..'m' (no 'l')
  47,48,49,50,51,52,53,54,55,56,57,-1,-1,-1,-1,-1,   // 'n'..'z'
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
};

static std::vector<uint8_t> base58Decode(const std::string& encoded) {
  if (encoded.empty()) return {};

  // Count leading '1's (leading zero bytes)
  size_t leadingZeros = 0;
  while (leadingZeros < encoded.size() && encoded[leadingZeros] == '1')
    ++leadingZeros;

  // Allocate enough space: log(58)/log(256) ~ 0.733
  size_t maxBytes = encoded.size() * 733 / 1000 + 1;
  std::vector<uint8_t> buf(maxBytes, 0);

  for (size_t i = 0; i < encoded.size(); ++i) {
    int8_t val = BASE58_MAP[static_cast<uint8_t>(encoded[i])];
    if (val < 0) return {};  // invalid character

    int carry = val;
    for (int j = static_cast<int>(maxBytes) - 1; j >= 0; --j) {
      carry += 58 * buf[static_cast<size_t>(j)];
      buf[static_cast<size_t>(j)] = static_cast<uint8_t>(carry % 256);
      carry /= 256;
    }
  }

  // Skip leading zeros in buffer
  size_t skip = 0;
  while (skip < maxBytes && buf[skip] == 0) ++skip;

  std::vector<uint8_t> result;
  result.reserve(leadingZeros + maxBytes - skip);
  result.resize(leadingZeros, 0);
  result.insert(result.end(), buf.begin() + static_cast<long>(skip), buf.end());
  return result;
}

static std::string base58Encode(const std::vector<uint8_t>& data) {
  if (data.empty()) return "";

  // Count leading zero bytes
  size_t leadingZeros = 0;
  while (leadingZeros < data.size() && data[leadingZeros] == 0)
    ++leadingZeros;

  // Allocate enough space: log(256)/log(58) ~ 1.366
  size_t maxChars = data.size() * 138 / 100 + 1;
  std::vector<uint8_t> buf(maxChars, 0);

  for (size_t i = 0; i < data.size(); ++i) {
    int carry = data[i];
    for (int j = static_cast<int>(maxChars) - 1; j >= 0; --j) {
      carry += 256 * buf[static_cast<size_t>(j)];
      buf[static_cast<size_t>(j)] = static_cast<uint8_t>(carry % 58);
      carry /= 58;
    }
  }

  // Skip leading zeros in buffer
  size_t skip = 0;
  while (skip < maxChars && buf[skip] == 0) ++skip;

  std::string result;
  result.reserve(leadingZeros + maxChars - skip);
  result.append(leadingZeros, '1');
  for (size_t i = skip; i < maxChars; ++i) {
    result += BASE58_ALPHABET[buf[i]];
  }
  return result;
}

// ---------------------------------------------------------------------------
// Compact-u16 encoding (Solana wire format)
// ---------------------------------------------------------------------------

static std::vector<uint8_t> compactU16Encode(uint16_t val) {
  std::vector<uint8_t> out;
  if (val <= 0x7F) {
    out.push_back(static_cast<uint8_t>(val));
  } else if (val <= 0x3FFF) {
    out.push_back(static_cast<uint8_t>((val & 0x7F) | 0x80));
    out.push_back(static_cast<uint8_t>(val >> 7));
  } else {
    out.push_back(static_cast<uint8_t>((val & 0x7F) | 0x80));
    out.push_back(static_cast<uint8_t>(((val >> 7) & 0x7F) | 0x80));
    out.push_back(static_cast<uint8_t>(val >> 14));
  }
  return out;
}

// ---------------------------------------------------------------------------
// SHA-256 using OpenSSL
// ---------------------------------------------------------------------------

static std::vector<uint8_t> sha256Hash(const std::vector<uint8_t>& data) {
  std::vector<uint8_t> hash(SHA256_DIGEST_LENGTH);
  SHA256(data.data(), data.size(), hash.data());
  return hash;
}

static std::vector<uint8_t> sha256Hash(const uint8_t* data, size_t len) {
  std::vector<uint8_t> hash(SHA256_DIGEST_LENGTH);
  SHA256(data, len, hash.data());
  return hash;
}

// ---------------------------------------------------------------------------
// Ed25519 on-curve check (returns true if point IS on the curve)
// ---------------------------------------------------------------------------

static bool isOnCurve(const uint8_t point[32]) {
  ge_p3 p;
  return ge_frombytes_vartime(&p, point) == 0;
}

// ---------------------------------------------------------------------------
// PDA derivation: find_program_address(seeds, program_id)
// ---------------------------------------------------------------------------
//
// SHA-256(seed1 || seed2 || ... || program_id || "ProgramDerivedAddress")
// Try bump from 255 down to 0. The first result that is NOT on the Ed25519
// curve is the PDA.

static std::pair<std::vector<uint8_t>, uint8_t> derivePDA(
    const std::vector<std::vector<uint8_t>>& seeds,
    const std::vector<uint8_t>& programId) {

  for (int bump = 255; bump >= 0; --bump) {
    // Build preimage: seeds || [bump] || program_id || "ProgramDerivedAddress"
    std::vector<uint8_t> preimage;
    for (const auto& seed : seeds) {
      preimage.insert(preimage.end(), seed.begin(), seed.end());
    }
    uint8_t bumpByte = static_cast<uint8_t>(bump);
    preimage.push_back(bumpByte);
    preimage.insert(preimage.end(), programId.begin(), programId.end());

    const char* suffix = "ProgramDerivedAddress";
    preimage.insert(preimage.end(), suffix, suffix + 21);

    std::vector<uint8_t> hash = sha256Hash(preimage);

    // PDA must NOT be on the Ed25519 curve
    if (!isOnCurve(hash.data())) {
      return {hash, bumpByte};
    }
  }

  // Should never happen in practice
  return {{}, 0};
}

// ---------------------------------------------------------------------------
// Ed25519 signing (NaCl-compatible, for Solana transaction signing)
// ---------------------------------------------------------------------------
//
// Solana keypairs are 64 bytes: [32-byte seed][32-byte pubkey].
// Signing uses the standard Ed25519 scheme from the seed:
//   1. SHA-512(seed) → 64 bytes, first 32 = expanded secret (clamped)
//   2. SHA-512(expanded_secret_high_32 || message) → nonce_hash, reduce mod L → r
//   3. R = r * B (basepoint), encode R
//   4. SHA-512(R || pubkey || message) → h, reduce mod L
//   5. s = r + h * a  (mod L)
//   6. Signature = R || s (64 bytes)

static void ed25519Sign(const uint8_t* message, size_t msgLen,
                         const uint8_t seed[32], const uint8_t pubkey[32],
                         uint8_t signature[64]) {
  // Step 1: expand seed with SHA-512
  uint8_t az[64];
  SHA512(seed, 32, az);
  az[0]  &= 248;
  az[31] &= 63;
  az[31] |= 64;

  // Step 2: SHA-512(az[32..63] || message) → nonce
  SHA512_CTX ctx;
  uint8_t nonceHash[64];
  SHA512_Init(&ctx);
  SHA512_Update(&ctx, az + 32, 32);
  SHA512_Update(&ctx, message, msgLen);
  SHA512_Final(nonceHash, &ctx);

  // Reduce nonce mod L to get scalar r
  uint8_t r[64];
  memcpy(r, nonceHash, 64);
  sc_reduce(r);  // reduces 64-byte input mod L, result in first 32 bytes

  // Step 3: R = r * B
  ge_p3 R_point;
  ge_scalarmult_base(&R_point, r);
  uint8_t R_bytes[32];
  ge_p3_tobytes(R_bytes, &R_point);
  memcpy(signature, R_bytes, 32);

  // Step 4: SHA-512(R || pubkey || message) → hram
  uint8_t hramHash[64];
  SHA512_Init(&ctx);
  SHA512_Update(&ctx, R_bytes, 32);
  SHA512_Update(&ctx, pubkey, 32);
  SHA512_Update(&ctx, message, msgLen);
  SHA512_Final(hramHash, &ctx);

  uint8_t hram[64];
  memcpy(hram, hramHash, 64);
  sc_reduce(hram);

  // Step 5: s = r + hram * a (mod L)
  // Compute via: s = r - (-hram * az)
  //   sc_mulsub(neg_prod, hram, az, zero) => 0 - hram*az = -hram*az
  //   sc_sub(s, r, neg_prod)              => r - (-hram*az) = r + hram*az
  uint8_t zero[32] = {};
  uint8_t neg_prod[32];
  sc_mulsub(neg_prod, hram, az, zero);
  uint8_t s[32];
  sc_sub(s, r, neg_prod);
  memcpy(signature + 32, s, 32);
}

// ---------------------------------------------------------------------------
// Anchor instruction discriminator
// ---------------------------------------------------------------------------

static std::vector<uint8_t> anchorDiscriminator(const std::string& namespacedName) {
  std::vector<uint8_t> input(namespacedName.begin(), namespacedName.end());
  std::vector<uint8_t> hash = sha256Hash(input);
  return std::vector<uint8_t>(hash.begin(), hash.begin() + 8);
}

// ---------------------------------------------------------------------------
// Helper: append little-endian u64
// ---------------------------------------------------------------------------

static void appendU64LE(std::vector<uint8_t>& out, uint64_t val) {
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<uint8_t>(val & 0xFF));
    val >>= 8;
  }
}

// ---------------------------------------------------------------------------
// System program ID (all zeros except last byte = 0x00, i.e., 11111111...)
// ---------------------------------------------------------------------------

static const std::vector<uint8_t> SYSTEM_PROGRAM_ID(32, 0);

// ---------------------------------------------------------------------------
// Minimal JSON helpers (no external dependency)
// ---------------------------------------------------------------------------

static std::string jsonGetString(const std::string& json, const std::string& key) {
  std::string needle = "\"" + key + "\"";
  auto pos = json.find(needle);
  if (pos == std::string::npos) return "";

  pos = json.find(':', pos + needle.size());
  if (pos == std::string::npos) return "";
  ++pos;

  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                                json[pos] == '\n' || json[pos] == '\r'))
    ++pos;

  if (pos >= json.size()) return "";
  if (json.compare(pos, 4, "null") == 0) return "";

  if (json[pos] != '"') return "";
  ++pos;

  std::string result;
  while (pos < json.size() && json[pos] != '"') {
    if (json[pos] == '\\' && pos + 1 < json.size()) ++pos;
    result += json[pos];
    ++pos;
  }
  return result;
}

static uint64_t jsonGetUint64(const std::string& json, const std::string& key) {
  std::string needle = "\"" + key + "\"";
  auto pos = json.find(needle);
  if (pos == std::string::npos) return 0;

  pos = json.find(':', pos + needle.size());
  if (pos == std::string::npos) return 0;
  ++pos;

  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                                json[pos] == '\n' || json[pos] == '\r'))
    ++pos;

  if (pos >= json.size()) return 0;

  std::string num;
  while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
    num += json[pos];
    ++pos;
  }
  return num.empty() ? 0 : std::stoull(num);
}

static bool jsonGetBool(const std::string& json, const std::string& key) {
  std::string needle = "\"" + key + "\"";
  auto pos = json.find(needle);
  if (pos == std::string::npos) return false;

  pos = json.find(':', pos + needle.size());
  if (pos == std::string::npos) return false;
  ++pos;

  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                                json[pos] == '\n' || json[pos] == '\r'))
    ++pos;

  return (pos + 4 <= json.size() && json.compare(pos, 4, "true") == 0);
}

static bool jsonHasError(const std::string& json) {
  std::string needle = "\"error\"";
  auto pos = json.find(needle);
  if (pos == std::string::npos) return false;

  pos = json.find(':', pos + needle.size());
  if (pos == std::string::npos) return false;
  ++pos;

  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                                json[pos] == '\n' || json[pos] == '\r'))
    ++pos;

  if (pos + 4 <= json.size() && json.compare(pos, 4, "null") == 0) return false;
  return true;
}

static std::string jsonGetResult(const std::string& json) {
  std::string needle = "\"result\"";
  auto pos = json.find(needle);
  if (pos == std::string::npos) return "";

  pos = json.find(':', pos + needle.size());
  if (pos == std::string::npos) return "";
  ++pos;

  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                                json[pos] == '\n' || json[pos] == '\r'))
    ++pos;

  if (pos >= json.size()) return "";
  if (json.compare(pos, 4, "null") == 0) return "";

  if (json[pos] == '"') {
    ++pos;
    std::string result;
    while (pos < json.size() && json[pos] != '"') {
      if (json[pos] == '\\' && pos + 1 < json.size()) ++pos;
      result += json[pos];
      ++pos;
    }
    return result;
  }

  if (json[pos] == '{' || json[pos] == '[') {
    char open = json[pos];
    char close = (open == '{') ? '}' : ']';
    int depth = 1;
    size_t start = pos;
    ++pos;
    bool inStr = false;
    while (pos < json.size() && depth > 0) {
      if (json[pos] == '\\' && inStr) { ++pos; }
      else if (json[pos] == '"') { inStr = !inStr; }
      else if (!inStr) {
        if (json[pos] == open)  ++depth;
        if (json[pos] == close) --depth;
      }
      ++pos;
    }
    return json.substr(start, pos - start);
  }

  // Number or boolean
  size_t start = pos;
  while (pos < json.size() && json[pos] != ',' && json[pos] != '}' &&
         json[pos] != ']' && json[pos] != ' ' && json[pos] != '\n')
    ++pos;
  return json.substr(start, pos - start);
}

// ---------------------------------------------------------------------------
// Hex helpers
// ---------------------------------------------------------------------------

std::vector<uint8_t> SolRpcClient::hexToBytes(const std::string& hex) {
  std::vector<uint8_t> bytes;
  std::string h = hex;
  if (h.size() >= 2 && h[0] == '0' && (h[1] == 'x' || h[1] == 'X'))
    h = h.substr(2);
  if (h.size() % 2 != 0) h = "0" + h;
  bytes.reserve(h.size() / 2);
  for (size_t i = 0; i < h.size(); i += 2) {
    uint8_t hi = 0, lo = 0;
    char c = h[i];
    if (c >= '0' && c <= '9')      hi = static_cast<uint8_t>(c - '0');
    else if (c >= 'a' && c <= 'f') hi = static_cast<uint8_t>(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') hi = static_cast<uint8_t>(c - 'A' + 10);
    c = h[i + 1];
    if (c >= '0' && c <= '9')      lo = static_cast<uint8_t>(c - '0');
    else if (c >= 'a' && c <= 'f') lo = static_cast<uint8_t>(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') lo = static_cast<uint8_t>(c - 'A' + 10);
    bytes.push_back(static_cast<uint8_t>((hi << 4) | lo));
  }
  return bytes;
}

std::string SolRpcClient::bytesToHex(const std::vector<uint8_t>& bytes) {
  std::ostringstream oss;
  for (uint8_t b : bytes) {
    oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(b);
  }
  return oss.str();
}

// ---------------------------------------------------------------------------
// Base64 helpers
// ---------------------------------------------------------------------------

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string SolRpcClient::base64Encode(const std::vector<uint8_t>& data) {
  std::string out;
  out.reserve(((data.size() + 2) / 3) * 4);
  for (size_t i = 0; i < data.size(); i += 3) {
    uint32_t n = static_cast<uint32_t>(data[i]) << 16;
    if (i + 1 < data.size()) n |= static_cast<uint32_t>(data[i + 1]) << 8;
    if (i + 2 < data.size()) n |= static_cast<uint32_t>(data[i + 2]);

    out += b64_table[(n >> 18) & 0x3F];
    out += b64_table[(n >> 12) & 0x3F];
    out += (i + 1 < data.size()) ? b64_table[(n >> 6) & 0x3F] : '=';
    out += (i + 2 < data.size()) ? b64_table[n & 0x3F] : '=';
  }
  return out;
}

static uint8_t b64_decode_char(char c) {
  if (c >= 'A' && c <= 'Z') return static_cast<uint8_t>(c - 'A');
  if (c >= 'a' && c <= 'z') return static_cast<uint8_t>(c - 'a' + 26);
  if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0' + 52);
  if (c == '+') return 62;
  if (c == '/') return 63;
  return 0;
}

std::vector<uint8_t> SolRpcClient::base64Decode(const std::string& encoded) {
  std::vector<uint8_t> out;
  if (encoded.size() % 4 != 0) return out;
  out.reserve((encoded.size() / 4) * 3);

  for (size_t i = 0; i < encoded.size(); i += 4) {
    uint32_t n = (static_cast<uint32_t>(b64_decode_char(encoded[i])) << 18) |
                 (static_cast<uint32_t>(b64_decode_char(encoded[i + 1])) << 12) |
                 (static_cast<uint32_t>(b64_decode_char(encoded[i + 2])) << 6) |
                 static_cast<uint32_t>(b64_decode_char(encoded[i + 3]));
    out.push_back(static_cast<uint8_t>((n >> 16) & 0xFF));
    if (encoded[i + 2] != '=') out.push_back(static_cast<uint8_t>((n >> 8) & 0xFF));
    if (encoded[i + 3] != '=') out.push_back(static_cast<uint8_t>(n & 0xFF));
  }
  return out;
}

// ---------------------------------------------------------------------------
// SolRpcClient
// ---------------------------------------------------------------------------

SolRpcClient::SolRpcClient(const std::string& host, uint16_t port,
                           const std::string& programId)
  : m_host(host), m_port(port), m_programId(programId) {
  m_rpcUrl = m_host + ":" + std::to_string(m_port);
}

// ---------------------------------------------------------------------------
// HTTP POST over POSIX sockets (same pattern as EthRpcClient)
// ---------------------------------------------------------------------------

std::string SolRpcClient::httpPost(const std::string& body) {
  struct addrinfo hints{}, *res = nullptr;
  hints.ai_family   = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  std::string portStr = std::to_string(m_port);
  int gaiRet = getaddrinfo(m_host.c_str(), portStr.c_str(), &hints, &res);
  if (gaiRet != 0 || !res) return "";

  int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (sock < 0) { freeaddrinfo(res); return ""; }

  struct timeval tv;
  tv.tv_sec  = 15;
  tv.tv_usec = 0;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
    close(sock);
    freeaddrinfo(res);
    return "";
  }
  freeaddrinfo(res);

  std::ostringstream req;
  req << "POST / HTTP/1.1\r\n";
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

  std::string response;
  char buf[4096];
  while (true) {
    ssize_t n = recv(sock, buf, sizeof(buf), 0);
    if (n <= 0) break;
    response.append(buf, static_cast<size_t>(n));
  }
  close(sock);

  auto headerEnd = response.find("\r\n\r\n");
  if (headerEnd == std::string::npos) return "";
  return response.substr(headerEnd + 4);
}

// ---------------------------------------------------------------------------
// JSON-RPC wrapper for Solana
// ---------------------------------------------------------------------------

std::string SolRpcClient::jsonRpc(const std::string& method, const std::string& params) {
  static std::atomic<int> requestId{1};
  std::ostringstream body;
  body << "{\"jsonrpc\":\"2.0\",\"id\":" << requestId++
       << ",\"method\":\"" << method
       << "\",\"params\":" << params << "}";
  return httpPost(body.str());
}

// ---------------------------------------------------------------------------
// Basic queries
// ---------------------------------------------------------------------------

bool SolRpcClient::getSlot(uint64_t& slot) {
  std::string resp = jsonRpc("getSlot", "[]");
  if (resp.empty() || jsonHasError(resp)) return false;

  std::string result = jsonGetResult(resp);
  if (result.empty()) return false;

  slot = std::stoull(result);
  return true;
}

bool SolRpcClient::getBalance(const std::string& pubkey, uint64_t& lamports) {
  std::string params = "[\"" + pubkey + "\"]";
  std::string resp = jsonRpc("getBalance", params);
  if (resp.empty() || jsonHasError(resp)) return false;

  std::string result = jsonGetResult(resp);
  if (result.empty()) return false;

  // Solana getBalance returns { "value": <lamports> }
  lamports = jsonGetUint64(result, "value");
  return true;
}

bool SolRpcClient::getAccountInfo(const std::string& pubkey, SolAccountInfo& info) {
  // Request base64 encoding for account data
  std::string params = "[\"" + pubkey + "\",{\"encoding\":\"base64\"}]";
  std::string resp = jsonRpc("getAccountInfo", params);
  if (resp.empty() || jsonHasError(resp)) return false;

  std::string result = jsonGetResult(resp);
  if (result.empty()) return false;

  // Result is {"value": {"lamports":..., "owner":"...", "data":["base64...", "base64"], ...}}
  std::string value = jsonGetString(result, "value");
  if (value.empty()) {
    // Try parsing value as object
    auto vpos = result.find("\"value\"");
    if (vpos == std::string::npos) return false;
    vpos = result.find('{', vpos);
    if (vpos == std::string::npos) return false;
    // Find matching close brace
    int depth = 1;
    size_t start = vpos;
    ++vpos;
    while (vpos < result.size() && depth > 0) {
      if (result[vpos] == '{') ++depth;
      if (result[vpos] == '}') --depth;
      ++vpos;
    }
    value = result.substr(start, vpos - start);
  }

  info.lamports   = jsonGetUint64(value, "lamports");
  info.owner      = jsonGetString(value, "owner");
  info.executable = jsonGetBool(value, "executable");
  info.rentEpoch  = jsonGetUint64(value, "rentEpoch");

  // Extract data array — first element is base64 encoded data
  auto dataPos = value.find("\"data\"");
  if (dataPos != std::string::npos) {
    auto arrStart = value.find('[', dataPos);
    if (arrStart != std::string::npos) {
      auto strStart = value.find('"', arrStart + 1);
      if (strStart != std::string::npos) {
        ++strStart;
        auto strEnd = value.find('"', strStart);
        if (strEnd != std::string::npos) {
          info.dataBase64 = value.substr(strStart, strEnd - strStart);
        }
      }
    }
  }

  return true;
}

bool SolRpcClient::getSignatureStatus(const std::string& signature, bool& confirmed) {
  std::string params = "[[\"" + signature + "\"]]";
  std::string resp = jsonRpc("getSignatureStatuses", params);
  if (resp.empty() || jsonHasError(resp)) return false;

  std::string result = jsonGetResult(resp);
  if (result.empty()) return false;

  // result.value[0] is null if unknown, or has confirmationStatus
  std::string status = jsonGetString(result, "confirmationStatus");
  confirmed = (status == "confirmed" || status == "finalized");
  return true;
}

// ---------------------------------------------------------------------------
// HTLC state parsing
// ---------------------------------------------------------------------------

bool SolRpcClient::parseHtlcState(const std::vector<uint8_t>& data, SolHtlcInfo& info) {
  // Anchor account layout:
  //   8 bytes: discriminator (SHA-256("account:HtlcState")[..8])
  //  32 bytes: sender pubkey
  //  32 bytes: recipient pubkey
  //   8 bytes: amount (u64 LE)
  //  32 bytes: hash_lock
  //   8 bytes: timeout_slot (u64 LE)
  //   1 byte:  claimed (bool)
  //   1 byte:  refunded (bool)
  //  32 bytes: preimage
  //   1 byte:  bump
  // Total: 8 + 147 = 155 bytes

  const size_t EXPECTED_SIZE = 155;
  if (data.size() < EXPECTED_SIZE) return false;

  size_t offset = 8; // skip discriminator

  // sender (32 bytes) — store as hex
  info.sender = bytesToHex(std::vector<uint8_t>(data.begin() + offset,
                                                 data.begin() + offset + 32));
  offset += 32;

  // recipient (32 bytes)
  info.recipient = bytesToHex(std::vector<uint8_t>(data.begin() + offset,
                                                    data.begin() + offset + 32));
  offset += 32;

  // amount (u64 LE)
  info.amount = 0;
  for (int i = 7; i >= 0; --i) {
    info.amount = (info.amount << 8) | data[offset + static_cast<size_t>(i)];
  }
  offset += 8;

  // hash_lock (32 bytes)
  info.hashLock = bytesToHex(std::vector<uint8_t>(data.begin() + offset,
                                                   data.begin() + offset + 32));
  offset += 32;

  // timeout_slot (u64 LE)
  info.timeoutSlot = 0;
  for (int i = 7; i >= 0; --i) {
    info.timeoutSlot = (info.timeoutSlot << 8) | data[offset + static_cast<size_t>(i)];
  }
  offset += 8;

  // claimed (1 byte)
  info.claimed = (data[offset] != 0);
  offset += 1;

  // refunded (1 byte)
  info.refunded = (data[offset] != 0);
  offset += 1;

  // preimage (32 bytes)
  info.preimage = bytesToHex(std::vector<uint8_t>(data.begin() + offset,
                                                   data.begin() + offset + 32));
  offset += 32;

  return true;
}

bool SolRpcClient::getHtlcState(const std::string& htlcAccount, SolHtlcInfo& info) {
  SolAccountInfo acctInfo;
  if (!getAccountInfo(htlcAccount, acctInfo)) return false;
  if (acctInfo.dataBase64.empty()) return false;

  std::vector<uint8_t> data = base64Decode(acctInfo.dataBase64);
  return parseHtlcState(data, info);
}

// ---------------------------------------------------------------------------
// Recent blockhash
// ---------------------------------------------------------------------------

bool SolRpcClient::getRecentBlockhash(std::string& blockhash) {
  std::string resp = jsonRpc("getLatestBlockhash", "[]");
  if (resp.empty() || jsonHasError(resp)) return false;

  std::string result = jsonGetResult(resp);
  if (result.empty()) return false;

  // result.value.blockhash
  std::string value;
  auto vpos = result.find("\"value\"");
  if (vpos != std::string::npos) {
    auto brace = result.find('{', vpos);
    if (brace != std::string::npos) {
      int depth = 1;
      size_t start = brace;
      size_t p = brace + 1;
      while (p < result.size() && depth > 0) {
        if (result[p] == '{') ++depth;
        if (result[p] == '}') --depth;
        ++p;
      }
      value = result.substr(start, p - start);
    }
  }

  blockhash = jsonGetString(value.empty() ? result : value, "blockhash");
  return !blockhash.empty();
}

// ---------------------------------------------------------------------------
// HTLC operations (stubs — transaction building requires Ed25519 signing)
// ---------------------------------------------------------------------------
//
// Full transaction construction requires:
//   1. Serialize Solana compact instruction format
//   2. Sign with Ed25519 (using Fuego's crypto library)
//   3. Base58/base64 encode and send via sendTransaction
//
// These are scaffolded — wire up once the Solana keypair management is
// integrated with the SwapDaemon.

bool SolRpcClient::lock(const std::string& senderSecretKey,
                         const std::string& recipientPubkey,
                         const std::string& hashLockHex,
                         uint64_t timeoutSlot,
                         uint64_t amountLamports,
                         SolTxResult& result) {
  std::string blockhash;
  if (!getRecentBlockhash(blockhash)) {
    result.error = "Failed to get recent blockhash";
    return false;
  }

  std::vector<uint8_t> hashLock = hexToBytes(hashLockHex);
  if (hashLock.size() != 32) {
    result.error = "Hash lock must be 32 bytes";
    return false;
  }

  std::vector<uint8_t> txBytes = buildLockTx(senderSecretKey, recipientPubkey,
                                              hashLock, timeoutSlot,
                                              amountLamports, blockhash);
  if (txBytes.empty()) {
    result.error = "Failed to build lock transaction";
    return false;
  }

  return sendAndConfirmTransaction(txBytes, result);
}

bool SolRpcClient::claim(const std::string& claimerSecretKey,
                          const std::string& htlcAccount,
                          const std::string& preimageHex,
                          SolTxResult& result) {
  std::string blockhash;
  if (!getRecentBlockhash(blockhash)) {
    result.error = "Failed to get recent blockhash";
    return false;
  }

  std::vector<uint8_t> preimage = hexToBytes(preimageHex);
  if (preimage.size() != 32) {
    result.error = "Preimage must be 32 bytes";
    return false;
  }

  std::vector<uint8_t> txBytes = buildClaimTx(claimerSecretKey, htlcAccount,
                                               preimage, blockhash);
  if (txBytes.empty()) {
    result.error = "Failed to build claim transaction";
    return false;
  }

  return sendAndConfirmTransaction(txBytes, result);
}

bool SolRpcClient::refund(const std::string& senderSecretKey,
                           const std::string& htlcAccount,
                           SolTxResult& result) {
  std::string blockhash;
  if (!getRecentBlockhash(blockhash)) {
    result.error = "Failed to get recent blockhash";
    return false;
  }

  std::vector<uint8_t> txBytes = buildRefundTx(senderSecretKey, htlcAccount,
                                                blockhash);
  if (txBytes.empty()) {
    result.error = "Failed to build refund transaction";
    return false;
  }

  return sendAndConfirmTransaction(txBytes, result);
}

// ---------------------------------------------------------------------------
// PDA derivation
// ---------------------------------------------------------------------------

std::string SolRpcClient::deriveHtlcAddress(const std::string& senderPubkey,
                                             const std::string& hashLockHex) {
  // Seeds: [b"xfg_htlc", sender_pubkey_bytes, hash_lock_bytes]
  std::vector<uint8_t> senderBytes = base58Decode(senderPubkey);
  if (senderBytes.size() != 32) return "";

  std::vector<uint8_t> hashLockBytes = hexToBytes(hashLockHex);
  if (hashLockBytes.size() != 32) return "";

  std::vector<uint8_t> programIdBytes = base58Decode(m_programId);
  if (programIdBytes.size() != 32) return "";

  const std::string seed = "xfg_htlc";
  std::vector<uint8_t> seedBytes(seed.begin(), seed.end());

  std::vector<std::vector<uint8_t>> seeds = {seedBytes, senderBytes, hashLockBytes};
  auto [pdaBytes, bump] = derivePDA(seeds, programIdBytes);
  if (pdaBytes.empty()) return "";

  return base58Encode(pdaBytes);
}

std::string SolRpcClient::deriveVaultAddress(const std::string& htlcPubkey) {
  // Seeds: [b"xfg_htlc", htlc_pubkey_bytes]
  std::vector<uint8_t> htlcBytes = base58Decode(htlcPubkey);
  if (htlcBytes.size() != 32) return "";

  std::vector<uint8_t> programIdBytes = base58Decode(m_programId);
  if (programIdBytes.size() != 32) return "";

  const std::string seed = "xfg_htlc";
  std::vector<uint8_t> seedBytes(seed.begin(), seed.end());

  std::vector<std::vector<uint8_t>> seeds = {seedBytes, htlcBytes};
  auto [pdaBytes, bump] = derivePDA(seeds, programIdBytes);
  if (pdaBytes.empty()) return "";

  return base58Encode(pdaBytes);
}

// ---------------------------------------------------------------------------
// Polling
// ---------------------------------------------------------------------------

bool SolRpcClient::waitForClaim(const std::string& htlcAccount,
                                 std::string& preimageHex,
                                 uint32_t pollIntervalMs,
                                 uint64_t maxWaitMs) {
  auto start = std::chrono::steady_clock::now();

  while (true) {
    SolHtlcInfo info;
    if (getHtlcState(htlcAccount, info)) {
      if (info.claimed) {
        preimageHex = info.preimage;
        return true;
      }
      if (info.refunded) {
        return false;  // HTLC was refunded, swap failed
      }
    }

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    if (static_cast<uint64_t>(elapsedMs) >= maxWaitMs) {
      return false;  // Timeout
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(pollIntervalMs));
  }
}

// ---------------------------------------------------------------------------
// Transaction building
// ---------------------------------------------------------------------------
//
// Solana transaction wire format:
//   [num_signatures (compact-u16)][signatures (64 bytes each)]
//   [message]:
//     [num_required_signatures (u8)]
//     [num_readonly_signed (u8)]
//     [num_readonly_unsigned (u8)]
//     [num_accounts (compact-u16)][account pubkeys (32 bytes each)]
//     [recent_blockhash (32 bytes)]
//     [num_instructions (compact-u16)]
//     [instructions]:
//       [program_id_index (u8)]
//       [num_account_indices (compact-u16)][account_indices (u8 each)]
//       [data_length (compact-u16)][instruction_data]
//
// Anchor instruction discriminators:
//   SHA-256("global:lock")[..8]
//   SHA-256("global:claim")[..8]
//   SHA-256("global:refund")[..8]

std::vector<uint8_t> SolRpcClient::buildLockTx(
    const std::string& senderSecretKey,
    const std::string& recipientPubkey,
    const std::vector<uint8_t>& hashLock,
    uint64_t timeoutSlot,
    uint64_t amountLamports,
    const std::string& recentBlockhash) {

  // Decode keypair: first 32 bytes = seed, last 32 = pubkey
  std::vector<uint8_t> keypair = base58Decode(senderSecretKey);
  if (keypair.size() != 64) return {};

  const uint8_t* seed   = keypair.data();
  const uint8_t* pubkey = keypair.data() + 32;

  std::vector<uint8_t> senderPub(pubkey, pubkey + 32);
  std::vector<uint8_t> recipientBytes = base58Decode(recipientPubkey);
  if (recipientBytes.size() != 32) return {};

  std::vector<uint8_t> programIdBytes = base58Decode(m_programId);
  if (programIdBytes.size() != 32) return {};

  std::vector<uint8_t> blockhashBytes = base58Decode(recentBlockhash);
  if (blockhashBytes.size() != 32) return {};

  // Derive HTLC PDA
  const std::string htlcSeed = "xfg_htlc";
  std::vector<uint8_t> htlcSeedBytes(htlcSeed.begin(), htlcSeed.end());
  auto [htlcPda, htlcBump] = derivePDA(
      {htlcSeedBytes, senderPub, hashLock}, programIdBytes);
  if (htlcPda.empty()) return {};

  // Derive vault PDA
  auto [vaultPda, vaultBump] = derivePDA(
      {htlcSeedBytes, htlcPda}, programIdBytes);
  if (vaultPda.empty()) return {};

  // Account list for Lock instruction (Anchor ordering from struct):
  //   0: sender       (signer, writable)
  //   1: recipient    (readonly, unsigned)
  //   2: htlc PDA     (writable, unsigned) — init by Anchor
  //   3: vault PDA    (writable, unsigned)
  //   4: system_program (readonly, unsigned)
  //   5: program_id   (program, not in accounts list but referenced by index)
  //
  // Solana message account ordering:
  //   - Signers writable first, then signers readonly
  //   - Non-signers writable, then non-signers readonly
  //
  // Sorted:
  //   [0] sender        — signer, writable
  //   [1] htlc PDA      — non-signer, writable
  //   [2] vault PDA     — non-signer, writable
  //   [3] recipient     — non-signer, readonly
  //   [4] system_program— non-signer, readonly
  //   [5] program_id    — non-signer, readonly

  std::vector<std::vector<uint8_t>> accounts = {
    senderPub,        // [0] signer, writable
    htlcPda,          // [1] writable
    vaultPda,         // [2] writable
    recipientBytes,   // [3] readonly
    SYSTEM_PROGRAM_ID,// [4] readonly
    programIdBytes,   // [5] readonly (the program itself)
  };

  uint8_t numRequiredSignatures   = 1;  // only sender signs
  uint8_t numReadonlySigned       = 0;
  uint8_t numReadonlyUnsigned     = 3;  // recipient, system_program, program_id

  // Build instruction data: discriminator + recipient_pubkey + hash_lock + timeout_slot + amount
  std::vector<uint8_t> ixData = anchorDiscriminator("global:lock");
  ixData.insert(ixData.end(), hashLock.begin(), hashLock.end());
  appendU64LE(ixData, timeoutSlot);
  appendU64LE(ixData, amountLamports);

  // Instruction account indices (referencing the Anchor struct order):
  //   sender=0, recipient=3, htlc=1, vault=2, system_program=4
  std::vector<uint8_t> ixAccountIndices = {0, 3, 1, 2, 4};

  // Build message
  std::vector<uint8_t> message;
  message.push_back(numRequiredSignatures);
  message.push_back(numReadonlySigned);
  message.push_back(numReadonlyUnsigned);

  // Account keys
  auto numAccounts = compactU16Encode(static_cast<uint16_t>(accounts.size()));
  message.insert(message.end(), numAccounts.begin(), numAccounts.end());
  for (const auto& acct : accounts) {
    message.insert(message.end(), acct.begin(), acct.end());
  }

  // Recent blockhash
  message.insert(message.end(), blockhashBytes.begin(), blockhashBytes.end());

  // Instructions (1 instruction)
  auto numIx = compactU16Encode(1);
  message.insert(message.end(), numIx.begin(), numIx.end());

  // Instruction: program_id_index
  message.push_back(5);  // program_id is at index 5

  // Instruction: account indices
  auto numIxAccts = compactU16Encode(static_cast<uint16_t>(ixAccountIndices.size()));
  message.insert(message.end(), numIxAccts.begin(), numIxAccts.end());
  message.insert(message.end(), ixAccountIndices.begin(), ixAccountIndices.end());

  // Instruction: data
  auto dataLen = compactU16Encode(static_cast<uint16_t>(ixData.size()));
  message.insert(message.end(), dataLen.begin(), dataLen.end());
  message.insert(message.end(), ixData.begin(), ixData.end());

  // Sign message
  uint8_t signature[64];
  ed25519Sign(message.data(), message.size(), seed, pubkey, signature);

  // Assemble full transaction: num_signatures + signatures + message
  std::vector<uint8_t> tx;
  auto numSigs = compactU16Encode(1);
  tx.insert(tx.end(), numSigs.begin(), numSigs.end());
  tx.insert(tx.end(), signature, signature + 64);
  tx.insert(tx.end(), message.begin(), message.end());

  return tx;
}

std::vector<uint8_t> SolRpcClient::buildClaimTx(
    const std::string& claimerSecretKey,
    const std::string& htlcAccount,
    const std::vector<uint8_t>& preimage,
    const std::string& recentBlockhash) {

  // Decode keypair
  std::vector<uint8_t> keypair = base58Decode(claimerSecretKey);
  if (keypair.size() != 64) return {};

  const uint8_t* seed   = keypair.data();
  const uint8_t* pubkey = keypair.data() + 32;

  std::vector<uint8_t> claimerPub(pubkey, pubkey + 32);
  std::vector<uint8_t> htlcBytes = base58Decode(htlcAccount);
  if (htlcBytes.size() != 32) return {};

  std::vector<uint8_t> programIdBytes = base58Decode(m_programId);
  if (programIdBytes.size() != 32) return {};

  std::vector<uint8_t> blockhashBytes = base58Decode(recentBlockhash);
  if (blockhashBytes.size() != 32) return {};

  // Derive vault PDA from htlc account
  const std::string vaultSeed = "xfg_htlc";
  std::vector<uint8_t> vaultSeedBytes(vaultSeed.begin(), vaultSeed.end());
  auto [vaultPda, vaultBump] = derivePDA(
      {vaultSeedBytes, htlcBytes}, programIdBytes);
  if (vaultPda.empty()) return {};

  // Account list for Claim instruction (Anchor struct order):
  //   htlc (mut), recipient (mut), vault (mut)
  //
  // The claimer is the recipient. They must also be a signer for the tx fee.
  //
  // Solana message account ordering:
  //   [0] claimer/recipient — signer, writable
  //   [1] htlc PDA          — non-signer, writable
  //   [2] vault PDA         — non-signer, writable
  //   [3] program_id        — non-signer, readonly

  std::vector<std::vector<uint8_t>> accounts = {
    claimerPub,       // [0] signer, writable (fee payer + recipient)
    htlcBytes,        // [1] writable
    vaultPda,         // [2] writable
    programIdBytes,   // [3] readonly (the program)
  };

  uint8_t numRequiredSignatures   = 1;
  uint8_t numReadonlySigned       = 0;
  uint8_t numReadonlyUnsigned     = 1;  // program_id

  // Build instruction data: discriminator + preimage
  std::vector<uint8_t> ixData = anchorDiscriminator("global:claim");
  ixData.insert(ixData.end(), preimage.begin(), preimage.end());

  // Instruction account indices (Anchor struct order: htlc=1, recipient=0, vault=2)
  std::vector<uint8_t> ixAccountIndices = {1, 0, 2};

  // Build message
  std::vector<uint8_t> message;
  message.push_back(numRequiredSignatures);
  message.push_back(numReadonlySigned);
  message.push_back(numReadonlyUnsigned);

  auto numAccounts = compactU16Encode(static_cast<uint16_t>(accounts.size()));
  message.insert(message.end(), numAccounts.begin(), numAccounts.end());
  for (const auto& acct : accounts) {
    message.insert(message.end(), acct.begin(), acct.end());
  }

  message.insert(message.end(), blockhashBytes.begin(), blockhashBytes.end());

  auto numIx = compactU16Encode(1);
  message.insert(message.end(), numIx.begin(), numIx.end());

  message.push_back(3);  // program_id at index 3

  auto numIxAccts = compactU16Encode(static_cast<uint16_t>(ixAccountIndices.size()));
  message.insert(message.end(), numIxAccts.begin(), numIxAccts.end());
  message.insert(message.end(), ixAccountIndices.begin(), ixAccountIndices.end());

  auto dataLen = compactU16Encode(static_cast<uint16_t>(ixData.size()));
  message.insert(message.end(), dataLen.begin(), dataLen.end());
  message.insert(message.end(), ixData.begin(), ixData.end());

  // Sign
  uint8_t signature[64];
  ed25519Sign(message.data(), message.size(), seed, pubkey, signature);

  // Assemble transaction
  std::vector<uint8_t> tx;
  auto numSigs = compactU16Encode(1);
  tx.insert(tx.end(), numSigs.begin(), numSigs.end());
  tx.insert(tx.end(), signature, signature + 64);
  tx.insert(tx.end(), message.begin(), message.end());

  return tx;
}

std::vector<uint8_t> SolRpcClient::buildRefundTx(
    const std::string& senderSecretKey,
    const std::string& htlcAccount,
    const std::string& recentBlockhash) {

  // Decode keypair
  std::vector<uint8_t> keypair = base58Decode(senderSecretKey);
  if (keypair.size() != 64) return {};

  const uint8_t* seed   = keypair.data();
  const uint8_t* pubkey = keypair.data() + 32;

  std::vector<uint8_t> senderPub(pubkey, pubkey + 32);
  std::vector<uint8_t> htlcBytes = base58Decode(htlcAccount);
  if (htlcBytes.size() != 32) return {};

  std::vector<uint8_t> programIdBytes = base58Decode(m_programId);
  if (programIdBytes.size() != 32) return {};

  std::vector<uint8_t> blockhashBytes = base58Decode(recentBlockhash);
  if (blockhashBytes.size() != 32) return {};

  // Derive vault PDA
  const std::string vaultSeed = "xfg_htlc";
  std::vector<uint8_t> vaultSeedBytes(vaultSeed.begin(), vaultSeed.end());
  auto [vaultPda, vaultBump] = derivePDA(
      {vaultSeedBytes, htlcBytes}, programIdBytes);
  if (vaultPda.empty()) return {};

  // Account list for Refund instruction (Anchor struct order):
  //   htlc (mut), sender (mut), vault (mut)
  //
  // The sender is the signer/fee-payer.
  //
  // Solana message ordering:
  //   [0] sender       — signer, writable
  //   [1] htlc PDA     — non-signer, writable
  //   [2] vault PDA    — non-signer, writable
  //   [3] program_id   — non-signer, readonly

  std::vector<std::vector<uint8_t>> accounts = {
    senderPub,        // [0] signer, writable
    htlcBytes,        // [1] writable
    vaultPda,         // [2] writable
    programIdBytes,   // [3] readonly (the program)
  };

  uint8_t numRequiredSignatures   = 1;
  uint8_t numReadonlySigned       = 0;
  uint8_t numReadonlyUnsigned     = 1;  // program_id

  // Instruction data: discriminator only (no args)
  std::vector<uint8_t> ixData = anchorDiscriminator("global:refund");

  // Instruction account indices (Anchor struct order: htlc=1, sender=0, vault=2)
  std::vector<uint8_t> ixAccountIndices = {1, 0, 2};

  // Build message
  std::vector<uint8_t> message;
  message.push_back(numRequiredSignatures);
  message.push_back(numReadonlySigned);
  message.push_back(numReadonlyUnsigned);

  auto numAccounts = compactU16Encode(static_cast<uint16_t>(accounts.size()));
  message.insert(message.end(), numAccounts.begin(), numAccounts.end());
  for (const auto& acct : accounts) {
    message.insert(message.end(), acct.begin(), acct.end());
  }

  message.insert(message.end(), blockhashBytes.begin(), blockhashBytes.end());

  auto numIx = compactU16Encode(1);
  message.insert(message.end(), numIx.begin(), numIx.end());

  message.push_back(3);  // program_id at index 3

  auto numIxAccts = compactU16Encode(static_cast<uint16_t>(ixAccountIndices.size()));
  message.insert(message.end(), numIxAccts.begin(), numIxAccts.end());
  message.insert(message.end(), ixAccountIndices.begin(), ixAccountIndices.end());

  auto dataLen = compactU16Encode(static_cast<uint16_t>(ixData.size()));
  message.insert(message.end(), dataLen.begin(), dataLen.end());
  message.insert(message.end(), ixData.begin(), ixData.end());

  // Sign
  uint8_t signature[64];
  ed25519Sign(message.data(), message.size(), seed, pubkey, signature);

  // Assemble transaction
  std::vector<uint8_t> tx;
  auto numSigs = compactU16Encode(1);
  tx.insert(tx.end(), numSigs.begin(), numSigs.end());
  tx.insert(tx.end(), signature, signature + 64);
  tx.insert(tx.end(), message.begin(), message.end());

  return tx;
}

bool SolRpcClient::sendAndConfirmTransaction(const std::vector<uint8_t>& txBytes,
                                              SolTxResult& result) {
  if (txBytes.empty()) {
    result.error = "Empty transaction";
    return false;
  }

  // Encode tx as base64 for sendTransaction
  std::string txBase64 = base64Encode(txBytes);
  std::string params = "[\"" + txBase64 + "\",{\"encoding\":\"base64\"}]";
  std::string resp = jsonRpc("sendTransaction", params);

  if (resp.empty() || jsonHasError(resp)) {
    result.error = "sendTransaction failed";
    return false;
  }

  result.signature = jsonGetResult(resp);
  if (result.signature.empty()) {
    result.error = "No signature in response";
    return false;
  }

  // Poll for confirmation (up to 30 seconds)
  for (int i = 0; i < 30; ++i) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    bool confirmed = false;
    if (getSignatureStatus(result.signature, confirmed) && confirmed) {
      result.confirmed = true;
      return true;
    }
  }

  result.error = "Transaction not confirmed within timeout";
  result.confirmed = false;
  return false;
}

bool SolRpcClient::verifyLock(const std::string& htlcAccount,
                               uint64_t expectedLamports) {
  SolHtlcInfo info;
  if (!getHtlcState(htlcAccount, info)) {
    // Account may not exist yet or parsing failed
    return false;
  }
  if (info.claimed || info.refunded) {
    return false;
  }
  return info.amount >= expectedLamports;
}

} // namespace XfgSwap
