#include <cassert>
#include <iostream>
#include <memory>
#include <string>

#include "SwapDaemon/BSC/BscChainClient.h"
#include "SwapDaemon/ChainClientResult.h"

using namespace XfgSwap;

// =============================================================================
// Tests
// =============================================================================

static void test_chain_name() {
  std::cout << "test_chain_name..." << std::endl;

  auto rpc = std::make_unique<EthRpcClient>("127.0.0.1", 1);
  BscChainClient client(std::move(rpc), "0x0000000000000000000000000000000000000001");

  assert(client.chainName() == "BSC");

  std::cout << "  PASSED" << std::endl;
}

static void test_lock_error_contains_chain_name() {
  std::cout << "test_lock_error_contains_chain_name..." << std::endl;

  auto rpc = std::make_unique<EthRpcClient>("127.0.0.1", 1);
  BscChainClient client(std::move(rpc), "0x0000000000000000000000000000000000000001");

  SwapParams params;
  params.ctrAddress = "0x0000000000000000000000000000000000000002";
  memset(&params.adaptorPoint, 0xAB, sizeof(params.adaptorPoint));
  params.ctrTimeoutBlock = 100000;
  params.ctrAmount = 1000000;

  auto result = client.lock(params);
  assert(!result.success);
  assert(result.error.find("BSC") != std::string::npos);

  std::cout << "  PASSED" << std::endl;
}

static void test_verifyLock_error_contains_chain_name() {
  std::cout << "test_verifyLock_error_contains_chain_name..." << std::endl;

  auto rpc = std::make_unique<EthRpcClient>("127.0.0.1", 1);
  BscChainClient client(std::move(rpc), "0x0000000000000000000000000000000000000001");

  SwapParams params;
  params.ctrLockTxId = "0xdeadbeef";
  params.ctrAmount = 1000000;

  auto result = client.verifyLock(params);
  assert(!result.success);
  assert(result.error.find("BSC") != std::string::npos);

  std::cout << "  PASSED" << std::endl;
}

static void test_claim_error_contains_chain_name() {
  std::cout << "test_claim_error_contains_chain_name..." << std::endl;

  auto rpc = std::make_unique<EthRpcClient>("127.0.0.1", 1);
  BscChainClient client(std::move(rpc), "0x0000000000000000000000000000000000000001");

  SwapParams params;
  params.ctrLockTxId = "0xdeadbeef";
  memset(&params.adaptorSecret, 0xCC, sizeof(params.adaptorSecret));

  auto result = client.claim(params);
  assert(!result.success);
  assert(result.error.find("BSC") != std::string::npos);

  std::cout << "  PASSED" << std::endl;
}

static void test_refund_error_contains_chain_name() {
  std::cout << "test_refund_error_contains_chain_name..." << std::endl;

  auto rpc = std::make_unique<EthRpcClient>("127.0.0.1", 1);
  BscChainClient client(std::move(rpc), "0x0000000000000000000000000000000000000001");

  SwapParams params;
  params.ctrLockTxId = "0xdeadbeef";

  auto result = client.refund(params);
  assert(!result.success);
  assert(result.error.find("BSC") != std::string::npos);

  std::cout << "  PASSED" << std::endl;
}

// =============================================================================
// Main
// =============================================================================

int main() {
  std::cout << "=== BscChainClient Tests ===" << std::endl;

  test_chain_name();
  test_lock_error_contains_chain_name();
  test_verifyLock_error_contains_chain_name();
  test_claim_error_contains_chain_name();
  test_refund_error_contains_chain_name();

  std::cout << "\nAll tests passed." << std::endl;
  return 0;
}
