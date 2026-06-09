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

#pragma once

#include "OfferManager.h"
#include "StatusServer.h"
#include "SwapTypes.h"
#include "SwapStateMachine.h"
#include "SwapDatabase.h"
#include "SwapTxBuilder.h"
#include "SwapPeerProtocol.h"
#include "FuegoRpcClient.h"
#include "PriceOracle.h"
#include "../Logging/ILogger.h"
#include "../Logging/LoggerRef.h"
#include "PoolOrganizer.h"
#include "ChainRegistry.h"

namespace CryptoNote {
  class SwapOfferRelay;
}

#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <map>
#include <vector>
#include <ctime>

namespace XfgSwap {

// Configuration for counterparty chain RPC endpoints.
// Pass to SwapDaemon constructor to wire per-chain clients.
// Leave host empty ("") for any chain that is not in use.
struct ChainClientConfig {
  // BCH
  std::string bchHost;
  uint16_t    bchPort     = 8332;
  std::string bchRpcUser;
  std::string bchRpcPass;
  std::string bchWif;          // WIF-encoded private key for HTLC signing

  // ETH
  std::string ethHost;
  uint16_t    ethPort     = 8545;

  // SOL
  std::string solHost;
  uint16_t    solPort     = 8899;
  std::string solProgramId;  // xfg_htlc program ID (base58)

  // XMR
  std::string xmrDaemonHost;
  uint16_t    xmrDaemonPort = 18081;
  std::string xmrWalletHost;
  uint16_t    xmrWalletPort = 18082;

  // ── Signer credentials ────────────────────────────────────────────────────
  // ETH private key (64 hex chars, 32 bytes) and derived address ("0x...")
  std::string ethPrivKeyHex;
  std::string ethAddress;
  uint64_t    ethChainId = 1;  // EIP-155 chain ID (1=mainnet, 11155111=Sepolia)
  // Optional: path to the pre-compiled HashedTimelock .bin file
  std::string ethHtlcBinPath;

  // ARB
  std::string arbHost;
  uint16_t    arbPort     = 8547;

  // ARB signer credentials (reuses ETH private key derivation; separate key optional)
  std::string arbPrivKeyHex;
  std::string arbAddress;
  uint64_t    arbChainId = 42161;
  std::string arbHtlcBinPath;

  // BASE (Base L2 — EVM, EIP-1559)
  std::string baseHost;
  uint16_t    basePort     = 8545;
  std::string basePrivKeyHex;
  std::string baseAddress;
  uint64_t    baseChainId  = 8453;
  std::string baseHtlcBinPath;

  // XMR spend/view keys (64 hex chars each)
  std::string xmrSpendKeyHex;
  std::string xmrViewKeyHex;

  // Solana keypair JSON file path (as produced by `solana-keygen new`)
  std::string solKeypairPath;

  // XFG wallet key for signing managed offers (hex-encoded 64-char Ed25519 secret key)
  std::string xfgSecretKeyHex;
  std::string xfgViewKeyHex;

  // Fuego wallet RPC config for escrow funding
  std::string xfgWalletRpcHost;
  uint16_t    xfgWalletRpcPort = 0;
  std::string xfgWalletRpcUser;
  std::string xfgWalletRpcPass;
};

class SwapDaemon {
public:
  // Construct with only the Fuegod connection.  Chain clients are disabled;
  // processSwap() will log a warning and skip counterparty-chain steps.
  SwapDaemon(const std::string& fuegodHost, uint16_t fuegodPort,
             const std::string& dataDir, Logging::ILogger& logger);

  // Construct with Fuegod connection and counterparty chain RPC config.
  // For any chain whose host is empty the corresponding client is not created.
  SwapDaemon(const std::string& fuegodHost, uint16_t fuegodPort,
             const std::string& dataDir, Logging::ILogger& logger,
             const ChainClientConfig& chainCfg);

  ~SwapDaemon();

  // Load persisted non-terminal swaps, log recovery summary, and start the
  // background tick thread.  Call once after construction.
  void start();

  // Stop the background tick thread.  Safe to call multiple times.
  void stop();

  // Configure wallet RPC endpoint for escrow funding.
  // Must be called before processSwap() can fund escrow.
  void setWalletRpc(const std::string& host, uint16_t port);

  void setSwapRelay(CryptoNote::SwapOfferRelay* relay) { m_swapRelay = relay; }

  void setMakerKeys(const Crypto::SecretKey& sk, const Crypto::PublicKey& pk);
  bool loadOfferConfig(const std::string& jsonPath);

  OfferManager& offerManager() { return *m_offerManager; }

  bool startStatusServer(uint16_t port);

  std::string buildStatusJson();

  // Start a new swap as initiator (Bob: has XFG, wants counterparty coin).
  bool initiate(SwapParams params);

  // Accept an incoming swap proposal.
  struct AcceptResult {
    bool success;
    std::string warning;
  };
  AcceptResult accept(const std::string& swapId);
  
  // Handle an incoming swap request from a taker for a soft order
  bool handleSwapRequest(const std::string& offerId, uint64_t amount,
                         const std::string& takerPubKey, const std::string& proofOfFunds);

  // Returns active AFK offers that are still valid (>= 1 hour remaining)
  std::vector<SwapStateMachine> getActiveAfkOffers();

  // Scan active swaps and refund any that have timed out.

  bool checkTimeouts();

  // Advance a specific swap to its next state based on chain observations.
  bool processSwap(const std::string& swapId);
  bool processSwap(SwapStateMachine& sm);  // avoids duplicate DB load in tick loop

  // Print a summary of all swaps.
  void listSwaps();

  // Print detailed info about a specific swap.
  void showSwap(const std::string& swapId);

