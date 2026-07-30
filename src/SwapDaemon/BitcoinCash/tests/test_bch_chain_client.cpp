// Copyright (c) 2017-2026 Fuego Developers
//
// Tests for BchChainClient SPV mode: verifyLock, extractSecret, and
// BchHtlcScript::parseClaimPreimage / redeemScriptToP2shScriptPubKey.

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "SwapDaemon/BitcoinCash/BchChainClient.h"
#include "SwapDaemon/BitcoinCash/HtlcScript.h"
#include "SwapDaemon/Spv/ISpvClient.h"
#include "Common/StringTools.h"

using namespace XfgSwap;

// =============================================================================
// Mock ISpvClient
// =============================================================================

class MockSpvClient : public ISpvClient {
public:
  std::string protocolName() const override { return "mock"; }
  bool syncHeaders() override { return true; }

  bool getTipHeight(uint64_t& height) override {
    height = m_tipHeight;
    return true;
  }

  bool verifyTxInclusion(const std::string& txid, SpvTxInclusion& out) override {
    auto it = m_inclusions.find(txid);
    if (it == m_inclusions.end()) return false;
    out = it->second;
    return true;
  }

  bool findSpend(const std::string& txid, uint32_t vout, SpvSpend& out) override {
    (void)txid; (void)vout; (void)out;
    return false;
  }

  bool getRawTx(const std::string& txid, std::vector<uint8_t>& rawTx) override {
    auto it = m_rawTxs.find(txid);
    if (it == m_rawTxs.end()) return false;
    rawTx = it->second;
    return true;
  }

  bool broadcastTx(const std::vector<uint8_t>& rawTx, std::string& txid) override {
    (void)rawTx;
    txid = "mock_broadcast_txid";
    return true;
  }

  // Test helpers
  uint64_t m_tipHeight = 1000;
  std::unordered_map<std::string, SpvTxInclusion> m_inclusions;
  std::unordered_map<std::string, std::vector<uint8_t>> m_rawTxs;
};

// =============================================================================
// Helper: build a minimal raw BCH transaction with given outputs
// =============================================================================

// Build a minimal raw tx with one input and N P2SH outputs.
// outputScriptPubKey: the P2SH scriptPubKey (23 bytes)
// outputValue: satoshis for each output
static std::vector<uint8_t> buildMinimalRawTx(
    const std::vector<uint8_t>& outputScriptPubKey,
    uint64_t outputValue) {

  std::vector<uint8_t> tx;

  // Version = 2
  tx.insert(tx.end(), {0x02, 0x00, 0x00, 0x00});

  // 1 input
  tx.push_back(0x01);

  // prev txid (32 bytes zeros)
  tx.insert(tx.end(), 32, 0x00);

  // prev vout = 0
  tx.insert(tx.end(), {0x00, 0x00, 0x00, 0x00});

  // scriptSig (empty)
  tx.push_back(0x00);

  // sequence
  tx.insert(tx.end(), {0xFF, 0xFF, 0xFF, 0xFF});

  // 1 output
  tx.push_back(0x01);

  // value (8 bytes LE)
  for (int i = 0; i < 8; ++i) {
    tx.push_back(static_cast<uint8_t>((outputValue >> (i * 8)) & 0xFF));
  }

  // scriptPubKey
  tx.push_back(static_cast<uint8_t>(outputScriptPubKey.size()));
  tx.insert(tx.end(), outputScriptPubKey.begin(), outputScriptPubKey.end());

  // locktime
  tx.insert(tx.end(), {0x00, 0x00, 0x00, 0x00});

  return tx;
}

// Build a minimal raw tx with an input whose scriptSig is a P2SH claim:
// <sig> <preimage> OP_TRUE <redeemScript>
static std::vector<uint8_t> buildClaimSpendingTx(
    const std::vector<uint8_t>& signature,
    const std::vector<uint8_t>& preimage,
    const std::vector<uint8_t>& redeemScript) {

  std::vector<uint8_t> tx;

  // Version = 2
  tx.insert(tx.end(), {0x02, 0x00, 0x00, 0x00});

  // 1 input
  tx.push_back(0x01);

  // prev txid (32 bytes zeros)
  tx.insert(tx.end(), 32, 0x00);

  // prev vout = 0
  tx.insert(tx.end(), {0x00, 0x00, 0x00, 0x00});

  // scriptSig: <sig-push> <preimage-push> OP_TRUE <redeemScript-push>
  std::vector<uint8_t> scriptSig;
  auto pushData = [&](const std::vector<uint8_t>& data) {
    if (data.size() <= 75) {
      scriptSig.push_back(static_cast<uint8_t>(data.size()));
    } else {
      scriptSig.push_back(0x4C);  // OP_PUSHDATA1
      scriptSig.push_back(static_cast<uint8_t>(data.size()));
    }
    scriptSig.insert(scriptSig.end(), data.begin(), data.end());
  };
  // <sig>
  pushData(signature);
  // <preimage>
  pushData(preimage);
  // OP_TRUE
  scriptSig.push_back(0x51);
  // <redeemScript>
  pushData(redeemScript);

  tx.push_back(static_cast<uint8_t>(scriptSig.size()));
  tx.insert(tx.end(), scriptSig.begin(), scriptSig.end());

  // sequence
  tx.insert(tx.end(), {0xFF, 0xFF, 0xFF, 0xFF});

  // 0 outputs
  tx.push_back(0x00);

  // locktime
  tx.insert(tx.end(), {0x00, 0x00, 0x00, 0x00});

  return tx;
}

