// Copyright (c) 2017-2026 Fuego Developers
//
// ElectrumConnection, ElectrumSpvClient, and TestElectrumServer tests.

#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "SwapDaemon/Spv/ElectrumConnection.h"
#include "SwapDaemon/Spv/ElectrumSpvClient.h"
#include "SwapDaemon/Spv/SpvHeader.h"
#include "SwapDaemon/Spv/SpvHeaderStore.h"
#include "SwapDaemon/Spv/SpvMerkle.h"
#include "SwapDaemon/BitcoinCash/HtlcScript.h"
#include "TestElectrumServer.h"

using namespace XfgSwap;

// =============================================================================
// Helpers: synthetic regtest-difficulty headers (bits = 0x207fffff)
// =============================================================================

static SpvHeader makeHeader(uint32_t version,
                            const std::vector<uint8_t>& prevHash,
                            const std::vector<uint8_t>& merkleRoot,
                            uint32_t time,
                            uint32_t nonce) {
  SpvHeader h;
  h.version = version;
  h.prevHash = prevHash;
  h.merkleRoot = merkleRoot;
  h.time = time;
  h.bits = 0x207fffff;
  h.nonce = nonce;
  while (!h.meetsPoW()) {
    ++h.nonce;
  }
  return h;
}

static std::vector<uint8_t> zeroHash() {
  return std::vector<uint8_t>(32, 0);
}

static std::vector<uint8_t> makeMerkle(uint8_t fill) {
  return std::vector<uint8_t>(32, fill);
}

// Build a synthetic chain of `count` headers starting from genesis.
static std::vector<SpvHeader> buildChain(size_t count) {
  std::vector<SpvHeader> chain;
  SpvHeader genesis = makeHeader(1, zeroHash(), makeMerkle(0xaa), 1000000000, 0);
  chain.push_back(genesis);

  for (size_t i = 1; i < count; ++i) {
    SpvHeader h = makeHeader(
        1, chain.back().hash(), makeMerkle(static_cast<uint8_t>(i)),
        1000000000 + static_cast<uint32_t>(i), static_cast<uint32_t>(i));
    chain.push_back(h);
  }
  return chain;
}

// =============================================================================
// ElectrumConnection tests (existing)
// =============================================================================

static void test_connection_basic_call() {
  TestElectrumServer server;
  server.setCannedResponse("server.version", R"(["ElectrumX 1.0","1.4"])");
  uint16_t port = server.start();

  ElectrumConnection conn;
  assert(conn.connect("127.0.0.1", port));
  assert(conn.isConnected());

  std::string result = conn.call("server.version", R"(["2.0.3","1.4"])");
  assert(!result.empty());
  assert(result.find("ElectrumX") != std::string::npos);

  conn.disconnect();
  server.stop();
}

static void test_connection_multiple_calls() {
  TestElectrumServer server;
  server.setCannedResponse("server.version", R"(["TestServer","1.4"])");
  server.setCannedResponse("blockchain.headers.subscribe", R"({"height":800000})");
  uint16_t port = server.start();

  ElectrumConnection conn;
  assert(conn.connect("127.0.0.1", port));

  std::string r1 = conn.call("server.version", R"(["2.0.3","1.4"])");
  assert(!r1.empty());

  std::string r2 = conn.call("blockchain.headers.subscribe", R"([])");
  assert(!r2.empty());
  assert(r2.find("800000") != std::string::npos);

  conn.disconnect();
  server.stop();
}

static void test_connection_custom_handler() {
  TestElectrumServer server;
  server.setHandler([](const std::string& method, const std::string& paramsJson) -> std::string {
    if (method == "server.ping") {
      return "null";
    }
    return R"({"error":"unknown method"})";
  });
  uint16_t port = server.start();

  ElectrumConnection conn;
  assert(conn.connect("127.0.0.1", port));

  std::string result = conn.call("server.ping", "[]");
  assert(result == "null");

  conn.disconnect();
  server.stop();
}

static void test_connection_received_method_tracking() {
  TestElectrumServer server;
  server.setCannedResponse("server.version", R"(["X","1.4"])");
  server.setCannedResponse("blockchain.scripthash.get_balance", R"({"confirmed":1000,"unconfirmed":0})");
  uint16_t port = server.start();

  assert(!server.receivedMethod("server.version"));

  ElectrumConnection conn;
  assert(conn.connect("127.0.0.1", port));

  conn.call("server.version", R"(["2.0","1.4"])");
  assert(server.receivedMethod("server.version"));
  assert(!server.receivedMethod("blockchain.scripthash.get_balance"));

  conn.call("blockchain.scripthash.get_balance", R"(["abc123"])");
  assert(server.receivedMethod("blockchain.scripthash.get_balance"));

  conn.disconnect();
  server.stop();
}

