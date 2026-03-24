// Copyright (c) 2017-2026 Fuego Developers
// Copyright (c) 2020-2026 Elderfire Privacy Group
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

#include <set>
#include <algorithm>
#include <cstring>
#include <ctime>

#include "CommitmentIndex.h"
#include "TransactionExtra.h"
#include "../Serialization/ISerializer.h"
#include "../Serialization/SerializationOverloads.h"
#include "../Common/StringTools.h"
#include "../crypto/hash.h"
#include "../CryptoNoteConfig.h"

namespace CryptoNote {

void CommitmentEntry::serialize(ISerializer& s) {
  s.binary(&commitment, sizeof(commitment), "commitment");
  s.binary(&txHash, sizeof(txHash), "tx_hash");
  s(blockHeight, "block_height");
  s(amount, "amount");
  s(term, "term");
  s(targetChainId, "target_chain_id");
  s(isLegacyMigration, "is_legacy_migration");
  s(isSlashed, "is_slashed");
  // type as underlying uint8_t
  uint8_t typeVal = static_cast<uint8_t>(type);
  s(typeVal, "type");
  if (s.type() == ISerializer::INPUT) type = static_cast<Type>(typeVal);
  s(senderAddress, "sender_address");
  s(ceremonyAlias, "ceremony_alias");
  s.binary(&signingPubKey, sizeof(signingPubKey), "signing_pub_key");
}

CommitmentIndex::CommitmentIndex(const CryptoNote::Currency& currency) : m_currency(currency) {
}

CommitmentIndex::~CommitmentIndex() {
}

void CommitmentIndex::addCommitment(const CommitmentEntry& entry) {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::string commitHex = Common::podToHex(entry.commitment);

  // Skip duplicate commitments
  if (m_commitments.find(commitHex) != m_commitments.end()) {
    return;
  }

  // Store the commitment
  m_commitments[commitHex] = entry;
  m_merkle_leaves.push_back(entry.commitment);
  m_heightIndex[entry.blockHeight].push_back(commitHex);

  // Index by txHash for fast slash lookup
  std::string txHashHex = Common::podToHex(entry.txHash);
  m_txHashToCommitHash[txHashHex] = commitHex;

  // Update type counters
  switch (entry.type) {
    case CommitmentEntry::Type::HEAT:
      m_heat_count++;
      break;
    case CommitmentEntry::Type::COLD:
      m_cold_count++;
      break;
    case CommitmentEntry::Type::ELDERFIER_STAKING:
      m_elderfier_stake_count++;
      break;
  }

  // Update highest block height
  if (entry.blockHeight > m_current_block_height) {
    m_current_block_height = entry.blockHeight;
  }

  // Recompute merkle root after adding new leaf
  m_current_merkle_root = computeMerkleRootInternal();

  // Track 0xEF deposits for elderfier registration
  if (isElderfierRegistrationDeposit(entry)) {
    // Use senderAddress from the CommitmentEntry (populated from TransactionExtraElderfierDeposit)
    const std::string& wallet = entry.senderAddress;
    if (!wallet.empty()) {
      m_pendingElderfierStakes[wallet].deposit_count++;
      m_pendingElderfierStakes[wallet].total_amount += entry.amount;

      // Extract ceremony alias and signing pubkey from CommitmentEntry if present
      if (!entry.ceremonyAlias.empty() && m_pendingElderfierStakes[wallet].alias.empty()) {
        m_pendingElderfierStakes[wallet].alias = entry.ceremonyAlias;
      }
      if (entry.signingPubKey != Crypto::PublicKey()) {
        m_pendingElderfierStakes[wallet].signing_pubkey = entry.signingPubKey;
      }

      // Auto-register when 20 deposits across all tiers are confirmed
      const uint64_t REGISTRATION_AMOUNT = m_currency.isTestnet()
          ? CryptoNote::parameters::TESTIFIER_CEREMONY_AMOUNT
          : CryptoNote::parameters::ELDERKING_CEREMONY_AMOUNT;
      const uint32_t REGISTRATION_COUNT = m_currency.isTestnet()
          ? CryptoNote::parameters::TESTIFIER_TOTAL_DEPOSITS
          : CryptoNote::parameters::ELDERKING_TOTAL_DEPOSITS;

      auto& pending = m_pendingElderfierStakes[wallet];

      if (pending.deposit_count >= REGISTRATION_COUNT &&
          pending.total_amount >= REGISTRATION_AMOUNT) {
        tryRegisterElderfier(wallet, pending.signing_pubkey, pending.alias);
        m_pendingElderfierStakes.erase(wallet);
      }
    }
  }
}

void CommitmentIndex::addSignatureToCache(const CachedElderfierSignature& sig) {
  std::lock_guard<std::mutex> lock(m_mutex);

  // Verify Ed25519 signature against registered signing pubkey
  CachedElderfierSignature verified_sig = sig;
  verified_sig.is_valid = false;

  if (sig.sig_algorithm == 0) {
    // Look up registered pubkey for this EFiD
    Crypto::PublicKey registered_pubkey = {};
    bool found = false;
    for (const auto& pair : m_elderfierRegistrations) {
      if (pair.second.elderfier_id == sig.elderfier_id &&
          pair.second.status == ElderfierStatus::ACTIVE) {
        registered_pubkey = pair.second.signing_pubkey;
        found = true;
        break;
      }
    }

    if (found && registered_pubkey != Crypto::PublicKey()) {
      // Cryptographic verification: is this signature from the registered EFier?
      verified_sig.is_valid = Crypto::check_signature(
          sig.merkle_root, registered_pubkey, sig.signature);
    }
  }

  std::string merkle_root_hex = Common::podToHex(sig.merkle_root);
  auto key = std::make_pair(sig.elderfier_id, merkle_root_hex);

  // Double-sign detection: same EFier, same block height, different merkle root
  if (verified_sig.is_valid) {
    for (auto it = m_signatures.begin(); it != m_signatures.end(); ++it) {
      if (it->first.first == sig.elderfier_id &&
          it->first.second != merkle_root_hex &&
          it->second.block_height == sig.block_height &&
          it->second.is_valid) {
        // Found conflicting signature for the same EFier at the same height
        DoubleSignEvent event;
        event.elderfier_id = sig.elderfier_id;
        event.root_a = it->second.merkle_root;
        event.root_b = sig.merkle_root;
        event.block_height = sig.block_height;
        event.detected_at_block = sig.received_block_height;
        // Look up ceremony alias for readable logs
        for (auto& reg : m_elderfierRegistrations) {
          if (reg.second.elderfier_id == sig.elderfier_id) {
            // alias not stored in registration; leave empty
            break;
          }
        }
        m_doubleSignEvents.push_back(event);
        // Keep double-sign list bounded (last 10,000 events)
        if (m_doubleSignEvents.size() > 10000) {
          m_doubleSignEvents.erase(m_doubleSignEvents.begin());
        }
        break;
      }
    }
  }

  m_signatures[key] = verified_sig;

  // Track when root was first seen
  if (m_root_first_seen_block.find(merkle_root_hex) == m_root_first_seen_block.end()) {
    m_root_first_seen_block[merkle_root_hex] = sig.received_block_height;
  }

  // NOTE: m_current_merkle_root is ONLY set by computeMerkleRootInternal() (via
  // addCommitment / loadFromStorage). Never overwrite it from incoming signatures —
  // the local tree is authoritative. Peer sigs for a different root are valid
  // attestations of their tree state but must not change ours.
}

void CommitmentIndex::checkAndFlushThreshold(uint64_t current_block_height) {
  std::lock_guard<std::mutex> lock(m_mutex);

  // Calculate consensus percentage
  std::string current_root_hex = Common::podToHex(m_current_merkle_root);
  size_t valid_signatures = 0;

  for (auto it = m_signatures.begin(); it != m_signatures.end(); ++it) {
    if (it->first.second == current_root_hex && it->second.is_valid) {
      valid_signatures++;
    }
  }

  if (m_elderfier_ids.empty()) {
    return;
  }

  uint64_t total_elderfiers = m_elderfier_ids.size();
  uint64_t consensus_pct = (valid_signatures * 100) / total_elderfiers;

  // At 69% threshold: flush stale signatures (for non-current roots)
  if (consensus_pct >= 69) {
    // Flush signatures for current root
    std::vector<std::pair<uint8_t, std::string>> to_remove;
    for (auto it = m_signatures.begin(); it != m_signatures.end(); ++it) {
      if (it->first.second != current_root_hex) {
        to_remove.push_back(it->first);
      }
    }
    for (size_t i = 0; i < to_remove.size(); ++i) {
      m_signatures.erase(to_remove[i]);
    }
  }
}

void CommitmentIndex::updateCurrentMerkleRoot(const Crypto::Hash& new_root) {
  std::lock_guard<std::mutex> lock(m_mutex);

  m_current_merkle_root = new_root;
  m_current_block_height = 0;

  std::string new_root_hex = Common::podToHex(new_root);
  if (m_root_first_seen_block.find(new_root_hex) == m_root_first_seen_block.end()) {
    m_root_first_seen_block[new_root_hex] = 0;
  }
}

uint64_t CommitmentIndex::getConsensusPercentageForCurrentRoot() const {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::string current_root_hex = Common::podToHex(m_current_merkle_root);
  size_t valid_signatures = 0;

  for (auto it = m_signatures.begin(); it != m_signatures.end(); ++it) {
    if (it->first.second == current_root_hex && it->second.is_valid) {
      valid_signatures++;
    }
  }

  if (m_elderfier_ids.empty()) {
    return 0;
  }

  return (valid_signatures * 100) / m_elderfier_ids.size();
}

std::vector<CommitmentIndex::ElderfierSignatureBundle> CommitmentIndex::getSignaturesForCurrentRoot() const {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::string current_root_hex = Common::podToHex(m_current_merkle_root);
  std::vector<ElderfierSignatureBundle> result;
  std::set<uint8_t> seen;

  for (auto it = m_signatures.begin(); it != m_signatures.end(); ++it) {
    if (it->first.second == current_root_hex && it->second.is_valid && seen.find(it->first.first) == seen.end()) {
      seen.insert(it->first.first);

      // Look up the signing pubkey for this EFiD
      Crypto::PublicKey pubkey = {};
      for (const auto& pair : m_elderfierRegistrations) {
        if (pair.second.elderfier_id == it->first.first &&
            pair.second.status == ElderfierStatus::ACTIVE) {
          pubkey = pair.second.signing_pubkey;
          break;
        }
      }

      ElderfierSignatureBundle entry;
      entry.elderfier_id = it->first.first;
      entry.signing_pubkey = pubkey;
      entry.signature = it->second.signature;
      entry.block_height = it->second.block_height;
      entry.timestamp = it->second.timestamp;
      result.push_back(entry);
    }
  }

  return result;
}

std::vector<uint8_t> CommitmentIndex::getSignedElderfierIds() const {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::string current_root_hex = Common::podToHex(m_current_merkle_root);
  std::vector<uint8_t> signed_ids;
  std::set<uint8_t> seen;

  for (auto it = m_signatures.begin(); it != m_signatures.end(); ++it) {
    if (it->first.second == current_root_hex && it->second.is_valid && seen.find(it->first.first) == seen.end()) {
      signed_ids.push_back(it->first.first);
      seen.insert(it->first.first);
    }
  }

  return signed_ids;
}

std::vector<uint8_t> CommitmentIndex::getPendingElderfierIds() const {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::set<uint8_t> signed_set;
  std::string current_root_hex = Common::podToHex(m_current_merkle_root);

  for (auto it = m_signatures.begin(); it != m_signatures.end(); ++it) {
    if (it->first.second == current_root_hex && it->second.is_valid) {
      signed_set.insert(it->first.first);
    }
  }

  std::vector<uint8_t> pending;
  for (size_t i = 0; i < m_elderfier_ids.size(); ++i) {
    uint8_t efid = m_elderfier_ids[i];
    if (signed_set.find(efid) == signed_set.end()) {
      pending.push_back(efid);
    }
  }

  return pending;
}

bool CommitmentIndex::isElderfierRegistrationDeposit(const CommitmentEntry& entry) {
  // Check for 0xEF deposits (used for elderfier registration)
  return entry.type == CommitmentEntry::Type::ELDERFIER_STAKING;
}

std::string CommitmentIndex::getWalletAddressFromTx(const Crypto::Hash& txHash) {
  // Look up the commitment entry by txHash to retrieve the senderAddress
  // that was populated from Common::podToHex(TransactionExtraElderfierDeposit::elderfierCommitment)
  // during addCommitment() from Blockchain::pushBlock()
  std::string txHex = Common::podToHex(txHash);
  for (const auto& pair : m_commitments) {
    if (Common::podToHex(pair.second.txHash) == txHex && !pair.second.senderAddress.empty()) {
      return pair.second.senderAddress;
    }
  }
  return "";
}

bool CommitmentIndex::tryRegisterElderfier(const std::string& wallet, const Crypto::PublicKey& pubkey, const std::string& alias) {
  // Check if this address can register (not already registered, not void)
  // Note: caller already holds m_mutex

  // Check active registrations
  if (m_elderfierRegistrations.find(wallet) != m_elderfierRegistrations.end()) {
    return false;  // Already registered
  }

  // Check void registrations
  for (const auto& vr : m_voidRegistrations) {
    if (vr.first == wallet) {
      return false;  // Address permanently VOID
    }
  }

  // Cap active EFiers at 8 (keeps fee splits meaningful, limits coinbase outputs)
  size_t activeCount = 0;
  for (const auto& r : m_elderfierRegistrations) {
    if (r.second.status == ElderfierStatus::ACTIVE) ++activeCount;
  }
  if (activeCount >= 8) {
    return false;  // Maximum 8 active Elderfiers
  }

  // Find next unused EFiD
  std::set<uint8_t> used_ids(m_elderfier_ids.begin(), m_elderfier_ids.end());
  uint8_t efid = 0;
  while (used_ids.count(efid) > 0 && efid < 255) {
    efid++;
  }
  if (used_ids.count(efid) > 0) {
    return false;  // All EFiDs exhausted
  }

  // Register the Elderfier
  ElderfierRegistration reg;
  reg.address = wallet;
  reg.ceremony_alias = alias;
  reg.elderfier_id = efid;
  reg.signing_pubkey = pubkey;
  reg.status = ElderfierStatus::ACTIVE;
  reg.unstaking_start_block = 0;
  reg.unstaking_review_window = 0;

  m_elderfierRegistrations[wallet] = reg;
  m_elderfier_ids.push_back(efid);
  m_elderfierAddresses[efid] = wallet;

  // Auto-register ceremony alias via AliasIndex (tied to EFiD — voids on unstake)
  if (m_aliasIndex && !alias.empty() && (alias.length() == 8 || alias == "GALAPAGOS" || alias == "WINSLAYER" || alias == "LOUDMINING")) {
    AliasEntry aliasEntry;
    aliasEntry.alias = alias;
    aliasEntry.ownerAddress = "";  // Not stored on-chain for privacy — addressHash is sufficient
    aliasEntry.aliasHash = Crypto::cn_fast_hash(alias.data(), alias.size());
    aliasEntry.addressHash = Crypto::cn_fast_hash(wallet.data(), wallet.size());
    aliasEntry.aliasType = 0;  // Elderfier type
    aliasEntry.registeredBlock = static_cast<uint32_t>(m_current_block_height);

    m_aliasIndex->registerAlias(aliasEntry);
  }

  return true;
}

bool CommitmentIndex::getElderfierSigningPubkey(uint8_t efid, Crypto::PublicKey& pubkey_out) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  for (const auto& pair : m_elderfierRegistrations) {
    if (pair.second.elderfier_id == efid && pair.second.status == ElderfierStatus::ACTIVE) {
      pubkey_out = pair.second.signing_pubkey;
      return pubkey_out != Crypto::PublicKey();  // Only valid if non-zero
    }
  }
  return false;
}

