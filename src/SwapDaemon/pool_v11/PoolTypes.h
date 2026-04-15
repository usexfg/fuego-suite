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
// Pool types for the LP swap pool system.
//
// LP pools provide an additional trading venue alongside direct P2P atomic swaps.
// Liquidity providers deposit both assets of a pair into a pool governed by
// a constant product (x*y=k) AMM formula.  Swaps execute against pool reserves
// with fees accrued proportionally to LP share holders.
//
// Pool state is tracked via merkle checkpoint attestations (mirroring the
// fuego-prover pattern) so that no trust in the pool organizer is required.

#pragma once

#include "SwapTypes.h"
#include "../../src/crypto/hash.h"
#include "../../src/crypto/crypto.h"
#include "../../src/crypto/musig2.h"

#include <string>
#include <cstdint>
#include <vector>
#include <functional>

namespace XfgSwap {

// ─── Pool identification ─────────────────────────────────────────────

// Unique identifier for a liquidity pool.
// XFG is always one side of every pool.
struct PoolId {
  Crypto::Hash assetA;   // always XFG (zeroed hash = native XFG)
  Crypto::Hash assetB;   // counter-asset identifier (e.g., wrapped SOL, ETH, etc.)
  uint32_t feeBps;       // fee in basis points (e.g., 30 = 0.3%)

  bool operator==(const PoolId& other) const {
    return memcmp(assetA.data, other.assetA.data, sizeof(assetA.data)) == 0 &&
           memcmp(assetB.data, other.assetB.data, sizeof(assetB.data)) == 0 &&
           feeBps == other.feeBps;
  }

  bool operator!=(const PoolId& other) const {
    return !(*this == other);
  }