static void test_connection_nonexistent_server() {
  ElectrumConnection conn;
  assert(!conn.connect("127.0.0.1", 1));
}

static void test_connection_disconnected_call() {
  ElectrumConnection conn;
  std::string result = conn.call("server.version", "[]");
  assert(result.empty());
}

// =============================================================================
// ElectrumSpvClient tests
// =============================================================================

// Test: syncHeaders populates store and getTipHeight returns correct height
static void test_spv_sync_headers() {
  std::vector<SpvHeader> chain = buildChain(5);

  // Build canned responses
  std::string tipHex = BchHtlcScript::bytesToHex(chain.back().serialize());

  std::string allHeadersHex;
  for (const auto& h : chain) {
    allHeadersHex += BchHtlcScript::bytesToHex(h.serialize());
  }

  std::string lastHash = chain.back().hashDisplay();

  TestElectrumServer server;
  server.setCannedResponse("server.version", R"(["TestServer","1.4"])");
  server.setCannedResponse("blockchain.headers.subscribe",
      R"({"height":4,"hex":")" + tipHex + R"("})");
  server.setCannedResponse("blockchain.block.headers",
      R"({"headers":")" + allHeadersHex + R"(","max":")" + lastHash + R"("})");

  uint16_t port = server.start();
  std::string addr = "127.0.0.1:" + std::to_string(port);

  ElectrumSpvClient client({addr}, 1, 0,
      "0000000000000000000000000000000000000000000000000000000000000000");

  assert(client.protocolName() == "electrum");
  assert(client.syncHeaders());

  uint64_t height = 0;
  assert(client.getTipHeight(height));
  assert(height == 4);

  // Verify store contains all headers
  const SpvHeaderStore& store = client.store();
  SpvHeader got;
  assert(store.headerAtHeight(0, got));
  assert(got.hashDisplay() == chain[0].hashDisplay());
  assert(store.headerAtHeight(2, got));
  assert(got.hashDisplay() == chain[2].hashDisplay());
  assert(store.headerAtHeight(4, got));
  assert(got.hashDisplay() == chain[4].hashDisplay());

  server.stop();
}

// Test: getTipHeight returns false on empty store
static void test_spv_empty_store_tip() {
  ElectrumSpvClient client({}, 1, 0,
      "0000000000000000000000000000000000000000000000000000000000000000");

  uint64_t height = 0;
  assert(!client.getTipHeight(height));
}

// Test: syncHeaders with more headers than a single batch
// (tests the batching loop, though with a small chain it still fits in one batch)
static void test_spv_sync_larger_chain() {
  std::vector<SpvHeader> chain = buildChain(10);

  std::string tipHex = BchHtlcScript::bytesToHex(chain.back().serialize());
  std::string allHeadersHex;
  for (const auto& h : chain) {
    allHeadersHex += BchHtlcScript::bytesToHex(h.serialize());
  }
  std::string lastHash = chain.back().hashDisplay();

  TestElectrumServer server;
  server.setCannedResponse("server.version", R"(["TestServer","1.4"])");
  server.setCannedResponse("blockchain.headers.subscribe",
      R"({"height":9,"hex":")" + tipHex + R"("})");
  server.setCannedResponse("blockchain.block.headers",
      R"({"headers":")" + allHeadersHex + R"(","max":")" + lastHash + R"("})");

  uint16_t port = server.start();
  std::string addr = "127.0.0.1:" + std::to_string(port);

  ElectrumSpvClient client({addr}, 1, 0,
      "0000000000000000000000000000000000000000000000000000000000000000");

  assert(client.syncHeaders());

  uint64_t height = 0;
  assert(client.getTipHeight(height));
  assert(height == 9);

  server.stop();
}

// Test: connectToServers fails when minServers > available servers
static void test_spv_insufficient_servers() {
  ElectrumSpvClient client({"127.0.0.1:1"}, 2, 0,
      "0000000000000000000000000000000000000000000000000000000000000000");

  // syncHeaders should fail because we can't connect to 2 servers
  assert(!client.syncHeaders());
}

