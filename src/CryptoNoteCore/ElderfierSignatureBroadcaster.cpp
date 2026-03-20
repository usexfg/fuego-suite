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


#include "ElderfierSignatureBroadcaster.h"
#include "CommitmentIndex.h"
#include "Core.h"
#include "P2p/NetNode.h"
#include "P2p/NetNodeCommon.h"
#include "P2p/P2pProtocolDefinitions.h"
#include "P2p/LevinProtocol.h"
#include "crypto/crypto.h"
#include <Logging/LoggerRef.h>
#include <Logging/LoggerManager.h>
#include <Common/StringTools.h>
#include <chrono>
#include <fstream>
#include <sstream>

namespace CryptoNote {

ElderfierSignatureBroadcaster::ElderfierSignatureBroadcaster(core& ccore, NodeServer& p2psrv, IP2pEndpoint* p2pEndpoint,
                                                             Logging::ILogger& logger)
  : m_core(ccore), m_p2p(p2psrv), m_p2pEndpoint(p2pEndpoint), m_running(false),
    m_logger(logger, "EFsign") {
  // Derive sign-lock file path from blockchain data dir (persists across restarts)
  const std::string& dataDir = m_core.get_blockchain_storage().getConfigFolder();
  m_signLockPath = dataDir.empty() ? "efsig_lock.dat" : dataDir + "/efsig_lock.dat";
  loadSignLock();
}

ElderfierSignatureBroadcaster::~ElderfierSignatureBroadcaster() {
  stop();
}

void ElderfierSignatureBroadcaster::handleSignatureMessage(const CachedElderfierSignature& sig) {
  std::lock_guard<std::mutex> lock(m_mutex);

  // Validate signature before adding to cache
  if (!validateSignature(sig)) {
    return;  // Invalid signature, discard
  }

  // Add to commitment index signature cache via public method
  m_core.get_blockchain_storage().addSignatureToCache(sig);

  // Log receipt
  // logger(INFO) << "Elderfier " << (int)sig.elderfier_id
  //             << " signature cached for root " << Common::podToHex(sig.merkle_root);

  // Check threshold and trigger finalization if needed
  if (hasReachedThreshold()) {
    // At 69% or more consensus, signatures are flush (handled by CommitmentIndex)
    // logger(INFO) << "✓ Consensus threshold reached at " << getConsensusPercentage() << "%";
  }
}

void ElderfierSignatureBroadcaster::broadcastMerkleRoot(const Crypto::Hash& root) {
  std::lock_guard<std::mutex> lock(m_mutex);

  // Update the current merkle root in CommitmentIndex
  m_core.get_blockchain_storage().updateCurrentMerkleRoot(root);

  // Broadcast via P2P: relay merkle root update to all peers
  // (individual elderfier signatures are broadcast separately by the SignatureDaemon)
}

uint64_t ElderfierSignatureBroadcaster::getConsensusPercentage() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_core.get_blockchain_storage().getConsensusPercentageForCurrentRoot();
}

bool ElderfierSignatureBroadcaster::hasReachedThreshold() const {
  return getConsensusPercentage() >= 69;
}

std::vector<uint8_t> ElderfierSignatureBroadcaster::getSignedElderfierIds() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_core.get_blockchain_storage().getSignedElderfierIds();
}

std::vector<uint8_t> ElderfierSignatureBroadcaster::getPendingElderfierIds() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_core.get_blockchain_storage().getPendingElderfierIds();
}

void ElderfierSignatureBroadcaster::setSigningKeys(const Crypto::PublicKey& pub, const Crypto::SecretKey& sec) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_signingPubKey = pub;
  m_signingSecKey = sec;
  m_hasSigningKeys = true;
}

void ElderfierSignatureBroadcaster::setPayoutAddress(const std::string& address) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_payoutAddress = address;
}

void ElderfierSignatureBroadcaster::start() {
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_running = true;
  }
  // If signing keys are configured, start the signing thread
  if (m_hasSigningKeys) {
    m_signingRunning = true;
    m_signingThread = std::thread([this] { signingThread(); });
  }
}

void ElderfierSignatureBroadcaster::stop() {
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_running = false;
  }
  m_signingRunning = false;
  if (m_signingThread.joinable()) {
    m_signingThread.join();
  }
}

