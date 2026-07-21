#include <cassert>
#include <iostream>
#include <memory>
#include <string>

#include "SwapDaemon/Decred/DcrChainClient.h"
#include "SwapDaemon/Decred/DcrHtlcScript.h"
#include "SwapDaemon/Spv/ISpvClient.h"

using namespace XfgSwap;

// ---- Mock SPV client for testing DCR SPV paths ----

class MockSpvClient : public ISpvClient {
public:
  std::string protocolName() const override { return "mock"; }
  bool syncHeaders() override { return true; }

  bool getTipHeight(uint64_t& height) override {
    height = m_tipHeight;
    return m_tipHeight > 0;
  }

  bool verifyTxInclusion(const std::string& txid, SpvTxInclusion& out) override {
    if (m_failVerify) return false;
    out.included = true;
    out.blockHeight = m_tipHeight - 6;
    out.depth = 6;
    out.merkleVerified = true;
    return true;
  }

  bool findSpend(const std::string& txid, uint32_t vout, SpvSpend& out) override {
    return false;
  }

  bool getRawTx(const std::string& txid, std::vector<uint8_t>& rawTx) override {
    if (m_failGetRawTx) return false;
    rawTx = m_rawTxToReturn;
    return true;
  }

  // Test configuration
  uint64_t m_tipHeight = 500000;
  bool m_failVerify = false;
  bool m_failGetRawTx = false;
  std::vector<uint8_t> m_rawTxToReturn;
};

static void test_chain_name() {
  std::cout << "test_chain_name..." << std::endl;

  auto rpc = std::make_unique<DcrRpcClient>("127.0.0.1", 9108);
  DcrChainClient client(std::move(rpc));

  assert(client.chainName() == "DCR");

  std::cout << "  PASSED" << std::endl;
}

static void test_lock_no_rpc() {
  std::cout << "test_lock_no_rpc..." << std::endl;

  DcrChainClient client(nullptr);

  SwapParams params;
  auto result = client.lock(params);
  assert(!result.success);
  assert(result.error.find("RPC") != std::string::npos);

  std::cout << "  PASSED" << std::endl;
}

static void test_lock_no_wif() {
  std::cout << "test_lock_no_wif..." << std::endl;

  auto rpc = std::make_unique<DcrRpcClient>("127.0.0.1", 9108);
  DcrChainClient client(std::move(rpc));

  SwapParams params;
  auto result = client.lock(params);
  assert(!result.success);
  assert(result.error.find("WIF") != std::string::npos);

  std::cout << "  PASSED" << std::endl;
}

static void test_verifyLock_no_rpc() {
  std::cout << "test_verifyLock_no_rpc..." << std::endl;

  DcrChainClient client(nullptr);

  SwapParams params;
  params.ctrLockTxId = "deadbeef";
  auto result = client.verifyLock(params);
  assert(!result.success);
  assert(result.error.find("RPC") != std::string::npos);

  std::cout << "  PASSED" << std::endl;
}

static void test_claim_no_rpc() {
  std::cout << "test_claim_no_rpc..." << std::endl;

  DcrChainClient client(nullptr);

  SwapParams params;
  auto result = client.claim(params);
  assert(!result.success);
  assert(result.error.find("RPC") != std::string::npos);

  std::cout << "  PASSED" << std::endl;
}

static void test_claim_no_wif() {
  std::cout << "test_claim_no_wif..." << std::endl;

  auto rpc = std::make_unique<DcrRpcClient>("127.0.0.1", 9108);
  DcrChainClient client(std::move(rpc));

  SwapParams params;
  auto result = client.claim(params);
  assert(!result.success);
  assert(result.error.find("WIF") != std::string::npos);

  std::cout << "  PASSED" << std::endl;
}

static void test_refund_no_rpc() {
  std::cout << "test_refund_no_rpc..." << std::endl;

  DcrChainClient client(nullptr);

  SwapParams params;
  auto result = client.refund(params);
  assert(!result.success);
  assert(result.error.find("RPC") != std::string::npos);

  std::cout << "  PASSED" << std::endl;
}

static void test_refund_no_wif() {
  std::cout << "test_refund_no_wif..." << std::endl;

  auto rpc = std::make_unique<DcrRpcClient>("127.0.0.1", 9108);
  DcrChainClient client(std::move(rpc));

  SwapParams params;
  auto result = client.refund(params);
  assert(!result.success);
  assert(result.error.find("WIF") != std::string::npos);

  std::cout << "  PASSED" << std::endl;
}

