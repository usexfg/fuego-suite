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

#include <HTTP/httplib.h>
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
#include "BitcoinCash/BchChainClient.h"
#include "Ethereum/EthChainClient.h"
#include "Solana/SolChainClient.h"
#include "Monero/XmrChainClient.h"

#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <cstring>
#include <ctime>
#include <stdexcept>
#include "../Logging/ILogger.h"

namespace XfgSwap {

// ── Base58 encode (Bitcoin alphabet, matches SolRpcClient) ───────────────
namespace {
static const char kBase58Alpha[] =
    "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

static std::string base58Encode(const std::vector<uint8_t>& data) {
  if (data.empty()) return "";
  size_t leadingZeros = 0;
  while (leadingZeros < data.size() && data[leadingZeros] == 0) ++leadingZeros;
  size_t maxChars = data.size() * 138 / 100 + 1;
  std::vector<uint8_t> buf(maxChars, 0);
  for (size_t i = 0; i < data.size(); ++i) {
    int carry = data[i];
    for (int j = static_cast<int>(maxChars) - 1; j >= 0; --j) {
      carry += 256 * buf[static_cast<size_t>(j)];
      buf[static_cast<size_t>(j)] = static_cast<uint8_t>(carry % 58);
      carry /= 58;
    }
  }
  size_t skip = 0;
  while (skip < maxChars && buf[skip] == 0) ++skip;
  std::string result(leadingZeros, '1');
  for (size_t i = skip; i < maxChars; ++i) result += kBase58Alpha[buf[i]];
  return result;
}

// Load Solana keypair JSON file (array of 64 integers [0..255]) → base58.
static std::string loadSolKeypairBase58(const std::string& path) {
  std::ifstream f(path);
  if (!f.is_open()) return "";
  std::string json((std::istreambuf_iterator<char>(f)),
                    std::istreambuf_iterator<char>());
  size_t pos = json.find('[');
  if (pos == std::string::npos) return "";
  ++pos;
  std::vector<uint8_t> bytes;
  while (pos < json.size()) {
    if (json[pos] == ']') break;
    if (json[pos] == ',' || json[pos] == ' ' ||
        json[pos] == '\n' || json[pos] == '\t' || json[pos] == '\r') {
      ++pos; continue;
    }
    int val = 0;
    while (pos < json.size() &&
           std::isdigit(static_cast<unsigned char>(json[pos])))
      val = val * 10 + (json[pos++] - '0');
    bytes.push_back(static_cast<uint8_t>(val));
  }
  if (bytes.size() != 64) return "";
  return base58Encode(bytes);
}
} // anonymous namespace

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
    auto rpc = std::make_unique<BchRpcClient>(
        chainCfg.bchHost, chainCfg.bchPort,
        chainCfg.bchRpcUser, chainCfg.bchRpcPass);
    m_chainRegistry.registerChain(SwapPair::BCH,
        std::make_unique<BchChainClient>(std::move(rpc), chainCfg.bchWif));
    m_logger(Logging::INFO) << "BCH chain client registered: "
      << chainCfg.bchHost << ":" << chainCfg.bchPort;
  }
  if (!chainCfg.ethHost.empty()) {
    std::unique_ptr<EthRpcClient> rpc;
    if (!chainCfg.ethPrivKeyHex.empty() && !chainCfg.ethAddress.empty()) {
      rpc = std::make_unique<EthRpcClient>(
          chainCfg.ethHost, chainCfg.ethPort,
          chainCfg.ethPrivKeyHex, chainCfg.ethAddress, chainCfg.ethChainId);
    } else {
      rpc = std::make_unique<EthRpcClient>(
          chainCfg.ethHost, chainCfg.ethPort);
    }
    m_chainRegistry.registerChain(SwapPair::ETH,
        std::make_unique<EthChainClient>(std::move(rpc), chainCfg.ethAddress));
    m_logger(Logging::INFO) << "ETH chain client registered: "
      << chainCfg.ethHost << ":" << chainCfg.ethPort;
  }
  if (!chainCfg.arbHost.empty()) {
    std::unique_ptr<EthRpcClient> rpc;
    if (!chainCfg.arbPrivKeyHex.empty() && !chainCfg.arbAddress.empty()) {
      rpc = std::make_unique<EthRpcClient>(
          chainCfg.arbHost, chainCfg.arbPort,
          chainCfg.arbPrivKeyHex, chainCfg.arbAddress, chainCfg.arbChainId,
          EthTxType::Eip1559);
    } else {
      rpc = std::make_unique<EthRpcClient>(chainCfg.arbHost, chainCfg.arbPort);
    }
    m_chainRegistry.registerChain(SwapPair::ARB,
        std::make_unique<EthChainClient>(std::move(rpc), chainCfg.arbAddress, "ARB"));
    m_logger(Logging::INFO) << "ARB chain client registered: "
      << chainCfg.arbHost << ":" << chainCfg.arbPort
      << " (chainId=" << chainCfg.arbChainId << ")";
  }
  if (!chainCfg.solHost.empty()) {
    auto rpc = std::make_unique<SolRpcClient>(
        chainCfg.solHost, chainCfg.solPort, chainCfg.solProgramId);
    std::string keypairBase58;
    if (!chainCfg.solKeypairPath.empty()) {
      keypairBase58 = loadSolKeypairBase58(chainCfg.solKeypairPath);
      if (!keypairBase58.empty()) {
        m_logger(Logging::INFO) << "Loaded SOL keypair from " << chainCfg.solKeypairPath;
      } else {
        m_logger(Logging::WARNING) << "Failed to load SOL keypair from " << chainCfg.solKeypairPath;
      }
    }
    m_chainRegistry.registerChain(SwapPair::SOL,
        std::make_unique<SolChainClient>(std::move(rpc), keypairBase58));
    m_logger(Logging::INFO) << "SOL chain client registered: "
      << chainCfg.solHost << ":" << chainCfg.solPort;
  }
  if (!chainCfg.xmrDaemonHost.empty()) {
    auto rpc = std::make_unique<MoneroRpcClient>(
        chainCfg.xmrDaemonHost, chainCfg.xmrDaemonPort,
        chainCfg.xmrWalletHost, chainCfg.xmrWalletPort);
    m_chainRegistry.registerChain(SwapPair::XMR,
        std::make_unique<XmrChainClient>(std::move(rpc),
            chainCfg.xmrSpendKeyHex, chainCfg.xmrViewKeyHex));
    m_logger(Logging::INFO) << "XMR chain client registered: "
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
      processSwap(sm);
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
  return processSwap(sm);
}

