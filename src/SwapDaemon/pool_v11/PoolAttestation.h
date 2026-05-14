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
// Pool attestation system using merkle checkpoint proofs.
//
// Mirrors the fuego-prover checkpoint pattern:
//   - LP shares are merkle leaves
//   - Fee records are merkle leaves
//   - Checkpoint hash binds pool state to a block height
//   - Proofs can be verified independently (trustless)

#pragma once

#include "PoolTypes.h"
#include "crypto/hash.h"

#include <vector>
#include <cstdint>

namespace XfgSwap {

// ─── Merkle tree ─────────────────────────────────────────────────────

// Binary merkle tree using keccak256 (matches fuego-prover).
class PoolMerkleTree {
public:
  PoolMerkleTree();

  // Add a leaf to the tree.
  void addLeaf(const Crypto::Hash& leaf);

  // Compute the merkle root from all leaves.
  Crypto::Hash computeRoot() const;

  // Get all leaves.
  const std::vector<Crypto::Hash>& leaves() const;

  // Get merkle proof for a leaf at given index.
  // Returns vector of sibling hashes from leaf to root.
  std::vector<Crypto::Hash> getProof(size_t leafIndex) const;

  // Verify a merkle proof.
  static bool verifyProof(const Crypto::Hash& leaf,
                          const std::vector<Crypto::Hash>& proof,
                          size_t leafIndex,
                          const Crypto::Hash& root);

  // Clear all leaves.
  void clear();

  // Number of leaves.
  size_t size() const;

private:
  // Hash two nodes together.
  static Crypto::Hash hashPair(const Crypto::Hash& left, const Crypto::Hash& right);

  std::vector<Crypto::Hash> m_leaves;
};

// ─── Checkpoint computation ──────────────────────────────────────────

// Compute LP share leaf hash:
//   keccak256(owner_pubkey || poolId_assetB || shareAmount || feeClaimedA || feeClaimedB)
Crypto::Hash computeLPShareLeaf(const LPShare& share);

// Compute fee record leaf hash:
//   keccak256(poolId_assetB || feeAmount || totalShares || eventHash)
Crypto::Hash computeFeeRecordLeaf(const PoolFeeRecord& record);

// Compute reserve commitment hash:
//   keccak256(reserveAmount || blockHeight)
Crypto::Hash computeReserveCommit(uint64_t reserve, uint32_t blockHeight);

// Compute full checkpoint hash:
//   keccak256(
//     prevCheckpoint ||
//     lpShareMerkleRoot ||
//     feeMerkleRoot ||
//     reserveCommitA ||
//     reserveCommitB ||
//     totalLPShares_le64 ||
//     blockHeight_le32 ||
//     timestamp_le64
//   )
Crypto::Hash computeCheckpointHash(const PoolCheckpoint& checkpoint);

// Compute checkpoint from pool state and merkle trees.
PoolCheckpoint buildCheckpoint(const PoolState& state,
                                const PoolMerkleTree& lpTree,
                                const PoolMerkleTree& feeTree,
                                const Crypto::Hash& prevCheckpoint);

// Verify a checkpoint is consistent with pool state and trees.
bool verifyCheckpoint(const PoolCheckpoint& checkpoint,
                       const PoolState& state,
                       const PoolMerkleTree& lpTree,
                       const PoolMerkleTree& feeTree,
                       const Crypto::Hash& prevCheckpoint);

} // namespace XfgSwap
