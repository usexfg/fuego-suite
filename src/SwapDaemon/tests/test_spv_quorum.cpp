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

// Remediation plan 2.6: the Electrum client under a server quorum with
// header rules. A dishonest server is outvoted by two honest ones, reorgs
// switch only to more work, fees and inclusion follow the quorum, and the
// trust policy names what is missing.

#include "SwapDaemon/Spv/ElectrumSpvClient.h"
#include "SwapDaemon/Spv/SpvHeaderStore.h"
#include "SwapDaemon/BitcoinCash/HtlcScript.h"
#include "TestElectrumServer.h"

#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

using namespace XfgSwap;

static int g_failures = 0;
static int g_checks = 0;

static void check(bool condition, const std::string& label) {
  ++g_checks;
  if (!condition) {
    ++g_failures;
    std::printf("FAIL: %s\n", label.c_str());
  }
}

static const uint32_t REGTEST_BITS = 0x207fffff;
static const uint32_t FIRST_TIME = 1600000000;

static SpvHeader minedHeader(const std::vector<uint8_t>& prevHash, const std::vector<uint8_t>& merkleRoot,
                             uint32_t time) {
  SpvHeader header;
  header.version = 0x20000000;
  header.prevHash = prevHash;
  header.merkleRoot = merkleRoot;
  header.time = time;
  header.bits = REGTEST_BITS;
  header.nonce = 0;
  while (!header.meetsPoW()) ++header.nonce;
  return header;
}

using Chain = std::vector<SpvHeader>;

// Headers 0..last: the first `keep` from `base`, the rest mined on a branch tagged `branchTag`.
static Chain extend(const Chain& base, size_t keep, size_t last, uint8_t branchTag) {
  Chain chain(base.begin(), base.begin() + static_cast<std::ptrdiff_t>(keep));
  for (size_t height = keep; height <= last; ++height) {
    const std::vector<uint8_t> prev = chain.empty() ? std::vector<uint8_t>(32, 0) : chain.back().hash();
    std::vector<uint8_t> root(32, branchTag);
    root[0] = static_cast<uint8_t>(height);
    chain.push_back(minedHeader(prev, root, FIRST_TIME + static_cast<uint32_t>(height) * 600));
  }
  return chain;
}

static std::string hexOf(const SpvHeader& header) {
  return BchHtlcScript::bytesToHex(header.serialize());
}

// An Electrum server over a header chain it can switch, with scripted fee,
// proof and broadcast answers.
class ChainServer {
public:
  explicit ChainServer(Chain chain) : m_chain(std::move(chain)) {
    m_server.setHandler([this](const std::string& method, const std::string& params) { return answer(method, params); });
  }
  uint16_t start(const char* address = "127.0.0.1") { return m_port = m_server.start(address); }
  std::string endpoint(const char* host = "127.0.0.1") const { return std::string(host) + ":" + std::to_string(m_port); }
  void setChain(Chain chain) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_chain = std::move(chain);
  }
  void setFees(const std::string& relay, const std::string& estimate) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_relayFee = relay;
    m_estimateFee = estimate;
  }
  void proveAt(const std::string& txid, uint64_t height) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_proofs[txid] = height;
  }
  void acceptBroadcasts(bool accept) { m_acceptBroadcast = accept; }
  bool sawBroadcast() const { return m_server.receivedMethod("blockchain.transaction.broadcast"); }
  void stop() { m_server.stop(); }

private:
  std::string answer(const std::string& method, const std::string& paramsJson) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const Common::JsonValue params = Common::JsonValue::fromString(paramsJson);
    if (method == "server.version") return R"(["ChainServer","1.4"])";
    if (method == "blockchain.headers.subscribe")
      return "{\"height\":" + std::to_string(m_chain.size() - 1) + ",\"hex\":\"" + hexOf(m_chain.back()) + "\"}";
    if (method == "blockchain.block.headers") {
      const uint64_t start = static_cast<uint64_t>(params[0].getInteger());
      const uint64_t count = static_cast<uint64_t>(params[1].getInteger());
      std::string hex;
      for (uint64_t h = start; h < start + count && h < m_chain.size(); ++h) hex += hexOf(m_chain[h]);
      return "{\"headers\":\"" + hex + "\",\"count\":" + std::to_string(hex.size() / 160) + "}";
    }
    if (method == "blockchain.transaction.get_merkle") {
      auto it = m_proofs.find(params[0].getString());
      if (it == m_proofs.end()) return "null";
      return "{\"block_height\":" + std::to_string(it->second) + ",\"merkle\":[],\"pos\":0}";
    }
    if (method == "blockchain.relayfee") return m_relayFee;
    if (method == "blockchain.estimatefee") return m_estimateFee;
    if (method == "blockchain.transaction.broadcast") {
      if (!m_acceptBroadcast) return "null";
      const std::vector<uint8_t> raw = BchHtlcScript::hexToBytes(params[0].getString());
      return "\"" + ElectrumSpvClient::computeTransactionId(raw) + "\"";
    }
    return "null";
  }

  TestElectrumServer m_server;
  std::mutex m_mutex;
  Chain m_chain;
  std::map<std::string, uint64_t> m_proofs;
  std::string m_relayFee = "0.00001";
  std::string m_estimateFee = "0.0002";
  bool m_acceptBroadcast = true;
  uint16_t m_port = 0;
};