bool CommitmentIndex::getElderfierBySigningPubkey(const Crypto::PublicKey& pubkey, ElderfierRegistration& out) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  for (const auto& kv : m_elderfierRegistrations) {
    if (kv.second.signing_pubkey == pubkey) {
      out = kv.second;
      return true;
    }
  }
  return false;
}

// ============================================================================
// COMMITMENT STORAGE AND MERKLE TREE
// ============================================================================

Crypto::Hash CommitmentIndex::computeMerkleRoot() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_current_merkle_root;
}

Crypto::Hash CommitmentIndex::computeMerkleRootInternal() const {
  // Build binary merkle tree from leaves (caller must hold m_mutex)
  if (m_merkle_leaves.empty()) {
    return Crypto::Hash();
  }

  std::vector<Crypto::Hash> level = m_merkle_leaves;

  while (level.size() > 1) {
    std::vector<Crypto::Hash> next_level;

    for (size_t i = 0; i < level.size(); i += 2) {
      if (i + 1 < level.size()) {
        // Hash pair: H(left || right)
        uint8_t combined[64];
        memcpy(combined, level[i].data, 32);
        memcpy(combined + 32, level[i + 1].data, 32);
        Crypto::Hash parent;
        Crypto::cn_fast_hash(combined, 64, parent);
        next_level.push_back(parent);
      } else {
        // Odd leaf: promote to next level (duplicate hash with itself)
        uint8_t combined[64];
        memcpy(combined, level[i].data, 32);
        memcpy(combined + 32, level[i].data, 32);
        Crypto::Hash parent;
        Crypto::cn_fast_hash(combined, 64, parent);
        next_level.push_back(parent);
      }
    }

    level = next_level;
  }

  return level[0];
}