  bool operator<(const PoolId& other) const {
    int cmp = memcmp(assetA.data, other.assetA.data, sizeof(assetA.data));
    if (cmp != 0) return cmp < 0;
    cmp = memcmp(assetB.data, other.assetB.data, sizeof(assetB.data));
    if (cmp != 0) return cmp < 0;
    return feeBps < other.feeBps;
  }
};

// Well-known pool identifiers for standard pairs.
inline PoolId makePoolId(SwapPair pair, uint32_t feeBps = 30) {
  Crypto::Hash assetB = {};
  assetB.data[0] = static_cast<uint8_t>(pair);
  Crypto::Hash assetA = {}; // zeroed = native XFG

  PoolId id;
  id.assetA = assetA;
  id.assetB = assetB;
  id.feeBps = feeBps;
  return id;
}

// ─── Pool state ──────────────────────────────────────────────────────

// Current state of a liquidity pool.
struct PoolState {
  PoolId id;
  uint64_t reserveA;           // XFG reserve (atomic units)
  uint64_t reserveB;           // Counter-asset reserve (atomic units)
  uint64_t totalLPShares;      // Total LP shares minted
  uint64_t feeAccumulatorA;    // Unclaimed fees (A side, atomic units)
  uint64_t feeAccumulatorB;    // Unclaimed fees (B side, atomic units)
  Crypto::Hash checkpointHash; // Current state commitment
  uint32_t blockHeight;        // Last update block height
  uint64_t timestamp;          // Last update timestamp
  uint64_t totalVolumeA;       // Cumulative swap volume (A side)
  uint64_t totalVolumeB;       // Cumulative swap volume (B side)
  uint64_t totalFeesA;         // Cumulative fees collected (A side)
  uint64_t totalFeesB;         // Cumulative fees collected (B side)
};

// ─── LP share ────────────────────────────────────────────────────────

// Represents an LP provider's share in a pool.
// Each LP share is a merkle leaf in the pool's LP share tree.
struct LPShare {
  Crypto::PublicKey owner;     // LP provider public key
  PoolId poolId;
  uint64_t shareAmount;        // Number of LP shares owned
  uint64_t feeClaimedA;        // Fees already claimed (A side)
  uint64_t feeClaimedB;        // Fees already claimed (B side)
  uint32_t depositHeight;      // Block height when deposited
  uint64_t depositTimestamp;   // Timestamp when deposited
  Crypto::Hash nullifier;      // Set when shares are burned (prevents double-spend)
};

// ─── Pool swap order ─────────────────────────────────────────────────

// A swap order against a pool (not P2P).
struct PoolSwapOrder {
  std::string orderId;         // unique order identifier (hex hash)
  PoolId poolId;
  bool swapAforB;              // true = swap XFG for asset B, false = reverse
  uint64_t inputAmount;        // Input amount (atomic units)
  uint64_t minOutput;          // Minimum acceptable output (slippage protection)
  Crypto::PublicKey traderPubKey; // Trader's public key for atomic swap
  std::string traderEndpoint;  // Trader's network address
  uint32_t expiryHeight;       // Block height after which order expires
  uint32_t postedHeight;       // Block height when order was posted
};

// ─── Pool event ──────────────────────────────────────────────────────

// Types of events that modify pool state.
enum class PoolEventType : uint8_t {
  DEPOSIT = 0,       // LP provider deposits assets, mints shares
  WITHDRAWAL = 1,    // LP provider burns shares, withdraws assets + fees
  SWAP = 2,          // Trader swaps against pool
  FEE_CLAIM = 3,     // LP provider claims accrued fees
  CHECKPOINT = 4     // Periodic checkpoint (no state change)
};

// A single pool state transition event.
struct PoolEvent {
  PoolEventType type;
  std::string eventId;       // unique event identifier (hex hash)
  PoolId poolId;
  uint64_t amountA;          // Asset A amount involved
  uint64_t amountB;          // Asset B amount involved
  uint64_t feeA;             // Fee charged (A side)
  uint64_t feeB;             // Fee charged (B side)
  Crypto::PublicKey actor;   // Who triggered the event
  uint32_t blockHeight;
  uint64_t timestamp;
  std::string swapId;        // Linked swap ID if this is a swap event
};

// ─── Pool checkpoint ─────────────────────────────────────────────────

// Attestation of pool state at a point in time.
// Mirrors the fuego-prover checkpoint pattern for trustless verification.
struct PoolCheckpoint {
  Crypto::Hash prevCheckpoint;   // Previous checkpoint hash
  Crypto::Hash newCheckpoint;    // New checkpoint hash
  Crypto::Hash lpShareMerkleRoot; // Merkle root of all LP shares
  Crypto::Hash feeMerkleRoot;    // Merkle root of fee records
  Crypto::Hash reserveCommitA;   // Commitment to reserve A
  Crypto::Hash reserveCommitB;   // Commitment to reserve B
  uint64_t totalLPShares;        // Total shares at this checkpoint
  uint32_t blockHeight;          // Block height of checkpoint
  uint64_t timestamp;            // Timestamp of checkpoint
  uint32_t eventCount;           // Number of events since prev checkpoint
};

// ─── Pool fee record ─────────────────────────────────────────────────

// Tracks fee accrual for merkle tree inclusion.
struct PoolFeeRecord {
  PoolId poolId;
  uint64_t feeAmount;      // Fee amount (atomic units)
  uint64_t totalShares;    // Total LP shares at time of fee
  Crypto::Hash eventHash;  // Hash of the swap event that generated this fee
  uint32_t blockHeight;
  uint64_t timestamp;
};

// ─── Pool LP deposit params ──────────────────────────────────────────

// Parameters for an LP deposit into a pool.
struct LPDepositParams {
  std::string depositId;         // unique deposit identifier (hex hash)
  PoolId poolId;
  uint64_t amountA;              // XFG amount to deposit
  uint64_t amountB;              // Counter-asset amount to deposit
  Crypto::PublicKey lpPubKey;    // LP provider's public key
  std::string lpEndpoint;        // LP provider's network address
  uint32_t expiryHeight;         // Block height after which deposit expires
  uint32_t postedHeight;         // Block height when deposit was posted

  // Musig2 state for atomic lock of both assets
  Crypto::SecretKey ourSecKey;
  Crypto::PublicKey ourPubKey;
  Crypto::PublicKey peerPubKey;  // Organizer's public key
  Crypto::PublicKey escrowPubKey; // Musig2 joint key
};

// ─── Pool LP withdrawal params ───────────────────────────────────────

// Parameters for an LP withdrawal from a pool.
struct LPWithdrawalParams {
  std::string withdrawalId;      // unique withdrawal identifier (hex hash)
  PoolId poolId;
  Crypto::PublicKey lpPubKey;    // LP provider's public key
  uint64_t burnAmount;           // LP shares to burn
  Crypto::Hash lpShareProof;     // Merkle proof of LP share ownership
  uint32_t expiryHeight;         // Block height after which withdrawal expires
};

// ─── AMM constants ───────────────────────────────────────────────────

// Default fee rate in basis points (0.3%)
static constexpr uint32_t DEFAULT_POOL_FEE_BPS = 30;

// Minimum liquidity to prevent dust attacks
static constexpr uint64_t MIN_INITIAL_LIQUIDITY = 1000;

// Maximum fee rate (1%)
static constexpr uint32_t MAX_POOL_FEE_BPS = 100;

// ─── String conversions ──────────────────────────────────────────────

const char* poolEventTypeToString(PoolEventType e);
PoolEventType poolEventTypeFromString(const std::string& s);

// ─── Pool ID serialization ───────────────────────────────────────────

std::string poolIdToHex(const PoolId& id);
PoolId poolIdFromHex(const std::string& hex);

} // namespace XfgSwap
