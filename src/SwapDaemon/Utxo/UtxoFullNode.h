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

#include "Common/JsonValue.h"
#include "SwapDaemon/ChainClientResult.h"

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace XfgSwap {

// One JSON-RPC round trip to a Bitcoin-family full node. Returns false on a
// transport failure or an RPC error object; `result` is the "result" member.
using UtxoRpcCall = std::function<bool(const std::string& method,
                                       const std::string& paramsJson,
                                       Common::JsonValue& result,
                                       std::string& error)>;

enum class UtxoNetwork { MAINNET, TESTNET, SIGNET, REGTEST };

struct UtxoNetworkParams {
  std::string nodeChainName;  // getblockchaininfo "chain"
  std::string bech32Hrp;      // empty where the chain has no segwit
  uint8_t p2pkhVersion = 0;
  uint8_t p2shVersion = 0;
  uint8_t wifVersion = 0;
};

bool parseUtxoNetwork(const std::string& name, UtxoNetwork& network);
UtxoNetworkParams bitcoinNetworkParams(UtxoNetwork network);
UtxoNetworkParams litecoinNetworkParams(UtxoNetwork network);
UtxoNetworkParams bitcoinCashNetworkParams(UtxoNetwork network);

std::string formatCoinAmount(uint64_t baseUnits);
bool parseCoinAmount(const Common::JsonValue& value, uint64_t& baseUnits);

bool isTxidHex(const std::string& txid);
bool decodeHex(const std::string& hex, std::vector<uint8_t>& bytes);
std::string encodeHex(const std::vector<uint8_t>& bytes);

struct UtxoOutpoint {
  std::string txid;
  uint32_t vout = 0;
  std::string key() const { return txid + ":" + std::to_string(vout); }
};

struct UtxoTxOutput {
  uint64_t value = 0;
  std::vector<uint8_t> scriptPubKey;
};
bool parseTransactionOutputs(const std::vector<uint8_t>& rawTx, std::vector<UtxoTxOutput>& outputs);

struct UtxoUnspentOutput {
  uint64_t value = 0;
  std::string scriptPubKeyHex;
  uint32_t confirmations = 0;
};
enum class UtxoOutputLookup { UNSPENT, NOT_UNSPENT, RPC_FAILED };
UtxoOutputLookup lookupUnspentOutput(const UtxoRpcCall& rpc, const UtxoOutpoint& outpoint,
                                     bool includeMempool, UtxoUnspentOutput& output,
                                     std::string& error);

struct UtxoLockExpectation {
  std::vector<uint8_t> scriptPubKey;
  uint64_t minValue = 0;
  uint32_t minConfirmations = 1;
};
ChainClientResult verifyLockedOutput(const UtxoRpcCall& rpc, const UtxoOutpoint& outpoint,
                                     const UtxoLockExpectation& expectation);

bool fetchChainTip(const UtxoRpcCall& rpc, uint64_t& height, std::string& error);
bool findWalletOutputIndex(const UtxoRpcCall& rpc, const std::string& txid,
                           const std::vector<uint8_t>& scriptPubKey, uint32_t& vout,
                           std::string& error);
bool walletTransactionPays(const UtxoRpcCall& rpc, const std::string& txid,
                           const std::vector<uint8_t>& scriptPubKey, uint64_t value);
bool lookupWalletTxBlockHeight(const UtxoRpcCall& rpc, const std::string& txid,
                               uint64_t& blockHeight, std::string& error);
bool broadcastRawTransaction(const UtxoRpcCall& rpc, const std::vector<uint8_t>& rawTx,
                             std::string& txid, std::string& error);
// Fee for a transaction of `virtualBytes`: estimatesmartfee, never below the
// node's relay and mempool minimums (nor 1 base unit per vbyte), and capped at
// `ceiling` unless the minimum alone exceeds it.
// Fee for `virtualBytes` at `targetRate`, never below `floorRate` and never
// above `ceiling` unless the floor itself demands more (rates per kilobyte).
uint64_t boundedFee(uint64_t floorRate, uint64_t targetRate, uint64_t virtualBytes, uint64_t ceiling);
uint64_t estimateFeeForSize(const UtxoRpcCall& rpc, uint64_t virtualBytes, uint64_t ceiling);

struct UtxoNodeHealth {
  bool ready = false;
  std::string reason;
};
// Ready means: RPC reachable, the node's chain equals `expectedChain`, initial
// block download finished, and a wallet with private keys is loaded (the
// locking role funds from it).
UtxoNodeHealth checkNodeHealth(const UtxoRpcCall& rpc, const std::string& expectedChain);

struct UtxoSpend {
  std::string spendingTxid;
  std::vector<uint8_t> rawTx;
  uint64_t blockHeight = 0;  // 0 while the spend is only in the mempool
  std::string blockHash;
  uint32_t confirmations = 0;
};

// Finds the transaction that spends a lock outpoint from chain data alone, so
// the locking party never depends on the claimer announcing its claim txid.
// Scans forward from the lock's block, remembers its progress per outpoint,
// and rewinds to the lock block when a remembered block hash leaves the main
// chain. A restart rescans from the lock block.
class UtxoSpendTracker {
public:
  explicit UtxoSpendTracker(uint64_t maxBlocksPerCall = 288);

  bool findSpend(const UtxoRpcCall& rpc, const UtxoOutpoint& lock, uint64_t lockHeight,
                 UtxoSpend& spend, std::string& error);

private:
  struct ScanProgress {
    uint64_t lockHeight = 0;
    uint64_t nextHeight = 0;
    uint64_t lastScannedHeight = 0;
    std::string lastScannedHash;
    bool spendFound = false;
    UtxoSpend spend;
  };

  bool confirmRecordedSpend(const UtxoRpcCall& rpc, ScanProgress& progress, uint64_t tip,
                            UtxoSpend& spend, std::string& error);
  bool rewindIfReorganized(const UtxoRpcCall& rpc, ScanProgress& progress, std::string& error);
  bool scanBlocks(const UtxoRpcCall& rpc, const UtxoOutpoint& lock, ScanProgress& progress,
                  uint64_t tip, UtxoSpend& spend, std::string& error);
  bool findMempoolSpend(const UtxoRpcCall& rpc, const UtxoOutpoint& lock, UtxoSpend& spend);

  const uint64_t m_maxBlocksPerCall;
  std::mutex m_mutex;
  std::map<std::string, ScanProgress> m_progress;
};

}  // namespace XfgSwap
