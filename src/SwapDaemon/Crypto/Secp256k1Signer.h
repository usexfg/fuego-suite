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
#include <cstdint>
#include <vector>

namespace CryptoNote {
namespace SwapDaemon {
namespace Crypto {

// Recoverable ECDSA signature over secp256k1.
// r and s are in big-endian byte order, s is normalised to low-S form (BIP-62).
struct RecoverableSignature {
  std::array<uint8_t, 32> r;
  std::array<uint8_t, 32> s;
  uint8_t recid;  // recovery id: 0 or 1
};

// Thin RAII wrapper over the bitcoin-core/secp256k1 C library.
// One context is kept per instance; the context is allocated with
// SECP256K1_CONTEXT_SIGN so it is inexpensive to construct.
// Thread-safety: sign/verify calls on the same context are thread-safe
// after construction (secp256k1 guarantees this).
class Secp256k1Signer {
 public:
  Secp256k1Signer();
  ~Secp256k1Signer();

  // Secp256k1Signer is not copyable (owns a secp256k1_context*).
  Secp256k1Signer(const Secp256k1Signer&) = delete;
  Secp256k1Signer& operator=(const Secp256k1Signer&) = delete;

  // Sign msgHash (32 bytes) with privKey (32 bytes).
  // Returns a recoverable signature with s in low-S form.
  // Throws std::runtime_error on invalid privKey.
  RecoverableSignature signRecoverable(
      const std::array<uint8_t, 32>& msgHash,
      const std::array<uint8_t, 32>& privKey);

  // Derive the 65-byte uncompressed public key (0x04 || X || Y) from privKey.
  // Throws std::runtime_error on invalid privKey.
  std::vector<uint8_t> derivePublicKey(
      const std::array<uint8_t, 32>& privKey);

  // Derive the 33-byte compressed public key (0x02/0x03 || X) from privKey.
  // Required for BCH P2PKH inputs.
  // Throws std::runtime_error on invalid privKey.
  std::vector<uint8_t> derivePublicKeyCompressed(
      const std::array<uint8_t, 32>& privKey);

 private:
  void* m_ctx;  // secp256k1_context*
};

}  // namespace Crypto
}  // namespace SwapDaemon
}  // namespace CryptoNote
