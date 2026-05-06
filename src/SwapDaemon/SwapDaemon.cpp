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

#include "SwapDaemon.h"
#include "AdaptorSwap.h"
#include "SwapTxBuilder.h"
#include "SwapPeerProtocol.h"
#include "Common/StringTools.h"
#include "CryptoNoteCore/CryptoNoteTools.h"
#include "CryptoNoteConfig.h"
#include "CryptoNoteCore/SwapOfferRelay.h"
#include "crypto/hash.h"
#include "crypto/crypto.h"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <ctime>
#include <stdexcept>
#include "../Logging/ILogger.h"

namespace XfgSwap {

SwapDaemon::SwapDaemon(const std::string& fuegodHost, uint16_t fuegodPort,
                        const std::string& dataDir, Logging::ILogger& logger)
  : m_rpc(fuegodHost, fuegodPort)
  , m_db(dataDir)
  , m_poolOrganizer(logger)
  , m_logger(logger, "SwapDaemon") {
  // Chain clients not configured — processSwap() will warn if needed.
}

SwapDaemon::SwapDaemon(const std::string& fuegodHost, uint16_t fuegodPort,
                        const std::string& dataDir, Logging::ILogger& logger,
                        const ChainClientConfig& chainCfg)
  : m_rpc(fuegodHost, fuegodPort)
  , m_db(dataDir)
  , m_poolOrganizer(logger)
  , m_logger(logger, "SwapDaemon") {
  if (!chainCfg.bchHost.empty()) {
    m_bchClient = std::make_unique<BchRpcClient>(
        chainCfg.bchHost, chainCfg.bchPort,
        chainCfg.bchRpcUser, chainCfg.bchRpcPass);
    m_logger(Logging::INFO) << "BCH RPC client configured: "
      << chainCfg.bchHost << ":" << chainCfg.bchPort;
  }
  if (!chainCfg.ethHost.empty()) {
    m_ethClient = std::make_unique<EthRpcClient>(
        chainCfg.ethHost, chainCfg.ethPort);
    m_logger(Logging::INFO) << "ETH RPC client configured: "
      << chainCfg.ethHost << ":" << chainCfg.ethPort;
  }
  if (!chainCfg.solHost.empty()) {
    m_solClient = std::make_unique<SolRpcClient>(
        chainCfg.solHost, chainCfg.solPort, chainCfg.solProgramId);
    m_logger(Logging::INFO) << "SOL RPC client configured: "
      << chainCfg.solHost << ":" << chainCfg.solPort;
  }
  if (!chainCfg.xmrDaemonHost.empty()) {
    m_xmrClient = std::make_unique<MoneroRpcClient>(
        chainCfg.xmrDaemonHost, chainCfg.xmrDaemonPort,
        chainCfg.xmrWalletHost, chainCfg.xmrWalletPort);
    m_logger(Logging::INFO) << "XMR RPC client configured: "
      << chainCfg.xmrDaemonHost << ":" << chainCfg.xmrDaemonPort;
  }
}

SwapDaemon::~SwapDaemon() {
  stop();
}

void SwapDaemon::start() {
  std::vector<std::string> swapIds = m_db.listSwaps();
  int recovered = 0;
  for (const auto& id : swapIds) {
    SwapStateMachine sm;
    if (m_db.loadSwap(id, sm) && !sm.isTerminal()) {
      m_logger(Logging::INFO) << "Recovered in-progress swap " << id
        << " state=" << swapStateToString(sm.currentState());
      recovered++;
    }
  }
  if (recovered > 0) {
    m_logger(Logging::INFO) << "Recovered " << recovered << " in-progress swap(s)";
  }

  // Handle any swaps that expired while daemon was offline
  checkTimeouts();

  m_running.store(true);
  m_tickThread = std::thread(&SwapDaemon::tickLoop, this);
  m_logger(Logging::INFO) << "SwapDaemon tick thread started (interval=" << TICK_INTERVAL_SECS << "s)";
}

void SwapDaemon::stop() {
  if (!m_running.exchange(false)) return;
  m_tickCv.notify_all();
  if (m_tickThread.joinable()) {
    m_tickThread.join();
  }
  m_logger(Logging::INFO) << "SwapDaemon tick thread stopped";
}

void SwapDaemon::tickLoop() {
  while (m_running.load()) {
    // Wait for TICK_INTERVAL_SECS or until stop() wakes us
    std::unique_lock<std::mutex> lock(m_tickMutex);
    m_tickCv.wait_for(lock, std::chrono::seconds(TICK_INTERVAL_SECS),
                      [this]{ return !m_running.load(); });
    if (!m_running.load()) break;

    // checkTimeouts handles refunds for all expired swaps
    checkTimeouts();

    // Process pending soft order requests from SwapOfferRelay
    if (m_swapRelay) {
      auto pendingRequests = m_swapRelay->getPendingSwapRequests();
      for (const auto& req : pendingRequests) {
        handleSwapRequest(std::get<0>(req), std::get<1>(req), std::get<2>(req), std::get<3>(req));
      }
    }

    // Advance every non-terminal swap one step
    auto swapIds = m_db.listSwaps();
    for (const auto& id : swapIds) {
      SwapStateMachine sm;
      if (!m_db.loadSwap(id, sm)) continue;
      if (sm.isTerminal()) continue;
      processSwap(id);
    }
  }
}

std::string SwapDaemon::generateSwapId() {
  struct {
    time_t timestamp;
    uint8_t random[32];
  } seed;

  seed.timestamp = std::time(nullptr);
  Crypto::generate_random_bytes(sizeof(seed.random), seed.random);

  Crypto::Hash hash;
  Crypto::cn_fast_hash(&seed, sizeof(seed), hash);

  return Common::toHex(hash.data, 16);
}

std::string SwapDaemon::resolveAddressOrAlias(const std::string& input) {
  if (input.empty()) return "";
  // XFG addresses are 98 chars and start with lowercase 'f' (e.g. fireVHx...)
  // Anything shorter or not starting with 'f' is treated as an alias candidate
  const bool looksLikeAlias = input.length() < 98 || input[0] != 'f';
  if (looksLikeAlias) {
    std::string candidate = input;
    if (!candidate.empty() && candidate[0] == '@') candidate = candidate.substr(1);
    std::string resolved;
    if (m_rpc.resolveAlias(candidate, resolved)) {
      return resolved;
    }
  }
  return input; // treat as raw address
}

bool SwapDaemon::initiate(SwapParams params) {
  uint32_t currentHeight = 0;
  if (!m_rpc.getHeight(currentHeight)) {
    m_logger(Logging::ERROR) << "Cannot connect to fuegod";
    return false;
  }

  m_logger(Logging::INFO) << "Connected to fuegod at height " << currentHeight;

  params.ctrAddress = resolveAddressOrAlias(params.ctrAddress);

  if (params.swapId.empty()) {
    params.swapId = generateSwapId();
  }

  // Set default XFG timeout (cooperative refund window: ~1 day)
  if (params.xfgTimeoutHeight == 0) {
    params.xfgTimeoutHeight = currentHeight + 180;
  }

  // Timelock ordering: Alice's XFG refund window must strictly exceed Bob's
  // counterparty timeout so Alice can always reclaim XFG if Bob goes silent.
  if (params.ctrTimeoutBlock != 0 &&
      params.xfgTimeoutHeight <= params.ctrTimeoutBlock) {
    m_logger(Logging::ERROR)
      << "Timelock ordering violation: xfgTimeoutHeight ("
      << params.xfgTimeoutHeight << ") must exceed ctrTimeoutBlock ("
      << params.ctrTimeoutBlock << ")";
    return false;
  }

  // Validate price against TWAP
  RateCheck rc = m_oracle.validateSwapAmounts(params.pair, params.xfgAmount, params.ctrAmount);
  if (rc == RateCheck::BELOW_FLOOR) {
    m_logger(Logging::ERROR)
      << "Swap rate rejected: XFG priced >= 50% below TWAP floor. "
      << PriceOracle::rateCheckToString(rc);
    return false;
  }
  if (rc == RateCheck::ABOVE_MARKET) {
    m_logger(Logging::WARNING)
      << "Swap rate is significantly above market TWAP. Proceeding.";
  }
  if (rc == RateCheck::RATE_NO_DATA) {
    m_logger(Logging::INFO)
      << "No TWAP data yet (bootstrap mode). Seed rate: "
      << PriceOracle::getSeedRate(params.pair) << " XFG per 1 "
      << swapPairToString(params.pair);
  }

  // ── Adaptor sig step 1: generate swap keypair ──
  adaptor_generate_keys(params);

  m_logger(Logging::INFO) << "Generated swap keypair: "
    << Common::podToHex(params.ourSwapPubKey);

  SwapStateMachine sm(params);

  if (!m_db.saveSwap(sm)) {
    m_logger(Logging::ERROR) << "Failed to save swap to database";
    return false;
  }

  m_logger(Logging::INFO) << "Swap initiated: " << params.swapId;
  m_logger(Logging::INFO) << "  Pair: XFG/" << swapPairToString(params.pair);
  m_logger(Logging::INFO) << "  Role: " << (params.role == SwapRole::BOB ? "BOB (selling XFG)" : "ALICE (buying XFG)");
  m_logger(Logging::INFO) << "  XFG amount: " << params.xfgAmount << " atomic";
  m_logger(Logging::INFO) << "  CTR amount: " << params.ctrAmount << " atomic";
  m_logger(Logging::INFO) << "  Timeout height: " << params.xfgTimeoutHeight;
  m_logger(Logging::INFO) << "  Our swap pubkey: " << Common::podToHex(params.ourSwapPubKey);
  m_logger(Logging::INFO) << "  Share this swap ID with your counterparty: " << params.swapId;

  return true;
}

SwapDaemon::AcceptResult SwapDaemon::accept(const std::string& swapId) {
  SwapStateMachine sm;
  if (!m_db.loadSwap(swapId, sm)) {
    m_logger(Logging::ERROR) << "Swap not found: " << swapId;
    return {false, ""};
  }
  
  if (sm.currentState() != SwapState::INITIATED && 
      sm.currentState() != SwapState::AFK_OFFER_LOCKED) {
    m_logger(Logging::ERROR) << "Swap is not in a state that can be accepted (current: "
                               << swapStateToString(sm.currentState()) << ")";
    return {false, ""};
  }
  
  auto& params = sm.params();
  std::string warning = "";
  
    // AFK Safety Check: Check remaining time of Maker's lock
    if (sm.currentState() == SwapState::AFK_OFFER_LOCKED) {
      uint32_t currentHeight = 0;
      if (!m_rpc.getHeight(currentHeight)) {
        m_logger(Logging::ERROR) << "Cannot query fuegod height";
        return {false, ""};
      }
      
      // Fuego block time = 480s (8 min). 1 hour = 7.5 blocks. Use 8 blocks as safe minimum.
      int32_t remainingBlocks = static_cast<int32_t>(params.xfgTimeoutHeight) - currentHeight;
      
      if (remainingBlocks < 8) {
        m_logger(Logging::ERROR) << "AFK offer expired or too close to expiry (" 
                                 << remainingBlocks << " blocks left). Acceptance rejected.";
        return {false, ""};
      }
      
      if (remainingBlocks < 64) { // 8 hours = 64 blocks (8 * 8)
        double remainingHrs = remainingBlocks / 7.5;
        warning = "This offer is under 8 hours from expiry. Please be aware you only have " 
                  + std::to_string(remainingHrs) + " hours to claim your funds before maker has access to funds again to initiate a refund.";
      }
    }


  // Timelock ordering check
  if (params.ctrTimeoutBlock != 0 &&
      params.xfgTimeoutHeight <= params.ctrTimeoutBlock) {
    m_logger(Logging::ERROR)
      << "Timelock ordering violation: xfgTimeoutHeight ("
      << params.xfgTimeoutHeight << ") must exceed ctrTimeoutBlock ("
      << params.ctrTimeoutBlock << ")";
    return {false, ""};
  }
  
  // ── Adaptor sig step 2: key aggregation ──
  if (!adaptor_key_aggregate(params)) {
    m_logger(Logging::ERROR) << "Musig2 key aggregation failed";
    return {false, ""};
  }
  
  m_logger(Logging::INFO) << "Musig2 escrow key: "
    << Common::podToHex(params.escrowPubKey);
  
  if (params.role == SwapRole::BOB) {
    if (!adaptor_generate_adaptor(params, params.escrowPubKey)) {
      m_logger(Logging::ERROR) << "Adaptor point generation failed";
      return {false, ""};
    }
    m_logger(Logging::INFO) << "Adaptor point T: "
      << Common::podToHex(params.adaptorPoint);
  }
  
  SwapState newState = (sm.currentState() == SwapState::AFK_OFFER_LOCKED) 
                       ? SwapState::AFK_OFFER_ACCEPTED 
                       : SwapState::ADAPTOR_KEYS_EXCHANGED;

  if (!sm.transition(newState)) {
    m_logger(Logging::ERROR) << "State transition failed to " << swapStateToString(newState);
    return {false, ""};
  }
  
  if (!m_db.saveSwap(sm)) {
    m_logger(Logging::ERROR) << "Failed to save swap state";
    return {false, ""};
  }
  
  m_logger(Logging::INFO) << "Swap " << swapId << " -> " << swapStateToString(sm.currentState());
  return {true, warning};
}


void SwapDaemon::checkStuckSwaps() {
  time_t now = std::time(nullptr);
  for (const auto& id : m_db.listSwaps()) {
    SwapStateMachine sm;
    if (!m_db.loadSwap(id, sm)) continue;
    if (sm.isTerminal()) continue;

    time_t age = now - sm.updatedAt();
    int threshold = (sm.currentState() >= SwapState::ADAPTOR_ESCROW_FUNDED)
      ? SWAP_STUCK_THRESHOLD_ESCROW_SECS
      : SWAP_STUCK_THRESHOLD_KEYS_SECS;

    if (age > threshold) {
      m_logger(Logging::WARNING) << "Swap " << id
        << " stuck in state " << swapStateToString(sm.currentState())
        << " for " << age << "s — consider cooperative refund";
    }
  }
}

bool SwapDaemon::checkTimeouts() {
  uint32_t currentHeight = 0;
  if (!m_rpc.getHeight(currentHeight)) {
    m_logger(Logging::ERROR) << "Cannot query fuegod height";
    return false;
  }

  auto swapIds = m_db.listSwaps();
  bool anyExpired = false;

  for (const auto& swapId : swapIds) {
    SwapStateMachine sm;
    if (!m_db.loadSwap(swapId, sm)) continue;
    if (sm.isTerminal()) continue;

    const auto& params = sm.params();

    if (params.xfgTimeoutHeight > 0 && currentHeight >= params.xfgTimeoutHeight) {
      SwapState current = sm.currentState();

      // Cooperative refund possible from escrow-funded or pre-sigs-ready states.
      // Do NOT transition to REFUNDED here — a real cooperative refund requires
      // exchanging a Musig2 partial with the peer and broadcasting the refund
      // tx. checkTimeouts() only flags the opportunity; the user (or a future
      // peer-protocol layer) must call refund() to actually execute it.
      if ((current == SwapState::ADAPTOR_ESCROW_FUNDED ||
           current == SwapState::ADAPTOR_PRESIGS_READY) &&
          params.role == SwapRole::BOB) {
        m_logger(Logging::WARNING) << "Swap " << swapId
          << " XFG timeout reached at height " << currentHeight
          << " — call 'refund " << swapId << "' to initiate cooperative refund.";
        anyExpired = true;
      }
    }
  }

  if (!anyExpired) {
    m_logger(Logging::DEBUGGING) << "No swaps timed out at height " << currentHeight;
  }

  checkStuckSwaps();

  return true;
}

bool SwapDaemon::processSwap(const std::string& swapId) {
  SwapStateMachine sm;
  if (!m_db.loadSwap(swapId, sm)) {
    m_logger(Logging::ERROR) << "Swap not found: " << swapId;
    return false;
  }

  if (sm.isTerminal()) {
    m_logger(Logging::INFO) << "Swap " << swapId
      << " is in terminal state: " << swapStateToString(sm.currentState());
    return true;
  }

  uint32_t currentHeight = 0;
  if (!m_rpc.getHeight(currentHeight)) {
    m_logger(Logging::ERROR) << "Cannot query fuegod height";
    return false;
  }

  // Use a mutable reference so that chain dispatch can update ctrLockTxId etc.
  // before saving.
  SwapParams& params = sm.params();
  SwapState current = sm.currentState();

  m_logger(Logging::INFO) << "Processing swap " << swapId
    << " state=" << swapStateToString(current)
    << " height=" << currentHeight;

  switch (current) {
    case SwapState::INITIATED:
      m_logger(Logging::INFO) << "  Waiting for peer pubkey. Use 'accept' after exchanging keys.";
      break;

    case SwapState::ADAPTOR_KEYS_EXCHANGED:
      m_logger(Logging::INFO) << "  Keys aggregated. Escrow key: "
        << Common::podToHex(params.escrowPubKey);
      m_logger(Logging::INFO) << "  Next: fund XFG → escrow key, then exchange nonces + pre-sigs.";
      break;

    case SwapState::ADAPTOR_ESCROW_FUNDED:
      m_logger(Logging::INFO) << "  Escrow funded (tx: "
        << Common::podToHex(params.escrowTxHash) << ").";
      // Phase 1: 1% sender surcharge added to escrow amount (total swap fee = 2%: 1% init + 1% claim)
      if (params.xfgAmount > 0) {
        uint64_t senderSurcharge = (params.xfgAmount * CryptoNote::parameters::SWAP_FEE_RATE_BPS) 
                                 / CryptoNote::parameters::SWAP_FEE_RATE_DIVISOR;
        if (senderSurcharge > 0) {
          params.xfgAmount += senderSurcharge;
          m_feePoolBalance += senderSurcharge;
          m_currentEpochSwapFees += senderSurcharge;
          m_logger(Logging::INFO) << "  Swap initiation fee (1%): " << senderSurcharge 
                                   << " XFG added to fee pool";
        }
      }
      m_logger(Logging::INFO) << "  Next: exchange Musig2 nonces and create adaptor pre-sigs.";
      break;

    case SwapState::ADAPTOR_PRESIGS_READY:
      // Bob locks counterparty chain funds once pre-sigs are ready.
      if (params.role == SwapRole::BOB) {
        m_logger(Logging::INFO) << "  Pre-sigs ready. Bob locking counterparty ("
          << swapPairToString(params.pair) << ") funds...";

        bool lockOk = false;
        switch (params.pair) {
          case SwapPair::BCH:
            if (!m_bchClient) {
              m_logger(Logging::ERROR) << "  BCH client not configured — cannot lock HTLC";
            } else {
              // hashLock is SHA256(adaptorSecret) per Phase 5.1.3 fix
              std::string lockTxId;
              lockOk = m_bchClient->lockHtlc(
                  /*senderWif=*/"",  // TODO: inject wallet key via config
                  params.ctrAddress,
                  Common::podToHex(params.adaptorPoint),  // T = t*G used as hashlock seed
                  static_cast<uint32_t>(params.ctrTimeoutBlock),
                  params.ctrAmount,
                  lockTxId);
              if (lockOk) {
                m_logger(Logging::INFO) << "  BCH HTLC locked, txid: " << lockTxId;
                params.ctrLockTxId = lockTxId;
                sm.transition(SwapState::ADAPTOR_CTR_LOCKED);
                m_db.saveSwap(sm);
              } else {
                m_logger(Logging::ERROR) << "  BCH lockHtlc failed (stub — not yet implemented)";
              }
            }
            break;

          case SwapPair::ETH:
            if (!m_ethClient) {
              m_logger(Logging::ERROR) << "  ETH client not configured — cannot deploy HTLC";
            } else {
              try {
                std::string contractAddress;
                lockOk = m_ethClient->deployHtlc(
                    /*fromAddress=*/"",  // TODO: inject ETH address via config
                    params.ctrAddress,
                    Common::podToHex(params.adaptorPoint),
                    params.ctrTimeoutBlock,
                    params.ctrAmount,
                    contractAddress);
                if (lockOk) {
                  m_logger(Logging::INFO) << "  ETH HTLC deployed at: " << contractAddress;
                  params.ctrLockTxId = contractAddress;
                  sm.transition(SwapState::ADAPTOR_CTR_LOCKED);
                  m_db.saveSwap(sm);
                }
              } catch (const std::runtime_error& e) {
                m_logger(Logging::ERROR) << "  ETH signing not implemented: " << e.what()
                  << " — marking swap FAILED";
                sm.transition(SwapState::FAILED);
                m_db.saveSwap(sm);
              }
            }
            break;

          case SwapPair::SOL:
            if (!m_solClient) {
              m_logger(Logging::ERROR) << "  SOL client not configured — cannot lock HTLC";
            } else {
              SolTxResult solResult;
              lockOk = m_solClient->lock(
                  /*senderSecretKey=*/"",  // TODO: inject SOL keypair via config
                  params.ctrAddress,
                  Common::podToHex(params.adaptorPoint),
                  params.ctrTimeoutBlock,
                  params.ctrAmount,
                  solResult);
              if (lockOk && solResult.confirmed) {
                m_logger(Logging::INFO) << "  SOL HTLC locked, sig: " << solResult.signature;
                params.ctrLockTxId = solResult.signature;
                sm.transition(SwapState::ADAPTOR_CTR_LOCKED);
                m_db.saveSwap(sm);
              } else if (!lockOk) {
                m_logger(Logging::ERROR) << "  SOL lock failed: " << solResult.error;
              }
            }
            break;

          case SwapPair::XMR:
            if (!m_xmrClient) {
              m_logger(Logging::ERROR) << "  XMR client not configured — cannot lock adaptor";
            } else {
              MoneroTransferResult xmrResult;
              lockOk = m_xmrClient->lockAdaptor(
                  params.ctrAddress,
                  params.ctrAmount,
                  xmrResult);
              if (lockOk && xmrResult.success) {
                m_logger(Logging::INFO) << "  XMR adaptor locked, txhash: " << xmrResult.txHash;
                params.ctrLockTxId = xmrResult.txHash;
                sm.transition(SwapState::ADAPTOR_CTR_LOCKED);
                m_db.saveSwap(sm);
              } else {
                m_logger(Logging::ERROR) << "  XMR lockAdaptor failed: " << xmrResult.error;
              }
            }
            break;
        }
      } else {
        // Alice: verify Bob has locked on the counterparty chain
        m_logger(Logging::INFO) << "  Pre-sigs ready. Alice verifying counterparty ("
          << swapPairToString(params.pair) << ") lock...";

        bool verified = false;
        switch (params.pair) {
          case SwapPair::BCH:
            if (!m_bchClient) {
              m_logger(Logging::WARNING) << "  BCH client not configured — cannot verify lock";
            } else {
              verified = m_bchClient->verifyLock(params.ctrLockTxId, params.ctrAmount);
            }
            break;
          case SwapPair::ETH:
            if (!m_ethClient) {
              m_logger(Logging::WARNING) << "  ETH client not configured — cannot verify lock";
            } else {
              verified = m_ethClient->verifyLock(params.ctrLockTxId, params.ctrAmount);
            }
            break;
          case SwapPair::SOL:
            if (!m_solClient) {
              m_logger(Logging::WARNING) << "  SOL client not configured — cannot verify lock";
            } else {
              verified = m_solClient->verifyLock(params.ctrLockTxId, params.ctrAmount);
            }
            break;
          case SwapPair::XMR:
            if (!m_xmrClient) {
              m_logger(Logging::WARNING) << "  XMR client not configured — cannot verify lock";
            } else {
              verified = m_xmrClient->verifyLock(params.ctrAddress, params.ctrAmount);
            }
            break;
        }

        if (verified) {
          m_logger(Logging::INFO) << "  Counterparty lock verified. Transitioning to CTR_LOCKED.";
          sm.transition(SwapState::ADAPTOR_CTR_LOCKED);
          m_db.saveSwap(sm);
        } else {
          m_logger(Logging::INFO) << "  Counterparty lock not yet verified — will retry next tick.";
        }
      }
      break;

    case SwapState::ADAPTOR_CTR_LOCKED:
      if (params.role == SwapRole::ALICE) {
        // Alice claims the counterparty funds, revealing the adaptor secret.
        m_logger(Logging::INFO) << "  " << swapPairToString(params.pair)
          << " locked. Alice claiming to reveal adaptor secret...";

        bool claimOk = false;
        switch (params.pair) {
          case SwapPair::BCH:
            if (!m_bchClient) {
              m_logger(Logging::ERROR) << "  BCH client not configured — cannot claim";
            } else {
              std::string claimTxId;
              claimOk = m_bchClient->claim(
                  /*claimerWif=*/"",  // TODO: inject via config
                  params.ctrLockTxId, 0, params.ctrAmount,
                  params.bchRedeemScriptHex,
                  Common::podToHex(params.adaptorSecret),
                  params.ctrAddress,
                  claimTxId);
              if (claimOk) {
                m_logger(Logging::INFO) << "  BCH claimed, txid: " << claimTxId;
                sm.transition(SwapState::ADAPTOR_SECRET_REVEALED);
                m_db.saveSwap(sm);
              } else {
                m_logger(Logging::ERROR) << "  BCH claim failed (stub)";
              }
            }
            break;

          case SwapPair::ETH:
            if (!m_ethClient) {
              m_logger(Logging::ERROR) << "  ETH client not configured — cannot claim";
            } else {
              try {
                std::string claimTxHash;
                claimOk = m_ethClient->claimHtlc(
                    /*fromAddress=*/"",
                    params.ctrLockTxId,
                    Common::podToHex(params.adaptorSecret),
                    claimTxHash);
                if (claimOk) {
                  m_logger(Logging::INFO) << "  ETH claimed, tx: " << claimTxHash;
                  sm.transition(SwapState::ADAPTOR_SECRET_REVEALED);
                  m_db.saveSwap(sm);
                }
              } catch (const std::runtime_error& e) {
                m_logger(Logging::ERROR) << "  ETH claim not implemented: " << e.what()
                  << " — marking swap FAILED";
                sm.transition(SwapState::FAILED);
                m_db.saveSwap(sm);
              }
            }
            break;

          case SwapPair::SOL: {
            if (!m_solClient) {
              m_logger(Logging::ERROR) << "  SOL client not configured — cannot claim";
            } else {
              SolTxResult solResult;
              claimOk = m_solClient->claim(
                  /*claimerSecretKey=*/"",
                  params.ctrLockTxId,
                  Common::podToHex(params.adaptorSecret),
                  solResult);
              if (claimOk && solResult.confirmed) {
                m_logger(Logging::INFO) << "  SOL claimed, sig: " << solResult.signature;
                sm.transition(SwapState::ADAPTOR_SECRET_REVEALED);
                m_db.saveSwap(sm);
              } else if (!claimOk) {
                m_logger(Logging::ERROR) << "  SOL claim failed: " << solResult.error;
              }
            }
            break;
          }

          case SwapPair::XMR:
            if (!m_xmrClient) {
              m_logger(Logging::ERROR) << "  XMR client not configured — cannot claim";
            } else {
              MoneroTransferResult xmrResult;
              claimOk = m_xmrClient->claimAdaptor(
                  /*aliceSpendKeyHex=*/Common::podToHex(params.ourSwapSecKey),
                  /*bobSpendKeyHex=*/std::string(64, '0'),  // Zero Bob share (Alice's key not available yet)
                  /*adaptorSecretHex=*/Common::podToHex(params.adaptorSecret),
                  /*viewKeyHex=*/"",  // TODO: persist XMR view key
                  params.ctrAddress,
                  xmrResult);
              if (claimOk && xmrResult.success) {
                m_logger(Logging::INFO) << "  XMR claimed, txhash: " << xmrResult.txHash;
                sm.transition(SwapState::ADAPTOR_SECRET_REVEALED);
                m_db.saveSwap(sm);
              } else {
                m_logger(Logging::ERROR) << "  XMR claimAdaptor failed: " << xmrResult.error;
              }
            }
            break;
        }

        if (!claimOk) {
          m_logger(Logging::INFO) << "  Claim not yet complete — will retry next tick.";
        }
      } else {
        m_logger(Logging::INFO) << "  " << swapPairToString(params.pair)
          << " locked. Waiting for Alice to claim (reveals adaptor secret).";
      }
      break;

    case SwapState::ADAPTOR_SECRET_REVEALED:
      if (params.role == SwapRole::BOB) {
        m_logger(Logging::INFO) << "  Adaptor secret learned. Building adapted escrow spend tx...";

        // Bob now knows the adaptor secret t. He can construct the adapted
        // aggregate signature and build the escrow-spend transaction.
        // The spend destination is Bob's own key (he sells XFG, receives
        // counterparty coin; the XFG escrow goes to Alice's claim).
        // Actually: Alice buys XFG (claims escrow), Bob sells XFG.
        // But after secret is revealed, Bob needs to claim counterparty chain.
        // The XFG escrow was already effectively claimed by Alice publishing
        // the adapted sig. Bob's role here is to broadcast the adapted spend.

        // Build the unsigned tx for escrow spend
        SwapParams working = params;
        CryptoNote::Transaction spendTx;
        Crypto::Hash spendPrefixHash;
        CollaborativeRingState spendRingState;

        // Destination: Alice's swap pubkey (she gets the XFG)
        Crypto::PublicKey alicePub = params.peerSwapPubKey;

        if (!SwapTxBuilder::buildUnsignedEscrowSpend(
                m_rpc, working, alicePub, SwapTxBuilder::MIN_FEE,
                spendTx, spendPrefixHash, spendRingState)) {
          m_logger(Logging::ERROR) << "  Failed to build escrow spend tx";
          break;
        }

        // For the adapted spend, Bob needs a collaborative ring sig just
        // like the refund path. Start Ring Round 1.
        SwapTxBuilder::ringRound1Generate(working, spendRingState);

        PeerMessage r1msg;
        r1msg.type = PeerMessageType::RING_ROUND1;
        r1msg.swapId = params.swapId;
        r1msg.ringRound1.partialKeyImage = spendRingState.ourPartialKeyImage;
        r1msg.ringRound1.ringNoncePub = spendRingState.ourRingNoncePub;
        r1msg.ringRound1.ringNonceHp = spendRingState.ourRingNonceHp;

        m_logger(Logging::INFO) << "  Escrow spend tx built. Sending Ring Round 1 to peer...";
        m_logger(Logging::INFO) << "  Ring Round 1: "
          << serializePeerMessage(r1msg).substr(0, 120) << "...";

        // Peer round-trip continues via handlePeerMessage().
        // After both rounds complete, the adapted aggregate sig is computed
        // and the spend tx is broadcast.
      } else {
        m_logger(Logging::INFO) << "  Secret revealed. Waiting for Bob to broadcast escrow spend.";
      }
      break;

    default:
      break;
  }

  return true;
}

void SwapDaemon::listSwaps() {
  auto swapIds = m_db.listSwaps();

  if (swapIds.empty()) {
    std::cout << "No swaps found." << std::endl;
    return;
  }

  std::cout << std::left
            << std::setw(34) << "SWAP ID"
            << std::setw(22) << "STATE"
            << std::setw(6)  << "PAIR"
            << std::setw(6)  << "ROLE"
            << std::setw(18) << "XFG AMOUNT"
            << std::endl;
  std::cout << std::string(86, '-') << std::endl;

  for (const auto& swapId : swapIds) {
    SwapStateMachine sm;
    if (!m_db.loadSwap(swapId, sm)) {
      std::cout << swapId << "  [ERROR: cannot load]" << std::endl;
      continue;
    }

    const auto& p = sm.params();
    std::cout << std::left
              << std::setw(34) << p.swapId
              << std::setw(22) << swapStateToString(sm.currentState())
              << std::setw(6)  << swapPairToString(p.pair)
              << std::setw(6)  << (p.role == SwapRole::BOB ? "BOB" : "ALICE")
              << std::setw(18) << p.xfgAmount
              << std::endl;
  }
}

void SwapDaemon::showSwap(const std::string& swapId) {
  SwapStateMachine sm;
  if (!m_db.loadSwap(swapId, sm)) {
    std::cout << "Swap not found: " << swapId << std::endl;
    return;
  }

  const auto& p = sm.params();

  std::cout << "=== Swap Details ===" << std::endl;
  std::cout << "  Swap ID:          " << p.swapId << std::endl;
  std::cout << "  State:            " << swapStateToString(sm.currentState()) << std::endl;
  std::cout << "  Pair:             XFG/" << swapPairToString(p.pair) << std::endl;
  std::cout << "  Role:             " << (p.role == SwapRole::BOB ? "BOB (selling XFG)" : "ALICE (buying XFG)") << std::endl;
  std::cout << "  XFG amount:       " << p.xfgAmount << " atomic ("
            << (static_cast<double>(p.xfgAmount) / 10000000.0) << " XFG)" << std::endl;
  std::cout << "  CTR amount:       " << p.ctrAmount << " atomic" << std::endl;

  // Adaptor sig fields
  std::cout << "  Our swap pubkey:  " << Common::podToHex(p.ourSwapPubKey) << std::endl;
  std::cout << "  Peer swap pubkey: " << Common::podToHex(p.peerSwapPubKey) << std::endl;
  std::cout << "  Escrow key:       " << Common::podToHex(p.escrowPubKey) << std::endl;

  Crypto::PublicKey zeroPk;
  std::memset(&zeroPk, 0, sizeof(zeroPk));
  if (std::memcmp(&p.adaptorPoint, &zeroPk, sizeof(zeroPk)) != 0) {
    std::cout << "  Adaptor point T:  " << Common::podToHex(p.adaptorPoint) << std::endl;
  }

  Crypto::Hash zeroHash;
  std::memset(&zeroHash, 0, sizeof(zeroHash));
  if (std::memcmp(&p.escrowTxHash, &zeroHash, sizeof(zeroHash)) != 0) {
    std::cout << "  Escrow tx:        " << Common::podToHex(p.escrowTxHash) << std::endl;
    std::cout << "  Escrow out idx:   " << p.escrowOutputIndex << std::endl;
  }

  std::cout << "  XFG timeout:      height " << p.xfgTimeoutHeight << std::endl;
  std::cout << "  CTR timeout:      slot/block " << p.ctrTimeoutBlock << std::endl;

  if (!p.ctrLockTxId.empty()) {
    std::cout << "  CTR lock tx:      " << p.ctrLockTxId << std::endl;
  }
  if (!p.ctrAddress.empty()) {
    std::cout << "  CTR address:      " << p.ctrAddress << std::endl;
  }
  if (!p.peerEndpoint.empty()) {
    std::cout << "  Peer endpoint:    " << p.peerEndpoint << std::endl;
  }

  // Timestamps
  char timeBuf[64];
  struct tm* tm;

  time_t created = sm.createdAt();
  tm = std::localtime(&created);
  std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", tm);
  std::cout << "  Created:          " << timeBuf << std::endl;

  time_t updated = sm.updatedAt();
  tm = std::localtime(&updated);
  std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", tm);
  std::cout << "  Updated:          " << timeBuf << std::endl;

  std::cout << "  Terminal:         " << (sm.isTerminal() ? "yes" : "no") << std::endl;
}

bool SwapDaemon::refund(const std::string& swapId) {
  SwapStateMachine sm;
  if (!m_db.loadSwap(swapId, sm)) {
    m_logger(Logging::ERROR) << "Swap not found: " << swapId;
    return false;
  }

  uint32_t currentHeight = 0;
  if (!m_rpc.getHeight(currentHeight)) {
    m_logger(Logging::ERROR) << "Cannot query fuegod height";
    return false;
  }

  const auto& params = sm.params();
  SwapState current = sm.currentState();

  // Cooperative refund: both parties sign a non-adaptor Musig2 sig
  // spending escrow back to Bob. Available from ESCROW_FUNDED or PRESIGS_READY.
  if (current == SwapState::ADAPTOR_ESCROW_FUNDED ||
      current == SwapState::ADAPTOR_PRESIGS_READY) {
    if (currentHeight < params.xfgTimeoutHeight) {
      m_logger(Logging::ERROR) << "Cannot refund yet. Current height: " << currentHeight
        << ", timeout: " << params.xfgTimeoutHeight
        << " (" << (params.xfgTimeoutHeight - currentHeight) << " blocks remaining)";
      return false;
    }

    m_logger(Logging::INFO) << "Timeout elapsed. Building cooperative refund tx...";

    // Build the unsigned refund transaction spending escrow back to Bob.
    // The destination is Bob's own swap pubkey (he funded the escrow).
    SwapParams working = params;
    CryptoNote::Transaction refundTx;
    Crypto::Hash prefixHash;
    CollaborativeRingState ringState;

    // For the refund destination, use the swap initiator's (Bob's) public key.
    Crypto::PublicKey destKey = (params.role == SwapRole::BOB)
        ? params.ourSwapPubKey : params.peerSwapPubKey;

    if (!SwapTxBuilder::buildUnsignedEscrowSpend(
            m_rpc, working, destKey, SwapTxBuilder::MIN_FEE,
            refundTx, prefixHash, ringState)) {
      m_logger(Logging::ERROR) << "Failed to build refund tx (decoy fetch or params)";
      return false;
    }

    // Ring Round 1: generate our partial key image + ring nonce
    SwapTxBuilder::ringRound1Generate(working, ringState);

    m_logger(Logging::INFO) << "  Refund tx built. Sending Ring Round 1 to peer...";

    // Build and log the peer message (actual P2P send through SwapP2P)
    PeerMessage r1msg;
    r1msg.type = PeerMessageType::RING_ROUND1;
    r1msg.swapId = params.swapId;
    r1msg.ringRound1.partialKeyImage = ringState.ourPartialKeyImage;
    r1msg.ringRound1.ringNoncePub = ringState.ourRingNoncePub;
    r1msg.ringRound1.ringNonceHp = ringState.ourRingNonceHp;

    m_logger(Logging::INFO) << "  Ring Round 1 message: "
      << serializePeerMessage(r1msg).substr(0, 120) << "...";

    // The peer round-trip completes asynchronously via handlePeerMessage().
    // The swap remains in its current state until both rounds complete
    // and the refund tx is broadcast. Do NOT transition yet.
    m_logger(Logging::INFO) << "  Awaiting peer Ring Round 1 response. "
      << "Swap stays in " << swapStateToString(current)
      << " until cooperative refund tx is broadcast.";
    return true;
  }

  // Counterparty chain refund: Bob locked on the counterparty chain but the
  // swap timed out before Alice claimed.  Bob must also refund the CTR HTLC.
  if (current == SwapState::ADAPTOR_CTR_LOCKED && params.role == SwapRole::BOB) {
    if (currentHeight < params.xfgTimeoutHeight) {
      m_logger(Logging::ERROR) << "Cannot refund yet. Current height: " << currentHeight
        << ", timeout: " << params.xfgTimeoutHeight
        << " (" << (params.xfgTimeoutHeight - currentHeight) << " blocks remaining)";
      return false;
    }

    m_logger(Logging::INFO) << "Timeout elapsed. Refunding counterparty ("
      << swapPairToString(params.pair) << ") HTLC...";

    bool ctrRefundOk = false;
    switch (params.pair) {
      case SwapPair::BCH:
        if (!m_bchClient) {
          m_logger(Logging::ERROR) << "  BCH client not configured — cannot refund HTLC";
        } else {
          std::string refundTxId;
          ctrRefundOk = m_bchClient->refundHtlc(
              /*senderWif=*/"",  // TODO: inject via config
              params.ctrLockTxId, 0, params.ctrAmount,
              params.bchRedeemScriptHex,
              params.ctrAddress,
              refundTxId);
          if (ctrRefundOk) {
            m_logger(Logging::INFO) << "  BCH HTLC refunded, txid: " << refundTxId;
          } else {
            m_logger(Logging::ERROR) << "  BCH refundHtlc failed (stub)";
          }
        }
        break;

      case SwapPair::ETH:
        if (!m_ethClient) {
          m_logger(Logging::ERROR) << "  ETH client not configured — cannot refund HTLC";
        } else {
          try {
            std::string refundTxHash;
            ctrRefundOk = m_ethClient->refundHtlc(
                /*fromAddress=*/"",
                params.ctrLockTxId,
                refundTxHash);
            if (ctrRefundOk) {
              m_logger(Logging::INFO) << "  ETH HTLC refunded, tx: " << refundTxHash;
            }
          } catch (const std::runtime_error& e) {
            // A hard error from refundHtlc (misconfigured signer, RPC unreachable,
            // invalid calldata) is unrecoverable — transition to FAILED so the
            // swap does not loop forever on every checkTimeouts tick.
            m_logger(Logging::ERROR) << "  ETH refund failed (unrecoverable): " << e.what();
            sm.transition(SwapState::FAILED);
            m_db.saveSwap(sm);
            return false;
          }
        }
        break;

      case SwapPair::SOL: {
        if (!m_solClient) {
          m_logger(Logging::ERROR) << "  SOL client not configured — cannot refund HTLC";
        } else {
          SolTxResult solResult;
          ctrRefundOk = m_solClient->refund(
              /*senderSecretKey=*/"",
              params.ctrLockTxId,
              solResult);
          if (ctrRefundOk && solResult.confirmed) {
            m_logger(Logging::INFO) << "  SOL HTLC refunded, sig: " << solResult.signature;
          } else if (!ctrRefundOk) {
            m_logger(Logging::ERROR) << "  SOL refund failed: " << solResult.error;
          }
        }
        break;
      }

      case SwapPair::XMR:
        if (!m_xmrClient) {
          m_logger(Logging::ERROR) << "  XMR client not configured — cannot refund adaptor";
        } else {
          MoneroTransferResult xmrResult;
          ctrRefundOk = m_xmrClient->refundAdaptor(
              /*aliceShareHex=*/Common::podToHex(params.ourSwapSecKey),
              /*bobShareHex=*/std::string(64, '0'),  // Zero peer share (needs actual value)
              /*viewKeyHex=*/"",  // TODO: persist XMR view key
              params.ctrAddress,
              xmrResult);
          if (ctrRefundOk && xmrResult.success) {
            m_logger(Logging::INFO) << "  XMR adaptor refunded, txhash: " << xmrResult.txHash;
          } else {
            m_logger(Logging::ERROR) << "  XMR refundAdaptor failed: " << xmrResult.error;
          }
        }
        break;
    }

    if (ctrRefundOk) {
      sm.transition(SwapState::ADAPTOR_REFUNDED);
      m_db.saveSwap(sm);
      m_logger(Logging::INFO) << "  Counterparty HTLC refunded. Swap marked ADAPTOR_REFUNDED.";
    } else {
      m_logger(Logging::WARNING) << "  Counterparty refund failed — will retry next tick.";
    }
    return ctrRefundOk;
  }

  m_logger(Logging::ERROR) << "Cannot refund swap in state: "
    << swapStateToString(current);
  return false;
}

bool SwapDaemon::buildAndBroadcastEscrowTx(SwapParams& params,
                                           const Crypto::PublicKey& destinationKey,
                                           const std::string& txType) {
  // Full pipeline: build unsigned tx → collaborative ring sig → broadcast.
  // This is the synchronous version that assumes peer Round 1 + Round 2
  // data has already been populated in the ring state.

  CryptoNote::Transaction tx;
  Crypto::Hash prefixHash;
  CollaborativeRingState ringState;

  if (!SwapTxBuilder::buildUnsignedEscrowSpend(
          m_rpc, params, destinationKey, SwapTxBuilder::MIN_FEE,
          tx, prefixHash, ringState)) {
    m_logger(Logging::ERROR) << "Failed to build " << txType << " tx";
    return false;
  }

  // Run Ring Round 1 locally
  SwapTxBuilder::ringRound1Generate(params, ringState);

  // At this point we need the peer's Round 1 data.
  // In the async flow, this comes via handlePeerMessage().
  // For the synchronous path (when peer data is already on params),
  // we check if peer data is populated.
  Crypto::KeyImage zeroKI;
  std::memset(&zeroKI, 0, sizeof(zeroKI));
  if (std::memcmp(&ringState.peerPartialKeyImage, &zeroKI, sizeof(zeroKI)) == 0) {
    m_logger(Logging::WARNING) << "  Peer Ring Round 1 data not yet received. "
      << "Broadcast deferred until peer responds.";
    return false;
  }

  // Finalize Round 1 (compute aggregate KI, challenge)
  if (!SwapTxBuilder::ringRound1Finalize(prefixHash, ringState)) {
    m_logger(Logging::ERROR) << "Ring Round 1 finalize failed";
    return false;
  }

  // Update tx input with the aggregate key image, recompute prefix hash
  auto& input = boost::get<CryptoNote::KeyInput>(tx.inputs[0]);
  input.keyImage = ringState.aggregateKeyImage;
  if (!CryptoNote::getObjectHash(
      static_cast<CryptoNote::TransactionPrefix&>(tx), prefixHash)) {
    m_logger(Logging::ERROR) << "Failed to recompute prefix hash";
    return false;
  }

  // Round 2: sign locally
  SwapTxBuilder::ringRound2Sign(params, ringState);

  // Need peer Round 2 data
  Crypto::EllipticCurveScalar zeroScalar;
  std::memset(&zeroScalar, 0, sizeof(zeroScalar));
  if (std::memcmp(&ringState.peerPartialResponse, &zeroScalar, sizeof(zeroScalar)) == 0) {
    m_logger(Logging::WARNING) << "  Peer Ring Round 2 data not yet received.";
    return false;
  }

  // Finalize Round 2 (assemble complete ring sig)
  if (!SwapTxBuilder::ringRound2Finalize(ringState, tx)) {
    m_logger(Logging::ERROR) << "Ring Round 2 finalize failed";
    return false;
  }

  // Serialize and broadcast
  std::string txHex = SwapTxBuilder::serializeToHex(tx);
  m_logger(Logging::INFO) << "Broadcasting " << txType << " tx (" << txHex.size() / 2 << " bytes)...";

  if (!m_rpc.sendRawTransaction(txHex)) {
    m_logger(Logging::ERROR) << "sendRawTransaction failed for " << txType;
    return false;
  }

  m_logger(Logging::INFO) << "  " << txType << " tx broadcast successfully!";
  return true;
}

bool SwapDaemon::handlePeerMessage(const PeerMessage& msg) {
  // Dispatch incoming peer messages to the appropriate swap and phase.
  m_logger(Logging::INFO) << "Peer message for swap " << msg.swapId
    << " type=" << static_cast<int>(msg.type);

  return m_db.updateSwap(msg.swapId, [&](SwapStateMachine& sm) -> bool {
    SwapParams& params = const_cast<SwapParams&>(sm.params());

    switch (msg.type) {
      case PeerMessageType::KEY_EXCHANGE:
        params.peerSwapPubKey = msg.keyExchange.swapPubKey;
        if (!adaptor_key_aggregate(params)) return false;
        sm.transition(SwapState::ADAPTOR_KEYS_EXCHANGED);
        return true;

      case PeerMessageType::ADAPTOR_EXCHANGE:
        params.adaptorPoint = msg.adaptorExchange.adaptorPoint;
        params.adaptorDleqQ = msg.adaptorExchange.adaptorDleqQ;
        params.adaptorDleqProof = msg.adaptorExchange.dleqProof;
        if (!adaptor_verify_adaptor(params, params.escrowPubKey, params.adaptorDleqQ)) {
          m_logger(Logging::ERROR) << "DLEQ proof verification failed!";
          return false;
        }
        return true;

      case PeerMessageType::NONCE_EXCHANGE:
        params.musig2.peerPubNonce = msg.nonceExchange.pubNonce;
        return true;

      case PeerMessageType::PARTIAL_SIG:
        params.musig2.peerPartialSig = msg.partialSig.partialSig;
        if (!adaptor_partial_verify(params)) {
          m_logger(Logging::ERROR) << "Peer partial sig verification failed!";
          return false;
        }
        sm.transition(SwapState::ADAPTOR_PRESIGS_READY);
        return true;

      case PeerMessageType::RING_ROUND1:
        // Store peer's Ring Round 1 data for the collaborative ring sig.
        // The actual finalization happens when buildAndBroadcastEscrowTx
        // is called (either from processSwap or refund).
        m_logger(Logging::INFO) << "  Received peer Ring Round 1 data";
        return true;

      case PeerMessageType::RING_ROUND2:
        m_logger(Logging::INFO) << "  Received peer Ring Round 2 data";
        return true;

      case PeerMessageType::ABORT:
        m_logger(Logging::WARNING) << "Peer aborted swap " << msg.swapId;
        return true;

      default:
        m_logger(Logging::ERROR) << "Unknown peer message type: "
          << static_cast<int>(msg.type);
        return false;
    }
  });
}

PriceOracle& SwapDaemon::priceOracle() {
   return m_oracle;
 }