bool SwapDaemon::processSwap(SwapStateMachine& sm) {
  const std::string& swapId = sm.params().swapId;

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
      break;

    case SwapState::ADAPTOR_ESCROW_FUNDED:
      handleEscrowFunded(sm, currentHeight);
      break;

    case SwapState::ADAPTOR_PRESIGS_READY:
      handlePreSigsReady(sm);
      break;

    case SwapState::ADAPTOR_CTR_LOCKED:
      handleCtrLocked(sm);
      break;

    case SwapState::ADAPTOR_SECRET_REVEALED:
      handleSecretRevealed(sm);
      break;

    default:
      break;
  }

  // Advance in-progress cooperative ring sig (refund or spend) if peer
  // data has arrived since the last tick.  This handles the async round-
  // trip for swaps that are waiting on peer Ring Round 1 or 2 responses.
  if (!sm.isTerminal() && params.ringOurRound1Sent &&
      params.ringPeerRound1Received && params.ringPeerRound2Received &&
      !params.ringTxBroadcast) {
    // Both peer rounds received — finalize and broadcast.
    Crypto::PublicKey destKey =
        (current == SwapState::ADAPTOR_SECRET_REVEALED && params.role == SwapRole::BOB)
            ? params.peerSwapPubKey   // spend: Alice gets XFG
            : params.ourSwapPubKey;    // refund: Bob gets XFG back
    std::string txType =
        (current == SwapState::ADAPTOR_SECRET_REVEALED) ? "spend" : "refund";

    if (buildAndBroadcastEscrowTx(params, destKey, txType)) {
      params.ringTxBroadcast = true;
      if (current == SwapState::ADAPTOR_SECRET_REVEALED) {
        sm.transition(SwapState::ADAPTOR_XFG_SPENT);
      } else {
        sm.transition(SwapState::ADAPTOR_REFUNDED, currentHeight);
      }
      m_db.saveSwap(sm);
      m_logger(Logging::INFO) << "  Escrow " << txType << " tx broadcast. Swap " << swapId << " completed.";
    }
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

  auto& params = sm.params();
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

    // If both peer rounds received, finalize and broadcast the refund tx.
    if (params.ringPeerRound1Received && params.ringPeerRound2Received) {
      Crypto::PublicKey destKey = (params.role == SwapRole::BOB)
          ? params.ourSwapPubKey : params.peerSwapPubKey;
      if (buildAndBroadcastEscrowTx(params, destKey, "refund")) {
        params.ringTxBroadcast = true;
        sm.transition(SwapState::ADAPTOR_REFUNDED, currentHeight);
        m_db.saveSwap(sm);
        m_logger(Logging::INFO) << "  Cooperative refund tx broadcast. Swap marked ADAPTOR_REFUNDED.";
        return true;
      }
      m_logger(Logging::ERROR) << "  Failed to broadcast cooperative refund tx";
      return false;
    }

    // If peer sent Round 1 but not Round 2, send our Round 2.
    if (params.ringPeerRound1Received && !params.ringOurRound2Sent) {
      SwapParams working = params;
      CryptoNote::Transaction refundTx;
      Crypto::Hash prefixHash;
      CollaborativeRingState ringState;
      Crypto::PublicKey destKey = (params.role == SwapRole::BOB)
          ? params.ourSwapPubKey : params.peerSwapPubKey;

      if (!SwapTxBuilder::buildUnsignedEscrowSpend(
              m_rpc, working, destKey, SwapTxBuilder::MIN_FEE,
              refundTx, prefixHash, ringState)) {
        m_logger(Logging::ERROR) << "Failed to build refund tx for Round 2";
        return false;
      }
      ringState.peerPartialKeyImage = params.ringPeerPartialKeyImage;
      ringState.peerRingNoncePub    = params.ringPeerRingNoncePub;
      ringState.peerRingNonceHp     = params.ringPeerRingNonceHp;
      SwapTxBuilder::ringRound1Generate(working, ringState);
      if (!SwapTxBuilder::ringRound1Finalize(prefixHash, ringState)) {
        m_logger(Logging::ERROR) << "Ring Round 1 finalize failed";
        return false;
      }
      auto& input = boost::get<CryptoNote::KeyInput>(refundTx.inputs[0]);
      input.keyImage = ringState.aggregateKeyImage;
      if (!CryptoNote::getObjectHash(
          static_cast<CryptoNote::TransactionPrefix&>(refundTx), prefixHash)) {
        m_logger(Logging::ERROR) << "Failed to recompute prefix hash";
        return false;
      }
      SwapTxBuilder::ringRound2Sign(working, ringState);
      params.ringOurRound2Sent = true;
      m_db.updateSwap(swapId, [&](SwapStateMachine& s) {
        s.params().ringOurRound2Sent = true;
        return true;
      });

      PeerMessage r2msg;
      r2msg.type = PeerMessageType::RING_ROUND2;
      r2msg.swapId = params.swapId;
      r2msg.ringRound2.partialResponse = ringState.ourPartialResponse;
      m_logger(Logging::INFO) << "  Sending Ring Round 2 to peer...";
      m_logger(Logging::INFO) << "  Ring Round 2: "
        << serializePeerMessage(r2msg).substr(0, 120) << "...";
      m_logger(Logging::INFO) << "  Awaiting peer Ring Round 2 response.";
      return true;
    }

    // If we haven't sent Round 1 yet, build tx and send Round 1.
    if (!params.ringOurRound1Sent) {
      SwapParams working = params;
      CryptoNote::Transaction refundTx;
      Crypto::Hash prefixHash;
      CollaborativeRingState ringState;
      Crypto::PublicKey destKey = (params.role == SwapRole::BOB)
          ? params.ourSwapPubKey : params.peerSwapPubKey;

      if (!SwapTxBuilder::buildUnsignedEscrowSpend(
              m_rpc, working, destKey, SwapTxBuilder::MIN_FEE,
              refundTx, prefixHash, ringState)) {
        m_logger(Logging::ERROR) << "Failed to build refund tx (decoy fetch or params)";
        return false;
      }
      SwapTxBuilder::ringRound1Generate(working, ringState);
      params.ringOurRound1Sent = true;
      m_db.updateSwap(swapId, [&](SwapStateMachine& s) {
        s.params().ringOurRound1Sent = true;
        return true;
      });

      PeerMessage r1msg;
      r1msg.type = PeerMessageType::RING_ROUND1;
      r1msg.swapId = params.swapId;
      r1msg.ringRound1.partialKeyImage = ringState.ourPartialKeyImage;
      r1msg.ringRound1.ringNoncePub = ringState.ourRingNoncePub;
      r1msg.ringRound1.ringNonceHp = ringState.ourRingNonceHp;

      m_logger(Logging::INFO) << "  Refund tx built. Sending Ring Round 1 to peer...";
      m_logger(Logging::INFO) << "  Ring Round 1 message: "
        << serializePeerMessage(r1msg).substr(0, 120) << "...";
      m_logger(Logging::INFO) << "  Awaiting peer Ring Round 1 response. "
        << "Call 'refund " << swapId << "' again when peer has responded.";
      return true;
    }

    m_logger(Logging::INFO) << "  Awaiting peer ring data for cooperative refund.";
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
    auto* client = m_chainRegistry.getClient(params.pair);
    if (!client) {
      m_logger(Logging::ERROR) << "  " << swapPairToString(params.pair)
        << " client not configured — cannot refund";
    } else {
      auto result = client->refund(params);
      if (result.success) {
        m_logger(Logging::INFO) << "  " << client->chainName()
          << " refunded, txid: " << result.txId;
        ctrRefundOk = true;
      } else {
        m_logger(Logging::ERROR) << "  " << client->chainName()
          << " refund failed: " << result.error;
        if (result.fatal) {
          sm.transition(SwapState::FAILED);
          m_db.saveSwap(sm);
          return false;
        }
      }
    }

    if (ctrRefundOk) {
      sm.transition(SwapState::ADAPTOR_REFUNDED, currentHeight);
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

  // Run Ring Round 1 locally (only if our data isn't already in params).
  // On recovery after restart, our Round 1 data was already sent and we
  // just need the peer's response.
  if (!params.ringOurRound1Sent) {
    SwapTxBuilder::ringRound1Generate(params, ringState);
    params.ringOurRound1Sent = true;
  }

  // Populate peer Round 1 data from persisted params (may arrive async via
  // handlePeerMessage, or may already be present on recovery after restart).
  if (params.ringPeerRound1Received) {
    ringState.peerPartialKeyImage = params.ringPeerPartialKeyImage;
    ringState.peerRingNoncePub    = params.ringPeerRingNoncePub;
    ringState.peerRingNonceHp     = params.ringPeerRingNonceHp;
  }

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

  // Round 2: sign locally (only if not already done)
  if (!params.ringOurRound2Sent) {
    SwapTxBuilder::ringRound2Sign(params, ringState);
    params.ringOurRound2Sent = true;
  }

  // Populate peer Round 2 data from persisted params.
  if (params.ringPeerRound2Received) {
    ringState.peerPartialResponse = params.ringPeerPartialResponse;
  }

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

  // Phase 2: 1% claim fee reported to daemon (total swap fee = 2%: 1% init + 1% claim)
  if (params.xfgAmount > 0) {
    uint64_t claimFee = (params.xfgAmount * CryptoNote::parameters::SWAP_FEE_RATE_BPS)
                      / CryptoNote::parameters::SWAP_FEE_RATE_DIVISOR;
    if (claimFee > 0) {
      m_rpc.addSwapFee(claimFee);
      m_logger(Logging::INFO) << "  Swap claim fee (1%): " << claimFee
                               << " XFG reported to daemon fee pool";
    }
  }

  return true;
}

bool SwapDaemon::handlePeerMessage(const PeerMessage& msg) {
  // Dispatch incoming peer messages to the appropriate swap and phase.
  m_logger(Logging::INFO) << "Peer message for swap " << msg.swapId
    << " type=" << static_cast<int>(msg.type);

  return m_db.updateSwap(msg.swapId, [&](SwapStateMachine& sm) -> bool {
    SwapParams& params = const_cast<SwapParams&>(sm.params());

    // Peer authentication gate:
    // KEY_EXCHANGE must only be accepted once (no key-swapping attacks).
    // All other messages require the peer key to have been set.
    static const Crypto::PublicKey ZERO_KEY{};
    bool keyExchanged = (std::memcmp(&params.peerSwapPubKey, &ZERO_KEY, sizeof(ZERO_KEY)) != 0);

    switch (msg.type) {
      case PeerMessageType::KEY_EXCHANGE:
        if (keyExchanged) {
          m_logger(Logging::WARNING) << "Duplicate KEY_EXCHANGE rejected for swap " << msg.swapId;
          return false;
        }
        if (std::memcmp(&msg.keyExchange.swapPubKey, &ZERO_KEY, sizeof(ZERO_KEY)) == 0) {
          m_logger(Logging::WARNING) << "KEY_EXCHANGE with zero pubkey rejected";
          return false;
        }
        params.peerSwapPubKey = msg.keyExchange.swapPubKey;
        if (!adaptor_key_aggregate(params)) return false;
        sm.transition(SwapState::ADAPTOR_KEYS_EXCHANGED);
        return true;

      case PeerMessageType::ADAPTOR_EXCHANGE:
        if (!keyExchanged) return false;
        params.adaptorPoint = msg.adaptorExchange.adaptorPoint;

      // ... rest of the cases need keyExchanged check
        params.adaptorDleqQ = msg.adaptorExchange.adaptorDleqQ;
        params.adaptorDleqProof = msg.adaptorExchange.dleqProof;
        if (!adaptor_verify_adaptor(params, params.escrowPubKey, params.adaptorDleqQ)) {
          m_logger(Logging::ERROR) << "DLEQ proof verification failed!";
          return false;
        }
        return true;

      case PeerMessageType::NONCE_EXCHANGE:
        if (!keyExchanged) return false;
        params.musig2.peerPubNonce = msg.nonceExchange.pubNonce;
        return true;

      case PeerMessageType::PARTIAL_SIG:
        if (!keyExchanged) return false;
        params.musig2.peerPartialSig = msg.partialSig.partialSig;
        if (!adaptor_partial_verify(params)) {
          m_logger(Logging::ERROR) << "Peer partial sig verification failed!";
          return false;
        }
        sm.transition(SwapState::ADAPTOR_PRESIGS_READY);
        return true;

      case PeerMessageType::RING_ROUND1:
        if (!keyExchanged) return false;
        params.ringPeerPartialKeyImage = msg.ringRound1.partialKeyImage;
        params.ringPeerRingNoncePub    = msg.ringRound1.ringNoncePub;
        params.ringPeerRingNonceHp     = msg.ringRound1.ringNonceHp;
        params.ringPeerRound1Received  = true;
        m_logger(Logging::INFO) << "  Received peer Ring Round 1 data (persisted)";
        return true;

      case PeerMessageType::RING_ROUND2:
        if (!keyExchanged) return false;
        params.ringPeerPartialResponse = msg.ringRound2.partialResponse;
        params.ringPeerRound2Received  = true;
        m_logger(Logging::INFO) << "  Received peer Ring Round 2 data (persisted)";
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

  // Search all pairs to find the target offer by ID

  CryptoNote::SwapOfferMsg targetOffer;
  bool found = false;
  for (int pair = 0; pair <= 4; ++pair) {
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
