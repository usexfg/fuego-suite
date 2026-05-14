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
// Pool organizer: manages LP pools, processes deposits/withdrawals/swaps,
// and produces checkpoint attestations.
//
// The organizer is the coordinator that:
//   - Tracks pool state and LP share registry
//   - Manages fee accumulation and distribution
//   - Processes swap orders against pool reserves
//   - Generates periodic checkpoint attestations
//
// Trust model: All state transitions produce merkle checkpoint attestations
// that can be independently verified, so no trust in the organizer is required.

#pragma once

#include "PoolTypes.h"
#include "PoolAMM.h"
#include "PoolAttestation.h"
#include "Logging/LoggerRef.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <cstdint>

namespace XfgSwap {

// ─── ZK Proof Epoch Constants ───────────────────────────────────────────

static constexpr uint32_t LP_EPOCH_BLOCKS = 100;  // ~13 hours
static constexpr uint32_t LP_PROOF_DEADLINE_BLOCKS = 115;  // 15 blocks after epoch end

class PoolOrganizer {
public:
  explicit PoolOrganizer(Logging::ILogger& logger);
  ~PoolOrganizer();

  // ─── Pool lifecycle ──────────────────────────────────────────────────

  // Create a new pool with initial parameters.
  // Returns false if pool already exists.
  bool createPool(const PoolId& poolId);

  // Get pool state by ID.
  bool getPool(const PoolId& poolId, PoolState& state) const;

  // Get all active pool IDs.
  std::vector<PoolId> getActivePools() const;

  // ─── LP deposit ──────────────────────────────────────────────────────

  // Process an LP deposit: lock both assets, mint LP shares.
  // Returns the checkpoint after the deposit.
  PoolCheckpoint processDeposit(const LPDepositParams& params,
                                 uint64_t shareAmount);

  // ─── LP withdrawal ───────────────────────────────────────────────────

  // Process an LP withdrawal: burn shares, return assets + fees.
  // Returns the checkpoint after the withdrawal.
  PoolCheckpoint processWithdrawal(const LPWithdrawalParams& params,
                                    WithdrawalAmounts& amounts);

  // ─── Swap execution ──────────────────────────────────────────────────

  // Execute a swap against a pool.
  // Returns the output amount and checkpoint after the swap.
  struct SwapResult {
    uint64_t outputAmount;
    uint64_t feeAmount;
    PoolCheckpoint checkpoint;
    bool success;
  };

  SwapResult executeSwap(const PoolSwapOrder& order);

  // Get expected output for a swap (without executing).
  uint64_t getExpectedOutput(const PoolId& poolId,
                              bool swapAforB,
                              uint64_t inputAmount) const;

  // ─── Fee management ──────────────────────────────────────────────────

  // Get claimable fees for an LP share holder.
  struct ClaimableFees {
    uint64_t feeA;
    uint64_t feeB;
  };

  ClaimableFees getClaimableFees(const Crypto::PublicKey& owner,
                                  const PoolId& poolId) const;

  // Process a fee claim for an LP share holder.
  PoolCheckpoint processFeeClaim(const Crypto::PublicKey& owner,
                                  const PoolId& poolId,
                                  ClaimableFees& claimed);

  // ─── Checkpoint management ───────────────────────────────────────────

  // Generate a checkpoint for a pool (periodic attestation).
  PoolCheckpoint generateCheckpoint(const PoolId& poolId);

  // Get current checkpoint for a pool.
  bool getCurrentCheckpoint(const PoolId& poolId, PoolCheckpoint& checkpoint) const;

  // Verify a checkpoint against current pool state.
  bool verifyCheckpoint(const PoolId& poolId,
                         const PoolCheckpoint& checkpoint) const;

  // ─── LP share queries ────────────────────────────────────────────────

  // Get LP shares for an owner in a pool.
  bool getLPShares(const Crypto::PublicKey& owner,
                    const PoolId& poolId,
                    LPShare& shares) const;

  // Get merkle proof for an LP share.
  std::vector<Crypto::Hash> getLPShareProof(const Crypto::PublicKey& owner,
                                             const PoolId& poolId,
                                             size_t& leafIndex) const;

  // ─── Pool statistics ─────────────────────────────────────────────────

  // Get pool spot price (B per A).
  uint64_t getSpotPrice(const PoolId& poolId) const;