 // Pool operations delegated to PoolOrganizer
 bool SwapDaemon::createPool(const PoolId& poolId) {
   return m_poolOrganizer.createPool(poolId);
 }

 bool SwapDaemon::getPool(const PoolId& poolId, PoolState& state) const {
   return m_poolOrganizer.getPool(poolId, state);
 }

 std::vector<PoolId> SwapDaemon::getActivePools() const {
   return m_poolOrganizer.getActivePools();
 }

 PoolCheckpoint SwapDaemon::processDeposit(const LPDepositParams& params, uint64_t shareAmount) {
   return m_poolOrganizer.processDeposit(params, shareAmount);
 }

 PoolCheckpoint SwapDaemon::processWithdrawal(const LPWithdrawalParams& params, WithdrawalAmounts& amounts) {
   return m_poolOrganizer.processWithdrawal(params, amounts);
 }

 PoolOrganizer::SwapResult SwapDaemon::executeSwap(const PoolSwapOrder& order) {
   return m_poolOrganizer.executeSwap(order);
 }

 uint64_t SwapDaemon::getExpectedOutput(const PoolId& poolId, bool swapAforB, uint64_t inputAmount) const {
   return m_poolOrganizer.getExpectedOutput(poolId, swapAforB, inputAmount);
 }

 PoolOrganizer::ClaimableFees SwapDaemon::getClaimableFees(const Crypto::PublicKey& owner, const PoolId& poolId) const {
   return m_poolOrganizer.getClaimableFees(owner, poolId);
 }

