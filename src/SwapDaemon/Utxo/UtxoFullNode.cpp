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

#include "UtxoFullNode.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

namespace XfgSwap {

namespace {

constexpr uint64_t COIN = 100000000ULL;
constexpr uint64_t MAX_COIN_AMOUNT = 21000000ULL * 84ULL * COIN;  // LTC supply bounds every chain here

std::string quoted(const std::string& s) { return "\"" + s + "\""; }

std::string lowercase(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

int hexDigitValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool readVarInt(const uint8_t*& p, const uint8_t* end, uint64_t& value) {
  if (p >= end) return false;
  const uint8_t prefix = *p++;
  size_t width = 0;
  if (prefix < 0xFD) {
    value = prefix;
    return true;
  }
  width = prefix == 0xFD ? 2 : (prefix == 0xFE ? 4 : 8);
  if (static_cast<size_t>(end - p) < width) return false;
  value = 0;
  for (size_t i = 0; i < width; ++i) value |= static_cast<uint64_t>(p[i]) << (8 * i);
  p += width;
  return true;
}

bool skipBytes(const uint8_t*& p, const uint8_t* end, uint64_t count) {
  if (static_cast<uint64_t>(end - p) < count) return false;
  p += count;
  return true;
}

bool readInteger(const Common::JsonValue& object, const std::string& key, int64_t& value) {
  if (!object.isObject() || !object.contains(key) || !object(key).isInteger()) return false;
  value = object(key).getInteger();
  return true;
}

bool readString(const Common::JsonValue& object, const std::string& key, std::string& value) {
  if (!object.isObject() || !object.contains(key) || !object(key).isString()) return false;
  value = object(key).getString();
  return true;
}

bool fetchBlockHash(const UtxoRpcCall& rpc, uint64_t height, std::string& hash, std::string& error) {
  Common::JsonValue result;
  if (!rpc("getblockhash", "[" + std::to_string(height) + "]", result, error)) return false;
  if (!result.isString() || !isTxidHex(result.getString())) {
    error = "getblockhash returned no hash for height " + std::to_string(height);
    return false;
  }
  hash = lowercase(result.getString());
  return true;
}

bool inputSpendsOutpoint(const Common::JsonValue& input, const UtxoOutpoint& lock) {
  std::string txid;
  int64_t vout = -1;
  return readString(input, "txid", txid) && readInteger(input, "vout", vout) &&
         lowercase(txid) == lock.txid && vout == static_cast<int64_t>(lock.vout);
}

bool transactionSpendsOutpoint(const Common::JsonValue& tx, const UtxoOutpoint& lock) {
  if (!tx.isObject() || !tx.contains("vin") || !tx("vin").isArray()) return false;
  const Common::JsonValue& inputs = tx("vin");
  for (size_t i = 0; i < inputs.size(); ++i) {
    if (inputSpendsOutpoint(inputs[i], lock)) return true;
  }
  return false;
}

bool recordSpendFromJson(const Common::JsonValue& tx, UtxoSpend& spend) {
  std::string txid, hex;
  if (!readString(tx, "txid", txid) || !readString(tx, "hex", hex)) return false;
  std::vector<uint8_t> raw;
  if (!isTxidHex(txid) || !decodeHex(hex, raw)) return false;
  spend.spendingTxid = lowercase(txid);
  spend.rawTx = std::move(raw);
  return true;
}

uint32_t confirmationsAt(uint64_t tip, uint64_t blockHeight) {
  if (blockHeight == 0 || tip < blockHeight) return 0;
  const uint64_t depth = tip - blockHeight + 1;
  return depth > std::numeric_limits<uint32_t>::max() ? std::numeric_limits<uint32_t>::max()
                                                      : static_cast<uint32_t>(depth);
}

}  // namespace

bool parseUtxoNetwork(const std::string& name, UtxoNetwork& network) {
  const std::string n = lowercase(name);
  if (n.empty() || n == "main" || n == "mainnet") network = UtxoNetwork::MAINNET;
  else if (n == "test" || n == "testnet") network = UtxoNetwork::TESTNET;
  else if (n == "signet") network = UtxoNetwork::SIGNET;
  else if (n == "regtest") network = UtxoNetwork::REGTEST;
  else return false;
  return true;
}

UtxoNetworkParams bitcoinNetworkParams(UtxoNetwork network) {
  switch (network) {
    case UtxoNetwork::MAINNET: return {"main", "bc", 0x00, 0x05, 0x80};
    case UtxoNetwork::TESTNET: return {"test", "tb", 0x6f, 0xc4, 0xef};
    case UtxoNetwork::SIGNET:  return {"signet", "tb", 0x6f, 0xc4, 0xef};
    case UtxoNetwork::REGTEST: return {"regtest", "bcrt", 0x6f, 0xc4, 0xef};
  }
  return {"main", "bc", 0x00, 0x05, 0x80};
}

UtxoNetworkParams litecoinNetworkParams(UtxoNetwork network) {
  switch (network) {
    case UtxoNetwork::MAINNET: return {"main", "ltc", 0x30, 0x32, 0xb0};
    case UtxoNetwork::TESTNET: return {"test", "tltc", 0x6f, 0x3a, 0xef};
    case UtxoNetwork::SIGNET:  return {"signet", "tltc", 0x6f, 0x3a, 0xef};
    case UtxoNetwork::REGTEST: return {"regtest", "rltc", 0x6f, 0x3a, 0xef};
  }
  return {"main", "ltc", 0x30, 0x32, 0xb0};
}

UtxoNetworkParams bitcoinCashNetworkParams(UtxoNetwork network) {
  switch (network) {
    case UtxoNetwork::MAINNET: return {"main", "", 0x00, 0x05, 0x80};
    case UtxoNetwork::TESTNET: return {"test", "", 0x6f, 0xc4, 0xef};
    case UtxoNetwork::SIGNET:  return {"signet", "", 0x6f, 0xc4, 0xef};
    case UtxoNetwork::REGTEST: return {"regtest", "", 0x6f, 0xc4, 0xef};
  }
  return {"main", "", 0x00, 0x05, 0x80};
}

std::string formatCoinAmount(uint64_t baseUnits) {
  std::string fraction = std::to_string(baseUnits % COIN);
  fraction.insert(0, 8 - fraction.size(), '0');
  return std::to_string(baseUnits / COIN) + "." + fraction;
}

bool parseCoinAmount(const Common::JsonValue& value, uint64_t& baseUnits) {
  if (value.isInteger()) {
    const int64_t coins = value.getInteger();
    if (coins < 0 || static_cast<uint64_t>(coins) > MAX_COIN_AMOUNT / COIN) return false;
    baseUnits = static_cast<uint64_t>(coins) * COIN;
    return true;
  }
  if (!value.isReal()) return false;
  const double coins = value.getReal();
  if (!std::isfinite(coins) || coins < 0.0) return false;
  const double units = std::round(coins * static_cast<double>(COIN));
  if (units > static_cast<double>(MAX_COIN_AMOUNT)) return false;
  baseUnits = static_cast<uint64_t>(units);
  return true;
}

bool isTxidHex(const std::string& txid) {
  return txid.size() == 64 &&
         std::all_of(txid.begin(), txid.end(), [](char c) { return hexDigitValue(c) >= 0; });
}

bool decodeHex(const std::string& hex, std::vector<uint8_t>& bytes) {
  if (hex.size() % 2 != 0) return false;
  std::vector<uint8_t> out;
  out.reserve(hex.size() / 2);
  for (size_t i = 0; i < hex.size(); i += 2) {
    const int hi = hexDigitValue(hex[i]);
    const int lo = hexDigitValue(hex[i + 1]);
    if (hi < 0 || lo < 0) return false;
    out.push_back(static_cast<uint8_t>((hi << 4) | lo));
  }
  bytes = std::move(out);
  return true;
}

std::string encodeHex(const std::vector<uint8_t>& bytes) {
  static const char digits[] = "0123456789abcdef";
  std::string out;
  out.reserve(bytes.size() * 2);
  for (uint8_t b : bytes) {
    out.push_back(digits[b >> 4]);
    out.push_back(digits[b & 0x0F]);
  }
  return out;
}

bool parseTransactionOutputs(const std::vector<uint8_t>& rawTx, std::vector<UtxoTxOutput>& outputs) {
  const uint8_t* p = rawTx.data();
  const uint8_t* end = p + rawTx.size();
  if (!skipBytes(p, end, 4)) return false;  // version
  // Segwit (flag 0x01) and Litecoin MWEB (flag 0x08) transactions carry a
  // 0x00 marker where a legacy transaction carries its input count. Outputs
  // precede witness and extension data, so the flag value does not matter.
  if (end - p >= 2 && p[0] == 0x00 && p[1] != 0x00) p += 2;
  uint64_t inputCount = 0;
  if (!readVarInt(p, end, inputCount)) return false;
  for (uint64_t i = 0; i < inputCount; ++i) {
    uint64_t scriptSize = 0;
    if (!skipBytes(p, end, 36) || !readVarInt(p, end, scriptSize) ||
        !skipBytes(p, end, scriptSize) || !skipBytes(p, end, 4))
      return false;
  }
  uint64_t outputCount = 0;
  if (!readVarInt(p, end, outputCount)) return false;
  std::vector<UtxoTxOutput> parsed;
  for (uint64_t i = 0; i < outputCount; ++i) {
    if (end - p < 8) return false;
    UtxoTxOutput output;
    for (int b = 0; b < 8; ++b) output.value |= static_cast<uint64_t>(p[b]) << (8 * b);
    p += 8;
    uint64_t scriptSize = 0;
    if (!readVarInt(p, end, scriptSize) || static_cast<uint64_t>(end - p) < scriptSize) return false;
    output.scriptPubKey.assign(p, p + scriptSize);
    p += scriptSize;
    parsed.push_back(std::move(output));
  }
  outputs = std::move(parsed);
  return true;
}

UtxoOutputLookup lookupUnspentOutput(const UtxoRpcCall& rpc, const UtxoOutpoint& outpoint,
                                     bool includeMempool, UtxoUnspentOutput& output,
                                     std::string& error) {
  if (!isTxidHex(outpoint.txid)) {
    error = "malformed lock txid";
    return UtxoOutputLookup::RPC_FAILED;
  }
  Common::JsonValue result;
  const std::string params = "[" + quoted(lowercase(outpoint.txid)) + "," +
                             std::to_string(outpoint.vout) + "," +
                             (includeMempool ? "true" : "false") + "]";
  if (!rpc("gettxout", params, result, error)) return UtxoOutputLookup::RPC_FAILED;
  if (result.isNil()) return UtxoOutputLookup::NOT_UNSPENT;
  int64_t confirmations = 0;
  if (!result.isObject() || !result.contains("value") ||
      !parseCoinAmount(result("value"), output.value) ||
      !readInteger(result, "confirmations", confirmations) || confirmations < 0 ||
      !result.contains("scriptPubKey") ||
      !readString(result("scriptPubKey"), "hex", output.scriptPubKeyHex)) {
    error = "gettxout returned a malformed output";
    return UtxoOutputLookup::RPC_FAILED;
  }
  output.scriptPubKeyHex = lowercase(output.scriptPubKeyHex);
  output.confirmations = static_cast<uint32_t>(
      std::min<int64_t>(confirmations, std::numeric_limits<uint32_t>::max()));
  return UtxoOutputLookup::UNSPENT;
}

ChainClientResult verifyLockedOutput(const UtxoRpcCall& rpc, const UtxoOutpoint& outpoint,
                                     const UtxoLockExpectation& expectation) {
  UtxoUnspentOutput output;
  std::string error;
  switch (lookupUnspentOutput(rpc, outpoint, false, output, error)) {
    case UtxoOutputLookup::RPC_FAILED:
      return ChainClientResult::fail("lock lookup failed: " + error);
    case UtxoOutputLookup::NOT_UNSPENT:
      return ChainClientResult::fail("lock output " + outpoint.key() +
                                     " is not an unspent confirmed output");
    case UtxoOutputLookup::UNSPENT:
      break;
  }
  if (output.scriptPubKeyHex != encodeHex(expectation.scriptPubKey))
    return ChainClientResult::fail("lock output " + outpoint.key() +
                                   " does not pay the negotiated lock script");
  if (output.value < expectation.minValue)
    return ChainClientResult::fail("lock output " + outpoint.key() + " holds " +
                                   std::to_string(output.value) + ", expected at least " +
                                   std::to_string(expectation.minValue));
  if (output.confirmations < expectation.minConfirmations)
    return ChainClientResult::fail("lock output " + outpoint.key() + " has " +
                                   std::to_string(output.confirmations) + " of " +
                                   std::to_string(expectation.minConfirmations) +
                                   " required confirmations");
  ChainClientResult verified = ChainClientResult::ok(outpoint.txid);
  verified.confirmed = true;
  verified.confirmations = output.confirmations;
  verified.outputIndex = outpoint.vout;
  return verified;
}

bool fetchChainTip(const UtxoRpcCall& rpc, uint64_t& height, std::string& error) {
  Common::JsonValue result;
  if (!rpc("getblockcount", "[]", result, error)) return false;
  if (!result.isInteger() || result.getInteger() < 0) {
    error = "getblockcount returned no height";
    return false;
  }
  height = static_cast<uint64_t>(result.getInteger());
  return true;
}

static bool readWalletTransactionOutputs(const UtxoRpcCall& rpc, const std::string& txid,
                                         std::vector<UtxoTxOutput>& outputs, std::string& error) {
  if (!isTxidHex(txid)) {
    error = "malformed txid";
    return false;
  }
  Common::JsonValue result;
  if (!rpc("gettransaction", "[" + quoted(lowercase(txid)) + "]", result, error)) return false;
  std::string hex;
  std::vector<uint8_t> raw;
  if (!readString(result, "hex", hex) || !decodeHex(hex, raw) || !parseTransactionOutputs(raw, outputs)) {
    error = "gettransaction returned no parsable transaction";
    return false;
  }
  return true;
}

bool findWalletOutputIndex(const UtxoRpcCall& rpc, const std::string& txid,
                           const std::vector<uint8_t>& scriptPubKey, uint32_t& vout,
                           std::string& error) {
  std::vector<UtxoTxOutput> outputs;
  if (!readWalletTransactionOutputs(rpc, txid, outputs, error)) return false;
  for (size_t i = 0; i < outputs.size(); ++i) {
    if (outputs[i].scriptPubKey == scriptPubKey) {
      vout = static_cast<uint32_t>(i);
      return true;
    }
  }
  error = "wallet transaction " + txid + " has no output paying the lock script";
  return false;
}

bool walletTransactionPays(const UtxoRpcCall& rpc, const std::string& txid,
                           const std::vector<uint8_t>& scriptPubKey, uint64_t value) {
  std::vector<UtxoTxOutput> outputs;
  std::string error;
  if (!readWalletTransactionOutputs(rpc, txid, outputs, error)) return false;
  return std::any_of(outputs.begin(), outputs.end(), [&](const UtxoTxOutput& output) {
    return output.scriptPubKey == scriptPubKey && output.value == value;
  });
}

bool lookupWalletTxBlockHeight(const UtxoRpcCall& rpc, const std::string& txid,
                               uint64_t& blockHeight, std::string& error) {
  if (!isTxidHex(txid)) {
    error = "malformed txid";
    return false;
  }
  Common::JsonValue tx;
  if (!rpc("gettransaction", "[" + quoted(lowercase(txid)) + "]", tx, error)) return false;
  std::string blockHash;
  if (!readString(tx, "blockhash", blockHash) || !isTxidHex(blockHash)) {
    error = "lock transaction " + txid + " is not in a block yet";
    return false;
  }
  Common::JsonValue header;
  int64_t height = 0;
  if (!rpc("getblockheader", "[" + quoted(lowercase(blockHash)) + "]", header, error)) return false;
  if (!readInteger(header, "height", height) || height <= 0) {
    error = "getblockheader returned no height";
    return false;
  }
  blockHeight = static_cast<uint64_t>(height);
  return true;
}

bool broadcastRawTransaction(const UtxoRpcCall& rpc, const std::vector<uint8_t>& rawTx,
                             std::string& txid, std::string& error) {
  Common::JsonValue result;
  if (!rpc("sendrawtransaction", "[" + quoted(encodeHex(rawTx)) + "]", result, error)) return false;
  if (!result.isString() || !isTxidHex(result.getString())) {
    error = "sendrawtransaction returned no txid";
    return false;
  }
  txid = lowercase(result.getString());
  return true;
}

namespace {

// Per-kilobyte rate from `field` of `method`'s result, in base units.
uint64_t nodeFeeRate(const UtxoRpcCall& rpc, const char* method, const char* params,
                     const char* field) {
  Common::JsonValue result;
  std::string error;
  uint64_t perKilobyte = 0;
  if (rpc(method, params, result, error) && result.isObject() && result.contains(field) &&
      parseCoinAmount(result(field), perKilobyte))
    return perKilobyte;
  return 0;
}

}  // namespace

uint64_t boundedFee(uint64_t floorRate, uint64_t targetRate, uint64_t virtualBytes, uint64_t ceiling) {
  const uint64_t floorFee = (floorRate * virtualBytes + 999) / 1000;
  const uint64_t targetFee = (std::max(floorRate, targetRate) * virtualBytes + 999) / 1000;
  return std::min(targetFee, std::max(ceiling, floorFee));
}

uint64_t estimateFeeForSize(const UtxoRpcCall& rpc, uint64_t virtualBytes, uint64_t ceiling) {
  // The node's relay and mempool minimums bound the fee from below (Litecoin's
  // default relay fee is ten times Bitcoin's); estimatesmartfee raises it.
  const uint64_t floorRate = std::max({uint64_t{1000},
                                       nodeFeeRate(rpc, "getnetworkinfo", "[]", "relayfee"),
                                       nodeFeeRate(rpc, "getmempoolinfo", "[]", "mempoolminfee")});
  return boundedFee(floorRate, nodeFeeRate(rpc, "estimatesmartfee", "[2]", "feerate"), virtualBytes,
                    ceiling);
}

UtxoNodeHealth checkNodeHealth(const UtxoRpcCall& rpc, const std::string& expectedChain) {
  UtxoNodeHealth health;
  Common::JsonValue info;
  std::string error;
  if (!rpc("getblockchaininfo", "[]", info, error)) {
    health.reason = "node RPC unreachable: " + error;
    return health;
  }
  std::string chain;
  if (!readString(info, "chain", chain)) {
    health.reason = "getblockchaininfo returned no chain";
    return health;
  }
  if (chain != expectedChain) {
    health.reason = "node is on chain '" + chain + "', configured '" + expectedChain + "'";
    return health;
  }
  if (!info.contains("initialblockdownload") || !info("initialblockdownload").isBool()) {
    health.reason = "node does not report initial block download state";
    return health;
  }
  if (info("initialblockdownload").getBool()) {
    health.reason = "node is in initial block download";
    return health;
  }
  Common::JsonValue wallet;
  if (!rpc("getwalletinfo", "[]", wallet, error)) {
    health.reason = "no wallet loaded on the node: " + error;
    return health;
  }
  if (wallet.isObject() && wallet.contains("private_keys_enabled") &&
      wallet("private_keys_enabled").isBool() && !wallet("private_keys_enabled").getBool()) {
    health.reason = "the loaded wallet is watch-only and cannot fund a lock";
    return health;
  }
  health.ready = true;
  return health;
}

UtxoSpendTracker::UtxoSpendTracker(uint64_t maxBlocksPerCall)
    : m_maxBlocksPerCall(maxBlocksPerCall == 0 ? 1 : maxBlocksPerCall) {}

bool UtxoSpendTracker::findSpend(const UtxoRpcCall& rpc, const UtxoOutpoint& lockIn,
                                 uint64_t lockHeight, UtxoSpend& spend, std::string& error) {
  if (!isTxidHex(lockIn.txid) || lockHeight == 0) {
    error = "lock outpoint or lock height unknown";
    return false;
  }
  UtxoOutpoint lock = lockIn;
  lock.txid = lowercase(lock.txid);

  std::lock_guard<std::mutex> guard(m_mutex);
  ScanProgress& progress = m_progress[lock.key()];
  if (progress.lockHeight != lockHeight) {
    progress = ScanProgress{};
    progress.lockHeight = lockHeight;
    progress.nextHeight = lockHeight;
  }

  uint64_t tip = 0;
  if (!fetchChainTip(rpc, tip, error)) return false;
  if (progress.spendFound) {
    if (confirmRecordedSpend(rpc, progress, tip, spend, error)) return true;
    if (!error.empty()) return false;
  }
  if (!rewindIfReorganized(rpc, progress, error)) return false;

  UtxoUnspentOutput unspent;
  switch (lookupUnspentOutput(rpc, lock, true, unspent, error)) {
    case UtxoOutputLookup::RPC_FAILED: return false;
    case UtxoOutputLookup::UNSPENT: return false;
    case UtxoOutputLookup::NOT_UNSPENT: break;
  }
  if (scanBlocks(rpc, lock, progress, tip, spend, error)) return true;
  if (!error.empty()) return false;
  return findMempoolSpend(rpc, lock, spend);
}

bool UtxoSpendTracker::confirmRecordedSpend(const UtxoRpcCall& rpc, ScanProgress& progress,
                                            uint64_t tip, UtxoSpend& spend, std::string& error) {
  std::string hashNow;
  if (!fetchBlockHash(rpc, progress.spend.blockHeight, hashNow, error)) {
    // A height above the tip after a reorg is a rewind, not a failure.
    if (progress.spend.blockHeight <= tip) return false;
    error.clear();
  }
  if (hashNow == progress.spend.blockHash) {
    spend = progress.spend;
    spend.confirmations = confirmationsAt(tip, spend.blockHeight);
    return true;
  }
  const uint64_t lockHeight = progress.lockHeight;
  progress = ScanProgress{};
  progress.lockHeight = lockHeight;
  progress.nextHeight = lockHeight;
  return false;
}

bool UtxoSpendTracker::rewindIfReorganized(const UtxoRpcCall& rpc, ScanProgress& progress,
                                           std::string& error) {
  if (progress.lastScannedHash.empty()) return true;
  std::string hashNow;
  if (!fetchBlockHash(rpc, progress.lastScannedHeight, hashNow, error)) {
    error.clear();
    hashNow.clear();
  }
  if (hashNow == progress.lastScannedHash) return true;
  const uint64_t lockHeight = progress.lockHeight;
  progress = ScanProgress{};
  progress.lockHeight = lockHeight;
  progress.nextHeight = lockHeight;
  return true;
}

bool UtxoSpendTracker::scanBlocks(const UtxoRpcCall& rpc, const UtxoOutpoint& lock,
                                  ScanProgress& progress, uint64_t tip, UtxoSpend& spend,
                                  std::string& error) {
  if (progress.nextHeight > tip) return false;
  const uint64_t last = std::min(tip, progress.nextHeight + m_maxBlocksPerCall - 1);
  for (uint64_t height = progress.nextHeight; height <= last; ++height) {
    std::string blockHash;
    if (!fetchBlockHash(rpc, height, blockHash, error)) return false;
    Common::JsonValue block;
    if (!rpc("getblock", "[" + quoted(blockHash) + ",2]", block, error)) return false;
    if (!block.isObject() || !block.contains("tx") || !block("tx").isArray()) {
      error = "getblock returned no transactions for " + blockHash;
      return false;
    }
    const Common::JsonValue& txs = block("tx");
    for (size_t i = 0; i < txs.size(); ++i) {
      if (!transactionSpendsOutpoint(txs[i], lock)) continue;
      UtxoSpend found;
      if (!recordSpendFromJson(txs[i], found)) {
        error = "getblock returned a spending transaction without hex";
        return false;
      }
      found.blockHeight = height;
      found.blockHash = blockHash;
      progress.spendFound = true;
      progress.spend = found;
      progress.lastScannedHeight = height;
      progress.lastScannedHash = blockHash;
      progress.nextHeight = height + 1;
      spend = found;
      spend.confirmations = confirmationsAt(tip, height);
      return true;
    }
    progress.lastScannedHeight = height;
    progress.lastScannedHash = blockHash;
    progress.nextHeight = height + 1;
  }
  return false;
}

bool UtxoSpendTracker::findMempoolSpend(const UtxoRpcCall& rpc, const UtxoOutpoint& lock,
                                        UtxoSpend& spend) {
  // gettxspendingprevout exists from Bitcoin Core 24; other nodes answer with
  // "method not found", which leaves discovery to the block scan.
  Common::JsonValue result;
  std::string error;
  const std::string params = "[[{\"txid\":" + quoted(lock.txid) + ",\"vout\":" +
                             std::to_string(lock.vout) + "}]]";
  if (!rpc("gettxspendingprevout", params, result, error) || !result.isArray() ||
      result.size() != 1)
    return false;
  std::string spendingTxid;
  if (!readString(result[0], "spendingtxid", spendingTxid) || !isTxidHex(spendingTxid))
    return false;
  Common::JsonValue raw;
  if (!rpc("getrawtransaction", "[" + quoted(lowercase(spendingTxid)) + ",false]", raw, error) ||
      !raw.isString())
    return false;
  UtxoSpend pending;
  if (!decodeHex(raw.getString(), pending.rawTx)) return false;
  pending.spendingTxid = lowercase(spendingTxid);
  spend = pending;
  return true;
}

}  // namespace XfgSwap