   // Attempt to refund a specific swap (if timeout has elapsed).
   bool refund(const std::string& swapId);

   // Access the price oracle for configuration.
   PriceOracle& priceOracle();

   // Pool operations
   bool createPool(const PoolId& poolId);
   bool getPool(const PoolId& poolId, PoolState& state) const;
   std::vector<PoolId> getActivePools() const;
   PoolCheckpoint processDeposit(const LPDepositParams& params, uint64_t shareAmount);
   PoolCheckpoint processWithdrawal(const LPWithdrawalParams& params, WithdrawalAmounts& amounts);
   PoolOrganizer::SwapResult executeSwap(const PoolSwapOrder& order);
   uint64_t getExpectedOutput(const PoolId& poolId, bool swapAforB, uint64_t inputAmount) const;
   PoolOrganizer::ClaimableFees getClaimableFees(const Crypto::PublicKey& owner, const PoolId& poolId) const;
   PoolCheckpoint processFeeClaim(const Crypto::PublicKey& owner, const PoolId& poolId, PoolOrganizer::ClaimableFees& claimed);
   PoolCheckpoint generateCheckpoint(const PoolId& poolId);
   bool getCurrentCheckpoint(const PoolId& poolId, PoolCheckpoint& checkpoint) const;
   bool verifyCheckpoint(const PoolId& poolId, const PoolCheckpoint& checkpoint) const;
   bool getLPShares(const Crypto::PublicKey& owner, const PoolId& poolId, LPShare& shares) const;
   std::vector<Crypto::Hash> getLPShareProof(const Crypto::PublicKey& owner, const PoolId& poolId, size_t& leafIndex) const;
   PoolOrganizer::PoolStats getPoolStats(const PoolId& poolId) const;
   uint64_t getSpotPrice(const PoolId& poolId) const;

 private:
  // Scan non-terminal swaps and warn about any stuck longer than threshold.
  // Called from checkTimeouts().
  void checkStuckSwaps();

  static constexpr int SWAP_STUCK_THRESHOLD_ESCROW_SECS = 1800;  // 30 min
  static constexpr int SWAP_STUCK_THRESHOLD_KEYS_SECS   = 600;   // 10 min

  // Generate a unique swap ID from the current time and random data.
  std::string generateSwapId();

  // Fund the XFG escrow by sending to the Musig2 joint key address.
  // Computes escrow address from params.escrowPubKey, sends XFG via
  // wallet RPC, and stores the resulting tx hash in params.
  // Returns true on success.
  bool fundEscrow(SwapParams& params);

  // Per-state handlers extracted from processSwap for readability
  bool handlePreSigsReady(SwapStateMachine& sm);
  bool handleSecretRevealed(SwapStateMachine& sm);
  bool handleCtrLocked(SwapStateMachine& sm);
  bool handleEscrowFunded(SwapStateMachine& sm, uint32_t currentHeight);

  // Verify that the escrow funding tx exists and contains an output
  // with the expected amount to the joint escrow key.
  // Returns true if the escrow is confirmed on chain.
  bool verifyEscrowFunding(const SwapParams& params);

  // Returns the resolved XFG address. If input is an alias (@name or short name),
  // resolves via RPC. If already an address, returns as-is. Returns "" on failure.
  std::string resolveAddressOrAlias(const std::string& input);

  // Build an unsigned escrow-spend tx, run collaborative ring sig rounds
  // with the peer, attach the final signature, and broadcast.
  // txType: "spend" (adapted, Bob claims) or "refund" (cooperative, both sign)
  bool buildAndBroadcastEscrowTx(SwapParams& params,
                                 const Crypto::PublicKey& destinationKey,
                                 const std::string& txType);

  // Handle an incoming peer message for an active swap.
  bool handlePeerMessage(const PeerMessage& msg);

  // Background tick thread — runs checkTimeouts + processSwap every 30 s
  void tickLoop();

  static constexpr int TICK_INTERVAL_SECS = 30;

   FuegoRpcClient m_rpc;
   SwapDatabase m_db;
   PriceOracle m_oracle;
   PoolOrganizer m_poolOrganizer;
   Logging::LoggerRef m_logger;
   ChainRegistry m_chainRegistry;

    CryptoNote::SwapOfferRelay* m_swapRelay = nullptr;
    std::unique_ptr<OfferManager> m_offerManager;
    std::unique_ptr<StatusServer> m_statusServer;

    Crypto::SecretKey m_makerSecretKey;
    Crypto::PublicKey m_makerPublicKey;
    Crypto::SecretKey m_makerViewSecretKey;
    bool m_makerKeysSet = false;

    std::string m_xfgWalletRpcHost;
    uint16_t m_xfgWalletRpcPort = 0;
    std::string m_xfgWalletRpcUser;
    std::string m_xfgWalletRpcPass;

   struct TakerRecord {
     std::vector<time_t> requestTimes;
     uint32_t failedSwaps = 0;
   };
   std::mutex m_takerMutex;
   std::map<std::string, TakerRecord> m_takerHistory;
   static constexpr uint32_t MAX_TAKER_REQUESTS_PER_HOUR = 5;
   static constexpr uint32_t TAKER_BAN_THRESHOLD = 3;

   bool isTakerRateLimited(const std::string& takerPubKey);
   void recordTakerFailure(const std::string& takerPubKey);

   std::thread           m_tickThread;
   std::atomic<bool>     m_running{false};
   std::mutex            m_tickMutex;
    std::condition_variable m_tickCv;
};

// Load a ChainClientConfig from a JSON file.
// Returns true on success; sets errorMsg on failure.
bool loadChainClientConfig(const std::string& path,
                            ChainClientConfig& out,
                            std::string& errorMsg);

} // namespace XfgSwap
