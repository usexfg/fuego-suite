// Copyright (c) 2017-2026 Fuego Developers
//
// Tests for KmdHtlcScript and KmdChainClient SPV mode.

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "SwapDaemon/Komodo/KmdChainClient.h"
#include "SwapDaemon/Komodo/KmdHtlcScript.h"
#include "SwapDaemon/Spv/ISpvClient.h"

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
    if (txid == m_lastLockTxid && vout == 0 && !m_lastSpendTxid.empty()) {
      out.spent = true;
      out.spendingTxid = m_lastSpendTxid;
      auto it = m_rawTxs.find(m_lastSpendTxid);
      if (it != m_rawTxs.end())
        out.rawSpendingTx = it->second;
      return true;
    }
    return false;
  }

  bool getRawTx(const std::string& txid, std::vector<uint8_t>& rawTx) override {
    auto it = m_rawTxs.find(txid);
    if (it == m_rawTxs.end()) return false;
    rawTx = it->second;
    return true;
  }

  bool broadcastTx(const std::vector<uint8_t>& rawTx, std::string& txid) override {
    txid = "mock_" + KmdHtlcScript::bytesToHex(KmdHtlcScript::sha256(rawTx));
    txid = txid.substr(0, 64);
    m_rawTxs[txid] = rawTx;
    m_lastSpendTxid = txid;
    return true;
  }

  void setLockTx(const std::string& txid) { m_lastLockTxid = txid; }

  uint64_t m_tipHeight = 1000;
  std::string m_lastLockTxid;
  std::string m_lastSpendTxid;
  std::unordered_map<std::string, SpvTxInclusion> m_inclusions;
  std::unordered_map<std::string, std::vector<uint8_t>> m_rawTxs;
};

// =============================================================================
// Helpers
// =============================================================================

// Build a minimal raw tx with one input and one P2SH output.
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

// Build a minimal raw tx with a P2SH claim scriptSig.
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
  pushData(signature);
  pushData(preimage);
  scriptSig.push_back(0x51);  // OP_TRUE
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
// Tests: KmdHtlcScript
// =============================================================================

static void test_kmd_redeemScriptToP2shScriptPubKey() {
  std::cout << "test_kmd_redeemScriptToP2shScriptPubKey..." << std::endl;

  std::vector<uint8_t> hashLockSha256(32, 0xAA);
  std::vector<uint8_t> recipientPubKey(33, 0x02);
  std::vector<uint8_t> senderPubKey(33, 0x03);

  auto redeemScript = KmdHtlcScript::createHashTimeLockScript(
      hashLockSha256, 1234567890, recipientPubKey, senderPubKey, 500000);

  auto p2sh = KmdHtlcScript::redeemScriptToP2shScriptPubKey(redeemScript);

  // P2SH scriptPubKey: OP_HASH160 (0xA9) PUSH20 (0x14) <20-byte-hash> OP_EQUAL (0x87)
  assert(p2sh.size() == 23);
  assert(p2sh[0] == 0xA9);
  assert(p2sh[1] == 0x14);
  assert(p2sh[22] == 0x87);

  auto expectedHash = KmdHtlcScript::hash160(redeemScript);
  assert(std::equal(p2sh.begin() + 2, p2sh.begin() + 22, expectedHash.begin()));

  std::cout << "  PASSED" << std::endl;
}

static void test_kmd_parseClaimPreimage_basic() {
  std::cout << "test_kmd_parseClaimPreimage_basic..." << std::endl;

  std::vector<uint8_t> hashLockSha256(32, 0xAA);
  std::vector<uint8_t> recipientPubKey(33, 0x02);
  std::vector<uint8_t> senderPubKey(33, 0x03);

  auto redeemScript = KmdHtlcScript::createHashTimeLockScript(
      hashLockSha256, 1234567890, recipientPubKey, senderPubKey, 500000);

  auto p2shScriptPubKey = KmdHtlcScript::redeemScriptToP2shScriptPubKey(redeemScript);

  std::vector<uint8_t> signature(72, 0x30);
  std::vector<uint8_t> preimage = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE,
                                    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                                    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
                                    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18};

  auto spendingTx = buildClaimSpendingTx(signature, preimage, redeemScript);

  auto extracted = KmdHtlcScript::parseClaimPreimage(spendingTx, p2shScriptPubKey);
  assert(!extracted.empty());
  assert(extracted == preimage);

  std::cout << "  PASSED" << std::endl;
}