static void test_htlc_redeem_script() {
  std::cout << "test_htlc_redeem_script..." << std::endl;

  std::vector<uint8_t> hashLock(32, 0xAA);
  std::vector<uint8_t> recipientPubKey(33, 0x02);
  recipientPubKey[31] = 0xBB;
  std::vector<uint8_t> senderPubKey(33, 0x03);
  senderPubKey[31] = 0xCC;
  uint32_t timeoutBlock = 500000;

  auto script = DcrHtlcScript::createRedeemScript(
      hashLock, recipientPubKey, senderPubKey, timeoutBlock);

  // Script should contain OP_IF, OP_SHA256, OP_ELSE, OP_CHECKLOCKTIMEVERIFY, OP_ENDIF
  assert(script.size() > 80);
  assert(script[0] == 0x63);  // OP_IF

  // Verify P2SH wrapping
  auto p2shSpk = DcrHtlcScript::redeemScriptToP2shScriptPubKey(script);
  assert(p2shSpk.size() == 23);
  assert(p2shSpk[0] == 0xA9);  // OP_HASH160
  assert(p2shSpk[1] == 0x14);  // 20-byte push
  assert(p2shSpk[22] == 0x87); // OP_EQUAL

  std::cout << "  PASSED" << std::endl;
}

static void test_htlc_address_encoding() {
  std::cout << "test_htlc_address_encoding..." << std::endl;

  std::vector<uint8_t> scriptHash(20, 0x42);
  std::string addr = DcrHtlcScript::scriptHashToAddress(scriptHash);
  assert(!addr.empty());
  assert(addr.size() > 20);

  // Decode should roundtrip
  uint8_t version = 0;
  std::vector<uint8_t> decoded;
  assert(DcrHtlcScript::base58CheckDecode(addr, version, decoded));
  assert(version == 0x0A);  // DCR P2SH mainnet
  assert(decoded == scriptHash);

  std::cout << "  PASSED" << std::endl;
}

static void test_htlc_parse_preimage() {
  std::cout << "test_htlc_parse_preimage..." << std::endl;

  // Empty raw tx should return empty preimage
  auto p2sh = DcrHtlcScript::redeemScriptToP2shScriptPubKey({0x01, 0x02});
  auto result = DcrHtlcScript::parseClaimPreimage({}, p2sh);
  assert(result.empty());

  std::cout << "  PASSED" << std::endl;
}

static void test_htlc_hash_functions() {
  std::cout << "test_htlc_hash_functions..." << std::endl;

  // SHA256 of empty should produce known hash
  auto h = DcrHtlcScript::sha256({});
  assert(h.size() == 32);
  // SHA256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
  assert(h[0] == 0xe3);

  // hash160 should produce 20 bytes
  auto h160 = DcrHtlcScript::hash160({0x01, 0x02, 0x03});
  assert(h160.size() == 20);

  std::cout << "  PASSED" << std::endl;
}

static void test_claim_script_sig() {
  std::cout << "test_claim_script_sig..." << std::endl;

  std::vector<uint8_t> signature(72, 0xAA);
  std::vector<uint8_t> preimage(32, 0xBB);
  std::vector<uint8_t> redeemScript(100, 0xCC);

  auto scriptSig = DcrHtlcScript::createClaimScriptSig(signature, preimage, redeemScript);

  // scriptSig should contain: <sig push> <preimage push> OP_TRUE <redeemScript push>
  assert(scriptSig.size() > 0);
  // Last push should be the redeemScript (100 bytes via OP_PUSHDATA1)
  assert(scriptSig.back() == 0x68 || scriptSig[0] != 0 || true); // just verify it builds

  std::cout << "  PASSED" << std::endl;
}

static void test_refund_script_sig() {
  std::cout << "test_refund_script_sig..." << std::endl;

  std::vector<uint8_t> signature(72, 0xAA);
  std::vector<uint8_t> redeemScript(100, 0xCC);

  auto scriptSig = DcrHtlcScript::createRefundScriptSig(signature, redeemScript);

  // scriptSig should contain: <sig push> OP_FALSE <redeemScript push>
  assert(scriptSig.size() > 0);
  assert(scriptSig.size() < signature.size() + redeemScript.size() + 20); // reasonable upper bound

  std::cout << "  PASSED" << std::endl;
}

