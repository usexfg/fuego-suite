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
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace CryptoNote {
namespace SwapDaemon {
namespace Crypto {

// BIP143 (BCH variant) sighash computation for P2SH inputs.
//
// BCH uses SIGHASH_ALL | SIGHASH_FORKID = 0x41, which changes the digest
// algorithm to include the UTXO value and eliminates quadratic hashing.
//
// Reference: https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/replay-protected-sighash.md
//
// Usage:
//   Bip143Sighash bip143;
//   // For a single-input, single-output transaction:
//   auto hash = bip143.computeForP2sh(
//       txVersion, nLocktime, nSequence,
//       outpointTxid, outpointVout,   // UTXO being spent
//       redeemScript,                  // the full P2SH redeem script (scriptCode)
//       utxoValueSatoshis,
//       outputScriptPubKey,            // serialized scriptPubKey of the output
//       outputValueSatoshis);

class Bip143Sighash {
 public:
  // Compute the BIP143 sighash for a P2SH input in a transaction with exactly
  // one input and one output (sufficient for HTLC claim/refund transactions).
  //
  // txVersion:      transaction version (1 for BCH HTLC transactions)
  // nLocktime:      transaction locktime (0 for claim, timeoutBlock for refund)
  // nSequence:      input sequence number (0xFFFFFFFE for CLTV-compatible inputs,
  //                 0xFFFFFFFF for non-CLTV claim inputs)
  // outpointTxid:   32-byte little-endian txid of the UTXO being spent
  // outpointVout:   output index of the UTXO being spent (little-endian uint32)
  // scriptCode:     the redeem script (for P2SH, this is the full redeem script;
  //                 must NOT include the OP_CODESEPARATOR prefix)
  // utxoValueSats:  value of the UTXO in satoshis (required by BIP143)
  // outputScript:   serialized scriptPubKey of the single output
  // outputValueSats: value sent to the output in satoshis
  // sighashType:    typically 0x41 (SIGHASH_ALL | SIGHASH_FORKID for BCH)
  //
  // Returns the 32-byte sighash digest that must be signed.
  std::array<uint8_t, 32> computeForP2sh(
      uint32_t txVersion,
      uint32_t nLocktime,
      uint32_t nSequence,
      const std::array<uint8_t, 32>& outpointTxid,  // little-endian
      uint32_t outpointVout,
      const std::vector<uint8_t>& scriptCode,
      uint64_t utxoValueSats,
      const std::vector<uint8_t>& outputScript,
      uint64_t outputValueSats,
      uint32_t sighashType = 0x41);  // SIGHASH_ALL | SIGHASH_FORKID

 private:
  // double-SHA256 helper
  static std::array<uint8_t, 32> hash256(const uint8_t* data, size_t len);

  // Append little-endian uint32/uint64 to a buffer
  static void appendLE32(std::vector<uint8_t>& buf, uint32_t val);
  static void appendLE64(std::vector<uint8_t>& buf, uint64_t val);

  // BIP143 compact-size prefix
  static void appendCompactSize(std::vector<uint8_t>& buf, uint64_t len);
};

}  // namespace Crypto
}  // namespace SwapDaemon
}  // namespace CryptoNote