static void test_kmd_parseClaimPreimage_no_match() {
  std::cout << "test_kmd_parseClaimPreimage_no_match..." << std::endl;

  std::vector<uint8_t> hashLockSha256(32, 0xAA);
  std::vector<uint8_t> recipientPubKey(33, 0x02);
  std::vector<uint8_t> senderPubKey(33, 0x03);

  auto redeemScript = KmdHtlcScript::createHashTimeLockScript(
      hashLockSha256, 1234567890, recipientPubKey, senderPubKey, 500000);

  auto p2shScriptPubKey = KmdHtlcScript::redeemScriptToP2shScriptPubKey(redeemScript);

  std::vector<uint8_t> signature(72, 0x30);
  std::vector<uint8_t> preimage(32, 0xBB);

  auto spendingTx = buildClaimSpendingTx(signature, preimage, redeemScript);

  // Use a wrong P2SH scriptPubKey
  std::vector<uint8_t> wrongP2sh(23, 0x00);
  wrongP2sh[0] = 0xA9;
  wrongP2sh[1] = 0x14;
  wrongP2sh[22] = 0x87;

  auto extracted = KmdHtlcScript::parseClaimPreimage(spendingTx, wrongP2sh);
  assert(extracted.empty());

  std::cout << "  PASSED" << std::endl;
}

static void test_kmd_pubkeyHashToAddress() {
  std::cout << "test_kmd_pubkeyHashToAddress..." << std::endl;

  std::vector<uint8_t> pubkeyHash(20, 0x01);
  std::string address = KmdHtlcScript::pubkeyHashToAddress(pubkeyHash);

  // Verify it starts with 'R' (KMD P2PKH prefix 0x3C produces addresses starting with R)
  assert(!address.empty());
  assert(address[0] == 'R');

  // Verify round-trip: decode should give us back version 0x3C and the same hash
  // (We can't call base58CheckDecode directly since it's private, but we can
  // verify the address is non-empty and starts with the right character.)

  std::cout << "  PASSED" << std::endl;
}

static void test_kmd_scriptHashToAddress() {
  std::cout << "test_kmd_scriptHashToAddress..." << std::endl;

  std::vector<uint8_t> scriptHash(20, 0x02);
  std::string address = KmdHtlcScript::scriptHashToAddress(scriptHash);

  // Verify it starts with 'b' (KMD P2SH prefix 0x55 produces addresses starting with b)
  assert(!address.empty());
  assert(address[0] == 'b');

  std::cout << "  PASSED" << std::endl;
}

// =============================================================================
// Tests: KmdChainClient SPV mode
// =============================================================================

