// Copyright (c) 2017-2026 Fuego Developers
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

#pragma once

#include "SwapTypes.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace XfgSwap {

// ── Envelope versions ───────────────────────────────────────────────────────
// v1: ChaCha8 + HMAC-SHA256 Encrypt-then-MAC. Retained so swaps already on disk
//     keep working across the upgrade — a swap interrupted mid-flight must still
//     recover its adaptor secret, or the funds are stuck until the timelock.
// v2: ChaCha20-Poly1305 AEAD. One primitive instead of a cipher plus a
//     hand-rolled MAC, and roughly 2.5x the security margin (ChaCha8 is an
//     8-round variant of a 20-round cipher).
constexpr uint8_t SECRET_ENVELOPE_V1 = 1;
constexpr uint8_t SECRET_ENVELOPE_V2 = 2;

constexpr size_t CHACHA8_KEY_SIZE    = 32;
constexpr size_t CHACHA8_NONCE_SIZE  = 8;    // v1 only — retained to read old records
constexpr size_t CHACHA20_KEY_SIZE   = 32;
constexpr size_t CHACHA20_NONCE_SIZE = 12;
constexpr size_t SALT_SIZE           = 16;
constexpr size_t TAG_SIZE            = 32;   // v1 HMAC-SHA256 tag
constexpr size_t POLY1305_TAG_SIZE   = 16;   // v2 AEAD tag (carried inside ciphertext)
constexpr size_t SECRET_PLAINTEXT    = 32;

// Serialised envelope layouts. Encoded by SwapStateMachine when it concatenates
// fields, so the sizes live next to the definitions that produce them rather
// than being repeated as arithmetic at each call site.
constexpr size_t V1_ENVELOPE_OVERHEAD = CHACHA8_NONCE_SIZE + SALT_SIZE + TAG_SIZE;
constexpr size_t V2_ENVELOPE_OVERHEAD = 1 + CHACHA20_NONCE_SIZE + SALT_SIZE; // + version byte

class SwapSecretEncryption {
public:
  struct EncryptedSecret {
    uint8_t                              version = SECRET_ENVELOPE_V2;
    std::array<uint8_t, CHACHA20_NONCE_SIZE> nonce{};
    std::array<uint8_t, SALT_SIZE>          salt{};
    std::vector<uint8_t>                    ciphertext;  // v2: includes the Poly1305 tag
    std::array<uint8_t, TAG_SIZE>           tag{};       // v1 only
  };

  static bool encrypt(
    const Crypto::SecretKey& plaintext,
    const std::string& encryptionKey,
    EncryptedSecret& out
  );

  static bool decrypt(
    const EncryptedSecret& encrypted,
    const std::string& encryptionKey,
    Crypto::SecretKey& out
  );

  // Generic byte-buffer variants (for secrets wider than SecretKey, e.g.
  // the 64-byte Musig2 secret nonce). Format identical to the 32-byte API.
  static bool encrypt(
    const uint8_t* plaintext,
    size_t plaintextLen,
    const std::string& encryptionKey,
    EncryptedSecret& out
  );

  static bool decrypt(
    const EncryptedSecret& encrypted,
    const std::string& encryptionKey,
    uint8_t* out,
    size_t outLen
  );

  // Serialise / parse the on-disk blob. Centralised so the pack and unpack
  // paths cannot drift: these were previously hand-inlined at three call sites
  // with the ciphertext length hardcoded, which mis-parsed any secret whose
  // plaintext was not 32 bytes.
  static std::vector<uint8_t> encode(const EncryptedSecret& s);
  static bool decode(const std::vector<uint8_t>& blob, size_t plaintextLen, EncryptedSecret& out);

  // Wipe that survives dead-store elimination. Public because the file-local v1
  // compatibility helpers below need it too.
  static void secureZero(void* buf, size_t len);
};

} // namespace XfgSwap