  // Get pool total value locked (sum of reserves).
  struct PoolStats {
    uint64_t reserveA;
    uint64_t reserveB;
    uint64_t totalLPShares;
    uint64_t totalVolumeA;
    uint64_t totalVolumeB;
    uint64_t totalFeesA;
    uint64_t totalFeesB;
    uint64_t activeLPProviders;
  };

  PoolStats getPoolStats(const PoolId& poolId) const;

  // ─── ZK Proof Epoch Management ───────────────────────────────────────

  // Start a new epoch for a pool (called at epoch boundary)
  void startEpoch(const PoolId& poolId, uint32_t blockHeight);

  // Check if a block height is an epoch boundary for a pool
  bool isEpochBoundary(const PoolId& poolId, uint32_t blockHeight) const;

  // Get current epoch number for a pool
  uint32_t getEpochNumber(const PoolId& poolId) const;

  // Get epoch start and end block heights
  bool getEpochRange(const PoolId& poolId, uint32_t& start, uint32_t& end) const;

  // Derive epoch key (for encrypted event decryption at epoch boundary)
  std::vector<uint8_t> deriveEpochKey(const PoolId& poolId, uint32_t epoch) const;

  // Buffer an encrypted event for the current epoch
  void bufferEncryptedEvent(const PoolId& poolId, const std::vector<uint8_t>& ciphertext);

  // Get buffered encrypted events for a pool's current epoch
  std::vector<std::vector<uint8_t>> getBufferedEvents(const PoolId& poolId) const;

  // Get current LP Merkle tree leaves for SP1 circuit
  std::vector<Crypto::Hash> getLpTreeLeaves(const PoolId& poolId) const;

  // Get current fee Merkle tree leaves for SP1 circuit
  std::vector<Crypto::Hash> getFeeTreeLeaves(const PoolId& poolId) const;

  // ─── ZK Proof Generation ─────────────────────────────────────────────

  // Generate SP1 proof inputs for an epoch (called at epoch end)
  struct EpochProofInputs {
    PoolState prevState;
    std::vector<PoolEvent> events;
    std::vector<uint8_t> epochKey;
    std::vector<Crypto::Hash> prevLpLeaves;
    std::vector<Crypto::Hash> prevFeeLeaves;
  };

  EpochProofInputs prepareProofInputs(const PoolId& poolId);

  // Update pool state after proof is verified on-chain
  void updatePoolStateFromProof(const PoolId& poolId, const Crypto::Hash& newStateCommitment,
                                 const Crypto::Hash& newLpMerkleRoot, const Crypto::Hash& newFeeMerkleRoot,
                                 uint32_t epochEnd);

  // Get pool's current state commitment (for proof verification)
  Crypto::Hash getStateCommitment(const PoolId& poolId) const;

private:
  // Internal helpers
  PoolState& getPoolMutable(const PoolId& poolId);
  void updatePoolState(PoolState& state, const PoolCheckpoint& checkpoint);
  void recordEvent(const PoolEvent& event);
  void addLPShareToTree(const PoolId& poolId, const LPShare& share);
  void addFeeRecordToTree(const PoolId& poolId, const PoolFeeRecord& record);

  // Pool state
  mutable std::mutex m_mutex;
  std::unordered_map<std::string, PoolState> m_pools; // key = poolIdToHex

  // LP share merkle trees per pool
  std::unordered_map<std::string, PoolMerkleTree> m_lpTrees;

  // Fee record merkle trees per pool
  std::unordered_map<std::string, PoolMerkleTree> m_feeTrees;

  // LP share registry: key = poolId_hex + ":" + pubkey_hex
  std::unordered_map<std::string, LPShare> m_lpRegistry;

  // Event history
  std::vector<PoolEvent> m_events;

  // Previous checkpoints per pool
  std::unordered_map<std::string, Crypto::Hash> m_prevCheckpoints;

  // ─── ZK Proof Epoch State ───────────────────────────────────────────

  // Epoch tracking per pool: key = poolId_hex
  struct EpochState {
    uint32_t epochNumber;
    uint32_t epochStart;
    uint32_t epochEnd;
    Crypto::Hash prevStateCommitment;
  };
  std::unordered_map<std::string, EpochState> m_epochStates;

  // Encrypted event buffers per pool
  std::unordered_map<std::string, std::vector<std::vector<uint8_t>>> m_eventBuffers;

  Logging::LoggerRef m_logger;
};

} // namespace XfgSwap