// Test: store state is fully consistent after sync (implicitly tests
// the "already synced" short-circuit path — storeTip >= tipHeight)
static void test_spv_store_consistency() {
  std::vector<SpvHeader> chain = buildChain(4);

  std::string tipHex = BchHtlcScript::bytesToHex(chain.back().serialize());
  std::string allHeadersHex;
  for (const auto& h : chain) {
    allHeadersHex += BchHtlcScript::bytesToHex(h.serialize());
  }
  std::string lastHash = chain.back().hashDisplay();

  TestElectrumServer server;
  server.setCannedResponse("server.version", R"(["TestServer","1.4"])");
  server.setCannedResponse("blockchain.headers.subscribe",
      R"({"height":3,"hex":")" + tipHex + R"("})");
  server.setCannedResponse("blockchain.block.headers",
      R"({"headers":")" + allHeadersHex + R"(","max":")" + lastHash + R"("})");

  uint16_t port = server.start();
  std::string addr = "127.0.0.1:" + std::to_string(port);

  ElectrumSpvClient client({addr}, 1, 0,
      "0000000000000000000000000000000000000000000000000000000000000000");

  assert(client.syncHeaders());

  // Verify every height's header hash matches the chain
  const SpvHeaderStore& store = client.store();
  for (size_t i = 0; i < chain.size(); ++i) {
    SpvHeader got;
    assert(store.headerAtHeight(i, got));
    assert(got.hashDisplay() == chain[i].hashDisplay());
  }

  // bestTip should be at height 3
  uint64_t tipH;
  std::string tipHash;
  assert(store.bestTip(tipH, tipHash));
  assert(tipH == 3);
  assert(tipHash == chain[3].hashDisplay());

  // depthOf(0) == 4 (4 headers in chain, 0-indexed)
  assert(store.depthOf(0) == 4);

  server.stop();
}

// Build a chain of `count` headers, with a specific merkle root at `targetHeight`.
// All other headers use makeMerkle(i). Returns the chain and the preimage bytes
// used for the target merkle root (caller can use to build a valid proof).
static std::pair<std::vector<SpvHeader>, std::vector<uint8_t>> buildChainWithMerkleAt(
    size_t count, size_t targetHeight, const std::vector<uint8_t>& merkleRoot) {
  std::vector<SpvHeader> chain;
  SpvHeader genesis = makeHeader(1, zeroHash(), makeMerkle(0xaa), 1000000000, 0);
  chain.push_back(genesis);

  for (size_t i = 1; i < count; ++i) {
    std::vector<uint8_t> mr = (i == targetHeight) ? merkleRoot : makeMerkle(static_cast<uint8_t>(i));
    SpvHeader h = makeHeader(
        1, chain.back().hash(), mr,
        1000000000 + static_cast<uint32_t>(i), static_cast<uint32_t>(i));
    chain.push_back(h);
  }
  return {chain, {}};
}

// Test: verifyTxInclusion with valid Merkle proof
static void test_spv_verify_tx_inclusion_valid() {
  // Two deterministic txids
  std::string txid0 = "0000000000000000000000000000000000000000000000000000000000000001";
  std::string txid1 = "0000000000000000000000000000000000000000000000000000000000000002";

  // Compute the merkle root for a 2-tx block with txid0 at position 0
  // Branch = [txid1], pos = 0
  std::string expectedRoot = SpvMerkle::computeRootHexDisplay(txid0, {txid1}, 0);

  // Convert expectedRoot (display hex) to internal LE bytes for the header
  std::vector<uint8_t> rootBE = BchHtlcScript::hexToBytes(expectedRoot);
  std::vector<uint8_t> rootLE(rootBE.rbegin(), rootBE.rend());

  // Build a 4-header chain; header at height 2 gets our computed merkle root
  auto [chain, _] = buildChainWithMerkleAt(4, 2, rootLE);

  // Serialize the chain for the Electrum server
  std::string tipHex = BchHtlcScript::bytesToHex(chain.back().serialize());
  std::string allHeadersHex;
  for (const auto& h : chain) {
    allHeadersHex += BchHtlcScript::bytesToHex(h.serialize());
  }
  std::string lastHash = chain.back().hashDisplay();

  // Merkle proof response: branch=[txid1], pos=0, block_height=2
  std::string merkleResponse = R"({"block_height":2,"merkle":[")" + txid1 + R"("],"pos":0})";

  TestElectrumServer server;
  server.setCannedResponse("server.version", R"(["TestServer","1.4"])");
  server.setCannedResponse("blockchain.headers.subscribe",
      R"({"height":3,"hex":")" + tipHex + R"("})");
  server.setCannedResponse("blockchain.block.headers",
      R"({"headers":")" + allHeadersHex + R"(","max":")" + lastHash + R"("})");
  server.setCannedResponse("blockchain.transaction.get_merkle", merkleResponse);

  uint16_t port = server.start();
  std::string addr = "127.0.0.1:" + std::to_string(port);

  ElectrumSpvClient client({addr}, 1, 0,
      "0000000000000000000000000000000000000000000000000000000000000000");

  assert(client.syncHeaders());

  SpvTxInclusion inc;
  assert(client.verifyTxInclusion(txid0, inc));
  assert(inc.included);
  assert(inc.merkleVerified);
  assert(inc.blockHeight == 2);
  assert(inc.depth == 2);  // tip=3, height=2 => depth = 3-2+1 = 2

  server.stop();
}

