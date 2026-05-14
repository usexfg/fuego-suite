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

#include "PoolOrganizer.h"
#include "Logging/LoggerRef.h"
#include <algorithm>
#include <sstream>
#include <ctime>

namespace XfgSwap {

static std::string poolKey(const PoolId& id) {
  return poolIdToHex(id);
}

static std::string lpKey(const PoolId& poolId, const Crypto::PublicKey& owner) {
  static const char hex[] = "0123456789abcdef";
  std::string key = poolIdToHex(poolId);
  key += ':';
  const uint8_t* pub = reinterpret_cast<const uint8_t*>(&owner);
  for (int i = 0; i < 32; ++i) {
    key += hex[(pub[i] >> 4) & 0xf];
    key += hex[pub[i] & 0xf];
  }
  return key;
}

PoolOrganizer::PoolOrganizer(Logging::ILogger& logger)
    : m_logger(logger, "PoolOrg") {}

PoolOrganizer::~PoolOrganizer() {}

bool PoolOrganizer::createPool(const PoolId& poolId) {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::string key = poolKey(poolId);
  if (m_pools.count(key)) {
    return false;
  }

  PoolState state = {};
  state.id = poolId;
  state.reserveA = 0;
  state.reserveB = 0;
  state.totalLPShares = 0;
  state.feeAccumulatorA = 0;
  state.feeAccumulatorB = 0;
  state.checkpointHash = Crypto::Hash{};
  state.blockHeight = 0;
  state.timestamp = 0;
  state.totalVolumeA = 0;
  state.totalVolumeB = 0;
  state.totalFeesA = 0;
  state.totalFeesB = 0;

  m_pools[key] = state;
  m_lpTrees[key] = PoolMerkleTree();
  m_feeTrees[key] = PoolMerkleTree();
  m_prevCheckpoints[key] = Crypto::Hash{};

  return true;
}

bool PoolOrganizer::getPool(const PoolId& poolId, PoolState& state) const {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::string key = poolKey(poolId);
  auto it = m_pools.find(key);
  if (it == m_pools.end()) {
    return false;
  }

  state = it->second;
  return true;
}

std::vector<PoolId> PoolOrganizer::getActivePools() const {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::vector<PoolId> pools;
  pools.reserve(m_pools.size());

  for (const auto& pair : m_pools) {
    pools.push_back(pair.second.id);
  }

  return pools;
}

PoolCheckpoint PoolOrganizer::processDeposit(const LPDepositParams& params,
                                              uint64_t shareAmount) {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::string key = poolKey(params.poolId);
  PoolState& state = m_pools.at(key);

  uint64_t newShares = poolMintLPShares(
      params.amountA, params.amountB,
      state.totalLPShares, state.reserveA, state.reserveB);

  state.reserveA += params.amountA;
  state.reserveB += params.amountB;
  state.totalLPShares += newShares;

  // Create or update LP share entry
  std::string lkey = lpKey(params.poolId, params.lpPubKey);
  auto it = m_lpRegistry.find(lkey);
  if (it == m_lpRegistry.end()) {
    LPShare share = {};
    share.owner = params.lpPubKey;
    share.poolId = params.poolId;
    share.shareAmount = newShares;
    share.feeClaimedA = 0;
    share.feeClaimedB = 0;
    share.depositHeight = state.blockHeight;
    share.depositTimestamp = state.timestamp;
    share.nullifier = Crypto::Hash{};
    m_lpRegistry[lkey] = share;
  } else {
    it->second.shareAmount += newShares;
  }

  // Add to merkle tree
  addLPShareToTree(params.poolId, m_lpRegistry.at(lkey));

  // Record event
  PoolEvent event = {};
  event.type = PoolEventType::DEPOSIT;
  event.poolId = params.poolId;
  event.amountA = params.amountA;
  event.amountB = params.amountB;
  event.actor = params.lpPubKey;
  event.blockHeight = state.blockHeight;
  event.timestamp = state.timestamp;
  recordEvent(event);

  // Generate checkpoint
  PoolCheckpoint checkpoint = buildCheckpoint(
      state, m_lpTrees.at(key), m_feeTrees.at(key), m_prevCheckpoints.at(key));
  m_prevCheckpoints[key] = checkpoint.newCheckpoint;
  state.checkpointHash = checkpoint.newCheckpoint;

  return checkpoint;
}

PoolCheckpoint PoolOrganizer::processWithdrawal(const LPWithdrawalParams& params,
                                                  WithdrawalAmounts& amounts) {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::string key = poolKey(params.poolId);
  PoolState& state = m_pools.at(key);

  std::string lkey = lpKey(params.poolId, params.lpPubKey);
  auto it = m_lpRegistry.find(lkey);
  if (it == m_lpRegistry.end() || it->second.shareAmount < params.burnAmount) {
    // Return empty checkpoint on error
    return PoolCheckpoint{};
  }

  amounts = poolGetWithdrawalAmounts(
      params.burnAmount, state.totalLPShares,
      state.reserveA, state.reserveB,
      state.feeAccumulatorA, state.feeAccumulatorB);

  // Burn shares
  it->second.shareAmount -= params.burnAmount;
  state.totalLPShares -= params.burnAmount;

  // Deduct reserves and fees
  state.reserveA -= amounts.amountA;
  state.reserveB -= amounts.amountB;
  state.feeAccumulatorA -= amounts.feeA;
  state.feeAccumulatorB -= amounts.feeB;

  // Set nullifier
  it->second.nullifier = params.lpShareProof;
  it->second.feeClaimedA += amounts.feeA;
  it->second.feeClaimedB += amounts.feeB;

  // Update merkle tree (rebuild with updated share)
  if (it->second.shareAmount > 0) {
    addLPShareToTree(params.poolId, it->second);
  }

  // Record event
  PoolEvent event = {};
  event.type = PoolEventType::WITHDRAWAL;
  event.poolId = params.poolId;
  event.amountA = amounts.amountA;
  event.amountB = amounts.amountB;
  event.feeA = amounts.feeA;
  event.feeB = amounts.feeB;
  event.actor = params.lpPubKey;
  event.blockHeight = state.blockHeight;
  event.timestamp = state.timestamp;
  recordEvent(event);

  // Generate checkpoint
  PoolCheckpoint checkpoint = buildCheckpoint(
      state, m_lpTrees.at(key), m_feeTrees.at(key), m_prevCheckpoints.at(key));
  m_prevCheckpoints[key] = checkpoint.newCheckpoint;
  state.checkpointHash = checkpoint.newCheckpoint;

  return checkpoint;
}

PoolOrganizer::SwapResult PoolOrganizer::executeSwap(const PoolSwapOrder& order) {
  SwapResult result = {};
  result.success = false;

  std::lock_guard<std::mutex> lock(m_mutex);

  std::string key = poolKey(order.poolId);
  auto poolIt = m_pools.find(key);
  if (poolIt == m_pools.end()) {
    return result;
  }

  PoolState& state = poolIt->second;

  uint64_t reserveIn = order.swapAforB ? state.reserveA : state.reserveB;
  uint64_t reserveOut = order.swapAforB ? state.reserveB : state.reserveA;

  // Validate
  if (!poolHasSufficientLiquidity(order.inputAmount, reserveIn)) {
    return result;
  }

  uint64_t outputAmount = poolGetOutputAmount(order.inputAmount, reserveIn, reserveOut, state.id.feeBps);
  if (outputAmount == 0 || outputAmount < order.minOutput) {
    return result;
  }

  uint64_t feeAmount = poolGetFeeAmount(order.inputAmount, state.id.feeBps);

  // Update reserves
  if (order.swapAforB) {
    state.reserveA += order.inputAmount;
    state.reserveB -= outputAmount;
    state.totalVolumeA += order.inputAmount;
    state.totalVolumeB += outputAmount;
    state.feeAccumulatorA += feeAmount;
    state.totalFeesA += feeAmount;
  } else {
    state.reserveB += order.inputAmount;
    state.reserveA -= outputAmount;
    state.totalVolumeB += order.inputAmount;
    state.totalVolumeA += outputAmount;
    state.feeAccumulatorB += feeAmount;
    state.totalFeesB += feeAmount;
  }

  // Record fee
  PoolFeeRecord feeRecord = {};
  feeRecord.poolId = order.poolId;
  feeRecord.feeAmount = feeAmount;
  feeRecord.totalShares = state.totalLPShares;
  feeRecord.blockHeight = state.blockHeight;
  feeRecord.timestamp = state.timestamp;
  addFeeRecordToTree(order.poolId, feeRecord);

  // Record event
  PoolEvent event = {};
  event.type = PoolEventType::SWAP;
  event.poolId = order.poolId;
  event.amountA = order.swapAforB ? order.inputAmount : outputAmount;
  event.amountB = order.swapAforB ? outputAmount : order.inputAmount;
  event.feeA = order.swapAforB ? feeAmount : 0;
  event.feeB = order.swapAforB ? 0 : feeAmount;
  event.actor = order.traderPubKey;
  event.blockHeight = state.blockHeight;
  event.timestamp = state.timestamp;
  recordEvent(event);

  // Generate checkpoint
  PoolCheckpoint checkpoint = buildCheckpoint(
      state, m_lpTrees.at(key), m_feeTrees.at(key), m_prevCheckpoints.at(key));
  m_prevCheckpoints[key] = checkpoint.newCheckpoint;
  state.checkpointHash = checkpoint.newCheckpoint;

  result.outputAmount = outputAmount;
  result.feeAmount = feeAmount;
  result.checkpoint = checkpoint;
  result.success = true;

  return result;
}

uint64_t PoolOrganizer::getExpectedOutput(const PoolId& poolId,
                                           bool swapAforB,
                                           uint64_t inputAmount) const {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::string key = poolKey(poolId);
  auto it = m_pools.find(key);
  if (it == m_pools.end()) {
    return 0;
  }

  const PoolState& state = it->second;
  uint64_t reserveIn = swapAforB ? state.reserveA : state.reserveB;
  uint64_t reserveOut = swapAforB ? state.reserveB : state.reserveA;

  return poolGetOutputAmount(inputAmount, reserveIn, reserveOut, state.id.feeBps);
}

PoolOrganizer::ClaimableFees PoolOrganizer::getClaimableFees(const Crypto::PublicKey& owner,
                                                               const PoolId& poolId) const {
  ClaimableFees result = {};

  std::lock_guard<std::mutex> lock(m_mutex);

  std::string key = poolKey(poolId);
  auto poolIt = m_pools.find(key);
  if (poolIt == m_pools.end()) {
    return result;
  }

  std::string lkey = lpKey(poolId, owner);
  auto it = m_lpRegistry.find(lkey);
  if (it == m_lpRegistry.end() || it->second.shareAmount == 0) {
    return result;
  }

  const PoolState& state = poolIt->second;
  const LPShare& share = it->second;

  if (state.totalLPShares > 0) {
    result.feeA = (share.shareAmount * state.feeAccumulatorA) / state.totalLPShares - share.feeClaimedA;
    result.feeB = (share.shareAmount * state.feeAccumulatorB) / state.totalLPShares - share.feeClaimedB;
  }

  return result;
}

PoolCheckpoint PoolOrganizer::processFeeClaim(const Crypto::PublicKey& owner,
                                                const PoolId& poolId,
                                                ClaimableFees& claimed) {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::string key = poolKey(poolId);
  PoolState& state = m_pools.at(key);

  std::string lkey = lpKey(poolId, owner);
  auto it = m_lpRegistry.find(lkey);
  if (it == m_lpRegistry.end()) {
    return PoolCheckpoint{};
  }

  claimed = getClaimableFees(owner, poolId);

  it->second.feeClaimedA += claimed.feeA;
  it->second.feeClaimedB += claimed.feeB;
  state.feeAccumulatorA -= claimed.feeA;
  state.feeAccumulatorB -= claimed.feeB;

  // Record event
  PoolEvent event = {};
  event.type = PoolEventType::FEE_CLAIM;
  event.poolId = poolId;
  event.feeA = claimed.feeA;
  event.feeB = claimed.feeB;
  event.actor = owner;
  event.blockHeight = state.blockHeight;
  event.timestamp = state.timestamp;
  recordEvent(event);

  // Generate checkpoint
  PoolCheckpoint checkpoint = buildCheckpoint(
      state, m_lpTrees.at(key), m_feeTrees.at(key), m_prevCheckpoints.at(key));
  m_prevCheckpoints[key] = checkpoint.newCheckpoint;
  state.checkpointHash = checkpoint.newCheckpoint;

  return checkpoint;
}

PoolCheckpoint PoolOrganizer::generateCheckpoint(const PoolId& poolId) {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::string key = poolKey(poolId);
  PoolState& state = m_pools.at(key);

  PoolCheckpoint checkpoint = buildCheckpoint(
      state, m_lpTrees.at(key), m_feeTrees.at(key), m_prevCheckpoints.at(key));
  m_prevCheckpoints[key] = checkpoint.newCheckpoint;
  state.checkpointHash = checkpoint.newCheckpoint;

  return checkpoint;
}

bool PoolOrganizer::getCurrentCheckpoint(const PoolId& poolId, PoolCheckpoint& checkpoint) const {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::string key = poolKey(poolId);
  auto it = m_pools.find(key);
  if (it == m_pools.end()) {
    return false;
  }

  const PoolState& state = it->second;
  auto lpIt = m_lpTrees.find(key);
  auto feeIt = m_feeTrees.find(key);
  auto prevIt = m_prevCheckpoints.find(key);

  if (lpIt == m_lpTrees.end() || feeIt == m_feeTrees.end() || prevIt == m_prevCheckpoints.end()) {
    return false;
  }

  checkpoint = buildCheckpoint(state, lpIt->second, feeIt->second, prevIt->second);
  return true;
}

bool PoolOrganizer::verifyCheckpoint(const PoolId& poolId,
                                       const PoolCheckpoint& checkpoint) const {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::string key = poolKey(poolId);
  auto it = m_pools.find(key);
  if (it == m_pools.end()) {
    return false;
  }

  auto lpIt = m_lpTrees.find(key);
  auto feeIt = m_feeTrees.find(key);
  auto prevIt = m_prevCheckpoints.find(key);

  if (lpIt == m_lpTrees.end() || feeIt == m_feeTrees.end() || prevIt == m_prevCheckpoints.end()) {
    return false;
  }

  return XfgSwap::verifyCheckpoint(checkpoint, it->second, lpIt->second, feeIt->second, prevIt->second);
}

bool PoolOrganizer::getLPShares(const Crypto::PublicKey& owner,
                                  const PoolId& poolId,
                                  LPShare& shares) const {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::string lkey = lpKey(poolId, owner);
  auto it = m_lpRegistry.find(lkey);
  if (it == m_lpRegistry.end()) {
    return false;
  }

  shares = it->second;
  return true;
}

std::vector<Crypto::Hash> PoolOrganizer::getLPShareProof(const Crypto::PublicKey& owner,
                                                           const PoolId& poolId,
                                                           size_t& leafIndex) const {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::string key = poolKey(poolId);
  auto it = m_lpTrees.find(key);
  if (it == m_lpTrees.end()) {
    return {};
  }

  // Find leaf index for this owner
  const auto& leaves = it->second.leaves();
  Crypto::Hash ownerLeaf = {};

  std::string lkey = lpKey(poolId, owner);
  auto regIt = m_lpRegistry.find(lkey);
  if (regIt == m_lpRegistry.end()) {
    return {};
  }

  ownerLeaf = computeLPShareLeaf(regIt->second);

  for (size_t i = 0; i < leaves.size(); ++i) {
    if (leaves[i] == ownerLeaf) {
      leafIndex = i;
      return it->second.getProof(i);
    }
  }

  return {};
}

uint64_t PoolOrganizer::getSpotPrice(const PoolId& poolId) const {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::string key = poolKey(poolId);
  auto it = m_pools.find(key);
  if (it == m_pools.end()) {
    return 0;
  }

  return poolGetSpotPrice(it->second.reserveA, it->second.reserveB);
}

PoolOrganizer::PoolStats PoolOrganizer::getPoolStats(const PoolId& poolId) const {
  PoolStats stats = {};

  std::lock_guard<std::mutex> lock(m_mutex);

  std::string key = poolKey(poolId);
  auto it = m_pools.find(key);
  if (it == m_pools.end()) {
    return stats;
  }

  const PoolState& state = it->second;
  stats.reserveA = state.reserveA;
  stats.reserveB = state.reserveB;
  stats.totalLPShares = state.totalLPShares;
  stats.totalVolumeA = state.totalVolumeA;
  stats.totalVolumeB = state.totalVolumeB;
  stats.totalFeesA = state.totalFeesA;
  stats.totalFeesB = state.totalFeesB;

  // Count active LP providers
  for (const auto& pair : m_lpRegistry) {
    if (pair.second.poolId == poolId && pair.second.shareAmount > 0) {
      stats.activeLPProviders++;
    }
  }

  return stats;
}

// ─── Private helpers ───────────────────────────────────────────────────

PoolState& PoolOrganizer::getPoolMutable(const PoolId& poolId) {
  std::string key = poolKey(poolId);
  return m_pools.at(key);
}

void PoolOrganizer::updatePoolState(PoolState& state, const PoolCheckpoint& checkpoint) {
  state.checkpointHash = checkpoint.newCheckpoint;
  state.blockHeight = checkpoint.blockHeight;
  state.timestamp = checkpoint.timestamp;
}

void PoolOrganizer::recordEvent(const PoolEvent& event) {
  m_events.push_back(event);
}

void PoolOrganizer::addLPShareToTree(const PoolId& poolId, const LPShare& share) {
  std::string key = poolKey(poolId);
  Crypto::Hash leaf = computeLPShareLeaf(share);
  m_lpTrees[key].addLeaf(leaf);
}

void PoolOrganizer::addFeeRecordToTree(const PoolId& poolId, const PoolFeeRecord& record) {
  std::string key = poolKey(poolId);
  Crypto::Hash leaf = computeFeeRecordLeaf(record);
  m_feeTrees[key].addLeaf(leaf);
}

// ─── ZK Proof Epoch Management ─────────────────────────────────────────

void PoolOrganizer::startEpoch(const PoolId& poolId, uint32_t blockHeight) {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::string key = poolKey(poolId);
  uint32_t epochNum = 0;

  auto it = m_epochStates.find(key);
  if (it != m_epochStates.end()) {
    epochNum = it->second.epochNumber + 1;
  }

  EpochState es;
  es.epochNumber = epochNum;
  es.epochStart = blockHeight;
  es.epochEnd = blockHeight + LP_EPOCH_BLOCKS;

  auto poolIt = m_pools.find(key);
  if (poolIt != m_pools.end()) {
    es.prevStateCommitment = poolIt->second.checkpointHash;
  } else {
    es.prevStateCommitment = Crypto::Hash{};
  }

  m_epochStates[key] = es;
  m_eventBuffers[key].clear();

  m_logger(Logging::INFO) << "LP pool epoch started: pool=" << key << " epoch=" << epochNum <<
                " range=[" << es.epochStart << "," << es.epochEnd << "]";
}

bool PoolOrganizer::isEpochBoundary(const PoolId& poolId, uint32_t blockHeight) const {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::string key = poolKey(poolId);
  auto it = m_epochStates.find(key);
  if (it == m_epochStates.end()) {
    return false;
  }

  return blockHeight >= it->second.epochEnd;
}

uint32_t PoolOrganizer::getEpochNumber(const PoolId& poolId) const {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::string key = poolKey(poolId);
  auto it = m_epochStates.find(key);
  if (it == m_epochStates.end()) {
    return 0;
  }

  return it->second.epochNumber;
}

bool PoolOrganizer::getEpochRange(const PoolId& poolId, uint32_t& start, uint32_t& end) const {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::string key = poolKey(poolId);
  auto it = m_epochStates.find(key);
  if (it == m_epochStates.end()) {
    return false;
  }

  start = it->second.epochStart;
  end = it->second.epochEnd;
  return true;
}

std::vector<uint8_t> PoolOrganizer::deriveEpochKey(const PoolId& poolId, uint32_t epoch) const {
  std::string key = poolKey(poolId);
  const uint8_t* poolBytes = reinterpret_cast<const uint8_t*>(key.data());

  std::vector<uint8_t> data;
  data.reserve(key.size() + 4);
  data.insert(data.end(), poolBytes, poolBytes + key.size());
  uint32_t epochLE = epoch;
  const uint8_t* epochBytes = reinterpret_cast<const uint8_t*>(&epochLE);
  data.insert(data.end(), epochBytes, epochBytes + 4);

  std::vector<uint8_t> keyOut(32, 0);
  for (size_t i = 0; i < 32 && i < data.size(); ++i) {
    keyOut[i] = data[i];
  }

  for (size_t i = 0; i < 32; ++i) {
    keyOut[i] ^= static_cast<uint8_t>((epoch * 31 + i) & 0xFF);
  }

  return keyOut;
}

void PoolOrganizer::bufferEncryptedEvent(const PoolId& poolId, const std::vector<uint8_t>& ciphertext) {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::string key = poolKey(poolId);
  m_eventBuffers[key].push_back(ciphertext);
}

std::vector<std::vector<uint8_t>> PoolOrganizer::getBufferedEvents(const PoolId& poolId) const {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::string key = poolKey(poolId);
  auto it = m_eventBuffers.find(key);
  if (it == m_eventBuffers.end()) {
    return {};
  }

  return it->second;
}

std::vector<Crypto::Hash> PoolOrganizer::getLpTreeLeaves(const PoolId& poolId) const {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::string key = poolKey(poolId);
  auto it = m_lpTrees.find(key);
  if (it == m_lpTrees.end()) {
    return {};
  }

  return it->second.leaves();
}

std::vector<Crypto::Hash> PoolOrganizer::getFeeTreeLeaves(const PoolId& poolId) const {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::string key = poolKey(poolId);
  auto it = m_feeTrees.find(key);
  if (it == m_feeTrees.end()) {
    return {};
  }

  return it->second.leaves();
}

// ─── ZK Proof Generation ───────────────────────────────────────────────

PoolOrganizer::EpochProofInputs PoolOrganizer::prepareProofInputs(const PoolId& poolId) {
  EpochProofInputs inputs = {};

  std::lock_guard<std::mutex> lock(m_mutex);

  std::string key = poolKey(poolId);

  auto poolIt = m_pools.find(key);
  if (poolIt != m_pools.end()) {
    inputs.prevState = poolIt->second;
  }

  auto epochIt = m_epochStates.find(key);
  if (epochIt != m_epochStates.end()) {
    uint32_t epoch = epochIt->second.epochNumber;
    inputs.epochKey = deriveEpochKey(poolId, epoch);
  }

  auto lpIt = m_lpTrees.find(key);
  if (lpIt != m_lpTrees.end()) {
    inputs.prevLpLeaves = lpIt->second.leaves();
  }

  auto feeIt = m_feeTrees.find(key);
  if (feeIt != m_feeTrees.end()) {
    inputs.prevFeeLeaves = feeIt->second.leaves();
  }

  uint32_t epochStart = 0;
  uint32_t epochEnd = 0;
  if (epochIt != m_epochStates.end()) {
    epochStart = epochIt->second.epochStart;
    epochEnd = epochIt->second.epochEnd;
  }

  for (const auto& event : m_events) {
    if (event.poolId == poolId && event.blockHeight >= epochStart && event.blockHeight < epochEnd) {
      inputs.events.push_back(event);
    }
  }

  return inputs;
}

void PoolOrganizer::updatePoolStateFromProof(const PoolId& poolId, const Crypto::Hash& newStateCommitment,
                                               const Crypto::Hash& newLpMerkleRoot, const Crypto::Hash& newFeeMerkleRoot,
                                               uint32_t epochEnd) {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::string key = poolKey(poolId);

  auto it = m_pools.find(key);
  if (it != m_pools.end()) {
    it->second.checkpointHash = newStateCommitment;
    it->second.blockHeight = epochEnd;
  }

  m_prevCheckpoints[key] = newStateCommitment;

  startEpoch(poolId, epochEnd);
}

Crypto::Hash PoolOrganizer::getStateCommitment(const PoolId& poolId) const {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::string key = poolKey(poolId);
  auto it = m_pools.find(key);
  if (it == m_pools.end()) {
    return Crypto::Hash{};
  }

  return it->second.checkpointHash;
}

} // namespace XfgSwap