std::vector<Crypto::Hash> CommitmentIndex::getMerkleProof(const Crypto::Hash& commitment) const {
  std::lock_guard<std::mutex> lock(m_mutex);

  if (m_merkle_leaves.empty()) {
    return {};
  }

  // Find leaf index
  size_t leaf_idx = SIZE_MAX;
  for (size_t i = 0; i < m_merkle_leaves.size(); ++i) {
    if (m_merkle_leaves[i] == commitment) {
      leaf_idx = i;
      break;
    }
  }

  if (leaf_idx == SIZE_MAX) {
    return {};  // Commitment not found
  }

  // Build proof by walking up the merkle tree
  std::vector<Crypto::Hash> proof;
  std::vector<Crypto::Hash> level = m_merkle_leaves;
  size_t idx = leaf_idx;

  while (level.size() > 1) {
    // Find sibling
    size_t sibling_idx;
    if (idx % 2 == 0) {
      sibling_idx = (idx + 1 < level.size()) ? idx + 1 : idx;  // Right sibling, or self if odd
    } else {
      sibling_idx = idx - 1;  // Left sibling
    }
    proof.push_back(level[sibling_idx]);

    // Compute next level
    std::vector<Crypto::Hash> next_level;
    for (size_t i = 0; i < level.size(); i += 2) {
      uint8_t combined[64];
      memcpy(combined, level[i].data, 32);
      if (i + 1 < level.size()) {
        memcpy(combined + 32, level[i + 1].data, 32);
      } else {
        memcpy(combined + 32, level[i].data, 32);  // Duplicate for odd
      }
      Crypto::Hash parent;
      Crypto::cn_fast_hash(combined, 64, parent);
      next_level.push_back(parent);
    }

    idx = idx / 2;
    level = next_level;
  }

  return proof;
}