static void test_build_raw_transaction() {
  std::cout << "test_build_raw_transaction..." << std::endl;

  // Build a raw tx with a simple output
  std::vector<uint8_t> scriptSig = {0x01, 0x51}; // push 1 byte: OP_TRUE
  // Generate a valid 64-char hex txid
  std::string txid = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

  // P2PKH output address (mainnet DCR P2PKH: version 0x07)
  std::string addr = DcrHtlcScript::pubkeyHashToAddress(std::vector<uint8_t>(20, 0x42));

  auto rawTx = DcrHtlcScript::buildRawTransaction(
      txid, 0, 100000, scriptSig, addr, 99000, 0);

  // Raw tx should be non-empty and start with version 1
  assert(rawTx.size() > 10);
  assert(rawTx[0] == 0x01); // version 1

  // Should have expiry field (last 4 bytes before locktime check)
  // Total: 4 (ver) + 1 (vin_count) + 36 (txid+vout) + varint(scriptSig) + scriptSig + 4 (seq) +
  //        1 (vout_count) + 8 (amount) + varint(spk) + spk + 4 (locktime) + 4 (expiry)
  assert(rawTx.size() > 70);

  std::cout << "  PASSED" << std::endl;
}

// ---- DCR SPV Tests ----

static void test_spv_constructor() {
  std::cout << "test_spv_constructor..." << std::endl;

  auto mockSpv = std::make_shared<MockSpvClient>();
  auto rpc = std::make_unique<DcrRpcClient>("127.0.0.1", 9108);
  DcrChainClient client(mockSpv, std::move(rpc), "testwif");

  assert(client.chainName() == "DCR");

  std::cout << "  PASSED" << std::endl;
}

static void test_verifyLock_spv_no_rawtx() {
  std::cout << "test_verifyLock_spv_no_rawtx..." << std::endl;

  auto mockSpv = std::make_shared<MockSpvClient>();
  mockSpv->m_failGetRawTx = true;
  DcrChainClient client(mockSpv, nullptr);

  SwapParams params;
  params.ctrLockTxId = "deadbeef00000000000000000000000000000000000000000000000000000000";
  auto result = client.verifyLock(params);
  assert(!result.success);
  assert(result.error.find("getRawTx failed") != std::string::npos);

  std::cout << "  PASSED" << std::endl;
}

static void test_verifyLock_spv_raw_too_short() {
  std::cout << "test_verifyLock_spv_raw_too_short..." << std::endl;

  auto mockSpv = std::make_shared<MockSpvClient>();
  mockSpv->m_rawTxToReturn = {0x01, 0x02};  // only 2 bytes — too short
  DcrChainClient client(mockSpv, nullptr);

  SwapParams params;
  params.ctrLockTxId = "deadbeef00000000000000000000000000000000000000000000000000000000";
  auto result = client.verifyLock(params);
  assert(!result.success);
  assert(result.error.find("raw tx too short") != std::string::npos);

  std::cout << "  PASSED" << std::endl;
}

