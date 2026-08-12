// Copyright (c) 2017-2026 Fuego Developers

#include "TonCell.h"

#include <openssl/sha.h>
#include <cstring>
#include <stdexcept>
#include <algorithm>
#include <functional>
#include <cstdio>

namespace XfgSwap {
namespace Ton {

namespace {
void sha256(const uint8_t* data, size_t len, uint8_t out[32]) {
  SHA256(data, len, out);
}
} // namespace

void Cell::storeBit(bool b) {
  m_bits.push_back(b);
}

void Cell::storeUint(uint64_t value, unsigned bits) {
  for (int i = static_cast<int>(bits) - 1; i >= 0; --i)
    storeBit(((value >> static_cast<unsigned>(i)) & 1ULL) != 0);
}

void Cell::storeBytes(const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) storeUint(data[i], 8);
}

void Cell::storeBytes(const std::vector<uint8_t>& data) {
  storeBytes(data.data(), data.size());
}

void Cell::storeRef(const std::shared_ptr<Cell>& ref) {
  if (m_refs.size() >= 4) throw std::runtime_error("TON cell: max 4 refs");
  m_refs.push_back(ref);
}

void Cell::storeUint256(const uint8_t be[32]) {
  storeBytes(be, 32);
}

std::vector<uint8_t> Cell::descriptorAndData() const {
  unsigned r = static_cast<unsigned>(m_refs.size());
  unsigned b = static_cast<unsigned>(m_bits.size());
  uint8_t d1 = static_cast<uint8_t>(r);
  uint8_t d2 = static_cast<uint8_t>((b / 8) * 2 + ((b % 8) != 0 ? 1 : 0));

  size_t dataBytes = (b + 7) / 8;
  std::vector<uint8_t> out;
  out.reserve(2 + dataBytes);
  out.push_back(d1);
  out.push_back(d2);

  for (size_t i = 0; i < dataBytes; ++i) {
    uint8_t byte = 0;
    for (unsigned j = 0; j < 8; ++j) {
      size_t idx = i * 8 + j;
      if (idx < m_bits.size() && m_bits[idx])
        byte |= static_cast<uint8_t>(1u << (7 - j));
    }
    if (i + 1 == dataBytes && (b % 8) != 0) {
      unsigned used = b % 8;
      byte |= static_cast<uint8_t>(1u << (7 - used));
    }
    out.push_back(byte);
  }
  return out;
}

std::array<uint8_t, 32> Cell::hash() const {
  auto body = descriptorAndData();
  std::vector<uint8_t> repr = body;
  for (const auto& r : m_refs) {
    uint16_t depth = 0;
    repr.push_back(static_cast<uint8_t>((depth >> 8) & 0xff));
    repr.push_back(static_cast<uint8_t>(depth & 0xff));
    auto h = r->hash();
    repr.insert(repr.end(), h.begin(), h.end());
  }
  std::array<uint8_t, 32> out;
  sha256(repr.data(), repr.size(), out.data());
  return out;
}

std::vector<uint8_t> Cell::toBocBytes() const {
  std::vector<const Cell*> order;
  std::function<void(const Cell*)> dfs = [&](const Cell* c) {
    for (const auto& r : c->m_refs) dfs(r.get());
    if (std::find(order.begin(), order.end(), c) == order.end())
      order.push_back(c);
  };
  dfs(this);

  std::vector<uint8_t> cellData;
  for (const Cell* c : order) {
    auto d = c->descriptorAndData();
    cellData.insert(cellData.end(), d.begin(), d.end());
    for (const auto& r : c->m_refs) {
      auto it = std::find(order.begin(), order.end(), r.get());
      cellData.push_back(static_cast<uint8_t>(it - order.begin()));
    }
  }

  std::vector<uint8_t> boc;
  boc.push_back(0xB5); boc.push_back(0xEE); boc.push_back(0x9C); boc.push_back(0x72);
  boc.push_back(0x01); // flags + size_bytes=1
  boc.push_back(0x01); // off_bytes
  boc.push_back(static_cast<uint8_t>(order.size()));
  boc.push_back(0x01); // roots
  boc.push_back(0x00); // absent
  boc.push_back(static_cast<uint8_t>(cellData.size()));
  auto rootIt = std::find(order.begin(), order.end(), this);
  boc.push_back(static_cast<uint8_t>(rootIt - order.begin()));
  boc.insert(boc.end(), cellData.begin(), cellData.end());
  return boc;
}

std::string Cell::toBocBase64() const {
  auto b = toBocBytes();
  return base64Encode(b.data(), b.size());
}

uint16_t crc16Ccitt(const uint8_t* data, size_t len) {
  uint16_t crc = 0;
  for (size_t i = 0; i < len; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (int j = 0; j < 8; ++j)
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                           : static_cast<uint16_t>(crc << 1);
  }
  return crc;
}

std::string base64Encode(const uint8_t* data, size_t len) {
  static const char* T =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((len + 2) / 3) * 4);
  for (size_t i = 0; i < len; i += 3) {
    uint32_t n = static_cast<uint32_t>(data[i]) << 16;
    if (i + 1 < len) n |= static_cast<uint32_t>(data[i + 1]) << 8;
    if (i + 2 < len) n |= static_cast<uint32_t>(data[i + 2]);
    out.push_back(T[(n >> 18) & 63]);
    out.push_back(T[(n >> 12) & 63]);
    out.push_back(i + 1 < len ? T[(n >> 6) & 63] : '=');
    out.push_back(i + 2 < len ? T[n & 63] : '=');
  }
  return out;
}

