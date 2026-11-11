// Copyright (c) 2017-2026 Fuego Developers
//
// Sia HTLC helpers — Blake2b-256 hashlock (per chains-staging/sia plan).
// Sia uses unlock conditions / spend policies rather than Bitcoin script;
// we hash preimages with Blake2b-256 and carry claim preimages in tx metadata.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace XfgSwap {

class SiaHtlcScript {
public:
  // Blake2b-256 of 32-byte preimage → 32-byte digest
  static std::array<uint8_t, 32> blake2b256(const uint8_t* data, size_t len);
  static std::array<uint8_t, 32> blake2b256(const std::vector<uint8_t>& data);

  static std::vector<uint8_t> hexToBytes(const std::string& hex);
  static std::string bytesToHex(const std::vector<uint8_t>& data);
  static std::string bytesToHex(const uint8_t* data, size_t len);

  // Hashlock hex (64 chars) from 32-byte secret
  static std::string hashLockHex(const uint8_t secret[32]);
};

} // namespace XfgSwap
