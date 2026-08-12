// Copyright (c) 2017-2026 Fuego Developers

#include "SiaStorageClient.h"
#include "Common/StringTools.h"

#include <HTTP/httplib.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <cstring>
#include <sstream>

namespace Fuego {
namespace Storage {

std::string ISiaStorage::contentHashHex(const uint8_t* data, size_t len) {
  uint8_t dig[32];
  SHA256(data, len, dig);
  static const char* h = "0123456789abcdef";
  std::string s;
  s.reserve(64);
  for (int i = 0; i < 32; ++i) {
    s.push_back(h[dig[i] >> 4]);
    s.push_back(h[dig[i] & 0xf]);
  }
  return s;
}

SiaStorageClient::SiaStorageClient(SiaRenterConfig cfg) : m_cfg(std::move(cfg)) {}

std::string SiaStorageClient::objectPath(const std::string& key) const {
  // /api/worker/objects/<bucket>/<key>
  std::string k = key;
  while (!k.empty() && k[0] == '/') k.erase(0, 1);
  return "/api/worker/objects/" + m_cfg.bucket + "/" + k;
}

std::string SiaStorageClient::http(const std::string& method, const std::string& path,
                                   const std::string& body, const std::string& contentType,
                                   int& statusOut) {
  statusOut = 0;
  std::string base = m_cfg.renterdUrl;
  bool https = false;
  if (base.rfind("https://", 0) == 0) { https = true; base = base.substr(8); }
  else if (base.rfind("http://", 0) == 0) { base = base.substr(7); }
  std::string host = base;
  uint16_t port = https ? 443 : 80;
  std::string pathPrefix;
  auto colon = host.find(':');
  auto slash = host.find('/');
  if (colon != std::string::npos && (slash == std::string::npos || colon < slash)) {
    port = static_cast<uint16_t>(std::stoi(host.substr(colon + 1)));
    host = host.substr(0, colon);
    // re-find slash after port strip — host may still include path if URL was host:port/path
    slash = host.find('/');
  }
  // If original base had path after host:port
  auto full = m_cfg.renterdUrl;
  auto schemeEnd = full.find("://");
  std::string rest = schemeEnd == std::string::npos ? full : full.substr(schemeEnd + 3);
  auto pathStart = rest.find('/');
  if (pathStart != std::string::npos)
    pathPrefix = rest.substr(pathStart);
  // strip port from host again if needed
  colon = host.rfind(':');
  if (colon != std::string::npos && host.find(']') == std::string::npos) {
    try { port = static_cast<uint16_t>(std::stoi(host.substr(colon + 1))); } catch (...) {}
    host = host.substr(0, colon);
  }

  std::string fullPath = pathPrefix + path;
  try {
    httplib::Headers headers;
    if (!contentType.empty())
      headers.emplace("Content-Type", contentType);

    auto doReq = [&](auto& cli) -> std::string {
      cli.set_connection_timeout(static_cast<int>(m_cfg.connectTimeoutSec), 0);
      cli.set_read_timeout(static_cast<int>(m_cfg.readTimeoutSec), 0);
      if (!m_cfg.apiPassword.empty())
        cli.set_basic_auth("", m_cfg.apiPassword.c_str());
      if (method == "GET") {
        auto res = cli.Get(fullPath.c_str(), headers);
        if (!res) return {};
        statusOut = res->status;
        return res->body;
      }
      if (method == "PUT") {
        auto res = cli.Put(fullPath.c_str(), headers, body, contentType.c_str());
        if (!res) return {};
        statusOut = res->status;
        return res->body;
      }
      if (method == "DELETE") {
        auto res = cli.Delete(fullPath.c_str(), headers);
        if (!res) return {};
        statusOut = res->status;
        return res->body;
      }
      // POST
      auto res = cli.Post(fullPath.c_str(), headers, body, contentType.c_str());
      if (!res) return {};
      statusOut = res->status;
      return res->body;
    };

    if (https) {
      httplib::SSLClient cli(host, port);
      return doReq(cli);
    }
    httplib::Client cli(host, port);
    return doReq(cli);
  } catch (...) {
    return {};
  }
}

bool SiaStorageClient::ping(std::string& statusOut) {
  int st = 0;
  // renterd bus state
  std::string body = http("GET", "/api/bus/state", {}, {}, st);
  if (st >= 200 && st < 300) {
    statusOut = "renterd bus OK (HTTP " + std::to_string(st) + ")";
    if (m_cfg.minBalanceHastings > 0) {
      uint64_t bal = 0;
      if (getWalletBalanceHastings(bal) && bal < m_cfg.minBalanceHastings) {
        statusOut += "; low SC balance";
        return false;
      }
    }
    return true;
  }
  // fallback: worker id
  body = http("GET", "/api/worker/id", {}, {}, st);
  if (st >= 200 && st < 300) {
    statusOut = "renterd worker OK";
    return true;
  }
  statusOut = "renterd unreachable (HTTP " + std::to_string(st) + ")";
  return false;
}

bool SiaStorageClient::getWalletBalanceHastings(uint64_t& hastings) {
  int st = 0;
  std::string body = http("GET", "/api/bus/wallet", {}, {}, st);
  if (st < 200 || st >= 300 || body.empty()) return false;
  // crude parse "confirmed" / "balance" fields
  auto findNum = [&](const char* key) -> bool {
    auto pos = body.find(key);
    if (pos == std::string::npos) return false;
    pos = body.find(':', pos);
    if (pos == std::string::npos) return false;
    ++pos;
    while (pos < body.size() && (body[pos] == ' ' || body[pos] == '"')) ++pos;
    try {
      hastings = std::stoull(body.substr(pos));
      return true;
    } catch (...) { return false; }
  };
  return findNum("confirmed") || findNum("balance") || findNum("spendable");
}

bool SiaStorageClient::encrypt(const uint8_t* in, size_t len,
                               std::vector<uint8_t>& out, std::string& err) {
  if (m_cfg.clientAesKeyHex.size() != 64) {
    out.assign(in, in + len);
    return true; // no key → plaintext pass-through
  }
  uint8_t key[32];
  try {
    auto kb = Common::fromHex(m_cfg.clientAesKeyHex);
    if (kb.size() != 32) { err = "AES key must be 32 bytes hex"; return false; }
    std::memcpy(key, kb.data(), 32);
  } catch (...) {
    err = "invalid AES key hex";
    return false;
  }
  uint8_t iv[16];
  if (RAND_bytes(iv, 16) != 1) { err = "RAND_bytes failed"; return false; }

  out.resize(16 + len + 16); // iv + ciphertext + possible pad room
  std::memcpy(out.data(), iv, 16);

  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  int outl = 0, fin = 0;
  if (!ctx ||
      EVP_EncryptInit_ex(ctx, EVP_aes_256_ctr(), nullptr, key, iv) != 1 ||
      EVP_EncryptUpdate(ctx, out.data() + 16, &outl, in, static_cast<int>(len)) != 1 ||
      EVP_EncryptFinal_ex(ctx, out.data() + 16 + outl, &fin) != 1) {
    if (ctx) EVP_CIPHER_CTX_free(ctx);
    err = "AES encrypt failed";
    return false;
  }
  EVP_CIPHER_CTX_free(ctx);
  out.resize(static_cast<size_t>(16 + outl + fin));
  OPENSSL_cleanse(key, sizeof(key));
  return true;
}

bool SiaStorageClient::decrypt(const uint8_t* in, size_t len,
                               std::vector<uint8_t>& out, std::string& err) {
  if (m_cfg.clientAesKeyHex.size() != 64) {
    out.assign(in, in + len);
    return true;
  }
  if (len < 16) { err = "ciphertext too short"; return false; }
  uint8_t key[32];
  try {
    auto kb = Common::fromHex(m_cfg.clientAesKeyHex);
    if (kb.size() != 32) { err = "AES key must be 32 bytes hex"; return false; }
    std::memcpy(key, kb.data(), 32);
  } catch (...) {
    err = "invalid AES key hex";
    return false;
  }
  const uint8_t* iv = in;
  const uint8_t* ct = in + 16;
  size_t ctLen = len - 16;
  out.resize(ctLen + 16);
  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  int outl = 0, fin = 0;
  if (!ctx ||
      EVP_DecryptInit_ex(ctx, EVP_aes_256_ctr(), nullptr, key, iv) != 1 ||
      EVP_DecryptUpdate(ctx, out.data(), &outl, ct, static_cast<int>(ctLen)) != 1 ||
      EVP_DecryptFinal_ex(ctx, out.data() + outl, &fin) != 1) {
    if (ctx) EVP_CIPHER_CTX_free(ctx);
    err = "AES decrypt failed";
    return false;
  }
  EVP_CIPHER_CTX_free(ctx);
  out.resize(static_cast<size_t>(outl + fin));
  OPENSSL_cleanse(key, sizeof(key));
  return true;
}

SiaPutResult SiaStorageClient::putObject(const std::string& key,
                                         const uint8_t* data, size_t len,
                                         bool encryptClientSide) {
  SiaPutResult r;
  r.contentHash = contentHashHex(data, len);
  r.sizeBytes = len;
  r.key = key.empty() ? ("cid/" + r.contentHash) : key;

  std::vector<uint8_t> payload;
  if (encryptClientSide) {
    std::string err;
    if (!encrypt(data, len, payload, err)) {
      r.error = err;
      return r;
    }
  } else {
    payload.assign(data, data + len);
  }

  int st = 0;
  std::string body(reinterpret_cast<const char*>(payload.data()), payload.size());
  std::string resp = http("PUT", objectPath(r.key), body, "application/octet-stream", st);
  if (st < 200 || st >= 300) {
    r.error = "renterd PUT failed HTTP " + std::to_string(st) +
              (resp.empty() ? "" : (": " + resp.substr(0, 200)));
    return r;
  }
  r.success = true;
  return r;
}

SiaGetResult SiaStorageClient::getObject(const std::string& key,
                                         const std::string& expectedCid) {
  SiaGetResult r;
  int st = 0;
  std::string body = http("GET", objectPath(key), {}, {}, st);
  if (st < 200 || st >= 300) {
    r.error = "renterd GET failed HTTP " + std::to_string(st);
    return r;
  }
  std::vector<uint8_t> raw(body.begin(), body.end());
  std::string err;
  if (!decrypt(raw.data(), raw.size(), r.data, err)) {
    r.error = err;
    return r;
  }
  r.contentHash = contentHashHex(r.data.data(), r.data.size());
  r.hashMatches = expectedCid.empty() || expectedCid == r.contentHash;
  if (!r.hashMatches) {
    r.error = "CID mismatch: expected " + expectedCid + " got " + r.contentHash;
    return r;
  }
  r.success = true;
  return r;
}

bool SiaStorageClient::deleteObject(const std::string& key, std::string& error) {
  int st = 0;
  http("DELETE", objectPath(key), {}, {}, st);
  if (st < 200 || st >= 300) {
    error = "renterd DELETE failed HTTP " + std::to_string(st);
    return false;
  }
  return true;
}

} // namespace Storage
} // namespace Fuego
