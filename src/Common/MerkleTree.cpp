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

#include "MerkleTree.h"
#include "../crypto/hash.h"
#include <cstring>

namespace CryptoNote {

static Crypto::Hash keccak256(const uint8_t* data, size_t size) {
  Crypto::Hash result;
  Crypto::cn_fast_hash(data, size, result);
  return result;
}

static Crypto::Hash keccak256_pair(const Crypto::Hash& left, const Crypto::Hash& right) {
  uint8_t buf[64];
  std::memcpy(buf, &left, 32);
  std::memcpy(buf + 32, &right, 32);
  return keccak256(buf, 64);
}

void MerkleTree::addLeaf(const Crypto::Hash& leaf) {
  m_leaves.push_back(leaf);
}

Crypto::Hash MerkleTree::computeRoot() const {
  if (m_leaves.empty()) return Crypto::Hash{};
  if (m_leaves.size() == 1) return m_leaves[0];

  std::vector<Crypto::Hash> level = m_leaves;
  while (level.size() > 1) {
    std::vector<Crypto::Hash> next;
    next.reserve((level.size() + 1) / 2);
    for (size_t i = 0; i < level.size(); i += 2) {
      const Crypto::Hash& left = level[i];
      const Crypto::Hash& right = (i + 1 < level.size()) ? level[i + 1] : left;
      next.push_back(hashPair(left, right));
    }
    level = std::move(next);
  }
  return level[0];
}

const std::vector<Crypto::Hash>& MerkleTree::leaves() const {
  return m_leaves;
}

std::vector<Crypto::Hash> MerkleTree::getProof(size_t leafIndex) const {
  std::vector<Crypto::Hash> proof;
  if (leafIndex >= m_leaves.size()) return proof;

  std::vector<Crypto::Hash> level = m_leaves;
  size_t index = leafIndex;

  while (level.size() > 1) {
    std::vector<Crypto::Hash> next;
    next.reserve((level.size() + 1) / 2);

    for (size_t i = 0; i < level.size(); i += 2) {
      if (i + 1 < level.size()) {
        next.push_back(hashPair(level[i], level[i + 1]));
        if (i == index || i + 1 == index) {
          size_t siblingIndex = (i == index) ? i + 1 : i;
          proof.push_back(level[siblingIndex]);
        }
      } else {
        next.push_back(hashPair(level[i], level[i]));
      }
    }

    level = std::move(next);
    index /= 2;
  }

  return proof;
}

bool MerkleTree::verifyProof(const Crypto::Hash& leaf,
                              const std::vector<Crypto::Hash>& proof,
                              size_t leafIndex,
                              const Crypto::Hash& root) {
  Crypto::Hash current = leaf;
  size_t index = leafIndex;

  for (const auto& sibling : proof) {
    if (index % 2 == 0) {
      current = hashPair(current, sibling);
    } else {
      current = hashPair(sibling, current);
    }
    index /= 2;
  }

  return current == root;
}

void MerkleTree::clear() {
  m_leaves.clear();
}

size_t MerkleTree::size() const {
  return m_leaves.size();
}

Crypto::Hash MerkleTree::hashPair(const Crypto::Hash& left, const Crypto::Hash& right) {
  return keccak256_pair(left, right);
}

} // namespace CryptoNote