static std::string tipHash(const ElectrumSpvClient& client) {
  uint64_t height = 0;
  std::string hash;
  client.store().bestTip(height, hash);
  return hash;
}

static ElectrumSpvClient regtestClient(const std::vector<std::string>& servers, const Chain& chain) {
  return ElectrumSpvClient(servers, 2, 1, chain[1].hashDisplay(), SpvChainRules::bitcoinRegtest());
}

// A transaction alone in its block: the Merkle root is its txid.
static std::string loneTxidOf(const SpvHeader& header) {
  return BchHtlcScript::bytesToHex(std::vector<uint8_t>(header.merkleRoot.rbegin(), header.merkleRoot.rend()));
}

static void testDishonestServerOutvoted() {
  const Chain honest = extend({}, 0, 30, 0xaa);
  const Chain forged = extend(honest, 3, 40, 0xee);
  ChainServer liar(forged), honestB(honest), honestC(honest);
  liar.start();
  honestB.start();
  honestC.start();
  ElectrumSpvClient client = regtestClient({liar.endpoint(), honestB.endpoint(), honestC.endpoint()}, honest);

  check(client.syncHeaders(), "sync succeeds with a dishonest first server");
  check(tipHash(client) == honest.back().hashDisplay(), "the honest chain wins; the forged branch is undone");
  uint64_t tip = 0;
  check(client.getTipHeight(tip) && tip == 30, "quorum tip is the honest median");

  const std::string genuine = loneTxidOf(honest[5]);
  honestB.proveAt(genuine, 5);
  honestC.proveAt(genuine, 5);
  SpvTxInclusion inclusion;
  check(client.verifyTxInclusion(genuine, inclusion) && inclusion.included && inclusion.blockHeight == 5 &&
            inclusion.depth == 26,
        "two honest proofs include a transaction the liar withholds");
  const std::string forgedTx = loneTxidOf(forged[5]);
  liar.proveAt(forgedTx, 5);
  check(client.verifyTxInclusion(forgedTx, inclusion) && !inclusion.included,
        "the liar's proof alone includes nothing");
  liar.proveAt(genuine, 6);
  check(client.verifyTxInclusion(genuine, inclusion) && inclusion.included && inclusion.blockHeight == 5,
        "a proof at another height does not move the quorum's answer");
  liar.stop();
  honestB.stop();
  honestC.stop();
}

static void testReorgFollowsMostWork() {
  const Chain first = extend({}, 0, 30, 0xaa);
  ChainServer a(first), b(first), c(first);
  a.start();
  b.start();
  c.start();
  ElectrumSpvClient client = regtestClient({a.endpoint(), b.endpoint(), c.endpoint()}, first);
  check(client.syncHeaders() && tipHash(client) == first.back().hashDisplay(), "initial sync");

  const Chain longer = extend(first, 26, 33, 0xbb);
  for (ChainServer* server : {&a, &b, &c}) server->setChain(longer);
  check(client.syncHeaders(), "sync across a four-block reorg");
  check(tipHash(client) == longer.back().hashDisplay(), "the longer branch replaces the old tip");
  SpvHeader header;
  check(client.store().headerAtHeight(27, header) && header.hashDisplay() == longer[27].hashDisplay(),
        "headers above the fork come from the new branch");

  const Chain shorter = extend(longer, 29, 31, 0xcc);
  for (ChainServer* server : {&a, &b, &c}) server->setChain(shorter);
  check(!client.syncHeaders(), "a branch with less work is refused");
  check(tipHash(client) == longer.back().hashDisplay(), "the refused branch leaves the stored chain untouched");

  const Chain deep = extend(first, 2, 34, 0xdd);
  for (ChainServer* server : {&a, &b, &c}) server->setChain(deep);
  check(client.syncHeaders() && tipHash(client) == deep.back().hashDisplay(),
        "a reorg back to the checkpoint is followed when it has more work");
  a.stop();
  b.stop();
  c.stop();
}

