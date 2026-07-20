// Copyright (c) 2017-2026 Fuego Developers
//
// NeutrinoSpvClient and GCS filter tests.

#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "SwapDaemon/Spv/Neutrino/NeutrinoSpvClient.h"
#include "SwapDaemon/Spv/SpvHeader.h"
#include "SwapDaemon/Spv/SpvHeaderStore.h"
#include "SwapDaemon/BitcoinCash/HtlcScript.h"

using namespace XfgSwap;

// =============================================================================
// SipHash known-answer tests (reference: https://github.com/veorq/SipHash)
// =============================================================================

static void test_siphash() {
  uint8_t key[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
  uint64_t k0, k1;
  std::memcpy(&k0, key, 8);
  std::memcpy(&k1, key + 8, 8);

  // SipHash-2-4 with key={0..15}, empty input
  uint64_t h = SipHash(nullptr, 0, k0, k1);
  assert(h == 0x726fdb47dd0e0e31ULL);

  // SipHash-2-4 of "a" (0x61)
  uint8_t data_a = 0x61;
  h = SipHash(&data_a, 1, k0, k1);
  assert(h == 0x2ba3e8e9a71148caULL);

  // SipHash-2-4 of "aa" (0x61, 0x61)
  uint8_t data_aa[2] = {0x61, 0x61};
  h = SipHash(data_aa, 2, k0, k1);
  assert(h == 0x20841991dfbd7110ULL);

  // SipHash-2-4 of "aaa" (0x61,0x61,0x61)
  uint8_t data_aaa[3] = {0x61, 0x61, 0x61};
  h = SipHash(data_aaa, 3, k0, k1);
  assert(h == 0x811f1dd761015e4aULL);

  std::cout << "test_siphash passed." << std::endl;
}

// =============================================================================
// GCS filter build and match tests
// =============================================================================

static void test_gcs_build_and_match() {
  GcsFilterParams params;
  params.M = 784931;
  params.P = 19;

  std::vector<std::vector<uint8_t>> items;
  items.push_back({0x01, 0x02, 0x03, 0x04});
  items.push_back({0xAA, 0xBB, 0xCC, 0xDD});
  items.push_back({0xDE, 0xAD, 0xBE, 0xEF});

  std::vector<uint8_t> filter = NeutrinoSpvClient::buildFilter(items, params);
  assert(!filter.empty());

  // All items in the set must match
  assert(NeutrinoSpvClient::matchFilter(filter, items[0], params));
  assert(NeutrinoSpvClient::matchFilter(filter, items[1], params));
  assert(NeutrinoSpvClient::matchFilter(filter, items[2], params));

  std::cout << "test_gcs_build_and_match passed." << std::endl;
}

static void test_gcs_no_false_negative() {
  GcsFilterParams params;
  params.M = 784931;
  params.P = 19;

  // Build a filter from 100 items
  std::vector<std::vector<uint8_t>> items;
  for (uint32_t i = 0; i < 100; ++i) {
    std::vector<uint8_t> item(4);
    item[0] = static_cast<uint8_t>(i);
    item[1] = static_cast<uint8_t>(i >> 8);
    item[2] = static_cast<uint8_t>(i >> 16);
    item[3] = static_cast<uint8_t>(i >> 24);
    items.push_back(item);
  }

  std::vector<uint8_t> filter = NeutrinoSpvClient::buildFilter(items, params);

  // Every item must match (no false negatives)
  for (const auto& item : items) {
    assert(NeutrinoSpvClient::matchFilter(filter, item, params));
  }

  std::cout << "test_gcs_no_false_negative passed." << std::endl;
}

static void test_gcs_false_positive_rate() {
  GcsFilterParams params;
  params.M = 784931;
  params.P = 19;

  // Build a filter from known items
  std::vector<std::vector<uint8_t>> items;
  for (uint32_t i = 0; i < 1000; ++i) {
    std::vector<uint8_t> item(4);
    item[0] = static_cast<uint8_t>(i);
    item[1] = static_cast<uint8_t>(i >> 8);
    item[2] = static_cast<uint8_t>(i >> 16);
    item[3] = static_cast<uint8_t>(i >> 24);
    items.push_back(item);
  }

  std::vector<uint8_t> filter = NeutrinoSpvClient::buildFilter(items, params);

  // Test items NOT in the set
  uint32_t falsePositives = 0;
  uint32_t testCount = 100000;
  for (uint32_t i = 1000; i < 1000 + testCount; ++i) {
    std::vector<uint8_t> testItem(4);
    testItem[0] = static_cast<uint8_t>(i);
    testItem[1] = static_cast<uint8_t>(i >> 8);
    testItem[2] = static_cast<uint8_t>(i >> 16);
    testItem[3] = static_cast<uint8_t>(i >> 24);
    if (NeutrinoSpvClient::matchFilter(filter, testItem, params)) {
      falsePositives++;
    }
  }

  double fpr = static_cast<double>(falsePositives) / testCount;
  std::cout << "False positive rate: " << fpr * 100.0 << "% ("
            << falsePositives << "/" << testCount << ")" << std::endl;

  // With P=19, expected FPR ≈ 1/2^19 ≈ 0.00019%. Allow 500 for safety margin.
  assert(falsePositives < 500);

  std::cout << "test_gcs_false_positive_rate passed." << std::endl;
}

static void test_gcs_empty_filter() {
  GcsFilterParams params;
  std::vector<uint8_t> filter = NeutrinoSpvClient::buildFilter({}, params);
  assert(filter.empty());
  assert(!NeutrinoSpvClient::matchFilter(filter, {0x01}, params));
  assert(!NeutrinoSpvClient::matchFilter({}, {0x01}, params));

  std::cout << "test_gcs_empty_filter passed." << std::endl;
}

static void test_gcs_duplicate_items() {
  GcsFilterParams params;
  params.M = 784931;
  params.P = 19;

  std::vector<std::vector<uint8_t>> items;
  items.push_back({0xAA, 0xBB});
  items.push_back({0xAA, 0xBB});  // duplicate
  items.push_back({0xCC, 0xDD});

  std::vector<uint8_t> filter = NeutrinoSpvClient::buildFilter(items, params);
  assert(!filter.empty());

  assert(NeutrinoSpvClient::matchFilter(filter, {0xAA, 0xBB}, params));
  assert(NeutrinoSpvClient::matchFilter(filter, {0xCC, 0xDD}, params));

  std::cout << "test_gcs_duplicate_items passed." << std::endl;
}

// =============================================================================
// NeutrinoSpvClient basic tests
// =============================================================================

static void test_neutrino_client_protocolName() {
  SpvHeaderStore store;
  NeutrinoSpvClient client(store, {});
  assert(client.protocolName() == "neutrino");
  std::cout << "test_neutrino_client_protocolName passed." << std::endl;
}

static void test_neutrino_empty_store_tip() {
  SpvHeaderStore store;
  NeutrinoSpvClient client(store, {});
  uint64_t height = 0;
  assert(!client.getTipHeight(height));
  std::cout << "test_neutrino_empty_store_tip passed." << std::endl;
}

// Test: matchFilter with a P2PKH scriptPubKey
static void test_neutrino_matchFilter_p2pkh() {
  GcsFilterParams params;

  // Build a P2PKH scriptPubKey
  std::vector<uint8_t> spk;
  spk.insert(spk.end(), {0x76, 0xA9});  // OP_DUP OP_HASH160
  spk.push_back(0x14);                    // push 20 bytes
  for (uint8_t i = 0x01; i <= 0x14; ++i) spk.push_back(i);
  spk.insert(spk.end(), {0x88, 0xAC});  // OP_EQUALVERIFY OP_CHECKSIG

  std::vector<std::vector<uint8_t>> items = {spk};
  std::vector<uint8_t> filter = NeutrinoSpvClient::buildFilter(items, params);

  assert(NeutrinoSpvClient::matchFilter(filter, spk, params));

  // Different script should not match
  std::vector<uint8_t> otherSpk = spk;
  otherSpk[22] = 0xFF;  // flip a byte in the hash
  assert(!NeutrinoSpvClient::matchFilter(filter, otherSpk, params));

  std::cout << "test_neutrino_matchFilter_p2pkh passed." << std::endl;
}

// =============================================================================
// Mock connection for integration tests
// =============================================================================

class MockNeutrinoConnection : public NeutrinoConnection {
public:
  using Handler = std::function<bool(const std::string&, const std::string&, std::string&)>;

  explicit MockNeutrinoConnection(Handler h) : m_handler(std::move(h)) {}

  bool sendRequest(const std::string& method, const std::string& params,
                   std::string& response) override {
    return m_handler(method, params, response);
  }

private:
  Handler m_handler;
};

// =============================================================================
// Integration test: syncHeaders with mock connection
// =============================================================================

static SpvHeader makeTestHeader(uint32_t version,
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

static std::vector<uint8_t> testZeroHash() {
  return std::vector<uint8_t>(32, 0);
}

static std::vector<uint8_t> testMerkle(uint8_t fill) {
  return std::vector<uint8_t>(32, fill);
}

static std::vector<SpvHeader> buildTestChain(size_t count) {
  std::vector<SpvHeader> chain;
  SpvHeader genesis = makeTestHeader(1, testZeroHash(), testMerkle(0xaa), 1000000000, 0);
  chain.push_back(genesis);
  for (size_t i = 1; i < count; ++i) {
    SpvHeader h = makeTestHeader(
        1, chain.back().hash(), testMerkle(static_cast<uint8_t>(i)),
        1000000000 + static_cast<uint32_t>(i), static_cast<uint32_t>(i));
    chain.push_back(h);
  }
  return chain;
}

static void test_neutrino_sync_headers() {
  std::vector<SpvHeader> chain = buildTestChain(5);

  std::string tipHex = BchHtlcScript::bytesToHex(chain.back().serialize());
  std::string allHeadersHex;
  for (const auto& h : chain) {
    allHeadersHex += BchHtlcScript::bytesToHex(h.serialize());
  }

  auto handler = [&](const std::string& method, const std::string& params,
                     std::string& response) -> bool {
    if (method == "getheaders") {
      if (params == "[]") {
        response = R"({"height":4,"hex":")" + tipHex + R"("})";
        return true;
      }
      response = R"({"headers":")" + allHeadersHex + R"("})";
      return true;
    }
    return false;
  };

  SpvHeaderStore store;
  NeutrinoSpvClient client(store, {});
  client.addConnection(std::make_unique<MockNeutrinoConnection>(handler));

  assert(client.syncHeaders());

  uint64_t height = 0;
  assert(client.getTipHeight(height));
  assert(height == 4);

  SpvHeader got;
  assert(store.headerAtHeight(0, got));
  assert(got.hashDisplay() == chain[0].hashDisplay());
  assert(store.headerAtHeight(4, got));
  assert(got.hashDisplay() == chain[4].hashDisplay());

  std::cout << "test_neutrino_sync_headers passed." << std::endl;
}

// Test: getRawTx with mock connection
static void test_neutrino_get_raw_tx() {
  std::vector<uint8_t> rawTx = {0x02, 0x00, 0x00, 0x00, 0x01, 0x00};
  std::string txHex = BchHtlcScript::bytesToHex(rawTx);
  std::string txid = "aabbccdd001122334455667788990011aabbccdd001122334455667788990011";

  auto handler = [&](const std::string& method, const std::string& params,
                     std::string& response) -> bool {
    if (method == "gettx") {
      response = "\"" + txHex + "\"";
      return true;
    }
    return false;
  };

  SpvHeaderStore store;
  NeutrinoSpvClient client(store, {});
  client.addConnection(std::make_unique<MockNeutrinoConnection>(handler));

  std::vector<uint8_t> result;
  assert(client.getRawTx(txid, result));
  assert(result == rawTx);

  std::cout << "test_neutrino_get_raw_tx passed." << std::endl;
}

// Test: getFilter with mock connection
static void test_neutrino_get_filter() {
  std::vector<uint8_t> filterBytes = {0x01, 0x02, 0x03, 0x04, 0x05};
  std::string filterHex = BchHtlcScript::bytesToHex(filterBytes);

  auto handler = [&](const std::string& method, const std::string& params,
                     std::string& response) -> bool {
    if (method == "getcfilters") {
      response = R"({"filter":")" + filterHex + R"("})";
      return true;
    }
    return false;
  };

  SpvHeaderStore store;
  NeutrinoSpvClient client(store, {});
  client.addConnection(std::make_unique<MockNeutrinoConnection>(handler));

  std::vector<uint8_t> filter;
  assert(client.getFilter(100, filter));
  assert(filter == filterBytes);

  // Second call should hit cache
  std::vector<uint8_t> filter2;
  assert(client.getFilter(100, filter2));
  assert(filter2 == filterBytes);

  std::cout << "test_neutrino_get_filter passed." << std::endl;
}

// =============================================================================
// Main
// =============================================================================

int main() {
  // SipHash tests
  test_siphash();

  // GCS filter tests
  test_gcs_build_and_match();
  test_gcs_no_false_negative();
  test_gcs_false_positive_rate();
  test_gcs_empty_filter();
  test_gcs_duplicate_items();

  // NeutrinoSpvClient tests
  test_neutrino_client_protocolName();
  test_neutrino_empty_store_tip();
  test_neutrino_matchFilter_p2pkh();

  // Integration tests
  test_neutrino_sync_headers();
  test_neutrino_get_raw_tx();
  test_neutrino_get_filter();

  std::cout << "All NeutrinoSpvClient tests passed." << std::endl;

  return 0;
}
