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
//
// Binary merkle tree using keccak256.
// Used for off-chain asset attestations (DIGM), merkle proofs, etc.

#pragma once

#include <vector>
#include <cstddef>
#include "../crypto/hash.h"

namespace CryptoNote {

class MerkleTree {
public:
  MerkleTree() = default;

  void addLeaf(const Crypto::Hash& leaf);
  Crypto::Hash computeRoot() const;
  const std::vector<Crypto::Hash>& leaves() const;
  std::vector<Crypto::Hash> getProof(size_t leafIndex) const;

  static bool verifyProof(const Crypto::Hash& leaf,
                          const std::vector<Crypto::Hash>& proof,
                          size_t leafIndex,
                          const Crypto::Hash& root);

  void clear();
  size_t size() const;

private:
  static Crypto::Hash hashPair(const Crypto::Hash& left, const Crypto::Hash& right);
  std::vector<Crypto::Hash> m_leaves;
};

} // namespace CryptoNote