size_t CommitmentIndex::getLeafIndex(const Crypto::Hash& commitment) const {
  std::lock_guard<std::mutex> lock(m_mutex);

  for (size_t i = 0; i < m_merkle_leaves.size(); ++i) {
    if (m_merkle_leaves[i] == commitment) {
      return i;
    }
  }
  return SIZE_MAX;  // Not found
}

CommitmentIndex::Height CommitmentIndex::highestBlock() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return static_cast<Height>(m_current_block_height);
}

size_t CommitmentIndex::rollbackToHeight(Height h) {
  std::lock_guard<std::mutex> lock(m_mutex);

  size_t removed = 0;

  // Find all commitments above height h and remove them
  auto height_it = m_heightIndex.upper_bound(h);
  while (height_it != m_heightIndex.end()) {
    for (const auto& commitHex : height_it->second) {
      auto it = m_commitments.find(commitHex);
      if (it != m_commitments.end()) {
        // Decrement type counters
        switch (it->second.type) {
          case CommitmentEntry::Type::HEAT: m_heat_count--; break;
          case CommitmentEntry::Type::COLD: m_cold_count--; break;
          case CommitmentEntry::Type::ELDERFIER_STAKING: m_elderfier_stake_count--; break;
        }
        m_commitments.erase(it);
        removed++;
      }
    }
    height_it = m_heightIndex.erase(height_it);
  }

  // Rebuild merkle leaves from remaining commitments (ordered by block height)
  m_merkle_leaves.clear();
  for (const auto& height_pair : m_heightIndex) {
    for (const auto& commitHex : height_pair.second) {
      auto it = m_commitments.find(commitHex);
      if (it != m_commitments.end()) {
        m_merkle_leaves.push_back(it->second.commitment);
      }
    }
  }

  // Update block height and merkle root
  if (!m_heightIndex.empty()) {
    m_current_block_height = m_heightIndex.rbegin()->first;
  } else {
    m_current_block_height = 0;
  }
  m_current_merkle_root = computeMerkleRootInternal();

  return removed;
}

void CommitmentIndex::clear() {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_commitments.clear();
  m_merkle_leaves.clear();
  m_heightIndex.clear();
  m_heat_count = 0;
  m_cold_count = 0;
  m_elderfier_stake_count = 0;
  m_signatures.clear();
  m_root_first_seen_block.clear();
  m_pendingElderfierStakes.clear();
  m_elderfier_ids.clear();
  // Note: AliasIndex is cleared/reset separately by Blockchain (owns its own lifecycle)
  m_elderfierRegistrations.clear();
  m_voidRegistrations.clear();
  m_current_merkle_root = Crypto::Hash();
  m_current_block_height = 0;
}

CommitmentEntry CommitmentIndex::getByCommitment(const Crypto::Hash& commitment) const {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::string commitHex = Common::podToHex(commitment);
  auto it = m_commitments.find(commitHex);
  if (it != m_commitments.end()) {
    return it->second;
  }
  return CommitmentEntry();
}

bool CommitmentIndex::hasCommitment(const Crypto::Hash& commitment) const {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::string commitHex = Common::podToHex(commitment);
  return m_commitments.find(commitHex) != m_commitments.end();
}

size_t CommitmentIndex::size() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_commitments.size();
}

size_t CommitmentIndex::heatCount() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_heat_count;
}

size_t CommitmentIndex::coldCount() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_cold_count;
}

// ============================================================================
// PHASE 5: PER-BLOCK EFIER FEE DISTRIBUTION
// ============================================================================

std::vector<std::pair<AccountPublicAddress, uint64_t>> CommitmentIndex::computePerBlockEfierRewards(
    uint64_t bankingFees, const Crypto::Hash& previousBlockHash) const {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::vector<std::pair<AccountPublicAddress, uint64_t>> rewards;

  if (bankingFees == 0) {
    return rewards;
  }

  // Collect all ACTIVE registered EFiers with valid addresses
  struct ActiveEfier {
    uint8_t efid;
    AccountPublicAddress addr;
  };
  std::vector<ActiveEfier> activeEfiers;

  for (const auto& pair : m_elderfierRegistrations) {
    if (pair.second.status != ElderfierStatus::ACTIVE) continue;

    auto addrIt = m_elderfierAddresses.find(pair.second.elderfier_id);
    if (addrIt == m_elderfierAddresses.end()) continue;

    AccountPublicAddress addr;
    if (!m_currency.parseAccountAddressString(addrIt->second, addr)) continue;

    activeEfiers.push_back({pair.second.elderfier_id, addr});
  }

  if (activeEfiers.empty()) {
    return rewards;  // No active EFiers — banking fees stay with miner
  }

  // Sort by EFiD for canonical ordering before shuffle
  std::sort(activeEfiers.begin(), activeEfiers.end(),
    [](const ActiveEfier& a, const ActiveEfier& b) { return a.efid < b.efid; });

  // Split equally, distribute remainder 1 atomic unit each
  uint64_t perEfier = bankingFees / activeEfiers.size();
  uint64_t remainder = bankingFees % activeEfiers.size();

  // Below dust threshold: skip split, miner keeps full reward for this block.
  // Avoids creating tiny UTXO spam (e.g. 0.0008 XFG fee split among 50 EFiers).
  uint64_t dustThreshold = m_currency.defaultDustThreshold();
  if (perEfier < dustThreshold) {
    return rewards;
  }

  for (size_t i = 0; i < activeEfiers.size(); ++i) {
    uint64_t share = perEfier + (i < remainder ? 1 : 0);
    if (share > 0) {
      rewards.push_back({activeEfiers[i].addr, share});
    }
  }

  // Deterministic Fisher-Yates shuffle seeded by previous block hash.
  // Breaks output-position-to-EFiD correlation so observers cannot map
  // coinbase output index to a specific EFier identity.
  if (rewards.size() > 1) {
    Crypto::Hash rng;
    Crypto::cn_fast_hash(previousBlockHash.data, sizeof(previousBlockHash.data), (char*)rng.data);
    for (size_t i = rewards.size() - 1; i > 0; --i) {
      uint64_t r;
      std::memcpy(&r, rng.data, sizeof(r));
      size_t j = r % (i + 1);
      std::swap(rewards[i], rewards[j]);
      Crypto::cn_fast_hash(rng.data, sizeof(rng.data), (char*)rng.data);
    }
  }

  return rewards;
}

