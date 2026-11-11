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

#include "crypto/crypto.h"
#include <string>

namespace XfgSwap {

// Counterparty HTLC hashlock derivation.
//
// claim() reveals the adaptor secret t as the HTLC preimage, and the
// counterparty programs verify H(preimage) == hash_lock. So the hashlock
// committed at lock() time must be H(t) — NOT the adaptor point T = t*G.
//
// The hash function must match the counterparty program exactly:
//   - Solana xfg_htlc verifies keccak256(preimage)  -> solHashLockHex
//   - BCH P2SH HTLC verifies OP_SHA256 (single SHA-256) -> bchHashLockHex
//
// Both return the 64-char lowercase hex of the 32-byte digest of the raw
// 32-byte secret.

// keccak256(adaptorSecret) — Solana HTLC hashlock.
std::string solHashLockHex(const Crypto::SecretKey& adaptorSecret);

// keccak256(adaptorSecret) — EVM HashedTimelock hashlock (same digest as Solana).
// Solidity claim: keccak256(abi.encodePacked(preimage)) == hashLock.
inline std::string ethHashLockHex(const Crypto::SecretKey& adaptorSecret) {
  return solHashLockHex(adaptorSecret);
}

// sha256(adaptorSecret) — BCH HTLC hashlock.
std::string bchHashLockHex(const Crypto::SecretKey& adaptorSecret);

// sha256(adaptorSecret) — Doge HTLC hashlock.
// Dogecoin is a pre-SegWit Bitcoin fork whose P2SH HTLC uses the same
// OP_SHA256 hashlock construction as BCH, so the digest is identical.
inline std::string dogeHashLockHex(const Crypto::SecretKey& adaptorSecret) {
  return bchHashLockHex(adaptorSecret);
}

// sha256(adaptorSecret) — Dash HTLC hashlock (pre-SegWit Bitcoin fork).
inline std::string dashHashLockHex(const Crypto::SecretKey& adaptorSecret) {
  return bchHashLockHex(adaptorSecret);
}

// sha256(adaptorSecret) — Zcash transparent HTLC hashlock.
inline std::string zecHashLockHex(const Crypto::SecretKey& adaptorSecret) {
  return bchHashLockHex(adaptorSecret);
}

} // namespace XfgSwap