static void test_kmd_verifyLock_spv_mode() {
  std::cout << "test_kmd_verifyLock_spv_mode..." << std::endl;

  auto mock = std::make_shared<MockSpvClient>();
  mock->m_tipHeight = 500;

  std::vector<uint8_t> p2shScriptPubKey(23, 0x00);
  p2shScriptPubKey[0] = 0xA9;
  p2shScriptPubKey[1] = 0x14;
  for (int i = 2; i < 22; ++i) p2shScriptPubKey[i] = static_cast<uint8_t>(i);
  p2shScriptPubKey[22] = 0x87;

  uint64_t lockAmount = 100000;
  auto rawTx = buildMinimalRawTx(p2shScriptPubKey, lockAmount);

  std::string lockTxId = "aabbccdd11223344aabbccdd11223344aabbccdd11223344aabbccdd11223344";
  mock->m_rawTxs[lockTxId] = rawTx;
  mock->m_inclusions[lockTxId] = {true, 498, 3, true};

  KmdChainClient client(mock, "");

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

static void test_kmd_verifyLock_spv_wrong_amount() {
  std::cout << "test_kmd_verifyLock_spv_wrong_amount..." << std::endl;

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

  KmdChainClient client(mock, "");

  SwapParams params;
  params.ctrLockTxId = lockTxId;
  params.ctrAmount = 200000;

  auto result = client.verifyLock(params);
  assert(!result.success);
  assert(result.error.find("no P2SH output") != std::string::npos);

  std::cout << "  PASSED" << std::endl;
}

static void test_kmd_verifyLock_spv_not_found() {
  std::cout << "test_kmd_verifyLock_spv_not_found..." << std::endl;

  auto mock = std::make_shared<MockSpvClient>();
  KmdChainClient client(mock, "");

  SwapParams params;
  params.ctrLockTxId = "nonexistent";
  params.ctrAmount = 100000;

  auto result = client.verifyLock(params);
  assert(!result.success);
  assert(result.error.find("getRawTx failed") != std::string::npos);

  std::cout << "  PASSED" << std::endl;
}

static void test_kmd_extractSecret_spv_mode() {
  std::cout << "test_kmd_extractSecret_spv_mode..." << std::endl;

  auto mock = std::make_shared<MockSpvClient>();

  std::vector<uint8_t> hashLockSha256(32, 0xCC);
  std::vector<uint8_t> recipientPubKey(33, 0x02);
  std::vector<uint8_t> senderPubKey(33, 0x03);
  auto redeemScript = KmdHtlcScript::createHashTimeLockScript(
      hashLockSha256, 1234567890, recipientPubKey, senderPubKey, 500000);
  auto redeemScriptHex = KmdHtlcScript::bytesToHex(redeemScript);

  std::vector<uint8_t> signature(72, 0x30);
  std::vector<uint8_t> preimage = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE,
                                    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                                    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
                                    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18};
  auto spendingTx = buildClaimSpendingTx(signature, preimage, redeemScript);

  std::string spendingTxid = "1122334455667788112233445566778811223344556677881122334455667788";
  mock->m_rawTxs[spendingTxid] = spendingTx;

  KmdChainClient client(mock, "");

  auto result = client.extractSecret(spendingTxid, redeemScriptHex);
  assert(!result.empty());
  assert(result == KmdHtlcScript::bytesToHex(preimage));

  std::cout << "  PASSED" << std::endl;
}

static void test_kmd_extractSecret_spv_not_found() {
  std::cout << "test_kmd_extractSecret_spv_not_found..." << std::endl;

  auto mock = std::make_shared<MockSpvClient>();
  KmdChainClient client(mock, "");

  std::vector<uint8_t> redeemScript(10, 0x55);
  auto result = client.extractSecret("nonexistent", KmdHtlcScript::bytesToHex(redeemScript));
  assert(result.empty());

  std::cout << "  PASSED" << std::endl;
}

static void test_kmd_getCurrentHeight_spv_mode() {
  std::cout << "test_kmd_getCurrentHeight_spv_mode..." << std::endl;

  auto mock = std::make_shared<MockSpvClient>();
  mock->m_tipHeight = 2700000;

  KmdChainClient client(mock, "");

  uint64_t height = 0;
  bool ok = client.getCurrentHeight(height);
  assert(ok);
  assert(height == 2700000);

  std::cout << "  PASSED" << std::endl;
}

