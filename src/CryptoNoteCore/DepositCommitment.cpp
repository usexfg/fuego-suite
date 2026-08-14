// Copyright (c) 2017-2026 Fuego Developers
// Copyright (c) 2017-2025 Elderfire Privacy Group
// Copyright (c) 2018-2019 Conceal Network & Conceal Devs
// Copyright (c) 2014-2016 The XDN developers
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

#include "DepositCommitment.h"
#include "crypto/randomize.h"
#include "crypto/hash.h"
#include "crypto/keccak.h"
#include "Common/StringTools.h"
#include "CryptoNoteCore/CryptoNoteBasic.h"
#include "CryptoNoteConfig.h"
#include <cstring>

namespace CryptoNote {

// ============================================================
// StarkCommitmentGenerator — v3 unified HEAT+COLD relay format
// ============================================================

StarkCommitmentResult StarkCommitmentGenerator::generate(
    uint64_t amount,
    uint32_t term,
    uint32_t networkId,
    uint32_t chainId,
    uint32_t version) {

  StarkCommitmentResult result;

  // Generate 32-byte random secret
  Randomize::randomBytes(sizeof(result.secret.data), result.secret.data);

  // Compute commitment and nullifier from that secret
  result.commitment = computeCommitment(result.secret, amount, term, networkId, chainId, version);
  result.nullifier  = computeNullifier(result.secret, amount);

  return result;
}

Crypto::Hash StarkCommitmentGenerator::computeCommitment(
    const Crypto::SecretKey& secret,
    uint64_t amount,
    uint32_t term,
    uint32_t networkId,
    uint32_t chainId,
    uint32_t version) {

  // Preimage (56 bytes):
  //   secret[32] || le64(amount) || le32(networkId) || le32(chainId) || le32(version) || le32(term)
  //
  // Matches xfg-stark-cli AIR compute_commitment (burn_mint_air.rs)
  // NO tx_hash — circular dependency (commitment is in tx_extra which is in the tx)

  uint8_t preimage[56];
  size_t offset = 0;

  // secret (32 bytes)
  memcpy(preimage + offset, secret.data, 32);
  offset += 32;

  // amount (8 bytes LE)
  memcpy(preimage + offset, &amount, 8);
  offset += 8;

  // networkId (4 bytes LE)
  memcpy(preimage + offset, &networkId, 4);
  offset += 4;

  // chainId (4 bytes LE)
  memcpy(preimage + offset, &chainId, 4);
  offset += 4;

  // version (4 bytes LE)
  memcpy(preimage + offset, &version, 4);
  offset += 4;

  // term (4 bytes LE) — 0xFFFFFFFF for HEAT, actual blocks for COLD
  memcpy(preimage + offset, &term, 4);
  offset += 4;

  Crypto::Hash commitment;
  keccak(preimage, offset, commitment.data, sizeof(commitment.data));
  return commitment;
}

Crypto::Hash StarkCommitmentGenerator::computeNullifier(
    const Crypto::SecretKey& secret,
    uint64_t amount) {

  // Nullifier preimage (49 bytes):
  //   secret[32] || "nullifier"[9] || le64(amount)
  //
  // Domain separator "nullifier" prevents collision with commitment hash.
  // Amount binding prevents cross-tier nullifier reuse.

  uint8_t preimage[49];
  size_t offset = 0;

  // secret (32 bytes)
  memcpy(preimage + offset, secret.data, 32);
  offset += 32;

  // "nullifier" domain separator (9 bytes)
  memcpy(preimage + offset, "nullifier", 9);
  offset += 9;

  // amount (8 bytes LE)
  memcpy(preimage + offset, &amount, 8);
  offset += 8;

  Crypto::Hash nullifier;
  keccak(preimage, offset, nullifier.data, sizeof(nullifier.data));
  return nullifier;
}

// ============================================================
// DEPRECATED — old MVP commitment generators (pre-v3)
// ============================================================
// These used keccak(secret || "commitment") without amount/network/term.
// Kept for backward compat with pre-v3 chain data.

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

DepositCommitment DepositCommitmentGenerator::generateHeatCommitment(
    uint64_t xfgAmount,
    const std::vector<uint8_t>& metadata) {

    Crypto::SecretKey secret;
    Randomize::randomBytes(sizeof(secret.data), secret.data);

    std::vector<uint8_t> enhancedMetadata = metadata;
    std::string networkId = "93385046440755750514194170694064996624";
    enhancedMetadata.insert(enhancedMetadata.end(), networkId.begin(), networkId.end());

    std::vector<uint8_t> heatData;
    heatData.insert(heatData.end(), secret.data, secret.data + sizeof(secret.data));
    heatData.insert(heatData.end(), reinterpret_cast<const uint8_t*>(&xfgAmount), reinterpret_cast<const uint8_t*>(&xfgAmount) + sizeof(xfgAmount));
    heatData.insert(heatData.end(), (uint8_t*)"commitment", (uint8_t*)"commitment" + 10);

    Crypto::Hash heatCommitment;
    keccak(heatData.data(), heatData.size(), heatCommitment.data, sizeof(heatCommitment.data));

    return DepositCommitment(CommitmentType::HEAT, heatCommitment, enhancedMetadata);
  }