// =============================================================================
// Tests
// =============================================================================

static void test_redeemScriptToP2shScriptPubKey() {
  std::cout << "test_redeemScriptToP2shScriptPubKey..." << std::endl;

  // Create a dummy redeem script (just 10 bytes of padding for testing)
  std::vector<uint8_t> redeemScript = {0x63, 0xA8, 0x20, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};

  auto p2sh = BchHtlcScript::redeemScriptToP2shScriptPubKey(redeemScript);

  // P2SH scriptPubKey: OP_HASH160 (0xA9) PUSH20 (0x14) <20-byte-hash> OP_EQUAL (0x87)
  assert(p2sh.size() == 23);
  assert(p2sh[0] == 0xA9);  // OP_HASH160
  assert(p2sh[1] == 0x14);  // push 20 bytes
  assert(p2sh[22] == 0x87); // OP_EQUAL

  // The hash should be hash160(redeemScript)
  auto expectedHash = BchHtlcScript::hash160(redeemScript);
  assert(std::equal(p2sh.begin() + 2, p2sh.begin() + 22, expectedHash.begin()));

  std::cout << "  PASSED" << std::endl;
}

static void test_parseClaimPreimage_basic() {
  std::cout << "test_parseClaimPreimage_basic..." << std::endl;

  // Build a simple HTLC redeem script for testing
  std::vector<uint8_t> hashLockSha256(32, 0xAA);
  std::vector<uint8_t> recipientPubKey(33, 0x02);
  std::vector<uint8_t> senderPubKey(33, 0x03);
  uint32_t timeoutBlock = 500000;

  auto redeemScript = BchHtlcScript::createRedeemScript(
      hashLockSha256, recipientPubKey, senderPubKey, timeoutBlock);

  auto p2shScriptPubKey = BchHtlcScript::redeemScriptToP2shScriptPubKey(redeemScript);

  // Build a claim scriptSig
  std::vector<uint8_t> signature(72, 0x30);  // dummy signature
  std::vector<uint8_t> preimage = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE,
                                    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                                    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
                                    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18};

  auto spendingTx = buildClaimSpendingTx(signature, preimage, redeemScript);

  auto extracted = BchHtlcScript::parseClaimPreimage(spendingTx, p2shScriptPubKey);
  assert(!extracted.empty());
  assert(extracted == preimage);

  std::cout << "  PASSED" << std::endl;
}

static void test_parseClaimPreimage_wrongP2sh() {
  std::cout << "test_parseClaimPreimage_wrongP2sh..." << std::endl;

  std::vector<uint8_t> hashLockSha256(32, 0xAA);
  std::vector<uint8_t> recipientPubKey(33, 0x02);
  std::vector<uint8_t> senderPubKey(33, 0x03);

  auto redeemScript = BchHtlcScript::createRedeemScript(
      hashLockSha256, recipientPubKey, senderPubKey, 500000);

  auto p2shScriptPubKey = BchHtlcScript::redeemScriptToP2shScriptPubKey(redeemScript);

  std::vector<uint8_t> signature(72, 0x30);
  std::vector<uint8_t> preimage(32, 0xBB);

  auto spendingTx = buildClaimSpendingTx(signature, preimage, redeemScript);

  // Use a wrong P2SH scriptPubKey (different hash)
  std::vector<uint8_t> wrongP2sh(23, 0x00);
  wrongP2sh[0] = 0xA9;
  wrongP2sh[1] = 0x14;
  wrongP2sh[22] = 0x87;

  auto extracted = BchHtlcScript::parseClaimPreimage(spendingTx, wrongP2sh);
  assert(extracted.empty());  // Should not find a match

  std::cout << "  PASSED" << std::endl;
}