// Test: verifyTxInclusion with tampered proof
static void test_spv_verify_tx_inclusion_tampered() {
  std::string txid0 = "0000000000000000000000000000000000000000000000000000000000000001";
  std::string txid1 = "0000000000000000000000000000000000000000000000000000000000000002";
  std::string fakeBranch = "00000000000000000000000000000000000000000000000000000000000000ff";

  // Compute the real merkle root for txid0 with valid branch
  std::string expectedRoot = SpvMerkle::computeRootHexDisplay(txid0, {txid1}, 0);
  std::vector<uint8_t> rootBE = BchHtlcScript::hexToBytes(expectedRoot);
  std::vector<uint8_t> rootLE(rootBE.rbegin(), rootBE.rend());

  auto [chain, _] = buildChainWithMerkleAt(4, 2, rootLE);

  std::string tipHex = BchHtlcScript::bytesToHex(chain.back().serialize());
  std::string allHeadersHex;
  for (const auto& h : chain) {
    allHeadersHex += BchHtlcScript::bytesToHex(h.serialize());
  }
  std::string lastHash = chain.back().hashDisplay();

  // Tampered proof: wrong branch hash
  std::string merkleResponse = R"({"block_height":2,"merkle":[")" + fakeBranch + R"("],"pos":0})";

  TestElectrumServer server;
  server.setCannedResponse("server.version", R"(["TestServer","1.4"])");
  server.setCannedResponse("blockchain.headers.subscribe",
      R"({"height":3,"hex":")" + tipHex + R"("})");
  server.setCannedResponse("blockchain.block.headers",
      R"({"headers":")" + allHeadersHex + R"(","max":")" + lastHash + R"("})");
  server.setCannedResponse("blockchain.transaction.get_merkle", merkleResponse);

  uint16_t port = server.start();
  std::string addr = "127.0.0.1:" + std::to_string(port);

  ElectrumSpvClient client({addr}, 1, 0,
      "0000000000000000000000000000000000000000000000000000000000000000");

  assert(client.syncHeaders());

  SpvTxInclusion inc;
  assert(client.verifyTxInclusion(txid0, inc));
  assert(!inc.included);
  assert(!inc.merkleVerified);

  server.stop();
}

// Test: verifyTxInclusion fails on empty store
static void test_spv_verify_tx_empty_store() {
  ElectrumSpvClient client({}, 1, 0,
      "0000000000000000000000000000000000000000000000000000000000000000");

  SpvTxInclusion inc;
  assert(!client.verifyTxInclusion("abc123", inc));
  assert(!inc.included);
}

// =============================================================================
// Helpers for building test raw transactions
// =============================================================================