void CommitmentIndex::addBlockBankingFee(uint64_t height, uint64_t fee) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_blockBankingFees[height] = fee;
}

uint64_t CommitmentIndex::getBlockBankingFee(uint64_t height) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_blockBankingFees.find(height);
  return (it != m_blockBankingFees.end()) ? it->second : 0;
}

void CommitmentIndex::registerElderfierAddress(uint8_t elderfier_id, const std::string& address) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_elderfierAddresses[elderfier_id] = address;
}

size_t CommitmentIndex::getActiveElderfierCount() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  size_t count = 0;
  for (const auto& pair : m_elderfierRegistrations) {
    if (pair.second.status == ElderfierStatus::ACTIVE) ++count;
  }
  return count;
}

// ============================================================================
// ELDERFIER REGISTRATION LIFECYCLE MANAGEMENT
// ============================================================================

bool CommitmentIndex::isAddressRegisteredAsElderfier(const std::string& address, uint8_t efid) const {
  std::lock_guard<std::mutex> lock(m_mutex);

  auto it = m_elderfierRegistrations.find(address);
  if (it == m_elderfierRegistrations.end()) {
    return false;
  }

  return it->second.elderfier_id == efid &&
         it->second.status == ElderfierStatus::ACTIVE;
}

bool CommitmentIndex::canAddressRegisterNewElderfier(const std::string& address) const {
  std::lock_guard<std::mutex> lock(m_mutex);

  // Check if any registration (active or unstaking) exists for this address
  if (m_elderfierRegistrations.find(address) != m_elderfierRegistrations.end()) {
    return false;  // Already registered (active or unstaking)
  }

  // Check void set - any pair with this address means permanently locked out
  for (const auto& vr : m_voidRegistrations) {
    if (vr.first == address) {
      return false;  // Address permanently VOID — cannot re-register
    }
  }

  return true;
}

bool CommitmentIndex::initiateElderfierUnstaking(const std::string& address, uint8_t efid,
                                                  uint32_t currentBlock, uint32_t reviewWindow) {
  std::lock_guard<std::mutex> lock(m_mutex);

  auto it = m_elderfierRegistrations.find(address);
  if (it == m_elderfierRegistrations.end()) {
    return false;
  }

  if (it->second.elderfier_id != efid) {
    return false;
  }

  if (it->second.status != ElderfierStatus::ACTIVE) {
    return false;  // Can only unstake from ACTIVE state
  }

  it->second.status = ElderfierStatus::UNSTAKING;
  it->second.unstaking_start_block = currentBlock;
  it->second.unstaking_review_window = reviewWindow;
  return true;
}

bool CommitmentIndex::completeElderfierUnstaking(const std::string& address, uint8_t efid,
                                                  uint32_t currentBlock) {
  std::lock_guard<std::mutex> lock(m_mutex);

  auto it = m_elderfierRegistrations.find(address);
  if (it == m_elderfierRegistrations.end()) {
    return false;
  }

  if (it->second.elderfier_id != efid) {
    return false;
  }

  if (!it->second.canCompleteUnstaking(currentBlock)) {
    return false;  // Review window not elapsed yet
  }

  // Move to VOID status permanently
  m_voidRegistrations.insert(std::make_pair(address, efid));

  // Remove EFiD from active list
  auto eid_it = std::find(m_elderfier_ids.begin(), m_elderfier_ids.end(), efid);
  if (eid_it != m_elderfier_ids.end()) {
    m_elderfier_ids.erase(eid_it);
  }

  // Void the alias tied to this EFiD (alias lifecycle follows EFiD)
  if (m_aliasIndex) {
    m_aliasIndex->voidAlias(address);
  }

  // Remove from registrations map (void set now tracks it permanently)
  m_elderfierRegistrations.erase(it);
  return true;
}

ElderfierStatus CommitmentIndex::getElderfierStatus(const std::string& address, uint8_t efid) const {
  std::lock_guard<std::mutex> lock(m_mutex);

  // Check active registrations first
  auto it = m_elderfierRegistrations.find(address);
  if (it != m_elderfierRegistrations.end() && it->second.elderfier_id == efid) {
    return it->second.status;
  }

  // Check void set
  if (m_voidRegistrations.count(std::make_pair(address, efid)) > 0) {
    return ElderfierStatus::VOID;
  }

  // Not found at all — return VOID as the default "not registered" state
  return ElderfierStatus::VOID;
}

bool CommitmentIndex::isElderfierInReviewWindow(const std::string& address, uint8_t efid,
                                                 uint32_t currentBlock) const {
  std::lock_guard<std::mutex> lock(m_mutex);

  auto it = m_elderfierRegistrations.find(address);
  if (it == m_elderfierRegistrations.end() || it->second.elderfier_id != efid) {
    return false;
  }

  return it->second.isInReviewWindow(currentBlock);
}

bool CommitmentIndex::isAddressBlacklisted(const std::string& address, uint8_t efid) const {
  std::lock_guard<std::mutex> lock(m_mutex);

  return m_voidRegistrations.count(std::make_pair(address, efid)) > 0;
}

std::vector<ElderfierRegistration> CommitmentIndex::getElderfierRegistrationsByAddress(
    const std::string& address) const {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::vector<ElderfierRegistration> results;

  auto it = m_elderfierRegistrations.find(address);
  if (it != m_elderfierRegistrations.end()) {
    results.push_back(it->second);
  }

  return results;
}