static void test_parseClaimPreimage_noP2shInput() {
  std::cout << "test_parseClaimPreimage_noP2shInput..." << std::endl;

  // Build a tx with no P2SH claim inputs
  std::vector<uint8_t> tx;
  tx.insert(tx.end(), {0x02, 0x00, 0x00, 0x00}); // version
  tx.push_back(0x01); // 1 input
  tx.insert(tx.end(), 32, 0x00); // prev txid
  tx.insert(tx.end(), {0x00, 0x00, 0x00, 0x00}); // prev vout
  tx.push_back(0x04); // scriptSig length
  tx.insert(tx.end(), {0x01, 0x02, 0x03, 0x04}); // random scriptSig
  tx.insert(tx.end(), {0xFF, 0xFF, 0xFF, 0xFF}); // sequence
  tx.push_back(0x00); // 0 outputs
  tx.insert(tx.end(), {0x00, 0x00, 0x00, 0x00}); // locktime

  std::vector<uint8_t> p2sh(23, 0x00);
  p2sh[0] = 0xA9; p2sh[1] = 0x14; p2sh[22] = 0x87;

  auto extracted = BchHtlcScript::parseClaimPreimage(tx, p2sh);
  assert(extracted.empty());

  std::cout << "  PASSED" << std::endl;
}

static void test_checkSwapStatus_spv_mode() {
  std::cout << "test_checkSwapStatus_spv_mode..." << std::endl;

  auto mock = std::make_shared<MockSpvClient>();
  mock->m_tipHeight = 500;

  // Build a raw locking tx with a P2SH output
  std::vector<uint8_t> p2shScriptPubKey(23, 0x00);
  p2shScriptPubKey[0] = 0xA9;
  p2shScriptPubKey[1] = 0x14;
  // Put some hash in the middle
  for (int i = 2; i < 22; ++i) p2shScriptPubKey[i] = static_cast<uint8_t>(i);
  p2shScriptPubKey[22] = 0x87;

  uint64_t lockAmount = 100000;
  auto rawTx = buildMinimalRawTx(p2shScriptPubKey, lockAmount);

  std::string lockTxId = "aabbccdd11223344aabbccdd11223344aabbccdd11223344aabbccdd11223344";
  mock->m_rawTxs[lockTxId] = rawTx;
  mock->m_inclusions[lockTxId] = {true, 498, 3, true};  // included, height 498, depth 3

  BchChainClient client(mock, "");

  SwapParams params;
  params.ctrLockTxId = lockTxId;
  params.ctrAmount = lockAmount;

  auto result = client.verifyLock(params);
  assert(result.success);
  assert(result.confirmed);
  assert(result.spvVerified);
  assert(result.blockHeight == 498);
  assert(result.confirmations == 3);

  std::cout << "  PASSED" << std::endl;
}

static void test_verifyLock_spv_wrong_amount() {
  std::cout << "test_verifyLock_spv_wrong_amount..." << std::endl;

  auto mock = std::make_shared<MockSpvClient>();
  mock->m_tipHeight = 500;

  std::vector<uint8_t> p2shScriptPubKey(23, 0x00);
  p2shScriptPubKey[0] = 0xA9;
  p2shScriptPubKey[1] = 0x14;
  p2shScriptPubKey[22] = 0x87;

  uint64_t lockAmount = 100000;
  auto rawTx = buildMinimalRawTx(p2shScriptPubKey, lockAmount);

  std::string lockTxId = "aabbccdd11223344aabbccdd11223344aabbccdd11223344aabbccdd11223344";
  mock->m_rawTxs[lockTxId] = rawTx;
  mock->m_inclusions[lockTxId] = {true, 498, 3, true};

  BchChainClient client(mock, "");

  SwapParams params;
  params.ctrLockTxId = lockTxId;
  params.ctrAmount = 200000;  // Higher than locked amount

  auto result = client.verifyLock(params);
  assert(!result.success);
  assert(result.error.find("no P2SH output") != std::string::npos);

  std::cout << "  PASSED" << std::endl;
}

static void test_verifyLock_spv_not_found() {
  std::cout << "test_verifyLock_spv_not_found..." << std::endl;

  auto mock = std::make_shared<MockSpvClient>();
  BchChainClient client(mock, "");

  SwapParams params;
  params.ctrLockTxId = "nonexistent";
  params.ctrAmount = 100000;

  auto result = client.verifyLock(params);
  assert(!result.success);
  assert(result.error.find("getRawTx failed") != std::string::npos);

  std::cout << "  PASSED" << std::endl;
}