// Build a minimal non-segwit Bitcoin transaction.
// Inputs: vector of {prevoutHash (32 bytes), prevoutIndex}
// Outputs: vector of scriptPubKey
static std::vector<uint8_t> buildTestRawTx(
    const std::vector<std::pair<std::vector<uint8_t>, uint32_t>>& inputs,
    const std::vector<std::vector<uint8_t>>& outputs) {
  std::vector<uint8_t> tx;

  // version 2 LE
  tx.insert(tx.end(), {0x02, 0x00, 0x00, 0x00});

  // input count
  tx.push_back(static_cast<uint8_t>(inputs.size()));

  for (const auto& [prevHash, prevIdx] : inputs) {
    // prevout hash (32 bytes)
    tx.insert(tx.end(), prevHash.begin(), prevHash.end());
    // prevout index (4 bytes LE)
    tx.push_back(static_cast<uint8_t>(prevIdx & 0xFF));
    tx.push_back(static_cast<uint8_t>((prevIdx >> 8) & 0xFF));
    tx.push_back(static_cast<uint8_t>((prevIdx >> 16) & 0xFF));
    tx.push_back(static_cast<uint8_t>((prevIdx >> 24) & 0xFF));
    // scriptSig: 1 byte push of 0x41 (arbitrary)
    tx.push_back(0x01);
    tx.push_back(0x41);
    // sequence
    tx.insert(tx.end(), {0xFF, 0xFF, 0xFF, 0xFF});
  }

  // output count
  tx.push_back(static_cast<uint8_t>(outputs.size()));

  for (const auto& spk : outputs) {
    // value: 1 BTC = 100000000 sat
    uint64_t value = 100000000;
    for (int i = 0; i < 8; ++i) {
      tx.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFF));
    }
    // scriptPubKey length
    tx.push_back(static_cast<uint8_t>(spk.size()));
    tx.insert(tx.end(), spk.begin(), spk.end());
  }

  // locktime
  tx.insert(tx.end(), {0x00, 0x00, 0x00, 0x00});

  return tx;
}

// Build a P2PKH scriptPubKey from a 20-byte pubkey hash.
static std::vector<uint8_t> buildP2pkhSpk(const std::vector<uint8_t>& hash) {
  std::vector<uint8_t> spk;
  spk.insert(spk.end(), {0x76, 0xA9});  // OP_DUP OP_HASH160
  spk.push_back(0x14);                    // push 20 bytes
  spk.insert(spk.end(), hash.begin(), hash.end());
  spk.insert(spk.end(), {0x88, 0xAC});  // OP_EQUALVERIFY OP_CHECKSIG
  return spk;
}

// Compute txid (display hex) from raw tx bytes.
static std::string computeTxid(const std::vector<uint8_t>& rawTx) {
  std::vector<uint8_t> hash = BchHtlcScript::doubleSha256(rawTx);
  std::vector<uint8_t> hashBE(hash.rbegin(), hash.rend());
  return BchHtlcScript::bytesToHex(hashBE);
}

// =============================================================================
// Task 8 tests: getRawTx + findSpend
// =============================================================================

// Test: getRawTx returns raw bytes
static void test_spv_get_raw_tx() {
  TestElectrumServer server;
  server.setCannedResponse("server.version", R"(["TestServer","1.4"])");
  uint16_t port = server.start();
  std::string addr = "127.0.0.1:" + std::to_string(port);

  ElectrumSpvClient client({addr}, 1, 0,
      "0000000000000000000000000000000000000000000000000000000000000000");

  // Build a known funding tx
  std::vector<uint8_t> pkh(20, 0xAA);
  std::vector<uint8_t> spk = buildP2pkhSpk(pkh);
  std::vector<uint8_t> zeroHash(32, 0);
  std::vector<uint8_t> fundingTx = buildTestRawTx({{zeroHash, 0xFFFFFFFF}}, {spk});
  std::string fundingHex = BchHtlcScript::bytesToHex(fundingTx);

  std::string fundingTxid = computeTxid(fundingTx);

  // Override handler for this test: only serve blockchain.transaction.get
  server.setHandler([&](const std::string& method, const std::string& paramsJson) -> std::string {
    if (method == "server.version") {
      return R"(["TestServer","1.4"])";
    }
    if (method == "blockchain.transaction.get") {
      Common::JsonValue params = Common::JsonValue::fromString(paramsJson);
      std::string reqTxid = params[0].getString();
      if (reqTxid == fundingTxid) {
        return "\"" + fundingHex + "\"";
      }
    }
    return "null";
  });

  std::vector<uint8_t> rawTx;
  assert(client.getRawTx(fundingTxid, rawTx));
  assert(!rawTx.empty());
  assert(rawTx == fundingTx);

  // Non-existent txid returns false
  assert(!client.getRawTx("0000000000000000000000000000000000000000000000000000000000000099", rawTx));

  server.stop();
}