bool CommitmentIndex::getCommitmentEntryByTxHash(const Crypto::Hash& txHash, CommitmentEntry& out) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::string txHashHex = Common::podToHex(txHash);
  auto it = m_txHashToCommitHash.find(txHashHex);
  if (it == m_txHashToCommitHash.end()) return false;
  auto cit = m_commitments.find(it->second);
  if (cit == m_commitments.end()) return false;
  out = cit->second;
  return true;
}

bool CommitmentIndex::addSlashVote(const Crypto::Hash& depositTxHash, uint8_t efid,
                                    const std::string& reason, uint64_t blockHeight,
                                    uint8_t requiredThresholdPct) {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::string key = Common::podToHex(depositTxHash);

  // Already executed — don't re-trigger
  auto it = m_slashProposals.find(key);
  if (it != m_slashProposals.end() && it->second.executed) {
    return false;
  }

  // The accused EFier may not vote on their own slashing.
  // Determine the owner of the targeted deposit by matching its signingPubKey
  // against EFier registrations.
  auto txIt = m_txHashToCommitHash.find(key);
  if (txIt != m_txHashToCommitHash.end()) {
    auto cIt = m_commitments.find(txIt->second);
    if (cIt != m_commitments.end()) {
      const CommitmentEntry& entry = cIt->second;
      // Find which EFiD owns this deposit
      for (auto& reg : m_elderfierRegistrations) {
        if (reg.second.signing_pubkey == entry.signingPubKey) {
          if (reg.second.elderfier_id == efid) {
            // Voter IS the accused — reject silently
            return false;
          }
          break;
        }
      }
    }
  }

  // Record this EFier's vote
  SlashProposal& proposal = m_slashProposals[key];
  if (proposal.firstVoteBlock == 0) {
    proposal.firstVoteBlock = blockHeight;
  }
  proposal.votingEfids.insert(efid);
  proposal.reason = reason;

  // Count active EFiers to determine quorum
  size_t activeCount = 0;
  for (auto& reg : m_elderfierRegistrations) {
    if (reg.second.status == ElderfierStatus::ACTIVE) {
      ++activeCount;
    }
  }
  if (activeCount == 0) return false;

  size_t voteCount = proposal.votingEfids.size();
  uint64_t pct = (voteCount * 100) / activeCount;

  // Quorum reached — caller should now execute the slash
  return pct >= static_cast<uint64_t>(requiredThresholdPct);
}

size_t CommitmentIndex::getSlashVoteCount(const Crypto::Hash& depositTxHash) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::string key = Common::podToHex(depositTxHash);
  auto it = m_slashProposals.find(key);
  if (it == m_slashProposals.end()) return 0;
  return it->second.votingEfids.size();
}

void CommitmentIndex::markSlashProposalExecuted(const Crypto::Hash& depositTxHash) {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::string key = Common::podToHex(depositTxHash);
  auto it = m_slashProposals.find(key);
  if (it != m_slashProposals.end()) {
    it->second.executed = true;
  }
}

bool CommitmentIndex::markCommitmentSlashed(const Crypto::Hash& depositTxHash) {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::string txHashHex = Common::podToHex(depositTxHash);
  auto idxIt = m_txHashToCommitHash.find(txHashHex);
  if (idxIt == m_txHashToCommitHash.end()) return false;
  auto cit = m_commitments.find(idxIt->second);
  if (cit == m_commitments.end()) return false;
  cit->second.isSlashed = true;
  return true;
}

bool CommitmentIndex::isCommitmentSlashed(const std::string& commitHashHex) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_commitments.find(commitHashHex);
  if (it == m_commitments.end()) return false;
  return it->second.isSlashed;
}

void CommitmentIndex::recordDoubleSign(const DoubleSignEvent& event) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_doubleSignEvents.push_back(event);
  if (m_doubleSignEvents.size() > 10000) {
    m_doubleSignEvents.erase(m_doubleSignEvents.begin());
  }
}

std::vector<DoubleSignEvent> CommitmentIndex::getDoubleSignEvents() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_doubleSignEvents;
}