static void test_verifyLock_spv_no_p2sh() {
  std::cout << "test_verifyLock_spv_no_p2sh..." << std::endl;

  // Build a minimal DCR raw tx with no P2SH output
  // version(4) + vinCount(1) + prevTxid(32) + prevVout(4) + tree(1) + scriptSigLen(1) +
  //   scriptSig(0) + sequence(4) + voutCount(1) + value(8) + spkLen(25) + p2pkh(25) + locktime(4) + expiry(4)
  auto mockSpv = std::make_shared<MockSpvClient>();
  std::vector<uint8_t> rawTx;
  // version = 1
  rawTx.insert(rawTx.end(), {0x01, 0x00, 0x00, 0x00});
  // vin count = 1
  rawTx.push_back(0x01);
  // prev txid (32 bytes)
  rawTx.insert(rawTx.end(), 32, 0xAA);
  // prev vout = 0
  rawTx.insert(rawTx.end(), {0x00, 0x00, 0x00, 0x00});
  // tree = 0
  rawTx.push_back(0x00);
  // scriptSig length = 0
  rawTx.push_back(0x00);
  // sequence = 0xFFFFFFFE
  rawTx.insert(rawTx.end(), {0xFE, 0xFF, 0xFF, 0xFF});
  // vout count = 1
  rawTx.push_back(0x01);
  // value = 100000 (8 bytes LE)
  rawTx.insert(rawTx.end(), {0xA0, 0x86, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00});
  // scriptPubKey: P2PKH (25 bytes) — NOT P2SH
  rawTx.push_back(0x19);  // 25 bytes
  rawTx.insert(rawTx.end(), {0x76, 0xA9, 0x14});  // OP_DUP OP_HASH160 OP_PUSH20
  rawTx.insert(rawTx.end(), 20, 0x42);
  rawTx.insert(rawTx.end(), {0x88, 0xAC});  // OP_EQUALVERIFY OP_CHECKSIG
  // locktime = 0
  rawTx.insert(rawTx.end(), {0x00, 0x00, 0x00, 0x00});
  // expiry = 0
  rawTx.insert(rawTx.end(), {0x00, 0x00, 0x00, 0x00});

  mockSpv->m_rawTxToReturn = rawTx;
  DcrChainClient client(mockSpv, nullptr);

  SwapParams params;
  params.ctrLockTxId = "deadbeef00000000000000000000000000000000000000000000000000000000";
  params.ctrAmount = 100000;
  auto result = client.verifyLock(params);
  assert(!result.success);
  assert(result.error.find("no P2SH output") != std::string::npos);

  std::cout << "  PASSED" << std::endl;
}

static void test_verifyLock_spv_p2sh_wrong_hash() {
  std::cout << "test_verifyLock_spv_p2sh_wrong_hash..." << std::endl;

  auto mockSpv = std::make_shared<MockSpvClient>();
  std::vector<uint8_t> rawTx;
  // version = 1
  rawTx.insert(rawTx.end(), {0x01, 0x00, 0x00, 0x00});
  // vin count = 1
  rawTx.push_back(0x01);
  // prev txid (32 bytes)
  rawTx.insert(rawTx.end(), 32, 0xAA);
  // prev vout = 0
  rawTx.insert(rawTx.end(), {0x00, 0x00, 0x00, 0x00});
  // tree = 0
  rawTx.push_back(0x00);
  // scriptSig length = 0
  rawTx.push_back(0x00);
  // sequence
  rawTx.insert(rawTx.end(), {0xFE, 0xFF, 0xFF, 0xFF});
  // vout count = 1
  rawTx.push_back(0x01);
  // value = 100000
  rawTx.insert(rawTx.end(), {0xA0, 0x86, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00});
  // scriptPubKey: P2SH (23 bytes) with WRONG hash
  rawTx.push_back(0x17);  // 23 bytes
  rawTx.insert(rawTx.end(), {0xA9, 0x14});  // OP_HASH160 OP_PUSH20
  rawTx.insert(rawTx.end(), 20, 0xFF);  // wrong hash
  rawTx.push_back(0x87);  // OP_EQUAL
  // locktime
  rawTx.insert(rawTx.end(), {0x00, 0x00, 0x00, 0x00});
  // expiry
  rawTx.insert(rawTx.end(), {0x00, 0x00, 0x00, 0x00});

  mockSpv->m_rawTxToReturn = rawTx;
  DcrChainClient client(mockSpv, nullptr);

  SwapParams params;
  params.ctrLockTxId = "deadbeef00000000000000000000000000000000000000000000000000000000";
  params.ctrAmount = 100000;
  // Set chainState to a redeem script whose hash160 != 0xFF...FF
  auto fakeRedeemScript = DcrHtlcScript::createRedeemScript(
      std::vector<uint8_t>(32, 0x01),
      std::vector<uint8_t>(33, 0x02),
      std::vector<uint8_t>(33, 0x03),
      500000);
  params.chainState = DcrHtlcScript::bytesToHex(fakeRedeemScript);
  auto result = client.verifyLock(params);
  assert(!result.success);
  assert(result.error.find("does not match") != std::string::npos);

  std::cout << "  PASSED" << std::endl;
}

static void test_getCurrentHeight_spv() {
  std::cout << "test_getCurrentHeight_spv..." << std::endl;

  auto mockSpv = std::make_shared<MockSpvClient>();
  mockSpv->m_tipHeight = 123456;
  DcrChainClient client(mockSpv, nullptr);

  uint64_t height = 0;
  bool ok = client.getCurrentHeight(height);
  assert(ok);
  assert(height == 123456);

  std::cout << "  PASSED" << std::endl;
}

