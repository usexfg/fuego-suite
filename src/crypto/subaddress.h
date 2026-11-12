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

#pragma once

#include <cstdint>
#include <cstring>
#include "crypto.h"
#include "hash.h"

// Sub-address key derivation
//
//   Master keys: a = viewSecretKey,  b = spendSecretKey
//                A = a*G (viewPublicKey), B = b*G (spendPublicKey)
//
//   V1 (legacy `fire` prefix, linkable):
//     m    = H_s("Sublime" || a || i_LE32 || j_LE32)
//     D_ij = B + m*G
//     C_ij = A
//     b_ij = b + m
//     Sender: R = r*G. Receiver: a*R = r*A.
//
//   V2 (`funsa` prefix, unlinkable):
//     m    = H_s("FuegoSubAddrV2" || a || i_LE32 || j_LE32)
//     D_ij = B + m*G
//     C_ij = a*D_ij     (no cofactor-8)
//     b_ij = b + m
//     Sender: R = r*D_ij. Receiver: a*R = r*C_ij.
//
// The index (0, 0) is reserved for the master address — do not create sub-addresses at (0,0).
//
// Janus Attack Mitigation (V2 only):
//
// The Janus attack allows an attacker holding two subaddresses D_ij and D_kj from the
// same wallet to link them by checking if b_kj * R == R_kj where R = r * D_ij.
//
// The mitigation uses TX_EXTRA_JANUS_BASE (tag 0x09) which carries janusBase = r * G.
// The receiver performs janusCheck: verify subSpendSecret * janusBase == txPubKey.
//
// Mathematical proof of correctness:
//
// For a single-subaddress V2 tx, the sender computes:
//   R = r * D_ij        (transaction public key)
//   janusBase = r * G    (Janus base, stored in extra)
//
// The receiver checks:
//   subSpendSecret * janusBase == txPubKey
//   (b + m) * (r * G) == r * D_ij
//   (b + m) * r * G == r * (B + m * G)
//   r * (b + m) * G == r * B + r * m * G
//   r * b * G + r * m * G == r * B + r * m * G
//   Since B = b * G:  r * B + r * m * G == r * B + r * m * G  (QED)
//
// For an attacker with a different subaddress (different m'):
//   (b + m') * r * G = r * B + r * m' * G != r * B + r * m * G
//   unless m' == m, which means the attacker holds the same subaddress.
//
// Therefore, the Janus check correctly distinguishes between the legitimate receiver
// and any attacker probing with a different subaddress from the same wallet.

namespace Crypto {

const uint8_t SUBADDRESS_VERSION_V1 = 1;
const uint8_t SUBADDRESS_VERSION_V2 = 2;

struct SubAddressKeys {
  PublicKey spendPublicKey;
  PublicKey viewPublicKey;
  SecretKey spendSecretKey;
  bool hasSpendSecretKey;
  uint8_t version;
};

SubAddressKeys deriveSubAddressKeys(
    const SecretKey&  masterViewSecretKey,
    const PublicKey&  masterSpendPublicKey,
    const SecretKey*  masterSpendSecretKey,
    uint32_t major,
    uint32_t minor);

SubAddressKeys deriveSubAddressKeysV2(
    const SecretKey&  masterViewSecretKey,
    const PublicKey&  masterSpendPublicKey,
    const SecretKey*  masterSpendSecretKey,
    uint32_t major,
    uint32_t minor);

bool scalarmultKeyNoCofactor(
    const PublicKey& point,
    const SecretKey& scalar,
    PublicKey& result);

bool subaddressTxPubKey(
    const SecretKey& txSecret,
    const PublicKey& subSpendPublicKey,
    PublicKey& txPubKey);

bool janusCheck(
    const PublicKey& txPubKey,
    const PublicKey& janusBase,
    const SecretKey& subSpendSecret);

} // namespace Crypto
