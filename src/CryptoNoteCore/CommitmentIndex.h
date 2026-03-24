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

// For XFG-STARKs + Elderfier Consensus


#pragma once

#include <unordered_map>
#include <vector>
#include <map>
#include <set>
#include <mutex>
#include <cstdint>
#include <string>
#include <optional>
#include "../crypto/hash.h"
#include "../Serialization/ISerializer.h"
#include "AliasIndex.h"
#include "Currency.h"

namespace CryptoNote {

// Simple commitment entry
struct CommitmentEntry {
  Crypto::Hash commitment;
  Crypto::Hash txHash;
  uint32_t blockHeight = 0;
  uint64_t amount = 0;
  uint32_t term = 0;

  // Internal commitment type (how deposit is used)
  //   HEAT = permanent burn via tx extra tag 0x08
  //   COLD = interest-bearing term deposit via tx extra tag 0xCD
  //   ELDERFIER_STAKING = service node stake via tx extra tag 0xEF (5x 800 XFG)
  enum class Type : uint8_t {
    HEAT = 0,              // Permanent burn (FOREVER deposits)
    COLD = 1,              // Interest-bearing term deposits
    ELDERFIER_STAKING = 2  // Elderfier registration stakes (5x 800 XFG)
  };

  Type type = Type::HEAT;

  uint32_t targetChainId = 0;  // Claim chain code: 1=ETH, 2=ARB, 3=SOL, etc. (0 = no cross-chain claim)

  // True only for 0xCE migrations where Blockchain.cpp confirmed a MultisignatureOutput
  // in the original tx. Never set for native v3 TransactionOutputCommitment outputs.
  // Used by L2 contract to apply legacy (pre-2026) interest rates without timestamp comparison.
  bool isLegacyMigration = false;

  std::string senderAddress;   // Wallet address that created this commitment (populated for 0xEF deposits)
  std::string ceremonyAlias;   // 8-char alias from 0xEF deposit metadata (0xEA tag), auto-registered with EFiD
  Crypto::PublicKey signingPubKey = {};  // Ed25519 signing pubkey from 0xEA metadata (for EFier signature verification)

  // Slashing flag (ELDERFIER_STAKING only).
  // When true, the commitment output is forbidden as a ring member — effectively burns the stake.
  // Only set by Blockchain.cpp when a 0xEC QUORUM slash verdict names this deposit's txHash.
  bool isSlashed = false;

  void serialize(ISerializer& s);
};

// Cached elderfier signature from P2P gossip
struct CachedElderfierSignature {
  Crypto::Hash merkle_root;
  Crypto::Signature signature;
  uint8_t elderfier_id;
  uint64_t block_height;
  uint64_t timestamp;
  uint64_t received_block_height;
  bool is_valid = false;

  // Post-quantum hybrid extension fields (backward compatible)
  uint8_t sig_algorithm = 0;              // 0=Ed25519, 1=ML-DSA-65
  std::vector<uint8_t> pq_signature;      // Empty for Ed25519
  std::vector<uint8_t> pq_public_key;     // Empty for Ed25519
};

// Elderfier epoch rewards (1000-block cycle)
struct ElderfierEpochRewards {
  uint64_t epochNumber = 0;
  std::vector<uint8_t> activeElderfiers;  // Elderfiers who signed during this epoch
  uint64_t totalFeesCollected = 0;
  std::map<uint8_t, uint64_t> distribution;  // EFiD -> fee amount (only for signers)
  uint64_t epochStartBlock = 0;
  uint64_t epochEndBlock = 0;
};

// Double-sign event: same EFier signed two different merkle roots at the same block height
struct DoubleSignEvent {
  uint8_t elderfier_id;
  Crypto::Hash root_a;       // First root signed
  Crypto::Hash root_b;       // Conflicting root signed
  uint64_t block_height;     // Block height at which both signatures claim to apply
  uint64_t detected_at_block;
  std::string ceremony_alias;  // For human-readable logs

  void serialize(ISerializer& s) {
    s(elderfier_id, "elderfier_id");
    s.binary(&root_a, sizeof(root_a), "root_a");
    s.binary(&root_b, sizeof(root_b), "root_b");
    s(block_height, "block_height");
    s(detected_at_block, "detected_at_block");
    s(ceremony_alias, "ceremony_alias");
  }
};

// Per-epoch activity report for elder_council review
struct EpochReport {
  uint64_t epochNumber = 0;
  uint64_t epochStartBlock = 0;
  uint64_t epochEndBlock = 0;
  uint64_t generatedAtBlock = 0;

