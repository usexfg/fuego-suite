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

#include "ContractAbi.h"

#include <cstring>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <algorithm>

extern "C" {
#include "crypto/keccak.h"
}

namespace XfgSwap {
namespace EthAbi {

// ---------------------------------------------------------------------------
// Internal hex helpers
// ---------------------------------------------------------------------------

static uint8_t hexCharToNibble(char c) {
  if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
  if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
  if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
  return 0;
}

// Convert a hex string (with or without 0x prefix) to raw bytes
static std::vector<uint8_t> hexToBytes(const std::string& hex) {
  std::string s = hex;
  if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    s = s.substr(2);
  }
  // Ensure even length
  if (s.size() % 2 != 0) {
    s = "0" + s;
  }
  std::vector<uint8_t> bytes(s.size() / 2);
  for (size_t i = 0; i < bytes.size(); ++i) {
    bytes[i] = (hexCharToNibble(s[2 * i]) << 4) | hexCharToNibble(s[2 * i + 1]);
  }
  return bytes;
}

// Convert raw bytes to lowercase hex string (no 0x prefix)
static std::string bytesToHex(const uint8_t* data, size_t len) {
  std::ostringstream oss;
  for (size_t i = 0; i < len; ++i) {
    oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(data[i]);
  }
  return oss.str();
}

// Parse a 64-char hex chunk into a uint64_t (big-endian, takes low 8 bytes)
static uint64_t hexChunkToUint64(const std::string& chunk) {
  // chunk is 64 hex chars = 32 bytes; uint256 big-endian, we take last 8 bytes
  if (chunk.size() < 64) return 0;
  // Last 16 hex chars = 8 bytes
  std::string low = chunk.substr(48, 16);
  uint64_t result = 0;
  for (char c : low) {
    result <<= 4;
    result |= hexCharToNibble(c);
  }
  return result;
}

// Parse a 64-char hex chunk into a bool (last byte != 0)
static bool hexChunkToBool(const std::string& chunk) {
  if (chunk.size() < 64) return false;
  return chunk[63] != '0';
}

// Parse a 64-char hex chunk into a 20-byte Ethereum address (last 40 hex chars)
static std::string hexChunkToAddress(const std::string& chunk) {
  if (chunk.size() < 64) return "";
  // Address is in the last 40 hex chars (20 bytes), left-padded with 24 hex zeros
  return "0x" + chunk.substr(24, 40);
}

// Parse a 64-char hex chunk into a Crypto::Hash (32 bytes, direct mapping)
static Crypto::Hash hexChunkToHash(const std::string& chunk) {
  Crypto::Hash h;
  std::memset(&h, 0, sizeof(h));
  if (chunk.size() < 64) return h;
  auto bytes = hexToBytes(chunk);
  if (bytes.size() >= 32) {
    std::memcpy(h.data, bytes.data(), 32);
  }
  return h;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::string functionSelector(const std::string& signature) {
  // keccak256 of the ASCII signature, take first 4 bytes (Ethereum ABI)
  uint8_t h[32];
  keccak(reinterpret_cast<const uint8_t*>(signature.data()),
         static_cast<int>(signature.size()), h, 32);
  // Return as hex with 0x prefix (8 hex chars = 4 bytes)
  return "0x" + bytesToHex(h, 4);
}

std::string padAddress(const std::string& addr) {
  // addr is "0x" + 40 hex chars. Strip prefix, left-pad to 64 hex chars.
  std::string hex = addr;
  if (hex.size() >= 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) {
    hex = hex.substr(2);
  }
  // Remove leading zeros beyond 40 chars, ensure exactly 40
  while (hex.size() < 40) hex = "0" + hex;

  // Left-pad to 64 hex chars (32 bytes) with zeros
  std::string padded(64 - hex.size(), '0');
  padded += hex;
  return padded;
}

std::string encodeUint256(uint64_t value) {
  // 32 bytes big-endian, value in last 8 bytes
  std::ostringstream oss;
  // 24 zero bytes = 48 hex zeros
  oss << std::string(48, '0');
  oss << std::hex << std::setfill('0') << std::setw(16) << value;
  return oss.str();
}

std::string encodeBytes32(const Crypto::Hash& h) {
  return bytesToHex(h.data, 32);
}

std::string encodeLock(const std::string& recipientAddr, const Crypto::Hash& hashLock,
                       uint64_t timeoutBlock) {
  // lock(address,bytes32,uint256)
  std::string selector = functionSelector("lock(address,bytes32,uint256)");
  // Strip 0x from selector
  std::string sel = selector.substr(2);

  std::string encoded = "0x" + sel
    + padAddress(recipientAddr)
    + encodeBytes32(hashLock)
    + encodeUint256(timeoutBlock);

  return encoded;
}

std::string encodeClaim(const std::string& contractId, const Crypto::Hash& preimage) {
  // claim(bytes32,bytes32)
  std::string selector = functionSelector("claim(bytes32,bytes32)");
  std::string sel = selector.substr(2);

  // contractId is a hex string (with or without 0x); pad/convert to 32 bytes
  auto cidBytes = hexToBytes(contractId);
  uint8_t cidBuf[32] = {};
  if (cidBytes.size() >= 32) {
    std::memcpy(cidBuf, cidBytes.data(), 32);
  } else {
    // Right-align in 32 bytes
    std::memcpy(cidBuf + (32 - cidBytes.size()), cidBytes.data(), cidBytes.size());
  }

  std::string encoded = "0x" + sel
    + bytesToHex(cidBuf, 32)
    + encodeBytes32(preimage);

  return encoded;
}

std::string encodeRefund(const std::string& contractId) {
  // refund(bytes32)
  std::string selector = functionSelector("refund(bytes32)");
  std::string sel = selector.substr(2);

  auto cidBytes = hexToBytes(contractId);
  uint8_t cidBuf[32] = {};
  if (cidBytes.size() >= 32) {
    std::memcpy(cidBuf, cidBytes.data(), 32);
  } else {
    std::memcpy(cidBuf + (32 - cidBytes.size()), cidBytes.data(), cidBytes.size());
  }

  return "0x" + sel + bytesToHex(cidBuf, 32);
}

std::string encodeGetContract(const std::string& contractId) {
  // getContract(bytes32)
  std::string selector = functionSelector("getContract(bytes32)");
  std::string sel = selector.substr(2);

  auto cidBytes = hexToBytes(contractId);
  uint8_t cidBuf[32] = {};
  if (cidBytes.size() >= 32) {
    std::memcpy(cidBuf, cidBytes.data(), 32);
  } else {
    std::memcpy(cidBuf + (32 - cidBytes.size()), cidBytes.data(), cidBytes.size());
  }

  return "0x" + sel + bytesToHex(cidBuf, 32);
}

bool decodeGetContract(const std::string& hexData, ContractInfo& info) {
  // getContract returns 8 values, each 32 bytes = 256 bytes = 512 hex chars
  // Return layout (ABI-encoded tuple):
  //   [0]  address sender     (left-padded to 32 bytes)
  //   [1]  address recipient  (left-padded to 32 bytes)
  //   [2]  uint256 amount
  //   [3]  bytes32 hashLock
  //   [4]  uint256 timeoutBlock
  //   [5]  bool    claimed
  //   [6]  bool    refunded
  //   [7]  bytes32 preimage

  std::string data = hexData;
  if (data.size() >= 2 && data[0] == '0' && (data[1] == 'x' || data[1] == 'X')) {
    data = data.substr(2);
  }

  // Need at least 8 * 64 = 512 hex chars
  if (data.size() < 512) return false;

  // Extract each 64-char chunk
  std::string chunks[8];
  for (int i = 0; i < 8; ++i) {
    chunks[i] = data.substr(static_cast<size_t>(i) * 64, 64);
  }

  info.sender       = hexChunkToAddress(chunks[0]);
  info.recipient    = hexChunkToAddress(chunks[1]);
  info.amount       = hexChunkToUint64(chunks[2]);
  info.hashLock     = hexChunkToHash(chunks[3]);
  info.timeoutBlock = hexChunkToUint64(chunks[4]);
  info.claimed      = hexChunkToBool(chunks[5]);
  info.refunded     = hexChunkToBool(chunks[6]);
  info.preimage     = hexChunkToHash(chunks[7]);

  return true;
}

} // namespace EthAbi
} // namespace XfgSwap