EpochReport CommitmentIndex::generateEpochReport(uint64_t epochNumber, uint64_t startBlock,
                                                  uint64_t endBlock, uint64_t generatedAtBlock) const {
  std::lock_guard<std::mutex> lock(m_mutex);

  EpochReport report;
  report.epochNumber = epochNumber;
  report.epochStartBlock = startBlock;
  report.epochEndBlock = endBlock;
  report.generatedAtBlock = generatedAtBlock;

  // Collect which EFiers signed during this epoch by scanning the signature cache.
  // Signatures are ephemeral (flushed after consensus), so we use the epoch-level
  // ElderfierEpochRewards records if available; otherwise fall back to live cache.
  std::set<uint8_t> signersThisEpoch;
  for (auto& kv : m_signatures) {
    uint8_t efid = kv.first.first;
    const CachedElderfierSignature& sig = kv.second;
    if (sig.is_valid &&
        sig.received_block_height >= startBlock &&
        sig.received_block_height <= endBlock) {
      signersThisEpoch.insert(efid);
    }
  }

  // Also count EFiers who have signed the current merkle root regardless of timing.
  // On fast testnet the signing thread (2s poll) can miss entire short epochs,
  // and if the root hasn't changed the sig's received_block_height may be from
  // a prior epoch. An EFier with a valid sig for the current root is participating.
  std::string current_root_hex = Common::podToHex(m_current_merkle_root);
  if (m_current_merkle_root != Crypto::Hash()) {
    for (auto& kv : m_signatures) {
      if (kv.second.is_valid && kv.first.second == current_root_hex) {
        signersThisEpoch.insert(kv.first.first);
      }
    }
  }

  // Collect all registered active EFiers
  std::set<uint8_t> allActiveEfids;
  for (auto& reg : m_elderfierRegistrations) {
    if (reg.second.status == ElderfierStatus::ACTIVE ||
        reg.second.status == ElderfierStatus::UNSTAKING) {
      allActiveEfids.insert(reg.second.elderfier_id);
    }
  }
  report.activeEfierCount = allActiveEfids.size();
  report.participatingEfierCount = signersThisEpoch.size();

  // Build per-EFier activity records
  for (uint8_t efid : allActiveEfids) {
    EpochReport::EFierActivity activity;
    activity.elderfier_id = efid;
    activity.signedThisEpoch = signersThisEpoch.count(efid) > 0;
    if (activity.signedThisEpoch) {
      report.signingEfierIds.push_back(efid);
    } else {
      report.missingEfierIds.push_back(efid);
    }

    // Count total valid signatures in cache for this EFier
    activity.signaturesSubmitted = 0;
    for (auto& sig_kv : m_signatures) {
      if (sig_kv.first.first == efid && sig_kv.second.is_valid) {
        activity.signaturesSubmitted++;
      }
    }

    // Look up address and alias from registration
    for (auto& reg : m_elderfierRegistrations) {
      if (reg.second.elderfier_id == efid) {
        activity.address = reg.first;
        break;
      }
    }
    // Look up ceremony alias from staking commitments
    for (auto& kv : m_commitments) {
      const CommitmentEntry& e = kv.second;
      if (e.type == CommitmentEntry::Type::ELDERFIER_STAKING && !e.ceremonyAlias.empty()) {
        // Find the EFier whose signing pubkey matches the registration
        bool matched = false;
        for (auto& reg : m_elderfierRegistrations) {
          if (reg.second.elderfier_id == efid &&
              e.signingPubKey == reg.second.signing_pubkey) {
            activity.ceremonyAlias = e.ceremonyAlias;
            matched = true;
            break;
          }
        }
        if (matched) break;
      }
    }

    // Check slashed/unstaking status
    for (auto& reg : m_elderfierRegistrations) {
      if (reg.second.elderfier_id == efid) {
        activity.isUnstaking = (reg.second.status == ElderfierStatus::UNSTAKING);
        break;
      }
    }
    // Check if any of their deposits are slashed
    for (auto& kv : m_commitments) {
      const CommitmentEntry& e = kv.second;
      if (e.type == CommitmentEntry::Type::ELDERFIER_STAKING && e.isSlashed) {
        for (auto& reg : m_elderfierRegistrations) {
          if (reg.second.elderfier_id == efid &&
              e.signingPubKey == reg.second.signing_pubkey) {
            activity.isSlashed = true;
            break;
          }
        }
      }
    }

    // Consecutive missed epochs (from tracking map)
    auto missIt = m_consecutiveMissedEpochs.find(efid);
    if (missIt != m_consecutiveMissedEpochs.end()) {
      activity.consecutiveMissedEpochs = missIt->second;
    }

    // Fees (from block fee map for this epoch's range)
    // Simple: total fees for epoch / number of active EFiers (same split as coinbase)
    uint64_t epochFeeTotal = 0;
    for (auto& feeKv : m_blockBankingFees) {
      if (feeKv.first >= startBlock && feeKv.first <= endBlock) {
        epochFeeTotal += feeKv.second;
      }
    }
    report.totalFeesDistributed = epochFeeTotal;
    if (!allActiveEfids.empty()) {
      activity.feesEarned = activity.signedThisEpoch ? (epochFeeTotal / allActiveEfids.size()) : 0;
    }

    report.efierActivity.push_back(activity);
  }

  // Collect double-sign events in this epoch's block range
  for (auto& ev : m_doubleSignEvents) {
    if (ev.detected_at_block >= startBlock && ev.detected_at_block <= endBlock) {
      report.doubleSignEvents.push_back(ev);
    }
  }

  // Build slash recommendations for elder_council
  for (auto& activity : report.efierActivity) {
    if (!activity.signedThisEpoch && !activity.isSlashed) {
      if (activity.consecutiveMissedEpochs >= 3) {
        report.slash_advisory.push_back(
            "INACTIVE:" + std::to_string(activity.elderfier_id) +
            ":" + std::to_string(activity.consecutiveMissedEpochs) + "_epochs_missed");
      }
    }
  }
  for (auto& ev : report.doubleSignEvents) {
    report.slash_advisory.push_back(
        "DOUBLE_SIGN:" + std::to_string(ev.elderfier_id) +
        ":height=" + std::to_string(ev.block_height));
  }

  return report;
}

void CommitmentIndex::recordEpochFeeRate(uint64_t epochNumber, uint64_t feeRate,
                                          uint64_t feesCollected, uint64_t totalLocked) {
  std::lock_guard<std::mutex> lock(m_mutex);
  // Grow vector if needed (epochs may skip if no blocks for a while)
  if (epochNumber >= m_epochFeeRates.size()) {
    m_epochFeeRates.resize(epochNumber + 1, 0);
  }
  m_epochFeeRates[epochNumber] = feeRate;
}

uint64_t CommitmentIndex::getEpochFeeRate(uint64_t epochNumber) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (epochNumber >= m_epochFeeRates.size()) return 0;
  return m_epochFeeRates[epochNumber];
}

uint64_t CommitmentIndex::getEpochCount() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_epochFeeRates.size();
}

void CommitmentIndex::storeEpochReport(const EpochReport& report) {
  std::lock_guard<std::mutex> lock(m_mutex);

  // Update consecutive missed epoch counters
  std::set<uint8_t> signers(report.signingEfierIds.begin(), report.signingEfierIds.end());
  for (uint8_t efid : report.missingEfierIds) {
    m_consecutiveMissedEpochs[efid]++;
  }
  for (uint8_t efid : report.signingEfierIds) {
    m_consecutiveMissedEpochs[efid] = 0;  // reset streak on participation
  }

  m_epochReports.push_back(report);
  // Keep only last 10 epochs in memory
  if (m_epochReports.size() > 10) {
    m_epochReports.erase(m_epochReports.begin());
  }
}

std::optional<EpochReport> CommitmentIndex::getEpochReport(uint64_t epochNumber) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  for (auto it = m_epochReports.rbegin(); it != m_epochReports.rend(); ++it) {
    if (it->epochNumber == epochNumber) return *it;
  }
  return std::nullopt;
}

std::optional<EpochReport> CommitmentIndex::getLatestEpochReport() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_epochReports.empty()) return std::nullopt;
  return m_epochReports.back();
}

// ── Unstaking review notice methods ──────────────────────────────────────────

