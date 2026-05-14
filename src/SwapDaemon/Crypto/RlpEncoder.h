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

#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <stdexcept>

namespace CryptoNote {
namespace SwapDaemon {
namespace Crypto {

// Minimal RLP encoder covering exactly what EIP-155 transaction signing needs:
//   - byte strings (arbitrary length, including empty)
//   - unsigned integers (encoded as big-endian byte strings with no leading zeros;
//     0 encodes as the empty byte string per Ethereum convention)
//   - a single top-level list wrapping the nine EIP-155 fields
//
// Usage:
//   RlpEncoder enc;
//   enc.beginList();
//   enc.writeUint(nonce);
//   enc.writeBytes(toAddress);   // 20-byte address
//   ...
//   enc.endList();
//   std::vector<uint8_t> encoded = enc.finalize();

class RlpEncoder {
 public:
  RlpEncoder();

  // Start a list.  Only one nesting level is supported (sufficient for a
  // flat EIP-155 transaction).  Throws if already inside a list.
  void beginList();

  // End the current list.  Throws if not inside a list.
  void endList();

  // Encode an unsigned 64-bit integer.
  // 0 is encoded as the empty byte string (RLP convention for uint 0).
  void writeUint(uint64_t value);

  // Encode an unsigned 256-bit integer given as 32 big-endian bytes.
  // Leading zero bytes are stripped as required by RLP.
  void writeUint256(const uint8_t* bytes32);

  // Encode a raw byte string (may be empty).
  void writeBytes(const std::vector<uint8_t>& data);
  void writeBytes(const uint8_t* data, size_t len);

  // Return the fully encoded RLP bytes.
  // Must be called after exactly one complete beginList/endList pair
  // (or with no list for a single item).
  std::vector<uint8_t> finalize() const;

 private:
  // Append RLP length prefix for a string of `contentLen` bytes.
  void appendStringPrefix(size_t contentLen);
  // Append RLP length prefix for a list whose encoded content is `contentLen` bytes.
  void appendLengthPrefix(uint8_t shortBase, size_t contentLen,
                          std::vector<uint8_t>& out);

  bool m_inList = false;
  std::vector<uint8_t> m_listContent;  // accumulates items while inside a list
  std::vector<uint8_t> m_top;          // accumulates top-level items
};

}  // namespace Crypto
}  // namespace SwapDaemon
}  // namespace CryptoNote
