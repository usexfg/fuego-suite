// Copyright (c) 2018-2026, Fuego Developers
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

#include "LegacySighash.h"

#include <openssl/sha.h>
#include <cstring>

namespace CryptoNote {
namespace SwapDaemon {
namespace Crypto {

// ─── Private helpers ────────────────────────────────────────────────────────

std::array<uint8_t, 32> LegacySighash::hash256(const uint8_t* data, size_t len) {
  // double-SHA256: SHA256(SHA256(data))
  uint8_t first[32];
  SHA256(data, len, first);
  std::array<uint8_t, 32> result;
  SHA256(first, 32, result.data());
  return result;
}

void LegacySighash::appendLE32(std::vector<uint8_t>& buf, uint32_t val) {
  buf.push_back(static_cast<uint8_t>(val & 0xFF));
  buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
  buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
  buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
}

void LegacySighash::appendLE64(std::vector<uint8_t>& buf, uint64_t val) {
  for (int i = 0; i < 8; ++i) {
    buf.push_back(static_cast<uint8_t>((val >> (8 * i)) & 0xFF));
  }
}

void LegacySighash::appendCompactSize(std::vector<uint8_t>& buf, uint64_t len) {
  if (len < 0xFD) {
    buf.push_back(static_cast<uint8_t>(len));
  } else if (len <= 0xFFFF) {
    buf.push_back(0xFD);
    buf.push_back(static_cast<uint8_t>(len & 0xFF));
    buf.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
  } else if (len <= 0xFFFFFFFF) {
    buf.push_back(0xFE);
    buf.push_back(static_cast<uint8_t>(len & 0xFF));
    buf.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((len >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((len >> 24) & 0xFF));
  } else {
    buf.push_back(0xFF);
    appendLE64(buf, len);
  }
}

// ─── Legacy (pre-SegWit) sighash for P2SH, single input + single output ────
//
// The legacy sighash serializes the ENTIRE transaction, replacing the
// scriptSig of the signed input with the scriptCode (the redeem script for
// P2SH). All other inputs have their scriptSig removed (there are none in
// the single-input case). Finally the 4-byte hashtype is appended.
//
// Preimage layout (SIGHASH_ALL):
//   1. nVersion (LE32)
//   2. vin count (CompactSize = 1)
//   3. input:
//      a. outpoint txid (32 bytes LE)
//      b. outpoint vout (LE32)
//      c. scriptCode with CompactSize prefix
//      d. nSequence (LE32)
//   4. vout count (CompactSize = 1)
//   5. output:
//      a. value in satoshis (LE64)
//      b. scriptPubKey with CompactSize prefix
//   6. nLockTime (LE32)
//   7. sighash type (LE32)
//
// Final: double-SHA256 of the preimage.

std::array<uint8_t, 32> LegacySighash::computeForP2sh(
    uint32_t txVersion,
    uint32_t nLocktime,
    uint32_t nSequence,
    const std::array<uint8_t, 32>& outpointTxid,
    uint32_t outpointVout,
    const std::vector<uint8_t>& scriptCode,
    const std::vector<uint8_t>& outputScript,
    uint64_t outputValueSats,
    uint32_t sighashType) {

  std::vector<uint8_t> preimage;

  // 1. nVersion
  appendLE32(preimage, txVersion);

  // 2. vin count
  appendCompactSize(preimage, 1);

  // 3a. outpoint txid (LE)
  preimage.insert(preimage.end(), outpointTxid.begin(), outpointTxid.end());

  // 3b. outpoint vout
  appendLE32(preimage, outpointVout);

  // 3c. scriptCode (redeem script) with CompactSize prefix
  appendCompactSize(preimage, scriptCode.size());
  preimage.insert(preimage.end(), scriptCode.begin(), scriptCode.end());

  // 3d. nSequence
  appendLE32(preimage, nSequence);

  // 4. vout count
  appendCompactSize(preimage, 1);

  // 5a. output value
  appendLE64(preimage, outputValueSats);

  // 5b. scriptPubKey with CompactSize prefix
  appendCompactSize(preimage, outputScript.size());
  preimage.insert(preimage.end(), outputScript.begin(), outputScript.end());

  // 6. nLockTime
  appendLE32(preimage, nLocktime);

  // 7. sighash type (SIGHASH_ALL = 0x01, no fork ID for Doge)
  appendLE32(preimage, sighashType);

  // Final: double-SHA256 of the preimage
  return hash256(preimage.data(), preimage.size());
}

}  // namespace Crypto
}  // namespace SwapDaemon
}  // namespace CryptoNote