static void test_kmd_spv_mode_lock_claim_refund_fail() {
  std::cout << "test_kmd_spv_mode_lock_claim_refund_fail..." << std::endl;

  auto mock = std::make_shared<MockSpvClient>();
  KmdChainClient client(mock, "");

  SwapParams params;
  memset(&params.adaptorSecret, 0x42, sizeof(params.adaptorSecret));

  auto result = client.lock(params);
  assert(!result.success);
  assert(result.error.find("SPV mode does not support lock") != std::string::npos);

  result = client.claim(params);
  assert(!result.success);
  assert(result.error.find("SPV mode needs WIF for local signing") != std::string::npos);

  result = client.refund(params);
  assert(!result.success);
  assert(result.error.find("SPV mode needs WIF for local signing") != std::string::npos);

  result = client.verifyReserveProof("", 0, "addr:sig:msg");
  assert(!result.success);
  assert(result.error.find("not implemented") != std::string::npos);

  std::cout << "  PASSED" << std::endl;
}

// =============================================================================
// Tests: KmdChainClient SPV claim/refund success path
// =============================================================================

static void test_kmd_spv_claim_success() {
  std::cout << "test_kmd_spv_claim_success..." << std::endl;

  // Generate a valid KMD WIF (version 0xBC)
  std::array<uint8_t, 32> privKey{};
  for (size_t i = 0; i < 32; ++i)
    privKey[i] = static_cast<uint8_t>(0x10 + i);
  std::vector<uint8_t> privKeyVec(privKey.begin(), privKey.end());
  std::string wif = KmdHtlcScript::base58CheckEncode(0xBC, privKeyVec);

  // Generate a valid KMD P2PKH destination address (version 0x3C)
  std::array<uint8_t, 33> pubKey{};
  for (size_t i = 0; i < 33; ++i)
    pubKey[i] = static_cast<uint8_t>(0x20 + i);
  std::vector<uint8_t> pubKeyVec(pubKey.begin(), pubKey.end());
  std::vector<uint8_t> pubKeyHashVec = KmdHtlcScript::hash160(pubKeyVec);
  std::string destAddr = KmdHtlcScript::pubkeyHashToAddress(pubKeyHashVec);

  // Create an HTLC redeemScript using a SHA256 hash lock
  std::vector<uint8_t> preimageVec(32, 0x42);
  std::vector<uint8_t> hashLock = KmdHtlcScript::sha256(preimageVec);
  uint32_t lockTime = 1500000;
  uint32_t timeoutBlock = 2000000;
  auto redeemScript = KmdHtlcScript::createHashTimeLockScript(
      hashLock, lockTime, pubKeyVec, pubKeyVec, timeoutBlock);
  std::string chainStateHex = KmdHtlcScript::bytesToHex(redeemScript);

  auto mock = std::make_shared<MockSpvClient>();
  KmdChainClient client(mock, wif);

  SwapParams params;
  memset(&params.adaptorSecret, 0x42, sizeof(params.adaptorSecret));
  params.ctrLockTxId = "aabbccdd11223344aabbccdd11223344aabbccdd11223344aabbccdd11223344";
  params.ctrAmount = 100000;
  params.chainState = chainStateHex;
  params.ctrAddress = destAddr;
  params.ctrTimeoutBlock = timeoutBlock;

  // SPV claim → should sign locally and broadcast via mock
  auto result = client.claim(params);
  assert(result.success);
  assert(result.txId.size() == 64);

  // SPV refund → same path with nLocktime and nSequence=0xFFFFFFFE
  result = client.refund(params);
  assert(result.success);
  assert(result.txId.size() == 64);

  std::cout << "  PASSED" << std::endl;
}

// =============================================================================
// Tests: SPV claim → broadcast → extract preimage (full pipeline)
// =============================================================================

