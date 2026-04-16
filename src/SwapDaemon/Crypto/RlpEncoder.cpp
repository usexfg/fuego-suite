// Copyright (c) 2018-2025, Fuego Developers
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

#include "RlpEncoder.h"
#include <algorithm>
#include <cstring>

namespace CryptoNote {
namespace SwapDaemon {
namespace Crypto {

// ─── RLP encoding rules (from the Yellow Paper) ────────────────────────────
//
// Byte string b of length L:
//   L == 0          → 0x80
//   L == 1, b[0] < 0x80  → b[0]          (single byte pass-through)
//   L <= 55         → (0x80 + L) || b
//   L > 55          → (0xB7 + len(BE(L))) || BE(L) || b
//
// List of encoded content C of length L:
//   L <= 55         → (0xC0 + L) || C
//   L > 55          → (0xF7 + len(BE(L))) || BE(L) || C
//
// where BE(x) is the big-endian encoding of x with no leading zeros.

static std::vector<uint8_t> encodeBigEndian(uint64_t value) {
  if (value == 0) return {};
  std::vector<uint8_t> result;
  while (value > 0) {
    result.push_back(static_cast<uint8_t>(value & 0xFF));
    value >>= 8;
  }
  std::reverse(result.begin(), result.end());
  return result;
}

// Encode the RLP prefix for a byte string of `contentLen` bytes, writing
// the prefix bytes into `out`.
static void writeStringPrefix(size_t contentLen, std::vector<uint8_t>& out) {
  if (contentLen <= 55) {
    out.push_back(static_cast<uint8_t>(0x80 + contentLen));
  } else {
    auto lenBytes = encodeBigEndian(static_cast<uint64_t>(contentLen));
    out.push_back(static_cast<uint8_t>(0xB7 + lenBytes.size()));
    out.insert(out.end(), lenBytes.begin(), lenBytes.end());
  }
}

// Encode the RLP prefix for a list whose encoded content occupies `contentLen`
// bytes, writing the prefix bytes into `out`.
static void writeListPrefix(size_t contentLen, std::vector<uint8_t>& out) {
  if (contentLen <= 55) {
    out.push_back(static_cast<uint8_t>(0xC0 + contentLen));
  } else {
    auto lenBytes = encodeBigEndian(static_cast<uint64_t>(contentLen));
    out.push_back(static_cast<uint8_t>(0xF7 + lenBytes.size()));
    out.insert(out.end(), lenBytes.begin(), lenBytes.end());
  }
}

// Write a byte string (already known to not be a single-byte pass-through) with
// its prefix into `out`.
static void writeEncodedBytes(const uint8_t* data, size_t len,
                               std::vector<uint8_t>& out) {
  if (len == 0) {
    out.push_back(0x80);
    return;
  }
  if (len == 1 && data[0] < 0x80) {
    // Single byte < 0x80: no prefix, just the byte.
    out.push_back(data[0]);
    return;
  }
  writeStringPrefix(len, out);
  out.insert(out.end(), data, data + len);
}

// ─── RlpEncoder implementation ──────────────────────────────────────────────

RlpEncoder::RlpEncoder() = default;

void RlpEncoder::beginList() {
  if (m_inList) {
    throw std::logic_error("RlpEncoder::beginList: nested lists not supported");
  }
  m_inList = true;
  m_listContent.clear();
}

void RlpEncoder::endList() {
  if (!m_inList) {
    throw std::logic_error("RlpEncoder::endList: not inside a list");
  }
  m_inList = false;
  // Encode the list: prefix + collected content.
  writeListPrefix(m_listContent.size(), m_top);
  m_top.insert(m_top.end(), m_listContent.begin(), m_listContent.end());
}

void RlpEncoder::writeUint(uint64_t value) {
  auto bytes = encodeBigEndian(value);
  auto& out = m_inList ? m_listContent : m_top;
  writeEncodedBytes(bytes.data(), bytes.size(), out);
}

void RlpEncoder::writeUint256(const uint8_t* bytes32) {
  // Strip leading zeros.
  size_t start = 0;
  while (start < 32 && bytes32[start] == 0) ++start;
  size_t len = 32 - start;
  auto& out = m_inList ? m_listContent : m_top;
  writeEncodedBytes(bytes32 + start, len, out);
}

void RlpEncoder::writeBytes(const std::vector<uint8_t>& data) {
  writeBytes(data.data(), data.size());
}

void RlpEncoder::writeBytes(const uint8_t* data, size_t len) {
  auto& out = m_inList ? m_listContent : m_top;
  writeEncodedBytes(data, len, out);
}

std::vector<uint8_t> RlpEncoder::finalize() const {
  if (m_inList) {
    throw std::logic_error("RlpEncoder::finalize: endList not called");
  }
  return m_top;
}

}  // namespace Crypto
}  // namespace SwapDaemon
}  // namespace CryptoNote