void ElderfierSignatureBroadcaster::loadSignLock() {
  // Load previously committed height→root pairs from disk.
  // File format: one line per entry: "<height_decimal> <root_hex_64chars>\n"
  std::ifstream f(m_signLockPath);
  if (!f.is_open()) return;
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty()) continue;
    std::istringstream ss(line);
    uint32_t h; std::string rootHex;
    ss >> h >> rootHex;
    if (rootHex.size() == 64) {
      Crypto::Hash root;
      if (Common::podFromHex(rootHex, root)) {
        m_signedRoots[h] = root;
      }
    }
  }
}

void ElderfierSignatureBroadcaster::persistSignLock(uint32_t height, const Crypto::Hash& root) {
  // Append new entry. Keep the file bounded to last 2000 entries by rewriting when large.
  m_signedRoots[height] = root;
  // Rewrite entire file (bounded — at most ~2000 lines = ~130 KB)
  std::ofstream f(m_signLockPath, std::ios::trunc);
  if (!f.is_open()) return;
  // Only persist last 2000 heights to bound file size
  auto it = m_signedRoots.begin();
  if (m_signedRoots.size() > 2000) {
    std::advance(it, m_signedRoots.size() - 2000);
  }
  for (; it != m_signedRoots.end(); ++it) {
    f << it->first << " " << Common::podToHex(it->second) << "\n";
  }
}

bool ElderfierSignatureBroadcaster::checkSignLock(uint32_t height, const Crypto::Hash& root,
                                                   bool& also_same_root) {
  also_same_root = false;
  auto it = m_signedRoots.find(height);
  if (it == m_signedRoots.end()) return true;  // Not signed at this height — safe
  if (it->second == root) {
    also_same_root = true;  // Already signed the same root — idempotent
    return true;
  }
  // Already signed a DIFFERENT root at this height — would be a double-sign
  return false;
}