  struct EFierActivity {
    uint8_t elderfier_id = 0;
    std::string address;
    std::string ceremonyAlias;
    bool signedThisEpoch = false;
    uint64_t signaturesSubmitted = 0;
    uint64_t feesEarned = 0;
    bool isSlashed = false;
    bool isUnstaking = false;
    uint32_t consecutiveMissedEpochs = 0;

    void serialize(ISerializer& s) {
      s(elderfier_id, "elderfier_id");
      s(address, "address");
      s(ceremonyAlias, "ceremony_alias");
      s(signedThisEpoch, "signed_this_epoch");
      s(signaturesSubmitted, "signatures_submitted");
      s(feesEarned, "fees_earned");
      s(isSlashed, "is_slashed");
      s(isUnstaking, "is_unstaking");
      s(consecutiveMissedEpochs, "consecutive_missed_epochs");
    }
  };

  std::vector<EFierActivity> efierActivity;
  std::vector<uint8_t> signingEfierIds;
  std::vector<uint8_t> missingEfierIds;
  std::vector<DoubleSignEvent> doubleSignEvents;  // double-signs detected this epoch
  uint64_t totalFeesDistributed = 0;
  uint64_t activeEfierCount = 0;
  uint64_t participatingEfierCount = 0;

  // Fee pool: swap fee data for this epoch
  uint64_t swapFeesCollected = 0;       // total swap fees from HTLC claims this epoch
  uint64_t totalCdLockedAtStart = 0;    // total XFG in CDs at epoch start
  uint64_t feeRateFixedPoint = 0;       // (swapFeesCollected * RATE_PRECISION) / totalCdLockedAtStart

  // Advisory notices for elder_council — informational only, never auto-acted upon.
  // A council member must explicitly run `propose_slash` in the wallet to act on these.
  // "DOUBLE_SIGN:<efid>" or "INACTIVE:<efid>:<N>_epochs_missed"
  std::vector<std::string> slash_advisory;

  void serialize(ISerializer& s) {
    s(epochNumber, "epoch_number");
    s(epochStartBlock, "epoch_start_block");
    s(epochEndBlock, "epoch_end_block");
    s(generatedAtBlock, "generated_at_block");
    s(efierActivity, "efier_activity");
    s(signingEfierIds, "signing_efier_ids");
    s(missingEfierIds, "missing_efier_ids");
    s(doubleSignEvents, "double_sign_events");
    s(totalFeesDistributed, "total_fees_distributed");
    s(activeEfierCount, "active_efer_count");
    s(participatingEfierCount, "participating_efer_count");
    s(slash_advisory, "slash_advisory");
    s(swapFeesCollected, "swap_fees_collected");
    s(totalCdLockedAtStart, "total_cd_locked_at_start");
    s(feeRateFixedPoint, "fee_rate_fixed_point");
  }
};

// Elderfier registration status tracking (public for method signatures)
// 0 = unregistered/dead state, higher values = more active
enum class ElderfierStatus : uint8_t {
  VOID = 0,             // Address permanently locked (unstaking completed or slashed) or not registered
  ACTIVE = 1,           // Actively registered and participating
  UNSTAKING = 2         // Unstaking initiated (review window active)
};

struct ElderfierRegistration {
  std::string address;          // "CEREMONY:<alias>" identifier (CommitmentIndex key)
  std::string ceremony_alias;   // The 8-char alias — used for display and pubkey lookup
  uint8_t elderfier_id = 0;
  Crypto::PublicKey signing_pubkey = {};  // Ed25519 pubkey for verifying EFier signatures
  ElderfierStatus status = ElderfierStatus::ACTIVE;
  uint32_t unstaking_start_block = 0;
  uint32_t unstaking_review_window = 0;

  bool isInReviewWindow(uint32_t currentBlock) const {
    if (status != ElderfierStatus::UNSTAKING) return false;
    uint32_t review_end = unstaking_start_block + unstaking_review_window;
    return currentBlock < review_end;
  }

  void serialize(ISerializer& s) {
    s(address, "address");
    s(ceremony_alias, "ceremony_alias");
    s(elderfier_id, "elderfier_id");
    s.binary(&signing_pubkey, sizeof(signing_pubkey), "signing_pubkey");
    uint8_t statusVal = static_cast<uint8_t>(status);
    s(statusVal, "status");
    if (s.type() == ISerializer::INPUT) status = static_cast<ElderfierStatus>(statusVal);
    s(unstaking_start_block, "unstaking_start_block");
    s(unstaking_review_window, "unstaking_review_window");
  }

