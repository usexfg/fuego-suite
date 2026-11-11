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
