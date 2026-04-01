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
#include "Common/StringTools.h"
#include "crypto/hash.h"
#include "crypto/crypto.h"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <ctime>

namespace XfgSwap {

SwapDaemon::SwapDaemon(const std::string& fuegodHost, uint16_t fuegodPort,
                       const std::string& dataDir, Logging::ILogger& logger)
  : m_rpc(fuegodHost, fuegodPort)
  , m_db(dataDir)
  , m_logger(logger, "SwapDaemon") {
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
  if (rc == RateCheck::NO_DATA) {
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

bool SwapDaemon::accept(const std::string& swapId) {
  SwapStateMachine sm;
  if (!m_db.loadSwap(swapId, sm)) {
    m_logger(Logging::ERROR) << "Swap not found: " << swapId;
    return false;
  }

  if (sm.currentState() != SwapState::INITIATED) {
    m_logger(Logging::ERROR) << "Swap is not in INITIATED state (current: "
                             << swapStateToString(sm.currentState()) << ")";
    return false;
  }

  auto& params = sm.params();

  // ── Adaptor sig step 2: key aggregation ──
  // Peer's pubkey must be set before calling accept
  if (!adaptor_key_aggregate(params)) {
    m_logger(Logging::ERROR) << "Musig2 key aggregation failed";
    return false;
  }

  m_logger(Logging::INFO) << "Musig2 escrow key: "
    << Common::podToHex(params.escrowPubKey);

  // If Bob, generate adaptor point + DLEQ proof
  if (params.role == SwapRole::BOB) {
    // Use escrow pubkey as DLEQ base point
    if (!adaptor_generate_adaptor(params, params.escrowPubKey)) {
      m_logger(Logging::ERROR) << "Adaptor point generation failed";
      return false;
    }
    m_logger(Logging::INFO) << "Adaptor point T: "
      << Common::podToHex(params.adaptorPoint);
  }

  if (!sm.transition(SwapState::ADAPTOR_KEYS_EXCHANGED)) {
    m_logger(Logging::ERROR) << "State transition failed";
    return false;
  }

  if (!m_db.saveSwap(sm)) {
    m_logger(Logging::ERROR) << "Failed to save swap state";
    return false;
  }

  m_logger(Logging::INFO) << "Swap " << swapId << " -> ADAPTOR_KEYS_EXCHANGED";
  m_logger(Logging::INFO) << "  Next: fund XFG escrow to joint key "
    << Common::podToHex(params.escrowPubKey);
  return true;
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

      // Cooperative refund possible from escrow-funded or pre-sigs-ready states
      if ((current == SwapState::ADAPTOR_ESCROW_FUNDED ||
           current == SwapState::ADAPTOR_PRESIGS_READY) &&
          params.role == SwapRole::BOB) {
        m_logger(Logging::WARNING) << "Swap " << swapId
          << " XFG timeout reached at height " << currentHeight;

        if (sm.transition(SwapState::ADAPTOR_REFUNDED)) {
          m_db.saveSwap(sm);
          m_logger(Logging::INFO) << "Swap " << swapId << " -> ADAPTOR_REFUNDED";
          anyExpired = true;
        }
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

  const auto& params = sm.params();
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
      m_logger(Logging::INFO) << "  Next: exchange Musig2 nonces and create adaptor pre-sigs.";
      break;

    case SwapState::ADAPTOR_PRESIGS_READY:
      m_logger(Logging::INFO) << "  Pre-sigs ready. Waiting for counterparty ("
        << swapPairToString(params.pair) << ") lock.";
      break;

    case SwapState::ADAPTOR_CTR_LOCKED:
      if (params.role == SwapRole::ALICE) {
        m_logger(Logging::INFO) << "  " << swapPairToString(params.pair)
          << " locked. Claim it to reveal adaptor secret.";
      } else {
        m_logger(Logging::INFO) << "  " << swapPairToString(params.pair)
          << " locked. Waiting for Alice to claim (reveals adaptor secret).";
      }
      break;

    case SwapState::ADAPTOR_SECRET_REVEALED:
      if (params.role == SwapRole::BOB) {
        m_logger(Logging::INFO) << "  Adaptor secret learned. Adapt pre-sig and broadcast escrow spend.";
      } else {
        m_logger(Logging::INFO) << "  Secret revealed. Waiting for Bob to spend XFG escrow.";
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

    m_logger(Logging::INFO) << "Timeout elapsed. Initiating cooperative refund...";
    m_logger(Logging::INFO) << "  Both parties must sign a refund tx (no adaptor point).";

    if (!sm.transition(SwapState::ADAPTOR_REFUNDED)) {
      m_logger(Logging::ERROR) << "State transition failed";
      return false;
    }

    m_db.saveSwap(sm);
    m_logger(Logging::INFO) << "Swap " << swapId << " -> ADAPTOR_REFUNDED";
    return true;
  }

  m_logger(Logging::ERROR) << "Cannot refund swap in state: "
    << swapStateToString(current);
  return false;
}

PriceOracle& SwapDaemon::priceOracle() {
  return m_oracle;
}

} // namespace XfgSwap