  bool canCompleteUnstaking(uint32_t currentBlock) const {
    if (status != ElderfierStatus::UNSTAKING) return false;
    uint32_t review_end = unstaking_start_block + unstaking_review_window;
    return currentBlock >= review_end;
  }
};



// Unstaking review notice — sent by P2P gossip when an EFier initiates unstaking.
// Contains brief activity stats so active EFiers can decide if a full review is warranted.
// No action taken by peers = unstake silently approved after the review window.
struct UnstakingNotice {
  uint8_t elderfier_id = 0;
  std::string ceremony_alias;
  uint32_t unstaking_start_block = 0;
  uint32_t review_window_blocks = 0;  // Peers have this many blocks to call for full review

  // Brief activity stats (populated from CommitmentIndex epoch records)
  uint64_t total_epochs_active = 0;
  uint64_t epochs_participated = 0;
  uint64_t epochs_missed = 0;
  uint32_t consecutive_missed = 0;
  uint32_t double_sign_count = 0;
  uint64_t total_fees_earned = 0;

  uint64_t broadcast_height = 0;
  uint64_t received_at = 0;  // local timestamp (seconds since epoch) when this node received it
};

// Full review request — sent by an active EFier who believes the unstaking EFier misbehaved.
// Requires a documented reason from the permitted list (enforced at wallet layer).
struct FullReviewRequest {
  uint8_t target_efid = 0;
  uint8_t requester_efid = 0;
  std::string reason;           // "double_sign" | "missed_epochs" | "invalid_sig" | "duty_abuse"
  std::string evidence_summary; // Human-readable summary (max 256 chars)
  Crypto::Signature requester_sig = {};
  Crypto::PublicKey requester_pubkey = {};
  uint64_t broadcast_height = 0;
  uint64_t received_at = 0;
};

// Main CommitmentIndex class
class CommitmentIndex {
public:
  CommitmentIndex(const CryptoNote::Currency& currency);
  ~CommitmentIndex();

  // Add a commitment entry
  void addCommitment(const CommitmentEntry& entry);

  // Add a signature to cache
  void addSignatureToCache(const CachedElderfierSignature& sig);

  // Check and flush signatures when threshold met
  void checkAndFlushThreshold(uint64_t current_block_height);

  // Update current merkle root
  void updateCurrentMerkleRoot(const Crypto::Hash& new_root);

  // Get consensus percentage
  uint64_t getConsensusPercentageForCurrentRoot() const;

  // Get signed elderfier IDs
  std::vector<uint8_t> getSignedElderfierIds() const;

  // Get pending elderfier IDs
  std::vector<uint8_t> getPendingElderfierIds() const;

  // Get validated signatures + pubkeys for current root (for L2 relay batching)
  struct ElderfierSignatureBundle {
    uint8_t elderfier_id;
    Crypto::PublicKey signing_pubkey;
    Crypto::Signature signature;
    uint64_t block_height;
    uint64_t timestamp;
  };
  std::vector<ElderfierSignatureBundle> getSignaturesForCurrentRoot() const;

  // Per-block EFier fee distribution
  // Splits bankingFees equally among all ACTIVE registered EFiers.
  // Returns {AccountPublicAddress, amount} pairs for coinbase outputs.
  // Output ordering is deterministically shuffled using previousBlockHash
  // so that output position does not reveal EFiD identity.
  std::vector<std::pair<AccountPublicAddress, uint64_t>> computePerBlockEfierRewards(
      uint64_t bankingFees, const Crypto::Hash& previousBlockHash) const;

  // Per-block banking fee tracking (for coinbase split)
  void addBlockBankingFee(uint64_t height, uint64_t fee);
  uint64_t getBlockBankingFee(uint64_t height) const;

  void registerElderfierAddress(uint8_t elderfier_id, const std::string& address);

  // Elderfier registration lifecycle management
  bool isAddressRegisteredAsElderfier(const std::string& address, uint8_t efid) const;
  bool canAddressRegisterNewElderfier(const std::string& address) const;
  bool initiateElderfierUnstaking(const std::string& address, uint8_t efid, uint32_t currentBlock, uint32_t reviewWindow);
  bool completeElderfierUnstaking(const std::string& address, uint8_t efid, uint32_t currentBlock);
  ElderfierStatus getElderfierStatus(const std::string& address, uint8_t efid) const;
  bool isElderfierInReviewWindow(const std::string& address, uint8_t efid, uint32_t currentBlock) const;
  bool isAddressBlacklisted(const std::string& address, uint8_t efid) const;
  std::vector<ElderfierRegistration> getElderfierRegistrationsByAddress(const std::string& address) const;

  // Lookup signing pubkey by EFiD (for signature verification)
  bool getElderfierSigningPubkey(uint8_t efid, Crypto::PublicKey& pubkey_out) const;