void CommitmentIndex::serialize(ISerializer& s) {
  // No mutex here — called from Blockchain serializer which already holds the lock.

  // Commitments map (keyed by commitHash hex string)
  s(m_commitments, "commitments");

  // EFier registrations map (keyed by wallet address)
  s(m_elderfierRegistrations, "elderfier_registrations");

  // EFiD -> wallet address mapping
  s(m_elderfierAddresses, "elderfier_addresses");

  // Slash proposals
  // (stored as individual fields since SlashProposal contains a std::set)
  // We skip m_slashProposals for now — slashes are rare and verifiable on-chain via commitments

  // Epoch reports (last 10)
  s(m_epochReports, "epoch_reports");

  // Fee pool epoch rates
  s(m_epochFeeRates, "epoch_fee_rates");

  if (s.type() == ISerializer::INPUT) {
    // Rebuild all derived data from m_commitments on load
    m_merkle_leaves.clear();
    m_heightIndex.clear();
    m_txHashToCommitHash.clear();
    m_heat_count = 0;
    m_cold_count = 0;
    m_elderfier_stake_count = 0;
    m_current_block_height = 0;

    for (const auto& kv : m_commitments) {
      const CommitmentEntry& entry = kv.second;
      m_merkle_leaves.push_back(entry.commitment);
      m_heightIndex[entry.blockHeight].push_back(kv.first);
      m_txHashToCommitHash[Common::podToHex(entry.txHash)] = kv.first;
      switch (entry.type) {
        case CommitmentEntry::Type::HEAT:           m_heat_count++; break;
        case CommitmentEntry::Type::COLD:           m_cold_count++; break;
        case CommitmentEntry::Type::ELDERFIER_STAKING: m_elderfier_stake_count++; break;
      }
      if (entry.blockHeight > m_current_block_height)
        m_current_block_height = entry.blockHeight;
    }

    // Rebuild EFier ID list from registrations
    m_elderfier_ids.clear();
    for (const auto& kv : m_elderfierRegistrations) {
      m_elderfier_ids.push_back(kv.second.elderfier_id);
    }

    m_current_merkle_root = computeMerkleRootInternal();
  }
}

void CommitmentIndex::addUnstakingNotice(const UnstakingNotice& notice) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_unstakingNotices[notice.elderfier_id] = notice;
}

std::vector<UnstakingNotice> CommitmentIndex::getUnstakingNotices() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::vector<UnstakingNotice> result;
  result.reserve(m_unstakingNotices.size());
  for (const auto& kv : m_unstakingNotices) {
    result.push_back(kv.second);
  }
  return result;
}

bool CommitmentIndex::buildUnstakingNotice(uint8_t efid, uint32_t currentBlock, UnstakingNotice& out) const {
  std::lock_guard<std::mutex> lock(m_mutex);

  // Find the registration
  ElderfierRegistration* reg = nullptr;
  for (auto& kv : m_elderfierRegistrations) {
    if (kv.second.elderfier_id == efid) {
      reg = &const_cast<ElderfierRegistration&>(kv.second);
      break;
    }
  }
  if (!reg) return false;

  // Find the commitment entry for alias
  std::string alias;
  for (const auto& kv : m_commitments) {
    if (kv.second.type == CommitmentEntry::Type::ELDERFIER_STAKING) {
      // Match by signing pubkey
      bool found = false;
      for (const auto& r : m_elderfierRegistrations) {
        if (r.second.elderfier_id == efid && r.second.signing_pubkey == kv.second.signingPubKey) {
          alias = kv.second.ceremonyAlias;
          found = true;
          break;
        }
      }
      if (found) break;
    }
  }

  // Tally activity across epoch reports
  uint64_t totalEpochs = 0, epochsParticipated = 0, epochsMissed = 0;
  uint32_t consecutiveMissed = 0;
  uint32_t doubleSignCount = 0;
  uint64_t totalFees = 0;

  for (const auto& report : m_epochReports) {
    totalEpochs++;
    bool participated = false;
    for (const auto& activity : report.efierActivity) {
      if (activity.elderfier_id == efid) {
        participated = activity.signedThisEpoch;
        totalFees += activity.feesEarned;
        break;
      }
    }
    if (participated) {
      epochsParticipated++;
      consecutiveMissed = 0;
    } else {
      epochsMissed++;
      consecutiveMissed++;
    }
  }

  // Count double-sign events for this EFiD
  for (const auto& ds : m_doubleSignEvents) {
    if (ds.elderfier_id == efid) doubleSignCount++;
  }

  // Populate consecutive missed from tracked counter
  auto missIt = m_consecutiveMissedEpochs.find(efid);
  if (missIt != m_consecutiveMissedEpochs.end()) {
    consecutiveMissed = missIt->second;
  }

  uint32_t reviewWindow = reg->unstaking_review_window > 0
    ? reg->unstaking_review_window
    : (uint32_t)CryptoNote::parameters::ELDERFIER_STAKING_REVIEW_WINDOW;

  out.elderfier_id = efid;
  out.ceremony_alias = alias;
  out.unstaking_start_block = currentBlock;
  out.review_window_blocks = reviewWindow;
  out.total_epochs_active = totalEpochs;
  out.epochs_participated = epochsParticipated;
  out.epochs_missed = epochsMissed;
  out.consecutive_missed = consecutiveMissed;
  out.double_sign_count = doubleSignCount;
  out.total_fees_earned = totalFees;
  out.broadcast_height = currentBlock;
  out.received_at = static_cast<uint64_t>(std::time(nullptr));

  return true;
}

void CommitmentIndex::addFullReviewRequest(const FullReviewRequest& req) {
  std::lock_guard<std::mutex> lock(m_mutex);

  // Validate reason code
  static const std::set<std::string> validReasons = {
    "double_sign", "missed_epochs", "invalid_sig", "duty_abuse"
  };
  if (validReasons.find(req.reason) == validReasons.end()) return;

  // Requester must be an active EFier
  bool requesterActive = false;
  for (const auto& kv : m_elderfierRegistrations) {
    if (kv.second.elderfier_id == req.requester_efid &&
        kv.second.status == ElderfierStatus::ACTIVE) {
      requesterActive = true;
      break;
    }
  }
  if (!requesterActive) return;

  // Requester cannot request review of themselves
  if (req.requester_efid == req.target_efid) return;

  auto key = std::make_pair(req.target_efid, req.requester_efid);
  m_fullReviewRequests[key] = req;
}

std::vector<FullReviewRequest> CommitmentIndex::getFullReviewRequests(uint8_t target_efid) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::vector<FullReviewRequest> result;
  for (const auto& kv : m_fullReviewRequests) {
    if (kv.first.first == target_efid) {
      result.push_back(kv.second);
    }
  }
  return result;
}

}  // namespace CryptoNote