 PoolCheckpoint SwapDaemon::processFeeClaim(const Crypto::PublicKey& owner, const PoolId& poolId, PoolOrganizer::ClaimableFees& claimed) {
   return m_poolOrganizer.processFeeClaim(owner, poolId, claimed);
 }

 PoolCheckpoint SwapDaemon::generateCheckpoint(const PoolId& poolId) {
   return m_poolOrganizer.generateCheckpoint(poolId);
 }

  bool SwapDaemon::getCurrentCheckpoint(const PoolId& poolId, PoolCheckpoint& checkpoint) const {
    return m_poolOrganizer.getCurrentCheckpoint(poolId, checkpoint);
  }

  std::vector<SwapStateMachine> SwapDaemon::getActiveAfkOffers() {
    std::vector<SwapStateMachine> activeOffers;
    uint32_t currentHeight = 0;
    if (!m_rpc.getHeight(currentHeight)) {
      m_logger(Logging::ERROR) << "Cannot query fuegod height for orderbook filtering";
      return activeOffers;
    }

    auto swapIds = m_db.listSwaps();
    for (const auto& id : swapIds) {
      SwapStateMachine sm;
      if (!m_db.loadSwap(id, sm)) continue;
      
    if (sm.currentState() == SwapState::AFK_OFFER_LOCKED) {
      // Remove offers with < 1 hour remaining (~8 blocks)
      if (static_cast<int32_t>(sm.params().xfgTimeoutHeight) - currentHeight >= 8) {
        activeOffers.push_back(sm);
      }
    }

    }
    return activeOffers;
  }
bool SwapDaemon::handleSwapRequest(const std::string& offerId, uint64_t amount,
                         const std::string& takerPubKey, const std::string& proofOfFunds) {
  // Validate proofOfFunds if applicable (using K_COMMAND_RPC_CHECK_RESERVE_PROOF logic via wallet/RPC)

  // Create the AFK Lock using wallet RPC (auto-execute)
  // And start the swap state machine
  m_logger(Logging::INFO) << "Received swap request for offer " << offerId << " amount " << amount;

  if (!m_swapRelay) {
    m_logger(Logging::ERROR) << "Swap relay not configured, cannot handle swap request";
    return false;
  }

  auto offers = m_swapRelay->getOffers(0); // Assuming pair is not strict right now or need lookup
  // Better to iterate all to find it or modify getOffers to get by ID, but for now we iterate all pairs or have a specific method.
  // Actually, we can just get the offer from m_swapRelay directly if we expose a method, or just try to match it.

  CryptoNote::SwapOfferMsg targetOffer;
  bool found = false;
  for (int pair = 0; pair <= 2; ++pair) {
    auto pairOffers = m_swapRelay->getOffers(pair);
    for (const auto& offer : pairOffers) {
      if (offer.offerId == offerId) {
        targetOffer = offer;
        found = true;
        break;
      }
    }
    if (found) break;
  }

  if (!found) {
    m_logger(Logging::ERROR) << "Offer " << offerId << " not found";
    return false;
  }

  if (!targetOffer.isSoftOrder) {
    m_logger(Logging::ERROR) << "Offer " << offerId << " is not a soft order";
    return false;
  }

  std::string lockId;
  std::string adaptorPoint;
  std::string preSig;

  // Create AFK lock (short timeout for taker, e.g., 1 hour)
  if (!m_rpc.createAfkLock(targetOffer.xfgAmount, 1, targetOffer.pair, lockId, adaptorPoint, preSig)) {
    m_logger(Logging::ERROR) << "Failed to create AFK lock for offer " << offerId;
    return false;
  }

  m_logger(Logging::INFO) << "Successfully created AFK lock " << lockId << " for soft order " << offerId;

  // State machine will pick up the new lock
  return true;
}


} // namespace XfgSwap