std::vector<uint8_t> base64Decode(const std::string& b64) {
  auto val = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
  };
  std::vector<uint8_t> out;
  uint32_t buf = 0;
  int bits = 0;
  for (char c : b64) {
    if (c == '=' || c == '\n' || c == '\r') continue;
    int v = val(c);
    if (v < 0) continue;
    buf = (buf << 6) | static_cast<uint32_t>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<uint8_t>((buf >> bits) & 0xff));
    }
  }
  return out;
}

// HTLC opcodes (must match contracts/ton/htlc.fc)
static constexpr uint32_t OP_CLAIM  = 1;
static constexpr uint32_t OP_REFUND = 2;
static constexpr uint32_t OP_LOCK   = 3;

std::shared_ptr<Cell> buildClaimBody(const uint8_t preimage[32]) {
  auto c = std::make_shared<Cell>();
  c->storeUint(OP_CLAIM, 32);
  c->storeUint256(preimage);
  return c;
}

std::shared_ptr<Cell> buildRefundBody() {
  auto c = std::make_shared<Cell>();
  c->storeUint(OP_REFUND, 32);
  return c;
}

std::shared_ptr<Cell> buildLockBody(const uint8_t hashLock[32], uint64_t timeoutUnix,
                                    int8_t recipientWc, const uint8_t recipientHash[32]) {
  auto c = std::make_shared<Cell>();
  c->storeUint(OP_LOCK, 32);
  c->storeUint256(hashLock);
  c->storeUint(timeoutUnix, 64);
  c->storeUint(static_cast<uint64_t>(static_cast<uint8_t>(recipientWc)), 8);
  c->storeUint256(recipientHash);
  return c;
}

std::shared_ptr<Cell> buildExternalInMessage(int8_t destWc, const uint8_t destHash[32],
                                             const std::shared_ptr<Cell>& body) {
  auto m = std::make_shared<Cell>();
  m->storeUint(0b10, 2);  // ext_in_msg_info
  m->storeUint(0b00, 2);  // src addr_none
  m->storeUint(0b10, 2);  // dest addr_std
  m->storeBit(false);     // no anycast
  m->storeUint(static_cast<uint64_t>(static_cast<uint8_t>(destWc)), 8);
  m->storeUint256(destHash);
  m->storeUint(0, 4);     // import_fee = 0
  m->storeBit(false);     // no StateInit
  m->storeBit(true);      // body as ref
  m->storeRef(body);
  return m;
}

bool parseTonAddress(const std::string& addr, int8_t& wc, uint8_t hash[32]) {
  auto colon = addr.find(':');
  if (colon != std::string::npos) {
    try {
      wc = static_cast<int8_t>(std::stoi(addr.substr(0, colon)));
    } catch (...) { return false; }
    std::string hex = addr.substr(colon + 1);
    if (hex.size() != 64) return false;
    for (size_t i = 0; i < 32; ++i) {
      auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
      };
      int hi = nib(hex[i * 2]), lo = nib(hex[i * 2 + 1]);
      if (hi < 0 || lo < 0) return false;
      hash[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
  }
  auto raw = base64Decode(addr);
  if (raw.size() == 36) {
    wc = static_cast<int8_t>(raw[1]);
    std::memcpy(hash, raw.data() + 2, 32);
    return true;
  }
  return false;
}

std::string formatRawAddress(int8_t wc, const uint8_t hash[32]) {
  static const char* hexd = "0123456789abcdef";
  std::string s = std::to_string(static_cast<int>(wc)) + ":";
  for (int i = 0; i < 32; ++i) {
    s.push_back(hexd[hash[i] >> 4]);
    s.push_back(hexd[hash[i] & 0xf]);
  }
  return s;
}

} // namespace Ton
} // namespace XfgSwap
