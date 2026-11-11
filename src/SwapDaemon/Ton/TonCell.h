// Copyright (c) 2017-2026 Fuego Developers
//
// Minimal TON cell / BOC builder for HTLC external messages.
// Implements enough TL-B to encode: integers, bytes, refs, and serialize BOC.

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <memory>

namespace XfgSwap {
namespace Ton {

class Cell {
public:
  Cell() = default;

  void storeBit(bool b);
  void storeUint(uint64_t value, unsigned bits);
  void storeBytes(const uint8_t* data, size_t len);
  void storeBytes(const std::vector<uint8_t>& data);
  void storeRef(const std::shared_ptr<Cell>& ref);
  // Store 256-bit big-endian hash/key
  void storeUint256(const uint8_t be[32]);

  // SHA-256 representation hash of this cell (TON cell hash).
  std::array<uint8_t, 32> hash() const;

  // Serialize as single-root BOC (base64).
  std::string toBocBase64() const;
  std::vector<uint8_t> toBocBytes() const;

  unsigned bitLen() const { return static_cast<unsigned>(m_bits.size()); }
  size_t refCount() const { return m_refs.size(); }

private:
  std::vector<bool> m_bits;
  std::vector<std::shared_ptr<Cell>> m_refs;

  std::vector<uint8_t> descriptorAndData() const;
};

// CRC16-CCITT (poly 0x1021) used by TON addresses / BOC.
uint16_t crc16Ccitt(const uint8_t* data, size_t len);

// Base64 encode
std::string base64Encode(const uint8_t* data, size_t len);
std::vector<uint8_t> base64Decode(const std::string& b64);

// Build HTLC claim body: op::claim + 32-byte preimage
std::shared_ptr<Cell> buildClaimBody(const uint8_t preimage[32]);
// Build HTLC refund body: op::refund
std::shared_ptr<Cell> buildRefundBody();
// Build HTLC lock/init body: op::lock + hashlock + timeout + recipient workchain/hash
std::shared_ptr<Cell> buildLockBody(const uint8_t hashLock[32], uint64_t timeoutUnix,
                                    int8_t recipientWc, const uint8_t recipientHash[32]);

// External inbound message wrapper (simple: no ihr, bounce false).
// dest: workchain + 32-byte address hash; body: message body cell.
std::shared_ptr<Cell> buildExternalInMessage(int8_t destWc, const uint8_t destHash[32],
                                             const std::shared_ptr<Cell>& body);

// Parse raw friendly or raw TON address into workchain + 32-byte hash.
// Accepts: "0:hex64" or base64url user-friendly.
bool parseTonAddress(const std::string& addr, int8_t& wc, uint8_t hash[32]);

// Format raw address "wc:hex64"
std::string formatRawAddress(int8_t wc, const uint8_t hash[32]);

} // namespace Ton
} // namespace XfgSwap
