// Copyright (c) 2017-2026 Fuego Developers
//
// Standard RFC-8032 ed25519 signature verification using SHA-512.
// The CryptoNote ed25519 variant uses Keccak; this implements the
// standard hash for Solana key verification.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace XfgSwap {

class Ed25519Verify {
public:
  // Verify a standard ed25519 signature.
  // pubkey: 32-byte raw public key
  // message: arbitrary data
  // signature: 64-byte raw signature (R || S)
  static bool verify(const uint8_t pubkey[32],
                     const std::string& message,
                     const uint8_t signature[64]);
};

} // namespace XfgSwap
