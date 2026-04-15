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

#include "PoolAttestation.h"
#include "crypto/hash.h"
#include <cstring>
#include <algorithm>

namespace XfgSwap {

// ─── Keccak256 helper ────────────────────────────────────────────────

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

// ─── PoolMerkleTree ──────────────────────────────────────────────────

PoolMerkleTree::PoolMerkleTree() {}

void PoolMerkleTree::addLeaf(const Crypto::Hash& leaf) {
  m_leaves.push_back(leaf);
}

Crypto::Hash PoolMerkleTree::computeRoot() const {
  if (m_leaves.empty()) {
    return Crypto::Hash{};
  }
  if (m_leaves.size() == 1) {
    return m_leaves[0];
  }

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

const std::vector<Crypto::Hash>& PoolMerkleTree::leaves() const {
  return m_leaves;
}

std::vector<Crypto::Hash> PoolMerkleTree::getProof(size_t leafIndex) const {
  std::vector<Crypto::Hash> proof;
  if (leafIndex >= m_leaves.size()) {
    return proof;
  }

  std::vector<Crypto::Hash> level = m_leaves;
  size_t index = leafIndex;

  while (level.size() > 1) {
    std::vector<Crypto::Hash> next;
    next.reserve((level.size() + 1) / 2);

    for (size_t i = 0; i < level.size(); i += 2) {
      if (i + 1 < level.size()) {
        next.push_back(hashPair(level[i], level[i + 1]));
        if (i == index || i + 1 == index) {
          // Sibling hash
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

bool PoolMerkleTree::verifyProof(const Crypto::Hash& leaf,
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

void PoolMerkleTree::clear() {
  m_leaves.clear();
}

size_t PoolMerkleTree::size() const {
  return m_leaves.size();
}

Crypto::Hash PoolMerkleTree::hashPair(const Crypto::Hash& left, const Crypto::Hash& right) {
  return keccak256_pair(left, right);
}

// ─── Leaf hash computations ──────────────────────────────────────────

Crypto::Hash computeLPShareLeaf(const LPShare& share) {
  uint8_t buf[32 + 32 + 8 + 8 + 8]; // pubkey(32) + poolId(32) + shares(8) + feeA(8) + feeB(8)
  size_t offset = 0;

  std::memcpy(buf + offset, &share.owner, 32);
  offset += 32;
  std::memcpy(buf + offset, &share.poolId.assetB, 32);
  offset += 32;
  std::memcpy(buf + offset, &share.shareAmount, 8);
  offset += 8;
  std::memcpy(buf + offset, &share.feeClaimedA, 8);
  offset += 8;
  std::memcpy(buf + offset, &share.feeClaimedB, 8);

  return keccak256(buf, offset);
}

Crypto::Hash computeFeeRecordLeaf(const PoolFeeRecord& record) {
  uint8_t buf[32 + 8 + 8 + 32]; // poolId(32) + feeAmount(8) + totalShares(8) + eventHash(32)
  size_t offset = 0;

  std::memcpy(buf + offset, &record.poolId.assetB, 32);
  offset += 32;
  std::memcpy(buf + offset, &record.feeAmount, 8);
  offset += 8;
  std::memcpy(buf + offset, &record.totalShares, 8);
  offset += 8;
  std::memcpy(buf + offset, &record.eventHash, 32);

  return keccak256(buf, offset);
}

Crypto::Hash computeReserveCommit(uint64_t reserve, uint32_t blockHeight) {
  uint8_t buf[12]; // reserve(8) + height(4)
  std::memcpy(buf, &reserve, 8);
  std::memcpy(buf + 8, &blockHeight, 4);
  return keccak256(buf, 12);
}

Crypto::Hash computeCheckpointHash(const PoolCheckpoint& checkpoint) {
  uint8_t buf[32 * 5 + 8 + 4 + 8]; // 5 hashes + totalShares(8) + height(4) + timestamp(8)
  size_t offset = 0;

  std::memcpy(buf + offset, &checkpoint.prevCheckpoint, 32);
  offset += 32;
  std::memcpy(buf + offset, &checkpoint.lpShareMerkleRoot, 32);
  offset += 32;
  std::memcpy(buf + offset, &checkpoint.feeMerkleRoot, 32);
  offset += 32;
  std::memcpy(buf + offset, &checkpoint.reserveCommitA, 32);
  offset += 32;
  std::memcpy(buf + offset, &checkpoint.reserveCommitB, 32);
  offset += 32;
  std::memcpy(buf + offset, &checkpoint.totalLPShares, 8);
  offset += 8;
  std::memcpy(buf + offset, &checkpoint.blockHeight, 4);
  offset += 4;
  std::memcpy(buf + offset, &checkpoint.timestamp, 8);

  return keccak256(buf, offset);
}

PoolCheckpoint buildCheckpoint(const PoolState& state,
                                const PoolMerkleTree& lpTree,
                                const PoolMerkleTree& feeTree,
                                const Crypto::Hash& prevCheckpoint) {
  PoolCheckpoint checkpoint = {};

  checkpoint.prevCheckpoint = prevCheckpoint;
  checkpoint.lpShareMerkleRoot = lpTree.computeRoot();
  checkpoint.feeMerkleRoot = feeTree.computeRoot();
  checkpoint.reserveCommitA = computeReserveCommit(state.reserveA, state.blockHeight);
  checkpoint.reserveCommitB = computeReserveCommit(state.reserveB, state.blockHeight);
  checkpoint.totalLPShares = state.totalLPShares;
  checkpoint.blockHeight = state.blockHeight;
  checkpoint.timestamp = state.timestamp;
  checkpoint.eventCount = 0; // Set by caller

  checkpoint.newCheckpoint = computeCheckpointHash(checkpoint);

  return checkpoint;
}

bool verifyCheckpoint(const PoolCheckpoint& checkpoint,
                       const PoolState& state,
                       const PoolMerkleTree& lpTree,
                       const PoolMerkleTree& feeTree,
                       const Crypto::Hash& prevCheckpoint) {
  // Verify merkle roots
  if (checkpoint.lpShareMerkleRoot != lpTree.computeRoot()) {
    return false;
  }
  if (checkpoint.feeMerkleRoot != feeTree.computeRoot()) {
    return false;
  }

  // Verify reserve commitments
  if (checkpoint.reserveCommitA != computeReserveCommit(state.reserveA, state.blockHeight)) {
    return false;
  }
  if (checkpoint.reserveCommitB != computeReserveCommit(state.reserveB, state.blockHeight)) {
    return false;
  }

  // Verify total shares
  if (checkpoint.totalLPShares != state.totalLPShares) {
    return false;
  }

  // Verify block height and timestamp
  if (checkpoint.blockHeight != state.blockHeight) {
    return false;
  }
  if (checkpoint.timestamp != state.timestamp) {
    return false;
  }

  // Verify prev checkpoint linkage
  if (checkpoint.prevCheckpoint != prevCheckpoint) {
    return false;
  }

  // Verify checkpoint hash
  Crypto::Hash expectedHash = computeCheckpointHash(checkpoint);
  if (checkpoint.newCheckpoint != expectedHash) {
    return false;
  }

  return true;
}

} // namespace XfgSwap
