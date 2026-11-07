// Copyright (c) 2017-2026 Fuego Developers
//
// ElectrumSpvClient: ISpvClient implementation using the Electrum protocol
// for header synchronization.

#include "SwapDaemon/Spv/ElectrumSpvClient.h"
#include "SwapDaemon/Spv/SpvMerkle.h"

#include "SwapDaemon/BitcoinCash/HtlcScript.h"
#include "SwapDaemon/Utxo/UtxoFullNode.h"
#include "Common/JsonValue.h"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

namespace XfgSwap {

// =============================================================================
// Construction / Destruction
// =============================================================================

ElectrumSpvClient::ElectrumSpvClient(
    const std::vector<std::string>& servers,
    size_t minServers,
    uint64_t checkpointHeight,
    const std::string& checkpointHashDisplay)
    : m_serverAddrs(servers)
    , m_minServers(minServers)
    , m_checkpointHeight(checkpointHeight) {
  if (checkpointHeight > 0) {
    m_store.anchor(checkpointHeight, checkpointHashDisplay);
  }
  // Limit memory: keep only last 2000 block headers
  m_store.setMaxHeightDelta(2000);
}

ElectrumSpvClient::ElectrumSpvClient(
    const std::vector<std::string>& servers,
    size_t minServers,
    uint64_t checkpointHeight,
    const std::string& checkpointHashDisplay,
    const SpvChainRules& rules)
    : ElectrumSpvClient(servers, minServers, checkpointHeight, checkpointHashDisplay) {
  m_store.setRules(rules);
  // Keep a full retarget window plus the median-time span behind the tip.
  const uint64_t window = static_cast<uint64_t>(std::max<int64_t>(rules.interval(), 0)) + 12;
  m_store.setMaxHeightDelta(std::max<uint64_t>(2000, window));
}

std::string ElectrumSpvClient::quorumPolicyError() const {
  if (m_serverAddrs.size() < MIN_TRUSTED_SERVERS)
    return "SPV needs at least " + std::to_string(MIN_TRUSTED_SERVERS) + " Electrum servers";
  std::set<std::string> hosts;
  for (const auto& spec : m_serverAddrs) {
    ElectrumEndpoint endpoint;
    if (!parseElectrumEndpoint(spec, endpoint)) return "Electrum server '" + spec + "' is not a valid endpoint";
    hosts.insert(endpoint.host);
  }
  if (hosts.size() < MIN_TRUSTED_SERVERS) return "SPV servers must be on distinct hosts";
  if (m_minServers < 2 || m_minServers > m_serverAddrs.size())
    return "spv_min_servers must be at least 2 and at most the server count";
  if (m_checkpointHeight == 0) return "SPV needs a checkpoint";
  if (!m_store.hasRules()) return "SPV has no header rules for this chain";
  return m_store.rules().checkpointPlacementError(m_checkpointHeight);
}

std::string ElectrumSpvClient::newSwapTrustError() {
  const std::string policy = quorumPolicyError();
  if (!policy.empty()) return policy;
  if (!syncHeaders()) {
    const std::string why = m_store.lastRejection();
    return why.empty() ? "SPV header sync failed" : "SPV header sync failed: " + why;
  }
  return "";
}

namespace {

constexpr uint32_t HEADER_BATCH = 2016;  // ElectrumX's per-request maximum
constexpr uint32_t MAX_REORG_DEPTH = 100;

bool parseHeaderBatch(const std::string& result, std::vector<SpvHeader>& headers) {
  headers.clear();
  try {
    const Common::JsonValue json = Common::JsonValue::fromString(result);
    if (!json.isObject() || !json.contains("headers") || !json("headers").isString()) return false;
    const std::vector<uint8_t> raw = BchHtlcScript::hexToBytes(json("headers").getString());
    if (raw.empty() || raw.size() % 80 != 0) return false;
    for (size_t offset = 0; offset < raw.size(); offset += 80)
      headers.push_back(SpvHeader::parse(std::vector<uint8_t>(raw.begin() + offset, raw.begin() + offset + 80)));
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

bool fetchHeaders(ElectrumConnection& server, uint64_t start, uint32_t count,
                  std::vector<SpvHeader>& headers) {
  const std::string params = "[" + std::to_string(start) + "," + std::to_string(count) + "]";
  return parseHeaderBatch(server.call("blockchain.block.headers", params), headers);
}

bool fetchTipHeight(ElectrumConnection& server, uint64_t& height) {
  try {
    const Common::JsonValue json = Common::JsonValue::fromString(server.call("blockchain.headers.subscribe", "[]"));
    if (!json.isObject() || !json.contains("height") || !json("height").isInteger() ||
        json("height").getInteger() < 0)
      return false;
    height = static_cast<uint64_t>(json("height").getInteger());
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

bool fetchFeeRate(ElectrumConnection& server, const char* method, const char* params, uint64_t& perKilobyte) {
  try {
    const Common::JsonValue value = Common::JsonValue::fromString("[" + server.call(method, params) + "]")[0];
    return parseCoinAmount(value, perKilobyte) && perKilobyte > 0;
  } catch (const std::exception&) {
    return false;
  }
}

uint64_t median(std::vector<uint64_t> values) {
  std::sort(values.begin(), values.end());
  return values[values.size() / 2];
}

}  // namespace

bool ElectrumSpvClient::feeRatesPerKilobyte(uint64_t& relayFloor, uint64_t& nextBlocks) {
  if (!ensureConnected()) return false;
  std::vector<uint64_t> floors;
  std::vector<uint64_t> targets;
  for (auto& conn : m_conns) {
    uint64_t rate = 0;
    if (fetchFeeRate(*conn, "blockchain.relayfee", "[]", rate)) floors.push_back(rate);
    if (fetchFeeRate(*conn, "blockchain.estimatefee", "[2]", rate)) targets.push_back(rate);
  }
  // Medians: one server can neither starve a claim of fee nor inflate it.
  if (floors.size() < quorumSize()) return false;
  relayFloor = median(floors);
  nextBlocks = targets.size() >= quorumSize() ? std::max(relayFloor, median(targets)) : relayFloor;
  return true;
}

size_t ElectrumSpvClient::quorumSize() const {
  return m_conns.size() / 2 + 1;
}

bool ElectrumSpvClient::startFromCheckpoint() {
  const uint64_t span = std::min<uint64_t>(m_store.hasRules() ? m_store.rules().checkpointHistory() : 1,
                                           m_checkpointHeight + 1);
  for (auto& conn : m_conns) {
    std::vector<SpvHeader> history;
    for (uint64_t height = m_checkpointHeight + 1 - span; height <= m_checkpointHeight;) {
      const uint32_t count = static_cast<uint32_t>(std::min<uint64_t>(HEADER_BATCH, m_checkpointHeight - height + 1));
      std::vector<SpvHeader> batch;
      if (!fetchHeaders(*conn, height, count, batch) || batch.size() != count) break;
      history.insert(history.end(), batch.begin(), batch.end());
      height += count;
    }
    if (history.size() == span && m_store.addCheckpointHistory(history)) return true;
  }
  return false;
}

ElectrumSpvClient::~ElectrumSpvClient() = default;

// =============================================================================
// ISpvClient interface
// =============================================================================

std::string ElectrumSpvClient::protocolName() const {
  return "electrum";
}

bool ElectrumSpvClient::quorumTipHeight(uint64_t& height) {
  std::vector<uint64_t> tips;
  for (auto& conn : m_conns) {
    uint64_t tip = 0;
    if (fetchTipHeight(*conn, tip)) tips.push_back(tip);
  }
  if (tips.empty() || tips.size() < quorumSize()) return false;
  // The median tip cannot be raised or lowered by a minority of servers.
  height = median(tips);
  return true;
}

bool ElectrumSpvClient::syncHeaders() {
  if (!ensureConnected()) return false;
  uint64_t storeTip = 0;
  std::string storeTipHash;
  if (m_checkpointHeight > 0 && !m_store.bestTip(storeTip, storeTipHash) && !startFromCheckpoint())
    return false;
  uint64_t target = 0;
  if (!quorumTipHeight(target)) return false;
  // Headers come from one server at a time; a server whose headers the
  // quorum refuses is skipped and its headers are undone.
  for (auto& source : m_conns)
    if (syncFrom(*source, target)) return true;
  return false;
}

bool ElectrumSpvClient::syncFrom(ElectrumConnection& source, uint64_t target) {
  const SpvHeaderStore::Snapshot before = m_store.snapshot();
  const long double workBefore = m_store.tipWork();
  bool switchedBranch = false;
  uint64_t next = 0;
  if (!resumeHeight(source, target, next, switchedBranch) || !appendHeaders(source, next, target) ||
      (switchedBranch && !(m_store.tipWork() > workBefore))) {
    m_store.restore(before);
    return false;
  }
  return true;
}

bool ElectrumSpvClient::resumeHeight(ElectrumConnection& source, uint64_t target, uint64_t& next,
                                     bool& switchedBranch) {
  uint64_t tip = 0;
  std::string tipHash;
  if (!m_store.bestTip(tip, tipHash)) {
    next = 0;
    return m_checkpointHeight == 0;
  }
  const uint64_t compareTop = std::min(tip, target);
  std::vector<SpvHeader> top;
  SpvHeader ours;
  if (!fetchHeaders(source, compareTop, 1, top) || top.size() != 1 || !m_store.headerAtHeight(compareTop, ours))
    return false;
  if (top.front().hashDisplay() == ours.hashDisplay()) {
    next = compareTop + 1;
    return true;
  }
  uint64_t lowest = 0;
  m_store.lowestRewindHeight(lowest);
  const uint64_t windowStart = compareTop >= lowest + MAX_REORG_DEPTH ? compareTop - MAX_REORG_DEPTH + 1 : lowest;
  std::vector<SpvHeader> window;
  if (!fetchHeaders(source, windowStart, static_cast<uint32_t>(compareTop - windowStart + 1), window))
    return false;
  for (uint64_t offset = window.size(); offset-- > 0;) {
    const uint64_t height = windowStart + offset;
    if (m_store.headerAtHeight(height, ours) && ours.hashDisplay() == window[offset].hashDisplay()) {
      switchedBranch = true;
      next = height + 1;
      return m_store.rewindTo(height);
    }
  }
  return false;  // no common ancestor within the reorg limit
}

bool ElectrumSpvClient::appendHeaders(ElectrumConnection& source, uint64_t from, uint64_t target) {
  for (uint64_t height = from; height <= target;) {
    const uint32_t count = static_cast<uint32_t>(std::min<uint64_t>(HEADER_BATCH, target - height + 1));
    std::vector<SpvHeader> batch;
    if (!fetchHeaders(source, height, count, batch) || batch.empty() || batch.size() > count) return false;
    for (const SpvHeader& header : batch)
      if (!m_store.addHeader(header)) return false;
    const uint64_t batchTip = height + batch.size() - 1;
    if (!crossCheckHeader(batchTip, batch.back().hashDisplay())) return false;
    height = batchTip + 1;
  }
  return true;
}

// =============================================================================
// Eclipse mitigation: multi-server cross-check
// =============================================================================

bool ElectrumSpvClient::crossCheckHeader(uint64_t blockHeight, const std::string& hashDisplay) {
  if (m_conns.empty()) return false;
  if (m_conns.size() == 1) return true;  // the source is the only server; nothing to compare against
  size_t agreeCount = 0;
  for (auto& conn : m_conns) {
    std::vector<SpvHeader> headers;
    if (fetchHeaders(*conn, blockHeight, 1, headers) && headers.size() == 1 &&
        headers.front().hashDisplay() == hashDisplay)
      ++agreeCount;
  }
  return agreeCount >= quorumSize();
}

bool ElectrumSpvClient::getTipHeight(uint64_t& height) {
  if (m_conns.empty()) {
    std::string hash;
    return m_store.bestTip(height, hash);
  }
  uint64_t quorumTip = 0;
  SpvHeader header;
  if (!quorumTipHeight(quorumTip) || !m_store.headerAtHeight(quorumTip, header) ||
      !crossCheckHeader(quorumTip, header.hashDisplay()))
    return false;
  height = quorumTip;
  return true;
}

// =============================================================================
// Transaction inclusion
// =============================================================================

ElectrumSpvClient::ProofCheck ElectrumSpvClient::provenHeight(ElectrumConnection& server,
                                                              const std::string& txid, uint64_t& height) {
  Common::JsonValue json;
  try {
    json = Common::JsonValue::fromString(server.call("blockchain.transaction.get_merkle", "[\"" + txid + "\"]"));
  } catch (const std::exception&) {
    return ProofCheck::UNAVAILABLE;
  }
  if (!json.isObject() || !json.contains("block_height") || !json.contains("merkle") || !json.contains("pos") ||
      !json("block_height").isInteger() || json("block_height").getInteger() <= 0 ||
      !json("pos").isInteger() || json("pos").getInteger() < 0 || json("pos").getInteger() > UINT32_MAX ||
      !json("merkle").isArray() || json("merkle").size() > 64)
    return ProofCheck::UNAVAILABLE;
  std::vector<std::string> branch;
  const Common::JsonValue& merkle = json("merkle");
  for (size_t i = 0; i < merkle.size(); ++i) {
    if (!merkle[i].isString()) return ProofCheck::UNAVAILABLE;
    branch.push_back(merkle[i].getString());
  }
  height = static_cast<uint64_t>(json("block_height").getInteger());
  std::vector<uint8_t> rootLE;
  if (!m_store.merkleRootAt(height, rootLE)) return ProofCheck::UNAVAILABLE;
  const std::vector<uint8_t> rootBE(rootLE.rbegin(), rootLE.rend());
  const uint32_t position = static_cast<uint32_t>(json("pos").getInteger());
  return SpvMerkle::computeRootHexDisplay(txid, branch, position) == BchHtlcScript::bytesToHex(rootBE)
             ? ProofCheck::PROVEN
             : ProofCheck::MISMATCH;
}

bool ElectrumSpvClient::verifyTxInclusion(const std::string& txid, SpvTxInclusion& out) {
  out = SpvTxInclusion();
  if (m_conns.empty() || !isTxidHex(txid)) return false;
  std::map<uint64_t, size_t> provenAt;
  bool answered = false;
  for (auto& conn : m_conns) {
    uint64_t height = 0;
    const ProofCheck check = provenHeight(*conn, txid, height);
    if (check != ProofCheck::UNAVAILABLE) answered = true;
    if (check == ProofCheck::PROVEN) ++provenAt[height];
  }
  for (const auto& vote : provenAt) {
    if (vote.second < quorumSize()) continue;
    out.included = true;
    out.merkleVerified = true;
    out.blockHeight = vote.first;
    out.depth = m_store.depthOf(vote.first);
    return true;
  }
  return answered;
}

// =============================================================================
// Task 8: getRawTx + findSpend
// =============================================================================

// Minimal raw Bitcoin transaction parser (non-witness parts only).

struct ParsedTxInput {
  std::vector<uint8_t> prevoutHash;  // 32 bytes
  uint32_t prevoutIndex = 0;
};

struct ParsedTxOutput {
  std::vector<uint8_t> scriptPubKey;
};

struct ParsedTx {
  bool valid = false;
  bool hasWitness = false;
  size_t baseStart = 4;
  size_t baseEnd = 0;
  std::vector<ParsedTxInput> inputs;
  std::vector<ParsedTxOutput> outputs;
};

static bool readVarInt(const std::vector<uint8_t>& data, size_t& pos, uint64_t& result) {
  if (pos >= data.size()) return false;
  uint8_t first = data[pos++];
  if (first < 0xFD) {
    result = first;
    return true;
  }
  if (first == 0xFD) {
    if (pos + 2 > data.size()) return false;
    result = static_cast<uint64_t>(data[pos]) | (static_cast<uint64_t>(data[pos + 1]) << 8);
    pos += 2;
    return true;
  }
  if (first == 0xFE) {
    if (pos + 4 > data.size()) return false;
    result = static_cast<uint64_t>(data[pos])
        | (static_cast<uint64_t>(data[pos + 1]) << 8)
        | (static_cast<uint64_t>(data[pos + 2]) << 16)
        | (static_cast<uint64_t>(data[pos + 3]) << 24);
    pos += 4;
    return true;
  }
  // 0xFF — 8-byte varint
  if (pos + 8 > data.size()) return false;
  result = 0;
  for (int i = 0; i < 8; ++i) {
    result |= static_cast<uint64_t>(data[pos + i]) << (8 * i);
  }
  pos += 8;
  return true;
}

static bool readBytes(const std::vector<uint8_t>& data, size_t& pos, size_t count, std::vector<uint8_t>& out) {
  if (pos + count > data.size()) return false;
  out.assign(data.begin() + pos, data.begin() + pos + count);
  pos += count;
  return true;
}

static bool readU32LE(const std::vector<uint8_t>& data, size_t& pos, uint32_t& out) {
  if (pos + 4 > data.size()) return false;
  out = static_cast<uint32_t>(data[pos])
      | (static_cast<uint32_t>(data[pos + 1]) << 8)
      | (static_cast<uint32_t>(data[pos + 2]) << 16)
      | (static_cast<uint32_t>(data[pos + 3]) << 24);
  pos += 4;
  return true;
}

static ParsedTx parseRawTx(const std::vector<uint8_t>& raw) {
  ParsedTx tx;
  size_t pos = 0;

  // version (4 bytes LE)
  uint32_t version;
  if (!readU32LE(raw, pos, version)) return tx;

  // Check for SegWit marker: after version, if byte 0x00 then next is 0x01
  bool hasWitness = false;
  if (pos < raw.size() && raw[pos] == 0x00) {
    if (pos + 1 < raw.size() && raw[pos + 1] == 0x01) {
      hasWitness = true;
      pos += 2;  // skip marker + flag
    }
  }
  tx.hasWitness = hasWitness;
  tx.baseStart = pos;

  // input count
  uint64_t inputCount;
  if (!readVarInt(raw, pos, inputCount)) return tx;

  // parse inputs
  for (uint64_t i = 0; i < inputCount; ++i) {
    ParsedTxInput in;
    if (!readBytes(raw, pos, 32, in.prevoutHash)) return tx;
    if (!readU32LE(raw, pos, in.prevoutIndex)) return tx;

    // scriptSig
    uint64_t scriptSigLen;
    if (!readVarInt(raw, pos, scriptSigLen)) return tx;
    pos += scriptSigLen;
    if (pos > raw.size()) return tx;

    // sequence (4 bytes LE)
    pos += 4;
    if (pos > raw.size()) return tx;

    tx.inputs.push_back(std::move(in));
  }

  // output count
  uint64_t outputCount;
  if (!readVarInt(raw, pos, outputCount)) return tx;

  // parse outputs
  for (uint64_t i = 0; i < outputCount; ++i) {
    ParsedTxOutput out;
    pos += 8;  // value (8 bytes LE)
    if (pos > raw.size()) return tx;

    uint64_t scriptPubKeyLen;
    if (!readVarInt(raw, pos, scriptPubKeyLen)) return tx;
    if (!readBytes(raw, pos, scriptPubKeyLen, out.scriptPubKey)) return tx;

    tx.outputs.push_back(std::move(out));
  }
  tx.baseEnd = pos;

  // If witness, skip witness data (not needed for our purposes)
  // Witness: for each input, count + witness items
  if (hasWitness) {
    for (uint64_t i = 0; i < inputCount; ++i) {
      uint64_t itemCount;
      if (!readVarInt(raw, pos, itemCount)) return tx;
      for (uint64_t j = 0; j < itemCount; ++j) {
        uint64_t itemLen;
        if (!readVarInt(raw, pos, itemLen)) return tx;
        pos += itemLen;
        if (pos > raw.size()) return tx;
      }
    }
  }

  // Locktime is exactly four bytes; reject trailing or truncated data before
  // hashing the transaction identity.
  if (pos + 4 != raw.size()) return tx;

  tx.valid = true;
  return tx;
}

std::string ElectrumSpvClient::computeTransactionId(
    const std::vector<uint8_t>& rawTx) {
  const ParsedTx parsed = parseRawTx(rawTx);
  if (!parsed.valid || rawTx.size() == 64) return {};
  std::vector<uint8_t> base;
  if (parsed.hasWitness) {
    base.insert(base.end(), rawTx.begin(), rawTx.begin() + 4);
    base.insert(base.end(), rawTx.begin() + parsed.baseStart,
                rawTx.begin() + parsed.baseEnd);
    base.insert(base.end(), rawTx.end() - 4, rawTx.end());
  } else {
    base = rawTx;
  }
  std::vector<uint8_t> hash = BchHtlcScript::doubleSha256(base);
  if (hash.size() != 32) return {};
  std::reverse(hash.begin(), hash.end());
  return BchHtlcScript::bytesToHex(hash);
}

// Compute Electrum scripthash: reverse(sha256(scriptPubKey))
static std::string computeElectrumScripthash(const std::vector<uint8_t>& scriptPubKey) {
  std::vector<uint8_t> hash = BchHtlcScript::sha256(scriptPubKey);
  std::reverse(hash.begin(), hash.end());
  return BchHtlcScript::bytesToHex(hash);
}

bool ElectrumSpvClient::getRawTx(const std::string& txid, std::vector<uint8_t>& rawTx) {
  rawTx.clear();
  if (!isTxidHex(txid)) return false;
  std::string expected = txid;
  std::transform(expected.begin(), expected.end(), expected.begin(), ::tolower);
  // The bytes authenticate themselves against the txid, so any one server
  // that returns them is enough.
  for (auto& conn : m_conns) {
    const std::string result = conn->call("blockchain.transaction.get", "[\"" + txid + "\"]");
    std::vector<uint8_t> decoded;
    if (!decodeHex(result, decoded)) continue;
    std::string computed = computeTransactionId(decoded);
    std::transform(computed.begin(), computed.end(), computed.begin(), ::tolower);
    if (computed.empty() || computed != expected) continue;
    rawTx = std::move(decoded);
    return true;
  }
  return false;
}

std::set<std::string> ElectrumSpvClient::scriptHistory(const std::vector<uint8_t>& scriptPubKey) {
  const std::string params = "[\"" + computeElectrumScripthash(scriptPubKey) + "\"]";
  std::set<std::string> txids;
  for (auto& conn : m_conns) {
    try {
      const Common::JsonValue history =
          Common::JsonValue::fromString(conn->call("blockchain.scripthash.get_history", params));
      if (!history.isArray()) continue;
      for (size_t i = 0; i < history.size(); ++i) {
        const Common::JsonValue& entry = history[i];
        if (entry.isObject() && entry.contains("tx_hash") && entry("tx_hash").isString() &&
            isTxidHex(entry("tx_hash").getString()))
          txids.insert(entry("tx_hash").getString());
      }
    } catch (const std::exception&) {
      continue;
    }
  }
  return txids;
}

bool ElectrumSpvClient::findSpend(const std::string& txid, uint32_t vout, SpvSpend& out) {
  out = SpvSpend();
  std::vector<uint8_t> fundingRaw;
  if (!getRawTx(txid, fundingRaw)) return false;
  const ParsedTx fundingTx = parseRawTx(fundingRaw);
  if (!fundingTx.valid) return false;
  if (vout >= fundingTx.outputs.size()) return true;

  const std::vector<uint8_t> fundingTxidBE = BchHtlcScript::hexToBytes(txid);
  const std::vector<uint8_t> fundingTxidLE(fundingTxidBE.rbegin(), fundingTxidBE.rend());
  // Every server's history is searched: one server hiding the spend is not enough.
  for (const std::string& candidateTxid : scriptHistory(fundingTx.outputs[vout].scriptPubKey)) {
    std::vector<uint8_t> candidateRaw;
    if (candidateTxid == txid || !getRawTx(candidateTxid, candidateRaw)) continue;
    const ParsedTx candidateTx = parseRawTx(candidateRaw);
    if (!candidateTx.valid) continue;
    for (const auto& input : candidateTx.inputs) {
      if (input.prevoutHash != fundingTxidLE || input.prevoutIndex != vout) continue;
      out.spent = true;
      out.spendingTxid = candidateTxid;
      out.rawSpendingTx = std::move(candidateRaw);
      verifyTxInclusion(candidateTxid, out.inclusion);
      return true;
    }
  }
  return true;
}

bool ElectrumSpvClient::broadcastTx(const std::vector<uint8_t>& rawTx, std::string& txid) {
  const std::string localTxid = computeTransactionId(rawTx);
  if (localTxid.empty()) return false;
  const std::string params = "[\"" + BchHtlcScript::bytesToHex(rawTx) + "\"]";
  // Sent to every server: one server cannot withhold a claim it has seen.
  bool accepted = false;
  for (auto& conn : m_conns) {
    std::string returned = conn->call("blockchain.transaction.broadcast", params);
    std::transform(returned.begin(), returned.end(), returned.begin(), ::tolower);
    if (returned == localTxid) accepted = true;
  }
  if (accepted) txid = localTxid;
  return accepted;
}

// =============================================================================
// Private helpers
// =============================================================================

bool ElectrumSpvClient::ensureConnected() {
  const bool alive = !m_conns.empty() && m_conns.size() >= m_minServers &&
                     std::all_of(m_conns.begin(), m_conns.end(), [](const std::unique_ptr<ElectrumConnection>& conn) {
                       return !conn->call("server.ping", "[]").empty();
                     });
  return alive || connectToServers();
}

bool ElectrumSpvClient::connectToServers() {
  m_conns.clear();
  for (const auto& addr : m_serverAddrs) {
    ElectrumEndpoint endpoint;
    if (!parseElectrumEndpoint(addr, endpoint)) continue;
    auto conn = std::make_unique<ElectrumConnection>();
    conn->setConnectTimeout(5);
    conn->setReadTimeout(30);
    if (conn->connect(endpoint) && !conn->call("server.version", R"(["FuegoSPV","1.4"])").empty())
      m_conns.push_back(std::move(conn));
  }
  return !m_conns.empty() && m_conns.size() >= m_minServers;
}

} // namespace XfgSwap