// Test: findSpend finds the spending transaction
static void test_spv_find_spend() {
  // Build a funding tx with a known P2PKH output
  std::vector<uint8_t> pkh(20, 0xBB);
  std::vector<uint8_t> spk = buildP2pkhSpk(pkh);
  std::vector<uint8_t> zeroHash(32, 0);
  std::vector<uint8_t> fundingTx = buildTestRawTx({{zeroHash, 0xFFFFFFFF}}, {spk});
  std::string fundingHex = BchHtlcScript::bytesToHex(fundingTx);
  std::string fundingTxid = computeTxid(fundingTx);

  // Build a spending tx: input spends funding tx output #0
  std::vector<uint8_t> fundingTxidInternal = BchHtlcScript::hexToBytes(fundingTxid);
  std::reverse(fundingTxidInternal.begin(), fundingTxidInternal.end());  // to LE

  std::vector<uint8_t> destPk(20, 0xCC);
  std::vector<uint8_t> destSpk = buildP2pkhSpk(destPk);
  std::vector<uint8_t> spendingTx = buildTestRawTx({{fundingTxidInternal, 0}}, {destSpk});
  std::string spendingHex = BchHtlcScript::bytesToHex(spendingTx);
  std::string spendingTxid = computeTxid(spendingTx);

  // Compute the scripthash for the funding output's scriptPubKey
  // Electrum scripthash = reverse(sha256(scriptPubKey))
  std::vector<uint8_t> spkSha = BchHtlcScript::sha256(spk);
  std::reverse(spkSha.begin(), spkSha.end());
  std::string scripthash = BchHtlcScript::bytesToHex(spkSha);

  // Set up handler to serve all three RPCs
  TestElectrumServer server;
  server.setHandler([&](const std::string& method, const std::string& paramsJson) -> std::string {
    if (method == "server.version") {
      return R"(["TestServer","1.4"])";
    }
    if (method == "blockchain.transaction.get") {
      Common::JsonValue params = Common::JsonValue::fromString(paramsJson);
      std::string reqTxid = params[0].getString();
      if (reqTxid == fundingTxid) {
        return "\"" + fundingHex + "\"";
      }
      if (reqTxid == spendingTxid) {
        return "\"" + spendingHex + "\"";
      }
    }
    if (method == "blockchain.scripthash.get_history") {
      Common::JsonValue params = Common::JsonValue::fromString(paramsJson);
      std::string reqScripthash = params[0].getString();
      if (reqScripthash == scripthash) {
        return R"([{"tx_hash":")" + spendingTxid + R"(","height":100}])";
      }
    }
    return "null";
  });

  uint16_t port = server.start();
  std::string addr = "127.0.0.1:" + std::to_string(port);

  ElectrumSpvClient client({addr}, 1, 0,
      "0000000000000000000000000000000000000000000000000000000000000000");

  SpvSpend spend;
  assert(client.findSpend(fundingTxid, 0, spend));
  assert(spend.spent);
  assert(spend.spendingTxid == spendingTxid);
  assert(!spend.rawSpendingTx.empty());
  assert(spend.rawSpendingTx == spendingTx);

  // Spending an output that doesn't exist returns false (no inputs match)
  assert(client.findSpend(fundingTxid, 1, spend));
  assert(!spend.spent);

  server.stop();
}

// Test: findSpend returns false when no history
static void test_spv_find_spend_no_history() {
  std::vector<uint8_t> pkh(20, 0xDD);
  std::vector<uint8_t> spk = buildP2pkhSpk(pkh);
  std::vector<uint8_t> zeroHash(32, 0);
  std::vector<uint8_t> fundingTx = buildTestRawTx({{zeroHash, 0xFFFFFFFF}}, {spk});
  std::string fundingHex = BchHtlcScript::bytesToHex(fundingTx);
  std::string fundingTxid = computeTxid(fundingTx);

  std::vector<uint8_t> spkSha = BchHtlcScript::sha256(spk);
  std::reverse(spkSha.begin(), spkSha.end());
  std::string scripthash = BchHtlcScript::bytesToHex(spkSha);

  TestElectrumServer server;
  server.setHandler([&](const std::string& method, const std::string& paramsJson) -> std::string {
    if (method == "server.version") {
      return R"(["TestServer","1.4"])";
    }
    if (method == "blockchain.transaction.get") {
      Common::JsonValue params = Common::JsonValue::fromString(paramsJson);
      std::string reqTxid = params[0].getString();
      if (reqTxid == fundingTxid) {
        return "\"" + fundingHex + "\"";
      }
    }
    if (method == "blockchain.scripthash.get_history") {
      return "[]";  // empty history
    }
    return "null";
  });

  uint16_t port = server.start();
  std::string addr = "127.0.0.1:" + std::to_string(port);

  ElectrumSpvClient client({addr}, 1, 0,
      "0000000000000000000000000000000000000000000000000000000000000000");

  SpvSpend spend;
  assert(client.findSpend(fundingTxid, 0, spend));
  assert(!spend.spent);

  server.stop();
}

