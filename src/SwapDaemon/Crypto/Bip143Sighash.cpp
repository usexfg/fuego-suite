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

#include "Bip143Sighash.h"

#include <openssl/sha.h>
#include <cstring>

namespace CryptoNote {
namespace SwapDaemon {
namespace Crypto {

// ─── Private helpers ────────────────────────────────────────────────────────

std::array<uint8_t, 32> Bip143Sighash::hash256(const uint8_t* data, size_t len) {
  // double-SHA256: SHA256(SHA256(data))
  uint8_t first[32];
  SHA256(data, len, first);
  std::array<uint8_t, 32> result;
  SHA256(first, 32, result.data());
  return result;
}

void Bip143Sighash::appendLE32(std::vector<uint8_t>& buf, uint32_t val) {
  buf.push_back(static_cast<uint8_t>(val & 0xFF));
  buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
  buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
  buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
}

void Bip143Sighash::appendLE64(std::vector<uint8_t>& buf, uint64_t val) {
  for (int i = 0; i < 8; ++i) {
    buf.push_back(static_cast<uint8_t>((val >> (8 * i)) & 0xFF));
  }
}

void Bip143Sighash::appendCompactSize(std::vector<uint8_t>& buf, uint64_t len) {
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

// ─── BIP143 sighash for P2SH, single input + single output ─────────────────
//
// Digest components per BIP143 §Specification:
//   1. nVersion (LE32)
//   2. hashPrevouts = dSHA256( outpoint || ... for all inputs )
//   3. hashSequence = dSHA256( nSequence || ... for all inputs )
//   4. outpoint (txid LE32 || vout LE32)
//   5. scriptCode with compactSize prefix
//   6. value of the UTXO in satoshis (LE64)
//   7. nSequence of the input (LE32)
//   8. hashOutputs = dSHA256( output amount LE64 || scriptPubKey with compactSize prefix )
//   9. nLocktime (LE32)
//  10. sighash type (LE32)

std::array<uint8_t, 32> Bip143Sighash::computeForP2sh(
    uint32_t txVersion,
    uint32_t nLocktime,
    uint32_t nSequence,
    const std::array<uint8_t, 32>& outpointTxid,
    uint32_t outpointVout,
    const std::vector<uint8_t>& scriptCode,
    uint64_t utxoValueSats,
    const std::vector<uint8_t>& outputScript,
    uint64_t outputValueSats,
    uint32_t sighashType) {

  // ── Component 2: hashPrevouts ──────────────────────────────────────────
  // For SIGHASH_ALL, hash all outpoints: txid (LE) || vout (LE32).
  // With one input this is just dSHA256(txid || vout).
  std::vector<uint8_t> prevoutsBuf;
  prevoutsBuf.insert(prevoutsBuf.end(), outpointTxid.begin(), outpointTxid.end());
  appendLE32(prevoutsBuf, outpointVout);
  auto hashPrevouts = hash256(prevoutsBuf.data(), prevoutsBuf.size());

  // ── Component 3: hashSequence ──────────────────────────────────────────
  // For SIGHASH_ALL, hash all input sequences.
  std::vector<uint8_t> seqBuf;
  appendLE32(seqBuf, nSequence);
  auto hashSequence = hash256(seqBuf.data(), seqBuf.size());

  // ── Component 8: hashOutputs ───────────────────────────────────────────
  // For SIGHASH_ALL, hash all outputs: amount (LE64) || scriptPubKey.
  std::vector<uint8_t> outputsBuf;
  appendLE64(outputsBuf, outputValueSats);
  appendCompactSize(outputsBuf, outputScript.size());
  outputsBuf.insert(outputsBuf.end(), outputScript.begin(), outputScript.end());
  auto hashOutputs = hash256(outputsBuf.data(), outputsBuf.size());

  // ── Assemble the full preimage ─────────────────────────────────────────
  std::vector<uint8_t> preimage;

  // 1. nVersion
  appendLE32(preimage, txVersion);

  // 2. hashPrevouts
  preimage.insert(preimage.end(), hashPrevouts.begin(), hashPrevouts.end());

  // 3. hashSequence
  preimage.insert(preimage.end(), hashSequence.begin(), hashSequence.end());

  // 4. outpoint (txid LE || vout LE32)
  preimage.insert(preimage.end(), outpointTxid.begin(), outpointTxid.end());
  appendLE32(preimage, outpointVout);

  // 5. scriptCode with compactSize prefix
  appendCompactSize(preimage, scriptCode.size());
  preimage.insert(preimage.end(), scriptCode.begin(), scriptCode.end());

  // 6. value of UTXO (LE64)
  appendLE64(preimage, utxoValueSats);

  // 7. nSequence of this input (LE32)
  appendLE32(preimage, nSequence);

  // 8. hashOutputs
  preimage.insert(preimage.end(), hashOutputs.begin(), hashOutputs.end());

  // 9. nLocktime (LE32)
  appendLE32(preimage, nLocktime);

  // 10. sighash type (LE32)
  appendLE32(preimage, sighashType);

  // Final: double-SHA256 of the preimage
  return hash256(preimage.data(), preimage.size());
}

}  // namespace Crypto
}  // namespace SwapDaemon
}  // namespace CryptoNote