static void testFeesAndBroadcastFollowTheQuorum() {
  const Chain chain = extend({}, 0, 12, 0xaa);
  ChainServer a(chain), b(chain), c(chain);
  a.start();
  b.start();
  c.start();
  a.setFees("5.0", "0.000001");
  b.setFees("0.00001", "0.0002");
  c.setFees("0.00002", "0.00025");
  ElectrumSpvClient client = regtestClient({a.endpoint(), b.endpoint(), c.endpoint()}, chain);
  uint64_t floor = 0, target = 0;
  check(client.feeRatesPerKilobyte(floor, target) && floor == 2000 && target == 20000,
        "fee rates are the medians; one server can neither inflate nor starve them");

  a.acceptBroadcasts(false);
  const std::vector<uint8_t> raw = BchHtlcScript::hexToBytes(
      "0200000001" + std::string(64, '3') + "00000000" "00" "ffffffff" "01" "e803000000000000" "00" "00000000");
  std::string txid;
  check(client.syncHeaders() && client.broadcastTx(raw, txid) &&
            txid == ElectrumSpvClient::computeTransactionId(raw),
        "a broadcast withheld by one server is still accepted");
  check(a.sawBroadcast() && b.sawBroadcast() && c.sawBroadcast(), "every server receives the broadcast");
  a.stop();
  b.stop();
  c.stop();
}

static void testTrustPolicy() {
  const Chain chain = extend({}, 0, 12, 0xaa);
  const std::string checkpoint = chain[1].hashDisplay();
  const SpvChainRules regtest = SpvChainRules::bitcoinRegtest();
  const std::vector<std::string> sameHost = {"127.0.0.1:1", "127.0.0.1:2", "127.0.0.1:3"};
  check(ElectrumSpvClient({"127.0.0.1:1", "127.0.0.2:1"}, 2, 1, checkpoint, regtest).newSwapTrustError() ==
            "SPV needs at least 3 Electrum servers",
        "two servers are not a quorum");
  check(ElectrumSpvClient(sameHost, 2, 1, checkpoint, regtest).newSwapTrustError() ==
            "SPV servers must be on distinct hosts",
        "three ports on one host are one operator");
  const std::vector<std::string> distinct = {"127.0.0.1:1", "127.0.0.2:1", "127.0.0.3:1"};
  check(ElectrumSpvClient(distinct, 1, 1, checkpoint, regtest).newSwapTrustError() ==
            "spv_min_servers must be at least 2 and at most the server count",
        "a quorum of one is refused");
  check(ElectrumSpvClient(distinct, 2, 0, checkpoint, regtest).newSwapTrustError() == "SPV needs a checkpoint",
        "no checkpoint, no trust");
  check(ElectrumSpvClient(distinct, 2, 1, checkpoint).newSwapTrustError() == "SPV has no header rules for this chain",
        "no header rules, no trust");
  check(ElectrumSpvClient(distinct, 2, 800351, checkpoint, SpvChainRules::bitcoinMainnet())
                .newSwapTrustError()
                .find("checkpoint must") == std::string::npos &&
            ElectrumSpvClient(distinct, 2, 2352616, checkpoint, SpvChainRules::litecoinMainnet())
                .newSwapTrustError()
                .find("checkpoint must") == std::string::npos,
        "BTC and LTC checkpoints may sit anywhere in a retarget period");
  check(ElectrumSpvClient(distinct, 2, 661646, checkpoint, SpvChainRules::bitcoinCashMainnet())
            .newSwapTrustError()
            .find("ASERT anchor") != std::string::npos,
        "a BCH checkpoint below the ASERT anchor is refused");

#ifdef __linux__
  ChainServer a(chain), b(chain), c(chain);
  a.start("127.0.0.1");
  b.start("127.0.0.2");
  c.start("127.0.0.3");
  ElectrumSpvClient trusted({a.endpoint("127.0.0.1"), b.endpoint("127.0.0.2"), c.endpoint("127.0.0.3")}, 2, 1,
                            checkpoint, regtest);
  check(trusted.newSwapTrustError().empty(), "three distinct servers, rules and a synced checkpoint are trusted");
  a.stop();
  b.stop();
  c.stop();
#endif
}