  // Lookup EFier registration by signing pubkey (for elder_council wallet check).
  // Returns false if no active registration has this pubkey.
  bool getElderfierBySigningPubkey(const Crypto::PublicKey& pubkey, ElderfierRegistration& out) const;

  // Get count of active registered EFiers
  size_t getActiveElderfierCount() const;

  // Set the AliasIndex reference (called by Blockchain after construction)
  void setAliasIndex(AliasIndex* aliasIndex) { m_aliasIndex = aliasIndex; }

  // Legacy commitment methods (for backward compatibility with Blockchain)
  typedef uint32_t Height;

  Crypto::Hash computeMerkleRoot() const;
  std::vector<Crypto::Hash> getMerkleProof(const Crypto::Hash& commitment) const;
  size_t getLeafIndex(const Crypto::Hash& commitment) const;
  Height highestBlock() const;
  size_t rollbackToHeight(Height h);

  // Additional legacy methods
  void clear();
  CommitmentEntry getByCommitment(const Crypto::Hash& commitment) const;
  bool hasCommitment(const Crypto::Hash& commitment) const;
  size_t size() const;
  size_t heatCount() const;
  size_t coldCount() const;

  // Lookup a commitment entry by the tx hash that created it (for slash processing)
  bool getCommitmentEntryByTxHash(const Crypto::Hash& txHash, CommitmentEntry& out) const;

  // Slash vote accumulation — each EFier independently submits a 0xEC QUORUM proposal.
  // Slash executes only when vote_count / active_efiers >= requiredThresholdPct (e.g. 80).
  // Returns true if the quorum threshold was just reached and slash should execute NOW.
  // Returns false if the vote was recorded but threshold not yet met (or already executed).
  bool addSlashVote(const Crypto::Hash& depositTxHash, uint8_t efid,
                    const std::string& reason, uint64_t blockHeight,
                    uint8_t requiredThresholdPct);

  // How many EFiers have voted to slash this deposit (0 if none / unknown)
  size_t getSlashVoteCount(const Crypto::Hash& depositTxHash) const;

  // Mark the slash proposal as executed (called right after markCommitmentSlashed)
  void markSlashProposalExecuted(const Crypto::Hash& depositTxHash);

  // Slash an EFier's staking deposits.
  // Marks all CommitmentEntry records whose txHash matches depositTxHash as isSlashed=true.
  // Called by Blockchain.cpp ONLY after addSlashVote returns true (quorum reached).
  // Returns false if no matching commitment found.
  bool markCommitmentSlashed(const Crypto::Hash& depositTxHash);

  // Check if a commitment (by its hash hex) is slashed
  bool isCommitmentSlashed(const std::string& commitHashHex) const;

  // Record a detected double-sign event (called from addSignatureToCache)
  void recordDoubleSign(const DoubleSignEvent& event);

  // Get all double-sign events (for epoch report generation)
  std::vector<DoubleSignEvent> getDoubleSignEvents() const;

  // Generate an epoch report covering [startBlock, endBlock]
  // Called by Blockchain.cpp at each EPOCH_DURATION_BLOCKS boundary
  EpochReport generateEpochReport(uint64_t epochNumber, uint64_t startBlock, uint64_t endBlock,
                                  uint64_t generatedAtBlock) const;

  // Fee pool epoch rate tracking
  void recordEpochFeeRate(uint64_t epochNumber, uint64_t feeRate,
                          uint64_t feesCollected, uint64_t totalLocked);
  uint64_t getEpochFeeRate(uint64_t epochNumber) const;
  uint64_t getEpochCount() const;

  // Store a finalized epoch report (Blockchain calls this after generating)
  void storeEpochReport(const EpochReport& report);

  // Retrieve a stored epoch report by epoch number
  std::optional<EpochReport> getEpochReport(uint64_t epochNumber) const;

  // Get the most recent stored epoch report
  std::optional<EpochReport> getLatestEpochReport() const;

  // ── Unstaking review notice storage ──────────────────────────────────────────
  // Record an incoming UnstakingNotice (from P2P gossip or wallet-triggered broadcast).
  // Overwrites any prior notice for the same EFiD.
  void addUnstakingNotice(const UnstakingNotice& notice);

  // Retrieve all pending UnstakingNotices (for elder_council display / RPC).
  std::vector<UnstakingNotice> getUnstakingNotices() const;

  // Build an UnstakingNotice for a locally-registered EFier (populates activity stats
  // from epoch records). Returns false if EFiD has no registration.
  bool buildUnstakingNotice(uint8_t efid, uint32_t currentBlock, UnstakingNotice& out) const;