// =============================================================================
// Task 9 tests: multi-server cross-check (eclipse mitigation)
// =============================================================================

// Test: cross-check passes when two servers agree on headers
static void test_spv_crosscheck_servers_agree() {
  std::vector<SpvHeader> chain = buildChain(5);

  std::string tipHex = BchHtlcScript::bytesToHex(chain.back().serialize());

  // Server A handler
  TestElectrumServer serverA;
  serverA.setHandler([&](const std::string& method, const std::string& paramsJson) -> std::string {
    if (method == "server.version") return R"(["ServerA","1.4"])";
    if (method == "blockchain.headers.subscribe") {
      return R"({"height":4,"hex":")" + tipHex + R"("})";
    }
    if (method == "blockchain.block.headers") {
      Common::JsonValue params = Common::JsonValue::fromString(paramsJson);
      uint64_t h = params[0].getInteger();
      uint32_t cnt = params[1].getInteger();
      std::string hex;
      for (uint64_t i = h; i < h + cnt && i < chain.size(); ++i) {
        hex += BchHtlcScript::bytesToHex(chain[i].serialize());
      }
      return R"({"headers":")" + hex + R"("})";
    }
    return "null";
  });
  uint16_t portA = serverA.start();

  // Server B handler (identical chain)
  TestElectrumServer serverB;
  serverB.setHandler([&](const std::string& method, const std::string& paramsJson) -> std::string {
    if (method == "server.version") return R"(["ServerB","1.4"])";
    if (method == "blockchain.headers.subscribe") {
      return R"({"height":4,"hex":")" + tipHex + R"("})";
    }
    if (method == "blockchain.block.headers") {
      Common::JsonValue params = Common::JsonValue::fromString(paramsJson);
      uint64_t h = params[0].getInteger();
      uint32_t cnt = params[1].getInteger();
      std::string hex;
      for (uint64_t i = h; i < h + cnt && i < chain.size(); ++i) {
        hex += BchHtlcScript::bytesToHex(chain[i].serialize());
      }
      return R"({"headers":")" + hex + R"("})";
    }
    return "null";
  });
  uint16_t portB = serverB.start();

  std::string addrA = "127.0.0.1:" + std::to_string(portA);
  std::string addrB = "127.0.0.1:" + std::to_string(portB);

  ElectrumSpvClient client({addrA, addrB}, 2, 0,
      "0000000000000000000000000000000000000000000000000000000000000000");

  assert(client.syncHeaders());

  uint64_t height = 0;
  assert(client.getTipHeight(height));
  assert(height == 4);

  serverA.stop();
  serverB.stop();
}