static void testCheckpointHistoryIsAuthenticated() {
  const Chain honest = extend({}, 0, 30, 0xaa);
  const Chain forged = extend(honest, 8, 30, 0xee);
  ChainServer liar(forged), b(honest), c(honest);
  liar.start();
  b.start();
  c.start();
  ElectrumSpvClient client({liar.endpoint(), b.endpoint(), c.endpoint()}, 2, 15, honest[15].hashDisplay(),
                           SpvChainRules::bitcoinRegtest());
  check(client.syncHeaders(), "sync from a checkpoint inside the chain");
  SpvHeader header;
  check(client.store().headerAtHeight(5, header) && header.hashDisplay() == honest[5].hashDisplay() &&
            !client.store().headerAtHeight(4, header),
        "the median-time span before the checkpoint comes from servers whose headers link to it");
  check(tipHash(client) == honest.back().hashDisplay(), "sync continues from the checkpoint to the tip");

  SpvHeaderStore store;
  store.anchor(15, honest[15].hashDisplay());
  Chain unlinked(honest.begin() + 5, honest.begin() + 16);
  unlinked[3] = forged[8];
  check(!store.addCheckpointHistory(unlinked) && store.lastRejection() == "checkpoint history does not link to the checkpoint",
        "a history that does not hash-link to the checkpoint is refused");
  check(!store.addCheckpointHistory(Chain(honest.begin() + 5, honest.begin() + 15)),
        "a history not ending at the checkpoint is refused");
  check(store.addCheckpointHistory(Chain(honest.begin() + 5, honest.begin() + 16)) && !store.rewindTo(14),
        "the stored chain never rewinds below its checkpoint");
  liar.stop();
  b.stop();
  c.stop();
}

static void testStoreRefusesLowDifficultyUnderMainnetRules() {
  SpvHeader genesis;
  genesis.version = 1;
  genesis.prevHash = std::vector<uint8_t>(32, 0);
  genesis.merkleRoot = BchHtlcScript::hexToBytes("3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a");
  genesis.time = 1231006505;
  genesis.bits = 0x1d00ffff;
  genesis.nonce = 2083236893;
  SpvHeaderStore store;
  store.anchor(0, genesis.hashDisplay());
  store.setRules(SpvChainRules::bitcoinMainnet());
  check(genesis.hashDisplay() == "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f" &&
            store.addCheckpointHeader(genesis),
        "mainnet store anchored at the genesis block");
  const SpvHeader cheap = minedHeader(genesis.hash(), std::vector<uint8_t>(32, 1), genesis.time + 600);
  check(!store.addHeader(cheap) && store.lastRejection() == "target above powLimit",
        "a regtest-difficulty header cannot extend a mainnet chain");
  SpvHeader unmined = cheap;
  unmined.bits = 0x1d00ffff;
  check(!store.addHeader(unmined) && store.lastRejection() == "insufficient proof of work",
        "a mainnet-difficulty header without the work is refused");
  uint64_t height = 1;
  std::string hash;
  check(store.bestTip(height, hash) && height == 0, "the refused headers never reach the tip");
}

static void testQuorumCountsConfiguredServers() {
  const Chain honest = extend({}, 0, 30, 0xaa);
  const Chain forged = extend(honest, 3, 40, 0xee);
  ChainServer liarA(forged), liarB(forged), honestA(honest);
  liarA.start();
  liarB.start();
  honestA.start();
  const std::string unreachableA = "127.0.0.1:1";
  const std::string unreachableB = "127.0.0.1:2";
  const std::string unreachableC = "127.0.0.1:3";

  {
    ElectrumSpvClient liarsOnly =
        regtestClient({liarA.endpoint(), liarB.endpoint(), unreachableA, unreachableB, unreachableC}, honest);
    check(!liarsOnly.syncHeaders() && tipHash(liarsOnly) != forged.back().hashDisplay(),
          "two reachable liars of five configured servers are not a quorum");
  }
  {
    ElectrumSpvClient liarsAndOneHonest =
        regtestClient({liarA.endpoint(), liarB.endpoint(), honestA.endpoint(), unreachableA, unreachableB}, honest);
    liarsAndOneHonest.syncHeaders();
    check(tipHash(liarsAndOneHonest) != forged.back().hashDisplay(),
          "two liars outvoting one honest server of five configured do not move the tip");
    const std::string forgedTx = loneTxidOf(forged[5]);
    liarA.proveAt(forgedTx, 5);
    liarB.proveAt(forgedTx, 5);
    SpvTxInclusion inclusion;
    check(!liarsAndOneHonest.verifyTxInclusion(forgedTx, inclusion) || !inclusion.included,
          "two liars of five configured servers cannot prove a transaction");
  }
  liarA.stop();
  liarB.stop();
  honestA.stop();
}

int main() {
  testDishonestServerOutvoted();
  testQuorumCountsConfiguredServers();
  testReorgFollowsMostWork();
  testFeesAndBroadcastFollowTheQuorum();
  testTrustPolicy();
  testCheckpointHistoryIsAuthenticated();
  testStoreRefusesLowDifficultyUnderMainnetRules();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