  // Record an incoming FullReviewRequest.
  // Only stores if requester_efid is an active EFier and reason is valid.
  void addFullReviewRequest(const FullReviewRequest& req);

  // Retrieve all FullReviewRequests for a specific target EFiD.
  std::vector<FullReviewRequest> getFullReviewRequests(uint8_t target_efid) const;

  // Serialization support — saves commitment entries, EFier registrations, and addresses.
  // Derived indices (merkle leaves, height index, txHash index, counters) are rebuilt on load.
  void serialize(ISerializer& s);

private:
  mutable std::mutex m_mutex;

  // Pending elderfier stakes (0xEF deposits)
  struct PendingElderfierStake {
    int deposit_count = 0;
    uint64_t total_amount = 0;
    Crypto::PublicKey signing_pubkey;
    std::string alias;  // Ceremony alias extracted from 0xEF metadata (0xEA tag)
  };

  std::map<std::string, PendingElderfierStake> m_pendingElderfierStakes;

  // Fee pool epoch rates — indexed by epoch number, fixed-point (FEE_POOL_RATE_PRECISION)
  std::vector<uint64_t> m_epochFeeRates;

  // Signature cache
  std::map<std::pair<uint8_t, std::string>, CachedElderfierSignature> m_signatures;
  std::map<std::string, uint64_t> m_root_first_seen_block;
  Crypto::Hash m_current_merkle_root;
  uint64_t m_current_block_height = 0;

  // List of registered elderfier IDs
  std::vector<uint8_t> m_elderfier_ids;

  // Banking fee tracking
  std::map<uint8_t, std::string> m_elderfierAddresses;   // EFiD -> wallet address mapping
  std::map<uint64_t, uint64_t> m_blockBankingFees;       // height -> banking fee sum for that block

  // Elderfier registration and unstaking status tracking
  std::map<std::string, ElderfierRegistration> m_elderfierRegistrations;  // address -> registration
  std::set<std::pair<std::string, uint8_t>> m_voidRegistrations;   // (address, EFiD) -> permanently locked

  // AliasIndex reference (owned by Blockchain, not by CommitmentIndex)
  AliasIndex* m_aliasIndex = nullptr;

  // Commitment storage (indexed by commitment hash hex)
  std::map<std::string, CommitmentEntry> m_commitments;        // commitHash hex -> entry
  std::vector<Crypto::Hash> m_merkle_leaves;                   // ordered leaf hashes for merkle tree
  std::map<uint32_t, std::vector<std::string>> m_heightIndex;  // blockHeight -> list of commitHash hex
  size_t m_heat_count = 0;
  size_t m_cold_count = 0;
  size_t m_elderfier_stake_count = 0;

  // Currency reference for network detection
  const CryptoNote::Currency& m_currency;

  // Double-sign events detected across all time (trimmed to last 10 epochs)
  std::vector<DoubleSignEvent> m_doubleSignEvents;

  // Per-epoch reports stored for elder_council query (last 10 epochs)
  std::vector<EpochReport> m_epochReports;

  // Consecutive missed epoch counter per EFiD
  std::map<uint8_t, uint32_t> m_consecutiveMissedEpochs;

  // txHash hex -> commitment hash hex index (for fast slash lookup)
  std::map<std::string, std::string> m_txHashToCommitHash;

  // Unstaking review notices — keyed by elderfier_id (last notice per EFiD)
  std::map<uint8_t, UnstakingNotice> m_unstakingNotices;

  // Full review requests — keyed by (target_efid, requester_efid)
  std::map<std::pair<uint8_t,uint8_t>, FullReviewRequest> m_fullReviewRequests;

  // Slash proposal accumulation — keyed by depositTxHash hex
  struct SlashProposal {
    std::set<uint8_t> votingEfids;  // EFiDs that have submitted a proposal for this deposit
    std::string reason;             // Reason from the most recent vote
    uint64_t firstVoteBlock = 0;
    bool executed = false;          // True once quorum was reached and slash applied
  };
  std::map<std::string, SlashProposal> m_slashProposals;  // depositTxHash hex → proposal

  // Helper methods
  bool isElderfierRegistrationDeposit(const CommitmentEntry& entry);
  std::string getWalletAddressFromTx(const Crypto::Hash& txHash);
  bool tryRegisterElderfier(const std::string& wallet, const Crypto::PublicKey& pubkey, const std::string& alias);
  Crypto::Hash computeMerkleRootInternal() const;  // Recompute root from m_merkle_leaves (caller holds lock)
};

}  // namespace CryptoNote
