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

#include "PoolTypes.h"
#include <cstring>

namespace XfgSwap {

const char* poolEventTypeToString(PoolEventType e) {
  switch (e) {
    case PoolEventType::DEPOSIT:    return "deposit";
    case PoolEventType::WITHDRAWAL: return "withdrawal";
    case PoolEventType::SWAP:       return "swap";
    case PoolEventType::FEE_CLAIM:  return "fee_claim";
    case PoolEventType::CHECKPOINT: return "checkpoint";
    default:                        return "unknown";
  }
}

PoolEventType poolEventTypeFromString(const std::string& s) {
  if (s == "deposit")    return PoolEventType::DEPOSIT;
  if (s == "withdrawal") return PoolEventType::WITHDRAWAL;
  if (s == "swap")       return PoolEventType::SWAP;
  if (s == "fee_claim")  return PoolEventType::FEE_CLAIM;
  if (s == "checkpoint") return PoolEventType::CHECKPOINT;
  return PoolEventType::CHECKPOINT;
}

std::string poolIdToHex(const PoolId& id) {
  static const char hex[] = "0123456789abcdef";
  std::string out;
  out.reserve(64 + 8);

  for (int i = 0; i < 32; ++i) {
    out += hex[(id.assetB.data[i] >> 4) & 0xf];
    out += hex[id.assetB.data[i] & 0xf];
  }

  out += '_';
  out += hex[(id.feeBps >> 12) & 0xf];
  out += hex[(id.feeBps >> 8) & 0xf];
  out += hex[(id.feeBps >> 4) & 0xf];
  out += hex[id.feeBps & 0xf];

  return out;
}

PoolId poolIdFromHex(const std::string& hex) {
  PoolId id = {};

  if (hex.size() < 64) return id;

  auto hexByte = [](char c) -> uint8_t {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
  };

  for (int i = 0; i < 32 && (i * 2 + 1) < (int)hex.size(); ++i) {
    id.assetB.data[i] = (hexByte(hex[i * 2]) << 4) | hexByte(hex[i * 2 + 1]);
  }

  size_t underscore = hex.find('_');
  if (underscore != std::string::npos && underscore + 4 <= hex.size()) {
    id.feeBps = (hexByte(hex[underscore + 1]) << 12) |
                (hexByte(hex[underscore + 2]) << 8) |
                (hexByte(hex[underscore + 3]) << 4) |
                hexByte(hex[underscore + 4]);
  }

  return id;
}

} // namespace XfgSwap
