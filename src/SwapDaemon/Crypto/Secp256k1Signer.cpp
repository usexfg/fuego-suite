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

#include "Secp256k1Signer.h"

#include <secp256k1.h>
#include <secp256k1_recovery.h>

#include <stdexcept>
#include <cstring>

namespace CryptoNote {
namespace SwapDaemon {
namespace Crypto {

Secp256k1Signer::Secp256k1Signer()
    : m_ctx(secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY)) {
  if (!m_ctx) {
    throw std::runtime_error("Secp256k1Signer: failed to create secp256k1 context");
  }
}

Secp256k1Signer::~Secp256k1Signer() {
  if (m_ctx) {
    secp256k1_context_destroy(static_cast<secp256k1_context*>(m_ctx));
    m_ctx = nullptr;
  }
}

RecoverableSignature Secp256k1Signer::signRecoverable(
    const std::array<uint8_t, 32>& msgHash,
    const std::array<uint8_t, 32>& privKey) {
  auto* ctx = static_cast<secp256k1_context*>(m_ctx);

  secp256k1_ecdsa_recoverable_signature rawSig;
  if (!secp256k1_ecdsa_sign_recoverable(ctx,
                                        &rawSig,
                                        msgHash.data(),
                                        privKey.data(),
                                        /*noncefp=*/nullptr,
                                        /*ndata=*/nullptr)) {
    throw std::runtime_error("Secp256k1Signer::signRecoverable: signing failed (invalid private key?)");
  }

  // Serialize to compact form: 64 bytes (r||s) + 1-byte recid.
  uint8_t compact[64];
  int recid = 0;
  secp256k1_ecdsa_recoverable_signature_serialize_compact(ctx, compact, &recid, &rawSig);

  // Normalize s to low-S form (BIP-62) — prevents signature malleability.
  // secp256k1_ecdsa_signature_normalize works on a non-recoverable sig, so
  // we convert, normalize, then copy r and s back.
  secp256k1_ecdsa_signature nonRecovSig;
  secp256k1_ecdsa_recoverable_signature_convert(ctx, &nonRecovSig, &rawSig);
  secp256k1_ecdsa_signature_normalize(ctx, &nonRecovSig, &nonRecovSig);

  uint8_t normalizedCompact[64];
  secp256k1_ecdsa_signature_serialize_compact(ctx, normalizedCompact, &nonRecovSig);

  RecoverableSignature result;
  std::memcpy(result.r.data(), normalizedCompact,      32);
  std::memcpy(result.s.data(), normalizedCompact + 32, 32);
  result.recid = static_cast<uint8_t>(recid);
  return result;
}

std::vector<uint8_t> Secp256k1Signer::derivePublicKey(
    const std::array<uint8_t, 32>& privKey) {
  auto* ctx = static_cast<secp256k1_context*>(m_ctx);

  secp256k1_pubkey pubkey;
  if (!secp256k1_ec_pubkey_create(ctx, &pubkey, privKey.data())) {
    throw std::runtime_error("Secp256k1Signer::derivePublicKey: invalid private key");
  }

  std::vector<uint8_t> out(65);
  size_t outLen = 65;
  secp256k1_ec_pubkey_serialize(ctx, out.data(), &outLen, &pubkey, SECP256K1_EC_UNCOMPRESSED);
  out.resize(outLen);
  return out;
}

std::vector<uint8_t> Secp256k1Signer::derivePublicKeyCompressed(
    const std::array<uint8_t, 32>& privKey) {
  auto* ctx = static_cast<secp256k1_context*>(m_ctx);

  secp256k1_pubkey pubkey;
  if (!secp256k1_ec_pubkey_create(ctx, &pubkey, privKey.data())) {
    throw std::runtime_error("Secp256k1Signer::derivePublicKeyCompressed: invalid private key");
  }

  std::vector<uint8_t> out(33);
  size_t outLen = 33;
  secp256k1_ec_pubkey_serialize(ctx, out.data(), &outLen, &pubkey, SECP256K1_EC_COMPRESSED);
  out.resize(outLen);
  return out;
}

}  // namespace Crypto
}  // namespace SwapDaemon
}  // namespace CryptoNote
