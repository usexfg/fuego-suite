// Copyright (c) 2017-2026 Fuego Developers
// Copyright (c) 2020-2026 Elderfire Privacy Group
// Copyright (c) 2013-2026 The Monero Project
// Copyright (c) Shen Noether, Adam Mackenzie, Monero Research Lab
//
// This file is part of Fuego.
//
// Fuego is free & open source software distributed in the hope
// that it will be useful, but WITHOUT ANY WARRANTY; without even
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You may redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation. Fuego includes elements written
// by third parties. See file labeled LICENSE for more details.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.


// 2-layer: layer 0 = spend key, layer 1 = commitment difference (balance proof).

#pragma once

#include "../../include/CryptoTypes.h"

namespace Crypto {

// Generate a 2-layer MLSAG signature.
//
// Layer 0: spend authorization (commitKey or stealth key)
// Layer 1: commitment balance  (C_pseudo - C_ring[i])
//
// pubs:              ring member public keys, array of ring_size
// commitments:       ring member amount commitments (EllipticCurvePoint), array of ring_size
// pseudo_commitment: spender's pseudo-commitment C_pseudo = amount*H + z*G
// key_image:         H_p(pubs[sec_index]) * sec_key
// sec_key:           secret key for layer 0 (spend secret)
// sec_commit:        secret for layer 1 (z - amountMask)
// sec_index:         index of real member in ring
// prefix_hash:       transaction prefix hash (the signed message)
// sig_c0:            [out] initial challenge scalar
// sig_s:             [out] response scalars, array of ring_size * 2
//                    layout: [s[0][0], s[0][1], s[1][0], s[1][1], ...]
//
// Returns false on invalid inputs (bad points, sec_index out of range).
bool generate_mlsag(
    const Hash& prefix_hash,
    const PublicKey* pubs,
    const EllipticCurvePoint* commitments,
    const EllipticCurvePoint& pseudo_commitment,
    const KeyImage& key_image,
    const SecretKey& sec_key,
    const EllipticCurveScalar& sec_commit,
    size_t sec_index,
    size_t ring_size,
    EllipticCurveScalar& sig_c0,
    EllipticCurveScalar* sig_s);

// Verify a 2-layer MLSAG signature.
//
// Same params as generate_mlsag minus the secrets.
// Returns true if the signature is valid.
bool check_mlsag(
    const Hash& prefix_hash,
    const PublicKey* pubs,
    const EllipticCurvePoint* commitments,
    const EllipticCurvePoint& pseudo_commitment,
    const KeyImage& key_image,
    size_t ring_size,
    const EllipticCurveScalar& sig_c0,
    const EllipticCurveScalar* sig_s);

} // namespace Crypto