static void test_verifyLock_spv_not_confirmed() {
  std::cout << "test_verifyLock_spv_not_confirmed..." << std::endl;

  auto mock = std::make_shared<MockSpvClient>();
  mock->m_tipHeight = 500;

  std::vector<uint8_t> p2shScriptPubKey(23, 0x00);
  p2shScriptPubKey[0] = 0xA9;
  p2shScriptPubKey[1] = 0x14;
  p2shScriptPubKey[22] = 0x87;

  auto rawTx = buildMinimalRawTx(p2shScriptPubKey, 100000);

  std::string lockTxId = "aabbccdd11223344aabbccdd11223344aabbccdd11223344aabbccdd11223344";
  mock->m_rawTxs[lockTxId] = rawTx;
  mock->m_inclusions[lockTxId] = {false, 0, 0, false};  // not included

  BchChainClient client(mock, "");

  SwapParams params;
  params.ctrLockTxId = lockTxId;
  params.ctrAmount = 100000;

  auto result = client.verifyLock(params);
  // verifyTxInclusion returns true but inclusion.included is false
  assert(result.success);  // The lock amount matched, but confirmed is false
  assert(!result.confirmed);

  std::cout << "  PASSED" << std::endl;
}

static void test_extractSecret_spv_mode() {
  std::cout << "test_extractSecret_spv_mode..." << std::endl;

  auto mock = std::make_shared<MockSpvClient>();

  // Build an HTLC redeem script
  std::vector<uint8_t> hashLockSha256(32, 0xCC);
  std::vector<uint8_t> recipientPubKey(33, 0x02);
  std::vector<uint8_t> senderPubKey(33, 0x03);
  auto redeemScript = BchHtlcScript::createRedeemScript(
      hashLockSha256, recipientPubKey, senderPubKey, 500000);
  auto redeemScriptHex = BchHtlcScript::bytesToHex(redeemScript);

  // Build a claim spending tx
  std::vector<uint8_t> signature(72, 0x30);
  std::vector<uint8_t> preimage = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE,
                                    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                                    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
                                    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18};
  auto spendingTx = buildClaimSpendingTx(signature, preimage, redeemScript);

  std::string spendingTxid = "1122334455667788112233445566778811223344556677881122334455667788";
  mock->m_rawTxs[spendingTxid] = spendingTx;

  BchChainClient client(mock, "");

  auto result = client.extractSecret(spendingTxid, redeemScriptHex);
  assert(!result.empty());
  assert(result == BchHtlcScript::bytesToHex(preimage));

  std::cout << "  PASSED" << std::endl;
}

static void test_extractSecret_spv_not_found() {
  std::cout << "test_extractSecret_spv_not_found..." << std::endl;

  auto mock = std::make_shared<MockSpvClient>();
  BchChainClient client(mock, "");

  std::vector<uint8_t> redeemScript(10, 0x55);
  auto result = client.extractSecret("nonexistent", BchHtlcScript::bytesToHex(redeemScript));
  assert(result.empty());

  std::cout << "  PASSED" << std::endl;
}

static void test_getCurrentHeight_spv_mode() {
  std::cout << "test_getCurrentHeight_spv_mode..." << std::endl;

  auto mock = std::make_shared<MockSpvClient>();
  mock->m_tipHeight = 777777;

  BchChainClient client(mock, "");

  uint64_t height = 0;
  bool ok = client.getCurrentHeight(height);
  assert(ok);
  assert(height == 777777);

  std::cout << "  PASSED" << std::endl;
}

static void test_fullNode_mode_no_spv() {
  std::cout << "test_fullNode_mode_no_spv..." << std::endl;

  // SPV constructor — no RPC
  auto mock = std::make_shared<MockSpvClient>();
  BchChainClient client(mock, "");

  // lock() should fail — no RPC
  SwapParams params;
  // Set a non-zero adaptor secret so we reach the RPC check, not the secret check
  memset(&params.adaptorSecret, 0x42, sizeof(params.adaptorSecret));
  auto result = client.lock(params);
  assert(!result.success);
  assert(result.error.find("RPC client not available") != std::string::npos);

  // claim() should fail — no RPC
  result = client.claim(params);
  assert(!result.success);

  // refund() should fail — no RPC
  result = client.refund(params);
  assert(!result.success);

  // verifyReserveProof should fail — no RPC
  result = client.verifyReserveProof("", 0, "addr:sig:msg");
  assert(!result.success);

  std::cout << "  PASSED" << std::endl;
}

// =============================================================================
// Main
// =============================================================================

int main() {
  std::cout << "=== BchChainClient / BchHtlcScript SPV Tests ===" << std::endl;

  test_redeemScriptToP2shScriptPubKey();
  test_parseClaimPreimage_basic();
  test_parseClaimPreimage_wrongP2sh();
  test_parseClaimPreimage_noP2shInput();
  test_checkSwapStatus_spv_mode();
  test_verifyLock_spv_wrong_amount();
  test_verifyLock_spv_not_found();
  test_verifyLock_spv_not_confirmed();
  test_extractSecret_spv_mode();
  test_extractSecret_spv_not_found();
  test_getCurrentHeight_spv_mode();
  test_fullNode_mode_no_spv();

  std::cout << "\nAll tests passed." << std::endl;
  return 0;
}
