// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free & open source software distributed in the hope
// it will be useful, but WITHOUT ANY WARRANTY; without even an
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You may redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

#include "SolPointClaim.h"

#include <algorithm>
#include <cstring>
#include <vector>

extern "C" {
#include "crypto/crypto-ops.h"
#include "crypto/keccak.h"
}

namespace XfgSwap {
namespace {

using Bytes32 = std::array<uint8_t, 32>;

Bytes32 keccak256(const std::vector<uint8_t>& data) {
  Bytes32 digest;
  keccak(data.data(), static_cast<int>(data.size()), digest.data(), 32);
  return digest;
}

Bytes32 challenge(const Bytes32& nonce, const Crypto::PublicKey& T, const SolEscrowBinding& escrow) {
  const Bytes32 context = solClaimContext(escrow);
  std::vector<uint8_t> data(nonce.begin(), nonce.end());
  const uint8_t* point = reinterpret_cast<const uint8_t*>(&T);
  data.insert(data.end(), point, point + 32);
  data.insert(data.end(), context.begin(), context.end());
  Bytes32 e = keccak256(data);
  sc_reduce32(e.data());
  return e;
}

bool sameBytes(const Crypto::PublicKey& point, const Bytes32& bytes) {
  return std::memcmp(&point, bytes.data(), 32) == 0;
}

void wipe(void* data, size_t size) {
  volatile uint8_t* p = static_cast<volatile uint8_t*>(data);
  while (size--) *p++ = 0;
}

}  // namespace

std::array<uint8_t, 32> solClaimContext(const SolEscrowBinding& escrow) {
  std::vector<uint8_t> data(escrow.htlc.begin(), escrow.htlc.end());
  data.insert(data.end(), escrow.recipient.begin(), escrow.recipient.end());
  for (int i = 0; i < 8; ++i) data.push_back(static_cast<uint8_t>(escrow.amount >> (8 * i)));
  return keccak256(data);
}

bool proveSolPointClaim(const Crypto::SecretKey& t, const Crypto::PublicKey& T, const SolEscrowBinding& escrow,
                        SolPointClaimProof& proof) {
  Crypto::PublicKey derived;
  if (!Crypto::secret_key_to_public_key(t, derived) || !(derived == T)) return false;
  Crypto::PublicKey noncePoint;
  Crypto::SecretKey nonce;
  Crypto::generate_keys(noncePoint, nonce);
  std::memcpy(proof.nonce.data(), &noncePoint, 32);
  const Bytes32 e = challenge(proof.nonce, T, escrow);
  const uint8_t* secret = reinterpret_cast<const uint8_t*>(&t);
  Bytes32 s;
  sc_mulsub(s.data(), e.data(), secret, reinterpret_cast<const uint8_t*>(&nonce));
  sc_sub(proof.secretOffset.data(), s.data(), secret);
  std::fill(proof.completedSignature.begin(), proof.completedSignature.begin() + 32, 0);
  std::copy(s.begin(), s.end(), proof.completedSignature.begin() + 32);
  wipe(&nonce, sizeof(nonce));
  return true;
}

bool solPointClaimAccepted(const Crypto::PublicKey& T, const SolEscrowBinding& escrow,
                           const SolPointClaimProof& proof) {
  ge_p3 adaptorPoint, noncePoint;
  if (ge_frombytes_vartime(&adaptorPoint, reinterpret_cast<const uint8_t*>(&T)) != 0 ||
      ge_frombytes_vartime(&noncePoint, proof.nonce.data()) != 0)
    return false;
  Bytes32 s;
  std::copy(proof.completedSignature.begin() + 32, proof.completedSignature.end(), s.begin());
  sc_reduce32(s.data());
  const Bytes32 e = challenge(proof.nonce, T, escrow);
  ge_p2 sumPoint;
  Bytes32 sum;
  ge_double_scalarmult_base_vartime(&sumPoint, e.data(), &adaptorPoint, s.data());
  ge_tobytes(sum.data(), &sumPoint);
  if (sum != proof.nonce) return false;

  Bytes32 offset = proof.secretOffset;
  sc_reduce32(offset.data());
  Bytes32 recoveredSecret;
  sc_sub(recoveredSecret.data(), s.data(), offset.data());
  if (!sc_isnonzero(recoveredSecret.data())) return false;
  ge_p3 recoveredPoint;
  Bytes32 recovered;
  ge_scalarmult_base(&recoveredPoint, recoveredSecret.data());
  ge_p3_tobytes(recovered.data(), &recoveredPoint);
  return sameBytes(T, recovered);
}

}  // namespace XfgSwap