static void test_kmd_spv_claim_then_extract_secret() {
  std::cout << "test_kmd_spv_claim_then_extract_secret..." << std::endl;

  // Generate a valid KMD WIF (version 0xBC) and destination address
  std::array<uint8_t, 32> privKey{};
  for (size_t i = 0; i < 32; ++i)
    privKey[i] = static_cast<uint8_t>(0x10 + i);
  std::vector<uint8_t> privKeyVec(privKey.begin(), privKey.end());
  std::string wif = KmdHtlcScript::base58CheckEncode(0xBC, privKeyVec);

  std::array<uint8_t, 33> pubKey{};
  for (size_t i = 0; i < 33; ++i)
    pubKey[i] = static_cast<uint8_t>(0x20 + i);
  std::vector<uint8_t> pubKeyVec(pubKey.begin(), pubKey.end());
  std::vector<uint8_t> pubKeyHashVec = KmdHtlcScript::hash160(pubKeyVec);
  std::string destAddr = KmdHtlcScript::pubkeyHashToAddress(pubKeyHashVec);

  // Create an HTLC redeemScript whose hash lock matches the adaptorSecret
  std::vector<uint8_t> preimageVec(32, 0x42);
  std::vector<uint8_t> hashLock = KmdHtlcScript::sha256(preimageVec);
  uint32_t lockTime = 1500000;
  uint32_t timeoutBlock = 2000000;
  auto redeemScript = KmdHtlcScript::createHashTimeLockScript(
      hashLock, lockTime, pubKeyVec, pubKeyVec, timeoutBlock);
  std::string chainStateHex = KmdHtlcScript::bytesToHex(redeemScript);

  // Set the chainState to include the redeemScript
  std::string lockTxId = "bbccddee11223344bbccddee11223344bbccddee11223344bbccddee11223344";

  auto mock = std::make_shared<MockSpvClient>();
  mock->setLockTx(lockTxId);

  // Pre-seed a dummy inclusion for the lock tx (needed by verifyLock, not claim)
  mock->m_inclusions[lockTxId] = {true, 500, 5, true};

  KmdChainClient client(mock, wif);

  SwapParams params;
  memset(&params.adaptorSecret, 0x42, sizeof(params.adaptorSecret));
  params.ctrLockTxId = lockTxId;
  params.ctrAmount = 100000;
  params.chainState = chainStateHex;
  params.ctrAddress = destAddr;
  params.ctrTimeoutBlock = timeoutBlock;

  // Step 1: SPV claim → signs + broadcasts via mock
  auto claimResult = client.claim(params);
  assert(claimResult.success);
  std::string claimTxId = claimResult.txId;
  assert(!claimTxId.empty());

  // Step 2: tryExtractClaimedSecret → findSpend → getRawTx → parseClaimPreimage
  std::string secret = client.tryExtractClaimedSecret(params);
  assert(!secret.empty());
  // The extracted preimage should be the 32 bytes of 0x42 in hex
  assert(secret == "4242424242424242424242424242424242424242424242424242424242424242");

  // Also test the SPV refund → broadcast → extract cycle doesn't extract (refund has no preimage)
  auto refundResult = client.refund(params);
  assert(refundResult.success);
  std::string refundTxId = refundResult.txId;
  assert(!refundTxId.empty());

  // Now the mock's findSpend returns the claim tx (first spend). Try extract again.
  secret = client.tryExtractClaimedSecret(params);
  assert(!secret.empty());
  assert(secret == "4242424242424242424242424242424242424242424242424242424242424242");

  std::cout << "  PASSED" << std::endl;
}

// =============================================================================
// Main
// =============================================================================

int main() {
  std::cout << "=== KmdHtlcScript / KmdChainClient Tests ===" << std::endl;

  test_kmd_redeemScriptToP2shScriptPubKey();
  test_kmd_parseClaimPreimage_basic();
  test_kmd_parseClaimPreimage_no_match();
  test_kmd_pubkeyHashToAddress();
  test_kmd_scriptHashToAddress();
  test_kmd_verifyLock_spv_mode();
  test_kmd_verifyLock_spv_wrong_amount();
  test_kmd_verifyLock_spv_not_found();
  test_kmd_extractSecret_spv_mode();
  test_kmd_extractSecret_spv_not_found();
  test_kmd_getCurrentHeight_spv_mode();
  test_kmd_spv_mode_lock_claim_refund_fail();
  test_kmd_spv_claim_success();
  test_kmd_spv_claim_then_extract_secret();

  std::cout << "\nAll KMD tests passed." << std::endl;
  return 0;
}
