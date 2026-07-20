#include <cassert>
#include <iostream>
#include <memory>
#include <string>

#include "SwapDaemon/Decred/DcrChainClient.h"
#include "SwapDaemon/Decred/DcrHtlcScript.h"

using namespace XfgSwap;

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

  std::cout << "  PASSED" << std::endl;
}

static void test_refund_no_rpc() {
  std::cout << "test_refund_no_rpc..." << std::endl;

  DcrChainClient client(nullptr);

  SwapParams params;
  auto result = client.refund(params);
  assert(!result.success);

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

int main() {
  std::cout << "=== DcrChainClient Tests ===" << std::endl;

  test_chain_name();
  test_lock_no_rpc();
  test_lock_no_wif();
  test_verifyLock_no_rpc();
  test_claim_no_rpc();
  test_refund_no_rpc();
  test_htlc_redeem_script();
  test_htlc_address_encoding();
  test_htlc_parse_preimage();
  test_htlc_hash_functions();

  std::cout << "\nAll tests passed." << std::endl;
  return 0;
}