  std::pair<DepositCommitment, Crypto::SecretKey> DepositCommitmentGenerator::generateHeatCommitmentWithSecret(
      uint64_t xfgAmount,
      const std::vector<uint8_t>& metadata) {

    Crypto::SecretKey secret;
    Randomize::randomBytes(sizeof(secret.data), secret.data);

    std::vector<uint8_t> enhancedMetadata = metadata;
    std::string networkId = "93385046440755750514194170694064996624";
    enhancedMetadata.insert(enhancedMetadata.end(), networkId.begin(), networkId.end());

    std::vector<uint8_t> heatData;
    heatData.insert(heatData.end(), secret.data, secret.data + sizeof(secret.data));
    heatData.insert(heatData.end(), reinterpret_cast<const uint8_t*>(&xfgAmount), reinterpret_cast<const uint8_t*>(&xfgAmount) + sizeof(xfgAmount));
    heatData.insert(heatData.end(), (uint8_t*)"commitment", (uint8_t*)"commitment" + 10);

    Crypto::Hash heatCommitment;
    keccak(heatData.data(), heatData.size(), heatCommitment.data, sizeof(heatCommitment.data));

    DepositCommitment commitment(CommitmentType::HEAT, heatCommitment, enhancedMetadata);
    return std::make_pair(commitment, secret);
}

DepositCommitment DepositCommitmentGenerator::generateYieldCommitment(
    uint64_t term,
    uint64_t amount,
    const std::vector<uint8_t>& metadata) {

    Crypto::SecretKey secret;
    Randomize::randomBytes(sizeof(secret.data), secret.data);

    std::vector<uint8_t> yieldData;
    yieldData.insert(yieldData.end(), secret.data, secret.data + sizeof(secret.data));
    yieldData.insert(yieldData.end(),
        reinterpret_cast<const uint8_t*>(&term),
        reinterpret_cast<const uint8_t*>(&term) + sizeof(term));
    yieldData.insert(yieldData.end(),
        reinterpret_cast<const uint8_t*>(&amount),
        reinterpret_cast<const uint8_t*>(&amount) + sizeof(amount));
    yieldData.insert(yieldData.end(), metadata.begin(), metadata.end());

    Crypto::Hash yieldCommitment;
    keccak(yieldData.data(), yieldData.size(), yieldCommitment.data, sizeof(yieldCommitment.data));

    return DepositCommitment(CommitmentType::YIELD, yieldCommitment, metadata);
}

DepositCommitment DepositCommitmentGenerator::generateCommitment(
    uint64_t term,
    uint64_t amount,
    const std::vector<uint8_t>& metadata) {

    if (term == parameters::HEAT_TERM) {
        return generateHeatCommitment(amount, metadata);
    }
    return generateYieldCommitment(term, amount, metadata);
}

bool DepositCommitmentGenerator::validateCommitment(const DepositCommitment& commitment) {
    if (commitment.commitment == NULL_HASH) {
        return false;
    }
    switch (commitment.type) {
        case CommitmentType::HEAT:
            return !commitment.metadata.empty();
        // case CommitmentType::COLD:  // REMOVED: COLD deposit type
        case CommitmentType::YIELD:
            return commitment.metadata.size() >= sizeof(uint64_t);
        default:
            return false;
    }
}

#pragma GCC diagnostic pop

} // namespace CryptoNote