// Test: cross-check rejects when servers disagree on headers
static void test_spv_crosscheck_servers_disagree() {
  // Chain A
  std::vector<SpvHeader> chainA = buildChain(5);

  // Chain B: different merkle roots, same height
  std::vector<SpvHeader> chainB;
  {
    SpvHeader genesis = makeHeader(1, zeroHash(), makeMerkle(0xbb), 1000000000, 0);
    chainB.push_back(genesis);
    for (size_t i = 1; i < 5; ++i) {
      SpvHeader h = makeHeader(
          1, chainB.back().hash(), makeMerkle(static_cast<uint8_t>(0xbb + i)),
          1000000000 + static_cast<uint32_t>(i), static_cast<uint32_t>(i + 100));
      chainB.push_back(h);
    }
  }

  std::string tipHexA = BchHtlcScript::bytesToHex(chainA.back().serialize());
  std::string tipHexB = BchHtlcScript::bytesToHex(chainB.back().serialize());

  // Server A serves chain A
  TestElectrumServer serverA;
  serverA.setHandler([&](const std::string& method, const std::string& paramsJson) -> std::string {
    if (method == "server.version") return R"(["ServerA","1.4"])";
    if (method == "blockchain.headers.subscribe") {
      return R"({"height":4,"hex":")" + tipHexA + R"("})";
    }
    if (method == "blockchain.block.headers") {
      Common::JsonValue params = Common::JsonValue::fromString(paramsJson);
      uint64_t h = params[0].getInteger();
      uint32_t cnt = params[1].getInteger();
      std::string hex;
      for (uint64_t i = h; i < h + cnt && i < chainA.size(); ++i) {
        hex += BchHtlcScript::bytesToHex(chainA[i].serialize());
      }
      return R"({"headers":")" + hex + R"("})";
    }
    return "null";
  });
  uint16_t portA = serverA.start();

  // Server B serves chain B (different headers)
  TestElectrumServer serverB;
  serverB.setHandler([&](const std::string& method, const std::string& paramsJson) -> std::string {
    if (method == "server.version") return R"(["ServerB","1.4"])";
    if (method == "blockchain.headers.subscribe") {
      return R"({"height":4,"hex":")" + tipHexB + R"("})";
    }
    if (method == "blockchain.block.headers") {
      Common::JsonValue params = Common::JsonValue::fromString(paramsJson);
      uint64_t h = params[0].getInteger();
      uint32_t cnt = params[1].getInteger();
      std::string hex;
      for (uint64_t i = h; i < h + cnt && i < chainB.size(); ++i) {
        hex += BchHtlcScript::bytesToHex(chainB[i].serialize());
      }
      return R"({"headers":")" + hex + R"("})";
    }
    return "null";
  });
  uint16_t portB = serverB.start();

  std::string addrA = "127.0.0.1:" + std::to_string(portA);
  std::string addrB = "127.0.0.1:" + std::to_string(portB);

  ElectrumSpvClient client({addrA, addrB}, 2, 0,
      "0000000000000000000000000000000000000000000000000000000000000000");

  // syncHeaders uses server A (index 0), succeeds
  assert(client.syncHeaders());

  // getTipHeight queries both, takes min (both say 4),
  // cross-checks header at height 4 — fails because servers disagree
  uint64_t height = 0;
  assert(!client.getTipHeight(height));

  serverA.stop();
  serverB.stop();
}

// Test: single-server mode works without cross-checking
static void test_spv_crosscheck_single_server() {
  std::vector<SpvHeader> chain = buildChain(3);

  std::string tipHex = BchHtlcScript::bytesToHex(chain.back().serialize());

  TestElectrumServer server;
  server.setHandler([&](const std::string& method, const std::string& paramsJson) -> std::string {
    if (method == "server.version") return R"(["TestServer","1.4"])";
    if (method == "blockchain.headers.subscribe") {
      return R"({"height":2,"hex":")" + tipHex + R"("})";
    }
    if (method == "blockchain.block.headers") {
      Common::JsonValue params = Common::JsonValue::fromString(paramsJson);
      uint64_t h = params[0].getInteger();
      uint32_t cnt = params[1].getInteger();
      std::string hex;
      for (uint64_t i = h; i < h + cnt && i < chain.size(); ++i) {
        hex += BchHtlcScript::bytesToHex(chain[i].serialize());
      }
      return R"({"headers":")" + hex + R"("})";
    }
    return "null";
  });

  uint16_t port = server.start();
  std::string addr = "127.0.0.1:" + std::to_string(port);

  ElectrumSpvClient client({addr}, 1, 0,
      "0000000000000000000000000000000000000000000000000000000000000000");

  assert(client.syncHeaders());

  uint64_t height = 0;
  assert(client.getTipHeight(height));
  assert(height == 2);

  server.stop();
}

// =============================================================================
// Main
// =============================================================================

int main() {
  // ElectrumConnection tests
  test_connection_basic_call();
  test_connection_multiple_calls();
  test_connection_custom_handler();
  test_connection_received_method_tracking();
  test_connection_nonexistent_server();
  test_connection_disconnected_call();
  std::cout << "All ElectrumConnection tests passed." << std::endl;

  // ElectrumSpvClient tests
  test_spv_sync_headers();
  test_spv_empty_store_tip();
  test_spv_sync_larger_chain();
  test_spv_insufficient_servers();
  test_spv_store_consistency();
  test_spv_verify_tx_inclusion_valid();
  test_spv_verify_tx_inclusion_tampered();
  test_spv_verify_tx_empty_store();
  test_spv_get_raw_tx();
  test_spv_find_spend();
  test_spv_find_spend_no_history();

  // Task 9: cross-check tests
  test_spv_crosscheck_servers_agree();
  test_spv_crosscheck_servers_disagree();
  test_spv_crosscheck_single_server();

  std::cout << "All ElectrumSpvClient tests passed." << std::endl;

  return 0;
}
