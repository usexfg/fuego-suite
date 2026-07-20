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

#include "SpvHeader.h"
#include "../BitcoinCash/HtlcScript.h"
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <cmath>

namespace XfgSwap {

// =============================================================================
// Internal helpers
// =============================================================================

static uint32_t readLE32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0])
       | (static_cast<uint32_t>(p[1]) << 8)
       | (static_cast<uint32_t>(p[2]) << 16)
       | (static_cast<uint32_t>(p[3]) << 24);
}

static void writeLE32(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

static std::string reverseHex(const std::vector<uint8_t>& bytes) {
  std::vector<uint8_t> rev(bytes.rbegin(), bytes.rend());
  return BchHtlcScript::bytesToHex(rev);
}

// =============================================================================
// Parse / Serialize
// =============================================================================

SpvHeader SpvHeader::parse(const std::vector<uint8_t>& raw80) {
  if (raw80.size() != 80) {
    throw std::runtime_error("SpvHeader::parse: header must be exactly 80 bytes");
  }

  SpvHeader h;
  const uint8_t* p = raw80.data();

  h.version = readLE32(p);       p += 4;
  h.prevHash.assign(p, p + 32);  p += 32;
  h.merkleRoot.assign(p, p + 32); p += 32;
  h.time = readLE32(p);          p += 4;
  h.bits = readLE32(p);          p += 4;
  h.nonce = readLE32(p);

  return h;
}

std::vector<uint8_t> SpvHeader::serialize() const {
  std::vector<uint8_t> out;
  out.reserve(80);

  writeLE32(out, version);
  out.insert(out.end(), prevHash.begin(), prevHash.end());
  out.insert(out.end(), merkleRoot.begin(), merkleRoot.end());
  writeLE32(out, time);
  writeLE32(out, bits);
  writeLE32(out, nonce);

  return out;
}

// =============================================================================
// Hash
// =============================================================================

std::vector<uint8_t> SpvHeader::hash() const {
  return BchHtlcScript::doubleSha256(serialize());
}

std::string SpvHeader::hashDisplay() const {
  return reverseHex(hash());
}

std::string SpvHeader::prevHashDisplay() const {
  return reverseHex(prevHash);
}

std::string SpvHeader::merkleRootDisplay() const {
  return reverseHex(merkleRoot);
}

// =============================================================================
// nBits / PoW
// =============================================================================

std::vector<uint8_t> SpvHeader::nBitsToTargetBE(uint32_t bits) {
  // nBits format: top byte = exponent, bottom 3 bytes = mantissa (big-endian)
  // target = mantissa * 2^(8 * (exponent - 3))
  uint32_t exponent = bits >> 24;
  uint32_t mantissa = bits & 0x007FFFFF;

  std::vector<uint8_t> target(32, 0);

  if (exponent == 0) {
    return target;
  }

  if (exponent < 3) {
    mantissa >>= (8 * (3 - exponent));
  }

  if (exponent >= 3) {
    uint32_t pos = 32 - exponent;
    target[pos]     = static_cast<uint8_t>((mantissa >> 16) & 0xFF);
    target[pos + 1] = static_cast<uint8_t>((mantissa >> 8) & 0xFF);
    target[pos + 2] = static_cast<uint8_t>(mantissa & 0xFF);
  } else {
    for (int i = exponent - 1; i >= 0; i--) {
      target[32 - exponent + i] = static_cast<uint8_t>(mantissa & 0xFF);
      mantissa >>= 8;
    }
  }

  return target;
}

bool SpvHeader::meetsPoW() const {
  std::vector<uint8_t> h = hash();
  std::vector<uint8_t> target = nBitsToTargetBE(bits);

  // hash() returns internal LE bytes; reverse to BE for comparison
  std::reverse(h.begin(), h.end());
  // target is already big-endian from nBitsToTargetBE

  // Compare byte by byte (big-endian: hash <= target)
  for (size_t i = 0; i < 32; ++i) {
    if (h[i] < target[i]) return true;
    if (h[i] > target[i]) return false;
  }
  return true;  // equal
}

long double SpvHeader::work() const {
  // work = 2^256 / (target + 1)
  // Convert target (32 bytes BE) to a number
  std::vector<uint8_t> target = nBitsToTargetBE(bits);

  // Build target as a big integer using floating-point approximation
  // target = sum(target[i] * 256^(31-i)) for i in 0..31
  long double targetVal = 0;
  for (size_t i = 0; i < 32; ++i) {
    targetVal = targetVal * 256.0L + static_cast<long double>(target[i]);
  }

  if (targetVal == 0) return 0;

  // work = 2^256 / (target + 1)
  // Use log2 to avoid overflow: log2(work) = 256 - log2(target + 1)
  long double log2Work = 256.0L - std::log2(targetVal + 1.0L);
  return std::pow(2.0L, log2Work);
}

} // namespace XfgSwap
