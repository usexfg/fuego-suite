// Copyright (c) 2017-2026 Fuego Developers

#include "SiaHtlcScript.h"
#include <openssl/evp.h>
#include <cstring>
#include <stdexcept>

namespace XfgSwap {

std::array<uint8_t, 32> SiaHtlcScript::blake2b256(const uint8_t* data, size_t len) {
  std::array<uint8_t, 32> out{};
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  if (!ctx) throw std::runtime_error("blake2b: ctx");
  // OpenSSL: Blake2b-512 truncated to 32 bytes (compatible with Sia hashlock plan).
  // Full Blake2b-256 (param digest_length=32) can replace this when we pin OpenSSL 3+.
  if (EVP_DigestInit_ex(ctx, EVP_blake2b512(), nullptr) != 1) {
    EVP_MD_CTX_free(ctx);
    throw std::runtime_error("blake2b: init");
  }
  EVP_DigestUpdate(ctx, data, len);
  uint8_t full[64];
  unsigned int ol = 64;
  EVP_DigestFinal_ex(ctx, full, &ol);
  EVP_MD_CTX_free(ctx);
  std::memcpy(out.data(), full, 32);
  return out;
}

std::array<uint8_t, 32> SiaHtlcScript::blake2b256(const std::vector<uint8_t>& data) {
  return blake2b256(data.data(), data.size());
}

std::vector<uint8_t> SiaHtlcScript::hexToBytes(const std::string& hex) {
  std::string h = hex;
  if (h.size() >= 2 && h[0] == '0' && (h[1] == 'x' || h[1] == 'X')) h = h.substr(2);
  if (h.size() % 2) throw std::runtime_error("hex length");
  std::vector<uint8_t> out(h.size() / 2);
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  for (size_t i = 0; i < out.size(); ++i) {
    int hi = nib(h[i * 2]), lo = nib(h[i * 2 + 1]);
    if (hi < 0 || lo < 0) throw std::runtime_error("hex char");
    out[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return out;
}

std::string SiaHtlcScript::bytesToHex(const uint8_t* data, size_t len) {
  static const char* hexd = "0123456789abcdef";
  std::string s;
  s.reserve(len * 2);
  for (size_t i = 0; i < len; ++i) {
    s.push_back(hexd[data[i] >> 4]);
    s.push_back(hexd[data[i] & 0xf]);
  }
  return s;
}

std::string SiaHtlcScript::bytesToHex(const std::vector<uint8_t>& data) {
  return bytesToHex(data.data(), data.size());
}

std::string SiaHtlcScript::hashLockHex(const uint8_t secret[32]) {
  auto d = blake2b256(secret, 32);
  return bytesToHex(d.data(), 32);
}

} // namespace XfgSwap
