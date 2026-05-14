// Copyright (c) 2017-2026 Fuego Developers
// Copyright (c) 2017-2025 Elderfire Privacy Group
// Copyright (c) 2018-2019 Conceal Network & Conceal Devs
// Copyright (c) 2014-2017 The XDN developers
// Copyright (c) 2012-2018 The CryptoNote developers
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

#pragma once

#include <array>
#include <vector>
#include <cstdint>
#include "../../include/CryptoNote.h"

namespace CryptoNote {

// ============================================================
// Unified STARK Commitment (v3 — xfg-stark-cli relay model)
// ============================================================
//
// Preimage (56 bytes):
//   secret[32] || le64(amount) || le32(network_id) || le32(chain_id) || le32(version) || le32(term)
//
// HEAT burns:  term = DEPOSIT_TERM_FOREVER (0xFFFFFFFF)
// COLD deposits: term = actual lock duration in blocks
//
// Nullifier (49 bytes):
//   secret[32] || "nullifier"[9] || le64(amount)
//
// tx_hash is NOT in the commitment preimage (circular dependency: commitment
// goes in tx_extra which is part of the transaction whose hash we'd need).
// tx_hash binds via merkle leaf indexing + separate STARK public input.

struct StarkCommitmentResult {
  Crypto::Hash      commitment;  // keccak256(preimage) — goes on-chain in tx_extra
  Crypto::Hash      nullifier;   // keccak256(secret || "nullifier" || amount) — anti-double-spend
  Crypto::SecretKey secret;      // 32-byte random secret — user's claim ticket for xfg-stark-cli
};

// Unified STARK commitment generator for both HEAT burns and COLD deposits.
// Matches the xfg-stark-cli AIR (burn_mint_air.rs compute_commitment).
class StarkCommitmentGenerator {
public:
  // Generate fresh secret + commitment + nullifier.
  // This is what burn() and cold() wallet commands call.
  static StarkCommitmentResult generate(
      uint64_t amount,
      uint32_t term,        // DEPOSIT_TERM_FOREVER for HEAT, actual blocks for COLD
      uint32_t networkId,   // STARK_NETWORK_ID_MAINNET or _TESTNET
      uint32_t chainId,     // STARK_TARGET_CHAIN_ETH, _ARB, etc.
      uint32_t version);    // STARK_COMMITMENT_VERSION (currently 3)

  // Recompute commitment from a known secret (for proof generation / validation).
  static Crypto::Hash computeCommitment(
      const Crypto::SecretKey& secret,
      uint64_t amount,
      uint32_t term,
      uint32_t networkId,
      uint32_t chainId,
      uint32_t version);

  // Compute nullifier from a known secret (for proof generation / validation).
  static Crypto::Hash computeNullifier(
      const Crypto::SecretKey& secret,
      uint64_t amount);
};

// ============================================================
// DEPRECATED — old MVP commitment generators
// ============================================================
// These used keccak(secret || "commitment") without amount/network/term
// binding.  Kept for backward compatibility with pre-v3 chain data.
// New code should use StarkCommitmentGenerator above.

enum class CommitmentType : uint8_t {
    HEAT  = 0,   // 0x08 — permanent burn (FOREVER term)
    COLD  = 1,   // 0xCD — term-locked deposit (finite term, earns CD interest)
    YIELD = 2    // 0x07 — interest-bearing (FuCIA custom interest assets)
};

struct DepositCommitment {
    CommitmentType type;
    Crypto::Hash commitment;
    std::vector<uint8_t> metadata;

    DepositCommitment() : type(CommitmentType::YIELD) {}
    DepositCommitment(CommitmentType t, const Crypto::Hash& c, const std::vector<uint8_t>& m = {})
        : type(t), commitment(c), metadata(m) {}
};

// DEPRECATED: use StarkCommitmentGenerator::generate() instead
class [[deprecated("Use StarkCommitmentGenerator for v3+ commitments")]]
DepositCommitmentGenerator {
public:
    static DepositCommitment generateHeatCommitment(
        uint64_t xfgAmount,
        const std::vector<uint8_t>& metadata = {});

    static std::pair<DepositCommitment, Crypto::SecretKey> generateHeatCommitmentWithSecret(
        uint64_t xfgAmount,
        const std::vector<uint8_t>& metadata = {});

    static DepositCommitment generateYieldCommitment(
        uint64_t term,
        uint64_t amount,
        const std::vector<uint8_t>& metadata = {});

    static DepositCommitment generateCommitment(
        uint64_t term,
        uint64_t amount,
        const std::vector<uint8_t>& metadata = {});

    static bool validateCommitment(const DepositCommitment& commitment);
    static uint64_t convertXfgToHeat(uint64_t xfgAmount);
    static uint64_t convertHeatToXfg(uint64_t heatAmount);
};

} // namespace CryptoNote
