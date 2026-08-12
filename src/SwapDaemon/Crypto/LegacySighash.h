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

#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace CryptoNote {
namespace SwapDaemon {
namespace Crypto {

// Legacy (pre-SegWit) Bitcoin sighash computation for P2SH inputs.
//
// Dogecoin is a Bitcoin 0.10-era fork with NO SegWit support, so it does NOT
// use the BIP143 sighash (which the other UTXO chains BTC/LTC/BCH/KMD use).
// Instead it uses the original Bitcoin sighash algorithm that serializes the
// entire transaction with the input's scriptSig replaced by the scriptCode.
//
// Reference: https://en.bitcoin.it/wiki/OP_CHECKSIG#Procedure_for_Hashing_Inputs
//
// Usage:
//   LegacySighash legacy;
//   // For a single-input, single-output transaction (sufficient for HTLC
//   // claim/refund transactions):
//   auto hash = legacy.computeForP2sh(
//       txVersion, nLocktime, nSequence,
//       outpointTxid, outpointVout,   // UTXO being spent
//       redeemScript,                  // the full P2SH redeem script (scriptCode)
//       outputScriptPubKey,            // serialized scriptPubKey of the output
//       outputValueSatoshis);
//
// SIGHASH_ALL = 0x01 is the only supported hashtype (sufficient for the
// single-output HTLC claim/refund transactions used by this codebase).

class LegacySighash {
 public:
  // Compute the legacy sighash for a P2SH input in a transaction with exactly
  // one input and one output (sufficient for HTLC claim/refund transactions).
  //
  // txVersion:      transaction version (1 for Doge HTLC transactions)
  // nLocktime:      transaction locktime (0 for claim, timeoutBlock for refund)
  // nSequence:      input sequence number (0xFFFFFFFE for CLTV-compatible
  //                 refunds, 0xFFFFFFFF for claim inputs)
  // outpointTxid:   32-byte little-endian txid of the UTXO being spent
  // outpointVout:   output index of the UTXO being spent (little-endian uint32)
  // scriptCode:     the redeem script (for P2SH, this is the full redeem script)
  // outputScript:   serialized scriptPubKey of the single output
  // outputValueSats: value sent to the output in satoshis
  //
  // Returns the 32-byte sighash digest that must be signed.
  std::array<uint8_t, 32> computeForP2sh(
      uint32_t txVersion,
      uint32_t nLocktime,
      uint32_t nSequence,
      const std::array<uint8_t, 32>& outpointTxid,  // little-endian
      uint32_t outpointVout,
      const std::vector<uint8_t>& scriptCode,
      const std::vector<uint8_t>& outputScript,
      uint64_t outputValueSats,
      uint32_t sighashType = 0x01);  // SIGHASH_ALL

 private:
  // double-SHA256 helper
  static std::array<uint8_t, 32> hash256(const uint8_t* data, size_t len);

  // Append little-endian uint32/uint64 to a buffer
  static void appendLE32(std::vector<uint8_t>& buf, uint32_t val);
  static void appendLE64(std::vector<uint8_t>& buf, uint64_t val);

  // Bitcoin CompactSize prefix
  static void appendCompactSize(std::vector<uint8_t>& buf, uint64_t len);
};

}  // namespace Crypto
}  // namespace SwapDaemon
}  // namespace CryptoNote