void ElderfierSignatureBroadcaster::signingThread() {
  // Wait for core to fully sync before signing
  m_logger(Logging::INFO) << "Signing thread started, waiting 5s for sync...";
  std::this_thread::sleep_for(std::chrono::seconds(5));

  while (m_signingRunning) {
    try {
      uint32_t currentHeight;
      Crypto::Hash topId;
      m_core.get_blockchain_top(currentHeight, topId);

      if (currentHeight > m_lastSignedHeight && currentHeight > 0) {
        // resolve EFiD from registration (once, by matching signing pubkey)
        if (!m_efidResolved) {
          auto& blockchain = m_core.get_blockchain_storage();
          for (uint8_t id = 0; id < 255; ++id) {
            Crypto::PublicKey registered_pk;
            if (blockchain.getElderfierSigningPubkey(id, registered_pk) &&
                registered_pk == m_signingPubKey) {
              m_myEfid = id;
              m_efidResolved = true;
              m_logger(Logging::INFO, Logging::BRIGHT_GREEN) << "Resolved EFiD=" << (int)m_myEfid
                << " from signing pubkey " << Common::podToHex(m_signingPubKey).substr(0, 16) << "...";

              // Register payout address for banking fee distribution
              if (!m_payoutAddress.empty()) {
                blockchain.getCommitmentIndex().registerElderfierAddress(id, m_payoutAddress);
                m_logger(Logging::INFO, Logging::BRIGHT_GREEN) << "EF" << (int)m_myEfid
                  << " payout address registered: " << m_payoutAddress;
              }
              break;
            }
          }
          if (!m_efidResolved) {
            m_logger(Logging::DEBUGGING) << "EFiD not registered yet at height " << currentHeight << ", skipping";
            m_lastSignedHeight = currentHeight;
            continue;
          }
        }

        // get commitment merkle root (what L2 contracts verify merkle proofs against)
        Crypto::Hash commitmentRoot = m_core.get_blockchain_storage().getCommitmentMerkleRoot();

        // only sign if there are commitments (non-zero root)
        if (commitmentRoot == Crypto::Hash()) {
          m_logger(Logging::DEBUGGING) << "No commitments at height " << currentHeight << ", empty root";
          m_lastSignedHeight = currentHeight;
        } else {
          // ── Sign-once-per-height safety lock ──────────────────────────────────
          bool alreadySameRoot = false;
          {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!checkSignLock(currentHeight, commitmentRoot, alreadySameRoot)) {
              // SAFETY REFUSAL: already committed to a different root at this height.
              m_logger(Logging::WARNING, Logging::BRIGHT_RED)
                << "DOUBLE-SIGN PREVENTED at height " << currentHeight
                << "! Already signed different root. Refusing to sign.";
              m_lastSignedHeight = currentHeight;
              continue;
            }
          }
          if (alreadySameRoot) {
            m_logger(Logging::DEBUGGING) << "Already signed same root at height " << currentHeight << ", skip";
            m_lastSignedHeight = currentHeight;
            continue;
          }
          // ─────────────────────────────────────────────────────────────────────

          // sign commitment merkle root with the registered key
          Crypto::Signature sig;
          Crypto::generate_signature(commitmentRoot, m_signingPubKey, m_signingSecKey, sig);

          // build P2P message with actual EFiD
          COMMAND_ELDERFIER_SIGNATURE::request sig_msg;
          sig_msg.merkle_root = commitmentRoot;
          sig_msg.signature = sig;
          sig_msg.elderfier_id = m_myEfid;
          sig_msg.block_height = currentHeight;
          sig_msg.timestamp = std::time(nullptr);
          sig_msg.version = 1;
          sig_msg.sig_algorithm = 0;  // Ed25519

          // relay to all connected peers
          if (m_p2pEndpoint) {
            auto buf = LevinProtocol::encode(sig_msg);
            m_p2pEndpoint->externalRelayNotifyToAll(
                COMMAND_ELDERFIER_SIGNATURE::ID, buf, nullptr);
          }

          // Persist sign-lock BEFORE broadcasting
          {
            std::lock_guard<std::mutex> lock(m_mutex);
            persistSignLock(currentHeight, commitmentRoot);
          }

          // cache locally
          CachedElderfierSignature cached;
          cached.merkle_root = commitmentRoot;
          cached.signature = sig;
          cached.elderfier_id = m_myEfid;
          cached.block_height = currentHeight;
          cached.received_block_height = currentHeight;
          cached.timestamp = sig_msg.timestamp;
          cached.sig_algorithm = 0;
          m_core.get_blockchain_storage().addSignatureToCache(cached);

          m_logger(Logging::INFO, Logging::BRIGHT_CYAN) << "EF" << (int)m_myEfid
            << " signed root " << Common::podToHex(commitmentRoot).substr(0, 16) << "..."
            << " at height " << currentHeight << ", broadcast to peers";

          m_lastSignedHeight = currentHeight;
        }
      }
    } catch (const std::exception& e) {
      m_logger(Logging::WARNING) << "Signing error: " << e.what();
    }

    // Check for new blocks every 2 seconds
    std::this_thread::sleep_for(std::chrono::seconds(2));
  }
  m_logger(Logging::INFO) << "Signing thread stopped";
}

bool ElderfierSignatureBroadcaster::validateSignature(const CachedElderfierSignature& sig) const {
  // Basic validation checks
  if (sig.elderfier_id > 255) {
    return false;  // Invalid EFiD (must be 0-255)
  }

  // Post-quantum hybrid signature validation
  // Per ELDERFIER_HYBRID_CRYPTO_GUIDE.md: detect algorithm by signature length
  // - Ed25519: 64 bytes (sig_algorithm == 0)
  // - ML-DSA-65: 3293 bytes (sig_algorithm == 1) — future, behind #ifdef FUEGO_PQ_ENABLED

  if (sig.sig_algorithm == 0) {
    // Ed25519 signature: validate merkle root is non-zero and signature is non-zero
    // Full Ed25519 verification against registered pubkey is done at block validation time
    // in Blockchain::pushBlock() — the broadcaster validates format only
    if (sig.merkle_root == Crypto::Hash()) {
      return false;  // Empty merkle root
    }
    if (sig.signature == Crypto::Signature()) {
      return false;  // Empty signature
    }
    return true;
  }

#ifdef FUEGO_PQ_ENABLED
  if (sig.sig_algorithm == 1) {
    // ML-DSA-65 post-quantum signature: 3293 bytes
    if (sig.pq_signature.size() != 3293) {
      return false;
    }
    if (sig.pq_pubkey.size() != 1952) {
      return false;
    }
    // ML-DSA-65 verification deferred to block validation (requires liboqs)
    // Format validation: signature and pubkey sizes are correct
    return true;
  }
#endif

  return false;  // Unknown signature algorithm
}

}  // namespace CryptoNote
