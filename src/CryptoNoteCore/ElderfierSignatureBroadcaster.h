// Copyright (c) 2017-2025 Fuego Developers
// Copyright (c) 2020-2025 Elderfire Privacy Group
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


#pragma once

#include <memory>
#include <mutex>
#include <vector>
#include <thread>
#include <atomic>
#include "../crypto/crypto.h"
#include "../crypto/hash.h"
#include "CommitmentIndex.h"
#include <Logging/LoggerRef.h>

namespace CryptoNote {

class core;
class NodeServer;
class IP2pEndpoint;
struct COMMAND_ELDERFIER_SIGNATURE;

// Handles P2P signature gossip, consensus threshold tracking, and active signing
class ElderfierSignatureBroadcaster {
public:
  ElderfierSignatureBroadcaster(core& ccore, NodeServer& p2psrv, IP2pEndpoint* p2pEndpoint,
                               Logging::ILogger& logger);
  ~ElderfierSignatureBroadcaster();

  // Handle incoming signature messages from P2P network
  void handleSignatureMessage(const CachedElderfierSignature& sig);

  // Broadcast merkle root to network for signatures
  void broadcastMerkleRoot(const Crypto::Hash& root);

  // Get current consensus percentage
  uint64_t getConsensusPercentage() const;

  // Check if 69% consensus threshold has been reached
  bool hasReachedThreshold() const;

  // Get signed elderfier IDs for current root
  std::vector<uint8_t> getSignedElderfierIds() const;

  // Get pending elderfier IDs (haven't signed yet)
  std::vector<uint8_t> getPendingElderfierIds() const;

  // Configure signing keys (enables active signing mode)
  void setSigningKeys(const Crypto::PublicKey& pub, const Crypto::SecretKey& sec);

  // Set wallet address for receiving banking fee rewards
  void setPayoutAddress(const std::string& address);

  // Start broadcaster (and signing thread if keys are set)
  void start();

  // Stop broadcaster and signing thread
  void stop();

  // Broadcast an unstaking review notice to all connected peers.
  // Called from Blockchain.cpp (or RpcServer) when an EFier initiates unstaking.
  void broadcastUnstakingReview(const UnstakingNotice& notice);

  // Broadcast a full review request to all connected peers.
  // Called from RpcServer when an active EFier triggers /broadcast_review_request.
  void broadcastFullReviewRequest(const FullReviewRequest& req);

private:
  core& m_core;
  NodeServer& m_p2p;
  IP2pEndpoint* m_p2pEndpoint;
  mutable std::mutex m_mutex;
  bool m_running = false;
  Logging::LoggerRef m_logger;

  // Signing keys (set via --elderfier-key)
  Crypto::PublicKey m_signingPubKey;
  Crypto::SecretKey m_signingSecKey;
  bool m_hasSigningKeys = false;
  std::string m_payoutAddress;  // wallet address for banking fee rewards
  uint8_t m_myEfid = 0;        // resolved from registration by pubkey
  bool m_efidResolved = false;  // true once we've looked up EFiD

  // Signing thread
  std::thread m_signingThread;
  std::atomic<bool> m_signingRunning{false};
  std::atomic<uint32_t> m_lastSignedHeight{0};
  void signingThread();

  // Sign-once-per-height lock (persisted across restarts to prevent accidental double-signs).
  // Maps blockHeight → merkleRoot we committed to for that height.
  // Any signing attempt at a height already in this map with a DIFFERENT root is refused.
  std::map<uint32_t, Crypto::Hash> m_signedRoots;
  std::string m_signLockPath;  // Set from data dir on construction
  void loadSignLock();
  void persistSignLock(uint32_t height, const Crypto::Hash& root);
  // Returns false if signing would produce a double-sign, true if safe to proceed.
  // also_same_root is set true when the height is already locked to the SAME root (idempotent skip).
  bool checkSignLock(uint32_t height, const Crypto::Hash& root, bool& also_same_root);

  // Helper method to validate signatures
  bool validateSignature(const CachedElderfierSignature& sig) const;
};

}  // namespace CryptoNote
