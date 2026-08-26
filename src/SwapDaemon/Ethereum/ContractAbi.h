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

#include <string>
#include <cstdint>
#include "../../crypto/hash.h"

namespace XfgSwap {
namespace EthAbi {

// Encode a function selector: first 4 bytes of keccak256(signature)
std::string functionSelector(const std::string& signature);

// ABI-encode parameters for lock(address,bytes32,uint256) (HTLC)
std::string encodeLock(const std::string& recipientAddr, const Crypto::Hash& hashLock, uint64_t timeoutBlock);

// ABI-encode parameters for lock(address,address,uint256) (PointTimelock)
std::string encodeLockPoint(const std::string& recipientAddr, const std::string& pointAddr, uint64_t timeoutBlock);

// ABI-encode parameters for claim(bytes32,bytes32)
std::string encodeClaim(const std::string& contractId, const Crypto::Hash& preimage);

// ABI-encode parameters for refund(bytes32)
std::string encodeRefund(const std::string& contractId);

// ABI-encode parameters for getContract(bytes32) -- view call
std::string encodeGetContract(const std::string& contractId);

// Decode getContract response (HTLC)
struct ContractInfo {
  std::string sender;
  std::string recipient;
  uint64_t amount;
  Crypto::Hash hashLock;
  uint64_t timeoutBlock;
  bool claimed;
  bool refunded;
  Crypto::Hash preimage;
};
bool decodeGetContract(const std::string& hexData, ContractInfo& info);

// Decode getContract response (PointTimelock)
struct PointContractInfo {
  std::string sender;
  std::string recipient;
  uint64_t amount;
  std::string pointAddress;
  uint64_t timeoutBlock;
  bool claimed;
  bool refunded;
  Crypto::Hash secret;   // canonical BIG-endian scalar as revealed on-chain
};
bool decodeGetContractPoint(const std::string& hexData, PointContractInfo& info);

// Helper: pad a 20-byte address to 32 bytes (left-pad with zeros)
std::string padAddress(const std::string& addr);

// Helper: encode uint256 as 32-byte hex
std::string encodeUint256(uint64_t value);

// Helper: encode bytes32 from Crypto::Hash
std::string encodeBytes32(const Crypto::Hash& h);

// Point address derivation helpers for PointTimelock (secp256k1).
//
// ENDIAN CANONICAL RULE: CryptoNote SecretKey scalars are stored in memory
// LITTLE-endian; the secp256k1 library (and every Solidity uint256) reads
// scalars BIG-endian. Canonical rule throughout this ABI layer:
//   secp-domain scalar bytes = byte-reversed CryptoNote scalar.
// This is safe because cross-curve scalar reuse guarantees t < l_ed25519 < n_secp,
// so the same mathematical scalar is valid on both curves; only the byte order
// of its serialization differs.

// Derive the Ethereum address (0x + 40 hex) of a serialized secp256k1 public key.
// Accepts 33-byte compressed, 64-byte raw (x||y), or 65-byte uncompressed input.
std::string derivePointAddress(const uint8_t* pubkeyBytes, size_t pubkeyLen);

// Derive the point address from a CryptoNote SecretKey scalar. The secret's raw
// little-endian bytes are byte-reversed to canonical big-endian BEFORE being fed
// to secp256k1_ec_pubkey_create, matching the endian rule above and Solidity's
// uint256(secret) interpretation on-chain. NEVER pass a raw ed25519 point here.
std::string derivePointAddressFromSecret(const Crypto::SecretKey& secret);

// Derive the point address from bytes that are ALREADY a valid secp256k1 public
// key (e.g. output of secp256k1_ec_pubkey_serialize). NEVER pass an ed25519
// point (Crypto::PublicKey): ~55% of random 32-byte strings fail to parse as a
// compressed secp256k1 x-coordinate, and even when they parse the committed
// point would be wrong regardless — the curves share no points.
std::string derivePointAddressFromSecpBytes(const uint8_t* secpPubBytes, size_t len);

} // namespace EthAbi
} // namespace XfgSwap