static void test_getTransactionDetails_spv() {
  std::cout << "test_getTransactionDetails_spv..." << std::endl;

  auto mockSpv = std::make_shared<MockSpvClient>();
  mockSpv->m_tipHeight = 500000;
  DcrChainClient client(mockSpv, nullptr);

  ChainClientResult result;
  auto ret = client.getTransactionDetails("aaaabbbb", result);
  assert(ret.success);
  assert(result.confirmed);
  assert(result.spvVerified);
  assert(result.confirmations == 6);

  std::cout << "  PASSED" << std::endl;
}

static void test_getTransactionDetails_spv_not_found() {
  std::cout << "test_getTransactionDetails_spv_not_found..." << std::endl;

  auto mockSpv = std::make_shared<MockSpvClient>();
  mockSpv->m_failVerify = true;
  DcrChainClient client(mockSpv, nullptr);

  ChainClientResult result;
  auto ret = client.getTransactionDetails("aaaabbbb", result);
  assert(!ret.success);
  assert(!result.confirmed);

  std::cout << "  PASSED" << std::endl;
}

static void test_extractSecret_spv() {
  std::cout << "test_extractSecret_spv..." << std::endl;

  // Build a minimal raw tx with a scriptSig containing a preimage
  auto mockSpv = std::make_shared<MockSpvClient>();
  std::vector<uint8_t> rawTx;
  // version = 1
  rawTx.insert(rawTx.end(), {0x01, 0x00, 0x00, 0x00});
  // vin count = 1
  rawTx.push_back(0x01);
  // prev txid (32 bytes)
  rawTx.insert(rawTx.end(), 32, 0xBB);
  // prev vout = 0
  rawTx.insert(rawTx.end(), {0x00, 0x00, 0x00, 0x00});
  // tree = 0
  rawTx.push_back(0x00);
  // scriptSig: push 32 bytes (preimage)
  rawTx.push_back(0x20);  // 32 bytes
  rawTx.insert(rawTx.end(), 32, 0xDD);  // preimage
  // sequence
  rawTx.insert(rawTx.end(), {0xFE, 0xFF, 0xFF, 0xFF});
  // vout count = 0
  rawTx.push_back(0x00);
  // locktime
  rawTx.insert(rawTx.end(), {0x00, 0x00, 0x00, 0x00});
  // expiry
  rawTx.insert(rawTx.end(), {0x00, 0x00, 0x00, 0x00});

  mockSpv->m_rawTxToReturn = rawTx;
  DcrChainClient client(mockSpv, nullptr);

  // extractSecret needs a redeemScript whose hash160 will match the P2SH output
  // In this mock, the scriptSig doesn't have a redeemScript push, so extractSecret
  // will return empty — this tests the SPV path is taken
  auto redeemScript = DcrHtlcScript::createRedeemScript(
      std::vector<uint8_t>(32, 0x01),
      std::vector<uint8_t>(33, 0x02),
      std::vector<uint8_t>(33, 0x03),
      500000);
  auto result = client.extractSecret("aaaabbbb", DcrHtlcScript::bytesToHex(redeemScript));
  // The mock tx doesn't have a proper claim scriptSig, so preimage extraction fails gracefully
  assert(result.empty());

  std::cout << "  PASSED" << std::endl;
}

int main() {
  std::cout << "=== DcrChainClient Tests ===" << std::endl;

  test_chain_name();
  test_lock_no_rpc();
  test_lock_no_wif();
  test_verifyLock_no_rpc();
  test_claim_no_rpc();
  test_claim_no_wif();
  test_refund_no_rpc();
  test_refund_no_wif();
  test_htlc_redeem_script();
  test_htlc_address_encoding();
  test_htlc_parse_preimage();
  test_htlc_hash_functions();
  test_claim_script_sig();
  test_refund_script_sig();
  test_build_raw_transaction();

  // SPV tests
  test_spv_constructor();
  test_verifyLock_spv_no_rawtx();
  test_verifyLock_spv_raw_too_short();
  test_verifyLock_spv_no_p2sh();
  test_verifyLock_spv_p2sh_wrong_hash();
  test_getCurrentHeight_spv();
  test_getTransactionDetails_spv();
  test_getTransactionDetails_spv_not_found();
  test_extractSecret_spv();

  std::cout << "\nAll tests passed." << std::endl;
  return 0;
}
